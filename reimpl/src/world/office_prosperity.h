#pragma once
// Office / business "prosperity" per-turn update — the math core of
// VIBE_Amt_UpdateOfficeProsperity (gilde.exe 0x57b718). Once per Amt turn the
// pass walks every active business building, recomputes a prosperity score from
// the owner's wealth + the building's three production-room values vs. a
// city-wide reference maximum, and commits the score delta (object field +480)
// plus a decayed AI-method value (×0.95) through the command lockstep.
//
// The building/person table walk and the network-command commit are sim/command
// owned; recovered here byte-for-byte is the deterministic arithmetic, with the
// commit routed through a settable hook so it is testable in isolation.
//
// Translated:
//   VIBE_Amt_UpdateOfficeProsperity   0x57b718  (prosperity math + commit order)
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered double tuning constants (get_bytes; see comments).
// ===========================================================================
// dbl_6258B4 @0x6258B4 == 0x3FE0000000000000 == 0.5  (the prosperity blend weight,
// used twice: ratio*0.5 + 0.5*(1 - wealth/cityMax)).
constexpr double kProsperityBlend = 0.5;
// dbl_6258BC @0x6258BC == 0x3FEE666666666666 == 0.95 (AI-method decay factor at
// field +180).
constexpr double kProsperityAiDecay = 0.95;

// ===========================================================================
// Prosperity score for one active building.
// ===========================================================================
// Inputs mirror the fields the original reads off the 536-byte building record:
//   ownerWealth : VIBE_Person_ComputeTotalWealth(owner)        (v15)
//   room[3]     : the three production-room values at +468/+472/+476  (v21 loop)
//   cityMax     : the city-wide reference max from FindNextActiveBuilding (v16)
// The math (faithful to the float/double mix in the original):
//   roomSum  = room0+room1+room2;  nonzero = count(room != 0)
//   avg      = (ownerWealth + roomSum) / (nonzero + 1)          (integer divide)
//   ratio    = (float)avg / (float)ownerWealth  clamped to <= 1.0
//   score    = ratio*0.5 + 0.5*(1.0 - (double)ownerWealth/(double)cityMax)
struct ProsperityInput {
    i32 ownerWealth = 0;  // v15
    i32 room[3] = {0,0,0};// +468/+472/+476
    i32 cityMax = 1;      // v16 (reference max; never 0 in the live data)
};

// gilde.exe 0x57b718 — the prosperity score (float result, as stored at +480).
float ProsperityCompute(const ProsperityInput& in);

// The integer "average wealth" intermediate (v25): (wealth + roomSum)/(nonzero+1).
i32 ProsperityAverageWealth(const ProsperityInput& in);

// ===========================================================================
// Per-building commit (the three command emissions the original makes per
// building, in order). Routed through a settable hook so a test can observe the
// exact sequence + values. The originals are:
//   1. AppendDeltaField(4,1,&room[i], off)  x3   (re-write the room values)
//   2. AppendDeltaField(4,1,&wealth, +476)        (write the averaged wealth)
//   3. QueueRequestArgs26(objId, 480, -(old480 - score))  (prosperity delta)
//   4. AiMethod entry 3 = -(field180 - field180*0.95)      (AI decay delta)
// We surface (2) the wealth write, (3) the prosperity delta, and (4) the AI
// delta — the deterministic, value-bearing commits.
// ===========================================================================
enum class ProsperityCommit {
    WealthField,   // field +476 = averaged wealth
    ProsperityDelta, // field +480 += -(old - score)
    AiDecayDelta,    // AI method 3 += -(field180 - field180*0.95)
};

using ProsperityCommitHook = void (*)(ProsperityCommit which, i32 objectId,
                                      float value, void* ctx);
void ProsperitySetCommitHook(ProsperityCommitHook hook, void* ctx);

// One building's full update: computes the score, then emits (when a hook is
// installed) the wealth write, the prosperity delta and the AI decay delta in
// the original's order. `currentField480` is the building's current prosperity
// (v23+480), `aiField180` is the AI value at +180. `objectId` is the record's
// object id (the +4 dword). Returns the computed prosperity score.
struct ProsperityResult {
    float score = 0.0f;     // the new prosperity score (what +480 trends toward)
    i32   averageWealth = 0;// v25
    float prosperityDelta = 0.0f; // -(currentField480 - score)
    float aiDecayDelta = 0.0f;    // -(aiField180 - aiField180*0.95)
};
ProsperityResult ProsperityUpdateBuilding(const ProsperityInput& in,
                                          i32 objectId,
                                          float currentField480,
                                          float aiField180);

} // namespace guild::world
