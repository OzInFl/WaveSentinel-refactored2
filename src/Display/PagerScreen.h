#ifndef PAGER_SCREEN_H
#define PAGER_SCREEN_H

// =====================================================================
// PagerScreen.h — Virtual pager UI (hand-built LVGL, no SquareLine)
//
// Screens:
//   pgScr      — pager "face": system picker, RX indicator, live message
//                feed, MONITOR toggle, SETTINGS / CLEAR / BACK
//   pgSetScr   — settings: monitor mode, volume, logging, alert-on-all,
//                default tone (+ TEST), and nav to Systems / RICs
//   pgSysScr   — systems editor: name, frequency, format, invert
//   pgRicScr   — RIC watchlist editor: capcode, label, tone (+TEST), enable
//
// Data + radio + decoding all live in Pager.h.
// =====================================================================

#include <lvgl.h>
#include <ui.h>
#include "Pager/Pager.h"
#include "Display/Event.h"     // currentState, STATE_PAGER, STATE_IDLE

// ---- screen handles ----
static lv_obj_t *pgScr    = NULL;
static lv_obj_t *pgSetScr = NULL;
static lv_obj_t *pgSysScr = NULL;
static lv_obj_t *pgRicScr = NULL;

// ---- face widgets ----
static lv_obj_t *pg_ddlSystem = NULL;
static lv_obj_t *pg_lblFreq   = NULL;
static lv_obj_t *pg_lblRx     = NULL;
static lv_obj_t *pg_lblStatus = NULL;
static lv_obj_t *pg_taMsgs    = NULL;
static lv_obj_t *pg_lblMonitor= NULL;

// ---- settings widgets ----
static lv_obj_t *pg_swMode      = NULL;
static lv_obj_t *pg_lblMode     = NULL;
static lv_obj_t *pg_sliderVol   = NULL;
static lv_obj_t *pg_lblVol      = NULL;
static lv_obj_t *pg_swLog       = NULL;
static lv_obj_t *pg_swAlertAll  = NULL;
static lv_obj_t *pg_ddlDefSound = NULL;

// ---- systems editor widgets ----
static lv_obj_t *pg_ddlSysSel   = NULL;
static lv_obj_t *pg_taSysName   = NULL;
static lv_obj_t *pg_taSysFreq   = NULL;
static lv_obj_t *pg_ddlSysFmt   = NULL;
static lv_obj_t *pg_swSysInvert = NULL;
static int       pg_sysEditIdx  = -1;   // -1 == new

// ---- RIC editor widgets ----
static lv_obj_t *pg_ddlRicSel   = NULL;
static lv_obj_t *pg_taRicCode   = NULL;
static lv_obj_t *pg_taRicLabel  = NULL;
static lv_obj_t *pg_ddlRicSound = NULL;
static lv_obj_t *pg_swRicEn     = NULL;
static int       pg_ricEditIdx  = -1;

// ---- shared keyboard overlay ----
static lv_obj_t *pgKbd = NULL;

// ---- message feed bookkeeping ----
static uint16_t  pg_shownCount = 0;

// forward decls
static void pager_open_screen();
static void pg_refresh_system_ddl();
static void pg_refresh_sys_editor();
static void pg_refresh_ric_editor();
static void pg_restart_if_running();

// =====================================================================
// Small helpers
// =====================================================================
static lv_obj_t *pg_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *txt, lv_event_cb_t cb, uint32_t col) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *pg_make_label(lv_obj_t *parent, int x, int y, const char *txt,
                               uint32_t col, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    return l;
}

static void pg_style_ddl(lv_obj_t *d) {
    lv_obj_set_style_bg_color(d, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_text_color(d, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(d, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 1, LV_PART_MAIN);
}

// ---- keyboard overlay ----
static void pg_kbd_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (pgKbd) { lv_obj_add_flag(pgKbd, LV_OBJ_FLAG_HIDDEN); }
    }
}
static void pg_show_kbd(lv_obj_t *ta, lv_keyboard_mode_t mode) {
    if (!pgKbd) {
        pgKbd = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(pgKbd, 320, 180);
        lv_obj_align(pgKbd, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(pgKbd, pg_kbd_event, LV_EVENT_ALL, NULL);
    }
    lv_keyboard_set_mode(pgKbd, mode);
    lv_keyboard_set_textarea(pgKbd, ta);
    lv_obj_clear_flag(pgKbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pgKbd);
}
static void pg_ta_focus_text(lv_event_t *e) {
    pg_show_kbd(lv_event_get_target(e), LV_KEYBOARD_MODE_TEXT_LOWER);
}
static void pg_ta_focus_num(lv_event_t *e) {
    pg_show_kbd(lv_event_get_target(e), LV_KEYBOARD_MODE_NUMBER);
}

// =====================================================================
// Live message feed sync — call under lvgl_mutex from loop() (STATE_PAGER)
// =====================================================================
static void pager_screen_sync() {
    if (!pgScr) return;

    // RX indicator + status line
    if (pg_lblRx) {
        lv_label_set_text(pg_lblRx, LV_SYMBOL_GPS);   // dot-like glyph
        lv_obj_set_style_text_color(pg_lblRx,
            lv_color_hex(pgRunning ? 0x00FF66 : 0x555555), LV_PART_MAIN);
    }
    if (pg_lblStatus) {
        char s[48];
        snprintf(s, sizeof(s), "%s   Pages:%lu",
                 (pgMonitorMode == PM_SELECTED) ? "SELECTED" : "ALL PAGES",
                 (unsigned long)pgPageCount);
        lv_label_set_text(pg_lblStatus, s);
    }

    if (!pgUiDirty || !pg_taMsgs) return;
    pgUiDirty = false;

    uint16_t total = pgHistCount;
    uint16_t size  = pager_history_size();
    if (total < pg_shownCount) { pg_shownCount = 0; lv_textarea_set_text(pg_taMsgs, ""); }
    uint16_t newCount = total - pg_shownCount;
    if (newCount > size) newCount = size;

    for (uint16_t i = size - newCount; i < size; ++i) {
        const PagerMessage *m = pager_history_at(i);
        if (!m) continue;
        char line[160];
        if (m->ric == 0) {
            snprintf(line, sizeof(line), "  %s\n", m->text);
        } else {
            const char *mk   = m->matched ? "* " : "  ";
            const char *body = (m->type == MT_TONE) ? "<tone>" : m->text;
            snprintf(line, sizeof(line), "%sR%lu: %s\n", mk, (unsigned long)m->ric, body);
        }
        lv_textarea_add_text(pg_taMsgs, line);
    }
    pg_shownCount = total;

    // Trim the textarea if it grows too large (keep the recent tail)
    const char *cur = lv_textarea_get_text(pg_taMsgs);
    size_t clen = cur ? strlen(cur) : 0;
    if (clen > 4000) {
        const char *keep = cur + (clen - 2500);
        const char *nl = strchr(keep, '\n');
        if (nl) keep = nl + 1;
        char *tmp = (char *)malloc(strlen(keep) + 1);
        if (tmp) { strcpy(tmp, keep); lv_textarea_set_text(pg_taMsgs, tmp); free(tmp); }
    }
    lv_textarea_set_cursor_pos(pg_taMsgs, LV_TEXTAREA_CURSOR_LAST);
}

// =====================================================================
// FACE screen callbacks
// =====================================================================
static void pg_cb_monitor(lv_event_t *e) {
    if (pgRunning) {
        pager_stop();
        currentState = STATE_IDLE;
        lv_label_set_text(pg_lblMonitor, "MONITOR: OFF");
    } else {
        pager_start();
        currentState = STATE_PAGER;
        lv_label_set_text(pg_lblMonitor, "MONITOR: ON");
    }
}

static void pg_cb_system_changed(lv_event_t *e) {
    uint16_t idx = lv_dropdown_get_selected(pg_ddlSystem);
    pager_set_selected_system((uint8_t)idx);     // saves + restarts if running
    if (idx < pgSystemCount) {
        char f[32];
        snprintf(f, sizeof(f), "%.4f MHz  %s",
                 pgSystems[idx].freqMHz, PAGER_FORMAT_NAMES[pgSystems[idx].format]);
        lv_label_set_text(pg_lblFreq, f);
    }
}

static void pg_cb_clear(lv_event_t *e) {
    pager_clear_history();
    pg_shownCount = 0;
    if (pg_taMsgs) lv_textarea_set_text(pg_taMsgs, "");
}

static void pg_cb_face_back(lv_event_t *e) {
    pager_stop();
    currentState = STATE_IDLE;
    if (pgKbd) lv_obj_add_flag(pgKbd, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(ui_scrCC1101Stuff);
}

static void pg_cb_open_settings(lv_event_t *e) {
    // reflect current values
    if (pg_swMode) {
        if (pgMonitorMode == PM_SELECTED) lv_obj_add_state(pg_swMode, LV_STATE_CHECKED);
        else lv_obj_clear_state(pg_swMode, LV_STATE_CHECKED);
        lv_label_set_text(pg_lblMode, pgMonitorMode == PM_SELECTED ? "Monitor: SELECTED RICs" : "Monitor: ALL pages");
    }
    if (pg_sliderVol) {
        lv_slider_set_value(pg_sliderVol, pgVolume, LV_ANIM_OFF);
        char v[20]; snprintf(v, sizeof(v), "Volume: %u", pgVolume);
        lv_label_set_text(pg_lblVol, v);
    }
    if (pg_swLog)      { pgLogging   ? lv_obj_add_state(pg_swLog, LV_STATE_CHECKED)      : lv_obj_clear_state(pg_swLog, LV_STATE_CHECKED); }
    if (pg_swAlertAll) { pgAlertOnAll? lv_obj_add_state(pg_swAlertAll, LV_STATE_CHECKED): lv_obj_clear_state(pg_swAlertAll, LV_STATE_CHECKED); }
    if (pg_ddlDefSound) lv_dropdown_set_selected(pg_ddlDefSound, pgDefaultSound - 1);
    lv_scr_load(pgSetScr);
}

// =====================================================================
// SETTINGS screen callbacks
// =====================================================================
static void pg_cb_mode(lv_event_t *e) {
    bool sel = lv_obj_has_state(pg_swMode, LV_STATE_CHECKED);
    pager_set_mode(sel ? PM_SELECTED : PM_ALL);
    lv_label_set_text(pg_lblMode, sel ? "Monitor: SELECTED RICs" : "Monitor: ALL pages");
}
static void pg_cb_vol(lv_event_t *e) {
    uint8_t v = (uint8_t)lv_slider_get_value(pg_sliderVol);
    pager_set_volume(v);
    char s[20]; snprintf(s, sizeof(s), "Volume: %u", v);
    lv_label_set_text(pg_lblVol, s);
}
static void pg_cb_log(lv_event_t *e) {
    pager_set_logging(lv_obj_has_state(pg_swLog, LV_STATE_CHECKED));
}
static void pg_cb_alertall(lv_event_t *e) {
    pager_set_alert_on_all(lv_obj_has_state(pg_swAlertAll, LV_STATE_CHECKED));
}
static void pg_cb_defsound(lv_event_t *e) {
    pager_set_default_sound((uint8_t)lv_dropdown_get_selected(pg_ddlDefSound) + 1);
}
static void pg_cb_test_defsound(lv_event_t *e) {
    pager_tones_play((uint8_t)lv_dropdown_get_selected(pg_ddlDefSound) + 1, pgVolume);
}
static void pg_cb_settings_back(lv_event_t *e) {
    if (pgKbd) lv_obj_add_flag(pgKbd, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(pgScr);
}
static void pg_cb_open_systems(lv_event_t *e) { pg_refresh_sys_editor(); lv_scr_load(pgSysScr); }
static void pg_cb_open_rics(lv_event_t *e)    { pg_refresh_ric_editor(); lv_scr_load(pgRicScr); }

// =====================================================================
// SYSTEMS editor
// =====================================================================
static void pg_load_system_fields(int idx) {
    pg_sysEditIdx = idx;
    if (idx >= 0 && idx < pgSystemCount) {
        lv_textarea_set_text(pg_taSysName, pgSystems[idx].name);
        char f[24]; snprintf(f, sizeof(f), "%.4f", pgSystems[idx].freqMHz);
        lv_textarea_set_text(pg_taSysFreq, f);
        lv_dropdown_set_selected(pg_ddlSysFmt, pgSystems[idx].format);
        pgSystems[idx].invert ? lv_obj_add_state(pg_swSysInvert, LV_STATE_CHECKED)
                              : lv_obj_clear_state(pg_swSysInvert, LV_STATE_CHECKED);
    } else {
        lv_textarea_set_text(pg_taSysName, "New System");
        lv_textarea_set_text(pg_taSysFreq, "460.6125");
        lv_dropdown_set_selected(pg_ddlSysFmt, PF_POCSAG_AUTO);
        lv_obj_clear_state(pg_swSysInvert, LV_STATE_CHECKED);
    }
}
static void pg_refresh_sys_editor() {
    char opts[PAGER_MAX_SYSTEMS * (PAGER_SYS_NAME_LEN + 1) + 8];
    opts[0] = '\0';
    for (uint8_t i = 0; i < pgSystemCount; ++i) {
        strncat(opts, pgSystems[i].name, sizeof(opts) - strlen(opts) - 2);
        if (i < pgSystemCount - 1) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(pg_ddlSysSel, opts);
    uint8_t sel = (pgSelectedSys < pgSystemCount) ? pgSelectedSys : 0;
    lv_dropdown_set_selected(pg_ddlSysSel, sel);
    pg_load_system_fields(sel);
}
static void pg_cb_sys_sel(lv_event_t *e) {
    pg_load_system_fields((int)lv_dropdown_get_selected(pg_ddlSysSel));
}
static void pg_cb_sys_new(lv_event_t *e)  { pg_load_system_fields(-1); }
static void pg_cb_sys_save(lv_event_t *e) {
    char name[PAGER_SYS_NAME_LEN];
    strncpy(name, lv_textarea_get_text(pg_taSysName), PAGER_SYS_NAME_LEN - 1);
    name[PAGER_SYS_NAME_LEN - 1] = '\0';
    float freq = atof(lv_textarea_get_text(pg_taSysFreq));
    uint8_t fmt = (uint8_t)lv_dropdown_get_selected(pg_ddlSysFmt);
    uint8_t inv = lv_obj_has_state(pg_swSysInvert, LV_STATE_CHECKED) ? 1 : 0;
    if (freq < 300.0f || freq > 960.0f) return;               // CC1101 range guard

    if (pg_sysEditIdx >= 0 && pg_sysEditIdx < pgSystemCount)
        pager_update_system(pg_sysEditIdx, name, freq, fmt, inv);
    else
        pager_add_system(name, freq, fmt, inv);
    pg_refresh_sys_editor();
    pg_refresh_system_ddl();
    pg_restart_if_running();
}
static void pg_cb_sys_delete(lv_event_t *e) {
    if (pg_sysEditIdx >= 0) pager_delete_system((uint8_t)pg_sysEditIdx);
    pg_refresh_sys_editor();
    pg_refresh_system_ddl();
}
static void pg_cb_sys_back(lv_event_t *e) {
    if (pgKbd) lv_obj_add_flag(pgKbd, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(pgSetScr);
}

// =====================================================================
// RIC editor
// =====================================================================
static void pg_load_ric_fields(int idx) {
    pg_ricEditIdx = idx;
    if (idx >= 0 && idx < pgRicCount) {
        char c[16]; snprintf(c, sizeof(c), "%lu", (unsigned long)pgRics[idx].ric);
        lv_textarea_set_text(pg_taRicCode, c);
        lv_textarea_set_text(pg_taRicLabel, pgRics[idx].label);
        lv_dropdown_set_selected(pg_ddlRicSound, pgRics[idx].sound - 1);
        pgRics[idx].enabled ? lv_obj_add_state(pg_swRicEn, LV_STATE_CHECKED)
                            : lv_obj_clear_state(pg_swRicEn, LV_STATE_CHECKED);
    } else {
        lv_textarea_set_text(pg_taRicCode, "");
        lv_textarea_set_text(pg_taRicLabel, "New RIC");
        lv_dropdown_set_selected(pg_ddlRicSound, 0);
        lv_obj_add_state(pg_swRicEn, LV_STATE_CHECKED);
    }
}
static void pg_refresh_ric_editor() {
    if (pgRicCount == 0) {
        lv_dropdown_set_options(pg_ddlRicSel, "(none)");
        lv_dropdown_set_selected(pg_ddlRicSel, 0);
        pg_load_ric_fields(-1);
        return;
    }
    // Build "RIC  label" options
    static char opts[PAGER_MAX_RICS * (PAGER_RIC_LABEL_LEN + 14)];
    opts[0] = '\0';
    for (uint8_t i = 0; i < pgRicCount; ++i) {
        char row[PAGER_RIC_LABEL_LEN + 16];
        snprintf(row, sizeof(row), "%lu %s%s", (unsigned long)pgRics[i].ric,
                 pgRics[i].label, (i < pgRicCount - 1) ? "\n" : "");
        strncat(opts, row, sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(pg_ddlRicSel, opts);
    lv_dropdown_set_selected(pg_ddlRicSel, 0);
    pg_load_ric_fields(0);
}
static void pg_cb_ric_sel(lv_event_t *e) {
    if (pgRicCount == 0) { pg_load_ric_fields(-1); return; }
    pg_load_ric_fields((int)lv_dropdown_get_selected(pg_ddlRicSel));
}
static void pg_cb_ric_new(lv_event_t *e)  { pg_load_ric_fields(-1); }
static void pg_cb_ric_test(lv_event_t *e) {
    pager_tones_play((uint8_t)lv_dropdown_get_selected(pg_ddlRicSound) + 1, pgVolume);
}
static void pg_cb_ric_save(lv_event_t *e) {
    uint32_t ric = (uint32_t)strtoul(lv_textarea_get_text(pg_taRicCode), NULL, 10);
    if (ric == 0) return;
    char label[PAGER_RIC_LABEL_LEN];
    strncpy(label, lv_textarea_get_text(pg_taRicLabel), PAGER_RIC_LABEL_LEN - 1);
    label[PAGER_RIC_LABEL_LEN - 1] = '\0';
    uint8_t sound = (uint8_t)lv_dropdown_get_selected(pg_ddlRicSound) + 1;
    uint8_t en    = lv_obj_has_state(pg_swRicEn, LV_STATE_CHECKED) ? 1 : 0;

    if (pg_ricEditIdx >= 0 && pg_ricEditIdx < pgRicCount)
        pager_update_ric((uint8_t)pg_ricEditIdx, label, sound, en);
    else
        pager_add_ric(ric, label, sound, en);
    pg_refresh_ric_editor();
}
static void pg_cb_ric_delete(lv_event_t *e) {
    if (pg_ricEditIdx >= 0) pager_delete_ric((uint8_t)pg_ricEditIdx);
    pg_refresh_ric_editor();
}
static void pg_cb_ric_back(lv_event_t *e) {
    if (pgKbd) lv_obj_add_flag(pgKbd, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(pgSetScr);
}

// =====================================================================
// Shared: rebuild face system dropdown + restart helper
// =====================================================================
static void pg_refresh_system_ddl() {
    if (!pg_ddlSystem) return;
    char opts[PAGER_MAX_SYSTEMS * (PAGER_SYS_NAME_LEN + 1) + 8];
    opts[0] = '\0';
    for (uint8_t i = 0; i < pgSystemCount; ++i) {
        strncat(opts, pgSystems[i].name, sizeof(opts) - strlen(opts) - 2);
        if (i < pgSystemCount - 1) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(pg_ddlSystem, opts);
    if (pgSelectedSys < pgSystemCount) {
        lv_dropdown_set_selected(pg_ddlSystem, pgSelectedSys);
        char f[40];
        snprintf(f, sizeof(f), "%.4f MHz  %s",
                 pgSystems[pgSelectedSys].freqMHz,
                 PAGER_FORMAT_NAMES[pgSystems[pgSelectedSys].format]);
        if (pg_lblFreq) lv_label_set_text(pg_lblFreq, f);
    }
}
static void pg_restart_if_running() {
    if (pgRunning) { pager_stop(); pager_start(); }
}

// =====================================================================
// Screen construction
// =====================================================================
static void pg_build_face() {
    pgScr = lv_obj_create(NULL);
    lv_obj_clear_flag(pgScr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pgScr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(pgScr, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);

    pg_make_label(pgScr, 10, 6, "PAGER", 0xFF9100, &ui_font_Verdana18);
    pg_lblRx = pg_make_label(pgScr, 285, 8, LV_SYMBOL_GPS, 0x555555, &lv_font_montserrat_16);

    // System picker
    pg_ddlSystem = lv_dropdown_create(pgScr);
    lv_obj_set_size(pg_ddlSystem, 300, 34);
    lv_obj_set_pos(pg_ddlSystem, 10, 36);
    pg_style_ddl(pg_ddlSystem);
    lv_obj_add_event_cb(pg_ddlSystem, pg_cb_system_changed, LV_EVENT_VALUE_CHANGED, NULL);

    pg_lblFreq   = pg_make_label(pgScr, 12, 76,  "", 0x00E0FF, &lv_font_montserrat_14);
    pg_lblStatus = pg_make_label(pgScr, 12, 96,  "ALL PAGES", 0xAAAAAA, &lv_font_montserrat_12);

    // Message feed
    pg_taMsgs = lv_textarea_create(pgScr);
    lv_obj_set_size(pg_taMsgs, 300, 250);
    lv_obj_set_pos(pg_taMsgs, 10, 118);
    lv_textarea_set_text(pg_taMsgs, "");
    lv_textarea_set_cursor_click_pos(pg_taMsgs, false);
    lv_obj_clear_flag(pg_taMsgs, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(pg_taMsgs, lv_color_hex(0x02120A), LV_PART_MAIN);
    lv_obj_set_style_text_color(pg_taMsgs, lv_color_hex(0x33FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(pg_taMsgs, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_border_color(pg_taMsgs, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(pg_taMsgs, 2, LV_PART_MAIN);

    // Monitor toggle + status label
    lv_obj_t *bMon = pg_make_btn(pgScr, 10, 378, 150, 34, "MONITOR", pg_cb_monitor, 0x0A3A1A);
    pg_lblMonitor = lv_obj_get_child(bMon, 0);
    lv_label_set_text(pg_lblMonitor, "MONITOR: ON");

    pg_make_btn(pgScr, 168, 378, 142, 34, LV_SYMBOL_SETTINGS " SETTINGS", pg_cb_open_settings, 0x1A2A3A);

    // Bottom row
    pg_make_btn(pgScr, 10,  420, 150, 34, LV_SYMBOL_TRASH " CLEAR", pg_cb_clear, 0x3A1A1A);
    pg_make_btn(pgScr, 168, 420, 142, 34, LV_SYMBOL_LEFT " BACK",  pg_cb_face_back, 0x1A2A3A);
}

static void pg_build_settings() {
    pgSetScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pgSetScr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(pgSetScr, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(pgSetScr, LV_OBJ_FLAG_SCROLLABLE);

    pg_make_label(pgSetScr, 10, 8, "PAGER SETTINGS", 0xFF9100, &ui_font_Verdana16);

    // Monitor mode
    pg_lblMode = pg_make_label(pgSetScr, 12, 48, "Monitor: ALL pages", 0xFFFFFF, &lv_font_montserrat_14);
    pg_swMode = lv_switch_create(pgSetScr);
    lv_obj_set_pos(pg_swMode, 250, 44);
    lv_obj_add_event_cb(pg_swMode, pg_cb_mode, LV_EVENT_VALUE_CHANGED, NULL);

    // Volume
    pg_lblVol = pg_make_label(pgSetScr, 12, 86, "Volume: 12", 0xFFFFFF, &lv_font_montserrat_14);
    pg_sliderVol = lv_slider_create(pgSetScr);
    lv_slider_set_range(pg_sliderVol, 0, 100);
    lv_obj_set_size(pg_sliderVol, 290, 12);
    lv_obj_set_pos(pg_sliderVol, 14, 112);
    lv_obj_add_event_cb(pg_sliderVol, pg_cb_vol, LV_EVENT_VALUE_CHANGED, NULL);

    // Logging
    pg_make_label(pgSetScr, 12, 138, "Log pages to SD", 0xFFFFFF, &lv_font_montserrat_14);
    pg_swLog = lv_switch_create(pgSetScr);
    lv_obj_set_pos(pg_swLog, 250, 134);
    lv_obj_add_event_cb(pg_swLog, pg_cb_log, LV_EVENT_VALUE_CHANGED, NULL);

    // Alert on all (ALL mode)
    pg_make_label(pgSetScr, 12, 176, "Alert on unlisted", 0xFFFFFF, &lv_font_montserrat_14);
    pg_swAlertAll = lv_switch_create(pgSetScr);
    lv_obj_set_pos(pg_swAlertAll, 250, 172);
    lv_obj_add_event_cb(pg_swAlertAll, pg_cb_alertall, LV_EVENT_VALUE_CHANGED, NULL);

    // Default tone + test
    pg_make_label(pgSetScr, 12, 214, "Default tone", 0xFFFFFF, &lv_font_montserrat_14);
    pg_ddlDefSound = lv_dropdown_create(pgSetScr);
    lv_dropdown_set_options(pg_ddlDefSound,
        "1 Single\n2 Double\n3 Triple\n4 Warble\n5 Sweep");
    lv_obj_set_size(pg_ddlDefSound, 150, 32);
    lv_obj_set_pos(pg_ddlDefSound, 12, 236);
    pg_style_ddl(pg_ddlDefSound);
    lv_obj_add_event_cb(pg_ddlDefSound, pg_cb_defsound, LV_EVENT_VALUE_CHANGED, NULL);
    pg_make_btn(pgSetScr, 172, 236, 130, 32, LV_SYMBOL_PLAY " TEST", pg_cb_test_defsound, 0x1A2A3A);

    // Nav
    pg_make_btn(pgSetScr, 12, 288, 140, 40, "SYSTEMS", pg_cb_open_systems, 0x1A2A3A);
    pg_make_btn(pgSetScr, 164, 288, 140, 40, "RIC LIST", pg_cb_open_rics, 0x1A2A3A);
    pg_make_btn(pgSetScr, 12, 336, 292, 38, LV_SYMBOL_LEFT " BACK TO PAGER", pg_cb_settings_back, 0x1A2A3A);
}

static void pg_build_systems() {
    pgSysScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pgSysScr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(pgSysScr, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(pgSysScr, LV_OBJ_FLAG_SCROLLABLE);

    pg_make_label(pgSysScr, 10, 8, "PAGER SYSTEMS", 0xFF9100, &ui_font_Verdana16);

    pg_ddlSysSel = lv_dropdown_create(pgSysScr);
    lv_obj_set_size(pg_ddlSysSel, 292, 32);
    lv_obj_set_pos(pg_ddlSysSel, 12, 40);
    pg_style_ddl(pg_ddlSysSel);
    lv_obj_add_event_cb(pg_ddlSysSel, pg_cb_sys_sel, LV_EVENT_VALUE_CHANGED, NULL);

    pg_make_label(pgSysScr, 12, 82, "Name", 0xAAAAAA, &lv_font_montserrat_12);
    pg_taSysName = lv_textarea_create(pgSysScr);
    lv_textarea_set_one_line(pg_taSysName, true);
    lv_obj_set_size(pg_taSysName, 292, 34);
    lv_obj_set_pos(pg_taSysName, 12, 98);
    lv_obj_add_event_cb(pg_taSysName, pg_ta_focus_text, LV_EVENT_FOCUSED, NULL);

    pg_make_label(pgSysScr, 12, 138, "Frequency (MHz)", 0xAAAAAA, &lv_font_montserrat_12);
    pg_taSysFreq = lv_textarea_create(pgSysScr);
    lv_textarea_set_one_line(pg_taSysFreq, true);
    lv_obj_set_size(pg_taSysFreq, 292, 34);
    lv_obj_set_pos(pg_taSysFreq, 12, 154);
    lv_obj_add_event_cb(pg_taSysFreq, pg_ta_focus_num, LV_EVENT_FOCUSED, NULL);

    pg_make_label(pgSysScr, 12, 194, "Format", 0xAAAAAA, &lv_font_montserrat_12);
    pg_ddlSysFmt = lv_dropdown_create(pgSysScr);
    lv_dropdown_set_options(pg_ddlSysFmt,
        "POCSAG 512\nPOCSAG 1200\nPOCSAG 2400\nPOCSAG Auto\nFLEX 1600 (exp)");
    lv_obj_set_size(pg_ddlSysFmt, 200, 32);
    lv_obj_set_pos(pg_ddlSysFmt, 12, 210);
    pg_style_ddl(pg_ddlSysFmt);

    pg_make_label(pgSysScr, 220, 194, "Invert", 0xAAAAAA, &lv_font_montserrat_12);
    pg_swSysInvert = lv_switch_create(pgSysScr);
    lv_obj_set_pos(pg_swSysInvert, 234, 210);

    pg_make_btn(pgSysScr, 12, 256, 92, 38, "NEW",    pg_cb_sys_new,    0x1A2A3A);
    pg_make_btn(pgSysScr, 112, 256, 92, 38, "SAVE",  pg_cb_sys_save,   0x0A3A1A);
    pg_make_btn(pgSysScr, 212, 256, 92, 38, "DELETE",pg_cb_sys_delete, 0x3A1A1A);
    pg_make_btn(pgSysScr, 12, 304, 292, 38, LV_SYMBOL_LEFT " BACK", pg_cb_sys_back, 0x1A2A3A);
}

static void pg_build_rics() {
    pgRicScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pgRicScr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(pgRicScr, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(pgRicScr, LV_OBJ_FLAG_SCROLLABLE);

    pg_make_label(pgRicScr, 10, 8, "RIC WATCHLIST", 0xFF9100, &ui_font_Verdana16);

    pg_ddlRicSel = lv_dropdown_create(pgRicScr);
    lv_obj_set_size(pg_ddlRicSel, 292, 32);
    lv_obj_set_pos(pg_ddlRicSel, 12, 40);
    pg_style_ddl(pg_ddlRicSel);
    lv_obj_add_event_cb(pg_ddlRicSel, pg_cb_ric_sel, LV_EVENT_VALUE_CHANGED, NULL);

    pg_make_label(pgRicScr, 12, 82, "RIC / Capcode", 0xAAAAAA, &lv_font_montserrat_12);
    pg_taRicCode = lv_textarea_create(pgRicScr);
    lv_textarea_set_one_line(pg_taRicCode, true);
    lv_obj_set_size(pg_taRicCode, 140, 34);
    lv_obj_set_pos(pg_taRicCode, 12, 98);
    lv_obj_add_event_cb(pg_taRicCode, pg_ta_focus_num, LV_EVENT_FOCUSED, NULL);

    pg_make_label(pgRicScr, 164, 82, "Enabled", 0xAAAAAA, &lv_font_montserrat_12);
    pg_swRicEn = lv_switch_create(pgRicScr);
    lv_obj_set_pos(pg_swRicEn, 180, 98);

    pg_make_label(pgRicScr, 12, 138, "Label", 0xAAAAAA, &lv_font_montserrat_12);
    pg_taRicLabel = lv_textarea_create(pgRicScr);
    lv_textarea_set_one_line(pg_taRicLabel, true);
    lv_obj_set_size(pg_taRicLabel, 292, 34);
    lv_obj_set_pos(pg_taRicLabel, 12, 154);
    lv_obj_add_event_cb(pg_taRicLabel, pg_ta_focus_text, LV_EVENT_FOCUSED, NULL);

    pg_make_label(pgRicScr, 12, 194, "Alert tone", 0xAAAAAA, &lv_font_montserrat_12);
    pg_ddlRicSound = lv_dropdown_create(pgRicScr);
    lv_dropdown_set_options(pg_ddlRicSound,
        "1 Single\n2 Double\n3 Triple\n4 Warble\n5 Sweep");
    lv_obj_set_size(pg_ddlRicSound, 200, 32);
    lv_obj_set_pos(pg_ddlRicSound, 12, 210);
    pg_style_ddl(pg_ddlRicSound);
    pg_make_btn(pgRicScr, 220, 210, 84, 32, LV_SYMBOL_PLAY " TEST", pg_cb_ric_test, 0x1A2A3A);

    pg_make_btn(pgRicScr, 12, 256, 92, 38, "NEW",    pg_cb_ric_new,    0x1A2A3A);
    pg_make_btn(pgRicScr, 112, 256, 92, 38, "SAVE",  pg_cb_ric_save,   0x0A3A1A);
    pg_make_btn(pgRicScr, 212, 256, 92, 38, "DELETE",pg_cb_ric_delete, 0x3A1A1A);
    pg_make_btn(pgRicScr, 12, 304, 292, 38, LV_SYMBOL_LEFT " BACK", pg_cb_ric_back, 0x1A2A3A);
}

// =====================================================================
// Public: build all pager screens (call once in setup after ui_init)
// =====================================================================
static void pager_screen_init() {
    pg_build_face();
    pg_build_settings();
    pg_build_systems();
    pg_build_rics();
    pg_refresh_system_ddl();
}

// Open the pager (auto-start monitoring) — wired to the CC1101 launch btn
static void pager_open_screen() {
    pg_shownCount = pgHistCount;                 // don't replay old feed
    if (pg_taMsgs) lv_textarea_set_text(pg_taMsgs, "");
    pg_refresh_system_ddl();
    pager_start();
    currentState = STATE_PAGER;
    if (pg_lblMonitor) lv_label_set_text(pg_lblMonitor, "MONITOR: ON");
    lv_scr_load(pgScr);
}

// =====================================================================
// Add a "PAGER" tab + launch button to the CC1101 tabview
// (call in setup after ui_init)
// =====================================================================
static void pg_cb_launch(lv_event_t *e) { pager_open_screen(); }

static void pager_add_launch_tab() {
    lv_obj_t *tab = lv_tabview_add_tab(ui_tabCC1101Stuff, "PAGER");
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(tab);
    lv_label_set_text(l,
        "POCSAG / FLEX pager decoder.\n"
        "Monitor capcodes (RICs) with\n"
        "per-RIC alert tones.");
    lv_obj_set_style_text_color(l, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *b = pg_make_btn(tab, 0, 0, 220, 56, LV_SYMBOL_BELL " OPEN PAGER",
                              pg_cb_launch, 0x0A3A1A);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 20);
}

#endif // PAGER_SCREEN_H
