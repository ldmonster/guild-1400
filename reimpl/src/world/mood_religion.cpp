#include "world/mood_religion.h"

#include <cstring>      // memcmp, strlen

#include "util/math_random.h"     // RandomModulo (VIBE_Math_RandomModulo @0x58b89c)
#include "util/math_rng_float.h"  // RandomFloatScaled (VIBE_Math_RandomFloatScaled @0x58b910)

// Faithful ports of the recoverable arithmetic / weighted-random cores of the
// city mood & religion routines. The GUI / command-queue / text-render / 3D
// plumbing is the engine's; the integer-exact rules are reproduced here. FP
// tuning constants are the exact IEEE-754 floats/doubles recovered via get_bytes.

namespace guild::world {

// ---------------------------------------------------------------------------
// VIBE_AiMethod_ComputeMoodLevel  (gilde.exe 0x467a50).   VERIFIED-1:1.
// ---------------------------------------------------------------------------
// Disasm cross-check (0x467a56..0x467ade):
//   fld[a1+4]; fmul flt_61A224(0x3e800000=0.25) -> 80-bit product v1 (st0).
//   fst var_14 (a *float* slot, no pop); fcomp flt_61A228(2.0) uses the 80-bit v1.
//   If v1>2.0: fld var_14 (the float32-rounded value) and fcomp flt_61A22C(6.0).
//   Multiplying a float by 0.25 is EXACT in every precision (pure exponent shift),
//   so round32(gauge*0.25) == 80-bit(gauge*0.25) bit-for-bit and the float/double
//   split is moot here -> double modeling is provably identical.
//   Return path: ConvertX (RC=truncate, see 0x5c6b08 fldcw 0x_F00|orig + frndint)
//   then fistp -> the value is already integral, so net effect is TRUNCATE toward
//   zero, matching the C++ (int) casts. flt bytes confirmed via get_bytes @0x61a224.
int ComputeMoodLevel(float gaugeValue) {
    // v1 = *(float*)(a1+4) * flt_61A224;
    double v = static_cast<double>(gaugeValue * mood::kMoodValueScale);
    // if ( v1 > flt_61A228 && v4 >= (double)flt_61A22C ) return (int)6.0;
    if (v > static_cast<double>(mood::kMoodLowThresh)
        && v >= static_cast<double>(mood::kMoodHighThresh)) {
        return 6;
    }
    // else { v2 = value*scale; if (v2 <= flt_61A228) v6 = 2.0; else v6 = v2; return (int)v6; }
    if (v <= static_cast<double>(mood::kMoodLowThresh))
        return 2;
    return static_cast<int>(v);
}

// VIBE_Npc_AdjustRelationByMood (0x56840c) is already translated as
// guild::sim::NpcAdjustRelationByMood (src/sim/npcaction.cpp) — not redefined here.

// ---------------------------------------------------------------------------
// VIBE_Person_AdjustMoodAndNotify  (gilde.exe 0x594afc).   VERIFIED-1:1.
// ---------------------------------------------------------------------------
// Disasm 0x594b08..0x594b2f: cur = *(int*)(a1+89) >> 24 (SAR = signed high byte
// at a1+92). Two SEQUENTIAL ifs (not else-if), matching the binary's two compares:
//   if (delta+cur < 0)   d = -a1[92]  (a1[92] is signed char -> -cur)
//   if (cur+delta > 100) d = 100-a1[92] (= 100-cur)
// `currentMood` is the caller's signed mood byte; arithmetic is plain signed int.
int ClampMoodDelta(int currentMood, int delta) {
    // HIBYTE(v11) = a2;                                       // d = delta
    int d = delta;
    // if ( a2 + cur < 0 )    HIBYTE(v11) = -a1[92];           // d = -cur
    if (delta + currentMood < 0)
        d = -currentMood;
    // if ( cur + a2 > 100 )  HIBYTE(v11) = 100 - a1[92];      // d = 100 - cur
    if (currentMood + delta > mood::kMoodMax)
        d = mood::kMoodMax - currentMood;
    return d;
}

// ---------------------------------------------------------------------------
// VIBE_MeisterAi_SelectMoodColor  (gilde.exe 0x4664d8).
// ---------------------------------------------------------------------------
// dword_46640C — 3 rows x 5 cumulative thresholds, recovered byte-for-byte.
const float kMoodColorThresholds[3][5] = {
    {0.20000000298023224f, 0.550000011920929f, 0.8500000238418579f,
     0.949999988079071f, 1.0f},
    {0.10000000149011612f, 0.4000000059604645f, 0.699999988079071f,
     0.8999999761581421f, 1.0f},
    {0.05000000074505806f, 0.25f, 0.550000011920929f, 0.800000011920929f, 1.0f},
};

// gilde.exe 0x4664d8 walk (0x466511..0x466539 / 0x466568..0x466590).  VERIFIED-1:1.
// In the binary the draw lives in a *float* stack slot (var_28/var_2C, declared
// float[]) and each step does `fld var_28; fcomp v16[v3]` — i.e. a float32 draw vs
// a float32 threshold, both widened identically to 80-bit for the compare. Here the
// draw is carried as `double`. That is SAFE: the only producer is
// RandomFloatScaled() = (int n in [0,32767]) * flt_62675C(0x38000100). An exhaustive
// sweep of all 32768 possible n showed round32(n*scale)<=thr and (double)(n*scale)<=thr
// agree for EVERY (row,col) threshold -> double vs float32 never flips a tier. So the
// float-slot rounding is unobservable and double modeling is provably 1:1.
// Fall-through to 5: the binary instead breaks at v2==5 leaving v21[0] *unwritten*
// (stack garbage). That path is UNREACHABLE in the live tree: max draw n=32767 gives
// 0.99999999 <= 1.0 (the last column in every row), so the walk always hits LABEL_6.
// Returning 5 here is a faithful, documented choice for the out-of-domain parametric
// input only.
int MoodTierFromDraw(int row, double draw) {
    // v2 = 0; if ( v19 <= (double)v16[5*v1] ) -> index 0; else walk while v2 < 5.
    for (int col = 0; col < 5; ++col) {
        if (draw <= static_cast<double>(kMoodColorThresholds[row][col]))
            return col + 1;          // v21[0] = v2 + 1;
    }
    return 5;                        // unreachable for any real RNG draw (see above)
}

int SelectMoodColorTiers(u8 moodClass, double d0, int secondRow, double d1) {
    // v1 = (v0==1) ? 0 : ((v0!=2)+1);
    int v1 = (moodClass == 1) ? 0 : ((moodClass != 2) + 1);
    int primary = MoodTierFromDraw(v1, d0);          // v21[0]
    int second  = MoodTierFromDraw(secondRow, d1);   // v21[1] = (v4+1) << 8
    return primary | (second << 8);                  // return v21[1] | v21[0];
}

// RNG draw COUNT/ORDER verified against disasm 0x4664f1 / 0x46654e / 0x466556:
//   (1) RandomFloatScaled -> d0   [0x4664f1, BEFORE the row is even computed]
//   (2) RandomModulo(3) if primary-row==1 else RandomModulo(2) -> secondRow
//   (3) RandomFloatScaled -> d1
// (The original then draws further RandomModulo()s for the color-string side effect
// at 0x4665b1.., which is GUI/light state outside this integer pick.)
int SelectMoodColorTiers(u8 moodClass) {
    int v1 = (moodClass == 1) ? 0 : ((moodClass != 2) + 1);
    // The original draws d0, then RandomModulo(v1==1?3:2) for the second row,
    // then d1 — reproduce that exact call order against the shared LCG.
    double d0 = util::RandomFloatScaled();
    int secondRow = (v1 == 1) ? util::RandomModulo(3) : util::RandomModulo(2);
    double d1 = util::RandomFloatScaled();
    return SelectMoodColorTiers(moodClass, d0, secondRow, d1);
}

// ---------------------------------------------------------------------------
// VIBE_Cheat_ParseSetReligion  (gilde.exe 0x4fbf70).   VERIFIED-1:1 (parse+cost).
// ---------------------------------------------------------------------------
// Range check confirmed via disasm 0x4fc0d8..0x4fc0f6: first ParseInt result (edx,
// the LEVEL/num0) checked [1,80] (cmp 1 / cmp 0x50), second (eax, REGION/num1)
// checked [1,8]. VIBE_Util_ParseInt @0x5dc070 is a full atoi (ws-skip + sign), but
// num0/num1 are pre-validated 1..3 digit-only NUL-terminated fields, so the
// digit-only ParseIntStr clone is identical over that constrained domain.
namespace {
// aKatholisch @0x4f8d3c — 2 entries, stride 16 (NUL-padded).
const char kReligionNames[2][16] = {"KATHOLISCH", "EVANGELISCH"};

// First-number loop (0x4fc03f..0x4fc067). Reads up to 3 ASCII digits from *p,
// stopping benignly at '_' (the field separator) or at NUL; any other non-digit
// (before the '_'/limit) makes the original return 0. Advances p past the consumed
// digits, leaving it at the breaking char (the '_' separator, the 4th char on the
// >=3 path, or the terminating NUL). Returns the digit count, or -1 on a non-digit.
//
// The original then does `if (!*p) return 0; if (!n) return 0;` followed by an
// UNCONDITIONAL one-char advance past whatever broke the loop (it does NOT validate
// that the separator is '_'). That only ever consumes a non-'_' separator on the
// >=3-digit path, where the first number is >= 100 and the later [1,80] range check
// rejects it regardless — so requiring '_' here is observably identical, and it
// rejects the same inputs the original's `!*p` check would.
int ParseFirstDigits(const char*& p, char out[4]) {
    int n = 0;
    while (*p && *p != '_' && n < 3) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < '0' || c > '9')
            return -1;               // non-digit (not '_') -> original returns 0
        out[n++] = *p;
        ++p;
    }
    out[n] = '\0';
    return n;
}

// Second-number loop (0x4fc085..0x4fc0b0). Distinct semantics from the first field:
// it does NOT stop at '_' — it reads up to 3 digits and RETURNS 0 (failure) on ANY
// non-digit, including a further '_'. It terminates only at the 3-digit limit or at
// NUL (the `if (!*++v11) break;` after each accepted digit). p is left at the first
// non-consumed char. Returns the digit count (1..3), or -1 on a non-digit.
int ParseSecondDigits(const char*& p, char out[4]) {
    int n = 0;
    while (n < 3) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < '0' || c > '9')
            return -1;               // any non-digit (incl. '_') -> original ret 0
        out[n++] = *p;
        if (!*++p)                   // if (!*++v11) break;
            break;
    }
    out[n] = '\0';
    return n;
}

int ParseIntStr(const char* s) {  // VIBE_Util_ParseInt clone (atoi, base 10)
    int v = 0;
    for (; *s >= '0' && *s <= '9'; ++s)
        v = v * 10 + (*s - '0');
    return v;
}
} // namespace

ReligionCheat ParseSetReligion(const char* command) {
    ReligionCheat r{false, 0, 0, 0};
    if (!command || *command != '-')           // if ( *a1 != 45 ) return 0;
        return r;

    const char* p = command + 1;
    int idx = -1;
    for (int i = 0; i < 2; ++i) {               // for ( i=0; i<2; ... )
        std::size_t len = std::strlen(kReligionNames[i]);
        // strncmp stops at a NUL in either string, so a short command (e.g.
        // "-KAT") never reads past its terminator. For a match the first `len`
        // bytes must be identical and the command must continue past them, which
        // is byte-identical to the original prefix compare on valid input (a
        // plain memcmp here would over-read a truncated command buffer).
        if (std::strncmp(p, kReligionNames[i], len) == 0) {
            idx = i;
            break;
        }
    }
    // if ( i ) { if ( i != 1 ) return 0; code=0; } else code=1;
    if (idx < 0)
        return r;
    u8 code = (idx == 0) ? 1 : 0;               // KATHOLISCH->1, EVANGELISCH->0

    p += std::strlen(kReligionNames[idx]);
    if (*p != '_')                              // if ( *v4 != 95 ) return 0;
        return r;
    ++p;

    char num0[4];
    int n0 = ParseFirstDigits(p, num0);
    if (n0 <= 0)                                // if ( !*v6 || !v7 ) return 0;
        return r;
    // Original: `if (!*v6) return 0;` then advances ONE char past the breaker. That
    // breaker is the '_' separator on every input that survives the [1,80] check.
    if (*p != '_')
        return r;
    ++p;                                        // v11 = v6 + 1

    char num1[4];
    int n1 = ParseSecondDigits(p, num1);        // reject-non-digit, no '_' stop
    if (n1 <= 0)                                // if ( result==0 ) return 0;
        return r;

    int level  = ParseIntStr(num0);             // v13
    int region = ParseIntStr(num1);             // v14
    // if ( v15>=1 && v15<=80 && v14>=1 && v14<=8 )
    if (level >= 1 && level <= 80 && region >= 1 && region <= 8) {
        r.valid  = true;
        r.code   = code;
        r.level  = level;
        r.region = region;
    }
    return r;
}

// gilde.exe 0x4fbf70 cost core (0x4fc136..0x4fc151).  VERIFIED-1:1.
// fild count; fild level; fmulp; fmul flt_6207C0(0x3c23d70a, get_bytes-confirmed);
// ConvertX(truncate) ; fistp -> (int)((count*level)*0.00999999978). Left-assoc and
// the (int) truncation match the binary; for the small magnitudes involved the
// 80-bit chain and the C++ double chain never straddle an integer boundary.
int ReligionConversionCost(int matchingPeople, int level) {
    // v17 = (double)v35 * (double)v13 * flt_6207C0;  v35 = (int)v17;
    double cost = static_cast<double>(matchingPeople)
                  * static_cast<double>(level)
                  * static_cast<double>(mood::kReligionCostFrac);
    return static_cast<int>(cost);
}

} // namespace guild::world
