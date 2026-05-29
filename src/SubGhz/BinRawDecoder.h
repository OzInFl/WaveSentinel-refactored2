#pragma once
#include <stdint.h>
#include <stddef.h>

namespace BinRaw {

enum Encoding : uint8_t {
    ENC_UNKNOWN = 0,
    ENC_PWM,        // Pulse-width modulation (short=0, long=1 marks, same-length spaces)
    ENC_PPM,        // Pulse-position modulation (short space=0, long space=1, same-length marks)
    ENC_MANCHESTER, // 01 = data 0, 10 = data 1 (or vice versa)
    ENC_NRZ,        // Direct level-coded
};

struct Result {
    bool      ok;
    int       te_us;        // detected "elementary" pulse width (Te) in microseconds
    Encoding  encoding;
    int       bit_count;
    char      bits[513];    // null-terminated, max 512 bits + null
    char      hex[129];     // null-terminated hex representation (4 bits per char)
    int       symbol_count; // raw symbol (mark/space quanta) count
};

// Analyze a buffer of signed-int32 timings (Flipper RAW_Data convention).
// Returns true on success. Writes detected metrics + bit string into `out`.
//   - Builds a histogram of |timings|, finds Te as the median of the smallest cluster
//   - Quantizes each timing length / Te
//   - Tries to fit PWM / PPM / Manchester / NRZ; picks the best fit
//   - Emits the bit string in `out.bits` and the hex form in `out.hex`
bool analyze(const int32_t *timings, size_t n, Result &out);

// Helper: parse a Flipper "RAW_Data:" string (whitespace-separated signed ints
// possibly broken across multiple lines) into a malloc'd int32_t array.
// Caller must free(*out_buf).
size_t parse_raw_data(const char *raw_data_str, int32_t **out_buf);

}  // namespace BinRaw
