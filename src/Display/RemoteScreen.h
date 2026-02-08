#ifndef REMOTE_SCREEN_H
#define REMOTE_SCREEN_H

#include <lvgl.h>
#include <ui.h>
#include <SD.h>
#include "SD/SDCard.h"
#include "Display/Event.h"

// =====================================================================
// Dynamic LVGL screen for Universal Remote (Programmable .sub buttons)
// Created programmatically (no SquareLine Studio)
// Each button maps to a Flipper .sub file; profiles saved to SD card.
// =====================================================================

// --- Button ID enum ---
enum RemoteBtn {
    RB_POWER, RB_MUTE, RB_LAST, RB_EXIT,
    RB_0, RB_1, RB_2, RB_3, RB_4, RB_5, RB_6, RB_7, RB_8, RB_9,
    RB_UP, RB_DOWN, RB_LEFT, RB_RIGHT, RB_OK,
    RB_BACK, RB_MENU, RB_HOME, RB_GUIDE, RB_INFO,
    RB_VOL_UP, RB_VOL_DOWN, RB_CH_UP, RB_CH_DOWN,
    RB_PLAY, RB_PAUSE, RB_STOP, RB_PREV, RB_NEXT,
    RB_INPUT, RB_RECORD,
    RB_COUNT
};

// Human-readable key names for profile file (must match enum order)
static const char *rb_keyNames[] = {
    "power", "mute", "last", "exit",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "up", "down", "left", "right", "ok",
    "back", "menu", "home", "guide", "info",
    "vol_up", "vol_down", "ch_up", "ch_down",
    "play", "pause", "stop", "prev", "next",
    "input", "record"
};

// Display labels for buttons (used in UI and status messages)
static const char *rb_displayNames[] = {
    "POWER", "MUTE", "LAST", "EXIT",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "OK",
    "BACK", "MENU", "HOME", "GUIDE", "INFO",
    "VOL +", "VOL -", "CH +", "CH -",
    LV_SYMBOL_PLAY, LV_SYMBOL_PAUSE, LV_SYMBOL_STOP, LV_SYMBOL_PREV, LV_SYMBOL_NEXT,
    "INPUT", "REC"
};

// --- Profile data ---
#define RB_MAX_PATH 64
#define RB_MAX_NAME 32

typedef struct {
    char name[RB_MAX_NAME];
    char paths[RB_COUNT][RB_MAX_PATH];
} RemoteProfile;

static RemoteProfile remoteProfile;
static char remote_pendingPath[RB_MAX_PATH];
static uint8_t remote_pendingBtnId = 0;

// --- Screen widgets ---
static lv_obj_t *ui_scrRemote = NULL;
static lv_obj_t *remote_lblStatus = NULL;
static lv_obj_t *remote_ddlProfile = NULL;
static lv_obj_t *remote_btnEdit = NULL;
static lv_obj_t *remote_lblEdit = NULL;
static bool remote_editMode = false;

// Button object array for visual feedback
static lv_obj_t *remote_btnObjs[RB_COUNT] = {0};

// --- File picker overlay ---
static lv_obj_t *remote_pickerPanel = NULL;
static lv_obj_t *remote_pickerTitle = NULL;
static lv_obj_t *remote_pickerDdlFolder = NULL;
static lv_obj_t *remote_pickerDdlFile = NULL;
static uint8_t remote_assigningBtnId = 0;

// --- New profile keyboard overlay ---
static lv_obj_t *remote_namePanel = NULL;
static lv_obj_t *remote_nameTxt = NULL;
static lv_obj_t *remote_nameKbd = NULL;

// Forward declarations
static void remote_btn_event_cb(lv_event_t *e);
static void remote_back_event_cb(lv_event_t *e);
static void remote_edit_event_cb(lv_event_t *e);
static void remote_profile_changed_cb(lv_event_t *e);
static void remote_new_profile_cb(lv_event_t *e);
static void remote_del_profile_cb(lv_event_t *e);
static void remote_picker_assign_cb(lv_event_t *e);
static void remote_picker_clear_cb(lv_event_t *e);
static void remote_picker_cancel_cb(lv_event_t *e);
static void remote_picker_folder_cb(lv_event_t *e);
static void remote_name_ready_cb(lv_event_t *e);
static void remote_name_cancel_cb(lv_event_t *e);
static void remote_saveProfile(void);
static void remote_loadProfile(const char *filename);
static void remote_clearProfile(void);
static void remote_refreshProfileList(void);
static void remote_updateButtonStyles(void);
static int remote_keyToId(const char *key);
static void remote_showPicker(uint8_t btnId);
static void remote_hidePicker(void);

// =====================================================================
// Helper: create a styled remote button
// =====================================================================
static lv_obj_t *remote_createBtn(lv_obj_t *parent, int x, int y, int w, int h,
                                   const char *text, uint32_t bgColor, uint8_t btnId) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, w);
    lv_obj_set_height(btn, h);
    lv_obj_set_x(btn, x);
    lv_obj_set_y(btn, y);
    lv_obj_set_align(btn, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn, 200, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_align(lbl, LV_ALIGN_CENTER);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    lv_obj_set_user_data(btn, (void *)(uintptr_t)btnId);
    lv_obj_add_event_cb(btn, remote_btn_event_cb, LV_EVENT_CLICKED, NULL);

    if (btnId < RB_COUNT) remote_btnObjs[btnId] = btn;
    return btn;
}

// =====================================================================
// Profile I/O
// =====================================================================
static void remote_clearProfile(void) {
    memset(&remoteProfile, 0, sizeof(remoteProfile));
}

static void remote_saveProfile(void) {
    if (remoteProfile.name[0] == '\0') return;

    if (!sd_card_is_present()) return;
    if (!SD.exists("/remotes")) SD.mkdir("/remotes");

    char path[96];
    snprintf(path, sizeof(path), "/remotes/%s.remote", remoteProfile.name);

    File f = SD.open(path, FILE_WRITE, true);
    if (!f) { now_close_sd_card(); return; }

    char line[128];
    snprintf(line, sizeof(line), "Name: %s\n", remoteProfile.name);
    f.print(line);

    for (int i = 0; i < RB_COUNT; i++) {
        if (remoteProfile.paths[i][0] != '\0') {
            snprintf(line, sizeof(line), "%s=%s\n", rb_keyNames[i], remoteProfile.paths[i]);
            f.print(line);
        }
    }
    f.flush();
    f.close();
    now_close_sd_card();
}

static int remote_keyToId(const char *key) {
    for (int i = 0; i < RB_COUNT; i++) {
        if (strcmp(key, rb_keyNames[i]) == 0) return i;
    }
    return -1;
}

static void remote_loadProfile(const char *filename) {
    remote_clearProfile();

    if (!sd_card_is_present()) return;

    char path[96];
    snprintf(path, sizeof(path), "/remotes/%s", filename);

    File f = SD.open(path, FILE_READ);
    if (!f) { now_close_sd_card(); return; }

    char buf[256];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(buf) - 1) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            buf[len++] = c;
        }
        buf[len] = '\0';
        if (len == 0) continue;

        // Parse "Name: xxx"
        if (strncmp(buf, "Name: ", 6) == 0) {
            strncpy(remoteProfile.name, buf + 6, RB_MAX_NAME - 1);
            remoteProfile.name[RB_MAX_NAME - 1] = '\0';
            continue;
        }

        // Parse "key=path"
        char *eq = strchr(buf, '=');
        if (eq) {
            *eq = '\0';
            const char *key = buf;
            const char *val = eq + 1;
            int id = remote_keyToId(key);
            if (id >= 0 && id < RB_COUNT) {
                strncpy(remoteProfile.paths[id], val, RB_MAX_PATH - 1);
                remoteProfile.paths[id][RB_MAX_PATH - 1] = '\0';
            }
        }
    }
    f.close();
    now_close_sd_card();

    remote_updateButtonStyles();
}

static void remote_refreshProfileList(void) {
    if (!sd_card_is_present()) return;
    if (!SD.exists("/remotes")) SD.mkdir("/remotes");
    lv_dropdown_clear_options(remote_ddlProfile);
    refresh_sd_card_file(remote_ddlProfile, "/remotes", ".remote", true);
    now_close_sd_card();
}

// =====================================================================
// Visual feedback: dim unassigned buttons, highlight in edit mode
// =====================================================================
static void remote_updateButtonStyles(void) {
    for (int i = 0; i < RB_COUNT; i++) {
        if (remote_btnObjs[i] == NULL) continue;
        bool assigned = (remoteProfile.paths[i][0] != '\0');

        if (remote_editMode) {
            // Edit mode: green border for all
            lv_obj_set_style_border_color(remote_btnObjs[i], lv_color_hex(0x00FF00), LV_PART_MAIN);
            lv_obj_set_style_border_opa(remote_btnObjs[i], 255, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(remote_btnObjs[i], 255, LV_PART_MAIN);
        } else if (assigned) {
            // Normal, assigned: cyan border
            lv_obj_set_style_border_color(remote_btnObjs[i], lv_color_hex(0x00AFFF), LV_PART_MAIN);
            lv_obj_set_style_border_opa(remote_btnObjs[i], 200, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(remote_btnObjs[i], 255, LV_PART_MAIN);
        } else {
            // Normal, unassigned: dimmed
            lv_obj_set_style_border_color(remote_btnObjs[i], lv_color_hex(0x444444), LV_PART_MAIN);
            lv_obj_set_style_border_opa(remote_btnObjs[i], 150, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(remote_btnObjs[i], 120, LV_PART_MAIN);
        }
    }
}

// =====================================================================
// File Picker Overlay
// =====================================================================
static void remote_showPicker(uint8_t btnId) {
    remote_assigningBtnId = btnId;

    char title[48];
    snprintf(title, sizeof(title), "Assign: %s", rb_displayNames[btnId]);
    lv_label_set_text(remote_pickerTitle, title);

    // Populate folder dropdown
    if (sd_card_is_present()) {
        lv_dropdown_clear_options(remote_pickerDdlFolder);
        refresh_sd_card_folder(remote_pickerDdlFolder, "/");
        // Populate file dropdown from first folder
        lv_dropdown_clear_options(remote_pickerDdlFile);
        char folderBuf[64];
        lv_dropdown_get_selected_str(remote_pickerDdlFolder, folderBuf, sizeof(folderBuf));
        char folderPath[72];
        if (strcmp(folderBuf, "/") == 0) {
            snprintf(folderPath, sizeof(folderPath), "/");
        } else {
            snprintf(folderPath, sizeof(folderPath), "/%s", folderBuf);
        }
        refresh_sd_card_file(remote_pickerDdlFile, folderPath, ".sub", true);
        now_close_sd_card();
    }

    lv_obj_clear_flag(remote_pickerPanel, LV_OBJ_FLAG_HIDDEN);
}

static void remote_hidePicker(void) {
    lv_obj_add_flag(remote_pickerPanel, LV_OBJ_FLAG_HIDDEN);
}

static void remote_picker_folder_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    char folderBuf[64];
    lv_dropdown_get_selected_str(remote_pickerDdlFolder, folderBuf, sizeof(folderBuf));

    char folderPath[72];
    if (strcmp(folderBuf, "/") == 0) {
        snprintf(folderPath, sizeof(folderPath), "/");
    } else {
        snprintf(folderPath, sizeof(folderPath), "/%s", folderBuf);
    }

    if (sd_card_is_present()) {
        lv_dropdown_clear_options(remote_pickerDdlFile);
        refresh_sd_card_file(remote_pickerDdlFile, folderPath, ".sub", true);
        now_close_sd_card();
    }
}

static void remote_picker_assign_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    // Build full path from folder + file
    char folderBuf[64], fileBuf[64];
    lv_dropdown_get_selected_str(remote_pickerDdlFolder, folderBuf, sizeof(folderBuf));
    lv_dropdown_get_selected_str(remote_pickerDdlFile, fileBuf, sizeof(fileBuf));

    if (fileBuf[0] == '\0') {
        lv_label_set_text(remote_lblStatus, "No file selected");
        remote_hidePicker();
        return;
    }

    char fullPath[RB_MAX_PATH];
    if (strcmp(folderBuf, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "/%s", fileBuf);
    } else {
        snprintf(fullPath, sizeof(fullPath), "/%s/%s", folderBuf, fileBuf);
    }

    strncpy(remoteProfile.paths[remote_assigningBtnId], fullPath, RB_MAX_PATH - 1);
    remoteProfile.paths[remote_assigningBtnId][RB_MAX_PATH - 1] = '\0';

    char msg[80];
    snprintf(msg, sizeof(msg), "%s = %s", rb_displayNames[remote_assigningBtnId], sd_basename(fullPath));
    lv_label_set_text(remote_lblStatus, msg);

    remote_hidePicker();
    remote_updateButtonStyles();
    remote_saveProfile();
}

static void remote_picker_clear_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    remoteProfile.paths[remote_assigningBtnId][0] = '\0';

    char msg[48];
    snprintf(msg, sizeof(msg), "%s cleared", rb_displayNames[remote_assigningBtnId]);
    lv_label_set_text(remote_lblStatus, msg);

    remote_hidePicker();
    remote_updateButtonStyles();
    remote_saveProfile();
}

static void remote_picker_cancel_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    remote_hidePicker();
}

// =====================================================================
// Button click handler (shared by all remote buttons)
// =====================================================================
static void remote_btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t btnId = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    if (btnId >= RB_COUNT) return;

    if (remote_editMode) {
        remote_showPicker(btnId);
    } else {
        if (remoteProfile.paths[btnId][0] != '\0') {
            strncpy(remote_pendingPath, remoteProfile.paths[btnId], sizeof(remote_pendingPath) - 1);
            remote_pendingPath[sizeof(remote_pendingPath) - 1] = '\0';
            remote_pendingBtnId = btnId;
            currentState = STATE_SEND_REMOTE;

            char msg[48];
            snprintf(msg, sizeof(msg), "Sending %s...", rb_displayNames[btnId]);
            lv_label_set_text(remote_lblStatus, msg);
        } else {
            char msg[48];
            snprintf(msg, sizeof(msg), "%s not assigned", rb_displayNames[btnId]);
            lv_label_set_text(remote_lblStatus, msg);
        }
    }
}

// =====================================================================
// Edit mode toggle
// =====================================================================
static void remote_edit_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    remote_editMode = !remote_editMode;

    if (remote_editMode) {
        lv_label_set_text(remote_lblEdit, "DONE");
        lv_obj_set_style_bg_color(remote_btnEdit, lv_color_hex(0xFF6600), LV_PART_MAIN);
        lv_label_set_text(remote_lblStatus, "EDIT MODE - Tap a button to assign");
    } else {
        lv_label_set_text(remote_lblEdit, "EDIT");
        lv_obj_set_style_bg_color(remote_btnEdit, lv_color_hex(0x336699), LV_PART_MAIN);
        lv_label_set_text(remote_lblStatus, "Ready");
        remote_hidePicker();
    }
    remote_updateButtonStyles();
}

// =====================================================================
// Profile dropdown changed
// =====================================================================
static void remote_profile_changed_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    char selBuf[64];
    lv_dropdown_get_selected_str(remote_ddlProfile, selBuf, sizeof(selBuf));
    if (selBuf[0] == '\0') return;

    remote_loadProfile(selBuf);

    char msg[64];
    snprintf(msg, sizeof(msg), "Loaded: %s", remoteProfile.name);
    lv_label_set_text(remote_lblStatus, msg);
}

// =====================================================================
// New profile: show keyboard overlay
// =====================================================================
static void remote_new_profile_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_textarea_set_text(remote_nameTxt, "");
    lv_obj_clear_flag(remote_namePanel, LV_OBJ_FLAG_HIDDEN);
}

static void remote_name_ready_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_READY) return;

    const char *name = lv_textarea_get_text(remote_nameTxt);
    if (name[0] == '\0') {
        lv_label_set_text(remote_lblStatus, "Name cannot be empty");
        return;
    }

    // Create new empty profile
    remote_clearProfile();
    strncpy(remoteProfile.name, name, RB_MAX_NAME - 1);
    remoteProfile.name[RB_MAX_NAME - 1] = '\0';

    remote_saveProfile();
    remote_refreshProfileList();

    // Select the new profile in dropdown
    uint16_t cnt = lv_dropdown_get_option_cnt(remote_ddlProfile);
    char buf[64];
    char target[72];
    snprintf(target, sizeof(target), "%s.remote", remoteProfile.name);
    for (uint16_t i = 0; i < cnt; i++) {
        lv_dropdown_get_selected_str(remote_ddlProfile, buf, sizeof(buf));
        if (strcmp(buf, target) == 0) break;
        // Cycle through to find it
        lv_dropdown_set_selected(remote_ddlProfile, i);
        lv_dropdown_get_selected_str(remote_ddlProfile, buf, sizeof(buf));
        if (strcmp(buf, target) == 0) break;
    }

    remote_updateButtonStyles();

    char msg[64];
    snprintf(msg, sizeof(msg), "Created: %s", remoteProfile.name);
    lv_label_set_text(remote_lblStatus, msg);

    lv_obj_add_flag(remote_namePanel, LV_OBJ_FLAG_HIDDEN);
}

static void remote_name_cancel_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_add_flag(remote_namePanel, LV_OBJ_FLAG_HIDDEN);
}

// =====================================================================
// Delete profile
// =====================================================================
static const char *remote_del_btns[] = {"Yes", "No", ""};
static void remote_del_msgbox_cb(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);

    if (btn_id == 0) {  // Yes
        if (remoteProfile.name[0] != '\0') {
            char path[96];
            snprintf(path, sizeof(path), "/remotes/%s.remote", remoteProfile.name);
            if (sd_card_is_present()) {
                SD.remove(path);
                now_close_sd_card();
            }
            remote_clearProfile();
            remote_refreshProfileList();
            remote_updateButtonStyles();
            lv_label_set_text(remote_lblStatus, "Profile deleted");
        }
    }
    lv_msgbox_close(mbox);
}

static void remote_del_profile_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (remoteProfile.name[0] == '\0') {
        lv_label_set_text(remote_lblStatus, "No profile loaded");
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Delete \"%s\"?", remoteProfile.name);
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Confirm", msg, remote_del_btns, false);

    lv_obj_set_style_bg_color(mbox, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mbox, 255, LV_PART_MAIN);
    lv_obj_set_style_border_color(mbox, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(mbox, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mbox, 16, LV_PART_MAIN);

    lv_obj_t *titleObj = lv_msgbox_get_title(mbox);
    lv_obj_set_style_text_color(titleObj, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(titleObj, &ui_font_Verdana18, LV_PART_MAIN);

    lv_obj_t *textObj = lv_msgbox_get_text(mbox);
    lv_obj_set_style_text_color(textObj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(textObj, &ui_font_Verdana16, LV_PART_MAIN);

    lv_obj_t *btnsObj = lv_msgbox_get_btns(mbox);
    lv_obj_set_style_bg_color(btnsObj, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnsObj, lv_color_hex(0x336699), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnsObj, 255, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnsObj, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnsObj, &ui_font_Verdana16, LV_PART_ITEMS);
    lv_obj_set_style_border_color(btnsObj, lv_color_hex(0x00AFFF), LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnsObj, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnsObj, 6, LV_PART_ITEMS);

    lv_obj_t *bg = lv_obj_get_parent(mbox);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, 180, LV_PART_MAIN);

    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, remote_del_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// =====================================================================
// Back button
// =====================================================================
static void remote_back_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    remote_editMode = false;
    lv_label_set_text(remote_lblEdit, "EDIT");
    lv_obj_set_style_bg_color(remote_btnEdit, lv_color_hex(0x336699), LV_PART_MAIN);
    remote_hidePicker();
    currentState = STATE_IDLE;
    lv_scr_load(ui_scrMain);
}

// =====================================================================
// remote_screen_init() — build the entire screen dynamically
// =====================================================================
static void remote_screen_init(void) {
    remote_clearProfile();
    memset(remote_btnObjs, 0, sizeof(remote_btnObjs));

    // --- Screen ---
    ui_scrRemote = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrRemote, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrRemote, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(ui_scrRemote, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrRemote, LV_OBJ_FLAG_SCROLLABLE);

    // --- Title ---
    lv_obj_t *title = lv_label_create(ui_scrRemote);
    lv_obj_set_x(title, 0);
    lv_obj_set_y(title, 3);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_label_set_text(title, "UNIVERSAL REMOTE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &ui_font_Verdana18, LV_PART_MAIN);

    // --- Profile dropdown ---
    remote_ddlProfile = lv_dropdown_create(ui_scrRemote);
    lv_obj_set_width(remote_ddlProfile, 165);
    lv_obj_set_x(remote_ddlProfile, -55);
    lv_obj_set_y(remote_ddlProfile, 27);
    lv_obj_set_align(remote_ddlProfile, LV_ALIGN_TOP_MID);
    lv_dropdown_set_options(remote_ddlProfile, "");
    lv_obj_set_style_text_font(remote_ddlProfile, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(remote_ddlProfile, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_text_color(remote_ddlProfile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(remote_ddlProfile, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(remote_ddlProfile, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(remote_ddlProfile, remote_profile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // --- NEW button ---
    lv_obj_t *btnNew = lv_btn_create(ui_scrRemote);
    lv_obj_set_width(btnNew, 55);
    lv_obj_set_height(btnNew, 32);
    lv_obj_set_x(btnNew, 88);
    lv_obj_set_y(btnNew, 27);
    lv_obj_set_align(btnNew, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnNew, lv_color_hex(0x006633), LV_PART_MAIN);
    lv_obj_set_style_radius(btnNew, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnNew, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lblNew = lv_label_create(btnNew);
    lv_obj_set_align(lblNew, LV_ALIGN_CENTER);
    lv_label_set_text(lblNew, "NEW");
    lv_obj_set_style_text_font(lblNew, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblNew, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(btnNew, remote_new_profile_cb, LV_EVENT_CLICKED, NULL);

    // --- DEL button ---
    lv_obj_t *btnDel = lv_btn_create(ui_scrRemote);
    lv_obj_set_width(btnDel, 45);
    lv_obj_set_height(btnDel, 32);
    lv_obj_set_x(btnDel, 140);
    lv_obj_set_y(btnDel, 27);
    lv_obj_set_align(btnDel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnDel, lv_color_hex(0x990000), LV_PART_MAIN);
    lv_obj_set_style_radius(btnDel, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnDel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lblDel = lv_label_create(btnDel);
    lv_obj_set_align(lblDel, LV_ALIGN_CENTER);
    lv_label_set_text(lblDel, "DEL");
    lv_obj_set_style_text_font(lblDel, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblDel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(btnDel, remote_del_profile_cb, LV_EVENT_CLICKED, NULL);

    // --- Status label ---
    remote_lblStatus = lv_label_create(ui_scrRemote);
    lv_obj_set_width(remote_lblStatus, 210);
    lv_obj_set_x(remote_lblStatus, -30);
    lv_obj_set_y(remote_lblStatus, 62);
    lv_obj_set_align(remote_lblStatus, LV_ALIGN_TOP_MID);
    lv_label_set_text(remote_lblStatus, "Select or create a profile");
    lv_obj_set_style_text_color(remote_lblStatus, lv_color_hex(0x00FFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(remote_lblStatus, &ui_font_Verdana12, LV_PART_MAIN);

    // --- EDIT toggle button ---
    remote_btnEdit = lv_btn_create(ui_scrRemote);
    lv_obj_set_width(remote_btnEdit, 60);
    lv_obj_set_height(remote_btnEdit, 26);
    lv_obj_set_x(remote_btnEdit, 130);
    lv_obj_set_y(remote_btnEdit, 60);
    lv_obj_set_align(remote_btnEdit, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(remote_btnEdit, lv_color_hex(0x336699), LV_PART_MAIN);
    lv_obj_set_style_radius(remote_btnEdit, 6, LV_PART_MAIN);
    lv_obj_clear_flag(remote_btnEdit, LV_OBJ_FLAG_SCROLLABLE);
    remote_lblEdit = lv_label_create(remote_btnEdit);
    lv_obj_set_align(remote_lblEdit, LV_ALIGN_CENTER);
    lv_label_set_text(remote_lblEdit, "EDIT");
    lv_obj_set_style_text_font(remote_lblEdit, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_text_color(remote_lblEdit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(remote_btnEdit, remote_edit_event_cb, LV_EVENT_CLICKED, NULL);

    // ======================= TAB VIEW =======================
    lv_obj_t *tabview = lv_tabview_create(ui_scrRemote, LV_DIR_TOP, 30);
    lv_obj_set_pos(tabview, 0, 96);
    lv_obj_set_size(tabview, 320, 352);
    lv_obj_set_style_bg_opa(tabview, 0, LV_PART_MAIN);

    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab_btns, 255, LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFF9600), LV_PART_MAIN);
    lv_obj_set_style_text_font(tab_btns, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x003366), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t *tabNumbers = lv_tabview_add_tab(tabview, "Numbers");
    lv_obj_t *tabNav     = lv_tabview_add_tab(tabview, "Nav");
    lv_obj_t *tabMedia   = lv_tabview_add_tab(tabview, "Media");

    lv_obj_clear_flag(tabNumbers, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tabNav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tabMedia, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_opa(tabNumbers, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabNav, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabMedia, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabNumbers, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabNav, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabMedia, 0, LV_PART_MAIN);

    // ==================== NUMBERS TAB ====================
    int y, bw = 85, bh = 48;

    // Row 1: POWER, MUTE, LAST, EXIT
    y = 28;
    remote_createBtn(tabNumbers, -115, y, 68, 38, "POWER", 0xCC0000, RB_POWER);
    remote_createBtn(tabNumbers,  -42, y, 68, 38, "MUTE",  0x994400, RB_MUTE);
    remote_createBtn(tabNumbers,   42, y, 68, 38, "LAST",  0x333366, RB_LAST);
    remote_createBtn(tabNumbers,  115, y, 68, 38, "EXIT",  0x333366, RB_EXIT);

    // Numpad rows
    bw = 85; bh = 50;
    y = 74;
    remote_createBtn(tabNumbers, -90, y, bw, bh, "1", 0x2A2A4A, RB_1);
    remote_createBtn(tabNumbers,   0, y, bw, bh, "2", 0x2A2A4A, RB_2);
    remote_createBtn(tabNumbers,  90, y, bw, bh, "3", 0x2A2A4A, RB_3);

    y = 130;
    remote_createBtn(tabNumbers, -90, y, bw, bh, "4", 0x2A2A4A, RB_4);
    remote_createBtn(tabNumbers,   0, y, bw, bh, "5", 0x2A2A4A, RB_5);
    remote_createBtn(tabNumbers,  90, y, bw, bh, "6", 0x2A2A4A, RB_6);

    y = 186;
    remote_createBtn(tabNumbers, -90, y, bw, bh, "7", 0x2A2A4A, RB_7);
    remote_createBtn(tabNumbers,   0, y, bw, bh, "8", 0x2A2A4A, RB_8);
    remote_createBtn(tabNumbers,  90, y, bw, bh, "9", 0x2A2A4A, RB_9);

    y = 242;
    remote_createBtn(tabNumbers,   0, y, bw, bh, "0", 0x2A2A4A, RB_0);

    // ==================== NAV TAB ====================
    // Top row: BACK, MENU, HOME
    y = 23;
    remote_createBtn(tabNav, -95, y, 80, 40, "BACK", 0x333366, RB_BACK);
    remote_createBtn(tabNav,   0, y, 80, 40, "MENU", 0x333366, RB_MENU);
    remote_createBtn(tabNav,  95, y, 80, 40, "HOME", 0x333366, RB_HOME);

    // D-pad: UP
    y = 79;
    remote_createBtn(tabNav, 0, y, 80, 50, LV_SYMBOL_UP, 0x336699, RB_UP);

    // D-pad: LEFT, OK, RIGHT
    y = 137;
    remote_createBtn(tabNav, -90, y, 80, 50, LV_SYMBOL_LEFT, 0x336699, RB_LEFT);
    remote_createBtn(tabNav,   0, y, 80, 50, "OK", 0x006633, RB_OK);
    remote_createBtn(tabNav,  90, y, 80, 50, LV_SYMBOL_RIGHT, 0x336699, RB_RIGHT);

    // D-pad: DOWN
    y = 195;
    remote_createBtn(tabNav, 0, y, 80, 50, LV_SYMBOL_DOWN, 0x336699, RB_DOWN);

    // Bottom row: GUIDE, INFO
    y = 259;
    remote_createBtn(tabNav, -60, y, 105, 40, "GUIDE", 0x444466, RB_GUIDE);
    remote_createBtn(tabNav,  60, y, 105, 40, "INFO",  0x444466, RB_INFO);

    // ==================== MEDIA TAB ====================
    // Row 1: VOL+, VOL-, CH+, CH-
    y = 74;
    remote_createBtn(tabMedia, -115, y, 68, 50, "VOL +", 0x006633, RB_VOL_UP);
    remote_createBtn(tabMedia,  -38, y, 68, 50, "VOL -", 0x663300, RB_VOL_DOWN);
    remote_createBtn(tabMedia,   38, y, 68, 50, "CH +",  0x006633, RB_CH_UP);
    remote_createBtn(tabMedia,  115, y, 68, 50, "CH -",  0x663300, RB_CH_DOWN);

    // Row 2: PREV, PLAY, PAUSE, NEXT
    y = 136;
    remote_createBtn(tabMedia, -115, y, 68, 50, LV_SYMBOL_PREV,  0x336699, RB_PREV);
    remote_createBtn(tabMedia,  -38, y, 68, 50, LV_SYMBOL_PLAY,  0x006633, RB_PLAY);
    remote_createBtn(tabMedia,   38, y, 68, 50, LV_SYMBOL_PAUSE, 0x994400, RB_PAUSE);
    remote_createBtn(tabMedia,  115, y, 68, 50, LV_SYMBOL_NEXT,  0x336699, RB_NEXT);

    // Row 3: STOP, INPUT, RECORD
    y = 198;
    remote_createBtn(tabMedia, -90, y, 85, 50, LV_SYMBOL_STOP, 0xCC0000, RB_STOP);
    remote_createBtn(tabMedia,   0, y, 85, 50, "INPUT",         0x333366, RB_INPUT);
    remote_createBtn(tabMedia,  90, y, 85, 50, "REC",           0x990000, RB_RECORD);

    // ==================== BACK BUTTON ====================
    lv_obj_t *btnBack = lv_btn_create(ui_scrRemote);
    lv_obj_set_width(btnBack, 90);
    lv_obj_set_height(btnBack, 30);
    lv_obj_set_x(btnBack, -115);
    lv_obj_set_y(btnBack, 453);
    lv_obj_set_align(btnBack, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0xFFF700), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnBack, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(btnBack, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnBack, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblBack = lv_label_create(btnBack);
    lv_obj_set_align(lblBack, LV_ALIGN_CENTER);
    lv_label_set_text(lblBack, "BACK");
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(btnBack, remote_back_event_cb, LV_EVENT_CLICKED, NULL);

    // ======================= FILE PICKER OVERLAY =======================
    remote_pickerPanel = lv_obj_create(ui_scrRemote);
    lv_obj_set_size(remote_pickerPanel, 290, 290);
    lv_obj_set_align(remote_pickerPanel, LV_ALIGN_CENTER);
    lv_obj_set_y(remote_pickerPanel, -20);
    lv_obj_clear_flag(remote_pickerPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(remote_pickerPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(remote_pickerPanel, lv_color_hex(0x0A0A1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(remote_pickerPanel, 245, LV_PART_MAIN);
    lv_obj_set_style_border_color(remote_pickerPanel, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_border_width(remote_pickerPanel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(remote_pickerPanel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(remote_pickerPanel, 10, LV_PART_MAIN);

    // Picker title
    remote_pickerTitle = lv_label_create(remote_pickerPanel);
    lv_obj_set_x(remote_pickerTitle, 0);
    lv_obj_set_y(remote_pickerTitle, -5);
    lv_obj_set_align(remote_pickerTitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(remote_pickerTitle, "Assign: BUTTON");
    lv_obj_set_style_text_color(remote_pickerTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(remote_pickerTitle, &ui_font_Verdana16, LV_PART_MAIN);

    // Folder label + dropdown
    lv_obj_t *lblFolder = lv_label_create(remote_pickerPanel);
    lv_obj_set_x(lblFolder, -110);
    lv_obj_set_y(lblFolder, 35);
    lv_obj_set_align(lblFolder, LV_ALIGN_TOP_MID);
    lv_label_set_text(lblFolder, "Folder:");
    lv_obj_set_style_text_color(lblFolder, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblFolder, &ui_font_Verdana12, LV_PART_MAIN);

    remote_pickerDdlFolder = lv_dropdown_create(remote_pickerPanel);
    lv_obj_set_width(remote_pickerDdlFolder, 200);
    lv_obj_set_x(remote_pickerDdlFolder, 20);
    lv_obj_set_y(remote_pickerDdlFolder, 28);
    lv_obj_set_align(remote_pickerDdlFolder, LV_ALIGN_TOP_MID);
    lv_dropdown_set_options(remote_pickerDdlFolder, "");
    lv_obj_set_style_text_font(remote_pickerDdlFolder, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(remote_pickerDdlFolder, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_text_color(remote_pickerDdlFolder, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(remote_pickerDdlFolder, remote_picker_folder_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // File label + dropdown
    lv_obj_t *lblFile = lv_label_create(remote_pickerPanel);
    lv_obj_set_x(lblFile, -110);
    lv_obj_set_y(lblFile, 75);
    lv_obj_set_align(lblFile, LV_ALIGN_TOP_MID);
    lv_label_set_text(lblFile, "File:");
    lv_obj_set_style_text_color(lblFile, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblFile, &ui_font_Verdana12, LV_PART_MAIN);

    remote_pickerDdlFile = lv_dropdown_create(remote_pickerPanel);
    lv_obj_set_width(remote_pickerDdlFile, 200);
    lv_obj_set_x(remote_pickerDdlFile, 20);
    lv_obj_set_y(remote_pickerDdlFile, 68);
    lv_obj_set_align(remote_pickerDdlFile, LV_ALIGN_TOP_MID);
    lv_dropdown_set_options(remote_pickerDdlFile, "");
    lv_obj_set_style_text_font(remote_pickerDdlFile, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(remote_pickerDdlFile, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_text_color(remote_pickerDdlFile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    // Current assignment display
    lv_obj_t *lblCurrent = lv_label_create(remote_pickerPanel);
    lv_obj_set_x(lblCurrent, 0);
    lv_obj_set_y(lblCurrent, 115);
    lv_obj_set_align(lblCurrent, LV_ALIGN_TOP_MID);
    lv_label_set_text(lblCurrent, "");
    lv_obj_set_style_text_color(lblCurrent, lv_color_hex(0x00FFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblCurrent, &ui_font_Verdana12, LV_PART_MAIN);

    // ASSIGN button
    lv_obj_t *btnAssign = lv_btn_create(remote_pickerPanel);
    lv_obj_set_width(btnAssign, 80);
    lv_obj_set_height(btnAssign, 38);
    lv_obj_set_x(btnAssign, -85);
    lv_obj_set_y(btnAssign, 145);
    lv_obj_set_align(btnAssign, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnAssign, lv_color_hex(0x006633), LV_PART_MAIN);
    lv_obj_set_style_radius(btnAssign, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnAssign, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *la = lv_label_create(btnAssign);
    lv_obj_set_align(la, LV_ALIGN_CENTER);
    lv_label_set_text(la, "ASSIGN");
    lv_obj_set_style_text_font(la, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_style_text_color(la, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(btnAssign, remote_picker_assign_cb, LV_EVENT_CLICKED, NULL);

    // CLEAR button
    lv_obj_t *btnClear = lv_btn_create(remote_pickerPanel);
    lv_obj_set_width(btnClear, 75);
    lv_obj_set_height(btnClear, 38);
    lv_obj_set_x(btnClear, 0);
    lv_obj_set_y(btnClear, 145);
    lv_obj_set_align(btnClear, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnClear, lv_color_hex(0x990000), LV_PART_MAIN);
    lv_obj_set_style_radius(btnClear, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnClear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lc = lv_label_create(btnClear);
    lv_obj_set_align(lc, LV_ALIGN_CENTER);
    lv_label_set_text(lc, "CLEAR");
    lv_obj_set_style_text_font(lc, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(btnClear, remote_picker_clear_cb, LV_EVENT_CLICKED, NULL);

    // CANCEL button
    lv_obj_t *btnCancel = lv_btn_create(remote_pickerPanel);
    lv_obj_set_width(btnCancel, 80);
    lv_obj_set_height(btnCancel, 38);
    lv_obj_set_x(btnCancel, 85);
    lv_obj_set_y(btnCancel, 145);
    lv_obj_set_align(btnCancel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(btnCancel, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btnCancel, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btnCancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lx = lv_label_create(btnCancel);
    lv_obj_set_align(lx, LV_ALIGN_CENTER);
    lv_label_set_text(lx, "CANCEL");
    lv_obj_set_style_text_font(lx, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lx, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(btnCancel, remote_picker_cancel_cb, LV_EVENT_CLICKED, NULL);

    // ======================= NAME INPUT OVERLAY =======================
    remote_namePanel = lv_obj_create(ui_scrRemote);
    lv_obj_set_size(remote_namePanel, 310, 320);
    lv_obj_set_align(remote_namePanel, LV_ALIGN_CENTER);
    lv_obj_set_y(remote_namePanel, 30);
    lv_obj_clear_flag(remote_namePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(remote_namePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(remote_namePanel, lv_color_hex(0x0A0A1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(remote_namePanel, 250, LV_PART_MAIN);
    lv_obj_set_style_border_color(remote_namePanel, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_border_width(remote_namePanel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(remote_namePanel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(remote_namePanel, 8, LV_PART_MAIN);

    lv_obj_t *nameTitle = lv_label_create(remote_namePanel);
    lv_obj_set_align(nameTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(nameTitle, 0);
    lv_label_set_text(nameTitle, "Profile Name:");
    lv_obj_set_style_text_color(nameTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(nameTitle, &ui_font_Verdana16, LV_PART_MAIN);

    remote_nameTxt = lv_textarea_create(remote_namePanel);
    lv_obj_set_width(remote_nameTxt, 260);
    lv_obj_set_height(remote_nameTxt, 36);
    lv_obj_set_align(remote_nameTxt, LV_ALIGN_TOP_MID);
    lv_obj_set_y(remote_nameTxt, 25);
    lv_textarea_set_max_length(remote_nameTxt, RB_MAX_NAME - 5);  // room for .remote extension
    lv_textarea_set_one_line(remote_nameTxt, true);
    lv_textarea_set_text(remote_nameTxt, "");
    lv_obj_set_style_text_color(remote_nameTxt, lv_color_hex(0x00FFEB), LV_PART_MAIN);
    lv_obj_set_style_text_font(remote_nameTxt, &ui_font_Verdana16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(remote_nameTxt, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(remote_nameTxt, 255, LV_PART_MAIN);
    lv_obj_add_event_cb(remote_nameTxt, remote_name_ready_cb, LV_EVENT_READY, NULL);

    // Keyboard for name input
    remote_nameKbd = lv_keyboard_create(remote_namePanel);
    lv_obj_set_size(remote_nameKbd, 280, 200);
    lv_obj_set_align(remote_nameKbd, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(remote_nameKbd, 5);
    lv_keyboard_set_textarea(remote_nameKbd, remote_nameTxt);
    lv_obj_set_style_bg_color(remote_nameKbd, lv_color_hex(0x0A0A1E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(remote_nameKbd, lv_color_hex(0x2A2A4A), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(remote_nameKbd, 255, LV_PART_ITEMS);
    lv_obj_set_style_text_color(remote_nameKbd, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);

    // Cancel button for name overlay
    lv_obj_t *nameCancelBtn = lv_btn_create(remote_namePanel);
    lv_obj_set_width(nameCancelBtn, 70);
    lv_obj_set_height(nameCancelBtn, 28);
    lv_obj_set_x(nameCancelBtn, 100);
    lv_obj_set_y(nameCancelBtn, 25);
    lv_obj_set_align(nameCancelBtn, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(nameCancelBtn, lv_color_hex(0x990000), LV_PART_MAIN);
    lv_obj_set_style_radius(nameCancelBtn, 4, LV_PART_MAIN);
    lv_obj_clear_flag(nameCancelBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ncl = lv_label_create(nameCancelBtn);
    lv_obj_set_align(ncl, LV_ALIGN_CENTER);
    lv_label_set_text(ncl, "Cancel");
    lv_obj_set_style_text_font(ncl, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ncl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(nameCancelBtn, remote_name_cancel_cb, LV_EVENT_CLICKED, NULL);

    // Initial button styling (all dimmed)
    remote_updateButtonStyles();
}

#endif // REMOTE_SCREEN_H
