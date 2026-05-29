// ---------------------------------------------------------------
// WAVE SENTINEL — SubGHz / BLE / WiFi multi-tool
//
// Architecture:
//   Core 0: LVGL display refresh (Task_Refresh_Screen) — all
//           touch callbacks and event handlers run here.
//   Core 1: Main loop() state machine — RF capture/playback,
//           scanning, BLE spam, WiFi scan, etc.
//
// LVGL is NOT thread-safe. Any LVGL call from Core 1's loop()
// MUST be wrapped with xSemaphoreTake/Give(lvgl_mutex).
// Event handlers on Core 0 are already inside the mutex.
//
// Hardware: WT32-SC01-PLUS (ESP32-S3 + ST7796 LCD + FT5x06 touch)
//           + CC1101 RF module on default SPI (FSPI)
//           + SD card on HSPI (separate SPI bus)
//           + I2S audio output
// ---------------------------------------------------------------

#include "Misc/Config.h"
#include "Display/Display.h"
#include "SubGhz/SubGhz.h"
#include "WiFi/WiFix.h"
#include "BLE/BLE.h"
#include "WiFi/WiFiMarauder.h"
#include "SD/SDCard.h"
#include "Display/TouchTunesScreen.h"
#include "Display/RemoteScreen.h"
#include "Display/StatusBar.h"
#include "Display/MarauderBleScreen.h"
#include "IR/IRTransmit.h"
#include "WaveKai/WaveKaiClient.h"
#include "WaveKai/WaveKaiScreen.h"
#include "API/LocalAPI.h"
#include "Display/MarauderScreen.h"
#include "Display/SpaceInvadersScreen.h"
#include "Audio/ToneService.h"
#include "Display/SettingsScreen.h"
#include "Display/FlipperPlayerScreen.h"


// Scanner screen initialized from SubGhz.cpp

// WaveKai API client instance
WaveKaiClient waveKai;

// Local REST API server
LocalAPIServer localAPI;
#include "IR/FlipperIRFile.h"

#include "Arduino.h"
// MP3 playback removed; ToneService owns I2S directly
#include "esp_bt.h"
#include <Preferences.h>




// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DECLARE
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// SubGhz Class
SubGhz SUBGHZ;

// C-style trampolines that expose SubGhz::startRawCapture/stopRawCapture
// to the FlipperPlayerScreen.h header (kept .h-only / template-free).
extern "C" bool fp_subghz_raw_start(float freq_mhz, const char *filename) {
  return SUBGHZ.startRawCapture(freq_mhz, filename);
}
extern "C" bool fp_subghz_raw_start_preset(float freq_mhz, const char *filename, int preset) {
  return SUBGHZ.startRawCapture(freq_mhz, filename, (CC1101Preset)preset);
}
extern "C" const char *fp_subghz_preset_name(int preset) {
  return SUBGHZ.presetName((CC1101Preset)preset);
}
extern "C" void fp_subghz_raw_stop()              { SUBGHZ.stopRawCapture(); }
extern "C" int  fp_subghz_raw_count()             { return SUBGHZ.rawCaptureCount(); }
extern "C" bool fp_subghz_raw_running()           { return SUBGHZ.rawCaptureRunning(); }
extern "C" uint32_t fp_subghz_raw_last_ms()       { return SUBGHZ.rawCaptureLastTransitionMs(); }

// Status label on the new CHAOS tab (CC1101 Tools → CHAOS). Populated
// once in setup() via the Chaos tab builder; the Tesla state machine
// writes to it if non-NULL, otherwise falls back to ui_lblPresetsStatus.
static lv_obj_t *chaos_lblStatus = NULL;
static inline void chaos_status_set(const char *txt) {
  lv_obj_t *target = chaos_lblStatus ? chaos_lblStatus : ui_lblPresetsStatus;
  if (target) lv_label_set_text(target, txt);
}

// ---------------------------------------------------------------
// Rejoin the saved WiFi network. Some tool screens (Marauder, BLE
// spam, etc.) put the radio into WIFI_OFF / AP / monitor mode, which
// drops the saved STA connection. Call this whenever we return to
// the main menu so the WaveKai API + OTA stay reachable. No-op if
// already connected.
// ---------------------------------------------------------------
static void wifi_restore_persisted() {
  if (WiFi.status() == WL_CONNECTED) return;
  Preferences p;
  if (!p.begin("wifi", true)) return;
  String ssid = p.getString("ssid", "");
  String pass = p.getString("pass", "");
  p.end();
  if (ssid.length() == 0) return;
  Serial.printf("[WiFi] Restoring saved network '%s' after tool exit\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

// ---------------------------------------------------------------
// Walk the LVGL screen tree and replace every dropdown's chevron
// symbol with NULL. The bundled Montserrat fonts in this build
// don't ship the symbol glyph used by lv_dropdown, which paints
// as a small rectangle. NULL hides the symbol entirely.
// ---------------------------------------------------------------
static void purge_dropdown_symbols(lv_obj_t *root) {
  if (!root) return;
  if (lv_obj_check_type(root, &lv_dropdown_class)) {
    lv_dropdown_set_symbol(root, NULL);
  }
  uint32_t n = lv_obj_get_child_cnt(root);
  for (uint32_t i = 0; i < n; i++) {
    purge_dropdown_symbols(lv_obj_get_child(root, i));
  }
}
static void purge_all_dropdown_symbols() {
  // Sweep every screen LVGL knows about. lv_disp_get_scr_act + the
  // global screen objects from ui.h cover everything ui_init builds.
  extern lv_obj_t *ui_scrSplash, *ui_scrMain, *ui_scrProtAna, *ui_scrPresets,
                  *ui_scrSettings, *ui_scrWifiApps, *ui_scrRCSWMain,
                  *ui_scrCC1101Stuff, *ui_scrBLEApps;
  lv_obj_t *roots[] = {
    ui_scrSplash, ui_scrMain, ui_scrProtAna, ui_scrPresets, ui_scrSettings,
    ui_scrWifiApps, ui_scrRCSWMain, ui_scrCC1101Stuff, ui_scrBLEApps,
  };
  for (lv_obj_t *r : roots) purge_dropdown_symbols(r);
}

// Hand-coded sub-menu screens that mirror the CC1101 sub-menu pattern:
//   Main → WIFI → ui_scrWiFiMenu  → [WiFi Tools]  + [WiFi Marauder Tools]
//   Main → BLE  → ui_scrBLEMenu   → [BLE Tools]   + [BLE Marauder Tools]
lv_obj_t *ui_scrWiFiMenu = NULL;
lv_obj_t *ui_scrBLEMenu  = NULL;

// WiFi scan timeout tracking
unsigned long scanStartTime = 0;
const unsigned long WIFI_SCAN_TIMEOUT_MS = 15000; // 15 seconds

// BLE scan duration (set by event handler, used by state machine)
int bleScanDuration = 5;

// Marauder BLE � spam stats (read by MarauderBleScreen, written by dispatcher)
int bleMarSpamCount = 0;
int bleMarSpamRotState = 0;
static uint32_t bleMarLastSpamMs = 0;
static uint32_t bleMarLastRefreshMs = 0;

// Audio I2S Definitions
// Audio audio; removed — ToneService owns I2S

// Preferences Library
Preferences prefs;

// WiFi Join — keyboard overlay state
static lv_obj_t *wifiJoinPanel = NULL;
static lv_obj_t *wifiJoinTextarea = NULL;
static lv_obj_t *wifiJoinKeyboard = NULL;
static char wifiJoinSSID[33] = {0};
unsigned long wifiConnectStartTime = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;




// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void Task_Refresh_Screen(void *parameter)
// ---------------------------------------------------------------------
void Task_Refresh_Screen(void *parameter)
{
  while (true)
  {
    if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE) {
      lv_timer_handler();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  vTaskDelete(NULL);
}

// ---------------------------------------------------------------------
// void setup()
// ---------------------------------------------------------------------
void setup()
{
  // Release Classic BT memory early — frees ~30KB internal SRAM before
  // any heap allocations fragment it. Needed for BLE controller init later.
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

  Print_Debug("Initializing Stuff...");

  // Install I2S BEFORE the LCD so we don't steal LCD's GDMA channel.
  // The prior Audio library worked because `Audio audio;` static init
  // ran before setup() and grabbed channel 0 first. We mirror that
  // ordering by initializing the tone service as the very first thing.
  tone_service_init();

  /* this callback function will be invoked when starting */
  ArduinoOTA.onStart([]()
                     { lv_label_set_text(ui_lblSettingsStatus, "UPDATE STARTED"); });

  /* this callback function will be invoked when updating end */
  ArduinoOTA.onEnd([]()
                   {
    OTAInProgress=0;
    lv_label_set_text(ui_lblSettingsStatus,"COMPLETE - RESTARTING");
    delay(5000);  
    ESP.restart(); });

  /* this callback function will be invoked when updating error */
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Auth Failed");
    else if (error == OTA_BEGIN_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Begin Failed");
    else if (error == OTA_CONNECT_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) lv_label_set_text(ui_lblSettingsStatus,"Receive Failed");
    else if (error == OTA_END_ERROR) lv_label_set_text(ui_lblSettingsStatus,"End Failed"); });
  /* this callback function will be invoked when a number of chunks of software was flashed
    so we can use it to calculate the progress of flashing */
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    char updBuf[32];
    snprintf(updBuf, sizeof(updBuf), "Progress: %u", progress / (total / 100));
    lv_label_set_text(ui_lblSettingsStatus, updBuf);
    lv_bar_set_value(ui_barProgress,progress / (total / 100),LV_ANIM_ON); });

  // Start The Serial Debug Port
  Serial.begin(115200); /* prepare for possible serial debug */

  Print_Debug("Initializing Display...");

  Init_Display();

  // Pump LVGL so the splash renders to LCD as soon as the framebuffer
  // is ready — without this, setup() builds 30+ screens before the
  // refresh task starts and the splash only partially appears on first
  // boot. A few hundred millisecond's worth of pumping covers the
  // splash + version text and keeps the LCD live during long inits.
  auto pump_lvgl = []() {
    for (int i = 0; i < 5; i++) {
      lv_timer_handler();
      delay(5);
    }
  };

  Print_Debug("Initializing Default Value...");

  // Show version on splash with black background for readability
  char splashInfo[64];
  snprintf(splashInfo, sizeof(splashInfo), "v%d.%d.%d",
           APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);
  lv_label_set_text(ui_lblVersion, splashInfo);
  lv_obj_set_style_bg_color(ui_lblVersion, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblVersion, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblVersion, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Black background on splash labels for readability over image
  lv_obj_set_style_bg_color(ui_lblSplashStatus, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblSplashStatus, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblSplashStatus, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_lblSplash, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblSplash, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblSplash, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_label_set_text(ui_lblSplashStatus, "TAP ANYWHERE TO BEGIN");
  pump_lvgl();   // first paint of splash + version text

  Print_Debug("Initializing CC1101...");

  if (SUBGHZ.init())
  {
    lv_label_set_text(ui_lblSplash, "CC1101: Init Success");
    Print_Debug("CC1101 successfully initialized.");
  }
  else
  {
    lv_label_set_text(ui_lblSplash, "CC1101: Init Fail");
    Print_Debug("CC1101 not initialized.");
  }
  pump_lvgl();  // push the CC1101 status to the splash before the next long init

  // Build dynamic TouchTunes remote screen (no SquareLine license needed)
  tt_screen_init();

  // Wipe SquareLine widgets in Scanner tab and build hand-coded UI
  SUBGHZ.initScannerScreen();

  // Tone service was already initialized at the top of setup() — just play the chime now
  tone_play(&TONE_BOOT);

  // Build dynamic Universal Remote screen
  remote_screen_init();

  // Build dynamic Marauder BLE screen
  mbs_screen_init();

  // ============================================================
  // WiFi sub-menu screen — Main → WIFI → here → [WiFi Tools | WiFi Marauder]
  // ============================================================
  auto buildSubMenu = [](lv_obj_t **outScreen,
                         const char *title,
                         const char *aLabel, const char *aDesc, uint32_t aBorder,
                         lv_event_cb_t aCb,
                         const char *bLabel, const char *bDesc, uint32_t bBorder,
                         lv_event_cb_t bCb) {
    *outScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(*outScreen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*outScreen, 255, LV_PART_MAIN);
    lv_obj_clear_flag(*outScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(*outScreen);
    lv_obj_set_align(t, LV_ALIGN_TOP_MID);
    lv_obj_set_y(t, 15);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(t, &ui_font_Verdana18, LV_PART_MAIN);

    auto richBtn = [&](int y, const char *lbl, const char *desc, uint32_t border, lv_event_cb_t cb) {
      lv_obj_t *b = lv_btn_create(*outScreen);
      lv_obj_set_pos(b, 15, y);
      lv_obj_set_size(b, 290, 65);
      lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A3E), LV_PART_MAIN);
      lv_obj_set_style_radius(b, 10, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
      lv_obj_set_style_border_color(b, lv_color_hex(border), LV_PART_MAIN);
      lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
      lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
      lv_obj_t *l = lv_label_create(b);
      lv_label_set_text(l, lbl);
      lv_obj_set_align(l, LV_ALIGN_LEFT_MID);
      lv_obj_set_x(l, 5);
      lv_obj_set_y(l, -8);
      lv_obj_set_style_text_color(l, lv_color_hex(border), LV_PART_MAIN);
      lv_obj_set_style_text_font(l, &ui_font_Verdana16, LV_PART_MAIN);
      lv_obj_t *d = lv_label_create(b);
      lv_label_set_text(d, desc);
      lv_obj_set_align(d, LV_ALIGN_LEFT_MID);
      lv_obj_set_x(d, 5);
      lv_obj_set_y(d, 12);
      lv_obj_set_style_text_color(d, lv_color_hex(0x888888), LV_PART_MAIN);
      lv_obj_set_style_text_font(d, &ui_font_Verdana14, LV_PART_MAIN);
    };

    richBtn(60,  aLabel, aDesc, aBorder, aCb);
    richBtn(140, bLabel, bDesc, bBorder, bCb);

    // Back button — match CC1101 menu style
    lv_obj_t *bb = lv_btn_create(*outScreen);
    lv_obj_set_pos(bb, 15, 430);
    lv_obj_set_size(bb, 100, 35);
    lv_obj_set_style_bg_color(bb, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(bb, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(bb, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(bb, [](lv_event_t *e) {
      lv_scr_load(ui_scrMain);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(bb);
    lv_label_set_text(bl, "Back");
    lv_obj_center(bl);
    lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, &ui_font_Verdana14, LV_PART_MAIN);
  };

  buildSubMenu(&ui_scrWiFiMenu, "WIFI TOOLS",
               "WiFi Tools",          "Scan / Join / Beacon / Deauth / Sniff", 0x00AFFF,
               [](lv_event_t *e) { lv_scr_load(ui_scrWifiApps); },
               "WiFi Mantis Tools", "AP/STA scan, PMKID, Karma, SAE, Portal", 0xFF9100,
               [](lv_event_t *e) { marauder_screen_load(); });

  buildSubMenu(&ui_scrBLEMenu, "BLE TOOLS",
               "BLE Tools",           "Scan, Spam, NimBLE",                    0x00AFFF,
               [](lv_event_t *e) { lv_scr_load(ui_scrBLEApps); },
               "BLE Mantis Tools",  "AirTag, Skimmer, Flock, Spam+",         0xFF9100,
               [](lv_event_t *e) {
                 if (currentState == STATE_SEND_BLESPAM) { BLEstop(); }
                 if (currentState == STATE_BLE_SCAN_RUN || currentState == STATE_BLE_SCAN_INIT) BLEscanStop();
                 currentState = STATE_IDLE;
                 mbs_open();
               });

  // Initialize IR transmitter on GPIO 21
  IR_TX.init();

  // Wire the TouchTunes button (not wired in SquareLine)
  lv_obj_add_event_cb(ui_btnMainTTunes, fcnTouchTunes, LV_EVENT_CLICKED, NULL);

  // Build WaveKai config screen
  wk_screen_init();

  // Build CC1101 sub-menu screen (replaces direct jump to CC1101Stuff)
  {
    // Create sub-menu screen
    static lv_obj_t *ui_scrCC1101Menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrCC1101Menu, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrCC1101Menu, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(ui_scrCC1101Menu, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrCC1101Menu, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(ui_scrCC1101Menu);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 15);
    lv_label_set_text(title, "SUB-GHZ TOOLS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &ui_font_Verdana18, LV_PART_MAIN);

    // 320x480 portrait layout

    // CC1101 Tools button
    lv_obj_t *btn1101 = lv_btn_create(ui_scrCC1101Menu);
    lv_obj_set_pos(btn1101, 15, 60);
    lv_obj_set_size(btn1101, 290, 65);
    lv_obj_set_style_bg_color(btn1101, lv_color_hex(0x1A1A3E), LV_PART_MAIN);
    lv_obj_set_style_radius(btn1101, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn1101, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn1101, lv_color_hex(0x333366), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn1101, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(btn1101, [](lv_event_t *e) {
        lv_scr_load(ui_scrCC1101Stuff);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl1101 = lv_label_create(btn1101);
    lv_label_set_text(lbl1101, "CC1101 Tools");
    lv_obj_set_align(lbl1101, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(lbl1101, 5);
    lv_obj_set_y(lbl1101, -8);
    lv_obj_set_style_text_color(lbl1101, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl1101, &ui_font_Verdana16, LV_PART_MAIN);
    lv_obj_t *desc1101 = lv_label_create(btn1101);
    lv_label_set_text(desc1101, "Capture, Replay, Scanner");
    lv_obj_set_align(desc1101, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(desc1101, 5);
    lv_obj_set_y(desc1101, 12);
    lv_obj_set_style_text_color(desc1101, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(desc1101, &ui_font_Verdana14, LV_PART_MAIN);

    // WaveKai button
    lv_obj_t *btnWK = lv_btn_create(ui_scrCC1101Menu);
    lv_obj_set_pos(btnWK, 15, 140);
    lv_obj_set_size(btnWK, 290, 65);
    lv_obj_set_style_bg_color(btnWK, lv_color_hex(0x0A2A1A), LV_PART_MAIN);
    lv_obj_set_style_radius(btnWK, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnWK, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnWK, lv_color_hex(0x00AA44), LV_PART_MAIN);
    lv_obj_set_style_border_width(btnWK, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(btnWK, [](lv_event_t *e) {
        lv_disp_load_scr(ui_scrWaveKai);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lblWK = lv_label_create(btnWK);
    lv_label_set_text(lblWK, "WaveKai");
    lv_obj_set_align(lblWK, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(lblWK, 5);
    lv_obj_set_y(lblWK, -8);
    lv_obj_set_style_text_color(lblWK, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWK, &ui_font_Verdana16, LV_PART_MAIN);
    lv_obj_t *descWK = lv_label_create(btnWK);
    lv_label_set_text(descWK, "Config, Auto-Crack, Rolling Codes");
    lv_obj_set_align(descWK, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(descWK, 5);
    lv_obj_set_y(descWK, 12);
    lv_obj_set_style_text_color(descWK, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(descWK, &ui_font_Verdana14, LV_PART_MAIN);

    // BACK button
    lv_obj_t *btnBack = lv_btn_create(ui_scrCC1101Menu);
    lv_obj_set_pos(btnBack, 15, 430);
    lv_obj_set_size(btnBack, 100, 35);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(btnBack, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnBack, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btnBack, [](lv_event_t *e) {
        lv_disp_load_scr(ui_scrMain);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, "<" " BACK");
    lv_obj_center(lblBack);
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblBack, &ui_font_Verdana14, LV_PART_MAIN);

    // Redirect the main CC1101 button to our sub-menu instead of CC1101Stuff
    // Remove existing event handler and add new one
    lv_obj_remove_event_cb(ui_btnMain1101, event_load_screen_scan);
    lv_obj_add_event_cb(ui_btnMain1101, [](lv_event_t *e) {
        lv_disp_load_scr(ui_scrCC1101Menu);
    }, LV_EVENT_CLICKED, NULL);
  }

  // Persistent status bar (WiFi + battery icons) on lv_layer_top()
  statusbar_init();

  // Sweep every SquareLine-generated dropdown and disable its chevron
  // symbol — the bundled Montserrat fonts don't ship that glyph and it
  // would otherwise paint as a small rectangle.
  purge_all_dropdown_symbols();

  // Rebuild the Settings screen — the SquareLine version still showed
  // legacy WiFi-AP / OTA-enable widgets that are obsolete now that
  // OTA happens over HTTP. New screen has: Check Updates, Volume,
  // Brightness, Rotate, About, Back.
  settings_screen_build();

  // Replace the SquareLine "FLIPPER RAW PLAYER" layout on ui_scrPresets
  // with the new hand-coded Flipper Player (PLAY / STOP / READ RAW).
  fp_screen_build();

  // ============================================================
  // CHAOS tab (CC1101 Tools → was PACKET GEN). Hosts Tesla US/EU
  // and a status line. Built dynamically because the SquareLine
  // PACKET GEN layout is still in lib/ui — the Tesla widgets sit
  // on top of it at the bottom of the tab.
  // ============================================================
  if (ui_Generator) {
    auto mkChaosBtn = [](int x, int y, int w, int h,
                         const char *text, uint32_t bg,
                         lv_event_cb_t cb) {
      lv_obj_t *b = lv_btn_create(ui_Generator);
      lv_obj_set_size(b, w, h);
      lv_obj_set_pos(b, x, y);
      lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
      lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
      lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *l = lv_label_create(b);
      lv_label_set_text(l, text);
      lv_obj_center(l);
      lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_set_style_text_font(l, &ui_font_Verdana14, LV_PART_MAIN);
      if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
      return b;
    };

    // Two Tesla TX buttons at the bottom of the CHAOS tab
    mkChaosBtn(8,   220, 145, 42, "TESLA US 315", 0x661111,
               [](lv_event_t *e) {
                 extern uint8_t currentState;
                 if (currentState != STATE_IDLE) return;
                 chaos_status_set("Sending US Tesla...");
                 currentState = STATE_TESLA_US;
               });
    mkChaosBtn(160, 220, 145, 42, "TESLA EU 433", 0x661111,
               [](lv_event_t *e) {
                 extern uint8_t currentState;
                 if (currentState != STATE_IDLE) return;
                 chaos_status_set("Sending EU Tesla...");
                 currentState = STATE_TESLA_EU;
               });

    // Chaos status label below the Tesla buttons
    chaos_lblStatus = lv_label_create(ui_Generator);
    lv_obj_set_width(chaos_lblStatus, 300);
    lv_obj_set_pos(chaos_lblStatus, 8, 270);
    lv_label_set_text(chaos_lblStatus, "Ready");
    lv_obj_set_style_text_align(chaos_lblStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(chaos_lblStatus, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(chaos_lblStatus, &ui_font_Verdana14, LV_PART_MAIN);
  }

  // Whenever the user returns to the main menu, restore the saved
  // WiFi network — Marauder / BLE spam tool screens flip the radio
  // into AP / OFF / monitor modes and lose the STA connection. This
  // keeps the WaveKai API + OTA path live from the home screen.
  lv_obj_add_event_cb(ui_scrMain, [](lv_event_t *e) {
    wifi_restore_persisted();
  }, LV_EVENT_SCREEN_LOADED, NULL);

  // ============================================================
  // Rat-image easter egg on the main menu.
  //   Short click  → "shatter" effect (white crack lines + crush sound)
  //                  for ~3 s, then fades away.
  //   Hold 5 s     → launch the Space Invaders screen.
  // ============================================================
  {
    static lv_obj_t *si_shatter_root = nullptr;
    static lv_timer_t *si_shatter_clear_timer = nullptr;
    static lv_timer_t *si_long_press_timer = nullptr;
    static bool si_long_press_fired = false;

    lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_CLICKABLE);

    // Helper: clear an in-flight shatter overlay. Use lv_obj_del_async
    // so the destruction happens at a safe point in the LVGL loop rather
    // than potentially mid-render (which can fire a watchdog/reboot).
    static auto shatter_clear = []() {
      if (si_shatter_root) {
        lv_obj_del_async(si_shatter_root);
        si_shatter_root = nullptr;
      }
      if (si_shatter_clear_timer) {
        lv_timer_del(si_shatter_clear_timer);
        si_shatter_clear_timer = nullptr;
      }
    };

    // Helper: build a realistic cracked-glass overlay from a single
    // impact point. Strategy that avoids the previous reboots:
    //   - One full-screen darkening layer (single lv_obj rect)
    //   - One bright "impact" disc at the tap point
    //   - 6 thick radial cracks (lv_line each, 2 points, no branches)
    //   - 3 thin concentric ring fragments (short lv_line arcs)
    // Total widgets = 1 + 1 + 6 + 3 = 11. Static point arrays are
    // copied into a single pool so multiple fires don't race them.
    static auto shatter_fire = [](int impact_x, int impact_y) {
      // Drop the request if a shatter is already showing.
      if (si_shatter_root) return;

      // Clamp impact so the densest cracks stay on-screen
      if (impact_x < 8)         impact_x = 8;
      if (impact_x > 320 - 8)   impact_x = 320 - 8;
      if (impact_y < 8)         impact_y = 8;
      if (impact_y > 480 - 8)   impact_y = 480 - 8;

      // Slightly-darkened backdrop so the cracks read as bright glass
      // edges catching light. Not full-opacity — the menu still bleeds
      // through behind the cracks like a partially-fogged windshield.
      si_shatter_root = lv_obj_create(ui_scrMain);
      lv_obj_set_size(si_shatter_root, 320, 480);
      lv_obj_set_pos(si_shatter_root, 0, 0);
      lv_obj_set_style_bg_color(si_shatter_root, lv_color_hex(0x000010), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(si_shatter_root, 110, LV_PART_MAIN);
      lv_obj_set_style_border_width(si_shatter_root, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(si_shatter_root, 0, LV_PART_MAIN);
      lv_obj_clear_flag(si_shatter_root, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(si_shatter_root, LV_OBJ_FLAG_SCROLLABLE);

      // -----------------------------------------------------------
      // Realistic 3-level crack hierarchy:
      //   PRIMARY (14)   long radials, thick (2-3 px), bright white
      //   SECONDARY (14) branch off primaries at random midpoints,
      //                  medium length, 1 px, slightly cyan
      //   TERTIARY (12)  short hairlines off secondaries, 1 px,
      //                  darker for depth
      //   CHORDS (7)     connect primary endpoints — forms shard polys
      //   DUST (28)      tiny 2x2 specks scattered around impact
      // -----------------------------------------------------------
      static const int N_PRI    = 14;
      static const int N_SEC    = 14;
      static const int N_TER    = 12;
      static const int N_CHORDS = 7;
      static const int N_DUST   = 28;

      static lv_point_t pri_pts[N_PRI][2];
      static lv_point_t sec_pts[N_SEC][2];
      static lv_point_t ter_pts[N_TER][2];
      static lv_point_t chord_pts[N_CHORDS][2];

      // ---- Primary radial cracks ----
      for (int n = 0; n < N_PRI; n++) {
        float base = (n * (2.0f * 3.14159f / N_PRI));
        float jitter = ((int)(esp_random() % 28) - 14) * 0.017453f;
        float a = base + jitter;
        int len = 70 + (int)(esp_random() % 150);
        pri_pts[n][0].x = impact_x;
        pri_pts[n][0].y = impact_y;
        pri_pts[n][1].x = impact_x + (int)(cosf(a) * len);
        pri_pts[n][1].y = impact_y + (int)(sinf(a) * len);
        lv_obj_t *ln = lv_line_create(si_shatter_root);
        lv_line_set_points(ln, pri_pts[n], 2);
        // Thicker = more dramatic primary fracture
        lv_obj_set_style_line_color(ln, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_line_width(ln, (esp_random() & 1) ? 3 : 2, LV_PART_MAIN);
        lv_obj_set_style_line_opa(ln, 250, LV_PART_MAIN);
      }

      // ---- Secondary branches off primary cracks ----
      for (int n = 0; n < N_SEC; n++) {
        int src = esp_random() % N_PRI;
        float t = 0.30f + ((esp_random() % 50) / 100.0f);   // 0.30..0.80 along parent
        int mx = pri_pts[src][0].x +
                  (int)((pri_pts[src][1].x - pri_pts[src][0].x) * t);
        int my = pri_pts[src][0].y +
                  (int)((pri_pts[src][1].y - pri_pts[src][0].y) * t);
        // Direction within ±70° of the parent's radial heading
        float dx = pri_pts[src][1].x - pri_pts[src][0].x;
        float dy = pri_pts[src][1].y - pri_pts[src][0].y;
        float parent_ang = atan2f(dy, dx);
        float fork_ang = parent_ang + (((int)(esp_random() % 140) - 70) * 0.017453f);
        int flen = 22 + (esp_random() % 45);
        sec_pts[n][0].x = mx;
        sec_pts[n][0].y = my;
        sec_pts[n][1].x = mx + (int)(cosf(fork_ang) * flen);
        sec_pts[n][1].y = my + (int)(sinf(fork_ang) * flen);
        lv_obj_t *sl = lv_line_create(si_shatter_root);
        lv_line_set_points(sl, sec_pts[n], 2);
        // Slight cyan tint — light catching a real shard edge
        lv_obj_set_style_line_color(sl, lv_color_hex(0xCCEEFF), LV_PART_MAIN);
        lv_obj_set_style_line_width(sl, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(sl, 210, LV_PART_MAIN);
      }

      // ---- Tertiary hairlines off secondary cracks ----
      for (int n = 0; n < N_TER; n++) {
        int src = esp_random() % N_SEC;
        float t = 0.20f + ((esp_random() % 60) / 100.0f);
        int mx = sec_pts[src][0].x +
                  (int)((sec_pts[src][1].x - sec_pts[src][0].x) * t);
        int my = sec_pts[src][0].y +
                  (int)((sec_pts[src][1].y - sec_pts[src][0].y) * t);
        float dx = sec_pts[src][1].x - sec_pts[src][0].x;
        float dy = sec_pts[src][1].y - sec_pts[src][0].y;
        float parent_ang = atan2f(dy, dx);
        float fork_ang = parent_ang + (((int)(esp_random() % 120) - 60) * 0.017453f);
        int flen = 8 + (esp_random() % 18);
        ter_pts[n][0].x = mx;
        ter_pts[n][0].y = my;
        ter_pts[n][1].x = mx + (int)(cosf(fork_ang) * flen);
        ter_pts[n][1].y = my + (int)(sinf(fork_ang) * flen);
        lv_obj_t *tl = lv_line_create(si_shatter_root);
        lv_line_set_points(tl, ter_pts[n], 2);
        lv_obj_set_style_line_color(tl, lv_color_hex(0xAACCDD), LV_PART_MAIN);
        lv_obj_set_style_line_width(tl, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(tl, 160, LV_PART_MAIN);
      }

      // ---- Chord segments — close shards into polygons ----
      for (int n = 0; n < N_CHORDS; n++) {
        int a_idx = (n * 2) % N_PRI;
        int b_idx = (a_idx + 1 + (esp_random() % 2)) % N_PRI;
        // Connect at a fraction along the parent crack so chords
        // appear at varied radii (not all at the outer tip)
        float at = 0.5f + ((esp_random() % 50) / 100.0f);
        float bt = 0.5f + ((esp_random() % 50) / 100.0f);
        chord_pts[n][0].x = pri_pts[a_idx][0].x +
                            (int)((pri_pts[a_idx][1].x - pri_pts[a_idx][0].x) * at);
        chord_pts[n][0].y = pri_pts[a_idx][0].y +
                            (int)((pri_pts[a_idx][1].y - pri_pts[a_idx][0].y) * at);
        chord_pts[n][1].x = pri_pts[b_idx][0].x +
                            (int)((pri_pts[b_idx][1].x - pri_pts[b_idx][0].x) * bt);
        chord_pts[n][1].y = pri_pts[b_idx][0].y +
                            (int)((pri_pts[b_idx][1].y - pri_pts[b_idx][0].y) * bt);
        lv_obj_t *cl = lv_line_create(si_shatter_root);
        lv_line_set_points(cl, chord_pts[n], 2);
        lv_obj_set_style_line_color(cl, lv_color_hex(0xDDEEFF), LV_PART_MAIN);
        lv_obj_set_style_line_width(cl, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(cl, 195, LV_PART_MAIN);
      }

      // ---- Dust specks near the impact (tiny white pixels) ----
      // Polar scatter so density is higher near the centre — looks
      // like fine glass spray fanned from the strike.
      for (int n = 0; n < N_DUST; n++) {
        float a = (esp_random() % 360) * 0.017453f;
        // Bias toward small radii via squared random
        float r0 = (esp_random() % 100) / 100.0f;
        int r = (int)(r0 * r0 * 38) + 4;
        int dx = (int)(cosf(a) * r);
        int dy = (int)(sinf(a) * r);
        lv_obj_t *dot = lv_obj_create(si_shatter_root);
        lv_obj_set_size(dot, 2, 2);
        lv_obj_set_pos(dot, impact_x + dx, impact_y + dy);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, 200 + (esp_random() & 0x37), LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
      }

      tone_play_shatter();
      si_shatter_clear_timer = lv_timer_create([](lv_timer_t *t) {
        if (si_shatter_root) {
          lv_obj_del_async(si_shatter_root);
          si_shatter_root = nullptr;
        }
        si_shatter_clear_timer = nullptr;
      }, 2500, nullptr);
      lv_timer_set_repeat_count(si_shatter_clear_timer, 1);
      return;
    };

    // ----------------------------------------------------------------
    // Procedural-cracks fallback (kept around for reference / for the
    // case where the image is ever removed). Not reached in normal
    // builds because shatter_fire returns above. Wrapped in a no-op
    // lambda so the original code path stays self-contained.
    // ----------------------------------------------------------------
    static auto shatter_fire_procedural = [](int impact_x, int impact_y) {
      if (si_shatter_root) return;
      si_shatter_root = lv_obj_create(ui_scrMain);
      lv_obj_set_size(si_shatter_root, 320, 480);
      lv_obj_set_pos(si_shatter_root, 0, 0);
      lv_obj_set_style_bg_color(si_shatter_root, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(si_shatter_root, 140, LV_PART_MAIN);
      lv_obj_set_style_border_width(si_shatter_root, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(si_shatter_root, 0, LV_PART_MAIN);
      lv_obj_clear_flag(si_shatter_root, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(si_shatter_root, LV_OBJ_FLAG_SCROLLABLE);

      if (impact_x < 12)        impact_x = 12;
      if (impact_x > 320 - 12)  impact_x = 320 - 12;
      if (impact_y < 12)        impact_y = 12;
      if (impact_y > 480 - 12)  impact_y = 480 - 12;

      // Spider-web crack pattern — no filled discs (those read as
      // cartoon glow). Just lines: 12 primary radial cracks at varied
      // lengths, 6 branch forks off random midpoints, 5 chord segments
      // connecting endpoints to suggest a polygonal shatter mesh.
      static const int N_MAIN  = 12;
      static const int N_FORK  = 6;
      static const int N_CHORD = 5;
      static lv_point_t main_pts[N_MAIN][2];
      static lv_point_t fork_pts[N_FORK][2];
      static lv_point_t chord_pts[N_CHORD][2];

      // Primary radial cracks — angles in even slots ±jitter
      for (int n = 0; n < N_MAIN; n++) {
        float base = (n * (2.0f * 3.14159f / N_MAIN));
        float jitter = ((int)(esp_random() % 22) - 11) * 0.017453f;  // ±11°
        float a = base + jitter;
        // Lengths vary so the impact looks chaotic rather than radial
        int len = 80 + (int)(esp_random() % 130);
        main_pts[n][0].x = impact_x;
        main_pts[n][0].y = impact_y;
        main_pts[n][1].x = impact_x + (int)(cosf(a) * len);
        main_pts[n][1].y = impact_y + (int)(sinf(a) * len);
        lv_obj_t *ln = lv_line_create(si_shatter_root);
        lv_line_set_points(ln, main_pts[n], 2);
        // Alternate widths for depth — thick lines read as primary
        // fractures, thin hairlines as secondary stress lines
        lv_obj_set_style_line_color(ln, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_line_width(ln, (n & 1) ? 1 : 2, LV_PART_MAIN);
        lv_obj_set_style_line_opa(ln, (n & 1) ? 200 : 245, LV_PART_MAIN);
      }

      // Branch forks — sprout off a random midpoint of a random main
      // crack, angled away from the radial direction.
      for (int n = 0; n < N_FORK; n++) {
        int src = esp_random() % N_MAIN;
        float mt = 0.35f + ((esp_random() % 30) / 100.0f);   // 0.35..0.65 along
        int mx = main_pts[src][0].x + (int)((main_pts[src][1].x - main_pts[src][0].x) * mt);
        int my = main_pts[src][0].y + (int)((main_pts[src][1].y - main_pts[src][0].y) * mt);
        // Direction perpendicular-ish to parent crack
        float dx = main_pts[src][1].x - main_pts[src][0].x;
        float dy = main_pts[src][1].y - main_pts[src][0].y;
        float pang = atan2f(dy, dx) + 1.5708f + (((int)(esp_random() % 80) - 40) * 0.017453f);
        int flen = 16 + (esp_random() % 30);
        fork_pts[n][0].x = mx;
        fork_pts[n][0].y = my;
        fork_pts[n][1].x = mx + (int)(cosf(pang) * flen);
        fork_pts[n][1].y = my + (int)(sinf(pang) * flen);
        lv_obj_t *fl = lv_line_create(si_shatter_root);
        lv_line_set_points(fl, fork_pts[n], 2);
        lv_obj_set_style_line_color(fl, lv_color_hex(0xEEEEFF), LV_PART_MAIN);
        lv_obj_set_style_line_width(fl, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(fl, 180, LV_PART_MAIN);
      }

      // Chord fragments — connect endpoint A of one main crack to a
      // mid-radius of an adjacent crack to suggest polygonal shards
      for (int n = 0; n < N_CHORD; n++) {
        int a_idx = (n * 2) % N_MAIN;
        int b_idx = (a_idx + 2 + (esp_random() & 1)) % N_MAIN;
        float bt = 0.55f + ((esp_random() % 25) / 100.0f);   // 0.55..0.80
        chord_pts[n][0].x = main_pts[a_idx][1].x;
        chord_pts[n][0].y = main_pts[a_idx][1].y;
        chord_pts[n][1].x = main_pts[b_idx][0].x +
                            (int)((main_pts[b_idx][1].x - main_pts[b_idx][0].x) * bt);
        chord_pts[n][1].y = main_pts[b_idx][0].y +
                            (int)((main_pts[b_idx][1].y - main_pts[b_idx][0].y) * bt);
        lv_obj_t *cl = lv_line_create(si_shatter_root);
        lv_line_set_points(cl, chord_pts[n], 2);
        lv_obj_set_style_line_color(cl, lv_color_hex(0xCCDDEE), LV_PART_MAIN);
        lv_obj_set_style_line_width(cl, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(cl, 180, LV_PART_MAIN);
      }

      tone_play_shatter();
      // Single-fire cleanup timer. The callback deletes only the OVERLAY
      // (via lv_obj_del_async) and nulls the global pointers — it does
      // NOT call lv_timer_del() because LVGL auto-recycles a timer once
      // its repeat_count hits zero. Calling lv_timer_del() here would
      // double-free the same handle that shatter_clear() already removed
      // and crash on the next LVGL tick.
      si_shatter_clear_timer = lv_timer_create([](lv_timer_t *t) {
        if (si_shatter_root) {
          lv_obj_del_async(si_shatter_root);
          si_shatter_root = nullptr;
        }
        si_shatter_clear_timer = nullptr;   // timer auto-deletes; just forget it
      }, 2500, nullptr);
      lv_timer_set_repeat_count(si_shatter_clear_timer, 1);
    };

    // Coordinates captured at PRESS time. Even if the user slides their
    // finger before releasing, the shatter impact stays at this point.
    static lv_coord_t si_press_x = 160;
    static lv_coord_t si_press_y = 90;

    // Press handler — freeze tap point + start the 5 s long-press timer
    // for the Wave Invaderz easter egg. The shatter doesn't fire yet;
    // we wait until release so a 5 s hold can launch the game without
    // also triggering a shatter on the way in.
    lv_obj_add_event_cb(ui_Image1, [](lv_event_t *e) {
      lv_indev_t *indev = lv_indev_get_act();
      lv_point_t pt = { 160, 90 };
      if (indev) lv_indev_get_point(indev, &pt);
      si_press_x = pt.x;
      si_press_y = pt.y;

      si_long_press_fired = false;
      if (si_long_press_timer) lv_timer_del(si_long_press_timer);
      si_long_press_timer = lv_timer_create([](lv_timer_t *t) {
        si_long_press_fired = true;
        si_open();   // launch Wave Invaderz
        lv_timer_del(t);
        si_long_press_timer = nullptr;
      }, 5000, nullptr);
      lv_timer_set_repeat_count(si_long_press_timer, 1);
    }, LV_EVENT_PRESSED, NULL);

    // Release — just cancel the 5 s long-press timer. The shatter
    // effect on short tap was removed per user request; only the
    // 5 s hold (which launches Wave Invaderz) remains.
    lv_obj_add_event_cb(ui_Image1, [](lv_event_t *e) {
      if (si_long_press_timer) {
        lv_timer_del(si_long_press_timer);
        si_long_press_timer = nullptr;
      }
      si_long_press_fired = false;
    }, LV_EVENT_RELEASED, NULL);

    lv_obj_add_event_cb(ui_Image1, [](lv_event_t *e) {
      if (si_long_press_timer) {
        lv_timer_del(si_long_press_timer);
        si_long_press_timer = nullptr;
      }
    }, LV_EVENT_PRESS_LOST, NULL);
  }

  // ============================================================
  // Animated lightning on the main menu. Two main jagged bolts + four
  // shorter "fork" branches, random orientation (top→bottom, diagonals,
  // or horizontal "cloud-to-cloud"). Full-screen pale-blue sky flash
  // illuminates everything for ~180 ms.
  // Idle for 12+ s → also fires synthesized rolling thunder ~700 ms
  // after the bolt (realistic visual-to-audio delay for distant
  // lightning). All sits behind the buttons via lv_obj_move_background.
  // ============================================================
  {
    static lv_obj_t *lightningOverlay = nullptr;
    static lv_obj_t *drearyScene      = nullptr;
    static const int N_BOLTS    = 2;
    static const int N_FORKS    = 6;
    static const int BOLT_PTS_MAX = 12;   // upper bound; per-strike count is randomised
    static const int FORK_PTS   = 3;
    // Bolts never start above SAFE_TOP_Y so the lightning never crosses
    // the status bar (WiFi + battery icons sit at y=2..22).
    static const int SAFE_TOP_Y = 30;
    static lv_obj_t *bolts[N_BOLTS];
    static lv_obj_t *forks[N_FORKS];
    static lv_point_t bolt_pts[N_BOLTS][BOLT_PTS_MAX];
    static lv_point_t fork_pts[N_FORKS][FORK_PTS];

    // ----------------------------------------------------------------
    // Dreary scenes — four silhouette layers behind the menu, each
    // revealed only while a lightning bolt is in flight. Picking a
    // random one each strike gives the rat menu a "what's lurking
    // out there?" feel without ever being on screen long enough to
    // distract from the buttons. Each scene is a lv_obj group child
    // of drearyScene, default HIDDEN; strike() shows one + fades the
    // parent layer in/out.
    //
    //   Scene 0 — Bare trees + tombstones + a weathered cross
    //   Scene 1 — Gothic castle silhouette with bats
    //   Scene 2 — Ruined chapel with single watching figure
    //   Scene 3 — Twisted forest of looming branches
    // ----------------------------------------------------------------
    drearyScene = lv_obj_create(ui_scrMain);
    lv_obj_set_size(drearyScene, 320, 480);
    lv_obj_set_pos(drearyScene, 0, 0);
    lv_obj_set_style_bg_opa(drearyScene, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(drearyScene, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(drearyScene, 0, LV_PART_MAIN);
    lv_obj_set_style_opa(drearyScene, 0, LV_PART_MAIN);   // whole layer hidden
    lv_obj_clear_flag(drearyScene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(drearyScene, LV_OBJ_FLAG_SCROLLABLE);

    static const int N_DREARY_SCENES = 4;
    static lv_obj_t *dreary_scenes[N_DREARY_SCENES] = {nullptr};

    auto mk_scene_layer = []() {
      lv_obj_t *s = lv_obj_create(drearyScene);
      lv_obj_set_size(s, 320, 480);
      lv_obj_set_pos(s, 0, 0);
      lv_obj_set_style_bg_opa(s, 0, LV_PART_MAIN);
      lv_obj_set_style_border_width(s, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(s, 0, LV_PART_MAIN);
      lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
      return s;
    };

    auto mk_rect_in = [](lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
      lv_obj_t *r = lv_obj_create(parent);
      lv_obj_set_size(r, w, h);
      lv_obj_set_pos(r, x, y);
      lv_obj_set_style_bg_color(r, lv_color_hex(color), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(r, 255, LV_PART_MAIN);
      lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
      lv_obj_set_style_radius(r, radius, LV_PART_MAIN);
      lv_obj_set_style_pad_all(r, 0, LV_PART_MAIN);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
      return r;
    };

    auto mk_line_in = [](lv_obj_t *parent, lv_point_t *pts, int n,
                          uint32_t color, int width) {
      lv_obj_t *l = lv_line_create(parent);
      lv_line_set_points(l, pts, n);
      lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN);
      lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
      lv_obj_set_style_line_opa(l, 255, LV_PART_MAIN);
      return l;
    };

    // ---- Scene 0 — Bare trees + tombstones ----
    {
      lv_obj_t *s = mk_scene_layer();
      dreary_scenes[0] = s;
      // Ground
      mk_rect_in(s, 0, 340, 320, 140, 0x040408, 0);
      // Jagged ridge
      static lv_point_t ridge_pts[9] = {
        {0,340},{38,322},{70,344},{92,312},{130,334},
        {168,318},{210,342},{268,328},{320,340}
      };
      mk_line_in(s, ridge_pts, 9, 0x0A0A18, 3);
      // Spindly trees: trunk + a couple of angular branches each
      const int trunk_x[3] = {48, 170, 282};
      const int trunk_h[3] = {150, 120, 138};
      static lv_point_t br_pts[3][6][3];
      for (int t = 0; t < 3; t++) {
        int x = trunk_x[t], h = trunk_h[t], top = 340 - h;
        mk_rect_in(s, x - 1, top, 3, h, 0x000000, 0);
        // 3 branches per tree, alternating sides
        for (int b = 0; b < 3; b++) {
          int by = top + 24 + b * 28;
          int dir = (b & 1) ? 1 : -1;
          int reach = 10 + b * 4;
          br_pts[t][b][0] = {(lv_coord_t)x, (lv_coord_t)by};
          br_pts[t][b][1] = {(lv_coord_t)(x + dir * reach),
                              (lv_coord_t)(by - 4 - b)};
          br_pts[t][b][2] = {(lv_coord_t)(x + dir * (reach + 6)),
                              (lv_coord_t)(by - 10 - b * 2)};
          mk_line_in(s, br_pts[t][b], 3, 0x000000, 2);
        }
      }
      // Tombstones + cross between trees
      mk_rect_in(s, 96,  314, 14, 28, 0x141420, 4);
      mk_rect_in(s, 212, 320, 18, 22, 0x141420, 4);
      mk_rect_in(s, 146, 308, 3, 30, 0x222230, 0);
      mk_rect_in(s, 138, 314, 19, 3, 0x222230, 0);
    }

    // ---- Scene 1 — Gothic castle + flock of bats ----
    {
      lv_obj_t *s = mk_scene_layer();
      dreary_scenes[1] = s;
      // Ground
      mk_rect_in(s, 0, 360, 320, 120, 0x040408, 0);
      // Castle main keep — tall central block
      mk_rect_in(s, 130, 230, 60, 130, 0x0A0A18, 0);
      // Side towers (left + right) shorter
      mk_rect_in(s, 90,  270, 30, 90, 0x0A0A18, 0);
      mk_rect_in(s, 200, 260, 32, 100, 0x0A0A18, 0);
      // Crenellations on top of central keep (3 small rects)
      mk_rect_in(s, 135, 222, 10, 10, 0x0A0A18, 0);
      mk_rect_in(s, 155, 222, 10, 10, 0x0A0A18, 0);
      mk_rect_in(s, 175, 222, 10, 10, 0x0A0A18, 0);
      // Tower peaked roofs as triangular lines
      static lv_point_t roof_l[3] = {{90,270},{105,250},{120,270}};
      static lv_point_t roof_r[3] = {{200,260},{216,238},{232,260}};
      mk_line_in(s, roof_l, 3, 0x0A0A18, 4);
      mk_line_in(s, roof_r, 3, 0x0A0A18, 4);
      // Bats — small black "M" shapes scattered in the sky
      static lv_point_t bat_pts[5][3];
      const int bx[5] = {40, 80, 200, 250, 280};
      const int by[5] = {120, 160, 100, 140, 90};
      for (int i = 0; i < 5; i++) {
        bat_pts[i][0] = {(lv_coord_t)(bx[i] - 6), (lv_coord_t)by[i]};
        bat_pts[i][1] = {(lv_coord_t)bx[i],       (lv_coord_t)(by[i] - 3)};
        bat_pts[i][2] = {(lv_coord_t)(bx[i] + 6), (lv_coord_t)by[i]};
        mk_line_in(s, bat_pts[i], 3, 0x000000, 2);
      }
    }

    // ---- Scene 2 — Ruined chapel + watching figure ----
    {
      lv_obj_t *s = mk_scene_layer();
      dreary_scenes[2] = s;
      // Ground
      mk_rect_in(s, 0, 360, 320, 120, 0x040408, 0);
      // Chapel walls — collapsed look: short jagged wall fragments
      mk_rect_in(s, 30,  300, 60, 60, 0x0A0A18, 0);  // left wall
      mk_rect_in(s, 230, 290, 60, 70, 0x0A0A18, 0);  // right wall
      // Broken arch — line from top of left wall up & over to right wall
      static lv_point_t arch_pts[4] = {{50,300},{120,250},{210,260},{260,290}};
      mk_line_in(s, arch_pts, 4, 0x0A0A18, 4);
      // Cross atop the arch
      mk_rect_in(s, 158, 230, 3, 22, 0x222230, 0);
      mk_rect_in(s, 152, 238, 15, 3, 0x222230, 0);
      // The watching figure — short stick silhouette near the front
      mk_rect_in(s, 156, 330, 6, 22, 0x000000, 0);    // body
      mk_rect_in(s, 154, 318, 10, 12, 0x000000, 6);   // head (round-ish)
    }

    // ---- Scene 3 — Twisted forest of looming branches ----
    {
      lv_obj_t *s = mk_scene_layer();
      dreary_scenes[3] = s;
      // Ground
      mk_rect_in(s, 0, 360, 320, 120, 0x040408, 0);
      // Five close-up tree silhouettes at varying heights, framing the view
      const int fx[5] = { 10, 80, 160, 240, 300 };
      const int fh[5] = { 230, 200, 260, 210, 240 };
      static lv_point_t fb[5][4][3];
      for (int t = 0; t < 5; t++) {
        int x = fx[t], h = fh[t], top = 360 - h;
        mk_rect_in(s, x - 2, top, 5, h, 0x000000, 0);
        // 4 wild branches per trunk
        for (int b = 0; b < 4; b++) {
          int by = top + 14 + b * 30;
          int dir = ((t + b) & 1) ? 1 : -1;
          int reach = 12 + ((t * 7 + b * 3) % 14);
          fb[t][b][0] = {(lv_coord_t)x, (lv_coord_t)by};
          fb[t][b][1] = {(lv_coord_t)(x + dir * reach),
                          (lv_coord_t)(by - 6 - ((b * 5) % 10))};
          fb[t][b][2] = {(lv_coord_t)(x + dir * (reach + 8)),
                          (lv_coord_t)(by - 18 - ((b * 3) % 6))};
          mk_line_in(s, fb[t][b], 3, 0x000000, 2);
        }
      }
    }

    lightningOverlay = lv_obj_create(ui_scrMain);
    lv_obj_set_size(lightningOverlay, 320, 480);
    lv_obj_set_pos(lightningOverlay, 0, 0);
    // Strong blue sky-illumination tint (was pale wash)
    lv_obj_set_style_bg_color(lightningOverlay, lv_color_hex(0x4A90E2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lightningOverlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(lightningOverlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lightningOverlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(lightningOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(lightningOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(lightningOverlay);

    // Bluer bolts: main brighter sky-blue, secondary deeper electric-blue
    for (int i = 0; i < N_BOLTS; i++) {
      bolts[i] = lv_line_create(lightningOverlay);
      lv_obj_set_style_line_color(bolts[i],
          i == 0 ? lv_color_hex(0xAAD4FF) : lv_color_hex(0x66B5FF), LV_PART_MAIN);
      lv_obj_set_style_line_width(bolts[i], 3 - i, LV_PART_MAIN);
      lv_obj_set_style_line_opa(bolts[i], 0, LV_PART_MAIN);
    }
    for (int i = 0; i < N_FORKS; i++) {
      forks[i] = lv_line_create(lightningOverlay);
      lv_obj_set_style_line_color(forks[i], lv_color_hex(0x88C8FF), LV_PART_MAIN);
      lv_obj_set_style_line_width(forks[i], 1, LV_PART_MAIN);
      lv_obj_set_style_line_opa(forks[i], 0, LV_PART_MAIN);
    }

    // Bolts-only ramp animator. The full-screen sky-flash background
    // tint and the dreary-scene silhouette reveal were both removed
    // per user request — only the bolt + fork line opacity rides this
    // envelope now, so each strike is just the bolt itself flashing
    // bright and fading out.
    auto fade_cb = [](void *obj, int32_t v) {
      for (int i = 0; i < N_BOLTS; i++)
        lv_obj_set_style_line_opa(bolts[i],
                                  (lv_opa_t)((v * 255) / 100), LV_PART_MAIN);
      for (int i = 0; i < N_FORKS; i++)
        lv_obj_set_style_line_opa(forks[i],
                                  (lv_opa_t)((v * 200) / 100), LV_PART_MAIN);
    };

    // Generate a jagged polyline from (x0,y0) walking in (dx,dy) direction.
    // dx,dy are per-step deltas; per-segment perpendicular jitter added.
    // Vertical clamp uses SAFE_TOP_Y so bolts never enter the status-bar
    // strip at the top of the screen.
    static auto generate_path = [](lv_point_t *pts, int n,
                                   int x0, int y0,
                                   float dx_per, float dy_per,
                                   int jitter) {
      pts[0].x = x0; pts[0].y = y0;
      for (int i = 1; i < n; i++) {
        int j = (int)(esp_random() % (uint32_t)(jitter * 2 + 1)) - jitter;
        // Perpendicular jitter: rotate (dx,dy) by 90deg → (-dy,dx)
        float perp = (dx_per != 0.0f) ? (-dy_per / dx_per) : 0.0f;
        int nx = pts[i - 1].x + (int)(dx_per) + (int)(perp * j);
        int ny = pts[i - 1].y + (int)(dy_per) + j;
        if (nx < 2)   nx = 2;
        if (nx > 318) nx = 318;
        if (ny < SAFE_TOP_Y) ny = SAFE_TOP_Y;
        if (ny > 478) ny = 478;
        pts[i].x = nx; pts[i].y = ny;
      }
    };

    // Palette of bolt color variants — picked per strike so consecutive
    // flashes don't look identical.
    static const uint32_t BOLT_MAIN_PALETTE[4] = {
        0xAAD4FF,   // pale electric blue (default)
        0xE0F0FF,   // near-white hot
        0x9AC8FF,   // cool deep blue
        0xC8E0FF    // soft sky blue
    };
    static const uint32_t BOLT_SEC_PALETTE[4] = {
        0x66B5FF, 0x88C8FF, 0x5599EE, 0x77BBEE
    };

    static auto strike = [fade_cb]() {
      if (lv_scr_act() != ui_scrMain) return;

      // -------- Randomized parameters per strike --------
      int n_pts        = 7 + (int)(esp_random() % 6);      // 7..12 segments
      if (n_pts > BOLT_PTS_MAX) n_pts = BOLT_PTS_MAX;
      int jitter_amp   = 14 + (int)(esp_random() % 24);    // 14..37 px sideways
      int color_var    = (int)(esp_random() % 4);
      int main_w       = 2 + (int)(esp_random() % 2);      // 2 or 3
      int sec_w        = 1 + (int)(esp_random() % 2);      // 1 or 2
      int n_forks_used = 2 + (int)(esp_random() % (N_FORKS - 1));  // 2..N_FORKS
      // Slightly different shape between the two main bolts each strike
      int twin_offset_x = (int)(esp_random() % 18) - 9;    // ±9 px
      int twin_offset_y = (int)(esp_random() % 18) - 9;

      // -------- Orientation + start point (clear of status bar) --------
      int orient = esp_random() % 5;   // 4 base + 1 "partial-length" variant
      int sx0, sy0;
      float dx, dy;
      int span_y = 480 - SAFE_TOP_Y;
      switch (orient) {
        case 0:  // vertical top→bottom
          sx0 = 30 + (esp_random() % 260);
          sy0 = SAFE_TOP_Y + (esp_random() % 20);
          dx  = (float)((int)(esp_random() % 9) - 4) * 0.5f;   // slight drift
          dy  = (float)(span_y - 30) / (float)(n_pts - 1);
          break;
        case 1:  // diag top-right to bottom-left
          sx0 = 200 + (esp_random() % 110);
          sy0 = SAFE_TOP_Y + (esp_random() % 20);
          dx  = (float)(-18 - (int)(esp_random() % 10));
          dy  = (float)(span_y - 30) / (float)(n_pts - 1);
          break;
        case 2:  // diag top-left to bottom-right
          sx0 = 10 + (esp_random() % 110);
          sy0 = SAFE_TOP_Y + (esp_random() % 20);
          dx  = (float)(18 + (int)(esp_random() % 10));
          dy  = (float)(span_y - 30) / (float)(n_pts - 1);
          break;
        case 3: { // horizontal "cloud-to-cloud" — anywhere middle of screen
          sx0 = 0;
          sy0 = SAFE_TOP_Y + 40 + (esp_random() % 200);
          dx  = 320.0f / (float)(n_pts - 1);
          dy  = (float)((int)(esp_random() % 7) - 3);        // small wobble
          break;
        }
        default: { // partial-length vertical bolt — short flash in upper-mid
          sx0 = 30 + (esp_random() % 260);
          sy0 = SAFE_TOP_Y + (esp_random() % 80);
          dx  = (float)((int)(esp_random() % 13) - 6) * 0.5f;
          int end_y = sy0 + 140 + (esp_random() % 180);
          if (end_y > 470) end_y = 470;
          dy  = (float)(end_y - sy0) / (float)(n_pts - 1);
          break;
        }
      }

      // Apply per-strike color + width
      lv_obj_set_style_line_color(bolts[0],
          lv_color_hex(BOLT_MAIN_PALETTE[color_var]), LV_PART_MAIN);
      lv_obj_set_style_line_color(bolts[1],
          lv_color_hex(BOLT_SEC_PALETTE[color_var]),  LV_PART_MAIN);
      lv_obj_set_style_line_width(bolts[0], main_w, LV_PART_MAIN);
      lv_obj_set_style_line_width(bolts[1], sec_w,  LV_PART_MAIN);
      for (int f = 0; f < N_FORKS; f++) {
        lv_obj_set_style_line_color(forks[f],
            lv_color_hex(BOLT_SEC_PALETTE[color_var]), LV_PART_MAIN);
        lv_obj_set_style_line_width(forks[f],
            1 + (int)(esp_random() % 2), LV_PART_MAIN);
      }

      // Main bolts (two near-twin paths, slightly offset start)
      generate_path(bolt_pts[0], n_pts, sx0, sy0, dx, dy, jitter_amp);
      lv_line_set_points(bolts[0], bolt_pts[0], n_pts);
      generate_path(bolt_pts[1], n_pts,
                    sx0 + twin_offset_x, sy0 + twin_offset_y,
                    dx, dy, jitter_amp + 4);
      lv_line_set_points(bolts[1], bolt_pts[1], n_pts);

      // Forks: branch off random midpoints of bolt[0]. Hide unused slots
      // so leftover paths from a previous strike don't keep flashing.
      for (int f = 0; f < N_FORKS; f++) {
        if (f >= n_forks_used) {
          lv_obj_add_flag(forks[f], LV_OBJ_FLAG_HIDDEN);
          continue;
        }
        lv_obj_clear_flag(forks[f], LV_OBJ_FLAG_HIDDEN);
        int anchor = 1 + (esp_random() % (n_pts - 2));
        int branch_dx = (int)(esp_random() % 80) - 40;
        int branch_dy = 18 + (esp_random() % 50);
        if (orient == 3) {
          branch_dy = (int)(esp_random() % 80) - 40;
          branch_dx = 18 + (esp_random() % 50);
        }
        float fdx = (float)branch_dx / (FORK_PTS - 1);
        float fdy = (float)branch_dy / (FORK_PTS - 1);
        if (fdx == 0) fdx = 0.001f;
        generate_path(fork_pts[f], FORK_PTS,
                      bolt_pts[0][anchor].x, bolt_pts[0][anchor].y,
                      fdx, fdy, 6 + (esp_random() % 8));
        lv_line_set_points(forks[f], fork_pts[f], FORK_PTS);
      }

      // Flash on
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, lightningOverlay);
      lv_anim_set_values(&a, 0, 100);
      lv_anim_set_time(&a, 50);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
      lv_anim_set_exec_cb(&a, fade_cb);
      lv_anim_start(&a);

      // Fade out
      lv_anim_set_values(&a, 100, 0);
      lv_anim_set_time(&a, 220);
      lv_anim_set_delay(&a, 50);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_start(&a);

      // Idle for >=12 s → schedule rolling thunder ~500-1400 ms after the bolt
      if (lv_disp_get_inactive_time(NULL) >= 12000) {
        uint32_t delay = 500 + (esp_random() % 900);
        lv_timer_t *th = lv_timer_create([](lv_timer_t *t) {
          // Re-check idle right before firing (user may have touched in the interim)
          if (lv_disp_get_inactive_time(NULL) >= 12000 &&
              lv_scr_act() == ui_scrMain) {
            tone_play_thunder(1800 + (esp_random() % 1500));   // 1.8-3.3 s rumble
          }
          lv_timer_del(t);
        }, delay, nullptr);
        lv_timer_set_repeat_count(th, 1);
      }
    };

    // Tick every 700 ms; 1-in-7 chance of a strike → ~5 s average gap.
    lv_timer_create([](lv_timer_t *t) {
      if (lv_scr_act() != ui_scrMain) return;
      if ((esp_random() % 7) == 0) strike();
    }, 700, nullptr);
  }

  xTaskCreatePinnedToCore(Task_Refresh_Screen, "Task_Refresh_Screen", 20000, NULL, 1, NULL, 0);

  // WaveKai: Auto-connect WiFi from saved credentials
  {
    bool prefsOpen = prefs.begin("wifi", true); // read-only
    String savedSSID = prefs.getString("ssid", "");
    String savedPass = prefs.getString("pass", "");
    prefs.end();
    Serial.printf("[WiFi NVS] boot load: prefs.begin=%s ssid='%s' pass_len=%u\n",
                  prefsOpen ? "OK" : "FAIL", savedSSID.c_str(), savedPass.length());

    // Load WaveKai config (server URL, auth credentials, token balance)
    waveKai.loadConfig();
    Serial.printf("[WaveKai] Server: %s\n", waveKai.serverUrl.c_str());

    // Always register WiFi event handler so icon updates immediately on connect
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiEvent);

    prefs.begin("wavekai", true);
    bool autoConnect = prefs.getBool("autoconnect", true);
    prefs.end();

    if (autoConnect && savedSSID.length() > 0) {
      Serial.printf("[WaveKai] Auto-connecting WiFi: %s\n", savedSSID.c_str());
      WiFi.begin(savedSSID.c_str(), savedPass.c_str());

      // Wait up to 10 seconds for connection
      int tries = 0;
      while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        wkLog("WiFi connected! IP: " + WiFi.localIP().toString());
        wifiGotIP = true;
        wifiConnected = true;
        statusbar_update_wifi(true);

        // Store IP for other use
        snprintf(wifiLocalIP, sizeof(wifiLocalIP), "%s", WiFi.localIP().toString().c_str());

        // Copy SSID for use elsewhere
        strncpy(wifiJoinSSID, savedSSID.c_str(), sizeof(wifiJoinSSID) - 1);

        // Start debug web server
        waveKai.startDebugServer();

        // Start Local REST API
        if (!localAPI.running) {
          localAPI.begin();
        }

        // Check WaveKai server
        if (waveKai.checkConnection()) {
          wkLog("Server connected!");
        } else {
          wkLog("Server not reachable: " + waveKai.lastError);
        }
      } else {
        Serial.println("\n[WaveKai] WiFi auto-connect failed.");
        WiFi.disconnect(true);
      }
    }
  }

  Print_Debug("Setup done.");
}

// ---------------------------------------------------------------------
// void loop()
// Runs on Core 1. All LVGL calls MUST be wrapped with lvgl_mutex
// because the LVGL refresh task runs on Core 0. Without the mutex,
// concurrent LVGL access causes heap corruption and random crashes.
// ---------------------------------------------------------------------
void loop()
{
  // Update persistent status bar icons (WiFi state, battery placeholder)
  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    statusbar_update();
    xSemaphoreGive(lvgl_mutex);
  }

  // Handle debug web server requests
  waveKai.handleDebugServer();

  // Handle OTA updates when enabled
  if (OTAInProgress == 1)
  {
    ArduinoOTA.handle();
  }

  // --- State machine: each state handles its RF/BLE/WiFi work ---

  if (currentState == STATE_AUDIO_TEST)
  {
    // MP3 playback retired; immediately drop back to idle so the state
    // machine doesn't get stuck if the legacy button is ever pressed.
    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_WIFI_SCAN)
  {
    // Non-blocking: check each loop iteration instead of busy-waiting
    if (!scanFinished)
    {
      // Timeout after 15s to prevent permanent freeze
      if (millis() - scanStartTime > WIFI_SCAN_TIMEOUT_MS)
      {
        WiFi.scanDelete();
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          lv_label_set_text(ui_lblWifiScanNetsFound, "Scan timed out — try again");
          xSemaphoreGive(lvgl_mutex);
        }
        currentState = STATE_IDLE;
        return;
      }
      vTaskDelay(1);
      return;
    }

    int16_t wifiNetwork = WiFi.scanComplete();

    // Protect all LVGL calls with mutex (Core 0 runs LVGL refresh)
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      char buf[32];
      snprintf(buf, sizeof(buf), "WiFi Networks Found: %d", wifiNetwork > 0 ? wifiNetwork : 0);
      lv_label_set_text(ui_lblWifiScanNetsFound, buf);

      // Populate dropdown with discovered networks
      for (int i = 0; i < wifiNetwork; i++)
      {
        lv_dropdown_add_option(ui_ddlWifiSSID, WiFi.SSID(i).c_str(), LV_DROPDOWN_POS_LAST);
      }

      // Show details for the first network (only if results exist)
      if (wifiNetwork > 0)
      {
        String ssid;
        int32_t rssi;
        uint8_t encryptionType;
        uint8_t *bssid;
        int32_t channel;

        WiFi.getNetworkInfo(0, ssid, encryptionType, rssi, bssid, channel);

        if (bssid != NULL) {
          char mac[18];
          snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                   bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

          char textBuf[256];
          snprintf(textBuf, sizeof(textBuf),
                   "SSID: %s\nMAC: %s\nRSSI: %d dBm\nChannel: %d\nEncryption Type: %s\n\n",
                   ssid.c_str(), mac, rssi, channel, GetEncryptionTypeString(encryptionType));
          lv_textarea_add_text(ui_txtWifiScanNetsFound, textBuf);
        }
      }

      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_WIFI_CONNECTING)
  {
    if (wifiGotIP) {
      // Connection successful
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        char statusBuf[80];
        snprintf(statusBuf, sizeof(statusBuf), "Connected!\nIP: %s\nSSID: %s",
                 wifiLocalIP, wifiJoinSSID);
        lv_label_set_text(ui_lblWifiScanNetsFound, statusBuf);
        lv_textarea_set_text(ui_txtWifiScanNetsFound, statusBuf);
        xSemaphoreGive(lvgl_mutex);
      }
      statusbar_update_wifi(true);

      // Start debug web server
      waveKai.startDebugServer();

      // Start Local REST API if not already running
      if (!localAPI.running) {
        localAPI.begin();
      }

      // WaveKai: check server connection (uses saved/default server URL)
      waveKai.updateMac();
      if (waveKai.checkConnection()) {
        wkLog("Server connected after WiFi join!");
      } else {
        wkLog("Server not reachable after WiFi join: " + waveKai.lastError);
      }

      currentState = STATE_IDLE;
    }
    else if (millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS) {
      // Timeout
      WiFi.disconnect(true);
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblWifiScanNetsFound, "Connection timed out");
        xSemaphoreGive(lvgl_mutex);
      }
      statusbar_update_wifi(false);
      currentState = STATE_IDLE;
    }
    else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  else if (currentState == STATE_ANALYZER)
  {
    if (SUBGHZ.ProtAnalyzerLoop())
    {
      // showResultProtAnalyzer() makes LVGL calls — protect with mutex
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        SUBGHZ.showResultProtAnalyzer();
        xSemaphoreGive(lvgl_mutex);
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      SUBGHZ.resetProtAnalyzer();
    }
    vTaskDelay(1);
  }
  else if (currentState == STATE_CAPTURE)
  {
    if (SUBGHZ.CaptureLoop())
    {
      SUBGHZ.disableReceiver();
      // showResultRecPlay() makes LVGL calls — protect with mutex
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        SUBGHZ.showResultRecPlay();
        lv_obj_add_state(ui_btnStop, LV_STATE_DISABLED);
        // Update WaveKai capture tab
        wk_update_capture_status();
        xSemaphoreGive(lvgl_mutex);
      }

      // WaveKai: ONLY auto-send if captured from the WaveKai interface (not CC1101 tools)
      // The WaveKai capture tab has its own "Send to API" button.
      // Auto-send only happens when the WaveKai screen is active AND auto-crack is enabled.
      extern int sample[];
      extern int samplecount;
      extern float CC1101_MHZ;

      // Check if we're on the WaveKai screen (currentState == WK_STATE_CAPTURE)
      bool isWaveKaiCapture = (currentState == WK_STATE_CAPTURE);

      if (isWaveKaiCapture) {
        // Loop capture mode — handle separately
        if (wk_loopCaptureActive) {
          currentState = STATE_IDLE;
          wk_loop_on_capture_complete();
          // Don't fall through — loop handler restarts capture
        } else {
          // Check if auto-crack is enabled in WaveKai config
          bool autoCrack = false;
          if (wk_cbAutoCrack) {
            autoCrack = lv_obj_get_state(wk_cbAutoCrack) & LV_STATE_CHECKED;
          }

          if (autoCrack && samplecount > 30 && WiFi.status() == WL_CONNECTED && waveKai.isAuthenticated) {
            wkLog("Auto-crack: sending " + String(samplecount) + " samples...");

            WaveKaiClient::CrackResult crack = waveKai.crackSignal(
                sample, samplecount, CC1101_MHZ);

            if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
              wk_show_result(crack);
              if (crack.success) {
                waveKai.refreshBalance();
                lv_label_set_text_fmt(wk_lblCaptureStatus,
                  crack.found ? "CRACKED! (%d tokens left)" : "Analyzed (%d tokens left)",
                  waveKai.tokenBalance);
                lv_obj_set_style_text_color(wk_lblCaptureStatus,
                  lv_color_hex(crack.found ? 0x00FF88 : 0xFF9100), LV_PART_MAIN);
              }
              xSemaphoreGive(lvgl_mutex);
            }
          }
          currentState = STATE_IDLE;
        }
      } else {
        Serial.printf("[CC1101] Capture done: %d samples (use WaveKai to analyze)\n", samplecount);
        currentState = STATE_IDLE;
      }
    }
    vTaskDelay(1);
  }
  else if (currentState == STATE_PLAYBACK)
  {
    if (SUBGHZ.sendCapture())
    {
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (wk_lblCaptureStatus) {
          lv_label_set_text(wk_lblCaptureStatus, "Replay complete!");
          lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
        }
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    }
  }
  else if (currentState == STATE_SCANNER)
  {
    // RSSI sweep is pure SPI on the CC1101 bus — no LVGL needed during the
    // sweep itself. ScannerLoop takes the mutex internally only when it's
    // ready to push pixels.
    SUBGHZ.ScannerLoop();
  }

  // API-driven frequency scanner (runs independently of UI scanner)
  if (localAPI.scanRunning) {
    localAPI.scannerTick();
  }
  else if (currentState == STATE_GENERATOR)
  {
    // Tight loop for RF signal generation — GPIO toggling at 255us intervals.
    // No LVGL calls inside, so no mutex needed. State change comes from
    // event handler on Core 0 when user flips the switch.
    while (currentState == STATE_GENERATOR)
    {
      SUBGHZ.GeneratorLoop();
    }
    SUBGHZ.disableTransmit();
  }
  else if (currentState == STATE_TESLA_US)
  {
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      chaos_status_set("Sending US Tesla (315 MHz)...");
      xSemaphoreGive(lvgl_mutex);
    }
    SUBGHZ.send_tesla(315.00);  // US: 315 MHz, forces ASK/OOK + max power
    currentState = STATE_TESLA_EU;
  }
  else if (currentState == STATE_TESLA_EU)
  {
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      chaos_status_set("Sending EU Tesla (433.92 MHz)...");
      xSemaphoreGive(lvgl_mutex);
    }
    SUBGHZ.send_tesla(433.92);  // EU: 433.92 MHz, forces ASK/OOK + max power
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      chaos_status_set("Tesla Complete !");
      xSemaphoreGive(lvgl_mutex);
    }
    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_SEND_FLIPPER)
  {
    char dbg[80];
    snprintf(dbg, sizeof(dbg), "Send RAW Data, sample count: %d | Frequency: %.2f", tempSampleCount, tempFreq);
    Print_Debug(dbg);

    SUBGHZ.setFrequency(tempFreq);
    SUBGHZ.enableTransmit();
    SUBGHZ.sendSamples(tempSample, tempSampleCount);
    SUBGHZ.disableTransmit();

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      char status[80];
      snprintf(status, sizeof(status), "Flipper Complete!\n\nSample: %d | Freq: %.2f mHz", tempSampleCount, tempFreq);
      lv_label_set_text(ui_lblPresetsStatus, status);
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_BLE_INIT)
  {
    // Heavy BLE initialization deferred here from LVGL event callback.
    // NimBLEDevice::init() would trigger the interrupt watchdog
    // if called from Core 0's lv_timer_handler context.
    Print_Debug("BLE init starting on Core 1");

    if (!BLEinit()) {
      // Init failed — update UI and go back to IDLE
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblBLEStatus, "Init failed!");
        lv_obj_set_style_text_color(ui_lblBLEStatus, lv_color_hex(0xFF0000), 0);
        lv_obj_clear_state(ui_btnBLEStart, LV_STATE_DISABLED);
        lv_obj_add_state(ui_btnBLEStop, LV_STATE_DISABLED);
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    } else {
      BLEsetPayload(bleCurrentDevice);

      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblBLEStatus, "Spamming...");
        xSemaphoreGive(lvgl_mutex);
      }

      currentState = STATE_SEND_BLESPAM;
    }
  }
  else if (currentState == STATE_SEND_BLESPAM)
  {
    BLEadvertise(); // 100ms advertising burst (no mutex needed for BLE)
    bleSpamCount++;

    // Take mutex ONLY for the brief LVGL UI updates — don't hold it
    // during the 100ms BLE advertising or it would block display refresh
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      char countBuf[32];
      snprintf(countBuf, sizeof(countBuf), "Packets: %d", bleSpamCount);
      lv_label_set_text(ui_lblBLECount, countBuf);

      // Log every 10th packet to the textarea
      if (bleSpamCount % 10 == 0) {
        char logBuf[48];
        snprintf(logBuf, sizeof(logBuf), "TX #%d: %s\n", bleSpamCount,
                 blePayloadNames[bleCurrentDevice]);
        lv_textarea_add_text(ui_txtBLELog, logBuf);
      }
      xSemaphoreGive(lvgl_mutex);
    }

    // Cycle to next device type in random mode
    if (bleRandomMode) {
      bleCurrentDevice = (bleCurrentDevice + 1) % BLE_PAYLOAD_COUNT;
      BLEsetPayload(bleCurrentDevice);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
  else if (currentState == STATE_BLE_SCAN_INIT)
  {
    // Initialize BLE on Core 1, then start scan
    if (!BLEinit()) {
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblBLEScanStatus, "BLE init failed!");
        lv_obj_set_style_text_color(ui_lblBLEScanStatus, lv_color_hex(0xFF0000), 0);
        lv_obj_clear_state(ui_btnBLEScanStart, LV_STATE_DISABLED);
        lv_obj_add_state(ui_btnBLEScanStop, LV_STATE_DISABLED);
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    } else {
      BLEscanStart(bleScanDuration);
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        char statusBuf[32];
        snprintf(statusBuf, sizeof(statusBuf), "Scanning (%ds)...", bleScanDuration);
        lv_label_set_text(ui_lblBLEScanStatus, statusBuf);
        lv_obj_set_style_text_color(ui_lblBLEScanStatus, lv_color_hex(0x00FFEB), 0);
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_BLE_SCAN_RUN;
    }
  }
  else if (currentState == STATE_BLE_SCAN_RUN)
  {
    if (BLEscanIsRunning()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      return;
    }

    // Scan finished — collect and display results
    int count = BLEscanGetResults();

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      lv_textarea_set_text(ui_txtBLEScanResults, "");

      for (int i = 0; i < count; i++) {
        char line[80];
        snprintf(line, sizeof(line), "%s\n  %s  RSSI:%d\n",
                 bleScanResults[i].name,
                 bleScanResults[i].addr,
                 bleScanResults[i].rssi);
        lv_textarea_add_text(ui_txtBLEScanResults, line);
      }

      char countBuf[32];
      snprintf(countBuf, sizeof(countBuf), "Devices: %d", count);
      lv_label_set_text(ui_lblBLEScanCount, countBuf);
      lv_label_set_text(ui_lblBLEScanStatus, "Scan complete");
      lv_obj_set_style_text_color(ui_lblBLEScanStatus, lv_color_hex(0x00FF00), 0);
      lv_obj_clear_state(ui_btnBLEScanStart, LV_STATE_DISABLED);
      lv_obj_add_state(ui_btnBLEScanStop, LV_STATE_DISABLED);
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_BLE_MAR_INIT)
  {
    if (!BLEinit()) {
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (mbs_airtag_status) {
          lv_label_set_text(mbs_airtag_status, "BLE init failed");
          lv_obj_set_style_text_color(mbs_airtag_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
        }
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    } else {
      uint8_t m = bleMarauderMode;
      switch (m) {
        case BLE_MAR_AIRTAG_SNIFF:
        case BLE_MAR_AIRTAG_MONITOR:
        case BLE_MAR_SKIMMER:
        case BLE_MAR_FLOCK:
        case BLE_MAR_META:
        case BLE_MAR_ANALYZER:
          BleMarStartScan(m);
          if      (m == BLE_MAR_AIRTAG_SNIFF || m == BLE_MAR_AIRTAG_MONITOR) currentState = STATE_BLE_MAR_AIRTAG;
          else if (m == BLE_MAR_SKIMMER)  currentState = STATE_BLE_MAR_SKIMMER;
          else if (m == BLE_MAR_FLOCK)    currentState = STATE_BLE_MAR_FLOCK;
          else if (m == BLE_MAR_META)     currentState = STATE_BLE_MAR_META;
          else if (m == BLE_MAR_ANALYZER) currentState = STATE_BLE_MAR_ANALYZER;
          break;
        case BLE_MAR_AIRTAG_SPOOF: currentState = STATE_BLE_MAR_SPOOF; break;
        case BLE_MAR_SOUR_APPLE:   currentState = STATE_BLE_MAR_SOURAPPLE; break;
        case BLE_MAR_SWIFTPAIR:    currentState = STATE_BLE_MAR_SWIFTPAIR; break;
        case BLE_MAR_SPAM_PLUS:    currentState = STATE_BLE_MAR_SPAMPLUS; break;
        default: currentState = STATE_IDLE; break;
      }
      bleMarLastSpamMs = 0;
      bleMarLastRefreshMs = 0;
    }
  }
  else if (currentState == STATE_BLE_MAR_AIRTAG ||
           currentState == STATE_BLE_MAR_SKIMMER ||
           currentState == STATE_BLE_MAR_FLOCK ||
           currentState == STATE_BLE_MAR_META ||
           currentState == STATE_BLE_MAR_ANALYZER)
  {
    uint32_t now = millis();
    if (now - bleMarLastRefreshMs > 500) {
      bleMarLastRefreshMs = now;
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(80)) == pdTRUE) {
        switch (currentState) {
          case STATE_BLE_MAR_AIRTAG:   mbs_refresh_airtag();   break;
          case STATE_BLE_MAR_SKIMMER:  mbs_refresh_skim();     break;
          case STATE_BLE_MAR_FLOCK:    mbs_refresh_flock();    break;
          case STATE_BLE_MAR_META:     mbs_refresh_meta();     break;
          case STATE_BLE_MAR_ANALYZER: mbs_refresh_analyzer(); break;
        }
        xSemaphoreGive(lvgl_mutex);
      }
    }
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan && !pScan->isScanning()) {
      pScan->start(0, nullptr, false);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  else if (currentState == STATE_BLE_MAR_SPOOF ||
           currentState == STATE_BLE_MAR_SOURAPPLE ||
           currentState == STATE_BLE_MAR_SWIFTPAIR ||
           currentState == STATE_BLE_MAR_SPAMPLUS)
  {
    uint8_t mode = BLE_MAR_OFF;
    switch (currentState) {
      case STATE_BLE_MAR_SPOOF:     mode = BLE_MAR_AIRTAG_SPOOF; break;
      case STATE_BLE_MAR_SOURAPPLE: mode = BLE_MAR_SOUR_APPLE;   break;
      case STATE_BLE_MAR_SWIFTPAIR: mode = BLE_MAR_SWIFTPAIR;    break;
      case STATE_BLE_MAR_SPAMPLUS:  mode = BLE_MAR_SPAM_PLUS;    break;
    }
    // Throttle payload rotation to ~200 ms so the radio stays on each
    // ad long enough for an iPhone / Windows scan window to catch it.
    uint32_t nowMs = millis();
    if (nowMs - bleMarLastSpamMs >= 200) {
      bleMarLastSpamMs = nowMs;
      BleMarAdvertiseTick(mode, &bleMarSpamRotState);
      bleMarSpamCount++;
    }

    uint32_t now = millis();
    if (now - bleMarLastRefreshMs > 300) {
      bleMarLastRefreshMs = now;
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(80)) == pdTRUE) {
        if (mbs_sp_lblCount) {
          char buf[32];
          snprintf(buf, sizeof(buf), "Packets: %d", bleMarSpamCount);
          lv_label_set_text(mbs_sp_lblCount, buf);
        }
        if (mbs_sp_log && (bleMarSpamCount % 10) == 0) {
          char log[64];
          const char *name = "";
          if (mode == BLE_MAR_SOUR_APPLE)   name = "SourApple";
          else if (mode == BLE_MAR_SWIFTPAIR) name = "SwiftPair";
          else if (mode == BLE_MAR_SPAM_PLUS) name = bleSpamPlus[bleMarSpamRotState % BLE_SPAM_PLUS_COUNT].name;
          else if (mode == BLE_MAR_AIRTAG_SPOOF) name = "AirTag";
          snprintf(log, sizeof(log), "TX #%d  %s\n", bleMarSpamCount, name);
          lv_textarea_add_text(mbs_sp_log, log);
        }
        xSemaphoreGive(lvgl_mutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
  else if (currentState == STATE_SEND_TOUCHTUNES)
  {
    Print_Debug("Sending TouchTunes command");

    sendTouchTunesCommand(tt_pending_pin, tt_pending_cmd);

    char dbg[64];
    snprintf(dbg, sizeof(dbg), "Sent TouchTunes PIN:%03d CMD:0x%02X", tt_pending_pin, tt_pending_cmd);
    Print_Debug(dbg);

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      lv_label_set_text(tt_lblStatus, "Sent!");
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_SEND_REMOTE)
  {
    Print_Debug("Sending Remote command");

    bool ok = false;
    if (sd_card_is_present()) {
      if (read_sd_card_flipper_file(String(remote_pendingPath))) {
        now_close_sd_card();

        char dbg[96];
        snprintf(dbg, sizeof(dbg), "Remote TX: samples=%d freq=%.2f file=%s",
                 tempSampleCount, tempFreq, remote_pendingPath);
        Print_Debug(dbg);

        SUBGHZ.setFrequency(tempFreq);
        SUBGHZ.enableTransmit();
        SUBGHZ.sendSamples(tempSample, tempSampleCount);
        SUBGHZ.disableTransmit();
        ok = true;
      } else {
        now_close_sd_card();
      }
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (ok) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Sent: %s", rb_displayNames[remote_pendingBtnId]);
        lv_label_set_text(remote_lblStatus, msg);
      } else {
        lv_label_set_text(remote_lblStatus, "Error: file not found");
      }
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_SEND_IR)
  {
    Print_Debug("Sending IR command");

    bool ok = false;
    if (sd_card_is_present()) {
      FlipperIRSignal sig;
      const char *sigName = remote_pendingSignalName[0] ? remote_pendingSignalName : NULL;

      // If no signal name specified, use first signal in file
      if (!sigName) {
        IRFileIndex idx;
        if (ir_file_index(remote_pendingPath, idx) && idx.count > 0) {
          sigName = idx.names[0];
        }
      }

      if (sigName && ir_file_read_signal(remote_pendingPath, sigName, sig)) {
        now_close_sd_card();

        char dbg[128];
        snprintf(dbg, sizeof(dbg), "IR TX: signal=%s raw=%d freq=%u file=%s",
                 sig.name, sig.isRaw, sig.frequency, remote_pendingPath);
        Print_Debug(dbg);

        IR_TX.sendSignal(sig);
        ok = true;
      } else {
        now_close_sd_card();
      }
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (ok) {
        char msg[48];
        snprintf(msg, sizeof(msg), "IR Sent: %s", rb_displayNames[remote_pendingBtnId]);
        lv_label_set_text(remote_lblStatus, msg);
      } else {
        lv_label_set_text(remote_lblStatus, "Error: IR file not found");
      }
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_WIFI_SNIFF)
  {
    WiFiMarauder::sniffLoop();  // Channel hopping

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      // Update stats labels
      char statsBuf[64];
      snprintf(statsBuf, sizeof(statsBuf), "Mgmt:%d  Data:%d  Probe:%d",
               WiFiMarauder::pktMgmt, WiFiMarauder::pktData, WiFiMarauder::probeCount);
      lv_label_set_text(ui_lblSniffStats, statsBuf);

      char chBuf[16];
      snprintf(chBuf, sizeof(chBuf), "Ch: %d", WiFiMarauder::sniffChannel);
      lv_label_set_text(ui_lblSniffChannel, chBuf);

      // Drain new probe entries — max 5 per cycle to avoid holding mutex too long
      int drained = 0;
      while (WiFiMarauder::probeReadIdx < WiFiMarauder::probeWriteIdx && drained < 5) {
        int idx = WiFiMarauder::probeReadIdx % WiFiMarauder::PROBE_BUF_SIZE;
        const WiFiMarauder::ProbeEntry &p = WiFiMarauder::probes[idx];
        char line[80];
        snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X  %-16s  %d\n",
                 p.mac[0], p.mac[1], p.mac[2], p.mac[3], p.mac[4], p.mac[5],
                 p.ssid, p.rssi);

        // Cap textarea to ~3000 chars to prevent unbounded RAM growth
        const char* curText = lv_textarea_get_text(ui_txtSniffLog);
        if (curText && strlen(curText) > 3000) {
          // Find ~halfway point at a newline and keep only the second half
          const char* mid = curText + strlen(curText) / 2;
          const char* nl = strchr(mid, '\n');
          if (nl) {
            lv_textarea_set_text(ui_txtSniffLog, nl + 1);
          } else {
            lv_textarea_set_text(ui_txtSniffLog, "");
          }
        }

        lv_textarea_add_text(ui_txtSniffLog, line);
        WiFiMarauder::probeReadIdx++;
        drained++;
      }

      // If writer lapped reader (overflow), catch up
      if (WiFiMarauder::probeWriteIdx - WiFiMarauder::probeReadIdx > WiFiMarauder::PROBE_BUF_SIZE) {
        WiFiMarauder::probeReadIdx = WiFiMarauder::probeWriteIdx - WiFiMarauder::PROBE_BUF_SIZE;
      }

      xSemaphoreGive(lvgl_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
  else if (currentState == STATE_BEACON_FLOOD)
  {
    WiFiMarauder::beaconLoop();  // Sends batch + vTaskDelay(10) inside

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      char countBuf[32];
      snprintf(countBuf, sizeof(countBuf), "Beacons: %d", WiFiMarauder::beaconCount);
      lv_label_set_text(ui_lblBeaconCount, countBuf);

      // Log every 100 beacons
      if (WiFiMarauder::beaconCount > 0 && WiFiMarauder::beaconCount % 100 < 30) {
        char logBuf[48];
        snprintf(logBuf, sizeof(logBuf), "TX batch: %d total\n", WiFiMarauder::beaconCount);
        lv_textarea_add_text(ui_txtBeaconLog, logBuf);
      }
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_DEAUTH_SCAN)
  {
    // Synchronous WiFi scan — runs on Core 1, may take a few seconds
    int count = WiFiMarauder::scanTargets();

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (count > 0) {
        // Build dropdown options: "SSID (ch X, -YYdBm)"
        char opts[1024] = {0};
        int pos = 0;
        for (int i = 0; i < count && pos < (int)sizeof(opts) - 64; i++) {
          if (i > 0) opts[pos++] = '\n';
          pos += snprintf(&opts[pos], sizeof(opts) - pos, "%s (ch%d %ddBm)",
                          WiFiMarauder::targets[i].ssid,
                          WiFiMarauder::targets[i].channel,
                          WiFiMarauder::targets[i].rssi);
        }
        lv_dropdown_set_options(ui_ddlDeauthTarget, opts);
        char statusBuf[32];
        snprintf(statusBuf, sizeof(statusBuf), "Found %d targets", count);
        lv_label_set_text(ui_lblDeauthStatus, statusBuf);
      } else {
        lv_dropdown_set_options(ui_ddlDeauthTarget, "No targets found");
        lv_label_set_text(ui_lblDeauthStatus, "No targets found");
      }
      lv_obj_set_style_text_color(ui_lblDeauthStatus, lv_color_hex(0xFAFF00), 0);
      xSemaphoreGive(lvgl_mutex);
    }

    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_DEAUTH_RUN)
  {
    WiFiMarauder::deauthLoop();  // Sends burst + vTaskDelay(50) inside

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      char countBuf[32];
      snprintf(countBuf, sizeof(countBuf), "Packets: %d", WiFiMarauder::deauthCount);
      lv_label_set_text(ui_lblDeauthCount, countBuf);

      if (WiFiMarauder::deauthCount > 0 && WiFiMarauder::deauthCount % 100 < 20) {
        char logBuf[48];
        snprintf(logBuf, sizeof(logBuf), "Deauth burst: %d total\n", WiFiMarauder::deauthCount);
        lv_textarea_add_text(ui_txtDeauthLog, logBuf);
      }
      xSemaphoreGive(lvgl_mutex);
    }
  }
  // ================================================================
  // MARAUDER EXTENDED FEATURES
  // ================================================================
  else if (currentState == STATE_MAR_APSCAN)
  {
    // Synchronous WiFi scan via WiFiMarauder::scanTargets()
    int count = WiFiMarauder::scanTargets();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      marauder_targets_refresh_aps();
      char buf[64];
      snprintf(buf, sizeof(buf), "Scan done: %d APs", count);
      if (mar.lbl_tg_status) lv_label_set_text(mar.lbl_tg_status, buf);
      xSemaphoreGive(lvgl_mutex);
    }
    currentState = STATE_IDLE;
  }
  else if (currentState == STATE_MAR_STA_SCAN)
  {
    bool stillRunning = WiFiMarauder::stationScanLoop();

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_stascan_locked();
      // Refresh the station list every 500ms approx
      static unsigned long lastListRefresh = 0;
      if (millis() - lastListRefresh > 500) {
        lastListRefresh = millis();
        marauder_targets_refresh_stations();
      }
      xSemaphoreGive(lvgl_mutex);
    }

    if (!stillRunning) {
      // Scan finished — clean up
      WiFiMarauder::deinit();
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        char buf[64];
        snprintf(buf, sizeof(buf), "STA scan done: %d found",
                 WiFiMarauder::stationCount);
        if (mar.lbl_tg_status) lv_label_set_text(mar.lbl_tg_status, buf);
        marauder_targets_refresh_stations();
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  else if (currentState == STATE_MAR_PMKID)
  {
    // Channel-hop while capturing — reuse sniff hop logic
    WiFiMarauder::sniffLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_pmkid_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  else if (currentState == STATE_MAR_PKTGRAPH)
  {
    WiFiMarauder::sniffLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_pktgraph_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  else if (currentState == STATE_MAR_CHANANA)
  {
    WiFiMarauder::channelAnalyzerLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_chananalyzer_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  // ----- OPS-tab extended features -----
  else if (currentState == STATE_MAR_PWN)
  {
    WiFiMarauder::sniffLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_pwn_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  else if (currentState == STATE_MAR_MACTRACK)
  {
    WiFiMarauder::macTrackLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_mac_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  else if (currentState == STATE_MAR_PROBEFLOOD)
  {
    WiFiMarauder::probeFloodLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_probe_locked();
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_MAR_RAWSNIFF)
  {
    WiFiMarauder::rawSniffLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_raw_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  else if (currentState == STATE_MAR_KARMA_LISTEN)
  {
    WiFiMarauder::karmaListenLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_karma_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  else if (currentState == STATE_MAR_KARMA_CLONE)
  {
    if (karmaCloneActive) WiFiMarauder::karmaCloneOnce();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_karma_locked();
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_MAR_ASSOC_SLEEP)
  {
    WiFiMarauder::assocSleepLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_assoc_locked();
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_MAR_BADMSG)
  {
    WiFiMarauder::badMsgLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_bad_locked();
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_MAR_SAE)
  {
    WiFiMarauder::saeLoop();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_sae_locked();
      xSemaphoreGive(lvgl_mutex);
    }
  }
  else if (currentState == STATE_MAR_PINGSCAN)
  {
    if (pingScanRunning && pingHandle == nullptr) {
      // Kick off next host
      if (!ping_kick_next()) {
        pingScanRunning = false;
        pingDirty = true;
      }
    }
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_ping_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  else if (currentState == STATE_MAR_PORTAL)
  {
    if (portalDNS) portalDNS->processNextRequest();
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      marauder_update_portal_locked();
      xSemaphoreGive(lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =================================================================
// LVGL Event Handlers — Main Screen
// All event handlers below run on Core 0 inside lv_timer_handler(),
// so LVGL calls here are already mutex-protected.
// =================================================================

// ---------------------------------------------------------------------
// void event_load_screen_scan(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_scan(lv_event_t *e)
{
  Print_Debug("event_load_screen_scan");
  lv_scr_load(ui_scrCC1101Stuff);
}

// ---------------------------------------------------------------------
// void event_load_screen_rcsw(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_rcsw(lv_event_t *e)
{
  Print_Debug("event_load_screen_rcsw");
  lv_scr_load(ui_scrRCSWMain);
}

// ---------------------------------------------------------------------
// void event_stop_audio(lv_event_t *e)
// ---------------------------------------------------------------------
void event_stop_audio(lv_event_t *e)
{
  Print_Debug("event_stop_audio");

  if (currentState == STATE_AUDIO_TEST)
  {
    currentState = STATE_IDLE;
    // MP3 playback retired
    lv_obj_add_flag(ui_btnMainStopMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_lblMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_sliderMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);
    now_close_sd_card();
  }
}

// ---------------------------------------------------------------------
// void event_set_volume_audio(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_volume_audio(lv_event_t *e)
{
  Print_Debug("event_set_volume_audio");

  // MP3 playback retired — volume slider is a no-op
}

// ---------------------------------------------------------------------
// void event_play_audio_test(lv_event_t *e)
// ---------------------------------------------------------------------
void event_play_audio_test(lv_event_t *e)
{
  Print_Debug("event_play_audio_test");

  if (sd_card_is_present())
  {
    // MP3 playback retired
    lv_label_set_text(ui_lblMainStatus, "MP3 playback removed");
    now_close_sd_card();

    // Place the device in adequat mode
    currentState = STATE_AUDIO_TEST;
  }
}

// ---------------------------------------------------------------------
// void event_load_screen_marauder(lv_event_t *e)
// Launches extended Marauder screen (AP/STA/PMKID/PKT/SIG/CH tabs).
// ---------------------------------------------------------------------
static void event_load_screen_marauder(lv_event_t *e)
{
  Print_Debug("event_load_screen_marauder");
  marauder_screen_load();
}

// ---------------------------------------------------------------------
// void event_load_screen_wifi_apps(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_wifi_apps(lv_event_t *e)
{
  Print_Debug("event_load_screen_wifi_apps");
  lv_scr_load(ui_scrWiFiMenu);
}

// ---------------------------------------------------------------------
// void event_load_screen_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_load_screen_protocol_analyzer");
  lv_scr_load(ui_scrProtAna);
}

// ---------------------------------------------------------------------
// void event_load_screen_flipper(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_flipper(lv_event_t *e)
{
  Print_Debug("event_load_screen_flipper");

  if (sd_card_is_present())
  {
    // Auto-create /captures folder if it doesn't exist
    if (!SD.exists("/captures")) {
      SD.mkdir("/captures");
    }

    refresh_sd_card_folder(ui_ddPresetsFolder, "/");

    // Find and select the "captures" folder in the dropdown
    int32_t capturesIdx = lv_dropdown_get_option_index(ui_ddPresetsFolder, "captures");
    if (capturesIdx >= 0) {
      lv_dropdown_set_selected(ui_ddPresetsFolder, capturesIdx);
    }

    // Show .sub files from the selected folder
    char *currentFolder = (char *)malloc(generaleSize * sizeof(char));
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, currentFolder, generaleSize);
    if (strcmp(currentFolder, "/") == 0) {
      refresh_sd_card_file(ui_ddPresetsFile, "/", ".sub", true);
    } else {
      char folderPath[128];
      snprintf(folderPath, sizeof(folderPath), "/%s", currentFolder);
      refresh_sd_card_file(ui_ddPresetsFile, folderPath, ".sub", true);
    }
    free(currentFolder);

    now_close_sd_card();
  }

  lv_scr_load(ui_scrPresets);
}

// ---------------------------------------------------------------------
// void event_load_screen_settings(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_settings(lv_event_t *e)
{
  Print_Debug("event_load_screen_settings");
  lv_scr_load(ui_scrSettings);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Event Present in Flipper Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_refresh_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_refresh_flipper_file(lv_event_t *e)
{
  Print_Debug("event_refresh_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "-Status-");

  if (sd_card_is_present())
  {

    char *currentFolder = (char *)malloc(generaleSize * sizeof(char));
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, currentFolder, generaleSize);

    if (strcmp(currentFolder, "/") == 0)
    {
      refresh_sd_card_file(ui_ddPresetsFile, "/", ".sub", true);
    }
    else
    {
      char folderPath[128];
      snprintf(folderPath, sizeof(folderPath), "/%s", currentFolder);
      refresh_sd_card_file(ui_ddPresetsFile, folderPath, ".sub", true);
    }

    now_close_sd_card();

    free(currentFolder);
  }
}

// ---------------------------------------------------------------------
// void event_select_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_select_flipper_file(lv_event_t *e)
{
  Print_Debug("event_select_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "-Status-");
}

// ---------------------------------------------------------------------
// Delete confirmation popup
// ---------------------------------------------------------------------
static char pendingDeletePath[256] = {0};

static void confirm_delete_cb(lv_event_t *e)
{
  lv_obj_t *mbox = lv_event_get_current_target(e);
  const char *btn_text = lv_msgbox_get_active_btn_text(mbox);
  if (btn_text == NULL) return;

  if (strcmp(btn_text, "Yes") == 0) {
    if (sd_card_is_present()) {
      if (SD.remove(pendingDeletePath)) {
        char statusBuf[80];
        snprintf(statusBuf, sizeof(statusBuf), "Deleted: %s", pendingDeletePath);
        lv_label_set_text(ui_lblPresetsStatus, statusBuf);
        Print_Debug(statusBuf);
      } else {
        lv_label_set_text(ui_lblPresetsStatus, "Delete failed!");
      }

      // Refresh the file list
      char *currentFolder = (char *)malloc(generaleSize * sizeof(char));
      lv_dropdown_get_selected_str(ui_ddPresetsFolder, currentFolder, generaleSize);
      if (strcmp(currentFolder, "/") == 0) {
        refresh_sd_card_file(ui_ddPresetsFile, "/", ".sub", true);
      } else {
        char folderPath[128];
        snprintf(folderPath, sizeof(folderPath), "/%s", currentFolder);
        refresh_sd_card_file(ui_ddPresetsFile, folderPath, ".sub", true);
      }
      free(currentFolder);

      now_close_sd_card();
    } else {
      lv_label_set_text(ui_lblPresetsStatus, "SD Card not found");
    }
  }

  lv_msgbox_close(mbox);
}

// ---------------------------------------------------------------------
// void event_delete_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_delete_flipper_file(lv_event_t *e)
{
  Print_Debug("event_delete_flipper_file");

  // Check if there's a file selected
  uint16_t fileCount = lv_dropdown_get_option_cnt(ui_ddPresetsFile);
  if (fileCount == 0) {
    lv_label_set_text(ui_lblPresetsStatus, "No file selected");
    return;
  }

  // Build full path from selected folder + file
  int folderIndex = lv_dropdown_get_selected(ui_ddPresetsFolder);
  char *folderbuffer = (char *)malloc(generaleSize * sizeof(char));
  char *filebuffer = (char *)malloc(generaleSize * sizeof(char));

  lv_dropdown_get_selected_str(ui_ddPresetsFolder, folderbuffer, generaleSize);
  lv_dropdown_get_selected_str(ui_ddPresetsFile, filebuffer, generaleSize);

  if (folderIndex == 0) {
    snprintf(pendingDeletePath, sizeof(pendingDeletePath), "/%s", filebuffer);
  } else {
    snprintf(pendingDeletePath, sizeof(pendingDeletePath), "/%s/%s", folderbuffer, filebuffer);
  }

  free(folderbuffer);
  free(filebuffer);

  // Show confirmation popup
  static const char *btns[] = {"Yes", "No", ""};
  char msgBuf[300];
  snprintf(msgBuf, sizeof(msgBuf), "Delete file?\n%s", pendingDeletePath);

  lv_obj_t *mbox = lv_msgbox_create(NULL, "Confirm Delete", msgBuf, btns, false);
  lv_obj_center(mbox);
  lv_obj_set_style_bg_color(mbox, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
  lv_obj_set_style_border_color(mbox, lv_color_hex(0xFF4444), LV_PART_MAIN);
  lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
  lv_obj_set_style_text_color(mbox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_add_event_cb(mbox, confirm_delete_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// ---------------------------------------------------------------------
// void event_refresh_flipper_list(lv_event_t *e)
// ---------------------------------------------------------------------
void event_refresh_flipper_list(lv_event_t *e)
{
  Print_Debug("event_refresh_flipper_list");

  if (sd_card_is_present())
  {
    refresh_sd_card_folder(ui_ddPresetsFolder, "/");

    // Re-select captures folder if it exists
    int32_t capturesIdx = lv_dropdown_get_option_index(ui_ddPresetsFolder, "captures");
    if (capturesIdx >= 0) {
      lv_dropdown_set_selected(ui_ddPresetsFolder, capturesIdx);
    }

    // Refresh files from selected folder
    char *currentFolder = (char *)malloc(generaleSize * sizeof(char));
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, currentFolder, generaleSize);
    if (strcmp(currentFolder, "/") == 0) {
      refresh_sd_card_file(ui_ddPresetsFile, "/", ".sub", true);
    } else {
      char folderPath[128];
      snprintf(folderPath, sizeof(folderPath), "/%s", currentFolder);
      refresh_sd_card_file(ui_ddPresetsFile, folderPath, ".sub", true);
    }
    free(currentFolder);

    now_close_sd_card();

    lv_label_set_text(ui_lblPresetsStatus, "Refreshed");
  }
  else
  {
    lv_label_set_text(ui_lblPresetsStatus, "SD Card not found");
  }
}

// ---------------------------------------------------------------------
// void event_send_tesla(lv_event_t *e)
// ---------------------------------------------------------------------
void event_send_tesla(lv_event_t *e)
{
  Print_Debug("event_send_tesla");

  if (currentState == STATE_IDLE)
  {
    chaos_status_set("Sending US Tesla..");
    currentState = STATE_TESLA_US;
  }
}

// ---------------------------------------------------------------------
// void event_send_flipper_file(lv_event_t *e)
// ---------------------------------------------------------------------
void event_send_flipper_file(lv_event_t *e)
{
  Print_Debug("event_send_flipper_file");

  lv_label_set_text(ui_lblPresetsStatus, "Please wait..");

  // Get the currently selected option
  int index = lv_dropdown_get_selected(ui_ddPresetsFolder);
  char *folderbuffer = (char *)malloc(generaleSize * sizeof(char));
  char *filebuffer = (char *)malloc(generaleSize * sizeof(char));

  lv_dropdown_get_selected_str(ui_ddPresetsFolder, folderbuffer, generaleSize);
  lv_dropdown_get_selected_str(ui_ddPresetsFile, filebuffer, generaleSize);

  char fullfilename[256];

  if (index == 0) // root path "/"
  {
    snprintf(fullfilename, sizeof(fullfilename), "/%s", filebuffer);
  }
  else // root path "/" + folder
  {
    snprintf(fullfilename, sizeof(fullfilename), "/%s/%s", folderbuffer, filebuffer);
  }

  free(folderbuffer);
  free(filebuffer);
  // End get selected option

  bool parsed = false;

  if (sd_card_is_present())
  {
    if (read_sd_card_flipper_file(fullfilename))
    {
      parsed = true;
    }
    now_close_sd_card();
  }

  if (parsed)
  {
    currentState = STATE_SEND_FLIPPER;
  }
  else
  {
    lv_label_set_text(ui_lblPresetsStatus, "ERROR: File Invalid !");
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Event Present in Settings Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_restart_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_restart_device(lv_event_t *e)
{
  Print_Debug("event_restart_device");

  ESP.restart();
}

// ---------------------------------------------------------------------
// void event_rotate_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rotate_device(lv_event_t *e)
{
  Print_Debug("event_rotate_device");

  // Rotate the LCD
  if (tft.getRotation() == 2)
  {
    tft.clearDisplay();
    tft.setRotation(0);
    ui_init();
    purge_all_dropdown_symbols();
    settings_built = false;
    settings_screen_build();
    fp_built = false;
    fp_screen_build();
  }
  else if (tft.getRotation() == 0)
  {
    tft.clearDisplay();
    tft.setRotation(2);
    ui_init();
    purge_all_dropdown_symbols();
    settings_built = false;
    settings_screen_build();
    fp_built = false;
    fp_screen_build();
  }
}

// ---------------------------------------------------------------------
// void event_set_brightness_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_brightness_device(lv_event_t *e)
{
  Print_Debug("event_set_brightness_device");

  tft.setBrightness(lv_slider_get_value(ui_sldBrightness));
}

// ---------------------------------------------------------------------
// void event_save_settings_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_save_settings_device(lv_event_t *e)
{
  Print_Debug("event_save_settings_device");
}

// ---------------------------------------------------------------------
// void event_enable_ota_device(lv_event_t *e)
// ---------------------------------------------------------------------
void event_enable_ota_device(lv_event_t *e)
{
  Print_Debug("event_enable_ota_device");

  OTAInProgress = 1;
  WiFi.softAP(ssid, password, wifi_channel);

  lv_label_set_text(ui_lblSettingsStatus, "OTA READY");
  lv_label_set_text(ui_lblSettingsIPAddr, "192.168.4.1"); // Took Out String(WiFi.softAPIP()).c_str()

  // Start OTA
  ArduinoOTA.begin();

  // Set a callback function to reset the ESP32 after the update is completed  (DOESENT WORK - TODO)

  // Local REST API starts after WiFi connects (see WiFi connect handlers above)
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in CC1101 Stuff Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_tab_change_cc1101_stuff(lv_event_t *e)
// ---------------------------------------------------------------------
void event_tab_change_cc1101_stuff(lv_event_t *e)
{
  Print_Debug("event_tab_change_cc1101_stuff");

  switch (lv_tabview_get_tab_act(ui_tabCC1101Stuff))
  {
  case 0: // Scan Tab — Scanner UI is hand-coded in ScannerScreen.h
    if (currentState == STATE_GENERATOR)
    {
      // Stop
      lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "ON/OFF");
      SUBGHZ.disableTransmit();
      currentState = STATE_IDLE;
    }
    break;
  case 1: // Gen Tab
    if (currentState == STATE_SCANNER)
    {
      SUBGHZ.disableScanner();
      currentState = STATE_IDLE;
    }

    if (currentState != STATE_GENERATOR)
    {
      lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "ON/OFF");
    }
    else
    {
      lv_obj_add_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "GEN ON");
    }

    break;
  case 2: // Rec/Play Tab
    break;
  case 3: // Config Tab
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_exit_cc1101_stuff(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_cc1101_stuff(lv_event_t *e)
{
  Print_Debug("event_exit_cc1101_stuff");

  currentState = STATE_IDLE;
  // Scanner widgets are hand-coded (ScannerScreen.h); state shown via scn struct
  lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblGenEnable, "ON/OFF");

  lv_scr_load(ui_scrMain);
}

// Scan TAB
// ---------------------------------------------------------------------
// void event_set_scan_preset_freq(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_scan_preset_freq(lv_event_t *e)
{
  Print_Debug("event_set_scan_preset_freq");

  char *currentFreq = (char *)malloc(generaleSize * sizeof(char));
  lv_dropdown_get_selected_str(ui_ddl1101ScanPreset, currentFreq, generaleSize);

  if (strcmp(currentFreq, "< Manual") == 0)
  {
    Print_Debug("Manual Mode Selected");
  }
  else
  {
    lv_textarea_set_text(ui_txtScanStartFq, currentFreq);
    char stopFqBuf[16];
    snprintf(stopFqBuf, sizeof(stopFqBuf), "%.2f", atof(currentFreq) + 10.00);
    lv_textarea_set_text(ui_txtScanStopFq, stopFqBuf);
  }

  free(currentFreq);
}

// ---------------------------------------------------------------------
// void event_clear_scanner(lv_event_t *e)
// ---------------------------------------------------------------------
void event_clear_scanner(lv_event_t *e)
{
  Print_Debug("event_clear_scanner");

  lv_textarea_set_text(ui_txtScannerData, "");
}

// ---------------------------------------------------------------------
// void event_start_stop_scanner(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_scanner(lv_event_t *e)
{
  Print_Debug("event_start_stop_scanner");

  if (currentState == STATE_IDLE)
  {
    // Start
    lv_textarea_set_cursor_click_pos(ui_txtScannerData, false);
    float start = atof(lv_textarea_get_text(ui_txtScanStartFq));
    float stop = atof(lv_textarea_get_text(ui_txtScanStopFq));
    lv_obj_add_state(ui_swScannerOn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblScanEnable, "SCAN ON");
    SUBGHZ.enableScanner(start, stop);
    currentState = STATE_SCANNER;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblScanEnable, "SCAN OFF");
    SUBGHZ.disableScanner();
    currentState = STATE_IDLE;
  }
}

// Packet Gen TAB
// ---------------------------------------------------------------------
// void event_set_gen_preset_freq(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_gen_preset_freq(lv_event_t *e)
{
  Print_Debug("event_set_gen_preset_freq");

  char *currentFreq = (char *)malloc(generaleSize * sizeof(char));
  lv_dropdown_get_selected_str(ui_ddl1101GenPreset, currentFreq, generaleSize);

  if (strcmp(currentFreq, "< Manual") == 0)
  {
    Print_Debug("Manual Mode Selected");
  }
  else
  {
    lv_textarea_set_text(ui_txt1101GenFreq, currentFreq);
  }

  free(currentFreq);
}

// ---------------------------------------------------------------------
// void event_start_stop_packet_gen(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_packet_gen(lv_event_t *e)
{
  Print_Debug("event_start_stop_packet_gen");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = atof(lv_textarea_get_text(ui_txt1101GenFreq));
    lv_obj_add_state(ui_swGenEnable, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblGenEnable, "GEN ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableTransmit();
    currentState = STATE_GENERATOR;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblGenEnable, "GEN OFF");
    SUBGHZ.disableTransmit();
    currentState = STATE_IDLE;
  }
}

// Rec/Play TAB
// ---------------------------------------------------------------------
// void event_set_preset_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset_rec_play(lv_event_t *e)
{
  Print_Debug("event_set_preset_rec_play");

  uint8_t index = lv_dropdown_get_selected(ui_Preset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_recplay_freq_preset(lv_event_t *e)
// ---------------------------------------------------------------------
void event_recplay_freq_preset(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED) return;

  uint16_t sel = lv_dropdown_get_selected(ui_ddlRecPlayFreqPreset);
  if (sel == 0) return; // "< Manual" — leave freq textarea as-is
  char buf[10];
  lv_dropdown_get_selected_str(ui_ddlRecPlayFreqPreset, buf, sizeof(buf));
  lv_textarea_set_text(ui_txtRecPlayFq, buf);
}

// ---------------------------------------------------------------------
// void event_stop_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_stop_rec_play(lv_event_t *e)
{
  if (currentState == STATE_CAPTURE)
  {
    SUBGHZ.disableReceiver();
    lv_obj_add_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_lblRecPlayStatus, "Capture Stopped");
    lv_obj_add_state(ui_btnStop, LV_STATE_DISABLED);
    currentState = STATE_IDLE;
  }
}

// ---------------------------------------------------------------------
// void event_capture_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_capture_rec_play(lv_event_t *e)
{
  Print_Debug("event_capture_rec_play");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = atof(lv_textarea_get_text(ui_txtRecPlayFq));
    lv_obj_add_flag(ui_indGreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_indRed, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui_txtRawData, "");
    lv_label_set_text(ui_lblProtocolID, "");
    lv_label_set_text(ui_lblRecPlayStatus, "Capture Started..");

    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableReceiver();
    lv_obj_clear_state(ui_btnStop, LV_STATE_DISABLED);
    currentState = STATE_CAPTURE;
  }
}

// ---------------------------------------------------------------------
// void event_playback_rec_play(lv_event_t *e)
// ---------------------------------------------------------------------
void event_playback_rec_play(lv_event_t *e)
{
  Print_Debug("event_playback_rec_play");

  if (currentState == STATE_IDLE)
  {
    // Green stays visible (buffer still has data to play again)
    float freq = atof(lv_textarea_get_text(ui_txtRecPlayFq));
    SUBGHZ.setFrequency(freq);

    currentState = STATE_PLAYBACK;
  }
  else
  {
    Print_Debug("NOT IDLE");
  }
}

// Config TAB
// ---------------------------------------------------------------------
// void event_set_modulation(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_modulation(lv_event_t *e)
{
  Print_Debug("event_set_modulation");

  // BUG FIX: was reading from ui_ddlCC1101PktFormat (packet format dropdown)
  // instead of the modulation dropdown. Modulation never got set correctly.
  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101ModType);

  switch (index)
  {
  case 0: // ASK/OOK
    SUBGHZ.setModulation(2);
    break;
  case 1: // 2-FSK
    SUBGHZ.setModulation(0);
    break;
  case 2: // GFSK
    SUBGHZ.setModulation(1);
    break;
  case 3: // 4-FSK
    SUBGHZ.setModulation(3);
    break;
  case 4: // MSK
    SUBGHZ.setModulation(4);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_set_packet_format(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_packet_format(lv_event_t *e)
{
  Print_Debug("event_set_packet_format");

  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101PktFormat);

  switch (index)
  {
  case 0: // NORMAL
    SUBGHZ.setPacketFormat(0);
    break;
  case 1: // SYNCHRONOUS
    SUBGHZ.setPacketFormat(1);
    break;
  case 2: // RANDOM TX
    SUBGHZ.setPacketFormat(2);
    break;
  case 3: // ASYNCHRONOUS
    SUBGHZ.setPacketFormat(3);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_set_preset(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset(lv_event_t *e)
{
  Print_Debug("event_set_preset");

  uint8_t index = lv_dropdown_get_selected(ui_Config1101Preset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 0);
    lv_arc_set_value(ui_arcScanBW, 650);
    lv_label_set_text(ui_lblRXBWArc, "650");
    lv_arc_set_value(ui_arcDeviation, 1);
    lv_label_set_text(ui_lblDeviation, "1");
    lv_arc_set_value(ui_arcDataRate, 4);
    lv_label_set_text(ui_lblDataRate, "4");
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 0);
    lv_arc_set_value(ui_arcScanBW, 270);
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 1);
    lv_label_set_text(ui_lblDeviation, "1");
    lv_arc_set_value(ui_arcDataRate, 4);
    lv_label_set_text(ui_lblDataRate, "4");
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 1);
    lv_arc_set_value(ui_arcScanBW, 270);
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 2);
    lv_label_set_text(ui_lblDeviation, "2");
    lv_arc_set_value(ui_arcDataRate, 5);
    lv_label_set_text(ui_lblDataRate, "5");
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    lv_dropdown_set_selected(ui_ddlCC1101ModType, 1);
    lv_arc_set_value(ui_arcScanBW, 270);
    lv_label_set_text(ui_lblRXBWArc, "270");
    lv_arc_set_value(ui_arcDeviation, 47);
    lv_label_set_text(ui_lblDeviation, "47");
    lv_arc_set_value(ui_arcDataRate, 5);
    lv_label_set_text(ui_lblDataRate, "5");
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_set_rx_bw(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_rx_bw(lv_event_t *e)
{
  Print_Debug("event_set_rx_bw");
  int bw = lv_arc_get_value(ui_arcScanBW);
  SUBGHZ.setRxBandwidth((float)bw);
}

// ---------------------------------------------------------------------
// void event_set_deviation(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_deviation(lv_event_t *e)
{
  Print_Debug("event_set_deviation");
  int dev = lv_arc_get_value(ui_arcDeviation);
  SUBGHZ.setDeviation((float)dev);
}

// ---------------------------------------------------------------------
// void event_set_data_rate(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_data_rate(lv_event_t *e)
{
  Print_Debug("event_set_data_rate");
  int drate = lv_arc_get_value(ui_arcDataRate);
  SUBGHZ.setDataRate((float)drate);
}

// ---------------------------------------------------------------------
// void event_set_tx_power(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_tx_power(lv_event_t *e)
{
  Print_Debug("event_set_tx_power");
  // Dropdown index to dBm: -30,-20,-15,-10,-6,0,5,7,10,12
  static const int paValues[] = {-30, -20, -15, -10, -6, 0, 5, 7, 10, 12};
  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101TxPower);
  if (index < sizeof(paValues) / sizeof(paValues[0])) {
    SUBGHZ.setPower(paValues[index]);
  }
}

// ---------------------------------------------------------------------
// void event_set_sync_mode(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_sync_mode(lv_event_t *e)
{
  Print_Debug("event_set_sync_mode");
  uint8_t index = lv_dropdown_get_selected(ui_ddlCC1101SyncMode);
  if (index <= 7) {
    SUBGHZ.setSyncMode(index);
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in Protocol Analyzer Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_set_preset_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_set_preset_analyzer(lv_event_t *e)
{
  Print_Debug("event_set_preset_analyzer");

  uint8_t index = lv_dropdown_get_selected(ui_ProtanaPreset);

  switch (index)
  {
  case 0:
    SUBGHZ.setPreset(AM650);
    break;
  case 1:
    SUBGHZ.setPreset(AM270);
    break;
  case 2:
    SUBGHZ.setPreset(FM238);
    break;
  case 3:
    SUBGHZ.setPreset(FM476);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void event_exit_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_exit_protocol_analyzer");
  currentState = STATE_IDLE;
  lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblProtAnaRXEn, "ON/OFF");
  lv_scr_load(ui_scrMain);
}

// ---------------------------------------------------------------------
// void event_start_stop_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_stop_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_start_stop_protocol_analyzer");

  if (currentState == STATE_IDLE)
  {
    // Start
    float freq = atof(lv_textarea_get_text(ui_txtMainFreq));
    lv_obj_add_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableRCSwitch();
    currentState = STATE_ANALYZER;
  }
  else
  {
    // Stop
    lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX OFF");
    SUBGHZ.disableRCSwitch();
    currentState = STATE_IDLE;
  }
}

// ---------------------------------------------------------------------
// void event_clear_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_clear_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_clear_protocol_analyzer");

  lv_textarea_set_text(ui_txtProtAnaBinary, "-");          // Binary
  lv_textarea_set_text(ui_txtProtAnaPulsLen, "-");         // Pulse Length
  lv_textarea_set_text(ui_txtProtAnaProtAnaTriState, "-"); // TriState
  lv_textarea_set_text(ui_txtProtAnaProtocol, "-");        // Protocol
  lv_textarea_set_text(ui_txtProtAnaResults, "");
  lv_textarea_set_text(ui_txtProtAnaBitLength, "-");
  lv_textarea_set_text(ui_txtProtAnaReceived, "");
  lv_label_set_text(ui_lblProtAnaProtID, "");
}

// ---------------------------------------------------------------------
// void event_replay_protocol_analyzer(lv_event_t *e)
// ---------------------------------------------------------------------
void event_replay_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_replay_protocol_analyzer");

  bool inRecordingMode = false;

  // Check if recording
  if (currentState == STATE_ANALYZER)
  {
    inRecordingMode = true;
    // Stop
    lv_obj_clear_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX OFF");
    SUBGHZ.disableRCSwitch();
    currentState = STATE_IDLE;
  }

  // Send Last Signal
  float freq = atof(lv_textarea_get_text(ui_txtMainFreq));
  SUBGHZ.setFrequency(freq);
  SUBGHZ.enableTransmit();
  SUBGHZ.sendLastSignal();
  SUBGHZ.disableTransmit();

  char txDbg[256];
  snprintf(txDbg, sizeof(txDbg), "Signal transmitted, value: %s (%s bit) - Protocol: %s - Frequency: %.2f mHz",
           lv_textarea_get_text(ui_txtProtAnaReceived),
           lv_textarea_get_text(ui_txtProtAnaBitLength),
           lv_textarea_get_text(ui_txtProtAnaProtocol), freq);
  Print_Debug(txDbg);

  // Back to recording
  if (inRecordingMode)
  {
    // Start
    float freq = atof(lv_textarea_get_text(ui_txtMainFreq));
    lv_obj_add_state(ui_swtProtAnaRxEn, LV_STATE_CHECKED);
    lv_label_set_text(ui_lblProtAnaRXEn, "RX ON");
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableRCSwitch();
    currentState = STATE_ANALYZER;
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in RC Switch Apps Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_rcsw_type_changed(lv_event_t *e) — show/hide panels for device type
// ---------------------------------------------------------------------
void event_rcsw_type_changed(lv_event_t *e)
{
  Print_Debug("event_rcsw_type_changed");
  uint16_t sel = lv_dropdown_get_selected(ui_ddlTenProto);

  // Hide all 10 DIP switches + bit labels
  lv_obj_t *switches[] = {ui_TenPoleSW0, ui_TenPoleSW1, ui_TenPoleSW2, ui_TenPoleSW3, ui_TenPoleSW4,
                           ui_TenPoleSW5, ui_TenPoleSW6, ui_TenPoleSW7, ui_TenPoleSW8, ui_TenPoleSW9};
  lv_obj_t *labels[] = {ui_lblBit0, ui_lblBit1, ui_lblBit2, ui_lblBit3, ui_lblBit4,
                         ui_lblBit5, ui_lblBit6, ui_lblBit7, ui_lblBit8, ui_lblBit9};
  for (int i = 0; i < 10; i++) {
    if (sel == 0) {
      lv_obj_clear_flag(switches[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(switches[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Hide all type panels
  lv_obj_add_flag(ui_panelTypeB, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_panelTypeC, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_panelTypeD, LV_OBJ_FLAG_HIDDEN);

  // Show the selected type panel
  if (sel == 1) lv_obj_clear_flag(ui_panelTypeB, LV_OBJ_FLAG_HIDDEN);
  else if (sel == 2) lv_obj_clear_flag(ui_panelTypeC, LV_OBJ_FLAG_HIDDEN);
  else if (sel == 3) lv_obj_clear_flag(ui_panelTypeD, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(ui_lblRCSWStatus, "-");
}

// ---------------------------------------------------------------------
// void event_rc_switch_send_on(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_on(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_on");

  float freq = atof(lv_textarea_get_text(ui_txt10PoleFreq));
  SUBGHZ.setFrequency(freq);
  SUBGHZ.enableTransmit();

  uint16_t type = lv_dropdown_get_selected(ui_ddlTenProto);
  char txBuf[48];

  switch (type) {
    case 0: { // Type A — DIP switches
      char firstFive[6], secondFive[6];
      snprintf(firstFive, sizeof(firstFive), "%s%s%s%s%s",
               lv_label_get_text(ui_lblBit0), lv_label_get_text(ui_lblBit1),
               lv_label_get_text(ui_lblBit2), lv_label_get_text(ui_lblBit3),
               lv_label_get_text(ui_lblBit4));
      snprintf(secondFive, sizeof(secondFive), "%s%s%s%s%s",
               lv_label_get_text(ui_lblBit5), lv_label_get_text(ui_lblBit6),
               lv_label_get_text(ui_lblBit7), lv_label_get_text(ui_lblBit8),
               lv_label_get_text(ui_lblBit9));
      SUBGHZ.switchOn(firstFive, secondFive);
      snprintf(txBuf, sizeof(txBuf), "TX ON A: %s%s", firstFive, secondFive);
      break;
    }
    case 1: { // Type B — Rotary
      int addr = lv_dropdown_get_selected(ui_ddlTypeBAddr) + 1;
      int chan = lv_dropdown_get_selected(ui_ddlTypeBChan) + 1;
      SUBGHZ.switchOnB(addr, chan);
      snprintf(txBuf, sizeof(txBuf), "TX ON B: Addr=%d Ch=%d", addr, chan);
      break;
    }
    case 2: { // Type C — Intertechno
      char family = 'a' + lv_dropdown_get_selected(ui_ddlTypeCFamily);
      int group = lv_dropdown_get_selected(ui_ddlTypeCGroup) + 1;
      int device = lv_dropdown_get_selected(ui_ddlTypeCDevice) + 1;
      SUBGHZ.switchOnC(family, group, device);
      snprintf(txBuf, sizeof(txBuf), "TX ON C: %c G%d D%d", family, group, device);
      break;
    }
    case 3: { // Type D — REV
      char group = 'A' + lv_dropdown_get_selected(ui_ddlTypeDGroup);
      int device = lv_dropdown_get_selected(ui_ddlTypeDDevice) + 1;
      SUBGHZ.switchOnD(group, device);
      snprintf(txBuf, sizeof(txBuf), "TX ON D: Grp=%c Dev=%d", group, device);
      break;
    }
  }

  SUBGHZ.disableTransmit();
  lv_label_set_text(ui_lblRCSWStatus, txBuf);
  Print_Debug(txBuf);
}

// ---------------------------------------------------------------------
// void event_rc_switch_send_off(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_off(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_off");

  float freq = atof(lv_textarea_get_text(ui_txt10PoleFreq));
  SUBGHZ.setFrequency(freq);
  SUBGHZ.enableTransmit();

  uint16_t type = lv_dropdown_get_selected(ui_ddlTenProto);
  char txBuf[48];

  switch (type) {
    case 0: { // Type A — DIP switches
      char firstFive[6], secondFive[6];
      snprintf(firstFive, sizeof(firstFive), "%s%s%s%s%s",
               lv_label_get_text(ui_lblBit0), lv_label_get_text(ui_lblBit1),
               lv_label_get_text(ui_lblBit2), lv_label_get_text(ui_lblBit3),
               lv_label_get_text(ui_lblBit4));
      snprintf(secondFive, sizeof(secondFive), "%s%s%s%s%s",
               lv_label_get_text(ui_lblBit5), lv_label_get_text(ui_lblBit6),
               lv_label_get_text(ui_lblBit7), lv_label_get_text(ui_lblBit8),
               lv_label_get_text(ui_lblBit9));
      SUBGHZ.switchOff(firstFive, secondFive);
      snprintf(txBuf, sizeof(txBuf), "TX OFF A: %s%s", firstFive, secondFive);
      break;
    }
    case 1: { // Type B — Rotary
      int addr = lv_dropdown_get_selected(ui_ddlTypeBAddr) + 1;
      int chan = lv_dropdown_get_selected(ui_ddlTypeBChan) + 1;
      SUBGHZ.switchOffB(addr, chan);
      snprintf(txBuf, sizeof(txBuf), "TX OFF B: Addr=%d Ch=%d", addr, chan);
      break;
    }
    case 2: { // Type C — Intertechno
      char family = 'a' + lv_dropdown_get_selected(ui_ddlTypeCFamily);
      int group = lv_dropdown_get_selected(ui_ddlTypeCGroup) + 1;
      int device = lv_dropdown_get_selected(ui_ddlTypeCDevice) + 1;
      SUBGHZ.switchOffC(family, group, device);
      snprintf(txBuf, sizeof(txBuf), "TX OFF C: %c G%d D%d", family, group, device);
      break;
    }
    case 3: { // Type D — REV
      char group = 'A' + lv_dropdown_get_selected(ui_ddlTypeDGroup);
      int device = lv_dropdown_get_selected(ui_ddlTypeDDevice) + 1;
      SUBGHZ.switchOffD(group, device);
      snprintf(txBuf, sizeof(txBuf), "TX OFF D: Grp=%c Dev=%d", group, device);
      break;
    }
  }

  SUBGHZ.disableTransmit();
  lv_label_set_text(ui_lblRCSWStatus, txBuf);
  Print_Debug(txBuf);
}

// ---------------------------------------------------------------------
// void event_raw_tx_send(lv_event_t *e) — send arbitrary RCSwitch code
// ---------------------------------------------------------------------
void event_raw_tx_send(lv_event_t *e)
{
  Print_Debug("event_raw_tx_send");

  // Read frequency and set CC1101
  float freq = atof(lv_textarea_get_text(ui_txtRawFreq));
  SUBGHZ.setFrequency(freq);

  unsigned long code = strtoul(lv_textarea_get_text(ui_txtRawCode), NULL, 10);
  unsigned int bitLen = atoi(lv_textarea_get_text(ui_txtRawBitLen));
  int pulseLen = atoi(lv_textarea_get_text(ui_txtRawPulseLen));
  int proto = lv_dropdown_get_selected(ui_ddlRawProtocol) + 1; // dropdown 0-indexed, protocols 1-indexed

  // Repeat: dropdown options are "1\n5\n10\n15\n20\n50"
  static const int repeatValues[] = {1, 5, 10, 15, 20, 50};
  int repeatIdx = lv_dropdown_get_selected(ui_ddlRawRepeat);
  int repeat = repeatValues[repeatIdx < 6 ? repeatIdx : 2];

  // Validate
  if (code == 0 && strcmp(lv_textarea_get_text(ui_txtRawCode), "0") != 0) {
    lv_label_set_text(ui_lblRawStatus, "Invalid code!");
    return;
  }
  if (bitLen < 1 || bitLen > 32) {
    lv_label_set_text(ui_lblRawStatus, "Bits must be 1-32");
    return;
  }

  SUBGHZ.enableTransmit();
  SUBGHZ.sendRaw(code, bitLen, proto, pulseLen, repeat);
  SUBGHZ.disableTransmit();

  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf), "TX: %lu (%dbit) P%d", code, bitLen, proto);
  lv_label_set_text(ui_lblRawStatus, statusBuf);
  Print_Debug(statusBuf);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events in WiFi Screen
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------
// void event_start_wifi_scan(lv_event_t *e)
// ---------------------------------------------------------------------
void event_start_wifi_scan(lv_event_t *e)
{
  Print_Debug("event_start_wifi_scan");

  lv_label_set_text(ui_lblWifiScanNetsFound, "Scanning..");

  // Reset scan state
  scanFinished = false;
  WiFi.scanDelete();

  // Clear the textarea
  lv_textarea_set_text(ui_txtWifiScanNetsFound, "");

  // Clear the dropdown
  lv_dropdown_clear_options(ui_ddlWifiSSID);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);  // Ensure STA mode (may be OFF after BLE/exit cleanup)

  WiFi.onEvent(WiFiEvent);

  // Start async WiFi scan (result checked via scanFinished flag)
  WiFi.scanNetworks(true);
  scanStartTime = millis();

  currentState = STATE_WIFI_SCAN;
}

// ---------------------------------------------------------------------
// void event_refresh_wifi(lv_event_t *e)
// ---------------------------------------------------------------------
void event_refresh_wifi(lv_event_t *e)
{
  Print_Debug("event_refresh_wifi");

  int16_t result = WiFi.scanComplete();

  if (result > 0)
  {
    lv_textarea_set_text(ui_txtWifiScanNetsFound, "");

    int index = lv_dropdown_get_selected(ui_ddlWifiSSID);

    // Bounds check: dropdown selection must be within scan results
    if (index >= result) index = 0;

    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    uint8_t *bssid;
    int32_t channel;

    WiFi.getNetworkInfo(index, ssid, encryptionType, rssi, bssid, channel);

    // Guard against NULL bssid pointer (can happen if scan data is stale)
    if (bssid != NULL) {
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

      char infoBuf[256];
      snprintf(infoBuf, sizeof(infoBuf), "SSID: %s\nMAC: %s\nRSSI: %d dBm\nChannel: %d\nEncryption Type: %s\n\n",
               ssid.c_str(), mac, rssi, channel, GetEncryptionTypeString(encryptionType));
      lv_textarea_add_text(ui_txtWifiScanNetsFound, infoBuf);
    }
  }
}

// ---------------------------------------------------------------------
// WiFi Join keyboard callback — handles Enter (READY) and Cancel
// Runs on Core 0 (LVGL event context) — already mutex-protected
// ---------------------------------------------------------------------
static void wifi_join_keyboard_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY) {
    // User pressed Enter — extract password and connect
    const char *pwd = lv_textarea_get_text(wifiJoinTextarea);

    // Get selected SSID from dropdown
    lv_dropdown_get_selected_str(ui_ddlWifiSSID, wifiJoinSSID, sizeof(wifiJoinSSID));

    // Disconnect any prior connection, ensure STA mode. Use disconnect(false,
    // false) to NOT also erase the IDF-level saved creds — Arduino's default
    // erases them when the second arg is true, and some flows were passing
    // unintended booleans that wiped them.
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);  // explicit: IDF should also persist creds (backup)
    WiFi.onEvent(WiFiEvent);

    // Reset connection flags
    wifiConnected = false;
    wifiGotIP = false;

    // Save credentials to NVS BEFORE starting connect, so a long/failed
    // connect doesn't leave us with creds unsaved.
    bool prefsOpen = prefs.begin("wifi", false);
    bool ssidOk = prefs.putString("ssid", wifiJoinSSID) > 0;
    bool passOk = prefs.putString("pass", pwd) > 0;
    prefs.end();
    Serial.printf("[WiFi NVS] save: begin=%s ssid=%s pass=%s ssid='%s' pass_len=%u\n",
                  prefsOpen ? "OK" : "FAIL",
                  ssidOk    ? "OK" : "FAIL",
                  passOk    ? "OK" : "FAIL",
                  wifiJoinSSID, (unsigned)strlen(pwd));

    // Start connection
    WiFi.begin(wifiJoinSSID, pwd);
    wifiConnectStartTime = millis();

    // Update UI
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "Connecting to %s...", wifiJoinSSID);
    lv_label_set_text(ui_lblWifiScanNetsFound, statusBuf);

    // Delete keyboard overlay
    if (wifiJoinPanel) {
      lv_obj_del(wifiJoinPanel);
      wifiJoinPanel = NULL;
      wifiJoinTextarea = NULL;
      wifiJoinKeyboard = NULL;
    }

    currentState = STATE_WIFI_CONNECTING;
  }
  else if (code == LV_EVENT_CANCEL) {
    // User pressed Cancel/X — dismiss overlay
    if (wifiJoinPanel) {
      lv_obj_del(wifiJoinPanel);
      wifiJoinPanel = NULL;
      wifiJoinTextarea = NULL;
      wifiJoinKeyboard = NULL;
    }
  }
}

// ---------------------------------------------------------------------
// void event_join_wifi(lv_event_t *e)
// Show password keyboard overlay, then connect to selected WiFi network
// ---------------------------------------------------------------------
void event_join_wifi(lv_event_t *e)
{
  Print_Debug("event_join_wifi");

  if (currentState != STATE_IDLE) return;

  // Check that a network is selected in the dropdown
  char ssidCheck[33];
  lv_dropdown_get_selected_str(ui_ddlWifiSSID, ssidCheck, sizeof(ssidCheck));
  if (strlen(ssidCheck) == 0) {
    lv_label_set_text(ui_lblWifiScanNetsFound, "Scan first, then select a network");
    return;
  }

  // Destroy any leftover overlay
  if (wifiJoinPanel) {
    lv_obj_del(wifiJoinPanel);
    wifiJoinPanel = NULL;
  }

  // Create fullscreen overlay panel on the WiFi screen
  wifiJoinPanel = lv_obj_create(ui_scrWifiApps);
  lv_obj_set_size(wifiJoinPanel, 320, 480);
  lv_obj_set_align(wifiJoinPanel, LV_ALIGN_CENTER);
  lv_obj_clear_flag(wifiJoinPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(wifiJoinPanel, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(wifiJoinPanel, 255, LV_PART_MAIN);
  lv_obj_set_style_bg_img_src(wifiJoinPanel, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
  lv_obj_set_style_border_width(wifiJoinPanel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(wifiJoinPanel, 0, LV_PART_MAIN);

  // Title
  lv_obj_t *titleLbl = lv_label_create(wifiJoinPanel);
  lv_obj_set_align(titleLbl, LV_ALIGN_TOP_MID);
  lv_obj_set_y(titleLbl, 15);
  lv_label_set_text(titleLbl, "Enter WiFi Password");
  lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFF9100), LV_PART_MAIN);
  lv_obj_set_style_text_font(titleLbl, &ui_font_Verdana16, LV_PART_MAIN);

  // SSID label
  lv_obj_t *ssidLbl = lv_label_create(wifiJoinPanel);
  lv_obj_set_align(ssidLbl, LV_ALIGN_TOP_MID);
  lv_obj_set_y(ssidLbl, 45);
  char instrBuf[64];
  snprintf(instrBuf, sizeof(instrBuf), "Network: %s", ssidCheck);
  lv_label_set_text(ssidLbl, instrBuf);
  lv_obj_set_style_text_color(ssidLbl, lv_color_hex(0xFCFCFC), LV_PART_MAIN);
  lv_obj_set_style_text_font(ssidLbl, &ui_font_Verdana14, LV_PART_MAIN);

  // Password textarea
  wifiJoinTextarea = lv_textarea_create(wifiJoinPanel);
  lv_obj_set_width(wifiJoinTextarea, 280);
  lv_obj_set_height(wifiJoinTextarea, LV_SIZE_CONTENT);
  lv_obj_set_align(wifiJoinTextarea, LV_ALIGN_TOP_MID);
  lv_obj_set_y(wifiJoinTextarea, 75);
  lv_textarea_set_max_length(wifiJoinTextarea, 63);
  lv_textarea_set_one_line(wifiJoinTextarea, true);
  lv_textarea_set_password_mode(wifiJoinTextarea, true);
  lv_textarea_set_placeholder_text(wifiJoinTextarea, "Password...");
  lv_obj_set_style_text_color(wifiJoinTextarea, lv_color_hex(0x00FF0C), LV_PART_MAIN);
  lv_obj_set_style_text_font(wifiJoinTextarea, &ui_font_Verdana14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(wifiJoinTextarea, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(wifiJoinTextarea, 255, LV_PART_MAIN);
  lv_obj_set_style_border_color(wifiJoinTextarea, lv_color_hex(0x444466), LV_PART_MAIN);
  lv_obj_set_style_border_opa(wifiJoinTextarea, 255, LV_PART_MAIN);
  lv_obj_set_style_border_width(wifiJoinTextarea, 1, LV_PART_MAIN);

  // Full QWERTY keyboard (lv_keyboard, not btnmatrix — need alphanumeric)
  wifiJoinKeyboard = lv_keyboard_create(wifiJoinPanel);
  lv_obj_set_width(wifiJoinKeyboard, 320);
  lv_obj_set_height(wifiJoinKeyboard, 240);
  lv_obj_set_align(wifiJoinKeyboard, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_y(wifiJoinKeyboard, 0);
  lv_obj_set_style_bg_color(wifiJoinKeyboard, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(wifiJoinKeyboard, 255, LV_PART_MAIN);

  // Connect keyboard to textarea
  lv_keyboard_set_textarea(wifiJoinKeyboard, wifiJoinTextarea);

  // Register Enter and Cancel events
  lv_obj_add_event_cb(wifiJoinKeyboard, wifi_join_keyboard_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(wifiJoinKeyboard, wifi_join_keyboard_cb, LV_EVENT_CANCEL, NULL);
}

// ---------------------------------------------------------------------
// void event_exit_wifi_screen(lv_event_t *e)
// ---------------------------------------------------------------------
void event_exit_wifi_screen(lv_event_t *e)
{
  Print_Debug("event_exit_wifi_screen");

  // Clean up keyboard overlay if still open
  if (wifiJoinPanel) {
    lv_obj_del(wifiJoinPanel);
    wifiJoinPanel = NULL;
    wifiJoinTextarea = NULL;
    wifiJoinKeyboard = NULL;
  }

  // Stop any active Marauder feature
  if (currentState == STATE_WIFI_SNIFF) {
    WiFiMarauder::deinit();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_BEACON_FLOOD) {
    WiFiMarauder::deinitActive();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_DEAUTH_RUN) {
    WiFiMarauder::deinitActive();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_WIFI_CONNECTING) {
    // Abort in-progress connection attempt
    WiFi.disconnect(true);
    currentState = STATE_IDLE;
  } else if (currentState == STATE_MAR_STA_SCAN) {
    WiFiMarauder::stopStationScan();
    WiFiMarauder::deinit();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_MAR_PMKID) {
    WiFiMarauder::stopPMKIDScan();
    WiFiMarauder::deinit();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_MAR_PKTGRAPH) {
    WiFiMarauder::stopSniff();
    WiFiMarauder::deinit();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_MAR_CHANANA) {
    WiFiMarauder::stopChannelAnalyzer();
    WiFiMarauder::deinit();
    currentState = STATE_IDLE;
  } else if (currentState == STATE_MAR_APSCAN) {
    currentState = STATE_IDLE;
  } else if (currentState >= STATE_MAR_PWN && currentState <= STATE_MAR_PORTAL) {
    mar_stop_all_ops();
    currentState = STATE_IDLE;
  }

  // Clean up scan results but keep WiFi connection and event handler alive
  WiFi.scanDelete();

  // Only disconnect WiFi if NOT already connected (preserve active connection)
  if (!wifiGotIP) {
    WiFi.removeEvent(WiFiEvent);
    WiFi.disconnect(true);
  }

  lv_scr_load(ui_scrMain);
}


// ---------------------------------------------------------------------
// void event_save_capture_rec_play(lv_event_t * e);
// Show save dialog with editable filename
// ---------------------------------------------------------------------
void event_save_capture_rec_play(lv_event_t * e)
{
  Print_Debug("event_save_capture_rec_play");

  extern int samplecount;
  if (samplecount < 30) {
    lv_label_set_text(ui_lblRecPlayStatus, "No capture data to save");
    return;
  }

  // Generate default filename (without .sub extension)
  char defaultName[64];
  SUBGHZ.getDefaultFilename(defaultName, sizeof(defaultName));

  // Populate the textbox and show the save panel
  lv_textarea_set_text(ui_txtSaveFilename, defaultName);
  lv_obj_clear_flag(ui_panelSaveCapture, LV_OBJ_FLAG_HIDDEN);

  // Create keyboard attached to textarea
  if (keyboardSaveCapture == NULL) {
    keyboardSaveCapture = lv_keyboard_create(ui_panelSaveCapture);
    lv_keyboard_set_textarea(keyboardSaveCapture, ui_txtSaveFilename);
    lv_obj_set_size(keyboardSaveCapture, 320, 220);
    lv_obj_align(keyboardSaveCapture, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(keyboardSaveCapture, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboardSaveCapture, lv_color_hex(0x333355), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboardSaveCapture, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
  }
  lv_obj_clear_flag(keyboardSaveCapture, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------
// void event_save_filename_input(lv_event_t * e);
// Handles keyboard READY (save) and CANCEL (dismiss)
// ---------------------------------------------------------------------
void event_save_filename_input(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY) {
    // User pressed OK — save the file
    const char *filename = lv_textarea_get_text(ui_txtSaveFilename);
    if (strlen(filename) > 0) {
      char fullname[64];
      snprintf(fullname, sizeof(fullname), "%s.sub", filename);
      SUBGHZ.saveCaptureToSD(fullname);
    }

    // Hide panel and keyboard
    lv_obj_add_flag(ui_panelSaveCapture, LV_OBJ_FLAG_HIDDEN);
    if (keyboardSaveCapture != NULL) {
      lv_obj_del(keyboardSaveCapture);
      keyboardSaveCapture = NULL;
    }
    lv_obj_clear_state(ui_txtSaveFilename, LV_STATE_FOCUSED);
    lv_indev_reset(NULL, ui_txtSaveFilename);
  }
  else if (code == LV_EVENT_CANCEL) {
    // User pressed X — cancel
    lv_label_set_text(ui_lblRecPlayStatus, "Save cancelled");
    lv_obj_add_flag(ui_panelSaveCapture, LV_OBJ_FLAG_HIDDEN);
    if (keyboardSaveCapture != NULL) {
      lv_obj_del(keyboardSaveCapture);
      keyboardSaveCapture = NULL;
    }
    lv_obj_clear_state(ui_txtSaveFilename, LV_STATE_FOCUSED);
    lv_indev_reset(NULL, ui_txtSaveFilename);
  }
}

// ---------------------------------------------------------------------
// void fcnBleScreen(lv_event_t * e);
// ---------------------------------------------------------------------
void fcnBleScreen(lv_event_t * e)
{
  Print_Debug("event_load_screen_ble");
  lv_scr_load(ui_scrBLEMenu);
}

// ---------------------------------------------------------------------
// void event_exit_ble_screen(lv_event_t * e);
// ---------------------------------------------------------------------
void event_exit_ble_screen(lv_event_t * e)
{
  Print_Debug("event_exit_ble_screen");

  if (currentState == STATE_SEND_BLESPAM) {
    BLEstop();
  }
  if (currentState == STATE_BLE_SCAN_RUN || currentState == STATE_BLE_SCAN_INIT) {
    BLEscanStop();
  }
  BLEdeinit();

  // Reset BLE Spam tab
  bleSpamCount = 0;
  lv_label_set_text(ui_lblBLEStatus, "Ready");
  lv_label_set_text(ui_lblBLECount, "Packets: 0");
  lv_textarea_set_text(ui_txtBLELog, "");
  lv_obj_clear_state(ui_btnBLEStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnBLEStop, LV_STATE_DISABLED);

  // Reset BLE Scan tab
  lv_label_set_text(ui_lblBLEScanStatus, "Ready");
  lv_obj_set_style_text_color(ui_lblBLEScanStatus, lv_color_hex(0xDEFF00), 0);
  lv_label_set_text(ui_lblBLEScanCount, "Devices: 0");
  lv_textarea_set_text(ui_txtBLEScanResults, "");
  lv_obj_clear_state(ui_btnBLEScanStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnBLEScanStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
  lv_scr_load(ui_scrMain);
}

// ---------------------------------------------------------------------
// void fcnBLEToggle(lv_event_t * e);
// Legacy — switch replaced by START/STOP buttons
// ---------------------------------------------------------------------
void fcnBLEToggle(lv_event_t * e)
{
  // No longer used — BLE toggle replaced by START/STOP buttons
}

// ---------------------------------------------------------------------
// void fcnTouchTunes(lv_event_t * e);
// ---------------------------------------------------------------------
void fcnTouchTunes(lv_event_t * e)
{
  Print_Debug("event_load_screen_touchtunes");
  currentState = STATE_IDLE;
  lv_scr_load(ui_scrTouchTunes);
}

// ---------------------------------------------------------------------
// void event_load_remote_screen(lv_event_t * e);
// ---------------------------------------------------------------------
void event_load_remote_screen(lv_event_t * e)
{
  Print_Debug("event_load_remote_screen");
  currentState = STATE_IDLE;
  remote_refreshProfileList();
  lv_scr_load(ui_scrRemote);
}

// ---------------------------------------------------------------------
// void fcnBLEType(lv_event_t * e);
// Device type dropdown changed
// ---------------------------------------------------------------------
void fcnBLEType(lv_event_t * e)
{
  Print_Debug("fcnBLEType");

  uint16_t sel = lv_dropdown_get_selected(ui_ddlWifiSSID1);

  if (sel >= BLE_PAYLOAD_COUNT) {
    bleRandomMode = true;
  } else {
    bleRandomMode = false;
    if (bleInitialized) {
      BLEsetPayload(sel);
    }
  }
}

// ---------------------------------------------------------------------
// void event_ble_start(lv_event_t * e);
// ---------------------------------------------------------------------
void event_ble_start(lv_event_t * e)
{
  Print_Debug("event_ble_start");

  if (currentState != STATE_IDLE) return;

  // Read device selection from dropdown
  uint16_t sel = lv_dropdown_get_selected(ui_ddlWifiSSID1);
  if (sel >= BLE_PAYLOAD_COUNT) {
    bleRandomMode = true;
    bleCurrentDevice = 0;
  } else {
    bleRandomMode = false;
    bleCurrentDevice = sel;
  }

  // Reset counters and update UI
  bleSpamCount = 0;
  lv_textarea_set_text(ui_txtBLELog, "");
  lv_label_set_text(ui_lblBLEStatus, "Initializing BLE...");
  lv_label_set_text(ui_lblBLECount, "Packets: 0");

  lv_obj_add_state(ui_btnBLEStart, LV_STATE_DISABLED);
  lv_obj_clear_state(ui_btnBLEStop, LV_STATE_DISABLED);

  // Defer heavy BLE init to main loop on Core 1.
  // BLEDevice::init() takes ~500ms and would trigger interrupt watchdog
  // if called here (inside lv_timer_handler on Core 0).
  currentState = STATE_BLE_INIT;
}

// ---------------------------------------------------------------------
// void event_ble_stop(lv_event_t * e);
// ---------------------------------------------------------------------
void event_ble_stop(lv_event_t * e)
{
  Print_Debug("event_ble_stop");

  BLEstop();

  lv_label_set_text(ui_lblBLEStatus, "Stopped");
  lv_obj_clear_state(ui_btnBLEStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnBLEStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
}

// ---------------------------------------------------------------------
// void event_ble_scan_start(lv_event_t * e);
// Start BLE device scan — defers BLE init to Core 1
// ---------------------------------------------------------------------
void event_ble_scan_start(lv_event_t * e)
{
  Print_Debug("event_ble_scan_start");

  if (currentState != STATE_IDLE) return;

  // Read duration from dropdown: 0=5s, 1=10s, 2=30s
  static const int durations[] = {5, 10, 30};
  uint8_t sel = lv_dropdown_get_selected(ui_ddlBLEScanDuration);
  bleScanDuration = (sel < 3) ? durations[sel] : 5;

  lv_textarea_set_text(ui_txtBLEScanResults, "");
  lv_label_set_text(ui_lblBLEScanStatus, "Initializing BLE...");
  lv_label_set_text(ui_lblBLEScanCount, "Devices: 0");
  lv_obj_add_state(ui_btnBLEScanStart, LV_STATE_DISABLED);
  lv_obj_clear_state(ui_btnBLEScanStop, LV_STATE_DISABLED);

  currentState = STATE_BLE_SCAN_INIT;
}

// ---------------------------------------------------------------------
// void event_ble_scan_stop(lv_event_t * e);
// Stop an ongoing BLE scan
// ---------------------------------------------------------------------
void event_ble_scan_stop(lv_event_t * e)
{
  Print_Debug("event_ble_scan_stop");

  BLEscanStop();

  lv_label_set_text(ui_lblBLEScanStatus, "Stopped");
  lv_obj_clear_state(ui_btnBLEScanStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnBLEScanStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
}

// ---------------------------------------------------------------------
// void event_beacon_start(lv_event_t * e);
// Start beacon flood — AP mode required
// ---------------------------------------------------------------------
void event_beacon_start(lv_event_t * e)
{
  Print_Debug("event_beacon_start");

  if (currentState != STATE_IDLE) return;

  int mode = lv_dropdown_get_selected(ui_ddlBeaconMode);

  WiFiMarauder::initActive();
  WiFiMarauder::startBeaconFlood(mode);

  lv_label_set_text(ui_lblBeaconStatus, "Flooding...");
  lv_obj_set_style_text_color(ui_lblBeaconStatus, lv_color_hex(0x00FF00), 0);
  lv_label_set_text(ui_lblBeaconCount, "Beacons: 0");
  lv_textarea_set_text(ui_txtBeaconLog, "");

  lv_obj_add_state(ui_btnBeaconStart, LV_STATE_DISABLED);
  lv_obj_clear_state(ui_btnBeaconStop, LV_STATE_DISABLED);

  currentState = STATE_BEACON_FLOOD;
}

// ---------------------------------------------------------------------
// void event_beacon_stop(lv_event_t * e);
// Stop beacon flood
// ---------------------------------------------------------------------
void event_beacon_stop(lv_event_t * e)
{
  Print_Debug("event_beacon_stop");

  WiFiMarauder::stopBeaconFlood();
  WiFiMarauder::deinitActive();

  lv_label_set_text(ui_lblBeaconStatus, "Stopped");
  lv_obj_set_style_text_color(ui_lblBeaconStatus, lv_color_hex(0xFAFF00), 0);
  lv_obj_clear_state(ui_btnBeaconStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnBeaconStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
}

// ---------------------------------------------------------------------
// void event_deauth_scan(lv_event_t * e);
// Scan for deauth targets (uses STA mode temporarily)
// ---------------------------------------------------------------------
void event_deauth_scan(lv_event_t * e)
{
  Print_Debug("event_deauth_scan");

  if (currentState != STATE_IDLE) return;

  lv_label_set_text(ui_lblDeauthStatus, "Scanning...");
  lv_obj_set_style_text_color(ui_lblDeauthStatus, lv_color_hex(0x00FFEB), 0);
  lv_textarea_set_text(ui_txtDeauthLog, "Scanning for targets...\n");

  currentState = STATE_DEAUTH_SCAN;
}

// ---------------------------------------------------------------------
// void event_deauth_start(lv_event_t * e);
// Start deauth attack on selected target — AP mode required
// ---------------------------------------------------------------------
void event_deauth_start(lv_event_t * e)
{
  Print_Debug("event_deauth_start");

  if (currentState != STATE_IDLE) return;

  int sel = lv_dropdown_get_selected(ui_ddlDeauthTarget);
  if (sel < 0 || sel >= WiFiMarauder::targetCount) {
    lv_label_set_text(ui_lblDeauthStatus, "No target!");
    lv_obj_set_style_text_color(ui_lblDeauthStatus, lv_color_hex(0xFF0000), 0);
    return;
  }

  WiFiMarauder::initActive();
  WiFiMarauder::startDeauth(sel);

  char buf[64];
  snprintf(buf, sizeof(buf), "Target: %s", WiFiMarauder::targets[sel].ssid);
  lv_label_set_text(ui_lblDeauthStatus, buf);
  lv_obj_set_style_text_color(ui_lblDeauthStatus, lv_color_hex(0x00FF00), 0);
  lv_label_set_text(ui_lblDeauthCount, "Packets: 0");
  lv_textarea_set_text(ui_txtDeauthLog, "");

  lv_obj_add_state(ui_btnDeauthStart, LV_STATE_DISABLED);
  lv_obj_clear_state(ui_btnDeauthStop, LV_STATE_DISABLED);

  currentState = STATE_DEAUTH_RUN;
}

// ---------------------------------------------------------------------
// void event_deauth_stop(lv_event_t * e);
// Stop deauth attack
// ---------------------------------------------------------------------
void event_deauth_stop(lv_event_t * e)
{
  Print_Debug("event_deauth_stop");

  WiFiMarauder::stopDeauth();
  WiFiMarauder::deinitActive();

  lv_label_set_text(ui_lblDeauthStatus, "Stopped");
  lv_obj_set_style_text_color(ui_lblDeauthStatus, lv_color_hex(0xFAFF00), 0);
  lv_obj_clear_state(ui_btnDeauthStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnDeauthStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
}

// ---------------------------------------------------------------------
// void event_sniff_start(lv_event_t * e);
// Start promiscuous mode packet sniffer
// ---------------------------------------------------------------------
void event_sniff_start(lv_event_t * e)
{
  Print_Debug("event_sniff_start");

  if (currentState != STATE_IDLE) return;

  WiFiMarauder::init();
  WiFiMarauder::startSniff();

  lv_label_set_text(ui_lblSniffStatus, "Sniffing...");
  lv_obj_set_style_text_color(ui_lblSniffStatus, lv_color_hex(0x00FF00), 0);
  lv_label_set_text(ui_lblSniffStats, "Mgmt:0  Data:0  Probe:0");
  lv_label_set_text(ui_lblSniffChannel, "Ch: 1");
  lv_textarea_set_text(ui_txtSniffLog, "");

  lv_obj_add_state(ui_btnSniffStart, LV_STATE_DISABLED);
  lv_obj_clear_state(ui_btnSniffStop, LV_STATE_DISABLED);

  currentState = STATE_WIFI_SNIFF;
}

// ---------------------------------------------------------------------
// void event_sniff_stop(lv_event_t * e);
// Stop promiscuous mode packet sniffer
// ---------------------------------------------------------------------
void event_sniff_stop(lv_event_t * e)
{
  Print_Debug("event_sniff_stop");

  WiFiMarauder::stopSniff();
  WiFiMarauder::deinit();

  lv_label_set_text(ui_lblSniffStatus, "Stopped");
  lv_obj_set_style_text_color(ui_lblSniffStatus, lv_color_hex(0xFAFF00), 0);
  lv_obj_clear_state(ui_btnSniffStart, LV_STATE_DISABLED);
  lv_obj_add_state(ui_btnSniffStop, LV_STATE_DISABLED);

  currentState = STATE_IDLE;
}