#ifndef PAGER_TYPES_H
#define PAGER_TYPES_H

// =====================================================================
// PagerTypes.h — Shared data model for the Pager (POCSAG/FLEX) app
//
// Header-only, included once via Pager.h (which is included once in
// main.cpp) — same single-translation-unit pattern as RemoteScreen.h
// and SD/SDCard.h, so plain globals here are safe.
// =====================================================================

#include <Arduino.h>

// ---------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------
#define PAGER_MAX_SYSTEMS      8    // configurable "systems" (freq+format presets)
#define PAGER_MAX_RICS        32    // RIC (capcode) watchlist entries
#define PAGER_MAX_MESSAGES     40   // rolling in-RAM message history
#define PAGER_SYS_NAME_LEN     24
#define PAGER_RIC_LABEL_LEN    20
#define PAGER_MSG_TEXT_LEN     96
#define PAGER_NUM_TONES        5    // number of built-in alert tones

// ---------------------------------------------------------------------
// Pager format / modulation scheme
// POCSAG is 2-FSK NRZ at 512/1200/2400 baud — fully decodable from the
// CC1101 async (hard-sliced) data stream.
// FLEX 1600 is 2-level and *attemptable*; FLEX 3200/6400 are 4-level and
// cannot be recovered from a 1-bit slicer (documented limitation).
// ---------------------------------------------------------------------
enum PagerFormat : uint8_t {
    PF_POCSAG512  = 0,
    PF_POCSAG1200 = 1,
    PF_POCSAG2400 = 2,
    PF_POCSAG_AUTO = 3,   // auto-detect baud from preamble
    PF_FLEX1600   = 4,    // experimental (2-level only)
    PF_COUNT
};

static const char *PAGER_FORMAT_NAMES[] = {
    "POCSAG 512",
    "POCSAG 1200",
    "POCSAG 2400",
    "POCSAG Auto",
    "FLEX 1600 (exp)"
};

// Nominal baud for a format (0 == auto)
static inline uint16_t pager_format_baud(uint8_t fmt) {
    switch (fmt) {
        case PF_POCSAG512:  return 512;
        case PF_POCSAG1200: return 1200;
        case PF_POCSAG2400: return 2400;
        case PF_POCSAG_AUTO: return 0;
        case PF_FLEX1600:   return 1600;
        default:            return 1200;
    }
}

static inline bool pager_format_is_flex(uint8_t fmt) {
    return fmt == PF_FLEX1600;
}

// ---------------------------------------------------------------------
// Monitor mode
// ---------------------------------------------------------------------
enum PagerMonitorMode : uint8_t {
    PM_ALL      = 0,   // show/alert on every decoded page
    PM_SELECTED = 1    // only pages whose RIC is in the enabled watchlist
};

// ---------------------------------------------------------------------
// Message type (POCSAG function bits / FLEX vector type)
// ---------------------------------------------------------------------
enum PagerMsgType : uint8_t {
    MT_TONE    = 0,   // tone-only / no text
    MT_NUMERIC = 1,
    MT_ALPHA   = 2
};

// ---------------------------------------------------------------------
// A configurable pager "system" — a freq + format preset the user can
// name, save, and pick from a dropdown.
// ---------------------------------------------------------------------
typedef struct {
    char     name[PAGER_SYS_NAME_LEN];
    float    freqMHz;
    uint8_t  format;    // PagerFormat
    uint8_t  invert;    // 0/1 — data polarity inversion
} PagerSystem;

// ---------------------------------------------------------------------
// A RIC (capcode) the user wants to watch, with its alert sound.
// ---------------------------------------------------------------------
typedef struct {
    uint32_t ric;                       // capcode
    char     label[PAGER_RIC_LABEL_LEN];
    uint8_t  sound;                     // 1..PAGER_NUM_TONES
    uint8_t  enabled;                   // 0/1
} RicWatch;

// ---------------------------------------------------------------------
// A decoded page held in the rolling in-RAM history.
// ---------------------------------------------------------------------
typedef struct {
    uint32_t ric;
    uint8_t  type;       // PagerMsgType
    uint8_t  function;   // raw POCSAG function bits (0..3)
    bool     matched;    // matched a watchlist RIC
    uint32_t uptimeMs;   // millis() at decode
    char     text[PAGER_MSG_TEXT_LEN];
} PagerMessage;

#endif // PAGER_TYPES_H
