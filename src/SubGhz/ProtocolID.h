#ifndef PROTOCOLID_H
#define PROTOCOLID_H

#include <cmath>
#include <cstring>

// =====================================================================
// RF Protocol Identification Engine
// Analyzes raw captured timing arrays to identify known protocols
// Sources: FlipperZero firmware + RCSwitch library + NEC standard
// =====================================================================

struct ProtocolMatch {
    char name[32];
    int  confidence;  // 0-100
};

// Protocol signature entry
struct ProtocolSig {
    const char *name;
    int  teShort;        // nominal short pulse (µs)
    int  teLong;         // nominal long pulse (µs)
    int  bits;           // expected bit count (0 = don't check)
    int  tolerance;      // % tolerance for pulse matching
    // Preamble: long HIGH mark followed by long LOW space (like NEC)
    int  preambleMarkMin;
    int  preambleMarkMax;
    int  preambleSpaceMin;
    int  preambleSpaceMax;
    // Sync gap: a single long LOW duration (like Princeton guard time)
    int  syncGapMin;
    int  syncGapMax;
};

// =====================================================================
// Protocol Database (~42 entries)
// Order: most distinctive signatures first (preamble-based before
// sync-gap-based before pulse-only matches)
// =====================================================================
static const ProtocolSig PROTOCOL_DB[] = {
    // --- Preamble-based protocols (mark + space) ---
    // NEC: 9000µs mark + 4500µs space, 560µs base, 32 bits
    { "NEC",              560, 1690, 32, 30,  7500,10500, 3800, 5200,     0,    0 },
    // NEC Repeat: 9000µs mark + 2250µs space
    { "NEC Repeat",       560, 1690,  0, 30,  7500,10500, 1800, 2700,     0,    0 },
    // IDo 117/111: 4500µs mark + 4500µs space, 450/1450µs, 48 bits
    { "IDo 117/111",      450, 1450, 48, 30,  3500, 5500, 3500, 5500,     0,    0 },
    // Hormann HSM: preamble HIGH ~12000µs (24×500), 500/1000µs, 44 bits
    { "Hormann HSM",      500, 1000, 44, 30,  9500,14500,    0,    0,     0,    0 },
    // Dooya: preamble ~8796µs (12×te_long), 366/733µs, 40 bits
    { "Dooya",            366,  733, 40, 30,  7000,10500,    0,    0,     0,    0 },
    // MegaCode: preamble ~13000µs, 1000/1000µs, 24 bits
    { "MegaCode",        1000, 1000, 24, 25, 10000,16000,    0,    0,     0,    0 },

    // --- Sync-gap-based protocols (long gap in capture) ---
    // Security+ V1: huge sync ~60000µs (120×te), 500/1500µs, 21 bits
    { "Security+ V1",     500, 1500, 21, 25,     0,    0,    0,    0, 50000,70000 },
    // CAME TWEE: sync ~51000µs (51×te_long), 500/1000µs, 54 bits
    { "CAME TWEE",        500, 1000, 54, 30,     0,    0,    0,    0, 42000,60000 },
    // Linear Delta3: sync ~35000µs (70×te), 500/2000µs, 8 bits
    { "Linear Delta3",    500, 2000,  8, 30,     0,    0,    0,    0, 28000,42000 },
    // Nice FLO: sync ~25200µs (36×700), 700/1400µs, 12 bits
    { "Nice FLO",         700, 1400, 12, 30,     0,    0,    0,    0, 20000,30000 },
    // Phoenix V2: sync ~25620µs (60×427), 427/853µs, 52 bits
    { "Phoenix V2",       427,  853, 52, 25,     0,    0,    0,    0, 20000,31000 },
    // Doitrand: sync ~24800µs (62×400), 400/1100µs, 37 bits
    { "Doitrand",         400, 1100, 37, 30,     0,    0,    0,    0, 19000,30000 },
    // CAME 24-bit: sync ~24320µs (76×320), 320/640µs, 24 bits
    { "CAME 24-bit",      320,  640, 24, 30,     0,    0,    0,    0, 19000,30000 },
    // Linear: sync ~21000µs (42×500), 500/1500µs, 10 bits
    { "Linear",           500, 1500, 10, 30,     0,    0,    0,    0, 16000,26000 },
    // Clemsa: sync ~19635µs (51×385), 385/2695µs, 18 bits
    { "Clemsa",           385, 2695, 18, 30,     0,    0,    0,    0, 15000,24000 },
    // Ansonic: sync ~19425µs (35×555), 555/1111µs, 12 bits
    { "Ansonic",          555, 1111, 12, 25,     0,    0,    0,    0, 15000,24000 },
    // Gate TX: sync ~16450µs (47×350), 350/700µs, 24 bits
    { "Gate TX",          350,  700, 24, 30,     0,    0,    0,    0, 13000,20000 },
    // Mastercode: sync ~16080µs (15×1072), 1072/2145µs, 36 bits
    { "Mastercode",      1072, 2145, 36, 20,     0,    0,    0,    0, 13000,20000 },
    // Holtek: sync ~15480µs (36×430), 430/870µs, 40 bits
    { "Holtek",           430,  870, 40, 25,     0,    0,    0,    0, 12000,19000 },
    // CAME 12-bit: sync ~15040µs (47×320), 320/640µs, 12 bits
    { "CAME 12-bit",      320,  640, 12, 30,     0,    0,    0,    0, 12000,18000 },
    // BETT: sync ~14960µs (44×340), 340/2000µs, 18 bits
    { "BETT",             340, 2000, 18, 30,     0,    0,    0,    0, 11500,18500 },
    // Princeton: sync = 31×te (te 250-450 → gap 7750-13950µs), 24 bits
    { "Princeton",        390, 1170, 24, 35,     0,    0,    0,    0,  7500,20000 },
    // Nero Sketch: 47× alternating, 330/660µs, 40 bits (use sync ~15500)
    { "Nero Sketch",      330,  660, 40, 30,     0,    0,    0,    0, 12000,19000 },
    // Intertechno V3: sync ~10450µs (38×275), 275/1375µs, 32 bits
    { "Intertechno V3",   275, 1375, 32, 30,     0,    0,    0,    0,  8000,13000 },
    // Marantec: sync ~10000µs (5×2000), 1000/2000µs, 49 bits
    { "Marantec",        1000, 2000, 49, 25,     0,    0,    0,    0,  8000,13000 },
    // HT12E: sync ~9720µs (36×270), 270/540µs, inverted
    { "HT12E",            270,  540,  0, 30,     0,    0,    0,    0,  7500,12000 },
    // SM5212: sync ~11520µs (36×320), 320/640µs, inverted
    { "SM5212",           320,  640,  0, 30,     0,    0,    0,    0,  9000,14000 },
    // SMC5326: sync ~7200µs (24×300), 300/900µs, 25 bits
    { "SMC5326",          300,  900, 25, 30,     0,    0,    0,    0,  5500, 9000 },
    // HS2303-PT: sync ~9300µs (2×62×150=18600? or 62×150=9300), 150/900µs
    { "HS2303-PT",        150,  900,  0, 30,     0,    0,    0,    0,  7000,11000 },
    // Nero Radio: 49× alternating 200µs, 200/400µs, 56 bits (use sync ~10000)
    { "Nero Radio",       200,  400, 56, 30,     0,    0,    0,    0,  7500,12000 },
    // 1ByOne Doorbell: sync ~6570µs (18×365), 365/1095µs, inverted
    { "1ByOne Doorbell",  365, 1095,  0, 30,     0,    0,    0,    0,  5000, 8500 },
    // RCSwitch #2: sync ~6500µs (10×650), 650/1300µs
    { "RCSwitch #2",      650, 1300,  0, 30,     0,    0,    0,    0,  5000, 8500 },
    // Legrand: sync ~6000µs (16×375), 375/1125µs, 18 bits
    { "Legrand",          375, 1125, 18, 30,     0,    0,    0,    0,  4500, 7500 },
    // RCSwitch #5: sync ~7000µs (14×500), 500/1000µs
    { "RCSwitch #5",      500, 1000,  0, 30,     0,    0,    0,    0,  5000, 9000 },
    // RCSwitch #3: sync ~7100µs (71×100), 100/... µs
    { "RCSwitch #3",      100,  275,  0, 35,     0,    0,    0,    0,  5000, 9500 },
    // HT6P20B: sync ~10350µs (23×450), 450/900µs, inverted
    { "HT6P20B",          450,  900,  0, 30,     0,    0,    0,    0,  8000,13000 },
    // Honeywell WDB: tiny te, 160/320µs, 48 bits, header ~480µs
    { "Honeywell WDB",    160,  320, 48, 35,     0,    0,    0,    0,     0,    0 },

    // --- Protocols identified mainly by pulse width + bit count ---
    // KeeLoq: 400/800µs, 64 bits (rolling code)
    { "KeeLoq",           400,  800, 64, 30,     0,    0,    0,    0,     0,    0 },
    // KIA: 250/500µs, 61 bits
    { "KIA",              250,  500, 61, 30,     0,    0,    0,    0,     0,    0 },
    // Power Smart: 225/450µs, 64 bits (Manchester)
    { "Power Smart",      225,  450, 64, 30,     0,    0,    0,    0,     0,    0 },
    // GangQi: 500/1200µs, 34 bits, small sync ~2400µs
    { "GangQi",           500, 1200, 34, 30,     0,    0,    0,    0,  1800, 3200 },
    // Hollarm: 200/1000µs, 42 bits, small sync ~2400µs
    { "Hollarm",          200, 1000, 42, 35,     0,    0,    0,    0,  1800, 3200 },
};

static const int PROTOCOL_DB_COUNT = sizeof(PROTOCOL_DB) / sizeof(PROTOCOL_DB[0]);

// =====================================================================
// Helper: check if value is within tolerance% of target
// =====================================================================
static inline bool proto_inRange(int value, int target, int tolerancePct) {
    if (target <= 0 || value <= 0) return false;
    int margin = (target * tolerancePct) / 100;
    return (value >= target - margin) && (value <= target + margin);
}

// =====================================================================
// identifyProtocol() — analyze raw sample[] timing array
// samples[0] is unreliable, analysis starts at samples[1]
// Returns best-matching protocol name and confidence score
// =====================================================================
static ProtocolMatch identifyProtocol(const int *samples, int count) {
    ProtocolMatch result;
    memset(&result, 0, sizeof(result));
    strncpy(result.name, "Unknown", sizeof(result.name) - 1);
    result.confidence = 0;

    if (count < 10) return result;

    // --- Phase 1: Basic statistics on samples[1..count-1] ---
    int minPulse = 999999, maxPulse = 0;
    long long sumPulse = 0;
    int validCount = 0;

    for (int i = 1; i < count; i++) {
        int p = samples[i];
        if (p <= 0 || p > 100000) continue;
        if (p < minPulse) minPulse = p;
        if (p > maxPulse) maxPulse = p;
        sumPulse += p;
        validCount++;
    }

    if (validCount < 6) return result;

    // --- Phase 2: Detect NEC-style preamble (long mark in first few samples) ---
    int preambleMark = 0, preambleSpace = 0;
    bool hasPreamble = false;

    for (int i = 1; i < count - 1 && i < 8; i++) {
        if (samples[i] > 3000 && samples[i] < 100000) {
            preambleMark = samples[i];
            if (i + 1 < count && samples[i + 1] > 0) {
                preambleSpace = samples[i + 1];
            }
            hasPreamble = true;
            break;
        }
    }

    // --- Phase 3: Find longest gap (potential sync/guard) ---
    int syncGapDuration = 0;
    int syncGapIdx = -1;
    for (int i = 1; i < count; i++) {
        int p = samples[i];
        if (p > syncGapDuration && p > 1500) {
            syncGapDuration = p;
            syncGapIdx = i;
        }
    }

    // --- Phase 4: Cluster data pulses into short/long groups ---
    // Exclude preamble and sync gap from clustering
    int shortSum = 0, shortCnt = 0;
    int longSum = 0, longCnt = 0;
    int threshold = (minPulse < 1000) ? minPulse * 2 : minPulse + 500;

    for (int i = 1; i < count; i++) {
        int p = samples[i];
        if (p <= 0 || p > 5000) continue;  // skip outliers/gaps
        if (p <= threshold) {
            shortSum += p;
            shortCnt++;
        } else {
            longSum += p;
            longCnt++;
        }
    }

    int shortAvg = (shortCnt > 0) ? shortSum / shortCnt : 0;
    int longAvg = (longCnt > 0) ? longSum / longCnt : 0;

    // Estimate data bit count: data samples ÷ 2 (mark+space per bit)
    // Subtract preamble (2 samples) and sync gap (1 sample) if present
    int dataSamples = validCount;
    if (hasPreamble) dataSamples -= 2;
    if (syncGapIdx > 0) dataSamples -= 1;
    int estimatedBits = dataSamples / 2;

    // --- Phase 5: Score each protocol ---
    int bestScore = 0;
    int bestIdx = -1;

    for (int p = 0; p < PROTOCOL_DB_COUNT; p++) {
        const ProtocolSig &proto = PROTOCOL_DB[p];
        int score = 0;
        int tol = proto.tolerance;

        // Check preamble mark+space (required if defined)
        if (proto.preambleMarkMin > 0) {
            if (hasPreamble &&
                preambleMark >= proto.preambleMarkMin &&
                preambleMark <= proto.preambleMarkMax) {
                score += 35;
                // Also check space if defined
                if (proto.preambleSpaceMin > 0) {
                    if (preambleSpace >= proto.preambleSpaceMin &&
                        preambleSpace <= proto.preambleSpaceMax) {
                        score += 20;
                    } else {
                        continue;  // space doesn't match, skip
                    }
                }
            } else {
                continue;  // preamble required but not found
            }
        }

        // Check sync gap (required if defined)
        if (proto.syncGapMin > 0) {
            if (syncGapDuration >= proto.syncGapMin &&
                syncGapDuration <= proto.syncGapMax) {
                score += 30;
            } else {
                continue;  // sync gap required but not found
            }
        }

        // Penalty: if a large sync gap was detected but this protocol
        // doesn't use one (pulse-only like KeeLoq), penalize — the signal
        // is almost certainly a sync-gap protocol, not this one.
        if (proto.syncGapMin == 0 && proto.preambleMarkMin == 0 &&
            syncGapDuration > 5000) {
            score -= 15;
        }

        // Check base pulse width (te_short)
        if (shortAvg > 0 && proto_inRange(shortAvg, proto.teShort, tol)) {
            score += 20;
        } else if (shortAvg > 0 && proto_inRange(shortAvg, proto.teShort, tol + 20)) {
            score += 8;  // close but not great
        }

        // Check long pulse width (te_long)
        if (longAvg > 0 && proto_inRange(longAvg, proto.teLong, tol)) {
            score += 10;
        }

        // Check short:long ratio
        if (shortAvg > 0 && longAvg > 0 && proto.teLong > 0) {
            float expectedRatio = (float)proto.teShort / (float)proto.teLong;
            float actualRatio = (float)shortAvg / (float)longAvg;
            if (fabsf(actualRatio - expectedRatio) < 0.15f) {
                score += 10;
            }
        }

        // Check bit count
        if (proto.bits > 0) {
            if (abs(estimatedBits - proto.bits) <= 2) {
                score += 20;
            } else if (abs(estimatedBits - proto.bits) <= 5) {
                score += 8;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestIdx = p;
        }
    }

    // --- Phase 6: Return result ---
    if (bestIdx >= 0 && bestScore >= 28) {
        strncpy(result.name, PROTOCOL_DB[bestIdx].name, sizeof(result.name) - 1);
        result.name[sizeof(result.name) - 1] = '\0';
        result.confidence = (bestScore > 80) ? 95 :
                           (bestScore > 60) ? 80 :
                           (bestScore > 40) ? 60 : 40;
    } else if (shortAvg > 50 && shortAvg < 2000) {
        // Fallback: report generic OOK with measured pulse width
        snprintf(result.name, sizeof(result.name), "OOK ~%dus", shortAvg);
        result.confidence = 15;
    }

    return result;
}

#endif // PROTOCOLID_H
