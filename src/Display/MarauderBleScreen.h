#ifndef MARAUDER_BLE_SCREEN_H
#define MARAUDER_BLE_SCREEN_H

// =====================================================================
// MarauderBleScreen.h — hand-coded LVGL screen for Marauder BLE batch.
// Tabbed layout: AirTag | Skimmer | Flock | Meta | Analyzer | Spam+
// All construction is done programmatically (no SquareLine Studio).
// Theme: bg #080810, panel #060612, orange #FF9100, cyan #00DDFF,
// green #00FF88, red #FF4466.
//
// The screen owns no BLE state; it reads tables from BLE/BLE.h that
// are populated by the NimBLE scan callback. The Core 1 main loop
// dispatcher periodically calls mbs_refresh_*() (under lvgl_mutex)
// to update the tab views.
// =====================================================================

#include <lvgl.h>
#include <ui.h>
#include <Arduino.h>

#include "BLE/BLE.h"
#include "Display/Event.h"

// External state-machine variable (lives in main.cpp / Event.h)
extern uint8_t currentState;

// ---------------- screen handles ----------------
static lv_obj_t *ui_scrMarBLE          = NULL;
static lv_obj_t *mbs_tabview           = NULL;

// AirTag tab
static lv_obj_t *mbs_airtag_status     = NULL;
static lv_obj_t *mbs_airtag_list       = NULL;   // textarea
static lv_obj_t *mbs_airtag_btnScan    = NULL;
static lv_obj_t *mbs_airtag_btnStop    = NULL;
static lv_obj_t *mbs_airtag_btnSpoof   = NULL;
static lv_obj_t *mbs_airtag_chart      = NULL;
static lv_chart_series_t *mbs_airtag_series = NULL;
static lv_obj_t *mbs_airtag_lblDist    = NULL;
static lv_obj_t *mbs_airtag_ddlTarget  = NULL;
static lv_obj_t *mbs_airtag_btnMonitor = NULL;

// Skimmer tab
static lv_obj_t *mbs_skim_status       = NULL;
static lv_obj_t *mbs_skim_list         = NULL;
static lv_obj_t *mbs_skim_btnScan      = NULL;
static lv_obj_t *mbs_skim_btnStop      = NULL;

// Flock tab
static lv_obj_t *mbs_flock_status      = NULL;
static lv_obj_t *mbs_flock_list        = NULL;
static lv_obj_t *mbs_flock_btnScan     = NULL;
static lv_obj_t *mbs_flock_btnStop     = NULL;

// Meta tab
static lv_obj_t *mbs_meta_status       = NULL;
static lv_obj_t *mbs_meta_list         = NULL;
static lv_obj_t *mbs_meta_btnScan      = NULL;
static lv_obj_t *mbs_meta_btnStop      = NULL;

// Analyzer tab
static lv_obj_t *mbs_an_status         = NULL;
static lv_obj_t *mbs_an_lblStats       = NULL;
static lv_obj_t *mbs_an_lblBuckets     = NULL;
static lv_obj_t *mbs_an_lblVendors     = NULL;
static lv_obj_t *mbs_an_btnScan        = NULL;
static lv_obj_t *mbs_an_btnStop        = NULL;

// Spam+ tab (Sour Apple, SwiftPair, SpamPlus)
static lv_obj_t *mbs_sp_status         = NULL;
static lv_obj_t *mbs_sp_lblCount       = NULL;
static lv_obj_t *mbs_sp_ddlMode        = NULL;
static lv_obj_t *mbs_sp_btnStart       = NULL;
static lv_obj_t *mbs_sp_btnStop        = NULL;
static lv_obj_t *mbs_sp_log            = NULL;

static lv_obj_t *mbs_btnBack           = NULL;

// External counter exposed for spam tab. Driven by main.cpp dispatcher.
extern int bleMarSpamCount;
extern int bleMarSpamRotState;

// ---------------- forward decls ----------------
static void mbs_event_back(lv_event_t *e);
static void mbs_event_airtag_scan(lv_event_t *e);
static void mbs_event_airtag_stop(lv_event_t *e);
static void mbs_event_airtag_spoof(lv_event_t *e);
static void mbs_event_airtag_monitor(lv_event_t *e);
static void mbs_event_skim_scan(lv_event_t *e);
static void mbs_event_skim_stop(lv_event_t *e);
static void mbs_event_flock_scan(lv_event_t *e);
static void mbs_event_flock_stop(lv_event_t *e);
static void mbs_event_meta_scan(lv_event_t *e);
static void mbs_event_meta_stop(lv_event_t *e);
static void mbs_event_an_scan(lv_event_t *e);
static void mbs_event_an_stop(lv_event_t *e);
static void mbs_event_sp_start(lv_event_t *e);
static void mbs_event_sp_stop(lv_event_t *e);

// ---------------- style helpers ----------------
static lv_obj_t *mbs_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                              const char *text, uint32_t bg) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_align(btn, LV_ALIGN_TOP_LEFT);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    return btn;
}

static lv_obj_t *mbs_make_lbl(lv_obj_t *parent, int x, int y, const char *text,
                              uint32_t color, const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_align(lbl, LV_ALIGN_TOP_LEFT);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    return lbl;
}

static lv_obj_t *mbs_make_textarea(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_align(ta, LV_ALIGN_TOP_LEFT);
    lv_textarea_set_one_line(ta, false);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x060612), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, 255, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x222244), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, LV_PART_MAIN);
    return ta;
}

// ---------------- screen build ----------------
static void mbs_screen_init(void) {
    if (ui_scrMarBLE) return;

    ui_scrMarBLE = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrMarBLE, lv_color_hex(0x080810), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrMarBLE, 255, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrMarBLE, LV_OBJ_FLAG_SCROLLABLE);

    // Title bar — shifted below the status bar zone (lv_layer_top
    // owns y<22 for the WiFi/battery icons).
    lv_obj_t *title = mbs_make_lbl(ui_scrMarBLE, 0, 26, "MANTIS BLE",
                                   0xFF9100, &lv_font_montserrat_18);
    lv_obj_set_width(title, 320);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Tabview (bottom tabs, tab btn height 28)
    mbs_tabview = lv_tabview_create(ui_scrMarBLE, LV_DIR_BOTTOM, 28);
    lv_obj_set_size(mbs_tabview, 320, 430);
    lv_obj_set_pos(mbs_tabview, 0, 50);
    lv_obj_set_style_bg_color(mbs_tabview, lv_color_hex(0x080810), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(mbs_tabview),
                                lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(mbs_tabview),
                               &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(mbs_tabview),
                              lv_color_hex(0x080810), LV_PART_MAIN);

    // -----------------------------------------------------------------
    // Tab 1: AirTag (Sniff + Monitor + Spoof)
    // -----------------------------------------------------------------
    lv_obj_t *tabAT = lv_tabview_add_tab(mbs_tabview, "AirTag");
    lv_obj_clear_flag(tabAT, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabAT, 0, LV_PART_MAIN);

    mbs_airtag_status = mbs_make_lbl(tabAT, 6, 2, "Ready",
                                     0x00FF88, &lv_font_montserrat_14);

    mbs_airtag_list = mbs_make_textarea(tabAT, 5, 22, 305, 110);
    lv_textarea_set_placeholder_text(mbs_airtag_list, "AirTag sniff results...");

    // Row of buttons
    mbs_airtag_btnScan  = mbs_make_btn(tabAT,   5, 140, 90, 30, "SNIFF",   0x006633);
    mbs_airtag_btnStop  = mbs_make_btn(tabAT, 100, 140, 70, 30, "STOP",    0x661111);
    mbs_airtag_btnSpoof = mbs_make_btn(tabAT, 175, 140, 90, 30, "SPOOF",   0xAA5500);
    lv_obj_add_event_cb(mbs_airtag_btnScan,  mbs_event_airtag_scan,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_airtag_btnStop,  mbs_event_airtag_stop,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_airtag_btnSpoof, mbs_event_airtag_spoof, LV_EVENT_CLICKED, NULL);

    // Monitor section
    mbs_make_lbl(tabAT, 6, 178, "Monitor:", 0xFFFFFF, &lv_font_montserrat_12);
    mbs_airtag_ddlTarget = lv_dropdown_create(tabAT);
    lv_dropdown_set_symbol(mbs_airtag_ddlTarget, NULL);
    lv_dropdown_set_options(mbs_airtag_ddlTarget, "(none)");
    lv_obj_set_size(mbs_airtag_ddlTarget, 180, 28);
    lv_obj_set_pos(mbs_airtag_ddlTarget, 70, 175);
    lv_obj_set_style_bg_color(mbs_airtag_ddlTarget, lv_color_hex(0x060612), LV_PART_MAIN);
    lv_obj_set_style_text_color(mbs_airtag_ddlTarget, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(mbs_airtag_ddlTarget, &lv_font_montserrat_10, LV_PART_MAIN);

    mbs_airtag_btnMonitor = mbs_make_btn(tabAT, 255, 175, 55, 28, "WATCH", 0x0088AA);
    lv_obj_add_event_cb(mbs_airtag_btnMonitor, mbs_event_airtag_monitor, LV_EVENT_CLICKED, NULL);

    // Chart for RSSI over time
    mbs_airtag_chart = lv_chart_create(tabAT);
    lv_obj_set_size(mbs_airtag_chart, 305, 110);
    lv_obj_set_pos(mbs_airtag_chart, 5, 210);
    lv_chart_set_type(mbs_airtag_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(mbs_airtag_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -30);
    lv_chart_set_point_count(mbs_airtag_chart, BLE_MONITOR_SAMPLES);
    lv_chart_set_div_line_count(mbs_airtag_chart, 4, 6);
    lv_obj_set_style_bg_color(mbs_airtag_chart, lv_color_hex(0x060612), LV_PART_MAIN);
    lv_obj_set_style_size(mbs_airtag_chart, 0, LV_PART_INDICATOR);  // no point markers
    mbs_airtag_series = lv_chart_add_series(mbs_airtag_chart,
                                            lv_color_hex(0x00DDFF),
                                            LV_CHART_AXIS_PRIMARY_Y);

    mbs_airtag_lblDist = mbs_make_lbl(tabAT, 6, 325, "Dist: --  RSSI: --",
                                       0x00DDFF, &lv_font_montserrat_12);

    // -----------------------------------------------------------------
    // Tab 2: Skimmer
    // -----------------------------------------------------------------
    lv_obj_t *tabSk = lv_tabview_add_tab(mbs_tabview, "Skim");
    lv_obj_clear_flag(tabSk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabSk, 0, LV_PART_MAIN);

    mbs_skim_status = mbs_make_lbl(tabSk, 6, 2, "Ready",
                                   0x00FF88, &lv_font_montserrat_14);
    mbs_skim_list = mbs_make_textarea(tabSk, 5, 22, 305, 280);
    lv_textarea_set_placeholder_text(mbs_skim_list,
        "Scans for HC-05/06, JDY-31/33, DL16, HM-10, BT05...");

    mbs_skim_btnScan = mbs_make_btn(tabSk,  5, 310, 145, 32, "SCAN",  0x006633);
    mbs_skim_btnStop = mbs_make_btn(tabSk, 165, 310, 145, 32, "STOP",  0x661111);
    lv_obj_add_event_cb(mbs_skim_btnScan, mbs_event_skim_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_skim_btnStop, mbs_event_skim_stop, LV_EVENT_CLICKED, NULL);

    // -----------------------------------------------------------------
    // Tab 3: Flock
    // -----------------------------------------------------------------
    lv_obj_t *tabFl = lv_tabview_add_tab(mbs_tabview, "Flock");
    lv_obj_clear_flag(tabFl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabFl, 0, LV_PART_MAIN);

    mbs_flock_status = mbs_make_lbl(tabFl, 6, 2, "Ready",
                                    0x00FF88, &lv_font_montserrat_14);
    mbs_flock_list = mbs_make_textarea(tabFl, 5, 22, 305, 280);
    lv_textarea_set_placeholder_text(mbs_flock_list,
        "Mantis Flock packets (0xFFFF + FLOCK magic)");

    mbs_flock_btnScan = mbs_make_btn(tabFl,  5, 310, 145, 32, "SCAN", 0x006633);
    mbs_flock_btnStop = mbs_make_btn(tabFl, 165, 310, 145, 32, "STOP", 0x661111);
    lv_obj_add_event_cb(mbs_flock_btnScan, mbs_event_flock_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_flock_btnStop, mbs_event_flock_stop, LV_EVENT_CLICKED, NULL);

    // -----------------------------------------------------------------
    // Tab 4: Meta
    // -----------------------------------------------------------------
    lv_obj_t *tabMe = lv_tabview_add_tab(mbs_tabview, "Meta");
    lv_obj_clear_flag(tabMe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabMe, 0, LV_PART_MAIN);

    mbs_meta_status = mbs_make_lbl(tabMe, 6, 2, "Ready",
                                   0x00FF88, &lv_font_montserrat_14);
    mbs_meta_list = mbs_make_textarea(tabMe, 5, 22, 305, 280);
    lv_textarea_set_placeholder_text(mbs_meta_list,
        "Meta/Facebook BLE (UUID 0xFEF4, mfg 0x0131)");

    mbs_meta_btnScan = mbs_make_btn(tabMe,  5, 310, 145, 32, "SCAN", 0x006633);
    mbs_meta_btnStop = mbs_make_btn(tabMe, 165, 310, 145, 32, "STOP", 0x661111);
    lv_obj_add_event_cb(mbs_meta_btnScan, mbs_event_meta_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_meta_btnStop, mbs_event_meta_stop, LV_EVENT_CLICKED, NULL);

    // -----------------------------------------------------------------
    // Tab 5: Analyzer
    // -----------------------------------------------------------------
    lv_obj_t *tabAn = lv_tabview_add_tab(mbs_tabview, "Analyz");
    lv_obj_clear_flag(tabAn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabAn, 0, LV_PART_MAIN);

    mbs_an_status = mbs_make_lbl(tabAn, 6, 2, "Ready",
                                 0x00FF88, &lv_font_montserrat_14);
    mbs_an_lblStats   = mbs_make_lbl(tabAn, 6,  30, "Total: 0  Unique: 0",
                                     0xFFFFFF, &lv_font_montserrat_14);
    mbs_an_lblBuckets = mbs_make_lbl(tabAn, 6,  60,
                                     "RSSI buckets:\n--",
                                     0x00DDFF, &lv_font_montserrat_12);
    mbs_an_lblVendors = mbs_make_lbl(tabAn, 6, 170,
                                     "Vendors:\n--",
                                     0xFF9100, &lv_font_montserrat_12);

    mbs_an_btnScan = mbs_make_btn(tabAn,  5, 310, 145, 32, "SCAN", 0x006633);
    mbs_an_btnStop = mbs_make_btn(tabAn, 165, 310, 145, 32, "STOP", 0x661111);
    lv_obj_add_event_cb(mbs_an_btnScan, mbs_event_an_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_an_btnStop, mbs_event_an_stop, LV_EVENT_CLICKED, NULL);

    // -----------------------------------------------------------------
    // Tab 6: Spam+ (Sour Apple, SwiftPair, Spam Plus)
    // -----------------------------------------------------------------
    lv_obj_t *tabSp = lv_tabview_add_tab(mbs_tabview, "Spam+");
    lv_obj_clear_flag(tabSp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tabSp, 0, LV_PART_MAIN);

    mbs_sp_status = mbs_make_lbl(tabSp, 6, 2, "Ready",
                                 0x00FF88, &lv_font_montserrat_14);

    mbs_make_lbl(tabSp, 6, 28, "Mode:", 0xFFFFFF, &lv_font_montserrat_12);
    mbs_sp_ddlMode = lv_dropdown_create(tabSp);
    lv_dropdown_set_symbol(mbs_sp_ddlMode, NULL);
    lv_dropdown_set_options(mbs_sp_ddlMode,
        "Sour Apple (iOS crash)\nSwiftPair (Windows)\nSpam+ (Samsung/Flipper/Watch)");
    lv_obj_set_size(mbs_sp_ddlMode, 250, 28);
    lv_obj_set_pos(mbs_sp_ddlMode, 55, 24);
    lv_obj_set_style_bg_color(mbs_sp_ddlMode, lv_color_hex(0x060612), LV_PART_MAIN);
    lv_obj_set_style_text_color(mbs_sp_ddlMode, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(mbs_sp_ddlMode, &lv_font_montserrat_12, LV_PART_MAIN);

    mbs_sp_lblCount = mbs_make_lbl(tabSp, 6, 60, "Packets: 0",
                                   0x00DDFF, &lv_font_montserrat_14);

    mbs_sp_log = mbs_make_textarea(tabSp, 5, 90, 305, 215);
    lv_textarea_set_placeholder_text(mbs_sp_log, "Broadcast log...");

    mbs_sp_btnStart = mbs_make_btn(tabSp,  5, 310, 145, 32, "START", 0x006633);
    mbs_sp_btnStop  = mbs_make_btn(tabSp, 165, 310, 145, 32, "STOP",  0x661111);
    lv_obj_add_event_cb(mbs_sp_btnStart, mbs_event_sp_start, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mbs_sp_btnStop,  mbs_event_sp_stop,  LV_EVENT_CLICKED, NULL);

    // -----------------------------------------------------------------
    // Back button (overlay on screen)
    // -----------------------------------------------------------------
    mbs_btnBack = mbs_make_btn(ui_scrMarBLE, 4, 24, 56, 22, "BACK", 0x333355);
    lv_obj_add_event_cb(mbs_btnBack, mbs_event_back, LV_EVENT_CLICKED, NULL);
}

// ---------------- refresh helpers — called by main.cpp under lvgl_mutex ----------------
static inline void mbs_refresh_airtag(void) {
    if (!mbs_airtag_list) return;
    if (!bleMarLock(20)) return;

    char buf[2048]; buf[0] = 0; size_t off = 0;
    char ddl[1024]; ddl[0] = 0; size_t doff = 0;
    if (bleAirtagCount == 0) {
        strncpy(buf, "(none seen yet)", sizeof(buf));
    }
    for (int i = 0; i < bleAirtagCount && off + 80 < sizeof(buf); i++) {
        const BleAirTagEntry &e = bleAirtags[i];
        int n = snprintf(buf + off, sizeof(buf) - off,
                         "%s %s\n  RSSI:%d  hits:%u  tx:%d  age:%us\n",
                         e.isLost ? "[LOST]" : "[NEAR]",
                         e.addr, e.rssi, (unsigned)e.hits, e.txPower,
                         (unsigned)((millis() - e.firstSeenMs)/1000));
        if (n > 0) off += n;
        // Build dropdown options "addr\n..." for monitor selection
        if (doff + 20 < sizeof(ddl)) {
            int dn = snprintf(ddl + doff, sizeof(ddl) - doff,
                              "%s%s", (doff == 0) ? "" : "\n", e.addr);
            if (dn > 0) doff += dn;
        }
    }
    lv_textarea_set_text(mbs_airtag_list, buf);

    if (mbs_airtag_ddlTarget) {
        if (bleAirtagCount == 0) lv_dropdown_set_options(mbs_airtag_ddlTarget, "(none)");
        else                     lv_dropdown_set_options(mbs_airtag_ddlTarget, ddl);
    }

    char lbl[64];
    int n = bleMonitorCount;
    if (n > 0 && mbs_airtag_chart && mbs_airtag_series) {
        // Push samples in chronological order
        int start = (bleMonitorIdx - n);
        if (start < 0) start += BLE_MONITOR_SAMPLES;
        for (int i = 0; i < BLE_MONITOR_SAMPLES; i++) {
            int sIdx = (start + i) % BLE_MONITOR_SAMPLES;
            int8_t v = (i < n) ? bleMonitorRssi[sIdx] : (int8_t)-100;
            lv_chart_set_value_by_id(mbs_airtag_chart, mbs_airtag_series, i, v);
        }
        lv_chart_refresh(mbs_airtag_chart);

        if (bleMonitorDistance > 0)
            snprintf(lbl, sizeof(lbl), "Dist: %.1fm  RSSI: %d  TxP: %d",
                     bleMonitorDistance, bleMonitorLastRssi, bleMonitorTxPower);
        else
            snprintf(lbl, sizeof(lbl), "Dist: -- RSSI: %d", bleMonitorLastRssi);
    } else {
        snprintf(lbl, sizeof(lbl), "Dist: --  RSSI: --");
    }
    if (mbs_airtag_lblDist) lv_label_set_text(mbs_airtag_lblDist, lbl);

    bleMarUnlock();
}

static inline void mbs_refresh_skim(void) {
    if (!mbs_skim_list) return;
    if (!bleMarLock(20)) return;
    char buf[1024]; buf[0] = 0; size_t off = 0;
    if (bleSkimmerCount == 0) {
        strncpy(buf, "(none seen — looking for HC-05/06, JDY-3x, DL16...)", sizeof(buf));
    }
    for (int i = 0; i < bleSkimmerCount && off + 80 < sizeof(buf); i++) {
        const BleSkimmerEntry &e = bleSkimmers[i];
        int n = snprintf(buf + off, sizeof(buf) - off,
                         "[!] %s\n   %s  RSSI:%d  hits:%u\n",
                         e.name, e.addr, e.rssi, (unsigned)e.hits);
        if (n > 0) off += n;
    }
    lv_textarea_set_text(mbs_skim_list, buf);
    bleMarUnlock();
}

static inline void mbs_refresh_flock(void) {
    if (!mbs_flock_list) return;
    if (!bleMarLock(20)) return;
    char buf[1024]; buf[0] = 0; size_t off = 0;
    if (bleFlockCount == 0) {
        strncpy(buf, "(no Flock packets seen)", sizeof(buf));
    }
    for (int i = 0; i < bleFlockCount && off + 80 < sizeof(buf); i++) {
        const BleFlockEntry &e = bleFlocks[i];
        int n = snprintf(buf + off, sizeof(buf) - off,
                         "%s  RSSI:%d\n  %s\n",
                         e.addr, e.rssi, e.message);
        if (n > 0) off += n;
    }
    lv_textarea_set_text(mbs_flock_list, buf);
    bleMarUnlock();
}

static inline void mbs_refresh_meta(void) {
    if (!mbs_meta_list) return;
    if (!bleMarLock(20)) return;
    char buf[1024]; buf[0] = 0; size_t off = 0;
    if (bleMetaCount == 0) {
        strncpy(buf, "(no Meta/FB devices seen)", sizeof(buf));
    }
    for (int i = 0; i < bleMetaCount && off + 80 < sizeof(buf); i++) {
        const BleMetaEntry &e = bleMetas[i];
        const char *why = (e.reason==1)?"uuid":(e.reason==2)?"mfg":"name";
        int n = snprintf(buf + off, sizeof(buf) - off,
                         "%s [%s]\n  %s  RSSI:%d\n",
                         e.name, why, e.addr, e.rssi);
        if (n > 0) off += n;
    }
    lv_textarea_set_text(mbs_meta_list, buf);
    bleMarUnlock();
}

static inline void mbs_refresh_analyzer(void) {
    if (!mbs_an_lblStats) return;
    if (!bleMarLock(20)) return;
    char s1[64], s2[200], s3[200];
    snprintf(s1, sizeof(s1), "Total ads: %u  Unique: %u",
             (unsigned)bleAnalyzer.totalAds, (unsigned)bleAnalyzer.uniqueDevices);
    snprintf(s2, sizeof(s2),
             "RSSI buckets:\n <-90: %u    -90..-75: %u\n -75..-60: %u   -60..-45: %u\n  >=-45: %u\n"
             "Conn: %u   NoConn: %u\n WithName: %u   WithMfg: %u",
             (unsigned)bleAnalyzer.rssiBuckets[0],
             (unsigned)bleAnalyzer.rssiBuckets[1],
             (unsigned)bleAnalyzer.rssiBuckets[2],
             (unsigned)bleAnalyzer.rssiBuckets[3],
             (unsigned)bleAnalyzer.rssiBuckets[4],
             (unsigned)bleAnalyzer.connectable,
             (unsigned)bleAnalyzer.nonConnectable,
             (unsigned)bleAnalyzer.withName,
             (unsigned)bleAnalyzer.hasMfg);
    snprintf(s3, sizeof(s3),
             "Vendors seen:\n Apple: %u   MS: %u\n Google: %u   Samsung: %u\n Meta: %u",
             (unsigned)bleAnalyzer.apple,
             (unsigned)bleAnalyzer.microsoft,
             (unsigned)bleAnalyzer.google,
             (unsigned)bleAnalyzer.samsung,
             (unsigned)bleAnalyzer.meta);
    lv_label_set_text(mbs_an_lblStats, s1);
    lv_label_set_text(mbs_an_lblBuckets, s2);
    lv_label_set_text(mbs_an_lblVendors, s3);
    bleMarUnlock();
}

static inline void mbs_refresh_spam(void) {
    if (!mbs_sp_lblCount) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Packets: %d", bleMarSpamCount);
    lv_label_set_text(mbs_sp_lblCount, buf);
}

// ---------------- event callbacks (run on Core 0 inside lvgl_mutex) ----------------
// We only set state + light label updates here. Heavy NimBLE work
// happens on Core 1 in main.cpp dispatcher.

static void mbs_event_back(lv_event_t *e) {
    BleMarStopScan();
    BLEstop();
    BLEdeinit();
    currentState = STATE_IDLE;
    lv_scr_load(ui_scrBLEApps);
}

static void mbs_event_airtag_scan(lv_event_t *e) {
    if (currentState != STATE_IDLE && currentState != STATE_BLE_MAR_INIT) {
        // already running something — stop first
        BleMarStopScan();
    }
    bleMarClearAll();
    lv_label_set_text(mbs_airtag_status, "Sniffing...");
    lv_obj_set_style_text_color(mbs_airtag_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    // The dispatcher reads bleMarauderMode after init — set the intent
    bleMarauderMode = BLE_MAR_AIRTAG_SNIFF;
}

static void mbs_event_airtag_stop(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_airtag_status, "Stopped");
    lv_obj_set_style_text_color(mbs_airtag_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

static void mbs_event_airtag_spoof(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_airtag_status, "Spoofing AirTag");
    lv_obj_set_style_text_color(mbs_airtag_status, lv_color_hex(0xFF9100), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_AIRTAG_SPOOF;
}

static void mbs_event_airtag_monitor(lv_event_t *e) {
    // Capture selected address from dropdown
    char selBuf[24] = {0};
    lv_dropdown_get_selected_str(mbs_airtag_ddlTarget, selBuf, sizeof(selBuf));
    if (selBuf[0] == 0 || selBuf[0] == '(') {
        lv_label_set_text(mbs_airtag_status, "No target");
        return;
    }
    strncpy(bleMonitorTargetAddr, selBuf, sizeof(bleMonitorTargetAddr) - 1);
    bleMonitorTargetAddr[sizeof(bleMonitorTargetAddr) - 1] = 0;
    bleMonitorCount = 0;
    bleMonitorIdx = 0;

    BleMarStopScan();
    char st[40];
    snprintf(st, sizeof(st), "Monitoring %s", bleMonitorTargetAddr);
    lv_label_set_text(mbs_airtag_status, st);
    lv_obj_set_style_text_color(mbs_airtag_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_AIRTAG_MONITOR;
}

static void mbs_event_skim_scan(lv_event_t *e) {
    BleMarStopScan();
    bleMarClearAll();
    lv_label_set_text(mbs_skim_status, "Scanning...");
    lv_obj_set_style_text_color(mbs_skim_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_SKIMMER;
}
static void mbs_event_skim_stop(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_skim_status, "Stopped");
    lv_obj_set_style_text_color(mbs_skim_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

static void mbs_event_flock_scan(lv_event_t *e) {
    BleMarStopScan();
    bleMarClearAll();
    lv_label_set_text(mbs_flock_status, "Sniffing...");
    lv_obj_set_style_text_color(mbs_flock_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_FLOCK;
}
static void mbs_event_flock_stop(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_flock_status, "Stopped");
    lv_obj_set_style_text_color(mbs_flock_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

static void mbs_event_meta_scan(lv_event_t *e) {
    BleMarStopScan();
    bleMarClearAll();
    lv_label_set_text(mbs_meta_status, "Scanning...");
    lv_obj_set_style_text_color(mbs_meta_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_META;
}
static void mbs_event_meta_stop(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_meta_status, "Stopped");
    lv_obj_set_style_text_color(mbs_meta_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

static void mbs_event_an_scan(lv_event_t *e) {
    BleMarStopScan();
    bleMarClearAll();
    lv_label_set_text(mbs_an_status, "Analyzing...");
    lv_obj_set_style_text_color(mbs_an_status, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
    bleMarauderMode = BLE_MAR_ANALYZER;
}
static void mbs_event_an_stop(lv_event_t *e) {
    BleMarStopScan();
    lv_label_set_text(mbs_an_status, "Stopped");
    lv_obj_set_style_text_color(mbs_an_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

static void mbs_event_sp_start(lv_event_t *e) {
    BleMarStopScan();
    bleMarSpamCount = 0;
    bleMarSpamRotState = 0;
    lv_label_set_text(mbs_sp_lblCount, "Packets: 0");
    lv_textarea_set_text(mbs_sp_log, "");
    uint16_t sel = lv_dropdown_get_selected(mbs_sp_ddlMode);
    uint8_t mode = BLE_MAR_SOUR_APPLE;
    if (sel == 1) mode = BLE_MAR_SWIFTPAIR;
    if (sel == 2) mode = BLE_MAR_SPAM_PLUS;
    bleMarauderMode = mode;
    lv_label_set_text(mbs_sp_status, "Broadcasting...");
    lv_obj_set_style_text_color(mbs_sp_status, lv_color_hex(0xFF9100), LV_PART_MAIN);
    currentState = STATE_BLE_MAR_INIT;
}
static void mbs_event_sp_stop(lv_event_t *e) {
    BleMarStopScan();
    BLEstop();
    lv_label_set_text(mbs_sp_status, "Stopped");
    lv_obj_set_style_text_color(mbs_sp_status, lv_color_hex(0xFF4466), LV_PART_MAIN);
    currentState = STATE_IDLE;
}

// Externally callable launch — called from existing BLE menu screen
static inline void mbs_open(void) {
    mbs_screen_init();
    bleMarClearAll();
    lv_scr_load(ui_scrMarBLE);
}

#endif
