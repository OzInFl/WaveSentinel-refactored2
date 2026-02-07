#ifndef StatusBar_h
#define StatusBar_h

// ---------------------------------------------------------------
// StatusBar.h — Persistent WiFi + Battery icons on lv_layer_top()
//
// lv_layer_top() renders above ALL screens and survives screen
// transitions. Click-through so touches pass to underlying screens.
//
// WiFi icon: gray (disconnected) / blue (connected)
// Battery icon: static placeholder (ADC pin conflict with CC1101)
// ---------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>

// Widget handles
static lv_obj_t *sb_wifiIcon = NULL;
static lv_obj_t *sb_batteryIcon = NULL;
static lv_obj_t *sb_batteryPct = NULL;

// WiFi state for icon (synced from wifiGotIP volatile in WiFix.h)
static bool sb_wifiIsConnected = false;

// ---------------------------------------------------------------
// statusbar_init() — Create status bar widgets on lv_layer_top()
// Call once in setup() after ui_init() / Init_Display()
// Must be called before xTaskCreatePinnedToCore (Core 0 not yet running)
// ---------------------------------------------------------------
static void statusbar_init()
{
    lv_obj_t *layer = lv_layer_top();

    // Make the layer click-through so touches pass to underlying screens
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi icon (rightmost)
    sb_wifiIcon = lv_label_create(layer);
    lv_obj_set_align(sb_wifiIcon, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(sb_wifiIcon, -5);
    lv_obj_set_y(sb_wifiIcon, 3);
    lv_label_set_text(sb_wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(sb_wifiIcon, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(sb_wifiIcon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_opa(sb_wifiIcon, 200, LV_PART_MAIN);
    lv_obj_clear_flag(sb_wifiIcon, LV_OBJ_FLAG_CLICKABLE);

    // Battery icon (left of WiFi)
    sb_batteryIcon = lv_label_create(layer);
    lv_obj_set_align(sb_batteryIcon, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(sb_batteryIcon, -28);
    lv_obj_set_y(sb_batteryIcon, 3);
    lv_label_set_text(sb_batteryIcon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(sb_batteryIcon, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(sb_batteryIcon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_opa(sb_batteryIcon, 200, LV_PART_MAIN);
    lv_obj_clear_flag(sb_batteryIcon, LV_OBJ_FLAG_CLICKABLE);

    // Battery percentage label (left of battery icon) — placeholder
    sb_batteryPct = lv_label_create(layer);
    lv_obj_set_align(sb_batteryPct, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(sb_batteryPct, -50);
    lv_obj_set_y(sb_batteryPct, 5);
    lv_label_set_text(sb_batteryPct, "--%");
    lv_obj_set_style_text_color(sb_batteryPct, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(sb_batteryPct, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_opa(sb_batteryPct, 200, LV_PART_MAIN);
    lv_obj_clear_flag(sb_batteryPct, LV_OBJ_FLAG_CLICKABLE);
}

// ---------------------------------------------------------------
// statusbar_update_wifi() — Set WiFi icon state
// Can be called from any context (just sets a flag)
// ---------------------------------------------------------------
static void statusbar_update_wifi(bool connected)
{
    sb_wifiIsConnected = connected;
}

// ---------------------------------------------------------------
// statusbar_update() — Periodic UI refresh
// MUST be called with lvgl_mutex held (from Core 1 loop)
// ---------------------------------------------------------------
static void statusbar_update()
{
    // Sync WiFi icon from the volatile flag in WiFix.h
    extern volatile bool wifiGotIP;
    sb_wifiIsConnected = wifiGotIP;

    if (sb_wifiIcon) {
        if (sb_wifiIsConnected) {
            lv_obj_set_style_text_color(sb_wifiIcon, lv_color_hex(0x00AAFF), LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(sb_wifiIcon, lv_color_hex(0x555555), LV_PART_MAIN);
        }
    }

    // Battery: placeholder — no ADC reading (GPIO10 conflict with CC1101 MOSI)
    // When battery pin is wired, add analogReadMilliVolts() here
}

#endif // StatusBar_h
