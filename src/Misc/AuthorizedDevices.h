#ifndef AUTHORIZED_DEVICES_H
#define AUTHORIZED_DEVICES_H

// Authorized device serial numbers (ESP32 MAC-based chip IDs)
// Format: uppercase hex, no colons, e.g. "AABBCCDDEEFF"
// Add new devices here after verification.
// If this list is empty, ALL devices are allowed (development mode).
static const char* AUTHORIZED_SERIALS[] = {
    // --- Add authorized serials below ---
    "DCA736B3B668",
};
static const int AUTHORIZED_COUNT = sizeof(AUTHORIZED_SERIALS) / sizeof(AUTHORIZED_SERIALS[0]);

#endif
