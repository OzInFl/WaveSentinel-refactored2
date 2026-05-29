#include "ToneService.h"
#include "Misc/Config.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ============================================================
// Uses I2S_NUM_0. On ESP32-S3 the LCD uses the dedicated LCD_CAM
// peripheral + GDMA — not I2S — so I2S_NUM_0 is free and coexists
// with the display (confirmed by the prior ESP32-audioI2S MP3 path).
// I2S_NUM_1 turned out to clobber the LCD's GDMA channel.
//
// 16-bit signed, stereo interleaved, 16 kHz default sample rate.
// Config matches the prior Audio library's known-good values.
// ============================================================

#define TONE_I2S_PORT      I2S_NUM_0
// 44.1 kHz so BCLK = 44100*16*2 = 1.4112 MHz — comfortably above the
// MAX98357A's ~2 MHz BCLK lock threshold (16 kHz gave only 512 kHz BCLK
// which was below threshold; chip would click on init then mute).
#define TONE_SAMPLE_RATE   44100
#define TONE_AMPLITUDE_MAX 12000    // ~37% of int16 full scale at 100%
#define TONE_CHUNK_SAMPLES 512      // ~12ms per chunk at 44.1 kHz

// Volume control — written via tone_set_volume(), read by every render
// path. Default 75% strikes a balance between speaker headroom and audibility.
static volatile uint8_t s_volume_pct = 75;
static inline int16_t tone_amplitude() {
    // Linear map from percentage to amplitude. The MAX98357A drives a
    // small speaker so even at 100% there's no clipping risk.
    return (int16_t)((int)TONE_AMPLITUDE_MAX * (int)s_volume_pct / 100);
}

void tone_set_volume(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_volume_pct = pct;
}
uint8_t tone_get_volume() { return s_volume_pct; }

static QueueHandle_t toneQueue = NULL;
static volatile bool tone_active = false;
static bool i2s_ready = false;

// ============================================================
// Predefined sequences
// ============================================================
static const ToneNote N_BOOT[]          = { {523, 60}, {784, 80} };
static const ToneNote N_CAPTURE_START[] = { {440, 60} };
static const ToneNote N_CAPTURE_OK[]    = { {523, 80}, {659, 100} };
static const ToneNote N_CAPTURE_FAIL[]  = { {659, 80}, {440, 100} };
static const ToneNote N_TX_DONE[]       = { {880, 60} };
static const ToneNote N_SCANNER_HIT[]   = { {1200, 30}, {0, 20}, {1500, 30} };
static const ToneNote N_CRACK_WIN[]     = { {523, 100}, {659, 100}, {784, 200} };
static const ToneNote N_ERROR[]         = { {200, 300} };

// --- Space Invaders ---------------------------------------------------
// SHOOT  : laser zap, falling sweep
// KILL   : 3-step descending blat for an alien explosion
// HIT    : longer/lower thud for the player ship blowing up
// UFO    : warbling alternating notes (the mystery saucer)
// UFO_HIT: shorter falling bonk for nailing the UFO
// M1..M4 : the four-note descending alien march. Played in sequence,
//          one note per alien step. As the swarm thins the step gets
//          faster, so the iconic tempo ramp is emergent.
static const ToneNote N_SI_SHOOT[]   = { {1500, 12}, {1200, 12}, {900, 12}, {600, 12}, {380, 14} };
static const ToneNote N_SI_KILL[]    = { {800, 20}, {550, 22}, {300, 28} };
static const ToneNote N_SI_HIT[]     = { {220, 60}, {150, 80}, {90, 120} };
static const ToneNote N_SI_UFO[]     = { {1000, 35}, {820, 35}, {1000, 35}, {820, 35} };
static const ToneNote N_SI_UFO_HIT[] = { {900, 25}, {500, 35}, {220, 55} };
static const ToneNote N_SI_M1[]      = { {110, 70} };
static const ToneNote N_SI_M2[]      = { {100, 70} };
static const ToneNote N_SI_M3[]      = { { 92, 70} };
static const ToneNote N_SI_M4[]      = { { 87, 70} };

const ToneSequence TONE_BOOT          = { N_BOOT,          2 };
const ToneSequence TONE_CAPTURE_START = { N_CAPTURE_START, 1 };
const ToneSequence TONE_CAPTURE_OK    = { N_CAPTURE_OK,    2 };
const ToneSequence TONE_CAPTURE_FAIL  = { N_CAPTURE_FAIL,  2 };
const ToneSequence TONE_TX_DONE       = { N_TX_DONE,       1 };
const ToneSequence TONE_SCANNER_HIT   = { N_SCANNER_HIT,   3 };
const ToneSequence TONE_CRACK_WIN     = { N_CRACK_WIN,     3 };
const ToneSequence TONE_ERROR         = { N_ERROR,         1 };

const ToneSequence TONE_SI_SHOOT   = { N_SI_SHOOT,   5 };
const ToneSequence TONE_SI_KILL    = { N_SI_KILL,    3 };
const ToneSequence TONE_SI_HIT     = { N_SI_HIT,     3 };
const ToneSequence TONE_SI_UFO     = { N_SI_UFO,     4 };
const ToneSequence TONE_SI_UFO_HIT = { N_SI_UFO_HIT, 3 };
const ToneSequence TONE_SI_M1      = { N_SI_M1,      1 };
const ToneSequence TONE_SI_M2      = { N_SI_M2,      1 };
const ToneSequence TONE_SI_M3      = { N_SI_M3,      1 };
const ToneSequence TONE_SI_M4      = { N_SI_M4,      1 };

// ============================================================
// Install I2S0 in std-mode TX, stereo, 16-bit
// ============================================================
static bool installI2S() {
    // Match Audio library's known-good config (MP3 path coexisted with LCD)
    i2s_config_t cfg = {};
    cfg.mode               = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate        = TONE_SAMPLE_RATE;
    cfg.bits_per_sample    = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format     = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags   = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count      = 16;
    cfg.dma_buf_len        = 512;
    cfg.use_apll           = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk         = true;
    cfg.mclk_multiple      = I2S_MCLK_MULTIPLE_128;

    esp_err_t err = i2s_driver_install(TONE_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[TONE] i2s_driver_install failed: %d\n", err);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRC;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    err = i2s_set_pin(TONE_I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[TONE] i2s_set_pin failed: %d\n", err);
        return false;
    }

    i2s_zero_dma_buffer(TONE_I2S_PORT);
    Serial.printf("[TONE] I2S installed BCLK=%d LRC=%d DOUT=%d\n",
        I2S_BCLK, I2S_LRC, I2S_DOUT);
    return true;
}

// ============================================================
// Render one note (square wave or rest) into I2S
// ============================================================
static void writeNote(uint16_t freq_hz, uint16_t ms) {
    int total_samples = (TONE_SAMPLE_RATE * ms) / 1000;
    if (total_samples <= 0) return;

    int half = (freq_hz > 0) ? (TONE_SAMPLE_RATE / freq_hz) / 2 : 0;
    if (freq_hz > 0 && half < 2) half = 2;

    static int16_t buf[TONE_CHUNK_SAMPLES * 2];   // stereo interleaved
    int phase = 0;
    int written_total = 0;

    while (written_total < total_samples) {
        int chunk = min(TONE_CHUNK_SAMPLES, total_samples - written_total);
        for (int i = 0; i < chunk; i++) {
            int16_t s;
            if (freq_hz == 0) {
                s = 0;
            } else {
                int16_t amp = tone_amplitude();
                s = (phase < half) ? amp : (int16_t)-amp;
                if (++phase >= half * 2) phase = 0;
            }
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }
        size_t bytes_written;
        i2s_write(TONE_I2S_PORT, buf, chunk * 4, &bytes_written, pdMS_TO_TICKS(50));
        written_total += chunk;
    }
}

// ============================================================
// Worker task
// ============================================================
static void toneTask(void *arg) {
    const ToneSequence *seq;
    while (true) {
        if (xQueueReceive(toneQueue, &seq, portMAX_DELAY) == pdTRUE) {
            if (!seq || !seq->notes || seq->count == 0) continue;
            if (!i2s_ready) continue;

            tone_active = true;
            for (int i = 0; i < seq->count; i++) {
                writeNote(seq->notes[i].freq_hz, seq->notes[i].ms);
            }
            i2s_zero_dma_buffer(TONE_I2S_PORT);
            tone_active = false;
        }
    }
}

// ============================================================
// Public API
// ============================================================
void tone_service_init() {
    if (toneQueue) return;
    i2s_ready = installI2S();
    toneQueue = xQueueCreate(8, sizeof(ToneSequence *));
    xTaskCreatePinnedToCore(toneTask, "tone", 4096, NULL, 1, NULL, 0);
}

void tone_play(const ToneSequence *seq) {
    if (!toneQueue || !seq) return;
    xQueueSend(toneQueue, &seq, 0);
}

bool tone_is_playing() {
    return tone_active;
}

// ============================================================
// Thunder synth — lowpass-filtered noise + rolling envelope.
// One-shot task; coexists with the normal tone queue because user-facing
// tones are short and thunder only fires on the idle main menu anyway.
// ============================================================
static volatile bool thunder_active = false;

static void thunderTask(void *arg) {
    if (!i2s_ready || thunder_active) { vTaskDelete(NULL); return; }
    thunder_active = true;

    uint16_t duration_ms = (uint16_t)(intptr_t)arg;
    if (duration_ms < 300) duration_ms = 300;
    if (duration_ms > 6000) duration_ms = 6000;

    const int total_samples = (TONE_SAMPLE_RATE * (int)duration_ms) / 1000;
    static int16_t buf[TONE_CHUNK_SAMPLES * 2];

    float lp1 = 0.0f, lp2 = 0.0f;          // two cascaded lowpass for deeper rumble
    int written = 0;
    while (written < total_samples) {
        int chunk = TONE_CHUNK_SAMPLES;
        if (chunk > total_samples - written) chunk = total_samples - written;

        for (int i = 0; i < chunk; i++) {
            float t = (float)(written + i) / (float)total_samples;
            // Envelope: instant attack, ~exp decay, modulated by 1.7 Hz LFO
            // for the "rolling" character. Two bumps over the duration.
            float decay = expf(-t * 2.6f);
            float lfo   = 0.55f + 0.45f * sinf(6.2831853f * 1.7f * t);
            float env   = decay * lfo;
            if (env < 0) env = 0;

            // White noise via fast pseudo-random
            int16_t noise = (int16_t)((int32_t)(esp_random() & 0xFFFF) - 32768);
            lp1 = lp1 * 0.955f + (float)noise * 0.045f;     // ~1 kHz lowpass
            lp2 = lp2 * 0.92f  + lp1 * 0.08f;               // second stage, deeper

            int16_t s = (int16_t)(lp2 * env * 0.55f * (s_volume_pct / 100.0f));
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }

        size_t bytes_written;
        i2s_write(TONE_I2S_PORT, buf, chunk * 4, &bytes_written, pdMS_TO_TICKS(100));
        written += chunk;
    }

    // Settle the DMA so it doesn't loop the last fragment
    i2s_zero_dma_buffer(TONE_I2S_PORT);
    thunder_active = false;
    vTaskDelete(NULL);
}

void tone_play_thunder(uint16_t duration_ms) {
    if (!i2s_ready) return;
    if (thunder_active) return;            // drop if one is already rolling
    xTaskCreatePinnedToCore(thunderTask, "thunder", 4096,
                            (void *)(intptr_t)duration_ms, 1, NULL, 0);
}

// ============================================================
// Shatter synth — sharp transient + highpass noise decay (~700 ms).
// Reuses the thunder_active flag as a one-burst-at-a-time guard.
// ============================================================
static void shatterTask(void * /*arg*/) {
    if (!i2s_ready || thunder_active) { vTaskDelete(NULL); return; }
    thunder_active = true;

    // Real glass-break: one sharp transient (~6 ms attack) followed by
    // a fast-decaying bright noise tail (~250 ms). Total ~280 ms — short
    // enough to feel like a single break, long enough to hear shards.
    const uint16_t duration_ms = 280;
    const int total_samples = (TONE_SAMPLE_RATE * (int)duration_ms) / 1000;
    static int16_t buf[TONE_CHUNK_SAMPLES * 2];

    // Two short impulses near the start (the initial CRACK + a tail
    // micro-crunch ~30 ms later). Both decay fast.
    const float imp_t[2]   = { 0.005f, 0.10f };
    const float imp_amp[2] = { 1.00f,  0.40f };

    float lp_hp    = 0.0f;     // first stage (for highpass derivation)
    float lp_smooth = 0.0f;    // output smoothing stage
    int written = 0;
    while (written < total_samples) {
        int chunk = TONE_CHUNK_SAMPLES;
        if (chunk > total_samples - written) chunk = total_samples - written;

        for (int i = 0; i < chunk; i++) {
            float t = (float)(written + i) / (float)total_samples;

            // Brightly-decaying noise tail (the "tinkle" of falling shards).
            // Headroom reduced — peak env is ~0.55 (was 1.0+) so the
            // output stays comfortably away from int16 clipping where
            // the distortion was coming from.
            float env = 0.10f * expf(-t * 9.0f);
            for (int j = 0; j < 2; j++) {
                float dt = t - imp_t[j];
                if (dt >= 0 && dt < 0.10f) {
                    env += imp_amp[j] * 0.55f * expf(-dt * 55.0f);
                }
            }
            if (env > 0.6f) env = 0.6f;

            float noise = (float)((int32_t)(esp_random() & 0xFFFF) - 32768);
            lp_hp = lp_hp * 0.80f + noise * 0.20f;
            float hp = noise - lp_hp;
            float v  = hp * env;
            // Output smoothing — roll off the harsh 8-22 kHz hiss that
            // makes raw highpass noise crackle through a small speaker.
            lp_smooth = lp_smooth * 0.40f + v * 0.60f;   // ~5 kHz cutoff
            int16_t s = (int16_t)(lp_smooth * (s_volume_pct / 100.0f));
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }

        size_t bw;
        i2s_write(TONE_I2S_PORT, buf, chunk * 4, &bw, pdMS_TO_TICKS(100));
        written += chunk;
    }

    i2s_zero_dma_buffer(TONE_I2S_PORT);
    thunder_active = false;
    vTaskDelete(NULL);
}

void tone_play_shatter() {
    if (!i2s_ready) return;
    if (thunder_active) return;
    xTaskCreatePinnedToCore(shatterTask, "shatter", 4096, NULL, 1, NULL, 0);
}

// ============================================================
// Alien step note — clean square wave at one of four descending
// pitches with a quick percussive envelope. The noise mix that the
// older revision used made the march sound crunchy on the small
// speaker; this version is pure square + smoothing for a softer,
// more arcade-like thunk.
// ============================================================
static void alienStepTask(void *arg) {
    if (!i2s_ready) { vTaskDelete(NULL); return; }

    uint8_t step = (uint8_t)(uintptr_t)arg;
    if (step > 3) step = 3;
    // Bumped into a clearly-audible musical range on the MAX98357A
    // (E3 / D3 / C3 / A2). Same four-note descending pattern, easier
    // to make out as a melody than the previous near-sub-bass thumps.
    static const uint16_t freqs[4] = { 330, 294, 262, 220 };
    uint16_t f = freqs[step];

    const uint16_t duration_ms = 70;
    const int total_samples = (TONE_SAMPLE_RATE * (int)duration_ms) / 1000;
    static int16_t buf[TONE_CHUNK_SAMPLES * 2];

    int half = (TONE_SAMPLE_RATE / f) / 2;
    if (half < 2) half = 2;
    int phase = 0;
    float lp_out = 0.0f;       // gentle output lowpass to soften square edges
    int written = 0;
    while (written < total_samples) {
        int chunk = TONE_CHUNK_SAMPLES;
        if (chunk > total_samples - written) chunk = total_samples - written;
        for (int i = 0; i < chunk; i++) {
            float t = (float)(written + i) / (float)total_samples;
            // Quick attack, smooth decay envelope. Slower release than
            // before so each note "rings" a touch instead of clipping
            // off abruptly.
            float env = (t < 0.04f) ? (t * 25.0f)
                                    : expf(-(t - 0.04f) * 6.0f);
            // Pure square wave (no noise mix) at the chosen pitch.
            // Removing the noise eliminates the crunch the user was
            // hearing on the previous march.
            float sq = (phase < half) ? 14000.0f : -14000.0f;
            if (++phase >= half * 2) phase = 0;
            float v = sq * env;
            // Lowpass with a slightly higher cutoff (~3.5 kHz) than the
            // shatter task so the note still has some edge but no buzz.
            lp_out = lp_out * 0.55f + v * 0.45f;
            int16_t s = (int16_t)(lp_out * (s_volume_pct / 100.0f));
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }
        size_t bw;
        i2s_write(TONE_I2S_PORT, buf, chunk * 4, &bw, pdMS_TO_TICKS(50));
        written += chunk;
    }
    i2s_zero_dma_buffer(TONE_I2S_PORT);
    vTaskDelete(NULL);
}

void tone_play_alien_step(uint8_t step) {
    if (!i2s_ready) return;
    xTaskCreatePinnedToCore(alienStepTask, "asi_step", 3072,
                            (void *)(uintptr_t)step, 1, NULL, 0);
}
