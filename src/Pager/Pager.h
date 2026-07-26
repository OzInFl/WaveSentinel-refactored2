#ifndef PAGER_H
#define PAGER_H

// =====================================================================
// Pager.h — Core of the Pager (POCSAG/FLEX) app
//
// Ties everything together:
//   - Persistent config (systems, RIC watchlist, mode, volume, logging)
//     stored in NVS via Preferences (namespace "pager")
//   - CC1101 receive configuration (2-FSK async serial on GDO0)
//   - GDO0 edge-timing ISR -> ring buffer -> software NRZ bit recovery
//   - Dispatch to POCSAG (and experimental FLEX) decoders
//   - RIC filtering (ALL vs SELECTED), rolling message history,
//     per-RIC alert tones, and optional SD logging
//
// Header-only, included once via PagerScreen.h -> main.cpp (single TU),
// matching the RemoteScreen.h / SDCard.h pattern in this project.
// =====================================================================

#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

#include "Misc/Config.h"
#include "SD/SDCard.h"
#include "PagerTypes.h"
#include "PocsagDecoder.h"
#include "FlexDecoder.h"
#include "PagerTones.h"

// ---------------------------------------------------------------------
// Persistent configuration (loaded from / saved to NVS)
// ---------------------------------------------------------------------
static PagerSystem pgSystems[PAGER_MAX_SYSTEMS];
static uint8_t     pgSystemCount   = 0;
static uint8_t     pgSelectedSys   = 0;

static RicWatch    pgRics[PAGER_MAX_RICS];
static uint8_t     pgRicCount      = 0;

static uint8_t     pgMonitorMode   = PM_ALL;
static uint8_t     pgVolume        = 60;      // 0..100 (ToneService range)
static uint8_t     pgLogging       = 1;       // log matched pages to SD
static uint8_t     pgAlertOnAll    = 1;       // play a tone for unmatched pages (ALL mode)
static uint8_t     pgDefaultSound  = 1;       // tone for unmatched/ALL-mode pages

// ---------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------
static PagerMessage pgHistory[PAGER_MAX_MESSAGES];
static uint16_t     pgHistCount = 0;          // total received (monotonic)
static uint16_t     pgHistHead  = 0;          // ring write index
static volatile bool pgUiDirty  = false;      // new message(s) for the UI
static bool         pgRunning   = false;

// dedup guard
static uint32_t     pgLastRic     = 0xFFFFFFFF;
static uint32_t     pgLastHash    = 0;
static uint32_t     pgLastMsgMs   = 0;

// stats
static uint32_t     pgPageCount   = 0;        // pages decoded this session

// ---------------------------------------------------------------------
// Decoders — up to 3 POCSAG (for AUTO baud) + 1 FLEX
// ---------------------------------------------------------------------
static PocsagDecoder pgPocsag[3];
static uint8_t       pgPocsagN = 0;
static FlexDecoder   pgFlex;
static bool          pgFlexActive = false;

// ---------------------------------------------------------------------
// GDO0 edge ring buffer (written by ISR, drained by pager_poll)
// Each entry: bits [30:0] = duration in microseconds (capped),
//             bit  [31]   = level that just ENDED (0/1)
// ---------------------------------------------------------------------
#define PG_EDGE_BUF 1024
static volatile uint32_t pgEdge[PG_EDGE_BUF];
static volatile uint16_t pgEdgeHead = 0;
static volatile uint16_t pgEdgeTail = 0;
static volatile uint32_t pgLastEdgeUs = 0;

#define PG_GAP_BITS 40                        // >40 identical bits => idle/gap

static void IRAM_ATTR pagerEdgeISR() {
    uint32_t now = micros();
    uint32_t dur = now - pgLastEdgeUs;
    pgLastEdgeUs = now;
    if (dur > 0x7FFFFFFFu) dur = 0x7FFFFFFFu;

    // On a CHANGE edge the pin now holds the NEW level, so the segment
    // that just ended held the opposite level.
    uint8_t endedLevel = digitalRead(CC1101_GDO0) ? 0 : 1;

    uint16_t nh = (uint16_t)((pgEdgeHead + 1) % PG_EDGE_BUF);
    if (nh != pgEdgeTail) {
        pgEdge[pgEdgeHead] = (dur & 0x7FFFFFFFu) | ((uint32_t)endedLevel << 31);
        pgEdgeHead = nh;
    }
    // else: buffer full, drop this edge (decoder will re-sync)
}

// ---------------------------------------------------------------------
// Simple FNV-1a hash for message dedup
// ---------------------------------------------------------------------
static uint32_t pg_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

// ---------------------------------------------------------------------
// Watchlist lookup — returns index or -1
// ---------------------------------------------------------------------
static int pager_find_ric(uint32_t ric) {
    for (uint8_t i = 0; i < pgRicCount; ++i)
        if (pgRics[i].ric == ric) return i;
    return -1;
}

// ---------------------------------------------------------------------
// SD logging
// ---------------------------------------------------------------------
static void pager_log(const PagerMessage &m) {
    if (!pgLogging || !sdCardPresent) return;
    SD.mkdir("/pager");
    SD.mkdir("/pager/logs");
    File f = SD.open("/pager/logs/pager.log", FILE_APPEND, true);
    if (!f) return;
    const char *ty = (m.type == MT_NUMERIC) ? "NUM" :
                     (m.type == MT_ALPHA)   ? "ALP" : "TON";
    char line[160];
    snprintf(line, sizeof(line), "%lu\tRIC:%lu\t%s\t%s\n",
             (unsigned long)(m.uptimeMs / 1000), (unsigned long)m.ric, ty, m.text);
    f.print(line);
    f.close();
}

// ---------------------------------------------------------------------
// Decoder message callback — filter, store, alert, log
// ---------------------------------------------------------------------
static void pager_on_message(const PagerMessage &in, void * /*ctx*/) {
    PagerMessage m = in;

    // Dedup: same RIC+text within 2.5s is a repeat of the same page.
    uint32_t h = pg_hash(m.text);
    uint32_t now = m.uptimeMs;
    if (m.ric == pgLastRic && h == pgLastHash && (now - pgLastMsgMs) < 2500) return;
    pgLastRic = m.ric; pgLastHash = h; pgLastMsgMs = now;

    pgPageCount++;

    int wi = pager_find_ric(m.ric);
    bool inList = (wi >= 0 && pgRics[wi].enabled);
    m.matched = inList;

    // Monitor-mode filter
    if (pgMonitorMode == PM_SELECTED && !inList) return;   // ignore off-list pages

    // Store into rolling history
    pgHistory[pgHistHead] = m;
    pgHistHead = (pgHistHead + 1) % PAGER_MAX_MESSAGES;
    if (pgHistCount < 0xFFFF) pgHistCount++;
    pgUiDirty = true;

    // Alert tone
    uint8_t sound = 0;
    if (inList)                    sound = pgRics[wi].sound;
    else if (pgAlertOnAll)         sound = pgDefaultSound;
    if (sound) pager_tones_play(sound, pgVolume);

    // Log
    pager_log(m);
}

// ---------------------------------------------------------------------
// Configure decoders for the selected system's format
// ---------------------------------------------------------------------
static void pager_setup_decoders(uint8_t format) {
    pgPocsagN = 0;
    pgFlexActive = false;

    if (pager_format_is_flex(format)) {
        pgFlex.begin(pager_on_message, nullptr);
        pgFlexActive = true;
        return;
    }

    if (format == PF_POCSAG_AUTO) {
        pgPocsag[0].begin(512,  pager_on_message, nullptr);
        pgPocsag[1].begin(1200, pager_on_message, nullptr);
        pgPocsag[2].begin(2400, pager_on_message, nullptr);
        pgPocsagN = 3;
    } else {
        pgPocsag[0].begin(pager_format_baud(format), pager_on_message, nullptr);
        pgPocsagN = 1;
    }
}

// ---------------------------------------------------------------------
// Feed one reconstructed edge to all active decoders
// ---------------------------------------------------------------------
static inline void pager_feed_decoder_pocsag(PocsagDecoder &d, uint32_t dur, uint8_t level) {
    uint32_t bitPeriod = 1000000UL / d.baud();
    if (bitPeriod == 0) bitPeriod = 1;
    int nbits = (int)((dur + bitPeriod / 2) / bitPeriod);
    if (nbits < 1) nbits = 1;
    if (nbits > PG_GAP_BITS) { d.reset(); return; }
    while (nbits--) d.feedBit(level);
}

static inline void pager_feed_decoder_flex(uint32_t dur, uint8_t level) {
    const uint32_t bitPeriod = 1000000UL / 1600;   // FLEX sync layer is 1600
    int nbits = (int)((dur + bitPeriod / 2) / bitPeriod);
    if (nbits < 1) nbits = 1;
    if (nbits > PG_GAP_BITS) { pgFlex.reset(); return; }
    while (nbits--) pgFlex.feedBit(level);
}

// ---------------------------------------------------------------------
// pager_poll() — drain edge ring, decode. NO LVGL calls here.
// Called every loop() iteration while in STATE_PAGER.
// ---------------------------------------------------------------------
static void pager_poll() {
    if (!pgRunning) return;
    // Bound work per call so we never starve the main loop.
    int budget = 512;
    while (pgEdgeTail != pgEdgeHead && budget-- > 0) {
        uint32_t e = pgEdge[pgEdgeTail];
        pgEdgeTail = (uint16_t)((pgEdgeTail + 1) % PG_EDGE_BUF);
        uint32_t dur   = e & 0x7FFFFFFFu;
        uint8_t  level = (e >> 31) & 1;

        for (uint8_t i = 0; i < pgPocsagN; ++i)
            pager_feed_decoder_pocsag(pgPocsag[i], dur, level);
        if (pgFlexActive)
            pager_feed_decoder_flex(dur, level);
    }
}

// ---------------------------------------------------------------------
// Radio control
// ---------------------------------------------------------------------
static void pager_radio_start(const PagerSystem &sys) {
    uint16_t baud = pager_format_baud(sys.format);
    float drateKb = (baud ? baud : 1200) / 1000.0f;   // kBaud (0 auto -> 1.2)

    ELECHOUSE_cc1101.setSpiPin(CC1101_SCLK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setModulation(0);        // 2-FSK
    ELECHOUSE_cc1101.setMHZ(sys.freqMHz);
    ELECHOUSE_cc1101.setDeviation(4.5);       // POCSAG/FLEX ~ +/-4.5 kHz
    ELECHOUSE_cc1101.setDRate(drateKb);
    ELECHOUSE_cc1101.setRxBW(20.83);          // wide enough for 512..2400
    ELECHOUSE_cc1101.setDcFilterOff(1);       // FSK path (matches enableReceiver)
    ELECHOUSE_cc1101.setSyncMode(0);          // no hw preamble/sync; we sync in SW
    ELECHOUSE_cc1101.setPktFormat(3);         // async serial, data on GDO0
    ELECHOUSE_cc1101.SetRx();

    pinMode(CC1101_GDO0, INPUT);
    pgLastEdgeUs = micros();
    pgEdgeHead = 0;
    pgEdgeTail = 0;
    detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
    attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), pagerEdgeISR, CHANGE);
}

static void pager_radio_stop() {
    detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
}

// ---------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------
static void pager_config_defaults() {
    pgSystemCount = 1;
    strncpy(pgSystems[0].name, "Test 460.6125", PAGER_SYS_NAME_LEN - 1);
    pgSystems[0].name[PAGER_SYS_NAME_LEN - 1] = '\0';
    pgSystems[0].freqMHz = 460.6125f;
    pgSystems[0].format  = PF_POCSAG_AUTO;
    pgSystems[0].invert  = 0;
    pgSelectedSys = 0;

    pgRicCount    = 0;
    pgMonitorMode = PM_ALL;
    pgVolume      = 60;
    pgLogging     = 1;
    pgAlertOnAll  = 1;
    pgDefaultSound = 1;
}

static void pager_config_save() {
    Preferences p;
    p.begin("pager", false);
    p.putUChar("sysN", pgSystemCount);
    p.putBytes("sys", pgSystems, sizeof(PagerSystem) * pgSystemCount);
    p.putUChar("ricN", pgRicCount);
    p.putBytes("ric", pgRics, sizeof(RicWatch) * pgRicCount);
    p.putUChar("sel",  pgSelectedSys);
    p.putUChar("mode", pgMonitorMode);
    p.putUChar("vol",  pgVolume);
    p.putUChar("log",  pgLogging);
    p.putUChar("aoa",  pgAlertOnAll);
    p.putUChar("dsnd", pgDefaultSound);
    p.end();
}

static void pager_config_load() {
    Preferences p;
    p.begin("pager", true);
    if (!p.isKey("sysN")) {          // first boot
        p.end();
        pager_config_defaults();
        pager_config_save();
        return;
    }
    pgSystemCount = p.getUChar("sysN", 0);
    if (pgSystemCount > PAGER_MAX_SYSTEMS) pgSystemCount = PAGER_MAX_SYSTEMS;
    p.getBytes("sys", pgSystems, sizeof(PagerSystem) * pgSystemCount);

    pgRicCount = p.getUChar("ricN", 0);
    if (pgRicCount > PAGER_MAX_RICS) pgRicCount = PAGER_MAX_RICS;
    if (pgRicCount) p.getBytes("ric", pgRics, sizeof(RicWatch) * pgRicCount);

    pgSelectedSys  = p.getUChar("sel",  0);
    pgMonitorMode  = p.getUChar("mode", PM_ALL);
    pgVolume       = p.getUChar("vol",  60);
    pgLogging      = p.getUChar("log",  1);
    pgAlertOnAll   = p.getUChar("aoa",  1);
    pgDefaultSound = p.getUChar("dsnd", 1);
    p.end();

    if (pgSystemCount == 0) pager_config_defaults();
    if (pgSelectedSys >= pgSystemCount) pgSelectedSys = 0;
    if (pgDefaultSound < 1 || pgDefaultSound > PAGER_NUM_TONES) pgDefaultSound = 1;
}

// ---------------------------------------------------------------------
// Public control API (used by the screen + main.cpp state machine)
// ---------------------------------------------------------------------
static void pager_init() {
    pager_config_load();
}

// Begin monitoring the selected system. Ensures SD is mounted (for tones
// + logging) and generates the built-in tone files if missing.
static void pager_start() {
    if (pgSelectedSys >= pgSystemCount) pgSelectedSys = 0;
    if (pgSystemCount == 0) return;

    if (!sdCardPresent) sd_card_is_present();
    pager_tones_generate(false);

    pgPageCount = 0;
    pgUiDirty   = true;
    pager_setup_decoders(pgSystems[pgSelectedSys].format);
    pager_radio_start(pgSystems[pgSelectedSys]);
    pgRunning = true;
}

static void pager_stop() {
    pgRunning = false;
    pager_radio_stop();
}

// Clear the in-RAM message history
static void pager_clear_history() {
    pgHistCount = 0;
    pgHistHead  = 0;
    pgUiDirty   = true;
}

// Ordered access to history (0 = oldest currently held)
static uint16_t pager_history_size() {
    return (pgHistCount < PAGER_MAX_MESSAGES) ? pgHistCount : PAGER_MAX_MESSAGES;
}
static const PagerMessage *pager_history_at(uint16_t idx) {
    uint16_t n = pager_history_size();
    if (idx >= n) return nullptr;
    uint16_t start = (pgHistCount < PAGER_MAX_MESSAGES)
                     ? 0 : pgHistHead;                 // oldest slot
    return &pgHistory[(start + idx) % PAGER_MAX_MESSAGES];
}

// ---- config mutators (each saves to NVS) ----
static bool pager_add_system(const char *name, float freq, uint8_t fmt, uint8_t invert) {
    if (pgSystemCount >= PAGER_MAX_SYSTEMS) return false;
    PagerSystem &s = pgSystems[pgSystemCount];
    strncpy(s.name, name, PAGER_SYS_NAME_LEN - 1);
    s.name[PAGER_SYS_NAME_LEN - 1] = '\0';
    s.freqMHz = freq; s.format = fmt; s.invert = invert;
    pgSystemCount++;
    pager_config_save();
    return true;
}

static bool pager_update_system(uint8_t idx, const char *name, float freq,
                                uint8_t fmt, uint8_t invert) {
    if (idx >= pgSystemCount) return false;
    PagerSystem &s = pgSystems[idx];
    strncpy(s.name, name, PAGER_SYS_NAME_LEN - 1);
    s.name[PAGER_SYS_NAME_LEN - 1] = '\0';
    s.freqMHz = freq; s.format = fmt; s.invert = invert;
    pager_config_save();
    return true;
}

static bool pager_delete_system(uint8_t idx) {
    if (idx >= pgSystemCount || pgSystemCount <= 1) return false;   // keep >=1
    for (uint8_t i = idx; i < pgSystemCount - 1; ++i) pgSystems[i] = pgSystems[i + 1];
    pgSystemCount--;
    if (pgSelectedSys >= pgSystemCount) pgSelectedSys = pgSystemCount - 1;
    pager_config_save();
    return true;
}

static bool pager_add_ric(uint32_t ric, const char *label, uint8_t sound, uint8_t enabled) {
    if (pgRicCount >= PAGER_MAX_RICS) return false;
    if (pager_find_ric(ric) >= 0) return false;             // no duplicates
    RicWatch &r = pgRics[pgRicCount];
    r.ric = ric;
    strncpy(r.label, label, PAGER_RIC_LABEL_LEN - 1);
    r.label[PAGER_RIC_LABEL_LEN - 1] = '\0';
    r.sound = (sound < 1 || sound > PAGER_NUM_TONES) ? 1 : sound;
    r.enabled = enabled ? 1 : 0;
    pgRicCount++;
    pager_config_save();
    return true;
}

static bool pager_update_ric(uint8_t idx, const char *label, uint8_t sound, uint8_t enabled) {
    if (idx >= pgRicCount) return false;
    RicWatch &r = pgRics[idx];
    strncpy(r.label, label, PAGER_RIC_LABEL_LEN - 1);
    r.label[PAGER_RIC_LABEL_LEN - 1] = '\0';
    r.sound = (sound < 1 || sound > PAGER_NUM_TONES) ? 1 : sound;
    r.enabled = enabled ? 1 : 0;
    pager_config_save();
    return true;
}

static bool pager_delete_ric(uint8_t idx) {
    if (idx >= pgRicCount) return false;
    for (uint8_t i = idx; i < pgRicCount - 1; ++i) pgRics[i] = pgRics[i + 1];
    pgRicCount--;
    pager_config_save();
    return true;
}

static void pager_set_selected_system(uint8_t idx) {
    if (idx < pgSystemCount) { pgSelectedSys = idx; pager_config_save(); }
}
static void pager_set_mode(uint8_t mode) {
    pgMonitorMode = (mode == PM_SELECTED) ? PM_SELECTED : PM_ALL;
    pager_config_save();
}
static void pager_set_volume(uint8_t v) {
    pgVolume = (v > 100) ? 100 : v; pager_config_save();
}
static void pager_set_logging(bool on) { pgLogging = on ? 1 : 0; pager_config_save(); }
static void pager_set_alert_on_all(bool on) { pgAlertOnAll = on ? 1 : 0; pager_config_save(); }
static void pager_set_default_sound(uint8_t s) {
    pgDefaultSound = (s < 1 || s > PAGER_NUM_TONES) ? 1 : s; pager_config_save();
}

#endif // PAGER_H
