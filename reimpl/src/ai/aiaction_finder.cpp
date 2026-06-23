#include "ai/aiaction_finder.h"

#include "ai/method.h"            // RandomModulo (the ANSI LCG modulo)
#include "util/math_rng_float.h"  // RandomFloatScaled (@0x58b910)

namespace guild::ai {

// ===========================================================================
// Recovered radius constants (.rdata, byte-faithful via get_bytes).
//   0x42000000 = 32.0, 0x429A0000 = 77.0, 0x42700000 = 60.0,
//   0x42200000 = 40.0, 0x41900000 = 18.0.
// ===========================================================================
const float kRangedPersonBase = 32.0f;  // flt_61AC30
const float kEntityLevelBase  = 32.0f;  // flt_61AC34
const float kTwoInRangeBase   = 77.0f;  // flt_61AC38
const float kFavorabilityCeil = 60.0f;  // flt_61AC3C
const float kBuildingBase     = 40.0f;  // flt_61AC40
const float kWealthyBase      = 18.0f;  // flt_61AC44

// ===========================================================================
// Env plumbing.
// ===========================================================================
namespace {
struct InertEnv final : AiActionFinderEnv {
    u32 SpriteSearchFlags(bool) override { return 0; }
    ScanHit FindMatchingColors(i32, int, u32, float, float) override { return {}; }
    ScanHit FindNearestEntity(i32, int, float, float) override { ScanHit h; h.slotA = 0xFFFF; return h; }
    int FindPeopleByPalette(i32, int, int, float, float, u16*) override { return 0; }
    i32 PersonId(u16) override { return 0; }
    u32 PersonTurnBits(u16) override { return 0; }
    double Favorability(u16, u16) override { return 0.0; }
    int BuildingGroup(i32) override { return 0; }
    double BuildingRatingCurve(i32, u16) override { return 0.0; }
    int FindPairedEntityReverse(i32) override { return 0; }
    int GameTimeFaction() override { return 0; }
    i32 FindRecordById(i32) override { return 0; }
    bool FactionHandlerConflict(u32) override { return false; }
    int CollectPlayerEntities(u16, i32*, i32*) override { return 0; }
    i32 RecPersonId(i32) override { return 0; }
    u8  RecKind(i32) override { return 0; }
    u8  RecActive(i32) override { return 0; }
    u8  RecStateByte(i32) override { return 0; }
    u8  RecFlags457(i32) override { return 0; }
    bool OfficeTier(u8, int&) override { return false; }
};
InertEnv g_inert;
AiActionFinderEnv* g_env = &g_inert;
}  // namespace

void SetAiActionFinderEnv(AiActionFinderEnv* env) { g_env = env ? env : &g_inert; }
AiActionFinderEnv& GetAiActionFinderEnv() { return *g_env; }

// Helper: RandomModulo returns an int but every finder uses the (u16) truncation.
static inline u16 RandU16(u16 n) { return static_cast<u16>(RandomModulo(n)); }

// ===========================================================================
// gilde.exe 0x47b84c — VIBE_AiAction_FindNearbyPerson.
//   capacity < 3 -> 0. Random radius 0..63. On a 1-person hit, write the target
//   id and a group "count" derived from three independent RandomModulo(cap/2)
//   draws (the original's nested compare ladder).
// ===========================================================================
int FindNearbyPerson(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.capacity < 3)
        return 0;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(RandU16(0x40));
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 1, flags, 0.0f, radius);
    if (!hit.found)
        return hit.found;

    out.kind = 7;
    out.targetA = E.PersonId(hit.slotA);
    int half = a.capacity / 2;
    // The original's ladder: if RandomModulo(half) > 1 && RandomModulo(half) >= 5
    // -> count = 5; else if RandomModulo(half) <= 1 -> count = 1; else
    // count = RandomModulo(half). Each branch draws its own RandU16(half).
    if (RandU16(static_cast<u16>(half)) > 1u && RandU16(static_cast<u16>(half)) >= 5u) {
        out.count = 5;
    } else if (RandU16(static_cast<u16>(half)) <= 1u) {
        out.count = 1;
    } else {
        out.count = RandU16(static_cast<u16>(half));
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x47badc — VIBE_AiAction_FindNearbyPersonRanged.
//   capacity < 3 -> 0. Radius 32..49. 1-person hit -> target id.
// ===========================================================================
int FindNearbyPersonRanged(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.capacity < 3)
        return 0;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(static_cast<double>(RandU16(0x12)) + kRangedPersonBase);
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 1, flags, 0.0f, radius);
    if (!hit.found)
        return hit.found;
    out.kind = 7;
    out.targetA = E.PersonId(hit.slotA);
    return 1;
}

// ===========================================================================
// gilde.exe 0x47ba20 — VIBE_AiAction_FindTwoNearbyPeople.
//   No capacity gate. Radius 0..63. 2-person hit -> two target ids.
// ===========================================================================
int FindTwoNearbyPeople(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(RandU16(0x40));
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 2, flags, 0.0f, radius);
    if (!hit.found)
        return hit.found;
    out.kind = 7;
    out.targetA = E.PersonId(hit.slotA);
    out.targetB = E.PersonId(hit.slotB);
    return 1;
}

// ===========================================================================
// gilde.exe 0x47bd18 — VIBE_AiAction_FindTwoPeopleInRange.
//   Radius 77..100, min 33. 2-person hit gated on mutual favorability: succeed
//   only if EITHER favorability(A,B) <= 60 OR favorability(B,A) <= 60.
// ===========================================================================
int FindTwoPeopleInRange(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(static_cast<double>(RandU16(0x18)) + kTwoInRangeBase);
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 2, flags, 33.0f, radius);
    if (!hit.found)
        return hit.found;
    // The original stores each favorability into a 4-byte float slot (fstp
    // [var_10]/[var_C] @0x47bdbc/0x47bdc5) before fld+fcomp against flt_61AC3C.
    // The comparison therefore runs on float-truncated values, not the raw
    // x87 result — model that with an explicit float round-trip.
    float favAB = static_cast<float>(E.Favorability(hit.slotA, hit.slotB));
    float favBA = static_cast<float>(E.Favorability(hit.slotB, hit.slotA));
    if (static_cast<double>(favAB) <= static_cast<double>(kFavorabilityCeil) ||
        static_cast<double>(favBA) <= static_cast<double>(kFavorabilityCeil)) {
        out.kind = 7;
        out.targetA = E.PersonId(hit.slotA);
        out.targetB = E.PersonId(hit.slotB);
        return 1;
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x47bb88 — VIBE_AiAction_FindNearbyEntityByLevel.
//   Burns one RandomModulo(3) draw, probes the current selection (dword_62EB8C)
//   for a kind-4 entity, else a kind-6 entity in radius 32..63. On a hit, opens a
//   Person query and walks the 768-person table from a random offset, matching the
//   first live person whose home-building ptr == the found entity and whose prof
//   byte is 0. Writes the matched slot's inventory id (+2) and the query record's
//   +1 dword.
//
// The selection-probe / Person query / table walk are render+entity coupled; we
// model the net effect through the env: FindNearestEntity returns the matched
// slot, and the table-walk result is delivered as a single env query. To keep the
// translation faithful but isolatable, the walk is delegated to the env via
// FindMatchingColors-style search is NOT applicable here, so this finder is driven
// by FindNearestEntity + a person-table walk the production env implements.
// ===========================================================================
int FindNearbyEntityByLevel(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    // One RNG draw is consumed unconditionally (RandomModulo(3)) — preserve it.
    (void)RandU16(3);
    // Probe the active selection for a kind-4 entity; if none, scan kind-6 in
    // radius 32..63. The original's v11 holds the matched slot (-1 => none).
    ScanHit sel = E.FindNearestEntity(a.recordPtr, 4, 0.0f, 100.0f);
    ScanHit hit = sel;
    if (sel.slotA == 0xFFFF) {
        float radius = static_cast<float>(static_cast<double>(RandU16(0x20)) + kEntityLevelBase);
        hit = E.FindNearestEntity(a.recordPtr, 6, 0.0f, radius);
        if (!hit.found && hit.slotA == 0xFFFF)
            return 0;
    }
    // The person-table walk result is produced by the production env's matching;
    // here we surface the matched person via PersonId(hit.slotA) and require a
    // valid id. (The full 768-row scan is render/Person-query coupled.)
    i32 pid = E.PersonId(hit.slotA);
    if (pid == 0)
        return 0;
    out.kind = 7;
    out.targetA = pid;
    return 1;
}

// ===========================================================================
// gilde.exe 0x47bcd8 — VIBE_AiAction_CheckObjectState  (__thiscall, this=record).
//   Disasm (0x47bcd8): mov eax,[edx+0x161]; sar eax,0x18; call GroupFromCode;
//   cmp al,7; if ==7 { RandomModulo(3); if !=0 -> return 1 }; RandomModulo(4);
//   return (result==0).  The two RandomModulo calls use DISTINCT hard-coded
//   moduli (3 then 4) — the Hex-Rays collapsed them into one phantom `v3`.
//   The group code is `(*(i32*)(record+0x161)) >> 24` (signed sar), low byte,
//   fed to VIBE_BuildingType_GroupFromCode (which the env wraps as BuildingGroup).
//   net: (group==7 && RandomModulo(3)!=0) || RandomModulo(4)==0.
// ===========================================================================
bool CheckObjectState(i32 buildingTypeCode) {
    AiActionFinderEnv& E = *g_env;
    if (E.BuildingGroup(buildingTypeCode) == 7 && RandU16(3) != 0)
        return true;
    return RandU16(4) == 0;
}

// ===========================================================================
// gilde.exe 0x47bf98 — VIBE_AiAction_FindFactionPerson.
//   Abort if the game-time faction phase != actor faction low bits, or the actor
//   already has a partner record (spouseId resolves). Abort if any pending prof-65
//   handler already targets the actor's faction word. Then search for a same-
//   palette person (12-want) in radius 30..100 and pick a random one.
// ===========================================================================
int FindFactionPerson(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (E.GameTimeFaction() != static_cast<int>(a.faction4 & 3u))
        return 0;
    if (E.FindRecordById(a.spouseId) != 0)
        return 0;
    if (E.FactionHandlerConflict(a.faction4))
        return 0;
    // The palette is 3150856 + (isVariantB==0 ? 1 : 0)  (the original's
    // v8 = (*(actor+9)==0) + 3150857). i.e. variantB(actor+9 nonzero) -> 3150857,
    // variantA -> 3150858.
    int palette = (a.isVariantB == 0 ? 1 : 0) + 3150857;
    u16 list[12] = {0};
    int result = E.FindPeopleByPalette(a.recordPtr, 12, palette, 30.0f, 100.0f, list);
    if (result) {
        out.kind = 7;
        u16 pick = list[RandU16(static_cast<u16>(result))];
        out.targetA = E.PersonId(pick);
        return 1;
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x47c23c — VIBE_AiAction_FindNearbyBuilding.
//   capacity < 2 -> 0. Radius 40..71. 1-hit then a paired-entity-reverse gate.
// ===========================================================================
int FindNearbyBuilding(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.capacity < 2)
        return 0;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(static_cast<double>(RandU16(0x20)) + kBuildingBase);
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 1, flags, 0.0f, radius);
    if (!hit.found)
        return hit.found;
    if (!E.FindPairedEntityReverse(a.recordPtr))
        return 0;
    out.kind = 7;
    out.targetA = E.PersonId(hit.slotA);
    return 1;
}

// ===========================================================================
// gilde.exe 0x47c314 — VIBE_AiAction_FindNearbyWealthyTarget.
//   capacity < 3 OR (flags457 & 4) -> 0. Radius 18..49. 1-hit gated on: the
//   target's turn-bits lack 0x400, and ratingCurve + RandomFloatScaled() >= 1.0.
// ===========================================================================
int FindNearbyWealthyTarget(const AiActionActor& a, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.capacity < 3 || (a.flags457 & 4) != 0)
        return 0;
    u32 flags = E.SpriteSearchFlags(a.isVariantB != 0);
    float radius = static_cast<float>(static_cast<double>(RandU16(0x20)) + kWealthyBase);
    ScanHit hit = E.FindMatchingColors(a.recordPtr, 1, flags, 0.0f, radius);
    if (!hit.found)
        return hit.found;
    if ((E.PersonTurnBits(hit.slotA) & 0x400u) != 0)
        return 0;
    // Order (disasm 0x47c3bf..0x47c3fa): RandomFloatScaled() is drawn FIRST and
    // its result is stored to a 4-byte float slot (fstp [var_10] @0x47c3e6);
    // then BuildingRatingCurve is called and `fadd [var_10]` adds the truncated
    // float roll to the 80-bit curve on st0 before `fld1; fcompp` (>= 1.0).
    float roll = static_cast<float>(util::RandomFloatScaled());
    double curve = E.BuildingRatingCurve(a.recordPtr, hit.slotA);
    if (curve + static_cast<double>(roll) >= 1.0) {
        out.kind = 7;
        out.targetA = E.PersonId(hit.slotA);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Shared adjacency walk for FindAdjacentEntitySmall / Large. Both collect the
// player-entity list (ids + adjacency distances), then walk it round-robin from
// index 0 for `count` steps, taking the first entry whose distance >= threshold
// and whose person record passes the state gate.
// ---------------------------------------------------------------------------
static int AdjacencyWalk(const AiActionActor& a, u16 actorTypeWord, int threshold,
                         AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.stateByte == 13)
        return 0;
    i32 ids[32] = {0};
    i32 dist[32] = {0};
    int n = E.CollectPlayerEntities(actorTypeWord, ids, dist);
    if (n <= 0)
        return 0;
    int remaining = n;
    int idx = 0;
    i32 rec = 0;
    while (true) {
        if (threshold <= dist[idx]) {
            rec = E.FindRecordById(ids[idx]);
            if (rec) {
                if (E.RecKind(rec) != 15 && E.RecActive(rec)) {
                    u8 st = E.RecStateByte(rec);
                    if (st != 13 && st != 27 && st != 15 && st != 21)
                        break;
                }
            }
        }
        idx = (idx + 1) % n;
        if (--remaining == 0)
            return 0;
    }
    out.kind = 7;
    out.targetA = E.RecPersonId(rec);
    return 1;
}

// ===========================================================================
// gilde.exe 0x47be88 — VIBE_AiAction_FindAdjacentEntitySmall. threshold 2..5.
// ===========================================================================
int FindAdjacentEntitySmall(const AiActionActor& a, u16 actorTypeWord, AiActionResult& out) {
    int threshold = static_cast<int>(RandU16(4)) + 2;
    return AdjacencyWalk(a, actorTypeWord, threshold, out);
}

// ===========================================================================
// gilde.exe 0x47c094 — VIBE_AiAction_FindAdjacentEntityLarge. threshold 5..12.
// ===========================================================================
int FindAdjacentEntityLarge(const AiActionActor& a, u16 actorTypeWord, AiActionResult& out) {
    int threshold = static_cast<int>(RandU16(8)) + 5;
    return AdjacencyWalk(a, actorTypeWord, threshold, out);
}

// ===========================================================================
// gilde.exe 0x47c164 — VIBE_AiAction_FindEligibleNeighbor.
//   capacity < 2 OR (flags457 & 1) -> 0. Collect the player-entity list (here the
//   roles are swapped vs. the small/large walks: ids in the FIRST array, distances
//   in the SECOND). Walk round-robin, take the first entry with distance >= 4 whose
//   record lacks the +457 &2 bit and whose office tier >= 4.
// ===========================================================================
int FindEligibleNeighbor(const AiActionActor& a, u16 actorTypeWord, AiActionResult& out) {
    AiActionFinderEnv& E = *g_env;
    if (a.capacity < 2)
        return 0;
    if ((a.flags457 & 1) != 0)
        return 0;
    i32 ids[32] = {0};
    i32 dist[32] = {0};
    int n = E.CollectPlayerEntities(actorTypeWord, ids, dist);
    if (n <= 0)
        return 0;
    int remaining = n;
    int idx = 0;
    i32 rec = 0;
    while (true) {
        if (dist[idx] >= 4) {
            rec = E.FindRecordById(ids[idx]);
            if (rec) {
                int tier = 0;
                if ((E.RecFlags457(rec) & 2) == 0 &&
                    E.OfficeTier(E.RecStateByte(rec), tier) && tier >= 4)
                    break;
            }
        }
        --remaining;
        idx = (idx + 1) % n;
        if (remaining == 0)
            return 0;
    }
    out.kind = 7;
    out.targetA = E.RecPersonId(rec);
    return 1;
}

} // namespace guild::ai
