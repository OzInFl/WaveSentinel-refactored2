#ifndef FLEX_DECODER_H
#define FLEX_DECODER_H

// =====================================================================
// FlexDecoder.h — EXPERIMENTAL FLEX activity detector
//
// HARDWARE REALITY: the CC1101 async data output is a 1-bit hard slice.
// FLEX *synchronization* (BS1 + frame-sync A-word + FIW) is always sent
// 2-level at 1600 bps, so it IS recoverable here — we can reliably see
// that a FLEX system is transmitting and identify its mode + frame.
// The FLEX *message payload* for the common 3200/6400 modes is 4-level
// FSK, which a 1-bit slicer cannot represent, so those payloads are not
// decodable on this radio (would need an SDR / soft-demod front end).
//
// What this decoder does today:
//   - Detects the 32-bit frame-sync in any of the 4 standard modes
//     (1600/2, 3200/2, 3200/4, 6400/4), error-tolerant
//   - Recovers the Frame Information Word (cycle / frame) via BCH(31,21)
//   - Emits a "FLEX <mode> cyc<c> frm<f>" activity page
//
// Full 2-level block/word (address + message) decode is scaffolded for a
// future pass; it is intentionally NOT half-implemented to avoid emitting
// garbage that looks like real traffic.
// =====================================================================

#include <Arduino.h>
#include "PagerTypes.h"

class FlexDecoder {
public:
    typedef void (*MsgCallback)(const PagerMessage &msg, void *ctx);

    void begin(MsgCallback cb, void *ctx) {
        _cb = cb;
        _ctx = ctx;
        reset();
    }

    void reset() {
        _sr = 0;
        _mode = -1;
        _fiwScan = 0;
    }

    void feedBit(uint8_t bit) {
        _sr = (_sr << 1) | (bit & 1);

        if (_mode < 0) {
            // Hunt for a frame-sync word in any mode.
            for (int m = 0; m < NUM_MODES; ++m) {
                if (matchSync(_sr, MODES[m].sync)) {
                    _mode = m;
                    _fiwScan = 0;
                    _fiwWindow = 0;
                    _fiwBits = 0;
                    return;
                }
            }
            return;
        }

        // Post-sync: scan the following bits for the first valid BCH(31,21)
        // codeword and treat it as the Frame Information Word.
        _fiwWindow = (_fiwWindow << 1) | (bit & 1);
        _fiwBits++;
        _fiwScan++;

        if (_fiwBits >= 32) {
            uint32_t cw = _fiwWindow;
            if (bchSyndrome(cw) == 0 && parityOk(cw)) {
                emitFrame(cw);
                _mode = -1;                 // done with this sync
                return;
            }
        }

        if (_fiwScan > FIW_SCAN_LIMIT) {
            // No decodable FIW found — still report the FLEX activity/mode.
            emitFrame(0);                   // 0 => cycle/frame unknown
            _mode = -1;
        }
    }

private:
    struct Mode { uint32_t sync; const char *name; };

    // 32-bit frame sync = (A << 16) | (~A & 0xFFFF), per FLEX mode.
    static const int NUM_MODES = 4;
    static constexpr Mode MODES[NUM_MODES] = {
        { 0x870C78F3u, "1600/2" },
        { 0xB0684F97u, "3200/2" },
        { 0xDEA0215Fu, "3200/4" },
        { 0x4C7CB383u, "6400/4" },
    };
    static const uint16_t FIW_SCAN_LIMIT = 96;

    MsgCallback _cb  = nullptr;
    void       *_ctx = nullptr;

    uint32_t _sr        = 0;
    int8_t   _mode      = -1;
    uint16_t _fiwScan   = 0;
    uint32_t _fiwWindow = 0;
    uint8_t  _fiwBits   = 0;

    static inline uint8_t popcount32(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        x = (x + (x >> 4)) & 0x0F0F0F0Fu;
        return (uint8_t)((x * 0x01010101u) >> 24);
    }
    static inline bool matchSync(uint32_t sr, uint32_t sync) {
        return popcount32(sr ^ sync) <= 3;   // slightly looser than POCSAG
    }
    static inline uint32_t bchSyndrome(uint32_t cw) {
        uint32_t bch = cw >> 1;
        for (int i = 30; i >= 10; --i)
            if (bch & (1u << i)) bch ^= (0x769u << (i - 10));
        return bch & 0x3FFu;
    }
    static inline bool parityOk(uint32_t cw) {
        uint32_t x = cw;
        x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
        return (x & 1u) == 0;
    }

    void emitFrame(uint32_t fiw) {
        if (!_cb || _mode < 0) return;
        PagerMessage msg;
        msg.ric      = 0;                    // FLEX activity has no single RIC here
        msg.type     = MT_ALPHA;
        msg.function = 0;
        msg.matched  = false;
        msg.uptimeMs = millis();

        if (fiw) {
            uint32_t info = (fiw >> 11) & 0x1FFFFFu;   // 21-bit info field
            uint8_t cycle = (info >> 4) & 0x0F;
            uint8_t frame = (info >> 8) & 0x7F;
            snprintf(msg.text, sizeof(msg.text), "FLEX %s cyc%u frm%u",
                     MODES[_mode].name, (unsigned)cycle, (unsigned)frame);
        } else {
            snprintf(msg.text, sizeof(msg.text), "FLEX %s frame", MODES[_mode].name);
        }
        _cb(msg, _ctx);
    }
};

// Out-of-class definitions for the static constexpr members (C++14 / -std=gnu++14)
constexpr FlexDecoder::Mode FlexDecoder::MODES[];

#endif // FLEX_DECODER_H
