#ifndef IR_TRANSMIT_H
#define IR_TRANSMIT_H

#include <Arduino.h>
#include <IRsend.h>
#include <IRutils.h>
#include "Misc/Config.h"

// Parsed IR signal from a Flipper .ir file
struct FlipperIRSignal {
    char name[32];
    bool isRaw;
    // Raw fields:
    uint32_t frequency;      // carrier freq in Hz (typically 38000)
    float dutyCycle;         // typically 0.33
    uint16_t rawData[512];   // timing data (all positive, alternating mark/space)
    uint16_t rawLen;
    // Parsed fields:
    decode_type_t protocol;  // IRremoteESP8266 protocol enum
    uint64_t address;
    uint64_t command;
};

// Max signals we can index from a single .ir file
#define IR_MAX_SIGNALS 32
#define IR_SIG_NAME_LEN 32

class IRTransmit {
public:
    void init();
    void sendRaw(const uint16_t rawData[], uint16_t rawLen, uint32_t freqHz);
    void sendSignal(const FlipperIRSignal &sig);

private:
    IRsend *irsend;
};

extern IRTransmit IR_TX;

#endif
