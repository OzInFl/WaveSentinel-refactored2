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
#include "Misc/AuthorizedDevices.h"
#include "Display/Display.h"
#include "SubGhz/SubGhz.h"
#include "WiFi/WiFix.h"
#include "BLE/BLE.h"
#include "WiFi/WiFiMarauder.h"
#include "SD/SDCard.h"
#include "Display/TouchTunesScreen.h"
#include "Display/StatusBar.h"

#include "Arduino.h"
#include "Audio.h"
#include "esp_bt.h"
#include <Preferences.h>




// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DECLARE
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// SubGhz Class
SubGhz SUBGHZ;

// WiFi scan timeout tracking
unsigned long scanStartTime = 0;
const unsigned long WIFI_SCAN_TIMEOUT_MS = 15000; // 15 seconds

// BLE scan duration (set by event handler, used by state machine)
int bleScanDuration = 5;

// Audio I2S Definitions
Audio audio;

// Preferences Library
Preferences prefs;

// Device serial number (populated in setup from ESP32 eFuse MAC)
static char deviceSerial[13] = {0};
static bool deviceAuthorized = true;

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
// Auth-aware splash screen click handler
// Replaces the SquareLine-generated ui_event_scrSplash at runtime
// ---------------------------------------------------------------------
void auth_splash_event(lv_event_t * e) {
    if (deviceAuthorized) {
        _ui_screen_change(&ui_scrMain, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, &ui_scrMain_screen_init);
    }
}

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

  
  // I2S Stuff
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

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

  Print_Debug("Initializing Default Value...");

  // Read chip serial from eFuse MAC
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceSerial, sizeof(deviceSerial), "%012llX", chipId);
  Print_Debug("Device SN: %s", deviceSerial);

  // Show version + serial on splash with black background for readability
  char splashInfo[64];
  snprintf(splashInfo, sizeof(splashInfo), "v%d.%d.%d  SN:%s",
           APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, deviceSerial);
  lv_label_set_text(ui_lblVersion, splashInfo);
  lv_obj_set_style_bg_color(ui_lblVersion, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblVersion, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblVersion, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Authorization check (empty list = all devices allowed)
  if (AUTHORIZED_COUNT > 0) {
    deviceAuthorized = false;
    for (int i = 0; i < AUTHORIZED_COUNT; i++) {
      if (strcmp(deviceSerial, AUTHORIZED_SERIALS[i]) == 0) {
        deviceAuthorized = true;
        break;
      }
    }
  }

  // Replace splash click handler with auth-aware version
  lv_obj_remove_event_cb(ui_scrSplash, ui_event_scrSplash);
  lv_obj_add_event_cb(ui_scrSplash, auth_splash_event, LV_EVENT_CLICKED, NULL);

  // Black background on splash labels for readability over image
  lv_obj_set_style_bg_color(ui_lblSplashStatus, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblSplashStatus, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblSplashStatus, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_lblSplash, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_lblSplash, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui_lblSplash, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

  if (deviceAuthorized) {
    lv_label_set_text(ui_lblSplashStatus, "TAP ANYWHERE TO BEGIN");
  } else {
    lv_label_set_text(ui_lblSplashStatus, "UNAUTHORIZED DEVICE");
    lv_label_set_text(ui_lblSplash, "Contact admin with SN above");
    lv_obj_set_style_text_color(ui_lblSplashStatus, lv_color_hex(0xFF4444),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  }

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

  // Build dynamic TouchTunes remote screen (no SquareLine license needed)
  tt_screen_init();

  // Wire the TouchTunes button (not wired in SquareLine)
  lv_obj_add_event_cb(ui_btnMainTTunes, fcnTouchTunes, LV_EVENT_CLICKED, NULL);

  // Persistent status bar (WiFi + battery icons) on lv_layer_top()
  statusbar_init();

  xTaskCreatePinnedToCore(Task_Refresh_Screen, "Task_Refresh_Screen", 20000, NULL, 1, NULL, 0);

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

  // Handle OTA updates when enabled
  if (OTAInProgress == 1)
  {
    ArduinoOTA.handle();
    server.handleClient();
  }

  // --- State machine: each state handles its RF/BLE/WiFi work ---

  if (currentState == STATE_AUDIO_TEST)
  {
    audio.loop();
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
        xSemaphoreGive(lvgl_mutex);
      }
      currentState = STATE_IDLE;
    }
    vTaskDelay(1);
  }
  else if (currentState == STATE_PLAYBACK)
  {
    if (SUBGHZ.sendCapture())
    {
      currentState = STATE_IDLE;
    }
  }
  else if (currentState == STATE_SCANNER)
  {
    // ScannerLoop() reads LVGL widgets (arcs, labels) and writes to textarea
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      SUBGHZ.ScannerLoop();
      xSemaphoreGive(lvgl_mutex);
    }
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
    SUBGHZ.setFrequency(310.00);
    SUBGHZ.enableTransmit();
    if (SUBGHZ.send_tesla())
    {
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblPresetsStatus, "Sending US Tesla..");
        xSemaphoreGive(lvgl_mutex);
      }
    }
    SUBGHZ.disableTransmit();
    currentState = STATE_TESLA_EU;
  }
  else if (currentState == STATE_TESLA_EU)
  {
    SUBGHZ.setFrequency(433.92);
    SUBGHZ.enableTransmit();
    if (SUBGHZ.send_tesla())
    {
      if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(ui_lblPresetsStatus, "Sending Tesla Complete !");
        xSemaphoreGive(lvgl_mutex);
      }
    }
    SUBGHZ.disableTransmit();
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

    audio.stopSong();

    // Hide Stop Button / Volume
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

  if (currentState == STATE_AUDIO_TEST)
  {
    // Set Volume from slider
    audio.setVolume(lv_slider_get_value(ui_sliderMainVolumeMp3));
  }
}

// ---------------------------------------------------------------------
// void event_play_audio_test(lv_event_t *e)
// ---------------------------------------------------------------------
void event_play_audio_test(lv_event_t *e)
{
  Print_Debug("event_play_audio_test");

  if (sd_card_is_present())
  {
    // Set Volume from slider
    audio.setVolume(lv_slider_get_value(ui_sliderMainVolumeMp3));

    // Open and play music test file
    if (!audio.connecttoFS(SD, "/test.mp3"))
    {
      lv_label_set_text(ui_lblMainStatus, "Missing test.mp3");
      now_close_sd_card();
    }

    // Show Stop Button / Volume
    lv_obj_clear_flag(ui_btnMainStopMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_lblMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_sliderMainVolumeMp3, LV_OBJ_FLAG_HIDDEN);

    // Place the device in adequat mode
    currentState = STATE_AUDIO_TEST;
  }
}

// ---------------------------------------------------------------------
// void event_load_screen_wifi_apps(lv_event_t *e)
// ---------------------------------------------------------------------
void event_load_screen_wifi_apps(lv_event_t *e)
{
  Print_Debug("event_load_screen_wifi_apps");
  lv_scr_load(ui_scrWifiApps);
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

  char fullpath[256];
  if (folderIndex == 0) {
    snprintf(fullpath, sizeof(fullpath), "/%s", filebuffer);
  } else {
    snprintf(fullpath, sizeof(fullpath), "/%s/%s", folderbuffer, filebuffer);
  }

  free(folderbuffer);
  free(filebuffer);

  // Delete the file
  if (sd_card_is_present())
  {
    if (SD.remove(fullpath)) {
      char statusBuf[80];
      snprintf(statusBuf, sizeof(statusBuf), "Deleted: %s", fullpath);
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
  }
  else
  {
    lv_label_set_text(ui_lblPresetsStatus, "SD Card not found");
  }
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
    lv_label_set_text(ui_lblPresetsStatus, "Sending US Tesla..");
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
  }
  else if (tft.getRotation() == 0)
  {
    tft.clearDisplay();
    tft.setRotation(2);
    ui_init();
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

  // Handle firmware update via web page
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.onNotFound(handleRoot);
  server.begin();
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
  case 0: // Scan Tab
    if (currentState == STATE_GENERATOR)
    {
      // Stop
      lv_obj_clear_state(ui_swGenEnable, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblGenEnable, "ON/OFF");
      SUBGHZ.disableTransmit();
      currentState = STATE_IDLE;
    }

    if (currentState != STATE_SCANNER)
    {
      lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "ON/OFF");
    }
    else
    {
      lv_obj_add_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "SCAN ON");
    }

    break;
  case 1: // Gen Tab
    if (currentState == STATE_SCANNER)
    {
      // Stop
      lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
      lv_label_set_text(ui_lblScanEnable, "ON/OFF");
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
  lv_obj_clear_state(ui_swScannerOn, LV_STATE_CHECKED);
  lv_label_set_text(ui_lblScanEnable, "ON/OFF");

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
// void event_rc_switch_send_on(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_on(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_on");

  float freq = atof(lv_textarea_get_text(ui_txt10PoleFreq));
  SUBGHZ.setFrequency(freq);

  lv_label_set_text(ui_lblRCSWStatus, "TX 'On' Command.");

  char firstFive[6], secondFive[6];
  snprintf(firstFive, sizeof(firstFive), "%s%s%s%s%s",
           lv_label_get_text(ui_lblBit0), lv_label_get_text(ui_lblBit1),
           lv_label_get_text(ui_lblBit2), lv_label_get_text(ui_lblBit3),
           lv_label_get_text(ui_lblBit4));
  snprintf(secondFive, sizeof(secondFive), "%s%s%s%s%s",
           lv_label_get_text(ui_lblBit5), lv_label_get_text(ui_lblBit6),
           lv_label_get_text(ui_lblBit7), lv_label_get_text(ui_lblBit8),
           lv_label_get_text(ui_lblBit9));

  SUBGHZ.enableTransmit();
  SUBGHZ.switchOn(firstFive, secondFive);
  SUBGHZ.disableTransmit();

  char txBuf[32];
  snprintf(txBuf, sizeof(txBuf), "TX ON: %s%s", firstFive, secondFive);
  lv_label_set_text(ui_lblRCSWStatus, txBuf);

  char dbgBuf[96];
  snprintf(dbgBuf, sizeof(dbgBuf), "Send switch ON, value: %s%s - Frequency: %.2f mHz", firstFive, secondFive, freq);
  Print_Debug(dbgBuf);
}

// ---------------------------------------------------------------------
// void event_rc_switch_send_off(lv_event_t *e)
// ---------------------------------------------------------------------
void event_rc_switch_send_off(lv_event_t *e)
{
  Print_Debug("event_rc_switch_send_off");

  float freq = atof(lv_textarea_get_text(ui_txt10PoleFreq));
  SUBGHZ.setFrequency(freq);

  char freqBuf[16];
  snprintf(freqBuf, sizeof(freqBuf), "%.2f", SUBGHZ.getFrequency());
  lv_label_set_text(ui_lblRCSWStatus, freqBuf); //"TX 'Off' Command.");

  char firstFive[6], secondFive[6];
  snprintf(firstFive, sizeof(firstFive), "%s%s%s%s%s",
           lv_label_get_text(ui_lblBit0), lv_label_get_text(ui_lblBit1),
           lv_label_get_text(ui_lblBit2), lv_label_get_text(ui_lblBit3),
           lv_label_get_text(ui_lblBit4));
  snprintf(secondFive, sizeof(secondFive), "%s%s%s%s%s",
           lv_label_get_text(ui_lblBit5), lv_label_get_text(ui_lblBit6),
           lv_label_get_text(ui_lblBit7), lv_label_get_text(ui_lblBit8),
           lv_label_get_text(ui_lblBit9));

  SUBGHZ.enableTransmit();
  SUBGHZ.switchOff(firstFive, secondFive);
  SUBGHZ.disableTransmit();

  char txBuf[32];
  snprintf(txBuf, sizeof(txBuf), "TX OFF: %s%s", firstFive, secondFive);
  lv_label_set_text(ui_lblRCSWStatus, txBuf);

  char dbgBuf[96];
  snprintf(dbgBuf, sizeof(dbgBuf), "Send switch OFF, value: %s%s - Frequency: %.2f mHz", firstFive, secondFive, freq);
  Print_Debug(dbgBuf);
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

    // Disconnect any prior connection, ensure STA mode
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiEvent);

    // Reset connection flags
    wifiConnected = false;
    wifiGotIP = false;

    // Start connection
    WiFi.begin(wifiJoinSSID, pwd);
    wifiConnectStartTime = millis();

    // Save credentials to NVS
    prefs.begin("wifi", false);
    prefs.putString("ssid", wifiJoinSSID);
    prefs.putString("pass", pwd);
    prefs.end();

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
  }

  // delete old config
  WiFi.scanDelete();
  WiFi.removeEvent(WiFiEvent);

  // Only disconnect WiFi if NOT already connected (preserve active connection)
  if (!wifiGotIP) {
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
  lv_scr_load(ui_scrBLEApps);
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