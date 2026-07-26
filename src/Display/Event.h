#ifndef Event_h
#define Event_h

// ---------------------------------------------------------------
// Event.h — LVGL keyboard/event handlers and state machine
//
// Contains:
// - WaveSentinelState enum (state machine for main loop)
// - Numeric keyboard creation + callback functions for each screen
//   (Protocol Analyzer, RC Switch, Scanner, Generator, Rec/Play)
//
// All keyboard event handlers run on Core 0 inside lv_timer_handler()
// so they are already protected by lvgl_mutex.
// ---------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>

#include "Misc/Config.h"

#include "Arduino.h"

enum WaveSentinelState
{
  STATE_IDLE,
  STATE_GENERATOR,
  STATE_ANALYZER,
  STATE_SCANNER,
  STATE_CAPTURE,
  STATE_PLAYBACK,
  STATE_TESLA_US,
  STATE_TESLA_EU,
  STATE_AUDIO_TEST,
  STATE_SEND_FLIPPER,
  STATE_WIFI_SCAN,
  STATE_BLE_INIT,        // Deferred BLE init — runs on Core 1 to avoid watchdog timeout
  STATE_SEND_BLESPAM,
  STATE_SEND_TOUCHTUNES,
  STATE_WIFI_SNIFF,      // Promiscuous mode packet sniffer
  STATE_BEACON_FLOOD,    // Beacon spam loop (AP mode)
  STATE_DEAUTH_SCAN,     // Scanning for deauth targets
  STATE_DEAUTH_RUN,      // Sending deauth frames
  STATE_BLE_SCAN_INIT,   // Deferred BLE init for scanner
  STATE_BLE_SCAN_RUN,    // BLE scanning in progress
  STATE_WIFI_CONNECTING, // WiFi STA connection in progress
  STATE_SEND_REMOTE,     // Universal Remote: send .sub file (RF via CC1101)
  STATE_SEND_IR,         // Universal Remote: send .ir file (IR via LED)
  // -- Marauder extended features --
  STATE_MAR_APSCAN,      // Active AP scan for Marauder targets screen
  STATE_MAR_STA_SCAN,    // Promiscuous station enumeration
  STATE_MAR_PMKID,       // EAPOL/PMKID passive capture
  STATE_MAR_PKTGRAPH,    // Live packet-count strip chart
  STATE_MAR_SIGMON,      // Signal monitor (per-AP RSSI)
  STATE_MAR_CHANANA,     // Channel analyzer histogram
  STATE_MAR_PWN,         // Pwnagotchi detection viewer
  STATE_MAR_MACTRACK,    // MAC track (follow a station's RSSI)
  STATE_MAR_PROBEFLOOD,  // Probe-request flood
  STATE_MAR_RAWSNIFF,    // Raw frame header dumper
  STATE_MAR_KARMA_LISTEN,// Karma: collect probe SSIDs
  STATE_MAR_KARMA_CLONE, // Karma: clone collected SSIDs via beacons
  STATE_MAR_ASSOC_SLEEP, // Association sleep attack
  STATE_MAR_BADMSG,      // Bad msg action frame flood
  STATE_MAR_SAE,         // SAE Commit / Commit flood
  STATE_MAR_PINGSCAN,    // /24 ICMP scan
  STATE_MAR_PORTAL,      // Evil Portal AP + captive page
  // ---- Marauder BLE feature batch ----
  STATE_BLE_MAR_INIT,
  STATE_BLE_MAR_AIRTAG,
  STATE_BLE_MAR_MONITOR,
  STATE_BLE_MAR_SPOOF,
  STATE_BLE_MAR_SKIMMER,
  STATE_BLE_MAR_FLOCK,
  STATE_BLE_MAR_META,
  STATE_BLE_MAR_ANALYZER,
  STATE_BLE_MAR_SOURAPPLE,
  STATE_BLE_MAR_SWIFTPAIR,
  STATE_BLE_MAR_SPAMPLUS,
  STATE_PAGER,           // Pager (POCSAG/FLEX) receive + decode loop
};

// Current State
uint8_t currentState = STATE_IDLE;

// General Char * Size
uint32_t generaleSize = 1024;

static lv_obj_t *keyboardProtocolAnalyzer = NULL;
static lv_obj_t *keyboardRCSW = NULL;
static lv_obj_t *keyboardCC1101Stuff = NULL;
static lv_obj_t *keyboardSaveCapture = NULL;
static lv_obj_t *keyboardRawTx = NULL;
static lv_obj_t *rawTxFocusedTextarea = NULL;  // tracks which RAW TX textarea is active

static const char *mapNum[] = {"1", "2", "3", "\n",
                                     "4", "5", "6", "\n",
                                     "7", "8", "9", "\n",
                                     "0", ".", "<X", "\n",
                                     "OK", ""};

// ---------------------------------------------------------------------
// void KeyboardProtocolAnalyzer(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardProtocolAnalyzer(lv_event_t *e)
{
  Print_Debug("KeyboardProtocolAnalyzer");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txtMainFreq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txtMainFreq);
    lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtMainFreq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_event_send(ui_txtMainFreq, LV_EVENT_READY, NULL);
  }
  else
  {
    lv_textarea_add_text(ui_txtMainFreq, txt);
    lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtMainFreq));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_protocol_analyzer(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_protocol_analyzer(lv_event_t *e)
{
  Print_Debug("event_keyboard_protocol_analyzer");

  if (currentState == STATE_IDLE)
  {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {
      lv_textarea_set_cursor_click_pos(ui_txtMainFreq, false);
      lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtMainFreq));
      lv_obj_clear_flag(ui_panelProtAnaFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardProtocolAnalyzer == NULL) {
        keyboardProtocolAnalyzer = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardProtocolAnalyzer, 320, 360);
        lv_obj_set_style_bg_color(keyboardProtocolAnalyzer, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardProtocolAnalyzer, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardProtocolAnalyzer, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardProtocolAnalyzer, LV_ALIGN_CENTER, 0, 58);
        lv_obj_add_event_cb(keyboardProtocolAnalyzer, KeyboardProtocolAnalyzer, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardProtocolAnalyzer, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardProtocolAnalyzer, mapNum);
      }
      lv_obj_clear_flag(keyboardProtocolAnalyzer, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelProtAnaFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardProtocolAnalyzer != NULL) {
        lv_obj_del(keyboardProtocolAnalyzer);
        keyboardProtocolAnalyzer = NULL;
      }
      lv_obj_clear_state(ui_txtMainFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtMainFreq);
      lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void KeyboardRCSW(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardRCSW(lv_event_t *e)
{
  Print_Debug("KeyboardRCSW");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txt10PoleFreq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txt10PoleFreq);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt10PoleFreq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(ui_txt10PoleFreq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(ui_txt10PoleFreq, txt);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt10PoleFreq));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_rcsw(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_rcsw(lv_event_t *e)
{
  Print_Debug("event_keyboard_rcsw");
  if (currentState == STATE_IDLE)
  {

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {
      lv_textarea_set_cursor_click_pos(ui_txt10PoleFreq, false);
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt10PoleFreq));
      lv_obj_clear_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardRCSW == NULL) {
        keyboardRCSW = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardRCSW, 320, 315);
        lv_obj_set_style_bg_color(keyboardRCSW, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardRCSW, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardRCSW, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardRCSW, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardRCSW, KeyboardRCSW, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardRCSW, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardRCSW, mapNum);
      }
      lv_obj_clear_flag(keyboardRCSW, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardRCSW != NULL) {
        lv_obj_del(keyboardRCSW);
        keyboardRCSW = NULL;
      }
      lv_obj_clear_state(ui_txt10PoleFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txt10PoleFreq);
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void KeyboardRawTx(lv_event_t * e) — btnmatrix callback for RAW TX
// ---------------------------------------------------------------------
static void KeyboardRawTx(lv_event_t *e)
{
  Print_Debug("KeyboardRawTx");
  if (rawTxFocusedTextarea == NULL) return;

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(rawTxFocusedTextarea, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(rawTxFocusedTextarea);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(rawTxFocusedTextarea));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(rawTxFocusedTextarea, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK) return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(rawTxFocusedTextarea, txt);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(rawTxFocusedTextarea));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_raw_tx(lv_event_t * e) — show/hide keyboard for RAW TX textareas
// ---------------------------------------------------------------------
void event_keyboard_raw_tx(lv_event_t *e)
{
  Print_Debug("event_keyboard_raw_tx");
  if (currentState == STATE_IDLE)
  {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED)
    {
      rawTxFocusedTextarea = target;
      lv_textarea_set_cursor_click_pos(target, false);
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, lv_textarea_get_text(target));
      lv_obj_clear_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardRawTx == NULL) {
        keyboardRawTx = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardRawTx, 320, 315);
        lv_obj_set_style_bg_color(keyboardRawTx, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardRawTx, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardRawTx, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardRawTx, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardRawTx, KeyboardRawTx, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardRawTx, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardRawTx, mapNum);
      }
      lv_obj_clear_flag(keyboardRawTx, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardRawTx != NULL) {
        lv_obj_del(keyboardRawTx);
        keyboardRawTx = NULL;
      }
      if (rawTxFocusedTextarea != NULL) {
        lv_obj_clear_state(rawTxFocusedTextarea, LV_STATE_FOCUSED);
        lv_indev_reset(NULL, rawTxFocusedTextarea);
      }
      rawTxFocusedTextarea = NULL;
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void KeyboardScanStart(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardScanStart(lv_event_t *e)
{
  Print_Debug("KeyboardScanStart");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txtScanStartFq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txtScanStartFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStartFq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(ui_txtScanStartFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(ui_txtScanStartFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStartFq));
  }
}

// ---------------------------------------------------------------------
// void KeyboardScanStop(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardScanStop(lv_event_t *e)
{
  Print_Debug("KeyboardScanStop");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txtScanStopFq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txtScanStopFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStopFq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(ui_txtScanStopFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(ui_txtScanStopFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStopFq));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_scanner_start(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_scanner_start(lv_event_t *e)
{
  Print_Debug("event_keyboard_scanner_start");
  if (currentState == STATE_IDLE)
  {

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {
      lv_textarea_set_cursor_click_pos(ui_txtScanStartFq, false);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStartFq));
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardCC1101Stuff == NULL) {
        keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardScanStart, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
      }
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardCC1101Stuff != NULL) {
        lv_obj_del(keyboardCC1101Stuff);
        keyboardCC1101Stuff = NULL;
      }
      lv_obj_clear_state(ui_txtScanStartFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtScanStartFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_scanner_stop(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_scanner_stop(lv_event_t *e)
{
  Print_Debug("event_keyboard_scanner_stop");
  if (currentState == STATE_IDLE)
  {

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {
    lv_textarea_set_cursor_click_pos(ui_txtScanStopFq, false);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtScanStopFq));
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardCC1101Stuff == NULL) {
        keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardScanStop, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
      }
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardCC1101Stuff != NULL) {
        lv_obj_del(keyboardCC1101Stuff);
        keyboardCC1101Stuff = NULL;
      }
      lv_obj_clear_state(ui_txtScanStopFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtScanStopFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void KeyboardPacketGen(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardPacketGen(lv_event_t *e)
{
  Print_Debug("KeyboardPacketGen");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txt1101GenFreq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txt1101GenFreq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt1101GenFreq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(ui_txt1101GenFreq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(ui_txt1101GenFreq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt1101GenFreq));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_packet_generator(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_packet_generator(lv_event_t *e)
{
  Print_Debug("event_keyboard_packet_generator");
  if (currentState == STATE_IDLE)
  {

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {
      lv_textarea_set_cursor_click_pos(ui_txt1101GenFreq, false);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txt1101GenFreq));
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardCC1101Stuff == NULL) {
        keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardPacketGen, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
      }
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardCC1101Stuff != NULL) {
        lv_obj_del(keyboardCC1101Stuff);
        keyboardCC1101Stuff = NULL;
      }
      lv_obj_clear_state(ui_txt1101GenFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txt1101GenFreq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, "");
    }
  }
}

// ---------------------------------------------------------------------
// void KeyboardRecPlay(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardRecPlay(lv_event_t *e)
{
  Print_Debug("KeyboardRecPlay");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txtRecPlayFq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, "<X") == 0)
  {
    lv_textarea_del_char(ui_txtRecPlayFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtRecPlayFq));
  }
  else if (strcmp(txt, "OK") == 0)
  {
    lv_res_t res = lv_event_send(ui_txtRecPlayFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    vTaskDelay(1);
  }
  else
  {
    lv_textarea_add_text(ui_txtRecPlayFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtRecPlayFq));
  }
}

// ---------------------------------------------------------------------
// void event_keyboard_rec_play(lv_event_t * e)
// ---------------------------------------------------------------------
void event_keyboard_rec_play(lv_event_t *e)
{
  Print_Debug("event_keyboard_rec_play");
  if (currentState == STATE_IDLE)
  {

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED)
    {    
      lv_textarea_set_cursor_click_pos(ui_txtRecPlayFq, false);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, lv_textarea_get_text(ui_txtRecPlayFq));
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      if (keyboardCC1101Stuff == NULL) {
        keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
        lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
        lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
        lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardRecPlay, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
      }
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (keyboardCC1101Stuff != NULL) {
        lv_obj_del(keyboardCC1101Stuff);
        keyboardCC1101Stuff = NULL;
      }
      lv_obj_clear_state(ui_txtRecPlayFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtRecPlayFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, "");
    }
  }
}

#endif
