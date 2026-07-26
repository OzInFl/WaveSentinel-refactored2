#ifndef PAGER_TONES_H
#define PAGER_TONES_H

// =====================================================================
// PagerTones.h (v2.0.52) — Built-in alert tones for the Pager app
//
// v2.0.52 removed the ESP32-audioI2S `Audio` object; I2S is owned by the
// firmware's ToneService. So the 5 pager alert tones are defined as
// native ToneService sequences (freq_hz, ms) and played through
// tone_play() — no SD card / WAV files required for sound.
//
//   1  Single beep     2  Double beep    3  Triple chirp
//   4  Warble (hi/lo)  5  Rising sweep
// =====================================================================

#include <Arduino.h>
#include "Audio/ToneService.h"
#include "PagerTypes.h"

static const ToneNote PGT1[] = {{700,300}};
static const ToneNote PGT2[] = {{760,120},{0,80},{760,120}};
static const ToneNote PGT3[] = {{950,90},{0,55},{950,90},{0,55},{950,90}};
static const ToneNote PGT4[] = {{600,70},{950,70},{600,70},{950,70},
                                {600,70},{950,70},{600,70},{950,70}};
static const ToneNote PGT5[] = {{500,60},{650,60},{820,60},{1000,60},{1250,60},
                                {1500,60},{1250,60},{1000,60},{820,60},{600,60}};

static const ToneSequence PAGER_TONE_SEQ[PAGER_NUM_TONES] = {
    { PGT1, sizeof(PGT1)/sizeof(PGT1[0]) },
    { PGT2, sizeof(PGT2)/sizeof(PGT2[0]) },
    { PGT3, sizeof(PGT3)/sizeof(PGT3[0]) },
    { PGT4, sizeof(PGT4)/sizeof(PGT4[0]) },
    { PGT5, sizeof(PGT5)/sizeof(PGT5[0]) },
};

static const char *PAGER_TONE_NAMES[PAGER_NUM_TONES] = {
    "1 Single", "2 Double", "3 Triple", "4 Warble", "5 Sweep"
};

// Play alert tone `sound` (1..PAGER_NUM_TONES) at volume 0..100.
// Non-blocking — ToneService renders it on its own task.
static void pager_tones_play(uint8_t sound, uint8_t volumePct) {
    if (sound < 1 || sound > PAGER_NUM_TONES) sound = 1;
    tone_set_volume(volumePct);
    tone_play(&PAGER_TONE_SEQ[sound - 1]);
}

// No-op on v2 — ToneService synthesizes tones, so there are no WAV files
// to generate. Kept so Pager.h's call site is unchanged.
static inline int pager_tones_generate(bool force = false) { (void)force; return 0; }

#endif // PAGER_TONES_H
