#pragma once
// City mood & religion mechanics — the recoverable DECISION / arithmetic cores of
// the AI-mood, NPC-relation, mood-notify and religion-conversion routines.
//
// Each original is a GUI/command-coupled body; what is recovered here byte-for-byte
// are the arithmetic / clamp / weighted-random selection cores. The command-queue,
// text-render and 3D plumbing are routed through a small hook (mock in tests) or
// dropped where they do not affect the integer result. RNG is the shared CRT LCG
// (guild::crt::RandNext) via guild::util::RandomModulo / RandomFloatScaled — so the
// weighted mood-color selection has a deterministic golden vector under a fixed seed.
//
// Translated functions:
//   VIBE_AiMethod_ComputeMoodLevel    0x467a50  (gauge value -> mood tier 2/6)
//   VIBE_Person_AdjustMoodAndNotify   0x594afc  (clamp a mood delta into [0,100])
//   VIBE_MeisterAi_SelectMoodColor    0x4664d8  (weighted random mood tier pick)
//   VIBE_Cheat_ParseSetReligion       0x4fbf70  (religion-conversion cheat parse+cost)
//
// All floating-point tuning constants are recovered exactly from the binary
// (get_bytes provenance on each constant).
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP tuning constants (byte-for-byte from gilde.exe .rdata).
// ===========================================================================
namespace mood {
// ComputeMoodLevel (0x467a50).
constexpr float kMoodValueScale = 0.25f;  // flt_61A224  0x3e800000
constexpr float kMoodLowThresh  = 2.0f;   // flt_61A228  0x40000000
constexpr float kMoodHighThresh = 6.0f;   // flt_61A22C  0x40c00000

// Person mood bounds (0x594afc).
constexpr int kMoodMin = 0;
constexpr int kMoodMax = 100;

// Religion conversion (0x4fbf70).
constexpr float kReligionCostFrac = 0.009999999776482582f; // flt_6207C0  (0.01)
} // namespace mood

// ===========================================================================
// VIBE_AiMethod_ComputeMoodLevel  (gilde.exe 0x467a50).
// ===========================================================================
// Maps a raw float gauge (the AI method's value at +4) to a mood tier:
//   v = value * 0.25
//   if (v > 2.0 && v >= 6.0)   -> 6   (top tier; the && reads oddly but is faithful)
//   else if (v <= 2.0)         -> 2   (floor tier)
//   else                       -> (int)v  (the scaled value itself, truncated)
// The two VIBE_Coord_ConvertX calls only re-pack the float for the FPU and do not
// change the integer result, so they are folded out.
int ComputeMoodLevel(float gaugeValue);

// NOTE: VIBE_Npc_AdjustRelationByMood (0x56840c) — the mood->relation bump — is
// already translated as guild::sim::NpcAdjustRelationByMood in
// src/sim/npcaction.cpp; reuse that rather than redefining it here.

// ===========================================================================
// VIBE_Person_AdjustMoodAndNotify  (gilde.exe 0x594afc).
// ===========================================================================
// The recoverable clamp core: a signed mood delta `delta` is applied to the
// person's current mood (the signed high byte of *(int*)(person+89), i.e. the
// mood byte at person+92), but the *stored* value is clamped so the result stays
// in [0,100]:
//   d = delta
//   if (cur + delta < 0)    d = -cur          (so cur + d == 0)
//   if (cur + delta > 100)  d = 100 - cur     (so cur + d == 100)
// Returns the clamped delta `d` that the command writes into the mood field.
// (The text-render + entity-notify side-effects are the engine's.)
int ClampMoodDelta(int currentMood, int delta);

// ===========================================================================
// VIBE_MeisterAi_SelectMoodColor  (gilde.exe 0x4664d8).
// ===========================================================================
// The recoverable selection core: a weighted-random pick of two mood tiers from a
// 3-row x 5-column cumulative-threshold table (dword_46640C). Row is chosen from
// the mood class, then a RandomFloatScaled() draw walks the row's 5 cumulative
// thresholds and the first column whose threshold >= the draw selects the tier
// (1..5). A second tier is drawn the same way from a row picked by RandomModulo.
//   v1 = (moodClass==1) ? 0 : (moodClass==2 ? 1 : 2);   // row for primary tier
//   primary = walk row v1 with draw d0 -> 1..5
//   secondRow = (v1==1) ? RandomModulo(3) : RandomModulo(2)
//   second  = walk row secondRow with draw d1 -> 1..5
//   result  = primary | (second << 8)
// The 15-float threshold table is recovered byte-for-byte.
extern const float kMoodColorThresholds[3][5];  // dword_46640C

// Walks one row of the threshold table: returns the 1-based index of the first
// column whose cumulative threshold >= `draw`, or 5 if the draw exceeds all
// (matches the original's fall-through to index 5).
int MoodTierFromDraw(int row, double draw);

// gilde.exe 0x4664d8 — the full two-tier pick, packed as primary | (second<<8).
// `d0`/`d1` are the two RandomFloatScaled() draws; `secondRow` is the RandomModulo
// pick. Exposed parametrically so the packing is testable deterministically.
int SelectMoodColorTiers(u8 moodClass, double d0, int secondRow, double d1);

// gilde.exe 0x4664d8 — convenience wrapper driving the shared CRT LCG end-to-end
// (RandomFloatScaled + RandomModulo) exactly as the original does. Returns the
// packed tier word.
int SelectMoodColorTiers(u8 moodClass);

// ===========================================================================
// VIBE_Cheat_ParseSetReligion  (gilde.exe 0x4fbf70).
// ===========================================================================
// Parses the debug command "-<RELIGION>_<level>_<region>" (e.g. "-KATHOLISCH_40_3"),
// then computes how many conversions it would cost across the city's people.
//
// Religion-name table (aKatholisch @0x4f8d3c, 2 entries, stride 16):
//   index 0 = "KATHOLISCH"  -> religion code 1
//   index 1 = "EVANGELISCH" -> religion code 0
// `level`  (first number) must be in [1,80]; `region` (second) in [1,8].
//
// The cost scan counts every active person record whose type byte <= 4 and whose
// religion byte matches the target code:
//   count = #{ rec : active && rec.typeByte <= 4 && rec.religionByte == code }
//   cost  = (int)( (double)count * (double)level * 0.01 )
// (the original walks word_12CE910 with stride 536; we accept an explicit count of
//  matching people so the cost core is testable without the person table.)
struct ReligionCheat {
    bool valid;     // command well-formed and in range
    u8   code;      // 1 = KATHOLISCH, 0 = EVANGELISCH
    int  level;     // first number, [1,80]
    int  region;    // second number, [1,8]
};

// gilde.exe 0x4fbf70 — parse only (no scan). Returns valid=false on any of the
// original's early-return-0 paths: missing leading '-', unknown religion, missing
// '_' separators, non-digit / overlong (>3) number fields, or out-of-range numbers.
ReligionCheat ParseSetReligion(const char* command);

// gilde.exe 0x4fbf70 — the cost arithmetic: (int)(matchingPeople * level * 0.01).
int ReligionConversionCost(int matchingPeople, int level);

} // namespace guild::world
