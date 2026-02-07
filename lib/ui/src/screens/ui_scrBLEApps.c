// WaveSentinel — BLE Applications Screen
// Redesigned with proper device selection and dark theme

#include "../ui.h"

void ui_scrBLEApps_screen_init(void)
{
    ui_scrBLEApps = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_scrBLEApps, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_img_src(ui_scrBLEApps, &ui_img_blankpgbkgnd_png, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Title
    ui_lblWifiMain1 = lv_label_create(ui_scrBLEApps);
    lv_obj_set_width(ui_lblWifiMain1, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblWifiMain1, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lblWifiMain1, 0);
    lv_obj_set_y(ui_lblWifiMain1, -223);
    lv_obj_set_align(ui_lblWifiMain1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblWifiMain1, "BLE APPLICATIONS");
    lv_obj_set_style_text_color(ui_lblWifiMain1, lv_color_hex(0xFF9100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblWifiMain1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblWifiMain1, &ui_font_Verdana18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Tab view (matches CC1101 screen: y=5, h=395)
    ui_tabWifiApps1 = lv_tabview_create(ui_scrBLEApps, LV_DIR_BOTTOM, 30);
    lv_obj_set_width(ui_tabWifiApps1, 320);
    lv_obj_set_height(ui_tabWifiApps1, 395);
    lv_obj_set_x(ui_tabWifiApps1, 0);
    lv_obj_set_y(ui_tabWifiApps1, 5);
    lv_obj_set_align(ui_tabWifiApps1, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_tabWifiApps1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_tabWifiApps1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_tabWifiApps1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_tabWifiApps1), lv_color_hex(0xFF9600),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_tabWifiApps1), 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(ui_tabWifiApps1), &ui_font_Verdana11,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_tabWifiApps1), lv_color_hex(0xFFFFFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_tabWifiApps1), 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // =========================================================
    // Tab: BLE SPAM
    // =========================================================
    ui_BLESpam = lv_tabview_add_tab(ui_tabWifiApps1, "BLE SPAM");

    // ROW 1: Status label (y=-145)
    ui_lblBLEStatus = lv_label_create(ui_BLESpam);
    lv_obj_set_width(ui_lblBLEStatus, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEStatus, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lblBLEStatus, 0);
    lv_obj_set_y(ui_lblBLEStatus, -145);
    lv_obj_set_align(ui_lblBLEStatus, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEStatus, "Ready");
    lv_obj_set_style_text_color(ui_lblBLEStatus, lv_color_hex(0xDEFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBLEStatus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBLEStatus, &ui_font_Verdana16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ROW 2: "DEVICE TYPE" label (y=-105) + dropdown (y=-70)
    lv_obj_t *lblDevType = lv_label_create(ui_BLESpam);
    lv_obj_set_width(lblDevType, LV_SIZE_CONTENT);
    lv_obj_set_height(lblDevType, LV_SIZE_CONTENT);
    lv_obj_set_x(lblDevType, 0);
    lv_obj_set_y(lblDevType, -105);
    lv_obj_set_align(lblDevType, LV_ALIGN_CENTER);
    lv_label_set_text(lblDevType, "DEVICE TYPE");
    lv_obj_set_style_text_color(lblDevType, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lblDevType, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lblDevType, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ddlWifiSSID1 = lv_dropdown_create(ui_BLESpam);
    lv_dropdown_set_options(ui_ddlWifiSSID1,
        "Airpods\nAirpods Pro\nAirpods Max\nAirpods Gen2\n"
        "Airpods Gen3\nAirpods Pro Gen2\n"
        "PowerBeats\nPowerBeats Pro\nBeats Solo Pro\n"
        "Studio Buds\nBeats Flex\nBeats X\nSolo3\n"
        "Studio3\nStudio Pro\nFit Pro\nStudio Buds+\n"
        "TV Setup\nTV Pair\nTV New User\nApple ID Setup\n"
        "Audio Sync\nHomeKit Setup\nTV Keyboard\nConnect WiFi\n"
        "Homepod Setup\nSetup Phone\nTransfer Number\n"
        "Color Balance\nRandom");
    lv_obj_set_width(ui_ddlWifiSSID1, 280);
    lv_obj_set_height(ui_ddlWifiSSID1, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_ddlWifiSSID1, 0);
    lv_obj_set_y(ui_ddlWifiSSID1, -70);
    lv_obj_set_align(ui_ddlWifiSSID1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ddlWifiSSID1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_text_color(ui_ddlWifiSSID1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_ddlWifiSSID1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_ddlWifiSSID1, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_ddlWifiSSID1, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ddlWifiSSID1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_ddlWifiSSID1, lv_color_hex(0x444466), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_ddlWifiSSID1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_ddlWifiSSID1, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ROW 3: Packet count (y=-20)
    ui_lblBLECount = lv_label_create(ui_BLESpam);
    lv_obj_set_width(ui_lblBLECount, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLECount, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lblBLECount, 0);
    lv_obj_set_y(ui_lblBLECount, -20);
    lv_obj_set_align(ui_lblBLECount, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLECount, "Packets: 0");
    lv_obj_set_style_text_color(ui_lblBLECount, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBLECount, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBLECount, &ui_font_Verdana16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ROW 4: Activity log textarea (y=35, h=80)
    ui_txtBLELog = lv_textarea_create(ui_BLESpam);
    lv_obj_set_width(ui_txtBLELog, 290);
    lv_obj_set_height(ui_txtBLELog, 80);
    lv_obj_set_x(ui_txtBLELog, 0);
    lv_obj_set_y(ui_txtBLELog, 35);
    lv_obj_set_align(ui_txtBLELog, LV_ALIGN_CENTER);
    lv_textarea_set_placeholder_text(ui_txtBLELog, "Activity log...");
    lv_obj_set_style_text_color(ui_txtBLELog, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_txtBLELog, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtBLELog, &ui_font_Verdana11, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_txtBLELog, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_txtBLELog, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_txtBLELog, lv_color_hex(0x444466), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_txtBLELog, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_txtBLELog, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_txtBLELog, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // ROW 5: START | STOP buttons (y=120)
    ui_btnBLEStart = lv_btn_create(ui_BLESpam);
    lv_obj_set_width(ui_btnBLEStart, 120);
    lv_obj_set_height(ui_btnBLEStart, 38);
    lv_obj_set_x(ui_btnBLEStart, -68);
    lv_obj_set_y(ui_btnBLEStart, 120);
    lv_obj_set_align(ui_btnBLEStart, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_btnBLEStart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_btnBLEStart, lv_color_hex(0x006600), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_btnBLEStart, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblBLEStart = lv_label_create(ui_btnBLEStart);
    lv_obj_set_width(ui_lblBLEStart, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEStart, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblBLEStart, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEStart, "START");
    lv_obj_set_style_text_font(ui_lblBLEStart, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_btnBLEStop = lv_btn_create(ui_BLESpam);
    lv_obj_set_width(ui_btnBLEStop, 120);
    lv_obj_set_height(ui_btnBLEStop, 38);
    lv_obj_set_x(ui_btnBLEStop, 68);
    lv_obj_set_y(ui_btnBLEStop, 120);
    lv_obj_set_align(ui_btnBLEStop, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_btnBLEStop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_btnBLEStop, lv_color_hex(0x8B0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_btnBLEStop, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_state(ui_btnBLEStop, LV_STATE_DISABLED);

    ui_lblBLEStop = lv_label_create(ui_btnBLEStop);
    lv_obj_set_width(ui_lblBLEStop, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEStop, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblBLEStop, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEStop, "STOP");
    lv_obj_set_style_text_font(ui_lblBLEStop, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Keep these NULL since we no longer use them (prevents crash)
    ui_swBLEEnable = NULL;
    ui_lblBLEEnable = NULL;

    // =========================================================
    // Tab: BLE SCAN
    // =========================================================
    ui_BLEScan = lv_tabview_add_tab(ui_tabWifiApps1, "BLE SCAN");

    // ROW 1: Status label (y=-145)
    ui_lblBLEScanStatus = lv_label_create(ui_BLEScan);
    lv_obj_set_width(ui_lblBLEScanStatus, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEScanStatus, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lblBLEScanStatus, 0);
    lv_obj_set_y(ui_lblBLEScanStatus, -145);
    lv_obj_set_align(ui_lblBLEScanStatus, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEScanStatus, "Ready");
    lv_obj_set_style_text_color(ui_lblBLEScanStatus, lv_color_hex(0xDEFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBLEScanStatus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBLEScanStatus, &ui_font_Verdana16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ROW 2: Device count (left) + scan duration dropdown (right) (y=-112)
    ui_lblBLEScanCount = lv_label_create(ui_BLEScan);
    lv_obj_set_width(ui_lblBLEScanCount, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEScanCount, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lblBLEScanCount, -60);
    lv_obj_set_y(ui_lblBLEScanCount, -112);
    lv_obj_set_align(ui_lblBLEScanCount, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEScanCount, "Devices: 0");
    lv_obj_set_style_text_color(ui_lblBLEScanCount, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBLEScanCount, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBLEScanCount, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ddlBLEScanDuration = lv_dropdown_create(ui_BLEScan);
    lv_dropdown_set_options(ui_ddlBLEScanDuration, "5 sec\n10 sec\n30 sec");
    lv_obj_set_width(ui_ddlBLEScanDuration, 100);
    lv_obj_set_height(ui_ddlBLEScanDuration, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_ddlBLEScanDuration, 80);
    lv_obj_set_y(ui_ddlBLEScanDuration, -112);
    lv_obj_set_align(ui_ddlBLEScanDuration, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_ddlBLEScanDuration, lv_color_hex(0xFF9100), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_ddlBLEScanDuration, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_ddlBLEScanDuration, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_ddlBLEScanDuration, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ddlBLEScanDuration, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_ddlBLEScanDuration, lv_color_hex(0x444466), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_ddlBLEScanDuration, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ROW 3: Results textarea (y=5, h=185) — device list
    ui_txtBLEScanResults = lv_textarea_create(ui_BLEScan);
    lv_obj_set_width(ui_txtBLEScanResults, 290);
    lv_obj_set_height(ui_txtBLEScanResults, 185);
    lv_obj_set_x(ui_txtBLEScanResults, 0);
    lv_obj_set_y(ui_txtBLEScanResults, 5);
    lv_obj_set_align(ui_txtBLEScanResults, LV_ALIGN_CENTER);
    lv_textarea_set_placeholder_text(ui_txtBLEScanResults, "Scan results...");
    lv_obj_set_style_text_color(ui_txtBLEScanResults, lv_color_hex(0x00FFEB), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_txtBLEScanResults, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtBLEScanResults, &ui_font_Verdana11, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_txtBLEScanResults, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_txtBLEScanResults, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_txtBLEScanResults, lv_color_hex(0x444466), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_txtBLEScanResults, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_txtBLEScanResults, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_txtBLEScanResults, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // ROW 4: SCAN | STOP buttons (y=125)
    ui_btnBLEScanStart = lv_btn_create(ui_BLEScan);
    lv_obj_set_width(ui_btnBLEScanStart, 120);
    lv_obj_set_height(ui_btnBLEScanStart, 38);
    lv_obj_set_x(ui_btnBLEScanStart, -68);
    lv_obj_set_y(ui_btnBLEScanStart, 125);
    lv_obj_set_align(ui_btnBLEScanStart, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_btnBLEScanStart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_btnBLEScanStart, lv_color_hex(0x006600), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_btnBLEScanStart, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblBLEScanStart = lv_label_create(ui_btnBLEScanStart);
    lv_obj_set_width(ui_lblBLEScanStart, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEScanStart, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblBLEScanStart, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEScanStart, "SCAN");
    lv_obj_set_style_text_font(ui_lblBLEScanStart, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_btnBLEScanStop = lv_btn_create(ui_BLEScan);
    lv_obj_set_width(ui_btnBLEScanStop, 120);
    lv_obj_set_height(ui_btnBLEScanStop, 38);
    lv_obj_set_x(ui_btnBLEScanStop, 68);
    lv_obj_set_y(ui_btnBLEScanStop, 125);
    lv_obj_set_align(ui_btnBLEScanStop, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_btnBLEScanStop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_btnBLEScanStop, lv_color_hex(0x8B0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_btnBLEScanStop, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_state(ui_btnBLEScanStop, LV_STATE_DISABLED);

    ui_lblBLEScanStop = lv_label_create(ui_btnBLEScanStop);
    lv_obj_set_width(ui_lblBLEScanStop, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblBLEScanStop, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblBLEScanStop, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBLEScanStop, "STOP");
    lv_obj_set_style_text_font(ui_lblBLEScanStop, &ui_font_Verdana14, LV_PART_MAIN | LV_STATE_DEFAULT);

    // =========================================================
    // Back button (screen level)
    // =========================================================
    ui_btnWifiBack1 = lv_btn_create(ui_scrBLEApps);
    lv_obj_set_width(ui_btnWifiBack1, 90);
    lv_obj_set_height(ui_btnWifiBack1, 30);
    lv_obj_set_x(ui_btnWifiBack1, -115);
    lv_obj_set_y(ui_btnWifiBack1, 224);
    lv_obj_set_align(ui_btnWifiBack1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_btnWifiBack1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_btnWifiBack1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_btnWifiBack1, lv_color_hex(0xFFF700), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_btnWifiBack1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblWifiBack1 = lv_label_create(ui_btnWifiBack1);
    lv_obj_set_width(ui_lblWifiBack1, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lblWifiBack1, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_lblWifiBack1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblWifiBack1, "BACK");
    lv_obj_set_style_text_color(ui_lblWifiBack1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblWifiBack1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblWifiBack1, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // =========================================================
    // Event callbacks
    // =========================================================
    lv_obj_add_event_cb(ui_ddlWifiSSID1, ui_event_ddlWifiSSID1, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_btnWifiBack1, ui_event_btnWifiBack1, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_btnBLEStart, event_ble_start, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnBLEStop, event_ble_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnBLEScanStart, event_ble_scan_start, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnBLEScanStop, event_ble_scan_stop, LV_EVENT_CLICKED, NULL);
}
