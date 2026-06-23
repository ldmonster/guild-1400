// misc_recon4_parse.cpp — see header for provenance and the omitted-decoder note.
#include "util/misc_recon4_parse.h"

namespace guild::util {

// gilde.exe 0x5d3fe0 — VIBE_Util_ParseDoubleString (scanner portion only).
//
// 1:1 with the original control flow:
//   v5  = flags: bit0 sign(-), bit1 exp-negative, bit2 saw-mantissa-digit, bit3 saw-dot
//   v6  = stored significant digit count (v17[], leading zeros suppressed)
//   v7  = OR of digits seen so far | '0' (so v7!=48 means a nonzero digit appeared)
//   v9  = exponent value parsed after e/E
//   v21 = count of fractional digits (incremented while bit3 set)
//   v13 = v9 - v21 (+ (v6-19) when more than 19 sig digits), then trailing-zero adjust
int ParseDoubleStringScan(const u8* str, ParseScanResult* out, const u8** endPtr) {
    const u8* a1 = str;
    const u8* v20 = str;          // consume marker

    // --- whitespace skip: ' ' or 0x09..0x0D --------------------------------
    u8 v4;
    while (true) {
        v4 = *a1;
        if (v4 != 32 && (v4 < 9u || v4 > 0x0Du)) break;
        ++a1;
    }

    // --- optional sign ------------------------------------------------------
    char v5 = 0;
    if (v4 == '+') {
        ++a1;
    } else if (v4 == '-') {
        v5 = 1;
        ++a1;
    }

    // --- mantissa: digits with at most one '.' -----------------------------
    int  v6  = 0;
    char v7  = '0';
    int  v21 = 0;
    u8   v17[20] = {0};
    u8   v8 = 0;
    bool sawTerminatorBreak = false;

    while (true) {
        // inner loop: consume '.' (only one allowed)
        while (true) {
            v8 = *a1++;
            if (v8 != '.') break;
            if ((v5 & 8) != 0) { sawTerminatorBreak = true; break; } // second dot -> LABEL_23
            v5 |= 8;
        }
        if (sawTerminatorBreak) break;
        if (v8 < 0x30u || v8 > 0x39u) break;  // not a digit -> done with mantissa
        if ((v5 & 8) != 0) ++v21;             // fractional digit
        v7 = static_cast<char>(v7 | v8);
        if (v7 != '0') {                      // a nonzero digit has appeared
            if (v6 < 19) v17[v6] = v8;
            ++v6;
        }
        v5 |= 4;
    }

    // --- exponent -----------------------------------------------------------
    int v9 = 0;
    if ((v5 & 4) != 0) {                      // only if we saw a mantissa digit
        if (v8 == 'e' || v8 == 'E') {
            u8 v10 = *a1;
            const u8* v24 = a1 - 1;           // rollback point if no exp digits
            if (v10 == '+') {
                ++a1;
            } else if (v10 == '-') {
                v5 |= 2;
                ++a1;
            }
            // collect exponent digits; i tracks "saw an exp digit" via bit2
            char i = static_cast<char>(v5 & 0xFB);
            for (;; i |= 4) {
                u8 v12 = *a1;
                if (v12 < 0x30u || v12 > 0x39u) break;
                if (v9 < 1000) v9 = 10 * v9 + v12 - 48;
                ++a1;
            }
            if ((i & 2) != 0) v9 = -v9;
            if ((i & 4) == 0) a1 = v24;       // no exp digits: roll back to before 'e'
            v20 = a1;
        } else {
            --a1;                             // push back the non-exp char
            v20 = a1;
        }
    }
    // (if (v5&4)==0 we fall straight to LABEL_42 with v20 == original start-after-ws)

    if (endPtr) *endPtr = v20;

    // --- exponent/trailing-zero bookkeeping (LABEL_42 onward) --------------
    int v13 = v9 - v21;
    int rawDigits = v6;
    if (v6 > 19) {
        v13 += v6 - 19;
        v6 = 19;
    }
    // strip trailing zeros from the stored mantissa, bumping the exponent
    while (v6 > 0 && v17[v6 - 1] == '0') {
        ++v13;
        --v6;
    }

    out->negative   = (v5 & 1) != 0;
    out->rawDigitCount = rawDigits;
    out->endPtr     = v20;

    if (v6 == 0) {
        // all-zero result
        out->mantissa[0] = 0;
        out->digitCount  = 0;
        out->decExponent = 0;
        out->classification = 0;
        return 0;
    }

    for (int k = 0; k < v6; ++k) out->mantissa[k] = v17[k];
    out->mantissa[v6] = 0;
    out->digitCount   = v6;
    out->decExponent  = v13;

    // overflow/underflow classification on (v13 + v6 - 1):
    int v16 = v13 + v6 - 1;
    int cls;
    if (v16 > 308)        cls = 3;   // overflow
    else if (v16 >= -308) cls = 1;   // representable
    else                  cls = 2;   // underflow
    out->classification = cls;
    return cls;
}

// ---- PRNG seed -------------------------------------------------------------
// gilde.exe global dword_1452BD0 — the linear-congruential seed slot.
static i32 g_randSeed = 0;  // dword_1452BD0

i32 RandSetSeed(i32 seed) {
    g_randSeed = seed;   // 0x1422148
    return seed;         // 0x142214d
}

i32 RandCurrentSeed() { return g_randSeed; }

i32 GetTickSeed(const TickSeedHooks& hooks) {
    // 0x1414ae5: LocalTime = VIBE_Time_GetLocalTime(0)
    i32 localTime = hooks.getLocalTime ? hooks.getLocalTime() : 0;
    // 0x1414af6: return VIBE_Rand_SetSeed(LocalTime)
    return RandSetSeed(localTime);
}

} // namespace guild::util
