#ifndef SettingsScreen_h
#define SettingsScreen_h

// ---------------------------------------------------------------
// Hand-coded Settings screen. Replaces the SquareLine-generated
// version which still had the legacy AP-mode WiFi widgets and an
// OTA-enable button that's been obsolete since the move to HTTP
// OTA from crm.southeastdatacom.net.
//
//   - Check for Updates  (re-runs the same flow as the WaveKai
//                         Config tab — checkForUpdate + popup)
//   - Volume slider      (0..100 → tone_set_volume)
//   - Brightness slider  (0..255 → tft.setBrightness)
//   - Rotate             (event_rotate_device)
//   - About              (version + MAC + IP + uptime)
//   - Back               (back to ui_scrMain)
// ---------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>
#include <Preferences.h>
#include "Misc/Config.h"
#include "Audio/ToneService.h"
#include "WaveKai/WaveKaiClient.h"
#include "SubGhz/WorldMode.h"

extern WaveKaiClient waveKai;
extern SemaphoreHandle_t lvgl_mutex;
#define WAVEKAI_FW_VERSION_STR1(x) #x
#define WAVEKAI_FW_VERSION_STR2(major, minor, patch) \
        WAVEKAI_FW_VERSION_STR1(major) "." WAVEKAI_FW_VERSION_STR1(minor) "." WAVEKAI_FW_VERSION_STR1(patch)
#define SETTINGS_FW_VERSION_STR \
        WAVEKAI_FW_VERSION_STR2(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH)

static lv_obj_t *settings_lblAbout = NULL;
static bool      settings_built    = false;

// -------------------- Helpers --------------------
static lv_obj_t *settings_mk_btn(lv_obj_t *parent, int x, int y, int w, int h,
                                  const char *text, uint32_t bg) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &ui_font_Verdana14, LV_PART_MAIN);
    return b;
}

static lv_obj_t *settings_mk_lbl(lv_obj_t *parent, int x, int y, const char *t, uint32_t color,
                                  const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, t);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    return l;
}

// -------------------- Update About box --------------------
static void settings_refresh_about() {
    if (!settings_lblAbout) return;
    char buf[180];
    uint64_t mac = ESP.getEfuseMac();
    uint8_t  m[6] = { (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
                      (uint8_t)(mac >> 16), (uint8_t)(mac >> 8),  (uint8_t)(mac) };
    String ip = WiFi.localIP().toString();
    snprintf(buf, sizeof(buf),
             "v" SETTINGS_FW_VERSION_STR "\n"
             "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
             "IP : %s\n"
             "Up : %lus\n"
             "Region: %s",
             m[0], m[1], m[2], m[3], m[4], m[5],
             ip.c_str(),
             (unsigned long)(millis() / 1000),
             WorldMode::label());
    lv_label_set_text(settings_lblAbout, buf);
}

// -------------------- Check for Update flow --------------------
struct SettingsOTAUI {
    lv_obj_t *overlay;
    lv_obj_t *msgLbl;
    lv_obj_t *bar;
    lv_obj_t *pctLbl;
};
static SettingsOTAUI s_otaUI;

static void settings_run_check_for_update(lv_obj_t *btn, lv_obj_t *lbl) {
    lv_label_set_text(lbl, "Checking...");
    lv_refr_now(NULL);

    if (!waveKai.checkForUpdate()) {
        lv_label_set_text(lbl, "Up to date!");
        return;
    }

    // Build modal: dimmer + popup
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, 320, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 200, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 280, 260);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_radius(popup, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup, 14, LV_PART_MAIN);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    String msg = "Update available!\n\nCurrent: v" SETTINGS_FW_VERSION_STR
                 "\nLatest : " + waveKai.latestVersion +
                 "\n\nDownload and install?";
    lv_obj_t *msgLbl = lv_label_create(popup);
    lv_obj_set_pos(msgLbl, 0, 0);
    lv_obj_set_width(msgLbl, 245);
    lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msgLbl, msg.c_str());
    lv_obj_set_style_text_color(msgLbl, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_text_font(msgLbl, &ui_font_Verdana14, LV_PART_MAIN);

    lv_obj_t *bar = lv_bar_create(popup);
    lv_obj_set_pos(bar, 0, 120);
    lv_obj_set_size(bar, 245, 16);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pctLbl = lv_label_create(popup);
    lv_obj_set_pos(pctLbl, 0, 140);
    lv_label_set_text(pctLbl, "");
    lv_obj_set_style_text_color(pctLbl, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(pctLbl, &ui_font_Verdana14, LV_PART_MAIN);

    s_otaUI.overlay = overlay;
    s_otaUI.msgLbl  = msgLbl;
    s_otaUI.bar     = bar;
    s_otaUI.pctLbl  = pctLbl;

    // Install button
    lv_obj_t *btnInstall = settings_mk_btn(popup, 0, 170, 115, 36, "Install", 0x00AA66);
    lv_obj_add_event_cb(btnInstall, [](lv_event_t *e) {
        static bool running = false;
        if (running) return;
        running = true;

        lv_obj_t *self = lv_event_get_target(e);
        lv_obj_add_flag(self, LV_OBJ_FLAG_HIDDEN);
        // Hide the cancel button too
        lv_obj_t *parent = lv_obj_get_parent(self);
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
            lv_obj_t *child = lv_obj_get_child(parent, i);
            if (child != self && lv_obj_check_type(child, &lv_btn_class)) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_label_set_text(s_otaUI.msgLbl, "Downloading firmware...\nDo not power off!");
        lv_obj_clear_flag(s_otaUI.bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_otaUI.pctLbl, "Connecting...");

        waveKai.otaProgressCallback = [](int pct, size_t written, size_t total) {
            if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                lv_bar_set_value(s_otaUI.bar, pct, LV_ANIM_OFF);
                char b[40];
                snprintf(b, sizeof(b), "%d%%  %dK / %dK",
                         pct, (int)(written / 1024), (int)(total / 1024));
                lv_label_set_text(s_otaUI.pctLbl, b);
                xSemaphoreGive(lvgl_mutex);
            }
        };

        xTaskCreatePinnedToCore([](void *) {
            bool ok = waveKai.performOTA();
            waveKai.otaProgressCallback = nullptr;
            if (ok) {
                if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    lv_label_set_text(s_otaUI.msgLbl, "Update complete!\nRebooting...");
                    lv_bar_set_value(s_otaUI.bar, 100, LV_ANIM_OFF);
                    lv_label_set_text(s_otaUI.pctLbl, "100% - Rebooting...");
                    xSemaphoreGive(lvgl_mutex);
                }
                vTaskDelay(pdMS_TO_TICKS(1500));
                ESP.restart();
            } else {
                if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    String err = "Update failed:\n" + waveKai.lastError;
                    lv_label_set_text(s_otaUI.msgLbl, err.c_str());
                    lv_obj_set_style_text_color(s_otaUI.msgLbl,
                                                 lv_color_hex(0xFF4444), LV_PART_MAIN);
                    lv_label_set_text(s_otaUI.pctLbl, "Failed");
                    lv_obj_set_style_bg_color(s_otaUI.bar,
                                              lv_color_hex(0xFF4444), LV_PART_INDICATOR);
                    xSemaphoreGive(lvgl_mutex);
                }
            }
            vTaskDelete(NULL);
        }, "ota_task", 8192, NULL, 1, NULL, 1);
    }, LV_EVENT_CLICKED, NULL);

    // Cancel button
    lv_obj_t *btnCancel = settings_mk_btn(popup, 130, 170, 115, 36, "Cancel", 0x333355);
    lv_obj_add_event_cb(btnCancel, [](lv_event_t *e) {
        lv_obj_del(s_otaUI.overlay);
        s_otaUI.overlay = NULL;
    }, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(lbl, "Check for Updates");
}

// -------------------- Build the screen --------------------
static void settings_screen_build() {
    if (settings_built) return;
    if (!ui_scrSettings) return;

    // Wipe SquareLine widgets — keeps the screen object and its background image.
    lv_obj_clean(ui_scrSettings);
    ui_lblSettingsTitle    = NULL;
    ui_btnSettingsOTA      = NULL;
    ui_lblSettingsOTA      = NULL;
    ui_lblSettingsStatus   = NULL;
    ui_btnSettingsBack     = NULL;
    ui_lblSettingsBack     = NULL;
    ui_lblSettingsWifiIP   = NULL;
    ui_lblSettingsIPAddr   = NULL;
    ui_lblSettingsInfo     = NULL;
    ui_swSettingsWifiMode  = NULL;
    ui_lblSettingsWifiSSID = NULL;
    ui_lblSettingsWifiKey  = NULL;
    ui_lblSettingsWifiMode = NULL;
    ui_lblSettingsWifiModeAP  = NULL;
    ui_lblSettingsWifiModeSTA = NULL;
    ui_btnSettingsSave     = NULL;
    ui_lblSettingsSave     = NULL;
    ui_sldBrightness       = NULL;

    lv_obj_set_style_pad_all(ui_scrSettings, 0, 0);
    lv_obj_clear_flag(ui_scrSettings, LV_OBJ_FLAG_SCROLLABLE);

    // Title (clear of the status bar zone at y<22)
    settings_mk_lbl(ui_scrSettings, 0, 28, "SETTINGS", 0xFF9100, &ui_font_Verdana18);
    lv_obj_t *titleObj = lv_obj_get_child(ui_scrSettings, lv_obj_get_child_cnt(ui_scrSettings) - 1);
    lv_obj_set_width(titleObj, 320);
    lv_obj_set_style_text_align(titleObj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // ---- Check for Updates button (full width) ----
    {
        lv_obj_t *b = settings_mk_btn(ui_scrSettings, 20, 64, 280, 40,
                                       "CHECK FOR UPDATES", 0x6366F1);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target(e);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            settings_run_check_for_update(btn, lbl);
        }, LV_EVENT_CLICKED, NULL);
    }

    // ---- Volume slider ----
    settings_mk_lbl(ui_scrSettings, 20, 118, "VOLUME", 0xCCCCCC, &ui_font_Verdana14);
    {
        Preferences p; p.begin("wskcfg", true);
        uint8_t v = p.getUChar("vol", 75);
        p.end();
        tone_set_volume(v);

        lv_obj_t *sld = lv_slider_create(ui_scrSettings);
        lv_obj_set_size(sld, 200, 14);
        lv_obj_set_pos(sld, 100, 122);
        lv_slider_set_range(sld, 0, 100);
        lv_slider_set_value(sld, v, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sld, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sld, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sld, lv_color_hex(0x00FF88), LV_PART_KNOB);
        lv_obj_add_event_cb(sld, [](lv_event_t *e) {
            uint8_t v = (uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
            tone_set_volume(v);
            Preferences p; p.begin("wskcfg", false);
            p.putUChar("vol", v);
            p.end();
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ---- Brightness slider ----
    settings_mk_lbl(ui_scrSettings, 20, 152, "BRIGHTNESS", 0xCCCCCC, &ui_font_Verdana14);
    {
        Preferences p; p.begin("wskcfg", true);
        uint8_t b = p.getUChar("bri", 200);
        p.end();
        // Apply saved value on first build
        extern LGFX tft;
        tft.setBrightness(b);

        ui_sldBrightness = lv_slider_create(ui_scrSettings);
        lv_obj_set_size(ui_sldBrightness, 180, 14);
        lv_obj_set_pos(ui_sldBrightness, 120, 156);
        lv_slider_set_range(ui_sldBrightness, 30, 255);
        lv_slider_set_value(ui_sldBrightness, b, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui_sldBrightness, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_sldBrightness, lv_color_hex(0xFF9100), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_sldBrightness, lv_color_hex(0xFFCC44), LV_PART_KNOB);
        lv_obj_add_event_cb(ui_sldBrightness, [](lv_event_t *e) {
            int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
            extern LGFX tft;
            tft.setBrightness(v);
            Preferences p; p.begin("wskcfg", false);
            p.putUChar("bri", (uint8_t)v);
            p.end();
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ---- Rotate button ----
    {
        lv_obj_t *b = settings_mk_btn(ui_scrSettings, 20, 188, 130, 36,
                                       "ROTATE 180", 0x4A6A8A);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            extern void event_rotate_device(lv_event_t *e);
            event_rotate_device(e);
        }, LV_EVENT_CLICKED, NULL);
    }

    // ---- World Mode switch ----
    // Unlocks the CC1101's full 300-348/387-464/779-928 MHz bands. Default
    // US-only (315/433/915) until toggled on. Persisted via NVS in
    // WorldMode::setEnabled.
    settings_mk_lbl(ui_scrSettings, 20, 234, "WORLD MODE", 0xCCCCCC, &ui_font_Verdana14);
    {
        lv_obj_t *sw = lv_switch_create(ui_scrSettings);
        lv_obj_set_size(sw, 50, 26);
        lv_obj_set_pos(sw, 250, 230);
        if (WorldMode::enabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x00AA66), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, [](lv_event_t *e) {
            lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
            bool on = lv_obj_has_state(s, LV_STATE_CHECKED);
            WorldMode::setEnabled(on);
            settings_refresh_about();
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }
    {
        lv_obj_t *tip = settings_mk_lbl(ui_scrSettings, 20, 260,
            "Unlocks 868/915 + 300 MHz bands.\nCheck local laws before TX.",
            0x808080, &ui_font_Verdana14);
        lv_obj_set_width(tip, 280);
        lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
    }

    // ---- About box ----
    settings_mk_lbl(ui_scrSettings, 20, 300, "ABOUT", 0xFF9100, &ui_font_Verdana16);
    settings_lblAbout = lv_label_create(ui_scrSettings);
    lv_obj_set_pos(settings_lblAbout, 20, 328);
    lv_obj_set_width(settings_lblAbout, 280);
    lv_obj_set_style_text_color(settings_lblAbout, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(settings_lblAbout, &ui_font_Verdana14, LV_PART_MAIN);
    lv_label_set_long_mode(settings_lblAbout, LV_LABEL_LONG_WRAP);
    settings_refresh_about();

    // Refresh About on every screen-load event
    lv_obj_add_event_cb(ui_scrSettings, [](lv_event_t *e) {
        settings_refresh_about();
    }, LV_EVENT_SCREEN_LOADED, NULL);

    // ---- Back button ----
    {
        lv_obj_t *b = settings_mk_btn(ui_scrSettings, 110, 420, 100, 40, "BACK", 0x333355);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            lv_scr_load(ui_scrMain);
        }, LV_EVENT_CLICKED, NULL);
    }

    settings_built = true;
}

#endif
