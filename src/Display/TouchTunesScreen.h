#ifndef TOUCHTUNES_SCREEN_H
#define TOUCHTUNES_SCREEN_H

#include <lvgl.h>
#include <ui.h>
#include "SubGhz/TouchTunes.h"
#include "Display/Event.h"

// =====================================================================
// Dynamic LVGL screen for TouchTunes Remote (Tabbed Layout)
// Created programmatically (no SquareLine Studio)
// =====================================================================

// Screen and key widget handles
static lv_obj_t *ui_scrTouchTunes = NULL;
static lv_obj_t *tt_rollerHundreds = NULL;
static lv_obj_t *tt_rollerTens = NULL;
static lv_obj_t *tt_rollerOnes = NULL;
static lv_obj_t *tt_lblStatus = NULL;

// Confirmation popup state
static uint8_t tt_confirm_cmd = 0;
static uint8_t tt_confirm_pin = 0;
static const char *tt_confirm_btns[] = {"Yes", "No", ""};

// Forward declarations
static void tt_btn_event_cb(lv_event_t *e);
static void tt_back_event_cb(lv_event_t *e);
static void tt_msgbox_event_cb(lv_event_t *e);

// --- Helper: get current PIN from rollers ---
static uint8_t tt_getPinFromRollers(void) {
    int h = lv_roller_get_selected(tt_rollerHundreds);
    int t = lv_roller_get_selected(tt_rollerTens);
    int o = lv_roller_get_selected(tt_rollerOnes);
    int val = h * 100 + t * 10 + o;
    if (val > 255) val = 255;
    return (uint8_t)val;
}

// --- Helper: create a styled button with label ---
static lv_obj_t *tt_createBtn(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *text, uint32_t bgColor, uint8_t cmd) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, w);
    lv_obj_set_height(btn, h);
    lv_obj_set_x(btn, x);
    lv_obj_set_y(btn, y);
    lv_obj_set_align(btn, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00AFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_align(lbl, LV_ALIGN_CENTER);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_user_data(btn, (void *)(uintptr_t)cmd);
    lv_obj_add_event_cb(btn, tt_btn_event_cb, LV_EVENT_CLICKED, NULL);

    return btn;
}

// --- Helper: create a styled roller ---
static lv_obj_t *tt_createRoller(lv_obj_t *parent, const char *opts, int x, int y) {
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 1);
    lv_obj_set_width(roller, 42);
    lv_obj_set_height(roller, 32);
    lv_obj_set_x(roller, x);
    lv_obj_set_y(roller, y);
    lv_obj_set_align(roller, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_font(roller, &ui_font_Verdana16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(roller, lv_color_hex(0x00AFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(roller, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x003366), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x00FF00), LV_PART_SELECTED | LV_STATE_DEFAULT);
    return roller;
}

// --- Helper: check if command needs confirmation ---
static bool tt_needsConfirmation(uint8_t cmd) {
    return (cmd == TT_CMD_P3_SKIP || cmd == TT_CMD_P1 || cmd == TT_CMD_ON_OFF);
}

// --- Helper: get readable name for confirmed commands ---
static const char *tt_getConfirmName(uint8_t cmd) {
    if (cmd == TT_CMD_P3_SKIP) return "SKIP SONG";
    if (cmd == TT_CMD_P1)      return "FREE CREDIT";
    if (cmd == TT_CMD_ON_OFF)  return "ON/OFF";
    return "COMMAND";
}

// --- Confirmation msgbox callback ---
static void tt_msgbox_event_cb(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);

    if (btn_id == 0) {  // "Yes"
        tt_pending_pin = tt_confirm_pin;
        tt_pending_cmd = tt_confirm_cmd;
        currentState = STATE_SEND_TOUCHTUNES;

        char buf[40];
        snprintf(buf, sizeof(buf), "Sending %s...", tt_getConfirmName(tt_confirm_cmd));
        lv_label_set_text(tt_lblStatus, buf);
    } else {  // "No"
        lv_label_set_text(tt_lblStatus, "Cancelled");
    }

    lv_msgbox_close(mbox);
}

// --- Button event handler (shared by all remote buttons) ---
static void tt_btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t cmd = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    uint8_t pin = tt_getPinFromRollers();

    // Commands that require "Are you sure?" confirmation
    if (tt_needsConfirmation(cmd)) {
        tt_confirm_cmd = cmd;
        tt_confirm_pin = pin;

        char msg[64];
        snprintf(msg, sizeof(msg), "Send %s command?", tt_getConfirmName(cmd));

        lv_obj_t *mbox = lv_msgbox_create(NULL, "Confirm", msg, tt_confirm_btns, false);

        // Dark theme - background
        lv_obj_set_style_bg_color(mbox, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mbox, 255, LV_PART_MAIN);
        lv_obj_set_style_border_color(mbox, lv_color_hex(0xFF9100), LV_PART_MAIN);
        lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(mbox, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(mbox, 16, LV_PART_MAIN);

        // Title text (orange)
        lv_obj_t *titleObj = lv_msgbox_get_title(mbox);
        lv_obj_set_style_text_color(titleObj, lv_color_hex(0xFF9100), LV_PART_MAIN);
        lv_obj_set_style_text_font(titleObj, &ui_font_Verdana18, LV_PART_MAIN);

        // Message text (white, easy to read)
        lv_obj_t *textObj = lv_msgbox_get_text(mbox);
        lv_obj_set_style_text_color(textObj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(textObj, &ui_font_Verdana16, LV_PART_MAIN);

        // Button bar styling
        lv_obj_t *btnsObj = lv_msgbox_get_btns(mbox);
        lv_obj_set_style_bg_color(btnsObj, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btnsObj, lv_color_hex(0x336699), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(btnsObj, 255, LV_PART_ITEMS);
        lv_obj_set_style_text_color(btnsObj, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        lv_obj_set_style_text_font(btnsObj, &ui_font_Verdana16, LV_PART_ITEMS);
        lv_obj_set_style_border_color(btnsObj, lv_color_hex(0x00AFFF), LV_PART_ITEMS);
        lv_obj_set_style_border_width(btnsObj, 1, LV_PART_ITEMS);
        lv_obj_set_style_radius(btnsObj, 6, LV_PART_ITEMS);

        // Dark overlay behind the msgbox
        lv_obj_t *bg = lv_obj_get_parent(mbox);
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bg, 180, LV_PART_MAIN);

        lv_obj_center(mbox);
        lv_obj_add_event_cb(mbox, tt_msgbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_label_set_text(tt_lblStatus, "Confirm?");
        return;
    }

    tt_pending_pin = pin;
    tt_pending_cmd = cmd;
    currentState = STATE_SEND_TOUCHTUNES;

    char buf[40];
    snprintf(buf, sizeof(buf), "Sending PIN:%03d CMD:0x%02X", pin, cmd);
    lv_label_set_text(tt_lblStatus, buf);
}

// --- Back button handler ---
static void tt_back_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        currentState = STATE_IDLE;
        lv_scr_load(ui_scrMain);
    }
}

// =====================================================================
// tt_screen_init() — build the entire TouchTunes screen dynamically
// Uses tabview for optimal LCD real estate usage
// Layout: Title + PIN + Status (top) | Tabview (Control/Numpad/Volume) | BACK
// =====================================================================
static void tt_screen_init(void) {
    // --- Screen ---
    ui_scrTouchTunes = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrTouchTunes, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrTouchTunes, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(ui_scrTouchTunes, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrTouchTunes, LV_OBJ_FLAG_SCROLLABLE);

    // --- Title ---
    lv_obj_t *title = lv_label_create(ui_scrTouchTunes);
    lv_obj_set_x(title, 0);
    lv_obj_set_y(title, 3);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_label_set_text(title, "TOUCHTUNES REMOTE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &ui_font_Verdana18, LV_PART_MAIN);

    // --- PIN label ---
    lv_obj_t *pinLbl = lv_label_create(ui_scrTouchTunes);
    lv_obj_set_x(pinLbl, -75);
    lv_obj_set_y(pinLbl, 30);
    lv_obj_set_align(pinLbl, LV_ALIGN_TOP_MID);
    lv_label_set_text(pinLbl, "PIN:");
    lv_obj_set_style_text_color(pinLbl, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(pinLbl, &ui_font_Verdana16, LV_PART_MAIN);

    // --- PIN Rollers (3 digits) ---
    tt_rollerHundreds = tt_createRoller(ui_scrTouchTunes, "0\n1\n2", -28, 27);
    tt_rollerTens     = tt_createRoller(ui_scrTouchTunes, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 18, 27);
    tt_rollerOnes     = tt_createRoller(ui_scrTouchTunes, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 64, 27);

    // --- Status label ---
    tt_lblStatus = lv_label_create(ui_scrTouchTunes);
    lv_obj_set_width(tt_lblStatus, 280);
    lv_obj_set_x(tt_lblStatus, 0);
    lv_obj_set_y(tt_lblStatus, 60);
    lv_obj_set_align(tt_lblStatus, LV_ALIGN_TOP_MID);
    lv_label_set_text(tt_lblStatus, "Ready");
    lv_obj_set_style_text_align(tt_lblStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(tt_lblStatus, lv_color_hex(0x00FFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(tt_lblStatus, &ui_font_Verdana16, LV_PART_MAIN);

    // ======================= TAB VIEW =======================
    // Top area ends at Y~78, BACK button at Y~453, tabview fills between
    lv_obj_t *tabview = lv_tabview_create(ui_scrTouchTunes, LV_DIR_TOP, 32);
    lv_obj_set_pos(tabview, 0, 78);
    lv_obj_set_size(tabview, 320, 372);
    lv_obj_set_style_bg_opa(tabview, 0, LV_PART_MAIN);

    // Style the tab buttons bar
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab_btns, 255, LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFF9600), LV_PART_MAIN);
    lv_obj_set_style_text_font(tab_btns, &ui_font_Verdana16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x003366), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);

    // Create tabs
    lv_obj_t *tabControl = lv_tabview_add_tab(tabview, "Control");
    lv_obj_t *tabNumpad  = lv_tabview_add_tab(tabview, "Numpad");
    lv_obj_t *tabVolume  = lv_tabview_add_tab(tabview, "Volume");

    lv_obj_clear_flag(tabControl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tabNumpad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tabVolume, LV_OBJ_FLAG_SCROLLABLE);

    // Style tab content areas (dark, transparent to let bg image show)
    lv_obj_set_style_bg_opa(tabControl, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabNumpad,  0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabVolume,  0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabControl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabNumpad, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabVolume, 0, LV_PART_MAIN);

    // ==================== CONTROL TAB ====================
    // 5 rows of buttons, ~330px content height
    // Row spacing: ~65px, button height: 44px
    int y;

    // Row 1: PAUSE, ON/OFF, SKIP
    y = 5;
    tt_createBtn(tabControl, -95, y, 88, 44, "PAUSE",  0x333366, TT_CMD_PAUSE);
    tt_createBtn(tabControl,   0, y, 88, 44, "ON/OFF", 0xCC0000, TT_CMD_ON_OFF);
    tt_createBtn(tabControl,  95, y, 88, 44, "SKIP",   0x333366, TT_CMD_P3_SKIP);

    // Row 2: P1, P2, P3
    y = 58;
    tt_createBtn(tabControl, -90, y, 78, 38, "CREDIT", 0x994400, TT_CMD_P1);
    tt_createBtn(tabControl,   0, y, 78, 38, "P2", 0x224488, TT_CMD_P2_EDIT_QUEUE);
    tt_createBtn(tabControl,  90, y, 78, 38, "P3", 0x224488, TT_CMD_P3_SKIP);

    // Row 3: F1, UP, F2
    y = 108;
    tt_createBtn(tabControl, -95, y, 68, 44, "F1",          0x444466, TT_CMD_F1_RESTART);
    tt_createBtn(tabControl,   0, y, 78, 44, LV_SYMBOL_UP,  0x336699, TT_CMD_UP);
    tt_createBtn(tabControl,  95, y, 68, 44, "F2",          0x444466, TT_CMD_F2_KEY);

    // Row 4: LEFT, OK, RIGHT
    y = 162;
    tt_createBtn(tabControl, -95, y, 78, 44, LV_SYMBOL_LEFT,  0x336699, TT_CMD_LEFT);
    tt_createBtn(tabControl,   0, y, 78, 44, "OK",            0x006633, TT_CMD_OK);
    tt_createBtn(tabControl,  95, y, 78, 44, LV_SYMBOL_RIGHT, 0x336699, TT_CMD_RIGHT);

    // Row 5: F3, DOWN, F4
    y = 216;
    tt_createBtn(tabControl, -95, y, 68, 44, "F3",            0x444466, TT_CMD_F3_MIC_A_MUTE);
    tt_createBtn(tabControl,   0, y, 78, 44, LV_SYMBOL_DOWN,  0x336699, TT_CMD_DOWN);
    tt_createBtn(tabControl,  95, y, 68, 44, "F4",            0x444466, TT_CMD_F4_MIC_B_MUTE);

    // ==================== NUMPAD TAB ====================
    // 4 rows, big buttons for easy tapping
    int numY = 5;
    int numSpacing = 75;
    int numW = 85;
    int numH = 58;

    tt_createBtn(tabNumpad, -90, numY, numW, numH, "1", 0x2A2A4A, TT_CMD_1);
    tt_createBtn(tabNumpad,   0, numY, numW, numH, "2", 0x2A2A4A, TT_CMD_2);
    tt_createBtn(tabNumpad,  90, numY, numW, numH, "3", 0x2A2A4A, TT_CMD_3);

    numY += numSpacing;
    tt_createBtn(tabNumpad, -90, numY, numW, numH, "4", 0x2A2A4A, TT_CMD_4);
    tt_createBtn(tabNumpad,   0, numY, numW, numH, "5", 0x2A2A4A, TT_CMD_5);
    tt_createBtn(tabNumpad,  90, numY, numW, numH, "6", 0x2A2A4A, TT_CMD_6);

    numY += numSpacing;
    tt_createBtn(tabNumpad, -90, numY, numW, numH, "7", 0x2A2A4A, TT_CMD_7);
    tt_createBtn(tabNumpad,   0, numY, numW, numH, "8", 0x2A2A4A, TT_CMD_8);
    tt_createBtn(tabNumpad,  90, numY, numW, numH, "9", 0x2A2A4A, TT_CMD_9);

    numY += numSpacing;
    tt_createBtn(tabNumpad, -90, numY, numW, numH, "*", 0x665500, TT_CMD_STAR_KARAOKE);
    tt_createBtn(tabNumpad,   0, numY, numW, numH, "0", 0x2A2A4A, TT_CMD_0);
    tt_createBtn(tabNumpad,  90, numY, numW, numH, "#", 0x665500, TT_CMD_HASH_LOCK);

    // ==================== VOLUME TAB ====================
    // 3 zones with big VOL+/VOL- buttons
    int zoneH = 100;
    int volBtnW = 110;
    int volBtnH = 55;

    for (int z = 0; z < 3; z++) {
        int zy = 5 + z * zoneH;

        // Zone label
        lv_obj_t *zLbl = lv_label_create(tabVolume);
        lv_obj_set_x(zLbl, 0);
        lv_obj_set_y(zLbl, zy + 2);
        lv_obj_set_align(zLbl, LV_ALIGN_TOP_MID);
        char zoneText[16];
        snprintf(zoneText, sizeof(zoneText), "Zone %d", z + 1);
        lv_label_set_text(zLbl, zoneText);
        lv_obj_set_style_text_color(zLbl, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(zLbl, &ui_font_Verdana16, LV_PART_MAIN);

        uint8_t volUpCmd, volDownCmd;
        if (z == 0)      { volUpCmd = TT_CMD_ZONE1_VOL_UP; volDownCmd = TT_CMD_ZONE1_VOL_DOWN; }
        else if (z == 1)  { volUpCmd = TT_CMD_ZONE2_VOL_UP; volDownCmd = TT_CMD_ZONE2_VOL_DOWN; }
        else              { volUpCmd = TT_CMD_ZONE3_VOL_UP; volDownCmd = TT_CMD_ZONE3_VOL_DOWN; }

        tt_createBtn(tabVolume, -60, zy + 22, volBtnW, volBtnH, "VOL +", 0x006633, volUpCmd);
        tt_createBtn(tabVolume,  60, zy + 22, volBtnW, volBtnH, "VOL -", 0x663300, volDownCmd);
    }

    // ==================== BACK BUTTON ====================
    lv_obj_t *btnBack = lv_btn_create(ui_scrTouchTunes);
    lv_obj_set_width(btnBack, 90);
    lv_obj_set_height(btnBack, 30);
    lv_obj_set_x(btnBack, -115);
    lv_obj_set_y(btnBack, 453);
    lv_obj_set_align(btnBack, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0xFFF700), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnBack, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(btnBack, 6, LV_PART_MAIN);

    lv_obj_t *lblBack = lv_label_create(btnBack);
    lv_obj_set_align(lblBack, LV_ALIGN_CENTER);
    lv_label_set_text(lblBack, "BACK");
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(btnBack, tt_back_event_cb, LV_EVENT_CLICKED, NULL);
}

#endif // TOUCHTUNES_SCREEN_H
