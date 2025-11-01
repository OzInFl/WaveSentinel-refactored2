#ifndef Event_h
#define Event_h

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
  STATE_SEND_BLESPAM,
};

// Current State
uint8_t currentState = STATE_IDLE;

// General Char * Size
uint32_t generaleSize = 1024;

static lv_obj_t *keyboardProtocolAnalyzer;
static lv_obj_t *keyboardRCSW;
static lv_obj_t *keyboardCC1101Stuff;

static const char *mapNum[] = {"1", "2", "3", "\n",
                                     "4", "5", "6", "\n",
                                     "7", "8", "9", "\n",
                                     "0", ".", LV_SYMBOL_BACKSPACE, "\n",
                                     LV_SYMBOL_OK, ""};

// ---------------------------------------------------------------------
// void KeyboardProtocolAnalyzer(lv_event_t * e)
// ---------------------------------------------------------------------
static void KeyboardProtocolAnalyzer(lv_event_t *e)
{
  Print_Debug("KeyboardProtocolAnalyzer");

  lv_obj_t *obj = lv_event_get_target(e);
  lv_textarea_set_cursor_pos(ui_txtMainFreq, 100);

  const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txtMainFreq);
    lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtMainFreq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_event_send(ui_txtMainFreq, LV_EVENT_READY, NULL);
  }
  else
  {
    lv_textarea_add_text(ui_txtMainFreq, txt);
    lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtMainFreq)).c_str());
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
      lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtMainFreq)).c_str());
      lv_obj_clear_flag(ui_panelProtAnaFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardProtocolAnalyzer = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardProtocolAnalyzer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardProtocolAnalyzer, 320, 360);
      lv_obj_set_style_bg_color(keyboardProtocolAnalyzer, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardProtocolAnalyzer, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardProtocolAnalyzer, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardProtocolAnalyzer, LV_ALIGN_CENTER, 0, 58);
      lv_obj_add_event_cb(keyboardProtocolAnalyzer, KeyboardProtocolAnalyzer, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardProtocolAnalyzer, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardProtocolAnalyzer, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelProtAnaFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardProtocolAnalyzer, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardProtocolAnalyzer);
      lv_obj_clear_state(ui_txtMainFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtMainFreq);
      lv_label_set_text(ui_lblProtAnaFreqKeyboardValueUnits, String("").c_str());
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

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txt10PoleFreq);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt10PoleFreq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_res_t res = lv_event_send(ui_txt10PoleFreq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    delay(50);
  }
  else
  {
    lv_textarea_add_text(ui_txt10PoleFreq, txt);
    lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt10PoleFreq)).c_str());
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
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt10PoleFreq)).c_str());
      lv_obj_clear_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardRCSW = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardRCSW, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardRCSW, 320, 315);
      lv_obj_set_style_bg_color(keyboardRCSW, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardRCSW, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardRCSW, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardRCSW, LV_ALIGN_CENTER, 0, 33);
      lv_obj_add_event_cb(keyboardRCSW, KeyboardRCSW, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardRCSW, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardRCSW, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelRCSWFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardRCSW, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardRCSW);
      lv_obj_clear_state(ui_txt10PoleFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txt10PoleFreq);
      lv_label_set_text(ui_lblRCSWFreqKeyboardValueUnits, String("").c_str());
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

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txtScanStartFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStartFq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_res_t res = lv_event_send(ui_txtScanStartFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    delay(50);
  }
  else
  {
    lv_textarea_add_text(ui_txtScanStartFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStartFq)).c_str());
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

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txtScanStopFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStopFq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_res_t res = lv_event_send(ui_txtScanStopFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    delay(50);
  }
  else
  {
    lv_textarea_add_text(ui_txtScanStopFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStopFq)).c_str());
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
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStartFq)).c_str());
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
      lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardScanStart, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardCC1101Stuff);
      lv_obj_clear_state(ui_txtScanStartFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtScanStartFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String("").c_str());
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
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtScanStopFq)).c_str());
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
      lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardScanStop, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardCC1101Stuff);
      lv_obj_clear_state(ui_txtScanStopFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtScanStopFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String("").c_str());
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

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txt1101GenFreq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt1101GenFreq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_res_t res = lv_event_send(ui_txt1101GenFreq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    delay(50);
  }
  else
  {
    lv_textarea_add_text(ui_txt1101GenFreq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt1101GenFreq)).c_str());
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
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txt1101GenFreq)).c_str());
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
      lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardPacketGen, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardCC1101Stuff);
      lv_obj_clear_state(ui_txt1101GenFreq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txt1101GenFreq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String("").c_str());
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

  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
  {
    lv_textarea_del_char(ui_txtRecPlayFq);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtRecPlayFq)).c_str());
  }
  else if (strcmp(txt, LV_SYMBOL_OK) == 0)
  {
    lv_res_t res = lv_event_send(ui_txtRecPlayFq, LV_EVENT_READY, NULL);
    if (res != LV_RES_OK)
      return;
    delay(50);
  }
  else
  {
    lv_textarea_add_text(ui_txtRecPlayFq, txt);
    lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtRecPlayFq)).c_str());
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
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String(lv_textarea_get_text(ui_txtRecPlayFq)).c_str());
      lv_obj_clear_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);

      keyboardCC1101Stuff = lv_btnmatrix_create(lv_scr_act());
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keyboardCC1101Stuff, 320, 315);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_border_color(keyboardCC1101Stuff, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_color(keyboardCC1101Stuff, lv_color_hex(0xF0F0F0), LV_PART_ITEMS);
      lv_obj_align(keyboardCC1101Stuff, LV_ALIGN_CENTER, 0, 33);
      lv_obj_add_event_cb(keyboardCC1101Stuff, KeyboardRecPlay, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_clear_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      lv_btnmatrix_set_map(keyboardCC1101Stuff, mapNum);
    }
    else if (code == LV_EVENT_READY)
    {
      lv_obj_add_flag(ui_panelCC1101StuffFreqKeyboard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboardCC1101Stuff, LV_OBJ_FLAG_HIDDEN);
      // lv_obj_del(keyboardCC1101Stuff);
      lv_obj_clear_state(ui_txtRecPlayFq, LV_STATE_FOCUSED);
      lv_indev_reset(NULL, ui_txtRecPlayFq);
      lv_label_set_text(ui_lblCC1101StuffFreqKeyboardValueUnits, String("").c_str());
    }
  }
}

#endif
