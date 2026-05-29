// BinRawDecoder.cpp — Flipper "BinRAW"-style decoder for raw OOK timings.
//
// Input convention (Flipper RAW_Data):
//   positive int32_t = mark (carrier on) duration in microseconds
//   negative int32_t = space (carrier off) duration in microseconds
//
// Algorithm:
//   1) Histogram |timings| with bucket = 20us. The "Te" elementary symbol
//      is the median of the smallest-center bucket that holds at least
//      ~10% of total samples (i.e. the shortest pulse that appears often).
//   2) Score four candidate encodings (PWM, PPM, Manchester, NRZ).
//   3) Emit a bit string + hex packing for the best encoding (>= 0.5 score).

#include "BinRawDecoder.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace BinRaw {

// ---------- tunables ----------
static const int    HIST_BUCKET_US      = 20;     // histogram resolution
static const int    HIST_MAX_BUCKETS    = 512;    // covers 0..10.24 ms (plenty for Te search)
static const float  TE_PRESENCE_RATIO   = 0.10f;  // bucket needs >=10% of samples
static const float  ENC_SCORE_FLOOR     = 0.50f;  // below this -> ENC_UNKNOWN
static const float  TOL                 = 0.25f;  // +/- 25% tolerance vs N*Te
static const int    MAX_BITS            = 512;

// ---------- small utilities ----------
static inline int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

// Round value to nearest integer count of Te quanta (>=1).
static int quanta(int32_t v, int te) {
    if (te <= 0) return 0;
    int q = (abs32(v) + te / 2) / te;
    return q < 1 ? 1 : q;
}

static bool near_n_te(int32_t v, int n, int te) {
    if (te <= 0) return false;
    int target = n * te;
    int dv = abs32(v) - target;
    if (dv < 0) dv = -dv;
    return (float)dv <= TOL * (float)target;
}

// ---------- Te detection ----------
// Returns 0 on failure.
static int detect_te(const int32_t *timings, size_t n) {
    if (!timings || n == 0) return 0;

    // Histogram
    static int counts[HIST_MAX_BUCKETS];
    memset(counts, 0, sizeof(counts));

    size_t total = 0;
    for (size_t i = 0; i < n; ++i) {
        int32_t v = abs32(timings[i]);
        if (v <= 0) continue;
        int b = v / HIST_BUCKET_US;
        if (b < 0) continue;
        if (b >= HIST_MAX_BUCKETS) b = HIST_MAX_BUCKETS - 1;
        counts[b]++;
        total++;
    }
    if (total == 0) return 0;

    int threshold = (int)((float)total * TE_PRESENCE_RATIO);
    if (threshold < 1) threshold = 1;

    // Find lowest-center bucket meeting threshold.
    int te_bucket = -1;
    for (int b = 0; b < HIST_MAX_BUCKETS; ++b) {
        if (counts[b] >= threshold) {
            te_bucket = b;
            break;
        }
    }

    // Fallback: pick the densest bucket overall if no bucket passed threshold.
    if (te_bucket < 0) {
        int best = 0, best_b = 0;
        for (int b = 0; b < HIST_MAX_BUCKETS; ++b) {
            if (counts[b] > best) { best = counts[b]; best_b = b; }
        }
        if (best == 0) return 0;
        te_bucket = best_b;
    }

    // Median of values inside that bucket.
    int lo = te_bucket * HIST_BUCKET_US;
    int hi = lo + HIST_BUCKET_US;

    // Collect (cap to a sane stack array)
    static int samples[2048];
    int sn = 0;
    for (size_t i = 0; i < n && sn < (int)(sizeof(samples)/sizeof(samples[0])); ++i) {
        int v = (int)abs32(timings[i]);
        if (v >= lo && v < hi) samples[sn++] = v;
    }
    if (sn == 0) {
        // bucket center
        return lo + HIST_BUCKET_US / 2;
    }
    qsort(samples, sn, sizeof(int), cmp_int);
    int median = samples[sn / 2];
    if (median <= 0) median = lo + HIST_BUCKET_US / 2;
    return median;
}

// ---------- per-encoding scorers ----------
//
// We pass over the timings classifying each entry as mark/space.

static float score_pwm(const int32_t *t, size_t n, int te) {
    // marks ~ 1*Te or 3*Te, spaces ~ 1*Te
    int marks = 0, hits = 0;
    for (size_t i = 0; i < n; ++i) {
        if (t[i] > 0) {
            marks++;
            if (near_n_te(t[i], 1, te) || near_n_te(t[i], 3, te)) hits++;
        }
    }
    if (marks == 0) return 0.f;
    return (float)hits / (float)marks;
}

static float score_ppm(const int32_t *t, size_t n, int te) {
    int spaces = 0, hits = 0;
    for (size_t i = 0; i < n; ++i) {
        if (t[i] < 0) {
            spaces++;
            if (near_n_te(t[i], 1, te) || near_n_te(t[i], 3, te)) hits++;
        }
    }
    if (spaces == 0) return 0.f;
    return (float)hits / (float)spaces;
}

static float score_manchester(const int32_t *t, size_t n, int te) {
    // Count pairs (i, i+1) where both quantize to 1*Te.
    if (n < 2) return 0.f;
    int pairs = 0, ok_pairs = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        pairs++;
        if (near_n_te(t[i], 1, te) && near_n_te(t[i + 1], 1, te)) ok_pairs++;
    }
    if (pairs == 0) return 0.f;
    return (float)ok_pairs / (float)pairs;
}

static float score_nrz(const int32_t *t, size_t n, int te) {
    // Each timing should quantize cleanly to 1..4*Te.
    if (n == 0) return 0.f;
    int ok = 0;
    for (size_t i = 0; i < n; ++i) {
        for (int k = 1; k <= 4; ++k) {
            if (near_n_te(t[i], k, te)) { ok++; break; }
        }
    }
    return (float)ok / (float)n;
}

// ---------- bit emitters ----------
static int emit_pwm(const int32_t *t, size_t n, int te, char *bits) {
    int bc = 0;
    for (size_t i = 0; i < n && bc < MAX_BITS; ++i) {
        if (t[i] > 0) {
            bits[bc++] = (t[i] > 2 * te) ? '1' : '0';
        }
    }
    bits[bc] = '\0';
    return bc;
}

static int emit_ppm(const int32_t *t, size_t n, int te, char *bits) {
    int bc = 0;
    for (size_t i = 0; i < n && bc < MAX_BITS; ++i) {
        if (t[i] < 0) {
            int32_t sp = -t[i];
            bits[bc++] = (sp > 2 * te) ? '1' : '0';
        }
    }
    bits[bc] = '\0';
    return bc;
}

static int emit_manchester(const int32_t *t, size_t n, int te, char *bits) {
    // Treat each timing as 1 quantum; pair consecutively.
    // 01 (space then mark) -> '0', 10 (mark then space) -> '1'.
    int bc = 0;
    for (size_t i = 0; i + 1 < n && bc < MAX_BITS; i += 2) {
        bool a_mark = t[i] > 0;
        bool b_mark = t[i + 1] > 0;
        // expect alternating
        if (a_mark == b_mark) break;
        if (!a_mark && b_mark) {
            bits[bc++] = '0';   // space, mark
        } else if (a_mark && !b_mark) {
            bits[bc++] = '1';   // mark, space
        } else {
            break;
        }
    }
    bits[bc] = '\0';
    return bc;
}

static int emit_nrz(const int32_t *t, size_t n, int te, char *bits) {
    int bc = 0;
    for (size_t i = 0; i < n && bc < MAX_BITS; ++i) {
        int q = quanta(t[i], te);
        char c = (t[i] > 0) ? '1' : '0';
        for (int k = 0; k < q && bc < MAX_BITS; ++k) {
            bits[bc++] = c;
        }
    }
    bits[bc] = '\0';
    return bc;
}

// ---------- hex packing ----------
static void pack_hex(const char *bits, int bit_count, char *hex_out) {
    // 4 bits MSB-first per nibble, pad with zeros on the right.
    int nibbles = (bit_count + 3) / 4;
    if (nibbles > 128) nibbles = 128;
    for (int n = 0; n < nibbles; ++n) {
        unsigned v = 0;
        for (int b = 0; b < 4; ++b) {
            int idx = n * 4 + b;
            v <<= 1;
            if (idx < bit_count && bits[idx] == '1') v |= 1;
        }
        hex_out[n] = (char)((v < 10) ? ('0' + v) : ('A' + (v - 10)));
    }
    hex_out[nibbles] = '\0';
}

// ---------- symbol counter (for diagnostics) ----------
static int count_symbols(const int32_t *t, size_t n, int te) {
    int sc = 0;
    for (size_t i = 0; i < n; ++i) sc += quanta(t[i], te);
    return sc;
}

// ---------- public API ----------
bool analyze(const int32_t *timings, size_t n, Result &out) {
    memset(&out, 0, sizeof(out));
    out.ok = false;
    out.bits[0] = '\0';
    out.hex[0] = '\0';

    if (!timings || n < 4) return false;

    int te = detect_te(timings, n);
    if (te <= 0) return false;
    out.te_us = te;

    float s_pwm  = score_pwm(timings, n, te);
    float s_ppm  = score_ppm(timings, n, te);
    float s_man  = score_manchester(timings, n, te);
    float s_nrz  = score_nrz(timings, n, te);

    Encoding best = ENC_UNKNOWN;
    float    best_s = ENC_SCORE_FLOOR;
    if (s_pwm > best_s) { best_s = s_pwm; best = ENC_PWM; }
    if (s_ppm > best_s) { best_s = s_ppm; best = ENC_PPM; }
    if (s_man > best_s) { best_s = s_man; best = ENC_MANCHESTER; }
    if (s_nrz > best_s) { best_s = s_nrz; best = ENC_NRZ; }

    out.encoding = best;
    out.symbol_count = count_symbols(timings, n, te);

    int bc = 0;
    switch (best) {
        case ENC_PWM:        bc = emit_pwm(timings, n, te, out.bits); break;
        case ENC_PPM:        bc = emit_ppm(timings, n, te, out.bits); break;
        case ENC_MANCHESTER: bc = emit_manchester(timings, n, te, out.bits); break;
        case ENC_NRZ:        bc = emit_nrz(timings, n, te, out.bits); break;
        case ENC_UNKNOWN:
        default:
            // Still emit an NRZ-style bit string as a best-effort dump.
            bc = emit_nrz(timings, n, te, out.bits);
            break;
    }
    out.bit_count = bc;
    pack_hex(out.bits, bc, out.hex);

    out.ok = (bc > 0);
    return out.ok;
}

// ---------- parse_raw_data ----------
//
// Whitespace-separated signed ints. Grows geometrically with realloc().
size_t parse_raw_data(const char *raw_data_str, int32_t **out_buf) {
    if (out_buf) *out_buf = nullptr;
    if (!raw_data_str || !out_buf) return 0;

    size_t cap = 256;
    size_t cnt = 0;
    int32_t *buf = (int32_t *)malloc(cap * sizeof(int32_t));
    if (!buf) return 0;

    const char *p = raw_data_str;
    while (*p) {
        // skip whitespace and non-numeric leaders (but keep '-' sign)
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;

        // a valid token starts with '-' or digit
        if (*p != '-' && !isdigit((unsigned char)*p)) {
            // skip stray char (e.g. ':' in "RAW_Data:")
            ++p;
            continue;
        }

        // parse the integer
        char *endp = nullptr;
        long v = strtol(p, &endp, 10);
        if (endp == p) {
            ++p;
            continue;
        }
        p = endp;

        if (cnt == cap) {
            size_t new_cap = cap * 2;
            int32_t *nb = (int32_t *)realloc(buf, new_cap * sizeof(int32_t));
            if (!nb) {
                free(buf);
                *out_buf = nullptr;
                return 0;
            }
            buf = nb;
            cap = new_cap;
        }
        buf[cnt++] = (int32_t)v;
    }

    if (cnt == 0) {
        free(buf);
        *out_buf = nullptr;
        return 0;
    }

    *out_buf = buf;
    return cnt;
}

}  // namespace BinRaw
