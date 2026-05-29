#ifndef ScannerScreen_h
#define ScannerScreen_h

#include <lvgl.h>
#include <ui.h>
#include <driver/i2s.h>
#include "Audio/ToneService.h"

// Mirrors WaveSentinelState in Display/Event.h (which is not header-safe).
// IDLE=0, GENERATOR=1, ANALYZER=2, SCANNER=3
#define SCN_STATE_IDLE    0
#define SCN_STATE_SCANNER 3

// 44.1 kHz so BCLK is above the DAC's lock threshold (16 kHz was too low)
#define SCN_TONE_SAMPLE_RATE 44100
#define SCN_TONE_CHUNK       1024    // ~23ms at 44.1kHz
#define SCN_TONE_AMPLITUDE   5000    // 16-bit signed; ~15% of full scale
#define SCN_TONE_FREQ_LOW    300
#define SCN_TONE_FREQ_HIGH   2200

// ============================================================
// Flipper-style Scanner Screen
// Wipes the SquareLine Scanner tab and rebuilds it as a
// freq-preset FFT spectrum analyzer.
// ============================================================

#define FFT_BINS 48
#define FFT_MAX_HITS 24
#define FFT_BAR_H 80
#define FFT_BAR_MIN 8         // baseline pixel height so noise floor is visible
#define FFT_REFRESH_EVERY 2   // repaint every Nth sweep
#define FFT_HITS_SHOW 8
#define FFT_SNAP_TOLERANCE 1.5f   // MHz — snap measured freq to a preset within this
#define FFT_HIT_DEBOUNCE_MS 1200  // gap before a continuing signal counts as a new press

struct ScnFreqPreset { const char *label; float center; float span; };
static const ScnFreqPreset SCN_FREQS[] = {
    { "315.00 MHz",  315.00f, 10.0f },
    { "318.00 MHz",  318.00f, 10.0f },
    { "390.00 MHz",  390.00f, 10.0f },
    { "418.00 MHz",  418.00f, 10.0f },
    { "433.92 MHz",  433.92f, 10.0f },
    { "868.35 MHz",  868.35f, 10.0f },
    { "915.00 MHz",  915.00f, 10.0f },
    { "300-348 MHz", 324.0f,  48.0f },
    { "387-464 MHz", 425.5f,  77.0f },
    { "779-928 MHz", 853.5f, 149.0f },
};
#define SCN_FREQ_COUNT (sizeof(SCN_FREQS)/sizeof(SCN_FREQS[0]))
#define SCN_FREQ_DEFAULT 4   // 433.92 MHz

static struct ScannerState {
    bool initialized;
    bool running;
    uint32_t sweep_count;
    int freq_idx;

    lv_obj_t *fft_box;
    lv_obj_t *bars[FFT_BINS];
    uint32_t bar_color_cache[FFT_BINS];   // skip set_style if unchanged
    lv_obj_t *lbl_peak;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_hits;
    lv_obj_t *sld_thresh;
    lv_obj_t *lbl_thresh_val;
    lv_obj_t *btn_scan;
    lv_obj_t *lbl_btn_scan;
    lv_obj_t *cb_speaker;
    lv_obj_t *ddl_freq;

    int8_t rssi[FFT_BINS];
    float start_freq, stop_freq;
    float peak_freq;
    int8_t peak_rssi;

    struct { float freq; int count; int8_t rssi; uint32_t last_ms; } hits[FFT_MAX_HITS];
    int hit_count;
} scn = {};

// ============================================================
// Geiger-style speaker feedback — pitch tracks signal strength.
// Writes square-wave PCM directly to I2S0, which the Audio
// library has already configured via setPinout(). Audio library
// is dormant outside STATE_AUDIO_TEST, so we won't collide.
// ============================================================
static bool scn_tone_armed = false;

static void scn_speaker_update(int peak_rssi, int8_t thresh) {
    bool snd_on = scn.cb_speaker && (lv_obj_get_state(scn.cb_speaker) & LV_STATE_CHECKED);
    bool over = peak_rssi > thresh;
    bool want_tone = scn.running && snd_on && over;

    // Yield the speaker to event tones (boot chime, scanner hit, etc.)
    if (tone_is_playing()) return;

    if (!want_tone) {
        if (scn_tone_armed) {
            i2s_zero_dma_buffer(I2S_NUM_0);
            scn_tone_armed = false;
        }
        return;
    }

    if (!scn_tone_armed) {
        i2s_set_sample_rates(I2S_NUM_0, SCN_TONE_SAMPLE_RATE);
        scn_tone_armed = true;
    }

    int freq = map(peak_rssi, thresh, -30, SCN_TONE_FREQ_LOW, SCN_TONE_FREQ_HIGH);
    freq = constrain(freq, SCN_TONE_FREQ_LOW, SCN_TONE_FREQ_HIGH);

    int half = (SCN_TONE_SAMPLE_RATE / freq) / 2;
    if (half < 2) half = 2;
    if (half > 100) half = 100;

    static int16_t buf[SCN_TONE_CHUNK * 2];   // stereo interleaved
    static int phase = 0;
    for (int i = 0; i < SCN_TONE_CHUNK; i++) {
        int16_t s = (phase < half) ? SCN_TONE_AMPLITUDE : -SCN_TONE_AMPLITUDE;
        buf[i * 2]     = s;
        buf[i * 2 + 1] = s;
        if (++phase >= half * 2) phase = 0;
    }
    size_t written;
    // Non-blocking-ish: short timeout so a stalled DMA doesn't hang the sweep.
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, pdMS_TO_TICKS(5));
}

static lv_color_t scn_bar_color(int8_t rssi, int8_t thresh) {
    if (rssi > thresh) return lv_color_make(255, 60, 100);
    int v = constrain(map(rssi, -110, -30, 0, 255), 0, 255);
    if (v < 80)  return lv_color_make(20, 20, 80 + v);
    if (v < 160) return lv_color_make(0, (v-80)*2, 200);
    return lv_color_make(0, 200 + (v-160)/2, 200 - (v-160));
}

// Snap measured freq to the nearest single-freq preset (entries with span 10 MHz)
// so a remote that drifts a hair off (e.g. 433.78) lands on the canonical 433.92.
static float scn_snap_to_preset(float freq) {
    float best = freq;
    float best_d = FFT_SNAP_TOLERANCE;
    for (size_t i = 0; i < SCN_FREQ_COUNT; i++) {
        if (SCN_FREQS[i].span > 20.0f) continue;   // skip wide-band presets
        float d = fabsf(freq - SCN_FREQS[i].center);
        if (d < best_d) { best_d = d; best = SCN_FREQS[i].center; }
    }
    return best;
}

static void scn_record_hit(float freq, int8_t rssi) {
    float snapped = scn_snap_to_preset(freq);
    // 50 kHz bucket — keeps near-duplicates from drifting off-preset measurements unique
    float r = roundf(snapped * 20.0f) / 20.0f;
    uint32_t now = millis();
    for (int i = 0; i < scn.hit_count; i++) {
        if (fabsf(scn.hits[i].freq - r) < 0.06f) {
            // Same bucket: only increment if we've seen a gap, otherwise just
            // refresh the live RSSI / last-seen timestamp.
            if (now - scn.hits[i].last_ms >= FFT_HIT_DEBOUNCE_MS) {
                scn.hits[i].count++;
            }
            scn.hits[i].last_ms = now;
            if (rssi > scn.hits[i].rssi) scn.hits[i].rssi = rssi;
            return;
        }
    }
    if (scn.hit_count < FFT_MAX_HITS) {
        scn.hits[scn.hit_count++] = { r, 1, rssi, now };
        tone_play(&TONE_SCANNER_HIT);   // chirp on first detection of this freq
    }
}

static void scn_refresh_hit_list() {
    if (!scn.lbl_hits) return;
    for (int i = 0; i < scn.hit_count - 1; i++)
        for (int j = i + 1; j < scn.hit_count; j++)
            if (scn.hits[j].count > scn.hits[i].count) {
                auto t = scn.hits[i]; scn.hits[i] = scn.hits[j]; scn.hits[j] = t;
            }
    char buf[400] = "";
    int p = 0, show = min(scn.hit_count, FFT_HITS_SHOW);
    for (int i = 0; i < show; i++)
        p += snprintf(buf + p, sizeof(buf) - p, " %7.2f MHz   %4d dBm   x%d\n",
            scn.hits[i].freq, scn.hits[i].rssi, scn.hits[i].count);
    lv_label_set_text(scn.lbl_hits, scn.hit_count ? buf : " No signals yet");
}

extern SubGhz SUBGHZ;

static void scn_apply_freq_preset(int idx) {
    if (idx < 0 || idx >= (int)SCN_FREQ_COUNT) return;
    scn.freq_idx = idx;
    float c = SCN_FREQS[idx].center;
    float s = SCN_FREQS[idx].span;
    scn.start_freq = c - s / 2.0f;
    scn.stop_freq  = c + s / 2.0f;
    scn.hit_count = 0;
    scn_refresh_hit_list();
    // If the scanner is already running, push the new window into the
    // CC1101 driver immediately — otherwise the change wouldn't take
    // effect until the user toggled SCAN off/on.
    if (scn.running) {
        SUBGHZ.enableScanner(scn.start_freq, scn.stop_freq);
    }
}

// Wipe every SquareLine pointer that lived inside the Scanner tab.
// After lv_obj_clean(ui_Scanner) these objects are freed; null the
// extern globals so any stale handler can null-check before touching.
static void scn_null_squareline_globals() {
    ui_txtScannerData     = NULL;
    ui_txtScanStartFq     = NULL;
    ui_txtScanStopFq      = NULL;
    ui_lblScanStartFq     = NULL;
    ui_lblScanStopFq      = NULL;
    ui_swScannerOn        = NULL;
    ui_lblScanEnable      = NULL;
    ui_btnScannerClear    = NULL;
    ui_lblScannerClear    = NULL;
    ui_sldThreshold       = NULL;
    ui_lblSldThreshold    = NULL;
    ui_lblThreshold       = NULL;
    ui_ddl1101ScanPreset  = NULL;
    ui_lbl1101ScanPreset  = NULL;
    ui_chkScanPresets     = NULL;
}

// ============================================================
// scanner_init() — wipe Scanner tab, build new UI
// ============================================================
static void scanner_init() {
    if (scn.initialized) return;

    // Nuke every SquareLine widget on this tab; we use absolute positions
    lv_obj_clean(ui_Scanner);
    scn_null_squareline_globals();
    lv_obj_set_style_pad_all(ui_Scanner, 0, 0);
    lv_obj_clear_flag(ui_Scanner, LV_OBJ_FLAG_SCROLLABLE);

    scn_apply_freq_preset(SCN_FREQ_DEFAULT);

    // ===== Peak readout =====
    scn.lbl_peak = lv_label_create(ui_Scanner);
    lv_obj_set_width(scn.lbl_peak, 310);
    lv_obj_set_align(scn.lbl_peak, LV_ALIGN_TOP_MID);
    lv_obj_set_y(scn.lbl_peak, 2);
    lv_label_set_text(scn.lbl_peak, "READY");
    lv_obj_set_style_text_align(scn.lbl_peak, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scn.lbl_peak, lv_color_hex(0x00DDFF), 0);
    lv_obj_set_style_text_font(scn.lbl_peak, &lv_font_montserrat_20, 0);

    // ===== FFT bar box =====
    scn.fft_box = lv_obj_create(ui_Scanner);
    lv_obj_set_size(scn.fft_box, 310, FFT_BAR_H + 6);
    lv_obj_set_align(scn.fft_box, LV_ALIGN_TOP_MID);
    lv_obj_set_y(scn.fft_box, 28);
    lv_obj_set_style_bg_color(scn.fft_box, lv_color_hex(0x060612), 0);
    lv_obj_set_style_border_color(scn.fft_box, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(scn.fft_box, 1, 0);
    lv_obj_set_style_radius(scn.fft_box, 4, 0);
    lv_obj_set_style_pad_all(scn.fft_box, 2, 0);
    lv_obj_clear_flag(scn.fft_box, LV_OBJ_FLAG_SCROLLABLE);

    int bw = 306 / FFT_BINS;
    for (int i = 0; i < FFT_BINS; i++) {
        scn.bars[i] = lv_obj_create(scn.fft_box);
        lv_obj_set_size(scn.bars[i], max(bw - 1, 2), FFT_BAR_MIN);
        lv_obj_set_pos(scn.bars[i], i * bw, FFT_BAR_H - FFT_BAR_MIN);
        lv_obj_set_style_bg_color(scn.bars[i], lv_color_hex(0x223355), 0);
        lv_obj_set_style_bg_opa(scn.bars[i], 255, 0);
        lv_obj_set_style_radius(scn.bars[i], 0, 0);
        lv_obj_set_style_border_width(scn.bars[i], 0, 0);
        lv_obj_clear_flag(scn.bars[i], LV_OBJ_FLAG_SCROLLABLE);
        scn.bar_color_cache[i] = 0x223355;
        scn.rssi[i] = -128;
    }

    // ===== FREQ dropdown =====
    int dy = 28 + FFT_BAR_H + 14;

    lv_obj_t *lf = lv_label_create(ui_Scanner);
    lv_obj_set_pos(lf, 5, dy - 12);
    lv_label_set_text(lf, "FREQ");
    lv_obj_set_style_text_color(lf, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(lf, &lv_font_montserrat_10, 0);

    scn.ddl_freq = lv_dropdown_create(ui_Scanner);
    lv_dropdown_set_symbol(scn.ddl_freq, NULL);
    lv_obj_set_size(scn.ddl_freq, 310, 28);
    lv_obj_set_pos(scn.ddl_freq, 5, dy);
    {
        char buf[256] = "";
        int p = 0;
        for (size_t i = 0; i < SCN_FREQ_COUNT; i++)
            p += snprintf(buf + p, sizeof(buf) - p, "%s%s",
                i ? "\n" : "", SCN_FREQS[i].label);
        lv_dropdown_set_options(scn.ddl_freq, buf);
    }
    lv_dropdown_set_selected(scn.ddl_freq, SCN_FREQ_DEFAULT);
    lv_obj_set_style_bg_color(scn.ddl_freq, lv_color_hex(0x121226), 0);
    lv_obj_set_style_border_color(scn.ddl_freq, lv_color_hex(0x333355), 0);
    lv_obj_set_style_text_color(scn.ddl_freq, lv_color_hex(0x00DDFF), 0);
    lv_obj_set_style_text_font(scn.ddl_freq, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(scn.ddl_freq, [](lv_event_t *e) {
        scn_apply_freq_preset(lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e)));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ===== Scan + threshold + speaker row =====
    int cy = dy + 36;

    scn.btn_scan = lv_btn_create(ui_Scanner);
    lv_obj_set_size(scn.btn_scan, 90, 32);
    lv_obj_set_pos(scn.btn_scan, 5, cy);
    lv_obj_set_style_bg_color(scn.btn_scan, lv_color_hex(0x005533), 0);
    lv_obj_set_style_radius(scn.btn_scan, 4, 0);
    lv_obj_set_style_shadow_width(scn.btn_scan, 0, 0);
    scn.lbl_btn_scan = lv_label_create(scn.btn_scan);
    lv_obj_set_align(scn.lbl_btn_scan, LV_ALIGN_CENTER);
    lv_label_set_text(scn.lbl_btn_scan, ">" " SCAN");
    lv_obj_set_style_text_color(scn.lbl_btn_scan, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_text_font(scn.lbl_btn_scan, &lv_font_montserrat_12, 0);

    lv_obj_t *lt = lv_label_create(ui_Scanner);
    lv_obj_set_pos(lt, 102, cy - 2);
    lv_label_set_text(lt, "THRESH");
    lv_obj_set_style_text_color(lt, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_10, 0);

    scn.sld_thresh = lv_slider_create(ui_Scanner);
    lv_obj_set_size(scn.sld_thresh, 100, 10);
    lv_obj_set_pos(scn.sld_thresh, 102, cy + 14);
    lv_slider_set_range(scn.sld_thresh, -90, -30);
    lv_slider_set_value(scn.sld_thresh, -65, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(scn.sld_thresh, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(scn.sld_thresh, lv_color_hex(0xFF6600), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(scn.sld_thresh, lv_color_hex(0xFF9900), LV_PART_KNOB);
    lv_obj_set_style_pad_all(scn.sld_thresh, 2, LV_PART_KNOB);

    scn.lbl_thresh_val = lv_label_create(ui_Scanner);
    lv_obj_set_pos(scn.lbl_thresh_val, 208, cy + 8);
    lv_label_set_text(scn.lbl_thresh_val, "-65");
    lv_obj_set_style_text_color(scn.lbl_thresh_val, lv_color_hex(0xFF9100), 0);
    lv_obj_set_style_text_font(scn.lbl_thresh_val, &lv_font_montserrat_14, 0);

    scn.cb_speaker = lv_checkbox_create(ui_Scanner);
    lv_obj_set_pos(scn.cb_speaker, 245, cy + 4);
    lv_checkbox_set_text(scn.cb_speaker, "SND");
    lv_obj_set_style_text_color(scn.cb_speaker, lv_color_hex(0x778899), 0);
    lv_obj_set_style_text_font(scn.cb_speaker, &lv_font_montserrat_10, 0);

    // ===== Status line =====
    scn.lbl_status = lv_label_create(ui_Scanner);
    lv_obj_set_width(scn.lbl_status, 310);
    lv_obj_set_pos(scn.lbl_status, 5, cy + 36);
    lv_label_set_text(scn.lbl_status, "Pick a freq, press SCAN");
    lv_obj_set_style_text_color(scn.lbl_status, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(scn.lbl_status, &lv_font_montserrat_10, 0);

    // ===== Hit list =====
    lv_obj_t *lhdr = lv_label_create(ui_Scanner);
    lv_obj_set_pos(lhdr, 5, cy + 50);
    lv_label_set_text(lhdr, "DETECTED SIGNALS");
    lv_obj_set_style_text_color(lhdr, lv_color_hex(0xFF9100), 0);
    lv_obj_set_style_text_font(lhdr, &lv_font_montserrat_10, 0);

    scn.lbl_hits = lv_label_create(ui_Scanner);
    lv_obj_set_pos(scn.lbl_hits, 5, cy + 62);
    lv_obj_set_width(scn.lbl_hits, 310);
    lv_label_set_text(scn.lbl_hits, " No signals yet");
    lv_obj_set_style_text_color(scn.lbl_hits, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(scn.lbl_hits, &lv_font_montserrat_12, 0);

    // ===== Scan start/stop (toggle state + visuals synchronously) =====
    lv_obj_add_event_cb(scn.btn_scan, [](lv_event_t *e) {
        extern SubGhz SUBGHZ;
        extern uint8_t currentState;
        if (scn.running) {
            scn.running = false;
            currentState = SCN_STATE_IDLE;
            SUBGHZ.disableScanner();
            lv_label_set_text(scn.lbl_btn_scan, ">" " SCAN");
            lv_obj_set_style_bg_color(scn.btn_scan, lv_color_hex(0x005533), 0);
            lv_obj_set_style_text_color(scn.lbl_btn_scan, lv_color_hex(0x00FF88), 0);
            lv_label_set_text(scn.lbl_peak, "READY");
            lv_obj_set_style_text_color(scn.lbl_peak, lv_color_hex(0x00DDFF), 0);
        } else {
            // If a Read RAW capture was started from the Flipper Player
            // screen and never explicitly stopped (user backed out, or
            // auto-stop didn't fire because <8 edges were seen), its
            // pin-change ISR is still attached to GDO0. Tearing into
            // scanner mode with that ISR live races our SPI writes and
            // crashes the device. Stop it silently first.
            if (SUBGHZ.rawCaptureRunning()) {
                SUBGHZ.stopRawCapture();
            }
            scn.running = true;
            currentState = SCN_STATE_SCANNER;
            SUBGHZ.enableScanner(scn.start_freq, scn.stop_freq);
            lv_label_set_text(scn.lbl_btn_scan, "#" " STOP");
            lv_obj_set_style_bg_color(scn.btn_scan, lv_color_hex(0x550022), 0);
            lv_obj_set_style_text_color(scn.lbl_btn_scan, lv_color_hex(0xFF4466), 0);
        }
    }, LV_EVENT_CLICKED, NULL);

    // Long-press scan button = clear hit list
    lv_obj_add_event_cb(scn.btn_scan, [](lv_event_t *e) {
        scn.hit_count = 0;
        scn_refresh_hit_list();
    }, LV_EVENT_LONG_PRESSED, NULL);

    scn.initialized = true;
}

// ============================================================
// scanner_update_display() — called from ScannerLoop()
// Called under lvgl_mutex (held in main loop).
// Throttled + cached to keep render cost low.
// ============================================================
static void scanner_update_display() {
    if (!scn.initialized) return;
    if (scn.sweep_count % FFT_REFRESH_EVERY != 0) return;

    int8_t thresh = (int8_t)lv_slider_get_value(scn.sld_thresh);
    char tv[8]; snprintf(tv, sizeof(tv), "%d", thresh);
    lv_label_set_text(scn.lbl_thresh_val, tv);

    for (int i = 0; i < FFT_BINS; i++) {
        int h = map(scn.rssi[i], -110, -20, FFT_BAR_MIN, FFT_BAR_H - 4);
        h = constrain(h, FFT_BAR_MIN, FFT_BAR_H - 4);
        lv_obj_set_height(scn.bars[i], h);
        lv_obj_set_y(scn.bars[i], FFT_BAR_H - 4 - h);
        lv_color_t c = scn_bar_color(scn.rssi[i], thresh);
        uint32_t cv = lv_color_to32(c) & 0xFFFFFF;
        if (cv != scn.bar_color_cache[i]) {
            lv_obj_set_style_bg_color(scn.bars[i], c, 0);
            scn.bar_color_cache[i] = cv;
        }
    }

    // Always show live peak while running — lets us see what RSSI the radio
    // is actually reading even if it's below the threshold.
    if (scn.running) {
        char pb[40]; snprintf(pb, sizeof(pb), "%.2f MHz   %d dBm", scn.peak_freq, scn.peak_rssi);
        lv_label_set_text(scn.lbl_peak, pb);
        bool over = scn.peak_rssi > thresh;
        lv_obj_set_style_text_color(scn.lbl_peak,
            lv_color_hex(over ? 0xFF4466 : 0x00DDFF), 0);
        if (over) {
            scn_record_hit(scn.peak_freq, scn.peak_rssi);
            if (scn.sweep_count % 10 == 0) scn_refresh_hit_list();
        }
    }

    // Geiger-style speaker feedback (no-op if SND unchecked or peak below threshold)
    scn_speaker_update(scn.peak_rssi, thresh);

    char sb[64]; snprintf(sb, sizeof(sb), "%.2f-%.2f MHz   sweep #%lu",
        scn.start_freq, scn.stop_freq, scn.sweep_count);
    lv_label_set_text(scn.lbl_status, sb);

    if (scn.running) {
        lv_label_set_text(scn.lbl_btn_scan, "#" " STOP");
        lv_obj_set_style_bg_color(scn.btn_scan, lv_color_hex(0x550022), 0);
        lv_obj_set_style_text_color(scn.lbl_btn_scan, lv_color_hex(0xFF4466), 0);
    } else {
        lv_label_set_text(scn.lbl_btn_scan, ">" " SCAN");
        lv_obj_set_style_bg_color(scn.btn_scan, lv_color_hex(0x005533), 0);
        lv_obj_set_style_text_color(scn.lbl_btn_scan, lv_color_hex(0x00FF88), 0);
    }
}

#endif
