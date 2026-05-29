#ifndef ToneService_h
#define ToneService_h

#include <Arduino.h>

// ============================================================
// Flipper-style event tone service
// Owns I2S0 between writes by ESP32-audioI2S; suppresses itself
// when audio.isRunning() (MP3 playback active).
// ============================================================

struct ToneNote {
    uint16_t freq_hz;   // 0 = silence/rest
    uint16_t ms;        // duration of this note
};

struct ToneSequence {
    const ToneNote *notes;
    uint8_t count;
};

void tone_service_init();                     // FreeRTOS task + queue (~4KB stack)
void tone_play(const ToneSequence *seq);      // enqueue, non-blocking
bool tone_is_playing();                       // for Geiger coexistence

// Volume 0..100. Stored in NVS and applied to every render call.
void tone_set_volume(uint8_t pct);
uint8_t tone_get_volume();

// Synthesized rolling thunder — generates lowpass-filtered noise with a
// quick-attack / slow-decay envelope and a rolling LFO. Runs on its own
// one-shot task so it doesn't fight the note queue. Caller passes total
// duration in ms (typical: 1500-3500). Drops the call if I2S is dormant.
void tone_play_thunder(uint16_t duration_ms);

// Synthesized glass-shatter (sharp transient + highpass-filtered noise
// decaying over ~700 ms). One-shot, non-blocking, same i2s_ready gate.
void tone_play_shatter();

// Space Invaders alien-step thump. step = 0..3 picks the pitch
// (descending across the 4-note cycle). Each call is ~70 ms of
// noise-modulated low-pitch percussion — the iconic "duh duh duh"
// that speeds up as the swarm thins.
void tone_play_alien_step(uint8_t step);

// Predefined sequences (defined in ToneService.cpp)
extern const ToneSequence TONE_BOOT;
extern const ToneSequence TONE_CAPTURE_START;
extern const ToneSequence TONE_CAPTURE_OK;
extern const ToneSequence TONE_CAPTURE_FAIL;
extern const ToneSequence TONE_TX_DONE;
extern const ToneSequence TONE_SCANNER_HIT;
extern const ToneSequence TONE_CRACK_WIN;
extern const ToneSequence TONE_ERROR;

// --- Space Invaders SFX -----------------------------------------------
// Player laser, alien hit, player hit, UFO, and the four-note descending
// march pattern that loops once per alien step.
extern const ToneSequence TONE_SI_SHOOT;
extern const ToneSequence TONE_SI_KILL;
extern const ToneSequence TONE_SI_HIT;
extern const ToneSequence TONE_SI_UFO;
extern const ToneSequence TONE_SI_UFO_HIT;
extern const ToneSequence TONE_SI_M1;
extern const ToneSequence TONE_SI_M2;
extern const ToneSequence TONE_SI_M3;
extern const ToneSequence TONE_SI_M4;

#endif
