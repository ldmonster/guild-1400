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
// VIBE_AiMethod_ComputeMoodLevel  (gilde.exe 0x467a50).
// ---------------------------------------------------------------------------
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
// VIBE_Person_AdjustMoodAndNotify  (gilde.exe 0x594afc).
// ---------------------------------------------------------------------------
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

int MoodTierFromDraw(int row, double draw) {
    // v2 = 0; if ( v19 <= (double)v16[5*v1] ) -> index 0; else walk while v2 < 5.
    for (int col = 0; col < 5; ++col) {
        if (draw <= static_cast<double>(kMoodColorThresholds[row][col]))
            return col + 1;          // v21[0] = v2 + 1;
    }
    return 5;                        // loop exits at v2==5 -> last LABEL_6 used v2+1
}

int SelectMoodColorTiers(u8 moodClass, double d0, int secondRow, double d1) {
    // v1 = (v0==1) ? 0 : ((v0!=2)+1);
    int v1 = (moodClass == 1) ? 0 : ((moodClass != 2) + 1);
    int primary = MoodTierFromDraw(v1, d0);          // v21[0]
    int second  = MoodTierFromDraw(secondRow, d1);   // v21[1] = (v4+1) << 8
    return primary | (second << 8);                  // return v21[1] | v21[0];
}

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
// VIBE_Cheat_ParseSetReligion  (gilde.exe 0x4fbf70).
// ---------------------------------------------------------------------------
namespace {
// aKatholisch @0x4f8d3c — 2 entries, stride 16 (NUL-padded).
const char kReligionNames[2][16] = {"KATHOLISCH", "EVANGELISCH"};

// Parse up to 3 ASCII digits from *p, stopping at NUL or '_'; advances p past the
// consumed digits. Returns the count of digits read (0 if none / first char is
// non-digit). Mirrors the original's per-char loop that rejects non-digits.
int ParseDigits(const char*& p, char out[4]) {
    int n = 0;
    while (*p && *p != '_' && n < 3) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < '0' || c > '9')
            return -1;               // non-digit -> original returns 0
        out[n++] = *p;
        ++p;
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
        if (std::memcmp(p, kReligionNames[i], len) == 0) {
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
    int n0 = ParseDigits(p, num0);
    if (n0 <= 0)                                // if ( !*v6 || !v7 ) return 0;
        return r;
    if (*p != '_')                              // separator before second number
        return r;
    ++p;

    char num1[4];
    int n1 = ParseDigits(p, num1);
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

int ReligionConversionCost(int matchingPeople, int level) {
    // v17 = (double)v35 * (double)v13 * flt_6207C0;  v35 = (int)v17;
    double cost = static_cast<double>(matchingPeople)
                  * static_cast<double>(level)
                  * static_cast<double>(mood::kReligionCostFrac);
    return static_cast<int>(cost);
}

} // namespace guild::world
