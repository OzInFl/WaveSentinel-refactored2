#include "FlipperIRFile.h"
#include <string.h>
#include <stdlib.h>

// Map Flipper protocol name strings to IRremoteESP8266 decode_type_t
decode_type_t ir_protocol_from_string(const char *name) {
    if (strcasecmp(name, "NEC") == 0 || strcasecmp(name, "NECext") == 0)
        return decode_type_t::NEC;
    if (strcasecmp(name, "Samsung32") == 0 || strcasecmp(name, "Samsung") == 0)
        return decode_type_t::SAMSUNG;
    if (strcasecmp(name, "Sony") == 0 || strcasecmp(name, "SIRC") == 0 ||
        strcasecmp(name, "SIRC15") == 0 || strcasecmp(name, "SIRC20") == 0)
        return decode_type_t::SONY;
    if (strcasecmp(name, "RC5") == 0 || strcasecmp(name, "RC5X") == 0)
        return decode_type_t::RC5;
    if (strcasecmp(name, "RC6") == 0)
        return decode_type_t::RC6;
    if (strcasecmp(name, "Kaseikyo") == 0 || strcasecmp(name, "Panasonic") == 0)
        return decode_type_t::PANASONIC;
    if (strcasecmp(name, "LG") == 0 || strcasecmp(name, "LG32") == 0)
        return decode_type_t::LG;
    if (strcasecmp(name, "JVC") == 0)
        return decode_type_t::JVC;
    if (strcasecmp(name, "Sharp") == 0)
        return decode_type_t::SHARP;
    return decode_type_t::UNKNOWN;
}

// Parse hex string "04 00 00 00" to uint64_t (little-endian, Flipper format)
static uint64_t parseHexBytes(const char *hexStr) {
    uint64_t result = 0;
    int byteIdx = 0;

    // Work on a copy since strtok modifies the string
    char tmp[64];
    strncpy(tmp, hexStr, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *tok = strtok(tmp, " ");
    while (tok && byteIdx < 8) {
        uint8_t b = (uint8_t)strtoul(tok, NULL, 16);
        result |= ((uint64_t)b << (byteIdx * 8));
        byteIdx++;
        tok = strtok(NULL, " ");
    }
    return result;
}

bool ir_file_index(const char *filepath, IRFileIndex &index) {
    index.count = 0;

    File f = SD.open(filepath, FILE_READ);
    if (!f) return false;

    char buf[256];
    while (f.available() && index.count < IR_MAX_SIGNALS) {
        int len = 0;
        while (f.available() && len < (int)sizeof(buf) - 1) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            buf[len++] = c;
        }
        buf[len] = '\0';
        if (len == 0) continue;

        // Look for "name: SignalName"
        if (strncmp(buf, "name: ", 6) == 0) {
            strncpy(index.names[index.count], buf + 6, IR_SIG_NAME_LEN - 1);
            index.names[index.count][IR_SIG_NAME_LEN - 1] = '\0';
            index.count++;
        }
    }

    f.close();
    return (index.count > 0);
}

bool ir_file_read_signal(const char *filepath, const char *signalName, FlipperIRSignal &sig) {
    File f = SD.open(filepath, FILE_READ);
    if (!f) return false;

    memset(&sig, 0, sizeof(sig));

    char buf[1024];  // raw data lines can be long
    bool inTargetSignal = false;
    bool found = false;

    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(buf) - 1) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            buf[len++] = c;
        }
        buf[len] = '\0';
        if (len == 0) continue;

        // Signal separator
        if (buf[0] == '#') {
            if (found) break;  // already parsed our target, stop
            inTargetSignal = false;
            continue;
        }

        // Check for signal name
        if (strncmp(buf, "name: ", 6) == 0) {
            if (strcmp(buf + 6, signalName) == 0) {
                inTargetSignal = true;
                strncpy(sig.name, signalName, sizeof(sig.name) - 1);
            } else if (inTargetSignal && found) {
                break;  // hit next signal's name after ours
            }
            continue;
        }

        if (!inTargetSignal) continue;

        if (strncmp(buf, "type: ", 6) == 0) {
            sig.isRaw = (strcmp(buf + 6, "raw") == 0);
            found = true;
        }
        else if (strncmp(buf, "frequency: ", 11) == 0) {
            sig.frequency = (uint32_t)atol(buf + 11);
        }
        else if (strncmp(buf, "duty_cycle: ", 12) == 0) {
            sig.dutyCycle = atof(buf + 12);
        }
        else if (strncmp(buf, "data: ", 6) == 0) {
            // Parse space-separated raw timing data
            char *dataCopy = strdup(buf + 6);
            if (dataCopy) {
                char *tok = strtok(dataCopy, " ");
                sig.rawLen = 0;
                while (tok && sig.rawLen < 512) {
                    sig.rawData[sig.rawLen++] = (uint16_t)atoi(tok);
                    tok = strtok(NULL, " ");
                }
                free(dataCopy);
            }
        }
        else if (strncmp(buf, "protocol: ", 10) == 0) {
            sig.protocol = ir_protocol_from_string(buf + 10);
        }
        else if (strncmp(buf, "address: ", 9) == 0) {
            sig.address = parseHexBytes(buf + 9);
        }
        else if (strncmp(buf, "command: ", 9) == 0) {
            sig.command = parseHexBytes(buf + 9);
        }
    }

    f.close();
    return found;
}
