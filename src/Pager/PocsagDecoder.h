#ifndef POCSAG_DECODER_H
#define POCSAG_DECODER_H

// =====================================================================
// PocsagDecoder.h — Self-contained POCSAG (RPC1) receiver
//
// Consumes a raw NRZ bit stream (fed one bit at a time by the Pager
// core, which reconstructs bits from CC1101 GDO0 edge timings) and
// emits fully-decoded pages via a callback.
//
// Handles:
//   - Preamble / frame-sync (FSC 0x7CD215D8) hunt with error tolerance
//   - Automatic data-polarity detection (normal vs inverted FSK)
//   - BCH(31,21) + even-parity error DETECTION and CORRECTION (<=2 bits)
//   - 8-frame / 16-codeword batch structure
//   - Address codewords -> RIC (capcode) = (addr<<3) | frameIndex
//   - Numeric (4-bit BCD) and alphanumeric (7-bit) message assembly
//
// Baud-agnostic: the caller feeds bits already sampled at the target
// baud, so the same class serves 512 / 1200 / 2400 by feeding at the
// matching rate (see PF_POCSAG_AUTO handling in Pager.h).
// =====================================================================

#include <Arduino.h>
#include "PagerTypes.h"

class PocsagDecoder {
public:
    typedef void (*MsgCallback)(const PagerMessage &msg, void *ctx);

    void begin(uint16_t baud, MsgCallback cb, void *ctx) {
        _baud = baud;
        _cb = cb;
        _ctx = ctx;
        reset();
    }

    uint16_t baud() const { return _baud; }

    // Full re-sync (called on a long idle gap or when leaving RX)
    void reset() {
        _state = ST_HUNT;
        _sr = 0;
        _bitsInCw = 0;
        _cwIndex = 0;
        _pol = false;
        flushMessage();       // discard any partial
        _msgActive = false;
    }

    // Feed one demodulated bit (0/1), still in on-air polarity.
    void feedBit(uint8_t bit) {
        _sr = (_sr << 1) | (bit & 1);

        if (_state == ST_HUNT) {
            // Look for frame-sync in either polarity, tolerate <=2 bit errors.
            if (matchSync(_sr, FSC)) {
                _pol = false;
                enterBatch();
            } else if (matchSync(_sr, FSC_INV)) {
                _pol = true;
                enterBatch();
            }
            return;
        }

        // ST_BATCH / ST_EXPECT_SYNC — accumulate 32-bit codewords
        if (++_bitsInCw < 32) return;
        _bitsInCw = 0;

        uint32_t cw = _sr;                 // last 32 bits
        if (_pol) cw = ~cw;                // de-invert to canonical polarity

        if (_state == ST_EXPECT_SYNC) {
            // After a full batch we expect another sync codeword.
            if (matchSync(cw, FSC) || matchSync(_sr, FSC) || matchSync(_sr, FSC_INV)) {
                enterBatch();
            } else {
                // End of transmission (or lost lock) — flush and re-hunt.
                flushMessage();
                _state = ST_HUNT;
            }
            return;
        }

        // ST_BATCH: process this codeword slot
        processCodeword(cw, _cwIndex >> 1 /* frame index 0..7 */);

        if (++_cwIndex >= 16) {
            _cwIndex = 0;
            _state = ST_EXPECT_SYNC;       // next 32 bits should be sync
        }
    }

private:
    enum State : uint8_t { ST_HUNT, ST_BATCH, ST_EXPECT_SYNC };

    static const uint32_t FSC     = 0x7CD215D8u;   // POCSAG frame sync codeword
    static const uint32_t FSC_INV = 0x832DEA27u;   // ~FSC (inverted polarity)
    static const uint32_t IDLE_CW = 0x7A89C197u;   // idle codeword

    // ---- config / callback ----
    uint16_t    _baud = 1200;
    MsgCallback _cb   = nullptr;
    void       *_ctx  = nullptr;

    // ---- bit engine ----
    State    _state    = ST_HUNT;
    uint32_t _sr       = 0;
    uint8_t  _bitsInCw = 0;
    uint8_t  _cwIndex  = 0;
    bool     _pol      = false;   // true = inverted polarity detected

    // ---- current message assembly ----
    bool     _msgActive = false;
    uint32_t _curRic    = 0;
    uint8_t  _curFn     = 0;
    char     _alpha[PAGER_MSG_TEXT_LEN];
    uint16_t _alphaLen  = 0;
    uint32_t _alphaAcc  = 0;
    uint8_t  _alphaBits = 0;
    char     _num[PAGER_MSG_TEXT_LEN];
    uint16_t _numLen    = 0;
    uint32_t _numAcc    = 0;
    uint8_t  _numBits   = 0;

    static inline uint8_t popcount32(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        x = (x + (x >> 4)) & 0x0F0F0F0Fu;
        return (uint8_t)((x * 0x01010101u) >> 24);
    }

    static inline bool matchSync(uint32_t sr, uint32_t sync) {
        return popcount32(sr ^ sync) <= 2;   // tolerate up to 2 bit errors
    }

    void enterBatch() {
        _state = ST_BATCH;
        _bitsInCw = 0;
        _cwIndex = 0;
    }

    // ---- BCH(31,21) + parity ----
    // Syndrome over the 31 BCH bits (codeword bits 31..1); 0 == valid.
    static inline uint32_t bchSyndrome(uint32_t cw) {
        uint32_t bch = cw >> 1;             // 31-bit BCH word in bits 30..0
        for (int i = 30; i >= 10; --i) {
            if (bch & (1u << i)) bch ^= (0x769u << (i - 10));
        }
        return bch & 0x3FFu;
    }

    static inline bool parityOk(uint32_t cw) {
        uint32_t x = cw;
        x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
        return (x & 1u) == 0;               // even parity
    }

    // Try to correct up to 2 bit errors. Returns true if the corrected
    // codeword is valid; cw is updated in place.
    static bool correct(uint32_t &cw) {
        if (bchSyndrome(cw) == 0 && parityOk(cw)) return true;
        for (int i = 0; i < 32; ++i) {
            uint32_t t = cw ^ (1u << i);
            if (bchSyndrome(t) == 0 && parityOk(t)) { cw = t; return true; }
        }
        for (int i = 0; i < 32; ++i) {
            for (int j = i + 1; j < 32; ++j) {
                uint32_t t = cw ^ (1u << i) ^ (1u << j);
                if (bchSyndrome(t) == 0 && parityOk(t)) { cw = t; return true; }
            }
        }
        return false;
    }

    void processCodeword(uint32_t cw, uint8_t frameIndex) {
        if (!correct(cw)) return;           // uncorrectable — skip slot

        if (cw == IDLE_CW) { flushMessage(); return; }

        if ((cw & 0x80000000u) == 0) {
            // Address codeword: bit31 == 0
            flushMessage();                 // previous page ends here
            uint32_t addr = (cw >> 13) & 0x3FFFFu;      // 18-bit address field
            uint8_t  fn   = (cw >> 11) & 0x3u;          // 2 function bits
            _curRic    = (addr << 3) | (frameIndex & 0x7u);
            _curFn     = fn;
            _msgActive = true;
            _alphaLen = 0; _alphaAcc = 0; _alphaBits = 0;
            _numLen   = 0; _numAcc   = 0; _numBits   = 0;
        } else {
            // Message codeword: bit31 == 1, 20 data bits (30..11), MSB first
            if (!_msgActive) return;
            uint32_t data = (cw >> 11) & 0xFFFFFu;
            for (int k = 19; k >= 0; --k) {
                appendBit((data >> k) & 1u);
            }
        }
    }

    void appendBit(uint8_t b) {
        // Alphanumeric: 7-bit chars, first-received bit is LSB
        _alphaAcc |= ((uint32_t)b << _alphaBits);
        if (++_alphaBits >= 7) {
            char c = (char)(_alphaAcc & 0x7Fu);
            _alphaAcc >>= 7; _alphaBits -= 7;
            if (c >= 0x20 && c < 0x7F && _alphaLen < PAGER_MSG_TEXT_LEN - 1)
                _alpha[_alphaLen++] = c;
        }
        // Numeric: 4-bit BCD, first-received bit is LSB
        _numAcc |= ((uint32_t)b << _numBits);
        if (++_numBits >= 4) {
            static const char NMAP[16] = {'0','1','2','3','4','5','6','7',
                                          '8','9','*','U',' ','-',')','('};
            char c = NMAP[_numAcc & 0xFu];
            _numAcc >>= 4; _numBits -= 4;
            if (_numLen < PAGER_MSG_TEXT_LEN - 1) _num[_numLen++] = c;
        }
    }

    void flushMessage() {
        if (!_msgActive) return;
        _msgActive = false;

        PagerMessage msg;
        msg.ric      = _curRic;
        msg.function = _curFn;
        msg.matched  = false;
        msg.uptimeMs = millis();

        // Function bit 0 => numeric convention; otherwise alphanumeric.
        if (_curFn == 0 && _numLen > 0) {
            msg.type = MT_NUMERIC;
            _num[_numLen] = '\0';
            trimCopy(msg.text, _num);
        } else if (_alphaLen > 0) {
            msg.type = MT_ALPHA;
            _alpha[_alphaLen] = '\0';
            trimCopy(msg.text, _alpha);
        } else if (_numLen > 0) {
            msg.type = MT_NUMERIC;
            _num[_numLen] = '\0';
            trimCopy(msg.text, _num);
        } else {
            msg.type = MT_TONE;
            msg.text[0] = '\0';
        }

        if (_cb) _cb(msg, _ctx);
    }

    static void trimCopy(char *dst, const char *src) {
        // Copy, dropping trailing spaces
        strncpy(dst, src, PAGER_MSG_TEXT_LEN - 1);
        dst[PAGER_MSG_TEXT_LEN - 1] = '\0';
        int n = (int)strlen(dst);
        while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
    }
};

#endif // POCSAG_DECODER_H
