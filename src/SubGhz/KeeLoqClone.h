// KeeLoqClone.h — high-level KeeLoq clone workflow.
//
//   1) parseSubFileAsKeeLoq() — load a Flipper .sub from the SD card,
//      concatenate RAW_Data lines, run BinRaw::analyze, and if the result
//      looks like KeeLoq (PWM @ Te~400us, >=66 bits) parse the first 66
//      bits into a KeeLoq::Frame.
//
//   2) fetchKeeLoqKeys() — pull the manufacturer key list from the
//      WaveKai backend, with SPIFFS cache fallback.
//
//   3) transmitKeeLoqClone() — given a captured Frame + manufacturer key,
//      decrypt the counter, bump it, re-encrypt, render a Flipper-style
//      timing array, write a /captures/clone_<ts>.sub file, and TX it
//      via the existing SUBGHZ.sendSamples() path.
#pragma once

#include <Arduino.h>
#include <vector>
#include "KeeLoq.h"

namespace KeeLoqClone {

struct KeyEntry {
    String   name;
    uint64_t key;
};

// Read a .sub from SD and attempt to decode it as a 66-bit KeeLoq frame.
//   out_frame  — parsed fields (if true returned)
//   out_bits67 — 66-char + null bit string (if true returned)
// Returns true only when:
//   - file opens
//   - >=1 RAW_Data line present
//   - BinRaw::analyze succeeds
//   - encoding == PWM, te_us in [200..600], bit_count >= 66
bool parseSubFileAsKeeLoq(const char *path,
                          KeeLoq::Frame &out_frame,
                          char out_bits67[67]);

// Fetch manufacturer keys. Hits the WaveKai endpoint when WiFi is up,
// otherwise loads the SPIFFS cache.
std::vector<KeyEntry> fetchKeeLoqKeys();

// Build a clone frame (counter += counter_inc), render to RAW timings,
// write a temp .sub at /captures/clone_<millis>.sub, and TX it through
// SUBGHZ.sendSamples(). Returns true on successful TX path entry.
bool transmitKeeLoqClone(const KeeLoq::Frame &original,
                         uint64_t key,
                         uint16_t counter_inc,
                         float freq_mhz);

// Last-clone diagnostic — caller can read these after transmitKeeLoqClone()
// for status display.
extern char     lastClonePath[96];
extern uint16_t lastCloneOldCounter;
extern uint16_t lastCloneNewCounter;

}  // namespace KeeLoqClone
