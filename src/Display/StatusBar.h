#ifndef StatusBar_h
#define StatusBar_h

// ---------------------------------------------------------------
// StatusBar.h — Persistent WiFi + Battery icons on lv_layer_top()
//
// Icons are drawn from primitive lv_obj rectangles instead of font
// glyphs (the bundled Montserrat fonts don't ship the LV_SYMBOL_*
// bitmaps reliably). Click-through so touches reach the screens.
//
//   WiFi:    3 ascending vertical bars (signal strength style)
//   Battery: outer rounded body + tip + an inner fill bar
// ---------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>

// WiFi indicator: 3 bars
static lv_obj_t *sb_wifi_bars[3] = {NULL, NULL, NULL};
// Battery indicator: outer body + tip + fill
static lv_obj_t *sb_bat_body = NULL;
static lv_obj_t *sb_bat_tip  = NULL;
static lv_obj_t *sb_bat_fill = NULL;
// Battery percentage text (left of the battery icon)
static lv_obj_t *sb_batteryPct = NULL;

// Synced from volatile wifiGotIP in WiFix.h
static bool sb_wifiIsConnected = false;

// Build a small filled rectangle for compose-and-tint icon work
static lv_obj_t *sb_mk_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_align(o, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// ---------------------------------------------------------------
// statusbar_init() — Create status bar widgets on lv_layer_top()
// Call once in setup() after ui_init() / Init_Display()
// ---------------------------------------------------------------
static void statusbar_init()
{
    lv_obj_t *layer = lv_layer_top();
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);

    // ---------------- WiFi bars (rightmost, ascending heights) -----
    // Anchored TOP_RIGHT, x offsets are negative (toward the left edge
    // of the screen). Bars sit in a 22 px wide block ending 4 px from
    // the right edge of the screen.
    const int wifi_xs[3] = {-19, -13, -7};   // right edge of each bar
    const int wifi_hs[3] = {6, 10, 14};
    const int wifi_w = 4;
    for (int i = 0; i < 3; i++) {
        // y = top of bar = (max_height - this_height) + base_y
        int y = 3 + (14 - wifi_hs[i]);
        sb_wifi_bars[i] = sb_mk_rect(layer, wifi_xs[i], y, wifi_w, wifi_hs[i], 0x555555);
    }

    // ---------------- Battery icon (left of WiFi) ------------------
    // Body 18×10, tip 2×4 on the right side of the body, fill inside.
    const int bat_right_x = -32;   // body right edge offset (TOP_RIGHT)
    const int bat_top_y   = 5;
    const int bat_w       = 18;
    const int bat_h       = 10;
    sb_bat_body = sb_mk_rect(layer, bat_right_x,           bat_top_y,     bat_w, bat_h, 0x444444);
    // Tip is 2 px wide × 4 px tall, sticking out the right side
    sb_bat_tip  = sb_mk_rect(layer, bat_right_x + bat_w,   bat_top_y + 3, 2,    4,     0x444444);
    // Inner fill — sits inside the body with 1 px margin all around
    sb_bat_fill = sb_mk_rect(layer, bat_right_x + 1,       bat_top_y + 1, bat_w - 2, bat_h - 2, 0x00FF00);

    // ---------------- Battery percentage text ---------------------
    sb_batteryPct = lv_label_create(layer);
    lv_obj_set_align(sb_batteryPct, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(sb_batteryPct, -56);
    lv_obj_set_y(sb_batteryPct, 4);
    lv_label_set_text(sb_batteryPct, "--%");
    lv_obj_set_style_text_color(sb_batteryPct, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(sb_batteryPct, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_opa(sb_batteryPct, 220, LV_PART_MAIN);
    lv_obj_clear_flag(sb_batteryPct, LV_OBJ_FLAG_CLICKABLE);
}

// ---------------------------------------------------------------
// statusbar_update_wifi() — Set WiFi icon state (called from any context)
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
    extern volatile bool wifiGotIP;
    sb_wifiIsConnected = wifiGotIP;

    uint32_t color = sb_wifiIsConnected ? 0x00AAFF : 0x555555;
    for (int i = 0; i < 3; i++) {
        if (sb_wifi_bars[i])
            lv_obj_set_style_bg_color(sb_wifi_bars[i], lv_color_hex(color), LV_PART_MAIN);
    }

    // Battery: placeholder reading until ADC pin is freed from CC1101.
    // For now, just keep the fill at full green so the icon reads as healthy.
}

#endif // StatusBar_h
