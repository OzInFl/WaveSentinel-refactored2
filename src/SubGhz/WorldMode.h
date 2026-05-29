#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// WorldMode — region toggle for CC1101 frequency gating.
//
// US (default): 315 / 433.92 / 915 MHz bands only (FCC ISM ranges).
// WORLD       : full CC1101-supported bands (300-348, 387-464, 779-928 MHz).
//
// Persisted in NVS namespace "region", key "world" (bool, default false).
// Used as a gate inside SubGhz::enableScanner / SubGhz::startRawCapture
// before any radio configuration is touched.
// ---------------------------------------------------------------------------

namespace WorldMode {
    // Read current setting from NVS. Default false.
    inline bool enabled() {
        Preferences p;
        if (!p.begin("region", true)) return false;
        bool v = p.getBool("world", false);
        p.end();
        return v;
    }
    // Persist new setting to NVS.
    inline void setEnabled(bool on) {
        Preferences p;
        if (!p.begin("region", false)) return;
        p.putBool("world", on);
        p.end();
    }
    // Is freq_mhz allowed under current region settings?
    // Conservative ranges from CC1101 datasheet.
    inline bool freqAllowed(float mhz) {
        if (enabled()) {
            // Worldwide: all three CC1101-supported bands
            if (mhz >= 300.0f && mhz <= 348.0f) return true;
            if (mhz >= 387.0f && mhz <= 464.0f) return true;
            if (mhz >= 779.0f && mhz <= 928.0f) return true;
            return false;
        }
        // US-only default (most restrictive — 315/433.92/915 region edges):
        if (mhz >= 300.0f && mhz <= 348.0f) return true;   // 315 MHz
        if (mhz >= 387.0f && mhz <= 434.78f) return true;  // 433.92 (US ISM cap is 434.79)
        if (mhz >= 902.0f && mhz <= 928.0f) return true;   // 915 MHz US
        return false;
    }
    // Human-readable region label for the settings UI / about box.
    inline const char *label() {
        return enabled() ? "WORLD (300-928 MHz)" : "US (315/433/915)";
    }
}
