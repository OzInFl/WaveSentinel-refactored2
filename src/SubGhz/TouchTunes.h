#ifndef TOUCHTUNES_H
#define TOUCHTUNES_H

#include "Arduino.h"
#include "SubGhz.h"

// =====================================================================
// TouchTunes RF Remote Protocol Encoder
// NEC-like encoding at 433.92 MHz, ASK/OOK
// Based on notpike/The-Fonz research + raw capture analysis
// =====================================================================

// --- Protocol timing (microseconds) ---
#define TT_PREAMBLE_HIGH  9056
#define TT_PREAMBLE_LOW   4528
#define TT_BIT_HIGH       566
#define TT_BIT0_LOW       566
#define TT_BIT1_LOW       1698
#define TT_INTER_GAP_MS   40     // ms between repeated frames
#define TT_REPEAT_COUNT   3      // number of frame repetitions

// --- NEC address (always 0x5D for TouchTunes) ---
#define TT_ADDRESS        0x5D

// --- Command codes (MSB-first transmission order) ---
#define TT_CMD_ON_OFF         0x78
#define TT_CMD_PAUSE          0x32
#define TT_CMD_P1             0x70
#define TT_CMD_P2_EDIT_QUEUE  0x60
#define TT_CMD_P3_SKIP        0xCA
#define TT_CMD_OK             0x44
#define TT_CMD_UP             0xF2
#define TT_CMD_DOWN           0x80
#define TT_CMD_LEFT           0x84
#define TT_CMD_RIGHT          0xC4
#define TT_CMD_F1_RESTART     0x20
#define TT_CMD_F2_KEY         0xA0
#define TT_CMD_F3_MIC_A_MUTE  0x30
#define TT_CMD_F4_MIC_B_MUTE  0xB0
#define TT_CMD_1              0xF0
#define TT_CMD_2              0x08
#define TT_CMD_3              0x88
#define TT_CMD_4              0x48
#define TT_CMD_5              0xC8
#define TT_CMD_6              0x28
#define TT_CMD_7              0xA8
#define TT_CMD_8              0x68
#define TT_CMD_9              0xE8
#define TT_CMD_0              0x98
#define TT_CMD_STAR_KARAOKE   0x18
#define TT_CMD_HASH_LOCK      0x58
#define TT_CMD_ZONE1_VOL_UP   0xD0
#define TT_CMD_ZONE1_VOL_DOWN 0x50
#define TT_CMD_ZONE2_VOL_UP   0x90
#define TT_CMD_ZONE2_VOL_DOWN 0x10
#define TT_CMD_ZONE3_VOL_UP   0xC0
#define TT_CMD_ZONE3_VOL_DOWN 0x40

// --- Signal buffer size: preamble(2) + 32 bits * 2 + stop(1) = 67 ---
#define TT_SIGNAL_SIZE 67

// Reverse bits in a byte (for NEC LSB-first PIN encoding)
static inline uint8_t tt_bitReverse(uint8_t b) {
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

// Build a single TouchTunes NEC frame into a timing array
// pin: 0-255 (the establishment PIN)
// command: one of the TT_CMD_* values
// samples: output array (must be at least TT_SIGNAL_SIZE ints)
// returns: number of samples written (always TT_SIGNAL_SIZE = 67)
static int buildTouchTunesSignal(uint8_t pin, uint8_t command, int* samples) {
    uint8_t bytes[4] = {
        TT_ADDRESS,
        tt_bitReverse(pin),
        command,
        (uint8_t)(~command)
    };

    int idx = 0;

    // Preamble
    samples[idx++] = TT_PREAMBLE_HIGH;
    samples[idx++] = -TT_PREAMBLE_LOW;

    // 32 data bits (MSB first per byte)
    for (int b = 0; b < 4; b++) {
        for (int i = 7; i >= 0; i--) {
            samples[idx++] = TT_BIT_HIGH;
            if (bytes[b] & (1 << i)) {
                samples[idx++] = -TT_BIT1_LOW;
            } else {
                samples[idx++] = -TT_BIT0_LOW;
            }
        }
    }

    // Stop bit
    samples[idx++] = TT_BIT_HIGH;

    return idx;
}

// Global state for deferred transmission from main loop
extern SubGhz SUBGHZ;
static uint8_t tt_pending_pin = 0;
static uint8_t tt_pending_cmd = 0;

// Send a TouchTunes command (call from main loop, not from ISR/callback)
static void sendTouchTunesCommand(uint8_t pin, uint8_t cmd) {
    int samples[TT_SIGNAL_SIZE];
    int count = buildTouchTunesSignal(pin, cmd, samples);

    float savedFreq = SUBGHZ.getFrequency();
    SUBGHZ.setFrequency(433.92);
    SUBGHZ.setPreset(AM650);
    SUBGHZ.enableTransmit();

    for (int rep = 0; rep < TT_REPEAT_COUNT; rep++) {
        SUBGHZ.sendSamples(samples, count);
        if (rep < TT_REPEAT_COUNT - 1) {
            delay(TT_INTER_GAP_MS);
        }
    }

    SUBGHZ.disableTransmit();
    SUBGHZ.setFrequency(savedFreq);
}

#endif // TOUCHTUNES_H
