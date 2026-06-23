#pragma once
// AiAction target-FINDER cluster of the Guild simulation (gilde.exe). These are
// the per-NPC "pick the next interaction target" leaf functions the AiAction
// planner runs while deciding what an NPC should do next: each scans the world
// for a candidate person/building/entity, applies a need/eligibility gate, and on
// success writes a small result record {kind=7, targetId[, count]} that the
// action-execution loop then carries into a concrete NpcAction.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x47b84c VIBE_AiAction_FindNearbyPerson        — 1 person, random radius 0..64
//   0x47badc VIBE_AiAction_FindNearbyPersonRanged  — 1 person, radius 32..50
//   0x47ba20 VIBE_AiAction_FindTwoNearbyPeople     — 2 people, radius 0..64
//   0x47bd18 VIBE_AiAction_FindTwoPeopleInRange    — 2 people, min 33, favorability gate
//   0x47bb88 VIBE_AiAction_FindNearbyEntityByLevel — entity scan + person table walk
//   0x47bcd8 VIBE_AiAction_CheckObjectState        — building-group / random gate
//   0x47bf98 VIBE_AiAction_FindFactionPerson       — same-faction person by palette
//   0x47c23c VIBE_AiAction_FindNearbyBuilding      — 1 building (paired-entity gate)
//   0x47c314 VIBE_AiAction_FindNearbyWealthyTarget — 1 person, rating-curve gate
//   0x47be88 VIBE_AiAction_FindAdjacentEntitySmall — adjacency walk, threshold 2..5
//   0x47c094 VIBE_AiAction_FindAdjacentEntityLarge — adjacency walk, threshold 5..12
//   0x47c164 VIBE_AiAction_FindEligibleNeighbor    — adjacency walk, office gate
//
// All RNG goes through guild::ai::RandomModulo (the ANSI LCG; determinism-critical
// and golden-tested). The cross-cluster leaves (ObjectSearch_*, He_Collect*,
// Render flags, favorability, building/office defs, person-record lookup) are
// routed through AiActionFinderEnv so the FINDER RULES are exercisable in
// isolation; a production backend binds the env to the real sim globals.
#include "guild/common/types.h"

namespace guild::ai {

// ===========================================================================
// Recovered search-radius constants (.rdata, get_bytes @0x61AC30..0x61AC44).
//   flt_61AC30 = 32.0  (FindNearbyPersonRanged base)
//   flt_61AC34 = 32.0  (FindNearbyEntityByLevel base)
//   flt_61AC38 = 77.0  (FindTwoPeopleInRange base)
//   flt_61AC3C = 60.0  (FindTwoPeopleInRange favorability ceiling)
//   flt_61AC40 = 40.0  (FindNearbyBuilding base)
//   flt_61AC44 = 18.0  (FindNearbyWealthyTarget base)
// ===========================================================================
extern const float kRangedPersonBase;   // flt_61AC30
extern const float kEntityLevelBase;    // flt_61AC34
extern const float kTwoInRangeBase;     // flt_61AC38
extern const float kFavorabilityCeil;   // flt_61AC3C
extern const float kBuildingBase;       // flt_61AC40
extern const float kWealthyBase;        // flt_61AC44

// ===========================================================================
// The action context the finders run against. `actor` is the NPC person record
// pointer (the original's a2@ecx). The fields below mirror exactly the offsets
// the originals read off that record; the env supplies them so a finder is
// runnable without the full 536-byte record laid out in memory.
//   capacity  : *(int*)(actor+404) — action "budget"; gates most finders (<2/<3).
//   isVariantB: *(u8*)(actor+9)    — palette/variant flag (0 => +2, else +1 on flags).
//   flags457  : *(u8*)(actor+457)  — per-actor gate bits (&1 / &2 / &4).
//   stateByte : *(u8*)(actor+358)  — office/state code (==13 aborts adjacency walks).
//   factionLow: *(u32*)(actor+4)&7 — packed faction word low bits (faction gate).
//   spouseId  : *(u32*)(actor+92)  — partner id (FindFactionPerson aborts if set).
// ===========================================================================
struct AiActionActor {
    i32 capacity  = 0;   // *(int*)(actor+404)
    u8  isVariantB = 0;  // *(u8*)(actor+9)
    u8  flags457  = 0;   // *(u8*)(actor+457)
    u8  stateByte = 0;   // *(u8*)(actor+358)
    u32 faction4  = 0;   // *(u32*)(actor+4)
    i32 spouseId  = 0;   // *(u32*)(actor+92)
    i32 recordPtr = 0;   // the raw actor pointer (passed through to env searches)
};

// The result record a finder fills on success (the original's a3 output). All
// finders write byte[0]=7 (the "interaction" kind tag) plus 1-2 target person ids
// at +4/+8 and an optional count at +8.
struct AiActionResult {
    u8  kind = 0;      // +0  (set to 7 on success)
    i32 targetA = 0;   // +4  primary target person id
    i32 targetB = 0;   // +8  secondary target person id (two-person finders)
    i32 count = 0;     // +8  group-size count (FindNearbyPerson overloads +8)
};

// A scan hit: the matching slot index plus (for the 2-person scans) a second
// slot, exactly as ObjectSearch_FindMatchingColors returns them.
struct ScanHit {
    int found = 0;     // the function's int return (0 => no match)
    u16 slotA = 0;     // LOWORD(out) — primary person slot
    u16 slotB = 0;     // HIWORD(out) — secondary person slot (2-person scans)
};

// ===========================================================================
// Environment: the cross-cluster leaves the finders call. Null hooks return the
// documented neutral default (search => no hit). The production backend wires
// these to the sim globals/object-search subsystem; tests use a mock.
// ===========================================================================
struct AiActionFinderEnv {
    virtual ~AiActionFinderEnv() = default;

    // VIBE_Render_EncodeSpriteDrawFlags(dword_62EB8C != 0, 2) | 0x3C0500, with the
    // low byte OR'd by 2 (variant A) or 1 (variant B). The original branches on
    // *(actor+9); we fold that in. Returns the assembled search-flags word.
    virtual u32 SpriteSearchFlags(bool variantB) = 0;

    // VIBE_ObjectSearch_FindMatchingColors(actor, want, flags, min, max, &out).
    // `want` is 1 or 2 (person count). Returns {found, slotA, slotB}.
    virtual ScanHit FindMatchingColors(i32 actorPtr, int want, u32 flags,
                                       float minDist, float maxDist) = 0;

    // VIBE_ObjectSearch_FindNearestEntity(target, kind, palette, min, max, &out).
    // Returns the matched slot (-1 => none); `found` is the function's int return.
    virtual ScanHit FindNearestEntity(i32 targetPtr, int kind, float minDist,
                                      float maxDist) = 0;

    // VIBE_ObjectSearch_FindPeopleByPalette(actor, want, palette, min, max, &out16).
    // Fills the caller's 12-slot u16 list; returns the match count (the original's
    // `result`). `out` receives up to `result` slots.
    virtual int FindPeopleByPalette(i32 actorPtr, int want, int palette,
                                    float minDist, float maxDist, u16* out) = 0;

    // dword_12CE914[134*slot] — the person-id column (stride 134 dwords).
    virtual i32 PersonId(u16 slot) = 0;
    // dword_12CEAD8[134*slot] — the per-person turn-bit column (& 0x400 gate).
    virtual u32 PersonTurnBits(u16 slot) = 0;

    // VIBE_Ai_ComputePersonFavorability(self, other, applyLaw=1).
    virtual double Favorability(u16 self, u16 other) = 0;

    // VIBE_BuildingType_GroupFromCode(buildingPtr): the building group code.
    virtual int BuildingGroup(i32 buildingPtr) = 0;
    // VIBE_Building_ComputeRatingCurveA(3, actor, &inventory[268*slot]).
    virtual double BuildingRatingCurve(i32 actorPtr, u16 slot) = 0;
    // VIBE_CharAction_FindPairedEntityReverse(actor): nonzero => paired entity ok.
    virtual int FindPairedEntityReverse(i32 actorPtr) = 0;

    // (int)qword_13CE852 % 4 — packed game-time low dword mod 4 (faction gate).
    virtual int GameTimeFaction() = 0;
    // VIBE_Person_FindRecordById(id): a person record (0 => none).
    virtual i32 FindRecordById(i32 personId) = 0;
    // FindFactionPerson aborts if FindRecordById(actor.spouseId) != 0.
    // He pending-handler scan: any prof-65 handler whose +43/+44 dword equals the
    // actor's faction word => abort. Returns true if such a conflict exists.
    virtual bool FactionHandlerConflict(u32 faction4) = 0;

    // VIBE_He_CollectPlayerEntitiesByType(&ids[32], &dists[32], WORD(actor)): the
    // adjacency scan. Fills the two 32-slot arrays; returns the entry count.
    virtual int CollectPlayerEntities(u16 actorTypeWord, i32* outIds, i32* outDist) = 0;

    // Per-record fields the adjacency walks read off VIBE_Person_FindRecordById:
    //   +1 (dword) = the record's person id (written into the result).
    //   +2 (byte)  = kind code (==15 => skip).
    //   +8 (byte)  = active flag (0 => skip).
    //   +358(byte) = state code (13/27/15/21 => skip in small/large walks).
    //   +457(byte) = gate bits (&2 => skip in eligible-neighbor walk).
    virtual i32 RecPersonId(i32 recPtr) = 0;
    virtual u8  RecKind(i32 recPtr) = 0;
    virtual u8  RecActive(i32 recPtr) = 0;
    virtual u8  RecStateByte(i32 recPtr) = 0;
    virtual u8  RecFlags457(i32 recPtr) = 0;

    // VIBE_Office_GetDefinition(stateByte, &def): true on found; BYTE2(def[0]) is
    // the office tier (the eligible-neighbor walk requires tier >= 4).
    virtual bool OfficeTier(u8 stateByte, int& tierOut) = 0;
};

void SetAiActionFinderEnv(AiActionFinderEnv* env);
AiActionFinderEnv& GetAiActionFinderEnv();

// ===========================================================================
// The finders. Each returns 1 on a successful target (with `out` filled) else 0.
// ===========================================================================

// gilde.exe 0x47b84c — VIBE_AiAction_FindNearbyPerson.
int FindNearbyPerson(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47badc — VIBE_AiAction_FindNearbyPersonRanged.
int FindNearbyPersonRanged(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47ba20 — VIBE_AiAction_FindTwoNearbyPeople.
int FindTwoNearbyPeople(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47bd18 — VIBE_AiAction_FindTwoPeopleInRange.
int FindTwoPeopleInRange(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47bb88 — VIBE_AiAction_FindNearbyEntityByLevel.
int FindNearbyEntityByLevel(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47bcd8 — VIBE_AiAction_CheckObjectState (__thiscall, this=record).
//   net: (BuildingGroup(code)==7 && RandomModulo(3)!=0) || RandomModulo(4)==0.
// The two RandomModulo calls use DISTINCT hard-coded moduli (3 then 4) — verified
// against disasm (mov eax,3 @0x47bcea; mov eax,4 @0x47bcf9). `buildingTypeCode` is
// the caller-derived (*(i32*)(record+0x161))>>24 byte fed to GroupFromCode.
// Returns 0/1.
bool CheckObjectState(i32 buildingTypeCode);

// gilde.exe 0x47bf98 — VIBE_AiAction_FindFactionPerson.
int FindFactionPerson(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47c23c — VIBE_AiAction_FindNearbyBuilding.
int FindNearbyBuilding(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47c314 — VIBE_AiAction_FindNearbyWealthyTarget.
int FindNearbyWealthyTarget(const AiActionActor& actor, AiActionResult& out);

// gilde.exe 0x47be88 — VIBE_AiAction_FindAdjacentEntitySmall.
//   actorTypeWord = WORD(actor); thresholds drawn from RandomModulo(4)+2.
int FindAdjacentEntitySmall(const AiActionActor& actor, u16 actorTypeWord,
                            AiActionResult& out);

// gilde.exe 0x47c094 — VIBE_AiAction_FindAdjacentEntityLarge.
//   thresholds drawn from RandomModulo(8)+5.
int FindAdjacentEntityLarge(const AiActionActor& actor, u16 actorTypeWord,
                            AiActionResult& out);

// gilde.exe 0x47c164 — VIBE_AiAction_FindEligibleNeighbor.
int FindEligibleNeighbor(const AiActionActor& actor, u16 actorTypeWord,
                         AiActionResult& out);

} // namespace guild::ai
