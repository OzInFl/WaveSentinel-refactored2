#ifndef FLIPPER_IR_FILE_H
#define FLIPPER_IR_FILE_H

#include <Arduino.h>
#include <SD.h>
#include "IRTransmit.h"

// Index of signal names within a .ir file (for multi-signal files)
struct IRFileIndex {
    char names[IR_MAX_SIGNALS][IR_SIG_NAME_LEN];
    int count;
};

// Parse a .ir file and build an index of signal names
bool ir_file_index(const char *filepath, IRFileIndex &index);

// Parse and return a specific named signal from a .ir file
bool ir_file_read_signal(const char *filepath, const char *signalName, FlipperIRSignal &sig);

// Helper: convert Flipper protocol name string to IRremoteESP8266 decode_type_t
decode_type_t ir_protocol_from_string(const char *name);

#endif
