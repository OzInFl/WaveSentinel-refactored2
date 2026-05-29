/*
 * WaveKai All-in-One Screen for WaveSentinel
 * Built with LVGL 8.3 — tabbed interface (320x480 portrait)
 * Tabs: Capture | Results | Config
 */
#ifndef WAVEKAI_SCREEN_H
#define WAVEKAI_SCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include "WaveKaiClient.h"
#include "../SubGhz/ProtocolID.h"

// UI references from SquareLine (for background image and fonts)
extern const lv_img_dsc_t ui_img_blankpgbkgnd_png;
extern const lv_font_t ui_font_Verdana18;
extern const lv_font_t ui_font_Verdana16;
extern const lv_font_t ui_font_Verdana14;

// Forward declarations — SubGhz and state machine
extern WaveKaiClient waveKai;
extern SemaphoreHandle_t lvgl_mutex;
extern uint8_t currentState;
extern int sample[];
extern int samplecount;
extern float CC1101_MHZ;

// SubGhz class (for capture/replay/save)
class SubGhz;
extern SubGhz SUBGHZ;

// State machine states we need
#define WK_STATE_IDLE     0
#define WK_STATE_CAPTURE  4
#define WK_STATE_PLAYBACK 5

// Loop capture mode flag
static bool wk_loopCaptureActive = false;
static lv_obj_t *wk_btnLoopCapture = NULL;
static int wk_loopCount = 0;

// Preset dropdown
static lv_obj_t *wk_ddPreset = NULL;

// Screen and widget pointers
static lv_obj_t *ui_scrWaveKai = NULL;
static lv_obj_t *wk_tabview = NULL;

// --- Capture tab widgets ---
static lv_obj_t *wk_txtFreq = NULL;
static lv_obj_t *wk_lblCaptureStatus = NULL;
static lv_obj_t *wk_lblProtocol = NULL;
static lv_obj_t *wk_lblSamples = NULL;
static lv_obj_t *wk_btnCapture = NULL;
static lv_obj_t *wk_btnStop = NULL;
static lv_obj_t *wk_btnReplay = NULL;
static lv_obj_t *wk_btnSaveSD = NULL;
static lv_obj_t *wk_btnSendAPI = NULL;

// --- Results tab widgets ---
static lv_obj_t *wk_lblResultTitle = NULL;
static lv_obj_t *wk_lblResultBody = NULL;
static lv_obj_t *wk_lblRawHex = NULL;
static lv_obj_t *wk_panelCodes = NULL;  // scrollable panel for generated codes

// Last crack result for code generation
static WaveKaiClient::CrackResult wk_lastCrack;
static bool wk_hasCrackResult = false;

// Generated code storage (up to 5)
#define WK_MAX_CODES 5
struct WkGenCode {
    int counter;
    String codeHex;
    String encrypted;
    String serial;
};
static WkGenCode wk_genCodes[WK_MAX_CODES];
static int wk_genCodeCount = 0;

// --- Config tab widgets ---
static lv_obj_t *wk_lblStatus = NULL;
static lv_obj_t *wk_lblServerStatus = NULL;
static lv_obj_t *wk_lblWifiStatus = NULL;
static lv_obj_t *wk_txtServerIP = NULL;
// (port field removed — server uses hostname on port 80)
static lv_obj_t *wk_lblLastResult = NULL;
static lv_obj_t *wk_cbAutoConnect = NULL;
static lv_obj_t *wk_cbAutoCrack = NULL;

// --- Account tab widgets ---
static lv_obj_t *wk_txtLoginEmail = NULL;
static lv_obj_t *wk_txtLoginPass = NULL;
static lv_obj_t *wk_lblAccountStatus = NULL;
static lv_obj_t *wk_lblTokenBalance = NULL;
static lv_obj_t *wk_lblDeviceMac = NULL;
static lv_obj_t *wk_btnLogin = NULL;
static lv_obj_t *wk_btnLogout = NULL;
static lv_obj_t *wk_btnRegDevice = NULL;
static lv_obj_t *wk_btnRefreshBal = NULL;
static lv_obj_t *wk_panelLoggedIn = NULL;
static lv_obj_t *wk_panelLoginForm = NULL;

// Shared keyboard
static lv_obj_t *wk_keyboard = NULL;
static lv_obj_t *wk_activeTextarea = NULL;

// =====================================================================
// Helper: create styled button
// =====================================================================
static lv_obj_t* wk_createButton(lv_obj_t *parent, const char *text,
                                   int x, int y, int w, int h,
                                   uint32_t color, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &ui_font_Verdana14, LV_PART_MAIN);

    return btn;
}

// =====================================================================
// Helper: create label
// =====================================================================
static lv_obj_t* wk_createLabel(lv_obj_t *parent, const char *text,
                                  int x, int y, uint32_t color,
                                  const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    return lbl;
}

// =====================================================================
// Helper: create text input with keyboard support
// =====================================================================
static lv_obj_t* wk_createTextInput(lv_obj_t *parent, const char *placeholder,
                                      int x, int y, int w, int h) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 6, LV_PART_MAIN);

    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        lv_obj_t *ta = lv_event_get_target(e);
        wk_activeTextarea = ta;
        if (wk_keyboard) {
            lv_keyboard_set_textarea(wk_keyboard, ta);
            lv_obj_clear_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        if (wk_keyboard) {
            lv_obj_add_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_DEFOCUSED, NULL);

    return ta;
}

// =====================================================================
// WiFi status refresh
// =====================================================================
static void wk_refreshWifiStatus(void) {
    if (!wk_lblWifiStatus) return;
    if (WiFi.status() == WL_CONNECTED) {
        String wifiMsg = "WiFi: " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
        lv_label_set_text(wk_lblWifiStatus, wifiMsg.c_str());
        lv_obj_set_style_text_color(wk_lblWifiStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
    } else {
        lv_label_set_text(wk_lblWifiStatus, "WiFi: Not connected");
        lv_obj_set_style_text_color(wk_lblWifiStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

// =====================================================================
// Update capture tab after signal received (call from main loop)
// =====================================================================
static void wk_update_capture_status(void) {
    if (!wk_lblSamples) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "Samples: %d  |  Freq: %.2f MHz", samplecount, CC1101_MHZ);
    lv_label_set_text(wk_lblSamples, buf);

    // Run protocol identification
    if (samplecount > 30) {
        ProtocolMatch match = identifyProtocol(sample, samplecount);
        char protoBuf[80];
        snprintf(protoBuf, sizeof(protoBuf), "Protocol: %s (%d%%)", match.name, match.confidence);
        lv_label_set_text(wk_lblProtocol, protoBuf);

        if (match.confidence > 50) {
            lv_obj_set_style_text_color(wk_lblProtocol, lv_color_hex(0x00FF88), LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(wk_lblProtocol, lv_color_hex(0xFF9100), LV_PART_MAIN);
        }

        lv_label_set_text(wk_lblCaptureStatus, "Signal Captured!");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);

        // Re-enable capture, disable stop
        lv_obj_clear_state(wk_btnCapture, LV_STATE_DISABLED);
        lv_obj_add_state(wk_btnStop, LV_STATE_DISABLED);

        // Enable action buttons
        lv_obj_clear_state(wk_btnReplay, LV_STATE_DISABLED);
        lv_obj_clear_state(wk_btnSaveSD, LV_STATE_DISABLED);
        lv_obj_clear_state(wk_btnSendAPI, LV_STATE_DISABLED);
    } else {
        // Too few samples
        lv_obj_clear_state(wk_btnCapture, LV_STATE_DISABLED);
        lv_obj_add_state(wk_btnStop, LV_STATE_DISABLED);
    }
}

// =====================================================================
// Update results tab with crack result
// =====================================================================
static void wk_update_last_result(const String &result) {
    if (wk_lblResultBody) {
        lv_label_set_text(wk_lblResultBody, result.c_str());
    }
    if (wk_lblLastResult) {
        lv_label_set_text(wk_lblLastResult, result.c_str());
    }
}

// Build a row in the generated codes list
static void wk_add_code_row(lv_obj_t *parent, int idx, const WkGenCode &code) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 275, 52);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x12121A), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Counter + code hex
    String lbl = "#" + String(code.counter) + "  " + code.codeHex.substring(0, 18);
    lv_obj_t *lblCode = lv_label_create(row);
    lv_label_set_text(lblCode, lbl.c_str());
    lv_obj_set_pos(lblCode, 2, 2);
    lv_obj_set_style_text_color(lblCode, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblCode, &ui_font_Verdana14, LV_PART_MAIN);

    // Save button
    lv_obj_t *btnSave = lv_btn_create(row);
    lv_obj_set_pos(btnSave, 160, 22);
    lv_obj_set_size(btnSave, 50, 24);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(0x00AA66), LV_PART_MAIN);
    lv_obj_set_style_radius(btnSave, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnSave, 0, LV_PART_MAIN);
    lv_obj_t *lblSave = lv_label_create(btnSave);
    lv_label_set_text(lblSave, "Save");
    lv_obj_center(lblSave);
    lv_obj_set_style_text_font(lblSave, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_event_cb(btnSave, [](lv_event_t *e) {
        int codeIdx = (int)(intptr_t)lv_event_get_user_data(e);
        if (codeIdx < 0 || codeIdx >= wk_genCodeCount) return;
        // Save as .sub file to SD
        wkLog("Saving code #" + String(wk_genCodes[codeIdx].counter) + " to SD");
        // Build a simple sub file
        String filename = "/subghz/WK_code_" + String(wk_genCodes[codeIdx].counter) + ".sub";
        // TODO: integrate with SUBGHZ.saveCaptureToSD() for proper format
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, "OK!");
    }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    // Replay button
    lv_obj_t *btnReplay = lv_btn_create(row);
    lv_obj_set_pos(btnReplay, 215, 22);
    lv_obj_set_size(btnReplay, 55, 24);
    lv_obj_set_style_bg_color(btnReplay, lv_color_hex(0x6366F1), LV_PART_MAIN);
    lv_obj_set_style_radius(btnReplay, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnReplay, 0, LV_PART_MAIN);
    lv_obj_t *lblReplay = lv_label_create(btnReplay);
    lv_label_set_text(lblReplay, "Send");
    lv_obj_center(lblReplay);
    lv_obj_set_style_text_font(lblReplay, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_event_cb(btnReplay, [](lv_event_t *e) {
        int codeIdx = (int)(intptr_t)lv_event_get_user_data(e);
        if (codeIdx < 0 || codeIdx >= wk_genCodeCount) return;
        wkLog("Replaying code #" + String(wk_genCodes[codeIdx].counter));
        // TODO: encode the code back to OOK pulses and transmit via CC1101
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, "TX!");
    }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    // Encrypted portion label
    String encLbl = "Enc: " + code.encrypted;
    lv_obj_t *lblEnc = lv_label_create(row);
    lv_label_set_text(lblEnc, encLbl.c_str());
    lv_obj_set_pos(lblEnc, 2, 26);
    lv_obj_set_style_text_color(lblEnc, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblEnc, &lv_font_montserrat_12, LV_PART_MAIN);
}

// Populate the codes panel with generated codes
static void wk_show_generated_codes() {
    if (!wk_panelCodes) return;

    // Clear existing children
    lv_obj_clean(wk_panelCodes);

    if (wk_genCodeCount == 0) {
        lv_obj_t *lbl = lv_label_create(wk_panelCodes);
        lv_label_set_text(lbl, "No codes generated yet.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x64748B), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &ui_font_Verdana14, LV_PART_MAIN);
        return;
    }

    for (int i = 0; i < wk_genCodeCount; i++) {
        wk_add_code_row(wk_panelCodes, i, wk_genCodes[i]);
    }
}

// Generate next codes after a successful crack
static void wk_auto_generate_codes(const WaveKaiClient::CrackResult &crack) {
    if (!crack.found || crack.derivedKey.length() == 0) return;

    wkLog("Generating 5 codes from counter " + String(crack.counter + 1));

    // Parse serial from hex string
    uint32_t serial = strtoul(crack.serial.c_str(), NULL, 16);

    String resp = waveKai.generateCodes(serial, crack.derivedKey,
                                         crack.counter + 1, crack.button, WK_MAX_CODES);

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp)) {
        wkLog("Generate codes JSON parse failed");
        return;
    }

    JsonArray codes = doc["codes"];
    wk_genCodeCount = 0;
    for (int i = 0; i < codes.size() && i < WK_MAX_CODES; i++) {
        JsonObject c = codes[i];
        wk_genCodes[i].counter = c["counter"] | 0;
        wk_genCodes[i].codeHex = c["code_hex"].as<String>();
        wk_genCodes[i].encrypted = c["encrypted"].as<String>();
        wk_genCodes[i].serial = c["serial"].as<String>();
        wk_genCodeCount++;
    }

    wkLog("Generated " + String(wk_genCodeCount) + " codes");
}

static void wk_show_result(const WaveKaiClient::CrackResult &crack) {
    if (!wk_lblResultBody) return;

    // Clear previous results first
    wk_genCodeCount = 0;
    lv_label_set_text(wk_lblRawHex, "Hex: --");
    lv_label_set_text(wk_lblResultTitle, "LAST RESULT");
    lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);

    // Store for later use
    wk_lastCrack = crack;
    wk_hasCrackResult = crack.success;

    bool hasKey = crack.derivedKey.length() > 0;
    bool hasCrack = crack.found && hasKey;

    // A protocol with `found=true` + no derived key is NOT automatically
    // a static code. Most rolling-code families (KeeLoq, Nice Flor-S,
    // Came Atomo, Hörmann, Somfy Keytis, BFT, FAAC SLH, etc.) are
    // identified by the server even when the per-vendor manufacturer
    // key isn't on hand. Treat the protocol name as authoritative — a
    // name match against the rolling-code denylist routes to the
    // "IDENTIFIED" branch instead of mislabeling it as static.
    auto isRollingMethod = [](const String &m) -> bool {
        // Every rolling/encrypted family the server can identify. If the
        // server's `method` string contains any of these as a substring,
        // we treat the result as IDENTIFIED-but-not-cracked rather than
        // mislabeling it as a static code. List mirrors the rolling-code
        // protocols in /opt/wavekai/backend/protocols/ (84 total decoders
        // as of v2.0.52 — non-rolling fixed-code families like Princeton,
        // EV1527, Holtek HT12, CAME 12-bit, Nice Flo, SMC5326, Ansonic,
        // Clemsa, Ido, Phox, Intertechno, Linear basic, etc. are NOT
        // included so they correctly route to the DECODED/static branch).
        static const char *kRollingNames[] = {
            // KeeLoq family
            "KeeLoq", "Keeloq", "HCS101", "HCS200", "HCS300", "HCS301",
            "HCS361", "HCS362", "HCS412",
            // Nice family (Flor-S, One, Smilo — but NOT plain Nice Flo)
            "Nice Flor", "Nice Flo-S", "Nice One", "Nice Smilo",
            "Nice MHouse", "Flor-S",
            // CAME / BFT
            "Came Atomo", "BFT Mitto",
            // Hörmann
            "Hormann", "Hörmann",
            // Somfy
            "Somfy Keytis", "Somfy Telis", "Somfy",
            // FAAC
            "FAAC SLH", "FAAC RC,XT", "FAAC XT",
            // Chamberlain / Liftmaster Security+
            "Sec+ 2", "Security+ 2", "Sec+ v2", "SecPlus v2", "Secplus v2",
            "Sec+ 1", "Security+ 1", "Sec+ v1", "SecPlus v1", "Secplus v1",
            "Chamberlain",
            // Linear Delta-3 + Megacode rolling
            "Linear Delta", "Delta-3",
            // Russian/CIS aftermarket
            "Star Line", "Star-Line", "Starline",
            "Scher-Khan", "Scher Khan", "ScherKhan",
            // EU brands
            "Marantec", "Phoenix V2", "Phoenix v2",
            "Dickert", "Alutech", "AT-4N",
            "Beninca", "Benincà",
            // Visonic sensor (Manchester rolling)
            "Visonic",
            // Honeywell rolling (sensor + safety)
            "Honeywell WDB", "Honeywell",
            // KingGates rolling
            "KingGates", "Stylo 4K",
            // Magellan sensor (rolling)
            "Magellan",
            // Power Smoke (sensor encrypted)
            "Power Smoke",
            // Doitrand
            "Doitrand",
            // Mastiff (some variants are rolling)
            "Mastiff",
            // Norik (rolling)
            "Norik",
            // Generic catch-alls for the "(detect)" fingerprint suffix
            "rolling", "Rolling"
        };
        for (const char *n : kRollingNames) {
            if (m.indexOf(n) >= 0) return true;
        }
        return false;
    };
    bool isStatic = crack.found && !hasKey && !isRollingMethod(crack.method);

    if (crack.success && hasCrack) {
        // Rolling code CRACKED — key derived, can generate next codes
        String msg = "KEY CRACKED!\n\n";
        msg += "Manufacturer: " + crack.manufacturer + "\n";
        msg += "Serial: " + crack.serial + "\n";
        msg += "Counter: " + String(crack.counter) + "\n";
        msg += "Button: " + String(crack.button) + "\n";
        msg += "Key: " + crack.derivedKey + "\n";
        msg += "Method: " + crack.method;
        lv_label_set_text(wk_lblResultBody, msg.c_str());
        lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_label_set_text(wk_lblResultTitle, "CRACKED!");
        lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0x00FF88), LV_PART_MAIN);

        // Auto-generate next 5 rolling codes
        wk_auto_generate_codes(crack);
        wk_show_generated_codes();
    } else if (crack.success && isStatic) {
        // Static code decoded — show full info
        String msg = "DECODED: " + crack.method + "\n\n";
        msg += "Code: 0x" + crack.rawHex + "\n";
        if (crack.serial.length() > 0 && crack.serial != "0") {
            msg += "Serial/Addr: 0x" + crack.serial + "\n";
        }
        if (crack.button > 0) {
            msg += "Button/Cmd: " + String(crack.button) + "\n";
        }
        msg += "Bits: " + String(crack.rawHex.length() * 4) + "\n";
        msg += "\nThis is a static (fixed) code.\nReplay the original capture to retransmit.";
        lv_label_set_text(wk_lblResultBody, msg.c_str());
        lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_label_set_text(wk_lblResultTitle, "DECODED");
        lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0x00FF88), LV_PART_MAIN);

        // Show the static code in the codes panel
        wk_genCodeCount = 1;
        wk_genCodes[0].counter = 0;
        wk_genCodes[0].codeHex = "0x" + crack.rawHex;
        wk_genCodes[0].encrypted = crack.method;
        wk_genCodes[0].serial = crack.serial;
        wk_show_generated_codes();
    } else if (crack.success && crack.method.length() > 0) {
        // Protocol identified but not decoded (rolling code without key match)
        String msg = "Protocol: " + crack.method + "\n\n";
        msg += "Raw Hex: " + crack.rawHex + "\n";
        if (crack.serial.length() > 0) msg += "Serial: 0x" + crack.serial + "\n";
        msg += "Bits: " + String(crack.rawHex.length() * 4) + "\n";
        msg += "\nRolling code identified but\nmanufacturer key not found.";
        lv_label_set_text(wk_lblResultBody, msg.c_str());
        lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0xFF9100), LV_PART_MAIN);
        lv_label_set_text(wk_lblResultTitle, "IDENTIFIED");
        lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
        wk_genCodeCount = 0;
        wk_show_generated_codes();
    } else if (crack.success) {
        String msg = "No protocol match.\n\n";
        msg += "Raw Hex: " + crack.rawHex + "\n";
        msg += "Bits: " + String(crack.rawHex.length() * 4);
        lv_label_set_text(wk_lblResultBody, msg.c_str());
        lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0x94A3B8), LV_PART_MAIN);
        lv_label_set_text(wk_lblResultTitle, "NO MATCH");
        lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0x94A3B8), LV_PART_MAIN);
        wk_genCodeCount = 0;
        wk_show_generated_codes();
    } else {
        String msg = "Error: " + crack.error;
        lv_label_set_text(wk_lblResultBody, msg.c_str());
        lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0xFF4444), LV_PART_MAIN);
        lv_label_set_text(wk_lblResultTitle, "ERROR");
        lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0xFF4444), LV_PART_MAIN);
        wk_show_generated_codes();
    }

    // Show raw hex on results tab
    if (wk_lblRawHex) {
        if (crack.rawHex.length() > 0) {
            lv_label_set_text(wk_lblRawHex, ("Hex: " + crack.rawHex).c_str());
        } else {
            lv_label_set_text(wk_lblRawHex, "Hex: --");
        }
    }

    // Auto-switch to results tab
    if (wk_tabview) {
        lv_tabview_set_act(wk_tabview, 1, LV_ANIM_ON);
    }
}

// =====================================================================
// Capture tab event handlers
// =====================================================================
static void wk_event_capture_start(lv_event_t *e) {
    // Read frequency from text input
    const char *freqStr = lv_textarea_get_text(wk_txtFreq);
    float freq = atof(freqStr);
    if (freq < 300.0 || freq > 928.0) freq = 433.92;

    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableReceiver();
    currentState = WK_STATE_CAPTURE;

    lv_label_set_text(wk_lblCaptureStatus, "Listening...");
    lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_label_set_text(wk_lblProtocol, "Protocol: waiting...");
    lv_label_set_text(wk_lblSamples, "Samples: 0");

    // Disable capture, enable stop
    lv_obj_add_state(wk_btnCapture, LV_STATE_DISABLED);
    lv_obj_clear_state(wk_btnStop, LV_STATE_DISABLED);
    lv_obj_add_state(wk_btnReplay, LV_STATE_DISABLED);
    lv_obj_add_state(wk_btnSaveSD, LV_STATE_DISABLED);
    lv_obj_add_state(wk_btnSendAPI, LV_STATE_DISABLED);
}

static void wk_event_capture_stop(lv_event_t *e) {
    SUBGHZ.disableReceiver();
    currentState = WK_STATE_IDLE;

    lv_label_set_text(wk_lblCaptureStatus, "Capture stopped");
    lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x94A3B8), LV_PART_MAIN);

    lv_obj_clear_state(wk_btnCapture, LV_STATE_DISABLED);
    lv_obj_add_state(wk_btnStop, LV_STATE_DISABLED);

    // If we got samples, enable buttons
    if (samplecount > 30) {
        wk_update_capture_status();
    }
}

static void wk_event_replay(lv_event_t *e) {
    if (samplecount < 30) return;

    const char *freqStr = lv_textarea_get_text(wk_txtFreq);
    float freq = atof(freqStr);
    if (freq < 300.0 || freq > 928.0) freq = 433.92;

    SUBGHZ.setFrequency(freq);
    currentState = WK_STATE_PLAYBACK;

    lv_label_set_text(wk_lblCaptureStatus, "Replaying...");
    lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
}

static void wk_event_save_sd(lv_event_t *e) {
    if (samplecount < 30) return;

    lv_label_set_text(wk_lblCaptureStatus, "Saving to SD...");
    lv_refr_now(NULL);

    if (SUBGHZ.saveCaptureToSD()) {
        lv_label_set_text(wk_lblCaptureStatus, "Saved to SD!");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
    } else {
        lv_label_set_text(wk_lblCaptureStatus, "SD save failed");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

static void wk_event_send_api(lv_event_t *e) {
    if (samplecount < 30) {
        lv_label_set_text(wk_lblCaptureStatus, "No signal captured");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(wk_lblCaptureStatus, "WiFi not connected!");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        return;
    }

    if (!waveKai.isAuthenticated) {
        lv_label_set_text(wk_lblCaptureStatus, "Login required! Go to ACCT tab");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        return;
    }

    lv_label_set_text_fmt(wk_lblCaptureStatus, "Sending %d samples...", samplecount);
    lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00AFFF), LV_PART_MAIN);
    lv_refr_now(NULL);

    // Debug: dump first 50 raw samples to serial
    Serial.printf("[WaveKai] Raw samples (%d total, freq=%.2f):\n", samplecount, CC1101_MHZ);
    for (int i = 0; i < min(samplecount, 80); i++) {
        Serial.printf("%d ", sample[i]);
        if ((i + 1) % 20 == 0) Serial.println();
    }
    Serial.println();

    // Attempt crack (also uploads the signal)
    WaveKaiClient::CrackResult crack = waveKai.crackSignal(sample, samplecount, CC1101_MHZ);

    // Check for auth/token errors
    if (!crack.success && crack.error.length() > 0) {
        lv_label_set_text(wk_lblCaptureStatus, crack.error.c_str());
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        // Update token balance in case it was deducted
        waveKai.refreshBalance();
        if (wk_lblTokenBalance) {
            lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
        }
        return;
    }

    // Refresh token balance after operation
    waveKai.refreshBalance();
    if (wk_lblTokenBalance) {
        lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
    }

    if (crack.success && crack.found) {
        lv_label_set_text_fmt(wk_lblCaptureStatus, "CRACKED! (%d tokens left)", waveKai.tokenBalance);
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
    } else if (crack.success) {
        lv_label_set_text_fmt(wk_lblCaptureStatus, "Analyzed (%d tokens left)", waveKai.tokenBalance);
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
    } else {
        String err = "Error: " + crack.error;
        lv_label_set_text(wk_lblCaptureStatus, err.c_str());
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }

    // Show on results tab
    wk_show_result(crack);
}

// =====================================================================
// Loop Capture — continuously capture + send to API
// =====================================================================
static void wk_loop_start_capture();

static void wk_event_loop_capture(lv_event_t *e) {
    if (wk_loopCaptureActive) {
        // Stop loop
        wk_loopCaptureActive = false;
        SUBGHZ.disableReceiver();
        currentState = WK_STATE_IDLE;
        lv_label_set_text(lv_obj_get_child(wk_btnLoopCapture, 0), "Loop Capture");
        lv_obj_set_style_bg_color(wk_btnLoopCapture, lv_color_hex(0xFF6600), LV_PART_MAIN);
        lv_label_set_text(wk_lblCaptureStatus, "Loop stopped");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x94A3B8), LV_PART_MAIN);
        // Re-enable other buttons
        lv_obj_clear_state(wk_btnCapture, LV_STATE_DISABLED);
        lv_obj_clear_state(wk_btnSendAPI, LV_STATE_DISABLED);
        wkLog("Loop capture stopped after " + String(wk_loopCount) + " captures");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(wk_lblCaptureStatus, "WiFi not connected!");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        return;
    }
    if (!waveKai.isAuthenticated) {
        lv_label_set_text(wk_lblCaptureStatus, "Login required! Go to ACCT tab");
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        return;
    }

    // Start loop
    wk_loopCaptureActive = true;
    wk_loopCount = 0;
    lv_label_set_text(lv_obj_get_child(wk_btnLoopCapture, 0), "STOP LOOP");
    lv_obj_set_style_bg_color(wk_btnLoopCapture, lv_color_hex(0xCC3333), LV_PART_MAIN);
    // Disable other buttons during loop
    lv_obj_add_state(wk_btnCapture, LV_STATE_DISABLED);
    lv_obj_add_state(wk_btnSendAPI, LV_STATE_DISABLED);
    wkLog("Loop capture started");
    wk_loop_start_capture();
}

static void wk_loop_start_capture() {
    if (!wk_loopCaptureActive) return;

    const char *freqStr = lv_textarea_get_text(wk_txtFreq);
    float freq = atof(freqStr);
    if (freq < 300.0 || freq > 928.0) freq = 433.92;

    samplecount = 0;
    SUBGHZ.setFrequency(freq);
    SUBGHZ.enableReceiver();
    currentState = WK_STATE_CAPTURE;

    lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — Listening...", wk_loopCount + 1);
    lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00AFFF), LV_PART_MAIN);
}

// Called from main loop when capture completes during loop mode
static void wk_loop_on_capture_complete() {
    if (!wk_loopCaptureActive) return;
    if (samplecount < 30) {
        // Too few samples, restart capture
        wk_loop_start_capture();
        return;
    }

    wk_loopCount++;
    wkLog("Loop #" + String(wk_loopCount) + ": captured " + String(samplecount) + " samples, sending...");

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — Sending %d samples...", wk_loopCount, samplecount);
        lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
        lv_refr_now(NULL);
        xSemaphoreGive(lvgl_mutex);
    }

    // Send to API
    WaveKaiClient::CrackResult crack = waveKai.crackSignal(sample, samplecount, CC1101_MHZ);

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (crack.success && crack.found) {
            lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — CRACKED!", wk_loopCount);
            lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
        } else if (crack.success && crack.method.length() > 0) {
            lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — %s detected", wk_loopCount, crack.method.c_str());
            lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
        } else if (crack.success) {
            lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — No match", wk_loopCount);
            lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0x94A3B8), LV_PART_MAIN);
        } else {
            lv_label_set_text_fmt(wk_lblCaptureStatus, "Loop #%d — %s", wk_loopCount, crack.error.c_str());
            lv_obj_set_style_text_color(wk_lblCaptureStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        }

        // Update results tab
        wk_show_result(crack);

        // Update protocol/samples labels
        if (crack.method.length() > 0) {
            lv_label_set_text_fmt(wk_lblProtocol, "Protocol: %s", crack.method.c_str());
        } else {
            lv_label_set_text(wk_lblProtocol, "Protocol: --");
        }
        lv_label_set_text_fmt(wk_lblSamples, "Samples: %d", samplecount);

        lv_refr_now(NULL);
        xSemaphoreGive(lvgl_mutex);
    }

    // Brief pause then restart capture
    vTaskDelay(pdMS_TO_TICKS(500));
    if (wk_loopCaptureActive) {
        wk_loop_start_capture();
    }
}

// =====================================================================
// Config tab event handlers
// =====================================================================
static void wk_event_test_connection(lv_event_t *e) {
    wk_refreshWifiStatus();

    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(wk_lblServerStatus, "WiFi not connected!");
        lv_obj_set_style_text_color(wk_lblServerStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
        return;
    }

    lv_label_set_text(wk_lblServerStatus, "Testing...");
    lv_refr_now(NULL);

    String host = lv_textarea_get_text(wk_txtServerIP);
    String url = "http://" + host;
    waveKai.setServer(url);

    if (waveKai.checkConnection()) {
        lv_label_set_text(wk_lblServerStatus, "Connected!");
        lv_obj_set_style_text_color(wk_lblServerStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
    } else {
        String err = "X " + waveKai.lastError;
        lv_label_set_text(wk_lblServerStatus, err.c_str());
        lv_obj_set_style_text_color(wk_lblServerStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

static void wk_event_save_config(lv_event_t *e) {
    String host = lv_textarea_get_text(wk_txtServerIP);
    String url = "http://" + host;

    waveKai.setServer(url);
    waveKai.saveConfig();

    bool autoConn = lv_obj_get_state(wk_cbAutoConnect) & LV_STATE_CHECKED;
    Preferences wkPrefs;
    wkPrefs.begin("wavekai", false);
    wkPrefs.putBool("autoconnect", autoConn);
    wkPrefs.putBool("autocrack", lv_obj_get_state(wk_cbAutoCrack) & LV_STATE_CHECKED);
    wkPrefs.end();

    lv_label_set_text(wk_lblStatus, "Config saved!");
    lv_obj_set_style_text_color(wk_lblStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
}

static void wk_event_back(lv_event_t *e) {
    if (wk_keyboard) {
        lv_obj_add_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    // Stop any active capture
    if (currentState == WK_STATE_CAPTURE) {
        SUBGHZ.disableReceiver();
        currentState = WK_STATE_IDLE;
    }
    extern lv_obj_t *ui_scrMain;
    lv_disp_load_scr(ui_scrMain);
}

// =====================================================================
// Helper: style a tab content panel (dark transparent background)
// =====================================================================
static void wk_styleTabContent(lv_obj_t *tab) {
    lv_obj_set_style_bg_opa(tab, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 8, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

// =====================================================================
// Build CAPTURE tab
// =====================================================================
static void wk_set_freq(float freq) {
    extern float CC1101_MHZ;
    CC1101_MHZ = freq;
    char buf[10];
    snprintf(buf, sizeof(buf), "%.2f", freq);
    lv_textarea_set_text(wk_txtFreq, buf);
}

static void wk_ev_f433(lv_event_t *e) { wk_set_freq(433.92); }
static void wk_ev_f315(lv_event_t *e) { wk_set_freq(315.00); }
static void wk_ev_f868(lv_event_t *e) { wk_set_freq(868.35); }
static void wk_ev_f300(lv_event_t *e) { wk_set_freq(300.00); }

static void wk_ev_preset_changed(lv_event_t *e) {
    uint16_t sel = lv_dropdown_get_selected(wk_ddPreset);
    // 0=AM650, 1=AM270, 2=FM238, 3=FM476
    switch (sel) {
        case 0: SUBGHZ.setPreset(AM650);  wkLog("Preset: AM650"); break;
        case 1: SUBGHZ.setPreset(AM270);  wkLog("Preset: AM270"); break;
        case 2: SUBGHZ.setPreset(FM238);  wkLog("Preset: FM238"); break;
        case 3: SUBGHZ.setPreset(FM476);  wkLog("Preset: FM476"); break;
    }
}

static void wk_build_capture_tab(lv_obj_t *tab) {
    wk_styleTabContent(tab);

    // Frequency input + quick freq buttons
    wk_createLabel(tab, "Freq (MHz):", 0, 0, 0xCCCCCC, &ui_font_Verdana14);
    wk_txtFreq = wk_createTextInput(tab, "433.92", 100, -3, 100, 30);
    lv_textarea_set_text(wk_txtFreq, "433.92");
    wk_createButton(tab, "433", 205, -3, 42, 28, 0x333355, wk_ev_f433);
    wk_createButton(tab, "315", 250, -3, 42, 28, 0x333355, wk_ev_f315);

    // Preset dropdown
    wk_createLabel(tab, "Preset:", 0, 28, 0x94A3B8, &ui_font_Verdana14);
    wk_ddPreset = lv_dropdown_create(tab);
    lv_dropdown_set_symbol(wk_ddPreset, NULL);
    lv_dropdown_set_options(wk_ddPreset, "AM650\nAM270\nFM238\nFM476");
    lv_obj_set_pos(wk_ddPreset, 60, 26);
    lv_obj_set_size(wk_ddPreset, 110, 28);
    lv_obj_set_style_bg_color(wk_ddPreset, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_text_color(wk_ddPreset, lv_color_hex(0x00FF88), LV_PART_MAIN);
    // Use montserrat for dropdown (has arrow symbol), not custom Verdana
    lv_obj_set_style_text_font(wk_ddPreset, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_border_color(wk_ddPreset, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_pad_top(wk_ddPreset, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(wk_ddPreset, 4, LV_PART_MAIN);
    // Style the dropdown list popup
    lv_obj_t *ddList = lv_dropdown_get_list(wk_ddPreset);
    if (ddList) {
        lv_obj_set_style_bg_color(ddList, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_text_color(ddList, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_obj_set_style_text_font(ddList, &lv_font_montserrat_14, LV_PART_MAIN);
    }
    lv_obj_add_event_cb(wk_ddPreset, wk_ev_preset_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Capture / Stop buttons
    wk_btnCapture = wk_createButton(tab, ">> Capture", 0, 58, 140, 36, 0x6366F1, wk_event_capture_start);
    wk_btnStop = wk_createButton(tab, "[] Stop", 150, 58, 140, 36, 0xCC3333, wk_event_capture_stop);
    lv_obj_add_state(wk_btnStop, LV_STATE_DISABLED);

    // Status line
    wk_lblCaptureStatus = wk_createLabel(tab, "Ready to capture", 0, 100, 0x94A3B8, &ui_font_Verdana14);
    lv_obj_set_width(wk_lblCaptureStatus, 290);

    // Protocol identification
    wk_lblProtocol = wk_createLabel(tab, "Protocol: --", 0, 120, 0x94A3B8, &ui_font_Verdana14);
    lv_obj_set_width(wk_lblProtocol, 290);

    // Sample count
    wk_lblSamples = wk_createLabel(tab, "Samples: 0", 0, 140, 0x94A3B8, &ui_font_Verdana14);

    // Separator
    lv_obj_t *sep = lv_obj_create(tab);
    lv_obj_set_pos(sep, 0, 160);
    lv_obj_set_size(sep, 290, 2);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);

    // Action buttons row 1: Replay | Save SD
    wk_btnReplay = wk_createButton(tab, "Replay", 0, 168, 140, 34, 0xFF9100, wk_event_replay);
    lv_obj_add_state(wk_btnReplay, LV_STATE_DISABLED);

    wk_btnSaveSD = wk_createButton(tab, "Save SD", 150, 168, 140, 34, 0x00AA66, wk_event_save_sd);
    lv_obj_add_state(wk_btnSaveSD, LV_STATE_DISABLED);

    // Action button row 2: Send to API | Loop Capture
    wk_btnSendAPI = wk_createButton(tab, "Send to API", 0, 208, 140, 36, 0x6366F1, wk_event_send_api);
    lv_obj_add_state(wk_btnSendAPI, LV_STATE_DISABLED);

    wk_btnLoopCapture = wk_createButton(tab, "Loop Capture", 150, 208, 140, 36, 0xFF6600, wk_event_loop_capture);
}

// =====================================================================
// Build RESULTS tab
// =====================================================================
static void wk_build_results_tab(lv_obj_t *tab) {
    // Make this tab scrollable for all the content
    lv_obj_set_style_bg_opa(tab, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 6, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    // Result title
    wk_lblResultTitle = lv_label_create(tab);
    lv_label_set_text(wk_lblResultTitle, "LAST RESULT");
    lv_obj_set_style_text_color(wk_lblResultTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_lblResultTitle, &ui_font_Verdana18, LV_PART_MAIN);

    // Main result body
    lv_obj_t *resultPanel = lv_obj_create(tab);
    lv_obj_set_size(resultPanel, 295, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(resultPanel, 80, LV_PART_MAIN);
    lv_obj_set_style_max_height(resultPanel, 140, LV_PART_MAIN);
    lv_obj_set_style_bg_color(resultPanel, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(resultPanel, 200, LV_PART_MAIN);
    lv_obj_set_style_border_color(resultPanel, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_border_width(resultPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(resultPanel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(resultPanel, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(resultPanel, LV_SCROLLBAR_MODE_AUTO);

    wk_lblResultBody = lv_label_create(resultPanel);
    lv_label_set_text(wk_lblResultBody, "No results yet.\nCapture a signal and send\nit to the WaveKai API.");
    lv_obj_set_width(wk_lblResultBody, 275);
    lv_label_set_long_mode(wk_lblResultBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wk_lblResultBody, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_lblResultBody, &ui_font_Verdana14, LV_PART_MAIN);

    // Raw hex
    wk_lblRawHex = lv_label_create(tab);
    lv_label_set_text(wk_lblRawHex, "Hex: --");
    lv_obj_set_width(wk_lblRawHex, 295);
    lv_label_set_long_mode(wk_lblRawHex, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wk_lblRawHex, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_lblRawHex, &ui_font_Verdana14, LV_PART_MAIN);

    // Generated codes section title
    lv_obj_t *lblCodesTitle = lv_label_create(tab);
    lv_label_set_text(lblCodesTitle, "GENERATED CODES");
    lv_obj_set_style_text_color(lblCodesTitle, lv_color_hex(0x6366F1), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblCodesTitle, &ui_font_Verdana16, LV_PART_MAIN);

    // Scrollable panel for generated codes list
    wk_panelCodes = lv_obj_create(tab);
    lv_obj_set_size(wk_panelCodes, 295, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(wk_panelCodes, 60, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wk_panelCodes, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wk_panelCodes, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wk_panelCodes, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(wk_panelCodes, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wk_panelCodes, 4, LV_PART_MAIN);

    // Initial empty state
    lv_obj_t *lblEmpty = lv_label_create(wk_panelCodes);
    lv_label_set_text(lblEmpty, "Crack a rolling code to generate\nnext codes with Save/Send.");
    lv_obj_set_style_text_color(lblEmpty, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblEmpty, &ui_font_Verdana14, LV_PART_MAIN);
}

// =====================================================================
// Build CONFIG tab
// =====================================================================
// =====================================================================
// Account tab: Login, token balance, device registration
// =====================================================================
static void wk_event_login(lv_event_t *e) {
    String email = lv_textarea_get_text(wk_txtLoginEmail);
    String pass = lv_textarea_get_text(wk_txtLoginPass);
    if (email.length() == 0 || pass.length() == 0) {
        lv_label_set_text(wk_lblAccountStatus, "Enter email & password");
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
        return;
    }
    String loginMsg = "Logging in to " + waveKai.serverUrl + "...";
    lv_label_set_text(wk_lblAccountStatus, loginMsg.c_str());
    lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_refr_now(NULL);

    if (waveKai.login(email, pass)) {
        // Show logged-in panel
        lv_obj_add_flag(wk_panelLoginForm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wk_panelLoggedIn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(wk_lblAccountStatus, "Logged in as %s", waveKai.username.c_str());
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
        lv_label_set_text_fmt(wk_lblDeviceMac, "MAC: %s", waveKai.deviceMac.c_str());
    } else {
        lv_label_set_text_fmt(wk_lblAccountStatus, "Login failed: %s", waveKai.lastError.c_str());
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

static void wk_event_logout(lv_event_t *e) {
    waveKai.logout();
    lv_obj_clear_flag(wk_panelLoginForm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wk_panelLoggedIn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wk_lblAccountStatus, "Logged out");
    lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
}

static void wk_event_register_device(lv_event_t *e) {
    lv_label_set_text(wk_lblAccountStatus, "Registering...");
    if (waveKai.registerDevice()) {
        lv_label_set_text(wk_lblAccountStatus, "Device registered!");
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
    } else {
        String msg = "Register failed: " + waveKai.lastError;
        lv_label_set_text(wk_lblAccountStatus, msg.c_str());
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFF4444), LV_PART_MAIN);
    }
}

static void wk_event_refresh_balance(lv_event_t *e) {
    if (waveKai.refreshBalance()) {
        lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
    }
}

static void wk_build_account_tab(lv_obj_t *tab) {
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Status label
    wk_lblAccountStatus = lv_label_create(tab);
    lv_label_set_text(wk_lblAccountStatus, waveKai.isAuthenticated ? "Logged in" : "Not logged in");
    lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_lblAccountStatus, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_set_width(wk_lblAccountStatus, 290);

    // === Login form panel (hidden when logged in) ===
    wk_panelLoginForm = lv_obj_create(tab);
    lv_obj_set_size(wk_panelLoginForm, 300, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(wk_panelLoginForm, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(wk_panelLoginForm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wk_panelLoginForm, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(wk_panelLoginForm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wk_panelLoginForm, 6, LV_PART_MAIN);

    lv_obj_t *lblEmail = lv_label_create(wk_panelLoginForm);
    lv_label_set_text(lblEmail, "Email / Username:");
    lv_obj_set_style_text_color(lblEmail, lv_color_hex(0x94A3B8), LV_PART_MAIN);

    wk_txtLoginEmail = lv_textarea_create(wk_panelLoginForm);
    lv_textarea_set_one_line(wk_txtLoginEmail, true);
    lv_textarea_set_placeholder_text(wk_txtLoginEmail, "you@example.com");
    lv_obj_set_width(wk_txtLoginEmail, 280);
    lv_obj_set_style_bg_color(wk_txtLoginEmail, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
    lv_obj_set_style_text_color(wk_txtLoginEmail, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_border_color(wk_txtLoginEmail, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_add_event_cb(wk_txtLoginEmail, [](lv_event_t *e) {
        wk_activeTextarea = lv_event_get_target(e);
        lv_keyboard_set_textarea(wk_keyboard, wk_activeTextarea);
        lv_obj_clear_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *lblPass = lv_label_create(wk_panelLoginForm);
    lv_label_set_text(lblPass, "Password:");
    lv_obj_set_style_text_color(lblPass, lv_color_hex(0x94A3B8), LV_PART_MAIN);

    wk_txtLoginPass = lv_textarea_create(wk_panelLoginForm);
    lv_textarea_set_one_line(wk_txtLoginPass, true);
    lv_textarea_set_placeholder_text(wk_txtLoginPass, "password");
    lv_textarea_set_password_mode(wk_txtLoginPass, true);
    lv_obj_set_width(wk_txtLoginPass, 280);
    lv_obj_set_style_bg_color(wk_txtLoginPass, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
    lv_obj_set_style_text_color(wk_txtLoginPass, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_border_color(wk_txtLoginPass, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_add_event_cb(wk_txtLoginPass, [](lv_event_t *e) {
        wk_activeTextarea = lv_event_get_target(e);
        lv_keyboard_set_textarea(wk_keyboard, wk_activeTextarea);
        lv_obj_clear_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    // Pre-fill saved login credentials from NVS
    if (waveKai.loginEmail.length() > 0) {
        lv_textarea_set_text(wk_txtLoginEmail, waveKai.loginEmail.c_str());
    }
    if (waveKai.loginPass.length() > 0) {
        lv_textarea_set_text(wk_txtLoginPass, waveKai.loginPass.c_str());
    }

    wk_btnLogin = wk_createButton(wk_panelLoginForm, "Login", 0, 0, 280, 36, 0x6366F1, wk_event_login);

    // === Logged-in panel (hidden when not logged in) ===
    wk_panelLoggedIn = lv_obj_create(tab);
    lv_obj_set_size(wk_panelLoggedIn, 300, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(wk_panelLoggedIn, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(wk_panelLoggedIn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wk_panelLoggedIn, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(wk_panelLoggedIn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wk_panelLoggedIn, 8, LV_PART_MAIN);

    wk_lblTokenBalance = lv_label_create(wk_panelLoggedIn);
    lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
    lv_obj_set_style_text_color(wk_lblTokenBalance, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_lblTokenBalance, &ui_font_Verdana18, LV_PART_MAIN);

    wk_lblDeviceMac = lv_label_create(wk_panelLoggedIn);
    lv_label_set_text_fmt(wk_lblDeviceMac, "MAC: %s", waveKai.deviceMac.c_str());
    lv_obj_set_style_text_color(wk_lblDeviceMac, lv_color_hex(0x64748B), LV_PART_MAIN);

    wk_btnRegDevice = wk_createButton(wk_panelLoggedIn, "Register Device", 0, 0, 280, 36, 0x00AA66, wk_event_register_device);

    wk_btnRefreshBal = wk_createButton(wk_panelLoggedIn, "Refresh Balance", 0, 0, 280, 36, 0x6366F1, wk_event_refresh_balance);

    wk_btnLogout = wk_createButton(wk_panelLoggedIn, "Logout", 0, 0, 280, 36, 0xCC3333, wk_event_logout);

    // Set initial visibility based on persisted auth state
    if (waveKai.isAuthenticated) {
        lv_obj_add_flag(wk_panelLoginForm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wk_panelLoggedIn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(wk_lblAccountStatus, "Logged in as %s", waveKai.username.c_str());
        lv_obj_set_style_text_color(wk_lblAccountStatus, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
        lv_label_set_text_fmt(wk_lblDeviceMac, "MAC: %s", waveKai.deviceMac.c_str());
        // Refresh balance from server in background
        if (WiFi.status() == WL_CONNECTED) {
            waveKai.refreshBalance();
            lv_label_set_text_fmt(wk_lblTokenBalance, "Tokens: %d", waveKai.tokenBalance);
        }
    } else {
        lv_obj_add_flag(wk_panelLoggedIn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wk_panelLoginForm, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wk_build_config_tab(lv_obj_t *tab) {
    wk_styleTabContent(tab);

    // WiFi Status
    wk_lblWifiStatus = wk_createLabel(tab, "WiFi: Checking...", 0, 0, 0x94A3B8, &ui_font_Verdana14);
    lv_obj_set_width(wk_lblWifiStatus, 290);
    lv_label_set_long_mode(wk_lblWifiStatus, LV_LABEL_LONG_CLIP);

    // Server Host
    wk_createLabel(tab, "Server:", 0, 28, 0xCCCCCC, &ui_font_Verdana14);
    wk_txtServerIP = wk_createTextInput(tab, "3.224.236.50", 0, 48, 290, 30);

    // Load saved values
    bool savedAutoConn = true, savedAutoCrack = true;
    {
        Preferences wkPrefs;
        wkPrefs.begin("wavekai", true);
        String savedServer = wkPrefs.getString("server", WAVEKAI_SERVER);
        savedAutoConn = wkPrefs.getBool("autoconnect", true);
        savedAutoCrack = wkPrefs.getBool("autocrack", true);
        wkPrefs.end();

        // Extract host from saved URL (strip http:// prefix)
        String host = savedServer;
        if (host.startsWith("http://")) host = host.substring(7);
        if (host.startsWith("https://")) host = host.substring(8);
        // Remove trailing slash or port
        int slashIdx = host.indexOf('/');
        if (slashIdx > 0) host = host.substring(0, slashIdx);

        lv_textarea_set_text(wk_txtServerIP, host.c_str());
    }

    // Server status
    wk_lblServerStatus = wk_createLabel(tab, "Server: Not tested", 0, 85, 0x94A3B8, &ui_font_Verdana14);
    lv_obj_set_width(wk_lblServerStatus, 290);
    lv_label_set_long_mode(wk_lblServerStatus, LV_LABEL_LONG_CLIP);

    // Test / Save buttons
    wk_createButton(tab, "Test", 0, 108, 140, 36, 0x6366F1, wk_event_test_connection);
    wk_createButton(tab, "Save", 150, 108, 140, 36, 0x00AA66, wk_event_save_config);

    // Checkboxes
    wk_cbAutoConnect = lv_checkbox_create(tab);
    lv_obj_set_pos(wk_cbAutoConnect, 0, 155);
    lv_checkbox_set_text(wk_cbAutoConnect, "Auto-connect WiFi");
    lv_obj_set_style_text_color(wk_cbAutoConnect, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_cbAutoConnect, &ui_font_Verdana14, LV_PART_MAIN);
    if (savedAutoConn) lv_obj_add_state(wk_cbAutoConnect, LV_STATE_CHECKED);

    wk_cbAutoCrack = lv_checkbox_create(tab);
    lv_obj_set_pos(wk_cbAutoCrack, 0, 183);
    lv_checkbox_set_text(wk_cbAutoCrack, "Auto-crack on capture");
    lv_obj_set_style_text_color(wk_cbAutoCrack, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(wk_cbAutoCrack, &ui_font_Verdana14, LV_PART_MAIN);
    if (savedAutoCrack) lv_obj_add_state(wk_cbAutoCrack, LV_STATE_CHECKED);

    // Firmware version
    String verStr = "Firmware: v" + String(WAVEKAI_FW_VERSION);
    wk_createLabel(tab, verStr.c_str(), 0, 215, 0x64748B, &ui_font_Verdana14);

    // Check for Updates button
    wk_createButton(tab, "Check for Update", 0, 238, 290, 36, 0x6366F1, [](lv_event_t *e) {
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, "Checking...");
        lv_refr_now(NULL);

        if (waveKai.checkForUpdate()) {
            // Show update available popup
            String msg = "Update available!\n\nCurrent: v" + String(WAVEKAI_FW_VERSION) +
                         "\nLatest: " + waveKai.latestVersion +
                         "\n\nDownload and install?";

            // Create modal overlay
            lv_obj_t *overlay = lv_obj_create(lv_scr_act());
            lv_obj_set_size(overlay, 320, 480);
            lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(overlay, 200, LV_PART_MAIN);
            lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
            lv_obj_center(overlay);
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *popup = lv_obj_create(overlay);
            lv_obj_set_size(popup, 280, 260);
            lv_obj_center(popup);
            lv_obj_set_style_bg_color(popup, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
            lv_obj_set_style_border_color(popup, lv_color_hex(0x00FF88), LV_PART_MAIN);
            lv_obj_set_style_radius(popup, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_all(popup, 16, LV_PART_MAIN);
            lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *msgLbl = lv_label_create(popup);
            lv_label_set_text(msgLbl, msg.c_str());
            lv_obj_set_width(msgLbl, 245);
            lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(msgLbl, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
            lv_obj_set_style_text_font(msgLbl, &ui_font_Verdana14, LV_PART_MAIN);
            lv_obj_set_pos(msgLbl, 0, 0);

            // Progress bar (hidden initially)
            lv_obj_t *progressBar = lv_bar_create(popup);
            lv_obj_set_pos(progressBar, 0, 120);
            lv_obj_set_size(progressBar, 245, 16);
            lv_bar_set_range(progressBar, 0, 100);
            lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x0D0D1A), LV_PART_MAIN);
            lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
            lv_obj_set_style_radius(progressBar, 8, LV_PART_MAIN);
            lv_obj_set_style_radius(progressBar, 8, LV_PART_INDICATOR);
            lv_obj_add_flag(progressBar, LV_OBJ_FLAG_HIDDEN);

            // Progress label
            lv_obj_t *progressLbl = lv_label_create(popup);
            lv_obj_set_pos(progressLbl, 0, 140);
            lv_label_set_text(progressLbl, "");
            lv_obj_set_style_text_color(progressLbl, lv_color_hex(0x94A3B8), LV_PART_MAIN);
            lv_obj_set_style_text_font(progressLbl, &ui_font_Verdana14, LV_PART_MAIN);

            // Install button
            lv_obj_t *btnInstall = lv_btn_create(popup);
            lv_obj_set_pos(btnInstall, 0, 170);
            lv_obj_set_size(btnInstall, 120, 36);
            lv_obj_set_style_bg_color(btnInstall, lv_color_hex(0x00AA66), LV_PART_MAIN);
            lv_obj_set_style_radius(btnInstall, 6, LV_PART_MAIN);
            lv_obj_t *lblI = lv_label_create(btnInstall);
            lv_label_set_text(lblI, "Install");
            lv_obj_center(lblI);

            // Store UI refs for progress callback
            struct OTAContext {
                lv_obj_t *msgLabel;
                lv_obj_t *bar;
                lv_obj_t *pctLabel;
                lv_obj_t *overlay;
            };
            static OTAContext otaCtx;
            otaCtx.msgLabel = msgLbl;
            otaCtx.bar = progressBar;
            otaCtx.pctLabel = progressLbl;
            otaCtx.overlay = overlay;

            lv_obj_add_event_cb(btnInstall, [](lv_event_t *ev) {
                // Prevent double-tap
                static bool otaRunning = false;
                if (otaRunning) return;
                otaRunning = true;

                // Disable button immediately
                lv_obj_t *btn = lv_event_get_target(ev);
                lv_obj_add_state(btn, LV_STATE_DISABLED);

                // Show progress UI
                if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    lv_label_set_text(otaCtx.msgLabel, "Downloading firmware...\nDo not power off!");
                    lv_obj_clear_flag(otaCtx.bar, LV_OBJ_FLAG_HIDDEN);
                    lv_bar_set_value(otaCtx.bar, 0, LV_ANIM_OFF);
                    lv_label_set_text(otaCtx.pctLabel, "Connecting...");

                    // Hide buttons
                    lv_obj_t *btn = lv_event_get_target(ev);
                    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_t *parent = lv_obj_get_parent(btn);
                    for (int i = 0; i < (int)lv_obj_get_child_cnt(parent); i++) {
                        lv_obj_t *child = lv_obj_get_child(parent, i);
                        if (child != btn && lv_obj_check_type(child, &lv_btn_class)) {
                            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                    xSemaphoreGive(lvgl_mutex);
                }

                // Set progress callback (updates UI from OTA task)
                waveKai.otaProgressCallback = [](int pct, size_t written, size_t total) {
                    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        lv_bar_set_value(otaCtx.bar, pct, LV_ANIM_OFF);
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d%%  %dK / %dK", pct, (int)(written/1024), (int)(total/1024));
                        lv_label_set_text(otaCtx.pctLabel, buf);
                        xSemaphoreGive(lvgl_mutex);
                    }
                };

                // Run OTA in a separate task so LVGL doesn't get blocked
                xTaskCreatePinnedToCore([](void* param) {
                    wkLog("OTA task started");
                    bool success = waveKai.performOTA();
                    waveKai.otaProgressCallback = nullptr;

                    if (success) {
                        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                            lv_label_set_text(otaCtx.msgLabel, "Update complete!\nRebooting...");
                            lv_bar_set_value(otaCtx.bar, 100, LV_ANIM_OFF);
                            lv_label_set_text(otaCtx.pctLabel, "100% - Rebooting...");
                            xSemaphoreGive(lvgl_mutex);
                        }
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ESP.restart();
                    } else {
                        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                            lv_label_set_text(otaCtx.msgLabel, ("Update failed:\n" + waveKai.lastError).c_str());
                            lv_obj_set_style_text_color(otaCtx.msgLabel, lv_color_hex(0xFF4444), LV_PART_MAIN);
                            lv_label_set_text(otaCtx.pctLabel, "Failed");
                            lv_obj_set_style_bg_color(otaCtx.bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
                            xSemaphoreGive(lvgl_mutex);
                        }
                    }
                    vTaskDelete(NULL);
                }, "ota_task", 8192, NULL, 1, NULL, 1);  // Core 1, priority 1
            }, LV_EVENT_CLICKED, overlay);

            // Cancel button
            lv_obj_t *btnCancel = lv_btn_create(popup);
            lv_obj_set_pos(btnCancel, 130, 170);
            lv_obj_set_size(btnCancel, 115, 36);
            lv_obj_set_style_bg_color(btnCancel, lv_color_hex(0x333355), LV_PART_MAIN);
            lv_obj_set_style_radius(btnCancel, 6, LV_PART_MAIN);
            lv_obj_t *lblC = lv_label_create(btnCancel);
            lv_label_set_text(lblC, "Cancel");
            lv_obj_center(lblC);
            lv_obj_add_event_cb(btnCancel, [](lv_event_t *ev) {
                lv_obj_t *ol = (lv_obj_t*)lv_event_get_user_data(ev);
                lv_obj_del(ol);
            }, LV_EVENT_CLICKED, overlay);

            lv_label_set_text(lbl, "Check for Update");
        } else {
            lv_label_set_text(lbl, "Up to date!");
            // Reset button text after 3 seconds
        }
    });

    // Status label
    wk_lblStatus = wk_createLabel(tab, "", 0, 280, 0x00FF88, &ui_font_Verdana14);
}

// =====================================================================
// wk_screen_init() — build the tabbed WaveKai screen (320x480)
// =====================================================================
static void wk_screen_init(void) {
    // --- Screen ---
    ui_scrWaveKai = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrWaveKai, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrWaveKai, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(ui_scrWaveKai, &ui_img_blankpgbkgnd_png, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrWaveKai, LV_OBJ_FLAG_SCROLLABLE);

    // Refresh WiFi status on screen load
    lv_obj_add_event_cb(ui_scrWaveKai, [](lv_event_t *e) {
        wk_refreshWifiStatus();
    }, LV_EVENT_SCREEN_LOADED, NULL);

    // --- Title bar with BACK button ---
    lv_obj_t *titleBar = lv_obj_create(ui_scrWaveKai);
    lv_obj_set_pos(titleBar, 0, 0);
    lv_obj_set_size(titleBar, 320, 36);
    lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x0A0A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(titleBar, 220, LV_PART_MAIN);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(titleBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(titleBar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_SCROLLABLE);

    // BACK button (left side of title bar)
    lv_obj_t *btnBack = lv_btn_create(titleBar);
    lv_obj_set_pos(btnBack, 4, 3);
    lv_obj_set_size(btnBack, 60, 30);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(btnBack, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnBack, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btnBack, wk_event_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, "<");
    lv_obj_center(lblBack);
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    // Title text
    lv_obj_t *title = lv_label_create(titleBar);
    lv_label_set_text(title, "WAVEKAI");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &ui_font_Verdana18, LV_PART_MAIN);
    lv_obj_set_align(title, LV_ALIGN_CENTER);

    // --- Tabview (matches CC1101Stuff screen style) ---
    wk_tabview = lv_tabview_create(ui_scrWaveKai, LV_DIR_BOTTOM, 30);
    lv_obj_set_pos(wk_tabview, 0, 36);
    lv_obj_set_size(wk_tabview, 320, 444);
    lv_obj_clear_flag(wk_tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(wk_tabview, 0, LV_PART_MAIN);

    // Tab buttons: transparent bg, orange text (same as CC1101Stuff)
    lv_obj_t *tabBtns = lv_tabview_get_tab_btns(wk_tabview);
    lv_obj_set_style_bg_opa(tabBtns, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(tabBtns, lv_color_hex(0xFF9202), LV_PART_ITEMS);
    lv_obj_set_style_text_opa(tabBtns, 255, LV_PART_ITEMS);
    lv_obj_set_style_text_font(tabBtns, &ui_font_Verdana14, LV_PART_ITEMS);

    // Create tabs
    lv_obj_t *tabCapture = lv_tabview_add_tab(wk_tabview, "CAPTURE");
    lv_obj_t *tabResults = lv_tabview_add_tab(wk_tabview, "RESULTS");
    lv_obj_t *tabConfig  = lv_tabview_add_tab(wk_tabview, "CONFIG");
    lv_obj_t *tabAccount = lv_tabview_add_tab(wk_tabview, "ACCT");

    // Build each tab
    wk_build_capture_tab(tabCapture);
    wk_build_results_tab(tabResults);
    wk_build_config_tab(tabConfig);
    wk_build_account_tab(tabAccount);

    // --- Keyboard (shared, hidden initially, on top of everything) ---
    wk_keyboard = lv_keyboard_create(ui_scrWaveKai);
    lv_obj_set_size(wk_keyboard, 320, 200);
    lv_obj_align(wk_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(wk_keyboard, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(wk_keyboard, lv_color_hex(0x333355), LV_PART_ITEMS);
    lv_obj_set_style_text_color(wk_keyboard, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_add_flag(wk_keyboard, LV_OBJ_FLAG_HIDDEN);
}

#endif // WAVEKAI_SCREEN_H
