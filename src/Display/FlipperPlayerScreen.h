#ifndef FlipperPlayerScreen_h
#define FlipperPlayerScreen_h

// ----------------------------------------------------------------
// Hand-coded Flipper .sub player screen. Replaces the SquareLine
// "FLIPPER RAW PLAYER" layout that lived on ui_scrPresets.
//
//   - Folder + file dropdowns (browses /subghz on the SD card)
//   - File metadata box (protocol / freq / sample count)
//   - PLAY  — TX the selected .sub
//   - STOP  — interrupt current TX/capture
//   - READ RAW — start CC1101 OOK async capture into a new .sub
//                under /captures/raw_<ts>.sub, auto-stops after
//                1.5 s of silence or 4096 transitions
//   - Status line + capture-count progress label
//   - BACK
//
// Reuses the existing ui_ddPresetsFolder / ui_ddPresetsFile globals
// so the existing event_send_flipper_file() handler still works.
// ----------------------------------------------------------------

#include <lvgl.h>
#include <ui.h>
#include <SD.h>
#include <string>
#include <vector>
#include "Display/Event.h"
#include "Misc/Config.h"
#include "SubGhz/BinRawDecoder.h"
#include "SubGhz/KeeLoq.h"
#include "SubGhz/KeeLoqClone.h"

// Forward decl from main.cpp
extern uint8_t currentState;
void event_send_flipper_file(lv_event_t *e);

class SubGhz;
extern SubGhz SUBGHZ;

static lv_obj_t *fp_lblStatus    = NULL;
static lv_obj_t *fp_lblCapture   = NULL;
static lv_obj_t *fp_btnPlay      = NULL;
static lv_obj_t *fp_btnStop      = NULL;
static lv_obj_t *fp_btnRaw       = NULL;
static lv_obj_t *fp_btnDecode    = NULL;
static lv_obj_t *fp_ddPreset     = NULL;   // CC1101 modulation preset
static lv_obj_t *fp_ddFreq       = NULL;   // capture frequency MHz
static lv_obj_t *fp_recDot       = NULL;   // pulsing red record dot
static lv_obj_t *fp_recLbl       = NULL;   // "REC" text next to the dot
static lv_obj_t *fp_recCount     = NULL;   // big live "N" edge counter
static lv_timer_t *fp_rawTimer   = NULL;
static char fp_rawFilename[96]   = {0};
static uint8_t fp_recPulsePhase  = 0;      // toggles 0/1 each timer tick

static void fp_status_set(const char *txt, uint32_t color = 0x00DDFF) {
    if (!fp_lblStatus) return;
    lv_label_set_text(fp_lblStatus, txt);
    lv_obj_set_style_text_color(fp_lblStatus, lv_color_hex(color), LV_PART_MAIN);
}

static lv_obj_t *fp_mk_btn(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *text, uint32_t bg) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &ui_font_Verdana14, LV_PART_MAIN);
    return b;
}

static lv_obj_t *fp_mk_lbl(lv_obj_t *parent, int x, int y, const char *t,
                            uint32_t color, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, t);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    return l;
}

// -------------------- Raw capture progress timer --------------------
// SubGhz exposes capture state via rawCaptureCount() + rawCaptureRunning()
// + rawCaptureLastTransitionMs(). We poll at 200 ms cadence: update the
// capture-count label, and auto-stop after 1500 ms of silence.
extern "C" {
  bool fp_subghz_raw_running();
  int  fp_subghz_raw_count();
  uint32_t fp_subghz_raw_last_ms();
  void fp_subghz_raw_stop();
  bool fp_subghz_raw_start(float freq_mhz, const char *filename);
  bool fp_subghz_raw_start_preset(float freq_mhz, const char *filename, int preset);
  const char *fp_subghz_preset_name(int preset);
}

// Common SubGHz capture frequencies (MHz) — matches the dropdown order below.
// 433.92 is the typical default for ISM remotes and is index 6.
static const float FP_FREQ_TABLE[] = {
    300.00f, 303.87f, 315.00f, 390.00f, 418.00f,
    433.07f, 433.92f, 434.42f, 868.35f, 915.00f
};
static const int  FP_FREQ_DEFAULT_IDX = 6;   // 433.92 MHz
static const int  FP_PRESET_DEFAULT_IDX = 0; // OOK 650 (CC1101_PRESET_OOK_650 = AM650 = 0)

// Map the preset-dropdown index (0..3) to the CC1101Preset enum value used
// by the SubGhz driver. Keep order in sync with the dropdown options string
// below.
static inline int fp_preset_idx_to_enum(int idx) {
    // Dropdown: 0=OOK 270, 1=OOK 650, 2=FSK 238, 3=FSK 476
    // Enum   : AM650=0, AM270=1, FM238=2, FM476=3
    switch (idx) {
        case 0: return 1;  // AM270
        case 1: return 0;  // AM650 (default)
        case 2: return 2;  // FM238
        case 3: return 3;  // FM476
        default: return 0;
    }
}

// Show/hide the live REC overlay (red dot + REC text + big edge counter)
static void fp_rec_overlay_show(bool visible) {
    auto setvis = [visible](lv_obj_t *o) {
        if (!o) return;
        if (visible) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    setvis(fp_recDot);
    setvis(fp_recLbl);
    setvis(fp_recCount);
}

static void fp_raw_timer_cb(lv_timer_t * /*t*/) {
    if (!fp_subghz_raw_running()) return;
    int cnt = fp_subghz_raw_count();

    // Drive the buffer-fill bar (clamped at 4096 = our ring buffer size)
    if (fp_recDot) {
        int v = cnt > 4096 ? 4096 : cnt;
        lv_bar_set_value(fp_recDot, v, LV_ANIM_OFF);
    }

    // Big right-justified edge count — updates each timer tick so it
    // visually races whenever edges are coming in (the "live" feel).
    if (fp_recCount) {
        char b[32]; snprintf(b, sizeof(b), "%d edges", cnt);
        lv_label_set_text(fp_recCount, b);
    }

    // Toggle the "● CAPTURING" label color so the bullet pulses green
    // even when no new edges have arrived yet (positive confirmation
    // the capture loop is alive, like the scanner sweep counter).
    fp_recPulsePhase ^= 1;
    if (fp_recLbl) {
        lv_obj_set_style_text_color(fp_recLbl,
            lv_color_hex(fp_recPulsePhase ? 0x00FF88 : 0x006633),
            LV_PART_MAIN);
    }

    if (fp_lblCapture) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Captured: %d edges", cnt);
        lv_label_set_text(fp_lblCapture, buf);
    }

    uint32_t now = millis();
    uint32_t last = fp_subghz_raw_last_ms();
    // Auto-stop if no transition for 1.5 s AND we've captured >=8 edges
    if (cnt >= 8 && (now - last) > 1500) {
        fp_subghz_raw_stop();
        if (fp_rawTimer) {
            lv_timer_del(fp_rawTimer);
            fp_rawTimer = NULL;
        }
        fp_rec_overlay_show(false);
        char msg[128];
        snprintf(msg, sizeof(msg), "Saved %d edges to %s", cnt, fp_rawFilename);
        fp_status_set(msg, 0x00FF88);
    }
}

// -------------------- BinRAW decode popup --------------------
static lv_obj_t *fp_decodePopup = NULL;

static const char *fp_enc_name(BinRaw::Encoding enc) {
    switch (enc) {
        case BinRaw::ENC_PWM:        return "PWM";
        case BinRaw::ENC_PPM:        return "PPM";
        case BinRaw::ENC_MANCHESTER: return "Manchester";
        case BinRaw::ENC_NRZ:        return "NRZ";
        case BinRaw::ENC_UNKNOWN:
        default:                     return "Unknown";
    }
}

static void fp_decode_popup_close_cb(lv_event_t *e) {
    if (fp_decodePopup) {
        lv_obj_del(fp_decodePopup);
        fp_decodePopup = NULL;
    }
}

// Build a centered modal popup over ui_scrPresets showing decode results.
// If `result` is NULL, show only the title + body string (used for the
// "No RAW_Data" / error path).
static void fp_decode_popup_show(const char *title, const char *body,
                                  const BinRaw::Result *result) {
    if (fp_decodePopup) {
        lv_obj_del(fp_decodePopup);
        fp_decodePopup = NULL;
    }
    fp_decodePopup = lv_obj_create(ui_scrPresets);
    lv_obj_set_size(fp_decodePopup, 300, 380);
    lv_obj_align(fp_decodePopup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(fp_decodePopup, lv_color_hex(0x0A0A1E), LV_PART_MAIN);
    lv_obj_set_style_border_color(fp_decodePopup, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(fp_decodePopup, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(fp_decodePopup, 8, LV_PART_MAIN);
    lv_obj_clear_flag(fp_decodePopup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblTitle = lv_label_create(fp_decodePopup);
    lv_label_set_text(lblTitle, title);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblTitle, &ui_font_Verdana18, LV_PART_MAIN);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *lblBody = lv_label_create(fp_decodePopup);
    lv_obj_set_width(lblBody, 270);
    lv_label_set_long_mode(lblBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(lblBody, 10, 40);
    lv_obj_set_style_text_color(lblBody, lv_color_hex(0xCCEEFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblBody, &ui_font_Verdana12, LV_PART_MAIN);

    if (result) {
        // Truncate the bit string to first 80 chars for the display
        char bits_preview[96];
        size_t bn = strnlen(result->bits, sizeof(result->bits));
        size_t shown = bn > 80 ? 80 : bn;
        memcpy(bits_preview, result->bits, shown);
        bits_preview[shown] = '\0';
        bool truncated = (bn > 80);

        char buf[640];
        snprintf(buf, sizeof(buf),
                 "Te: %d us\n"
                 "Encoding: %s\n"
                 "Bits: %d\n"
                 "Symbols: %d\n"
                 "Hex: %s\n\n"
                 "Bits[0..%u]:\n%s%s",
                 result->te_us,
                 fp_enc_name(result->encoding),
                 result->bit_count,
                 result->symbol_count,
                 result->hex,
                 (unsigned)shown,
                 bits_preview,
                 truncated ? "..." : "");
        lv_label_set_text(lblBody, buf);
    } else {
        lv_label_set_text(lblBody, body ? body : "");
    }

    lv_obj_t *btnOk = lv_btn_create(fp_decodePopup);
    lv_obj_set_size(btnOk, 100, 40);
    lv_obj_align(btnOk, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btnOk, lv_color_hex(0x006633), LV_PART_MAIN);
    lv_obj_set_style_radius(btnOk, 6, LV_PART_MAIN);
    lv_obj_t *lblOk = lv_label_create(btnOk);
    lv_label_set_text(lblOk, "OK");
    lv_obj_center(lblOk);
    lv_obj_set_style_text_color(lblOk, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblOk, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_add_event_cb(btnOk, fp_decode_popup_close_cb, LV_EVENT_CLICKED, NULL);
}

// Read the currently selected folder/file dropdowns, open the .sub file,
// concatenate every "RAW_Data:" line into one buffer (capped at 64 KB),
// then run BinRaw::analyze() on the parsed timings and show a popup.
static void fp_decode_run() {
    if (!ui_ddPresetsFolder || !ui_ddPresetsFile) return;

    char folder[64];
    char file[96];
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, folder, sizeof(folder));
    lv_dropdown_get_selected_str(ui_ddPresetsFile,   file,   sizeof(file));

    if (file[0] == '\0' || strcmp(file, "(none)") == 0) {
        fp_decode_popup_show("BinRAW Decode", "No file selected.", NULL);
        return;
    }

    char fullpath[200];
    if (strcmp(folder, "/") == 0 || folder[0] == '\0') {
        snprintf(fullpath, sizeof(fullpath), "/%s", file);
    } else {
        snprintf(fullpath, sizeof(fullpath), "/%s/%s", folder, file);
    }

    // Mount the SD card via the project's bus-arbiter helper before
    // opening the file — the player screen never calls SD.begin()
    // directly, so a cold tap on DECODE used to fail with "Failed to
    // open" because the SD bus wasn't claimed.
    extern bool sd_card_is_present();
    extern void now_close_sd_card();
    if (!sd_card_is_present()) {
        fp_decode_popup_show("BinRAW Decode",
            "SD card not present.\nInsert card and retry.", NULL);
        return;
    }

    File f = SD.open(fullpath, FILE_READ);
    if (!f) {
        // Some captures end up in /captures/ even when the dropdown
        // shows them at root — retry with a /captures/ prefix as a
        // fallback before giving up.
        char alt[220];
        snprintf(alt, sizeof(alt), "/captures/%s", file);
        f = SD.open(alt, FILE_READ);
        if (f) {
            strncpy(fullpath, alt, sizeof(fullpath));
            fullpath[sizeof(fullpath) - 1] = '\0';
        }
    }
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open:\n%s", fullpath);
        fp_decode_popup_show("BinRAW Decode", msg, NULL);
        now_close_sd_card();
        return;
    }

    // Cap raw-data accumulation at ~64 KB to bound memory.
    static const size_t RAW_CAP = 64 * 1024;
    std::string raw;
    raw.reserve(4096);
    bool found_any = false;
    bool truncated = false;

    while (f.available() && raw.size() < RAW_CAP) {
        String line = f.readStringUntil('\n');
        // Look for "RAW_Data:" prefix (allow leading whitespace)
        const char *s = line.c_str();
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "RAW_Data:", 9) != 0) continue;
        found_any = true;
        const char *vals = s + 9;
        while (*vals == ' ' || *vals == '\t') vals++;
        size_t want = strlen(vals);
        if (raw.size() + want + 1 > RAW_CAP) {
            want = (raw.size() < RAW_CAP) ? (RAW_CAP - raw.size() - 1) : 0;
            truncated = true;
        }
        if (want > 0) {
            raw.append(vals, want);
            raw.push_back(' ');
        }
        if (truncated) break;
    }
    f.close();
    now_close_sd_card();

    if (!found_any) {
        fp_decode_popup_show("BinRAW Decode",
            "No RAW_Data in this file", NULL);
        return;
    }
    if (raw.empty()) {
        fp_decode_popup_show("BinRAW Decode",
            "RAW_Data section was empty.", NULL);
        return;
    }

    int32_t *timings = NULL;
    size_t n = BinRaw::parse_raw_data(raw.c_str(), &timings);
    if (n == 0 || !timings) {
        if (timings) free(timings);
        fp_decode_popup_show("BinRAW Decode",
            "Failed to parse RAW_Data timings.", NULL);
        return;
    }

    BinRaw::Result result;
    bool ok = BinRaw::analyze(timings, n, result);
    free(timings);

    if (!ok) {
        char msg[160];
        snprintf(msg, sizeof(msg),
            "Analyze failed.\nSymbols read: %u%s",
            (unsigned)n, truncated ? "\n(input truncated at 64KB)" : "");
        fp_decode_popup_show("BinRAW Decode", msg, NULL);
        return;
    }

    fp_decode_popup_show("BinRAW Decode", NULL, &result);
}

// -------------------- KeeLoq clone modal --------------------
//
// Lightweight first-pass UI: a modal popup that
//   1) shows decoded frame fields (serial, button, status, encrypted),
//   2) if any manufacturer keys are loaded, exposes a TRANSMIT CLONE
//      button that increments the counter by 1 with the first available
//      key and TX's the clone via SUBGHZ.
// If parse fails, shows a "Not a KeeLoq signal" toast in the status line.
static KeeLoq::Frame fp_kl_frame;
static char          fp_kl_bits[67];
static std::vector<KeeLoqClone::KeyEntry> fp_kl_keys;
static int           fp_kl_key_idx = 0;
static uint16_t      fp_kl_counter_inc = 1;
static lv_obj_t     *fp_kl_popup = NULL;
static lv_obj_t     *fp_kl_lblKey = NULL;
static lv_obj_t     *fp_kl_lblInc = NULL;

static void fp_kl_close_cb(lv_event_t *e) {
    if (fp_kl_popup) { lv_obj_del(fp_kl_popup); fp_kl_popup = NULL; }
}

static void fp_kl_next_key_cb(lv_event_t *e) {
    if (fp_kl_keys.empty()) return;
    fp_kl_key_idx = (fp_kl_key_idx + 1) % (int)fp_kl_keys.size();
    if (fp_kl_lblKey) {
        char b[96];
        snprintf(b, sizeof(b), "Key: %s", fp_kl_keys[fp_kl_key_idx].name.c_str());
        lv_label_set_text(fp_kl_lblKey, b);
    }
}

static void fp_kl_inc_plus_cb(lv_event_t *e) {
    fp_kl_counter_inc = (fp_kl_counter_inc + 1) % 100;
    if (fp_kl_counter_inc == 0) fp_kl_counter_inc = 1;
    if (fp_kl_lblInc) {
        char b[32];
        snprintf(b, sizeof(b), "+%u", (unsigned)fp_kl_counter_inc);
        lv_label_set_text(fp_kl_lblInc, b);
    }
}

static void fp_kl_tx_cb(lv_event_t *e) {
    if (fp_kl_keys.empty()) {
        fp_status_set("No KeeLoq keys available", 0xFF6666);
        return;
    }
    int freq_idx = fp_ddFreq ? lv_dropdown_get_selected(fp_ddFreq) : FP_FREQ_DEFAULT_IDX;
    if (freq_idx < 0 ||
        freq_idx >= (int)(sizeof(FP_FREQ_TABLE)/sizeof(FP_FREQ_TABLE[0]))) {
        freq_idx = FP_FREQ_DEFAULT_IDX;
    }
    float freq = FP_FREQ_TABLE[freq_idx];

    uint64_t key = fp_kl_keys[fp_kl_key_idx].key;
    bool ok = KeeLoqClone::transmitKeeLoqClone(fp_kl_frame, key,
                                                fp_kl_counter_inc, freq);
    if (ok) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "Clone queued: ctr %u -> %u, key=%s @ %.2f MHz",
                 (unsigned)KeeLoqClone::lastCloneOldCounter,
                 (unsigned)KeeLoqClone::lastCloneNewCounter,
                 fp_kl_keys[fp_kl_key_idx].name.c_str(),
                 freq);
        fp_status_set(buf, 0x00FF88);
    } else {
        fp_status_set("Clone TX failed", 0xFF6666);
    }
    if (fp_kl_popup) { lv_obj_del(fp_kl_popup); fp_kl_popup = NULL; }
}

static void fp_kl_show_popup() {
    if (fp_kl_popup) { lv_obj_del(fp_kl_popup); fp_kl_popup = NULL; }

    fp_kl_popup = lv_obj_create(ui_scrPresets);
    lv_obj_set_size(fp_kl_popup, 308, 380);
    lv_obj_align(fp_kl_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(fp_kl_popup, lv_color_hex(0x100020), LV_PART_MAIN);
    lv_obj_set_style_border_color(fp_kl_popup, lv_color_hex(0xA040FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(fp_kl_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(fp_kl_popup, 8, LV_PART_MAIN);
    lv_obj_clear_flag(fp_kl_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(fp_kl_popup);
    lv_label_set_text(t, "KEELOQ FRAME");
    lv_obj_set_style_text_color(t, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(t, &ui_font_Verdana18, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

    char info[256];
    snprintf(info, sizeof(info),
             "Serial:     0x%07lX\n"
             "Button:     %u\n"
             "Status:     %u\n"
             "Encrypted:  0x%08lX",
             (unsigned long)(fp_kl_frame.serial & 0x0FFFFFFFu),
             (unsigned)fp_kl_frame.button,
             (unsigned)fp_kl_frame.status,
             (unsigned long)fp_kl_frame.encrypted);
    lv_obj_t *body = lv_label_create(fp_kl_popup);
    lv_obj_set_pos(body, 14, 38);
    lv_label_set_text(body, info);
    lv_obj_set_style_text_color(body, lv_color_hex(0xCCEEFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(body, &ui_font_Verdana14, LV_PART_MAIN);

    // Key picker
    fp_kl_lblKey = lv_label_create(fp_kl_popup);
    lv_obj_set_pos(fp_kl_lblKey, 14, 140);
    lv_obj_set_width(fp_kl_lblKey, 280);
    lv_obj_set_style_text_color(fp_kl_lblKey, lv_color_hex(0xFFCC44), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_kl_lblKey, &ui_font_Verdana12, LV_PART_MAIN);
    lv_label_set_long_mode(fp_kl_lblKey, LV_LABEL_LONG_WRAP);
    if (fp_kl_keys.empty()) {
        lv_label_set_text(fp_kl_lblKey, "Key: (none — connect WiFi & retry)");
    } else {
        char b[96];
        snprintf(b, sizeof(b), "Key: %s", fp_kl_keys[fp_kl_key_idx].name.c_str());
        lv_label_set_text(fp_kl_lblKey, b);
    }

    lv_obj_t *btnNextKey = fp_mk_btn(fp_kl_popup, 14, 170, 140, 32,
                                      "NEXT KEY", 0x004080);
    lv_obj_add_event_cb(btnNextKey, fp_kl_next_key_cb, LV_EVENT_CLICKED, NULL);

    // Counter increment
    fp_kl_lblInc = lv_label_create(fp_kl_popup);
    lv_obj_set_pos(fp_kl_lblInc, 170, 178);
    {
        char b[32]; snprintf(b, sizeof(b), "+%u", (unsigned)fp_kl_counter_inc);
        lv_label_set_text(fp_kl_lblInc, b);
    }
    lv_obj_set_style_text_color(fp_kl_lblInc, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_kl_lblInc, &ui_font_Verdana14, LV_PART_MAIN);

    lv_obj_t *btnIncPlus = fp_mk_btn(fp_kl_popup, 210, 170, 80, 32,
                                      "+1", 0x006633);
    lv_obj_add_event_cb(btnIncPlus, fp_kl_inc_plus_cb, LV_EVENT_CLICKED, NULL);

    // TX / CANCEL
    lv_obj_t *btnTx = fp_mk_btn(fp_kl_popup, 14, 220, 140, 44,
                                 "TRANSMIT", 0x661111);
    lv_obj_add_event_cb(btnTx, fp_kl_tx_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btnCx = fp_mk_btn(fp_kl_popup, 170, 220, 120, 44,
                                 "CANCEL", 0x444444);
    lv_obj_add_event_cb(btnCx, fp_kl_close_cb, LV_EVENT_CLICKED, NULL);

    // Bits preview (truncated)
    lv_obj_t *bits = lv_label_create(fp_kl_popup);
    lv_obj_set_pos(bits, 14, 278);
    lv_obj_set_width(bits, 280);
    lv_label_set_long_mode(bits, LV_LABEL_LONG_WRAP);
    char preview[80];
    snprintf(preview, sizeof(preview), "Bits: %.66s", fp_kl_bits);
    lv_label_set_text(bits, preview);
    lv_obj_set_style_text_color(bits, lv_color_hex(0x88AABB), LV_PART_MAIN);
    lv_obj_set_style_text_font(bits, &ui_font_Verdana12, LV_PART_MAIN);
}

static void fp_keeloq_run() {
    if (!ui_ddPresetsFolder || !ui_ddPresetsFile) return;

    char folder[64];
    char file[96];
    lv_dropdown_get_selected_str(ui_ddPresetsFolder, folder, sizeof(folder));
    lv_dropdown_get_selected_str(ui_ddPresetsFile,   file,   sizeof(file));

    if (file[0] == '\0' || strcmp(file, "(none)") == 0) {
        fp_status_set("KeeLoq: no file selected", 0xFFCC44);
        return;
    }

    char fullpath[200];
    if (strcmp(folder, "/") == 0 || folder[0] == '\0') {
        snprintf(fullpath, sizeof(fullpath), "/%s", file);
    } else {
        snprintf(fullpath, sizeof(fullpath), "/%s/%s", folder, file);
    }

    if (!KeeLoqClone::parseSubFileAsKeeLoq(fullpath, fp_kl_frame, fp_kl_bits)) {
        fp_status_set("Not a KeeLoq signal", 0xFF6666);
        return;
    }

    // Lazy-load keys once per session (or once per popup open if empty).
    if (fp_kl_keys.empty()) {
        fp_kl_keys = KeeLoqClone::fetchKeeLoqKeys();
        fp_kl_key_idx = 0;
    }

    char status[160];
    snprintf(status, sizeof(status),
             "KeeLoq: ser=0x%07lX btn=%u enc=0x%08lX",
             (unsigned long)(fp_kl_frame.serial & 0x0FFFFFFFu),
             (unsigned)fp_kl_frame.button,
             (unsigned long)fp_kl_frame.encrypted);
    fp_status_set(status, 0x00FF88);

    fp_kl_show_popup();
}

// -------------------- SD browsing helpers --------------------
// Rescan the file dropdown for *.sub files inside `folder` (path
// fragment without leading slash, or "" for root). Wraps the call in
// the project's sd_card_is_present / now_close_sd_card arbiter so the
// CC1101 SPI bus doesn't collide with the SD reader — this was the
// reason file lists weren't refreshing on folder change.
static void fp_refresh_files(const char *folder) {
    if (!ui_ddPresetsFile) return;
    extern bool sd_card_is_present();
    extern void now_close_sd_card();
    extern void refresh_sd_card_file(lv_obj_t *dd, const char *dir,
                                      const char *ext, bool clear);

    lv_dropdown_clear_options(ui_ddPresetsFile);
    lv_dropdown_set_options_static(ui_ddPresetsFile, "");

    if (!sd_card_is_present()) {
        lv_dropdown_set_options(ui_ddPresetsFile, "(no SD)");
        return;
    }

    char path[96];
    if (!folder || folder[0] == '\0' || strcmp(folder, "/") == 0) {
        snprintf(path, sizeof(path), "/");
    } else {
        snprintf(path, sizeof(path), "/%s", folder);
    }
    refresh_sd_card_file(ui_ddPresetsFile, path, ".sub", true);

    if (lv_dropdown_get_option_cnt(ui_ddPresetsFile) == 0) {
        lv_dropdown_set_options(ui_ddPresetsFile, "(no .sub files)");
    }
    lv_dropdown_set_selected(ui_ddPresetsFile, 0);
    now_close_sd_card();
}

// Rescan the folder dropdown by enumerating real directories at /.
// Always includes "/" at index 0 so the user can return to root. Falls
// back to a hard-coded "/" + "subghz" + "captures" set if SD is absent.
static void fp_refresh_folders() {
    if (!ui_ddPresetsFolder) return;
    extern bool sd_card_is_present();
    extern void now_close_sd_card();

    char buf[512];
    size_t len = 0;
    auto append = [&](const char *s) {
        size_t n = strlen(s);
        if (len + n + 2 >= sizeof(buf)) return;
        if (len) buf[len++] = '\n';
        memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    };

    append("/");
    if (sd_card_is_present()) {
        File root = SD.open("/");
        if (root && root.isDirectory()) {
            for (File f = root.openNextFile(); f; f = root.openNextFile()) {
                if (f.isDirectory()) {
                    const char *name = sd_basename(f.name());
                    if (name && name[0] && strcmp(name, "/") != 0 && name[0] != '.') {
                        append(name);
                    }
                }
                f.close();
            }
        }
        if (root) root.close();
        now_close_sd_card();
    } else {
        append("subghz");
        append("captures");
    }

    lv_dropdown_set_options(ui_ddPresetsFolder, buf);
    lv_dropdown_set_selected(ui_ddPresetsFolder, 0);
}

// -------------------- Build the screen --------------------
static bool fp_built = false;
static void fp_screen_build() {
    if (fp_built) return;
    if (!ui_scrPresets) return;

    lv_obj_clean(ui_scrPresets);
    ui_lblPresetsTitle  = NULL;
    ui_lblPresetsFolder = NULL;
    ui_lblPresetsFile   = NULL;
    ui_btnPresetsBack   = NULL;
    ui_lblPresetsBack   = NULL;
    // NOTE: ui_lblPresetsStatus is NOT nulled — event_send_flipper_file()
    // and the STATE_SEND_FLIPPER state machine still write to it. We
    // re-point it at our fp_lblStatus widget below after that's created,
    // so all the existing handlers transparently update the new status
    // line instead of NULL-derefing into a reboot.
    ui_ddPresetsFolder  = NULL;
    ui_ddPresetsFile    = NULL;
    ui_btnPresetTx      = NULL;
    ui_lblPresetTx      = NULL;
    ui_btnPresetTesla   = NULL;
    ui_lblPresetTesla   = NULL;
    ui_btnPresetDelete  = NULL;
    ui_lblPresetDelete  = NULL;

    lv_obj_set_style_pad_all(ui_scrPresets, 0, 0);
    lv_obj_clear_flag(ui_scrPresets, LV_OBJ_FLAG_SCROLLABLE);

    // Title — kept clear of the status bar zone (y<22)
    lv_obj_t *title = fp_mk_lbl(ui_scrPresets, 0, 28, "FLIPPER PLAYER",
                                 0xFF9100, &ui_font_Verdana18);
    lv_obj_set_width(title, 320);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // ---- Folder dropdown ----
    fp_mk_lbl(ui_scrPresets, 12, 64, "Folder", 0xCCCCCC, &ui_font_Verdana14);
    ui_ddPresetsFolder = lv_dropdown_create(ui_scrPresets);
    lv_dropdown_set_symbol(ui_ddPresetsFolder, NULL);
    lv_obj_set_size(ui_ddPresetsFolder, 220, 30);
    lv_obj_set_pos(ui_ddPresetsFolder, 90, 60);
    lv_obj_set_style_text_font(ui_ddPresetsFolder, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_ddPresetsFolder, lv_color_hex(0x121226), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_ddPresetsFolder, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_ddPresetsFolder, lv_color_hex(0x335577), LV_PART_MAIN);
    lv_dropdown_set_options(ui_ddPresetsFolder, "/\nsubghz\ncaptures");

    // ---- File dropdown ----
    fp_mk_lbl(ui_scrPresets, 12, 102, "File", 0xCCCCCC, &ui_font_Verdana14);
    ui_ddPresetsFile = lv_dropdown_create(ui_scrPresets);
    lv_dropdown_set_symbol(ui_ddPresetsFile, NULL);
    lv_obj_set_size(ui_ddPresetsFile, 220, 30);
    lv_obj_set_pos(ui_ddPresetsFile, 90, 98);
    lv_obj_set_style_text_font(ui_ddPresetsFile, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_ddPresetsFile, lv_color_hex(0x121226), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_ddPresetsFile, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_ddPresetsFile, lv_color_hex(0x335577), LV_PART_MAIN);
    lv_dropdown_set_options(ui_ddPresetsFile, "(none)");
    lv_obj_add_event_cb(ui_ddPresetsFolder, [](lv_event_t *e) {
        // Selection changed → reload the file list for the new folder.
        // Goes through fp_refresh_files() which arbitrates the SD bus.
        char folder[64];
        lv_dropdown_get_selected_str((lv_obj_t *)lv_event_get_target(e),
                                      folder, sizeof(folder));
        fp_refresh_files(folder);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ---- Preset + Freq dropdown row ----
    // Place ABOVE the action button row so the user picks modulation/freq
    // before pressing READ RAW.
    fp_mk_lbl(ui_scrPresets, 12, 140, "Mod", 0xCCCCCC, &ui_font_Verdana14);
    fp_ddPreset = lv_dropdown_create(ui_scrPresets);
    lv_dropdown_set_symbol(fp_ddPreset, NULL);
    lv_obj_set_size(fp_ddPreset, 110, 30);
    lv_obj_set_pos(fp_ddPreset, 50, 136);
    lv_obj_set_style_text_font(fp_ddPreset, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fp_ddPreset, lv_color_hex(0x121226), LV_PART_MAIN);
    lv_obj_set_style_text_color(fp_ddPreset, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(fp_ddPreset, lv_color_hex(0x335577), LV_PART_MAIN);
    lv_dropdown_set_options(fp_ddPreset, "OOK 270\nOOK 650\nFSK 238\nFSK 476");
    lv_dropdown_set_selected(fp_ddPreset, 1);  // default OOK 650

    fp_mk_lbl(ui_scrPresets, 168, 140, "Freq", 0xCCCCCC, &ui_font_Verdana14);
    fp_ddFreq = lv_dropdown_create(ui_scrPresets);
    lv_dropdown_set_symbol(fp_ddFreq, NULL);
    lv_obj_set_size(fp_ddFreq, 100, 30);
    lv_obj_set_pos(fp_ddFreq, 212, 136);
    lv_obj_set_style_text_font(fp_ddFreq, &ui_font_Verdana12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fp_ddFreq, lv_color_hex(0x121226), LV_PART_MAIN);
    lv_obj_set_style_text_color(fp_ddFreq, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(fp_ddFreq, lv_color_hex(0x335577), LV_PART_MAIN);
    lv_dropdown_set_options(fp_ddFreq,
        "300.00\n303.87\n315.00\n390.00\n418.00\n"
        "433.07\n433.92\n434.42\n868.35\n915.00");
    lv_dropdown_set_selected(fp_ddFreq, FP_FREQ_DEFAULT_IDX);  // default 433.92

    // ---- PLAY / STOP / READ RAW / DECODE row ----
    fp_btnPlay   = fp_mk_btn(ui_scrPresets, 4,   176, 74, 40, "PLAY",     0x006633);
    fp_btnStop   = fp_mk_btn(ui_scrPresets, 82,  176, 74, 40, "STOP",     0x661111);
    fp_btnRaw    = fp_mk_btn(ui_scrPresets, 160, 176, 78, 40, "READ RAW", 0x8B5A00);
    fp_btnDecode = fp_mk_btn(ui_scrPresets, 242, 176, 74, 40, "DECODE",   0x004080);

    // PLAY → re-uses the existing event_send_flipper_file()
    lv_obj_add_event_cb(fp_btnPlay, [](lv_event_t *e) {
        fp_status_set("Loading...", 0xFFCC44);
        event_send_flipper_file(e);
    }, LV_EVENT_CLICKED, NULL);

    // STOP → cancel any active TX or raw capture
    lv_obj_add_event_cb(fp_btnStop, [](lv_event_t *e) {
        extern uint8_t currentState;
        if (fp_subghz_raw_running()) {
            fp_subghz_raw_stop();
            if (fp_rawTimer) {
                lv_timer_del(fp_rawTimer);
                fp_rawTimer = NULL;
            }
            fp_rec_overlay_show(false);
            int cnt = fp_subghz_raw_count();
            char msg[120];
            snprintf(msg, sizeof(msg), "Stopped at %d edges → %s",
                     cnt, fp_rawFilename);
            fp_status_set(msg, 0xFFCC44);
            return;
        }
        currentState = STATE_IDLE;
        fp_status_set("Stopped", 0xFFCC44);
    }, LV_EVENT_CLICKED, NULL);

    // READ RAW → start CC1101 OOK/FSK async capture using user-selected
    // frequency + modulation preset from the dropdowns above.
    lv_obj_add_event_cb(fp_btnRaw, [](lv_event_t *e) {
        if (fp_subghz_raw_running()) {
            fp_status_set("Already capturing - hit STOP first", 0xFFCC44);
            return;
        }
        // Read user-selected freq + preset from the dropdowns
        int freq_idx = fp_ddFreq ? lv_dropdown_get_selected(fp_ddFreq)
                                  : FP_FREQ_DEFAULT_IDX;
        if (freq_idx < 0 ||
            freq_idx >= (int)(sizeof(FP_FREQ_TABLE)/sizeof(FP_FREQ_TABLE[0]))) {
            freq_idx = FP_FREQ_DEFAULT_IDX;
        }
        float freq = FP_FREQ_TABLE[freq_idx];

        int preset_idx = fp_ddPreset ? lv_dropdown_get_selected(fp_ddPreset)
                                      : FP_PRESET_DEFAULT_IDX;
        int preset_enum = fp_preset_idx_to_enum(preset_idx);
        const char *preset_label = fp_subghz_preset_name(preset_enum);
        if (!preset_label) preset_label = "?";

        // Filename = /captures/raw_<millis>.sub
        snprintf(fp_rawFilename, sizeof(fp_rawFilename),
                 "/captures/raw_%lu.sub", (unsigned long)millis());
        if (!fp_subghz_raw_start_preset(freq, fp_rawFilename, preset_enum)) {
            fp_status_set("Failed to start capture (stop scanner first?)",
                          0xFF6666);
            return;
        }
        if (fp_lblCapture) lv_label_set_text(fp_lblCapture, "Captured: 0 edges");
        if (fp_recCount) lv_label_set_text(fp_recCount, "0");
        fp_rec_overlay_show(true);
        fp_recPulsePhase = 0;
        char buf[96];
        snprintf(buf, sizeof(buf), "Capturing at %.2f MHz / %s...",
                 freq, preset_label);
        fp_status_set(buf, 0x00DDFF);
        if (fp_rawTimer) lv_timer_del(fp_rawTimer);
        fp_rawTimer = lv_timer_create(fp_raw_timer_cb, 200, NULL);
    }, LV_EVENT_CLICKED, NULL);

    // DECODE → read selected .sub, extract RAW_Data, run BinRaw::analyze,
    // show results in a modal popup.
    lv_obj_add_event_cb(fp_btnDecode, [](lv_event_t *e) {
        fp_decode_run();
    }, LV_EVENT_CLICKED, NULL);

    // ---- Status line ----
    fp_lblStatus = lv_label_create(ui_scrPresets);
    lv_obj_set_width(fp_lblStatus, 300);
    lv_obj_set_pos(fp_lblStatus, 10, 226);
    lv_label_set_text(fp_lblStatus, "Ready");
    lv_obj_set_style_text_color(fp_lblStatus, lv_color_hex(0x00DDFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_lblStatus, &ui_font_Verdana14, LV_PART_MAIN);
    lv_label_set_long_mode(fp_lblStatus, LV_LABEL_LONG_WRAP);

    // Repoint the SquareLine status global so the existing TX handlers
    // and STATE_SEND_FLIPPER state machine all write to OUR status line
    // (previously they would NULL-deref into a reboot after rebuild).
    ui_lblPresetsStatus = fp_lblStatus;

    // ---- Live capture-count label (only meaningful while capturing) ----
    fp_lblCapture = lv_label_create(ui_scrPresets);
    lv_obj_set_pos(fp_lblCapture, 10, 256);
    lv_label_set_text(fp_lblCapture, "");
    lv_obj_set_style_text_color(fp_lblCapture, lv_color_hex(0xAACCFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_lblCapture, &ui_font_Verdana14, LV_PART_MAIN);

    // ---- Real-time capture indicator (modeled after the scanner's
    //      live FFT bars). Hidden by default, shown only during an
    //      active raw capture. The bar fills 0 → 4096 edges as the
    //      buffer fills; the big right-justified number is the live
    //      transition count that ticks up with each captured edge.
    fp_recDot = lv_bar_create(ui_scrPresets);          // (reused handle as the buffer-fill bar)
    lv_obj_set_size(fp_recDot, 300, 18);
    lv_obj_set_pos(fp_recDot, 10, 350);
    lv_bar_set_range(fp_recDot, 0, 4096);
    lv_bar_set_value(fp_recDot, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fp_recDot, lv_color_hex(0x121226), LV_PART_MAIN);
    lv_obj_set_style_bg_color(fp_recDot, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
    lv_obj_set_style_radius(fp_recDot, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(fp_recDot, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(fp_recDot, LV_OBJ_FLAG_HIDDEN);

    fp_recLbl = lv_label_create(ui_scrPresets);
    lv_obj_set_pos(fp_recLbl, 10, 376);
    lv_label_set_text(fp_recLbl, "● CAPTURING");
    lv_obj_set_style_text_color(fp_recLbl, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_recLbl, &ui_font_Verdana14, LV_PART_MAIN);
    lv_obj_add_flag(fp_recLbl, LV_OBJ_FLAG_HIDDEN);

    fp_recCount = lv_label_create(ui_scrPresets);
    lv_obj_set_width(fp_recCount, 160);
    lv_obj_set_pos(fp_recCount, 150, 372);
    lv_label_set_text(fp_recCount, "0 edges");
    lv_obj_set_style_text_color(fp_recCount, lv_color_hex(0x00FFAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(fp_recCount, &ui_font_Verdana18, LV_PART_MAIN);
    lv_obj_set_style_text_align(fp_recCount, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_add_flag(fp_recCount, LV_OBJ_FLAG_HIDDEN);

    // ---- Help text ----
    lv_obj_t *help = fp_mk_lbl(ui_scrPresets, 10, 296,
        "Tip: READ RAW captures the next button press\non a remote, saves it as a .sub you can re-PLAY.",
        0x888888, &ui_font_Verdana12);
    lv_obj_set_width(help, 300);
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);

    // ---- KEELOQ + BACK buttons ----
    // KEELOQ → parse selected .sub as a KeeLoq frame, popup with the
    // decoded fields. If a manufacturer key is cached/available, expose
    // a TRANSMIT CLONE action in the popup.
    lv_obj_t *btnKeeLoq = fp_mk_btn(ui_scrPresets, 10, 420, 130, 40,
                                    "KEELOQ CLONE", 0x8000A0);
    lv_obj_add_event_cb(btnKeeLoq, [](lv_event_t *e) {
        fp_keeloq_run();
    }, LV_EVENT_CLICKED, NULL);

    ui_btnPresetsBack = fp_mk_btn(ui_scrPresets, 220, 420, 90, 40, "BACK", 0x333355);
    lv_obj_add_event_cb(ui_btnPresetsBack, [](lv_event_t *e) {
        lv_scr_load(ui_scrMain);
    }, LV_EVENT_CLICKED, NULL);

    // Populate the initial file list (root folder)
    {
        extern void refresh_sd_card_file(lv_obj_t *dd, const char *folder,
                                          const char *ext, bool include_root);
        // Initial population — pull real folders + .sub files via the
        // SD-bus-arbitrated helpers.
        fp_refresh_folders();
        fp_refresh_files("/");
    }

    // Re-scan SD on every screen entry — captures the user just made
    // from READ RAW or new files dropped onto the card via the SD slot
    // show up without a reboot.
    lv_obj_add_event_cb(ui_scrPresets, [](lv_event_t *e) {
        fp_refresh_folders();
        char folder[64];
        lv_dropdown_get_selected_str(ui_ddPresetsFolder, folder, sizeof(folder));
        fp_refresh_files(folder);
    }, LV_EVENT_SCREEN_LOADED, NULL);

    fp_built = true;
}

#endif
