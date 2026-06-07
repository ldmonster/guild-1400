#include "test.h"

#include "ai/aiaction_finder.h"
#include "crt/rand.h"

#include <cstdint>

using namespace guild;
using namespace guild::ai;

// ---------------------------------------------------------------------------
// Deterministic mock env. The search hooks return scripted hits so the finder
// RULES (capacity gates, radius draws, result-record fills, favorability/curve
// gates) can be golden-tested. PersonId(slot) = 1000 + slot so the written ids
// are predictable.
// ---------------------------------------------------------------------------
namespace {
struct MockEnv final : AiActionFinderEnv {
    // scripted scan result
    ScanHit nextMatch{};
    ScanHit nextEntity{};
    int paletteCount = 0;
    u16 paletteList[12] = {0};
    int buildingGroup = 0;
    double favAB = 100.0, favBA = 100.0;
    double ratingCurve = 0.0;
    int pairedEntity = 1;
    int gameTimeFaction = 0;
    i32 spouseRec = 0;
    bool handlerConflict = false;
    u32 turnBits = 0;

    // adjacency
    int collectCount = 0;
    i32 ids[32] = {0};
    i32 dist[32] = {0};
    // record fields keyed by id (very small map)
    struct Rec { i32 id; i32 personId; u8 kind; u8 active; u8 state; u8 flags457; int tier; bool hasTier; };
    Rec recs[8]; int recN = 0;

    u32 lastFlags = 0;
    float lastMin = -1, lastMax = -1;

    u32 SpriteSearchFlags(bool variantB) override { return variantB ? 0x111u : 0x222u; }
    ScanHit FindMatchingColors(i32, int, u32 flags, float mn, float mx) override {
        lastFlags = flags; lastMin = mn; lastMax = mx; return nextMatch;
    }
    ScanHit FindNearestEntity(i32, int, float, float) override { return nextEntity; }
    int FindPeopleByPalette(i32, int, int, float, float, u16* out) override {
        for (int i = 0; i < paletteCount && i < 12; ++i) out[i] = paletteList[i];
        return paletteCount;
    }
    i32 PersonId(u16 slot) override { return 1000 + slot; }
    u32 PersonTurnBits(u16) override { return turnBits; }
    double Favorability(u16 self, u16 other) override {
        return (self < other) ? favAB : favBA;
    }
    int BuildingGroup(i32) override { return buildingGroup; }
    double BuildingRatingCurve(i32, u16) override { return ratingCurve; }
    int FindPairedEntityReverse(i32) override { return pairedEntity; }
    int GameTimeFaction() override { return gameTimeFaction; }
    i32 FindRecordById(i32 id) override {
        for (int i = 0; i < recN; ++i) if (recs[i].id == id) return id;
        // The spouse-probe in FindFactionPerson queries id == actor.spouseId.
        if (recN == 0) return spouseRec;
        return 0;
    }
    bool FactionHandlerConflict(u32) override { return handlerConflict; }
    int CollectPlayerEntities(u16, i32* outIds, i32* outDist) override {
        for (int i = 0; i < collectCount && i < 32; ++i) { outIds[i] = ids[i]; outDist[i] = dist[i]; }
        return collectCount;
    }
    Rec* find(i32 id) { for (int i = 0; i < recN; ++i) if (recs[i].id == id) return &recs[i]; return nullptr; }
    i32 RecPersonId(i32 r) override { Rec* x = find(r); return x ? x->personId : 0; }
    u8  RecKind(i32 r) override { Rec* x = find(r); return x ? x->kind : 0; }
    u8  RecActive(i32 r) override { Rec* x = find(r); return x ? x->active : 0; }
    u8  RecStateByte(i32 r) override { Rec* x = find(r); return x ? x->state : 0; }
    u8  RecFlags457(i32 r) override { Rec* x = find(r); return x ? x->flags457 : 0; }
    bool OfficeTier(u8 state, int& tierOut) override {
        for (int i = 0; i < recN; ++i) if (recs[i].state == state && recs[i].hasTier) { tierOut = recs[i].tier; return true; }
        return false;
    }
};
}  // namespace

// Capacity gates ------------------------------------------------------------
TEST(AiActionFinder, CapacityGates) {
    MockEnv env; SetAiActionFinderEnv(&env);
    AiActionResult out;
    AiActionActor a; a.capacity = 2;  // < 3
    CHECK_EQ(FindNearbyPerson(a, out), 0);
    CHECK_EQ(FindNearbyPersonRanged(a, out), 0);
    a.capacity = 1;                   // < 2
    CHECK_EQ(FindNearbyBuilding(a, out), 0);
    CHECK_EQ(FindEligibleNeighbor(a, 5, out), 0);
    a.capacity = 3; a.flags457 = 4;   // wealthy gate bit
    CHECK_EQ(FindNearbyWealthyTarget(a, out), 0);
    SetAiActionFinderEnv(nullptr);
}

// FindNearbyPerson golden: seed=1 -> radius 6, count 1, target id 1000+slot ----
TEST(AiActionFinder, FindNearbyPersonGolden) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 0, 0};   // found at slot 0
    crt::Srand(1);
    AiActionActor a; a.capacity = 10;   // half = 5
    a.isVariantB = 1;                   // variant B -> flags 0x111
    AiActionResult out;
    int r = FindNearbyPerson(a, out);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)out.kind, 7);
    CHECK_EQ(out.targetA, 1000);
    CHECK_EQ(out.count, 1);             // golden ladder result (seed 1)
    CHECK_EQ(env.lastFlags, 0x111u);    // variant-B flags forwarded
    CHECK(env.lastMax == 6.0f);         // radius draw seed=1
    SetAiActionFinderEnv(nullptr);
}

// FindNearbyPersonRanged golden: seed=1 -> radius 8+32 = 40 ------------------
TEST(AiActionFinder, FindNearbyPersonRangedGolden) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 3, 0};
    crt::Srand(1);
    AiActionActor a; a.capacity = 3; a.isVariantB = 0;  // variant A -> 0x222
    AiActionResult out;
    CHECK_EQ(FindNearbyPersonRanged(a, out), 1);
    CHECK_EQ(out.targetA, 1003);
    CHECK(env.lastMax == 40.0f);
    CHECK_EQ(env.lastFlags, 0x222u);
    SetAiActionFinderEnv(nullptr);
}

// FindTwoNearbyPeople: two targets, radius 6 (seed=1) -----------------------
TEST(AiActionFinder, FindTwoNearbyPeople) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 2, 7};
    crt::Srand(1);
    AiActionActor a; AiActionResult out;
    CHECK_EQ(FindTwoNearbyPeople(a, out), 1);
    CHECK_EQ(out.targetA, 1002);
    CHECK_EQ(out.targetB, 1007);
    CHECK(env.lastMax == 6.0f);
    SetAiActionFinderEnv(nullptr);
}

// FindTwoPeopleInRange favorability gate ------------------------------------
TEST(AiActionFinder, FindTwoPeopleInRangeGate) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 1, 2};
    // Both favorabilities above the 60.0 ceiling -> reject.
    env.favAB = 80.0; env.favBA = 90.0;
    crt::Srand(1);
    AiActionActor a; AiActionResult out;
    CHECK_EQ(FindTwoPeopleInRange(a, out), 0);
    // One below ceiling -> accept.
    env.favAB = 50.0; env.favBA = 90.0;
    crt::Srand(1);
    CHECK_EQ(FindTwoPeopleInRange(a, out), 1);
    CHECK_EQ(out.targetA, 1001);
    CHECK_EQ(out.targetB, 1002);
    CHECK(env.lastMin == 33.0f);
    CHECK(env.lastMax == 77.0f + 14.0f);  // 0x18(=24) draw seed=1 (=14) + 77
    SetAiActionFinderEnv(nullptr);
}

// CheckObjectState: group!=7 path -> !RandU16(4); seed=1 -> 2 -> false -------
TEST(AiActionFinder, CheckObjectState) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.buildingGroup = 0;
    crt::Srand(1);
    CHECK_EQ(CheckObjectState(0, 4), false);   // RandU16(4) seed1 = 2 != 0
    // group==7 with a nonzero first roll -> true (short-circuits second draw)
    env.buildingGroup = 7;
    crt::Srand(1);
    CHECK_EQ(CheckObjectState(0, 4), true);
    SetAiActionFinderEnv(nullptr);
}

// FindFactionPerson: faction-phase + spouse + handler gates -----------------
TEST(AiActionFinder, FindFactionPerson) {
    MockEnv env; SetAiActionFinderEnv(&env);
    AiActionActor a; a.faction4 = 2;  // &3 = 2
    AiActionResult out;
    env.gameTimeFaction = 1;          // mismatch
    CHECK_EQ(FindFactionPerson(a, out), 0);
    env.gameTimeFaction = 2;          // match
    env.spouseRec = 5;                // already partnered -> abort
    CHECK_EQ(FindFactionPerson(a, out), 0);
    env.spouseRec = 0;
    env.handlerConflict = true;       // handler conflict -> abort
    CHECK_EQ(FindFactionPerson(a, out), 0);
    env.handlerConflict = false;
    env.paletteCount = 3; env.paletteList[0] = 4; env.paletteList[1] = 8; env.paletteList[2] = 9;
    crt::Srand(1);
    int r = FindFactionPerson(a, out);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)out.kind, 7);
    // pick = list[RandU16(3) seed1]; RandU16(3) = 16838%3 = 2 -> list[2]=9 -> id 1009
    CHECK_EQ(out.targetA, 1009);
    SetAiActionFinderEnv(nullptr);
}

// FindNearbyBuilding: paired-entity gate ------------------------------------
TEST(AiActionFinder, FindNearbyBuilding) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 1, 0};
    AiActionActor a; a.capacity = 2; AiActionResult out;
    env.pairedEntity = 0;                         // gate fails
    crt::Srand(1);
    CHECK_EQ(FindNearbyBuilding(a, out), 0);
    env.pairedEntity = 1;
    crt::Srand(1);
    CHECK_EQ(FindNearbyBuilding(a, out), 1);
    CHECK_EQ(out.targetA, 1001);
    CHECK(env.lastMax == 40.0f + 6.0f);           // 0x20 draw seed1 (=6) + 40
    SetAiActionFinderEnv(nullptr);
}

// FindNearbyWealthyTarget: turn-bit + rating-curve gate ---------------------
TEST(AiActionFinder, FindNearbyWealthyTarget) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.nextMatch = ScanHit{1, 0, 0};
    AiActionActor a; a.capacity = 3; AiActionResult out;
    env.turnBits = 0x400;                          // skip-bit set -> reject
    crt::Srand(1);
    CHECK_EQ(FindNearbyWealthyTarget(a, out), 0);
    env.turnBits = 0;
    env.ratingCurve = 5.0;                          // curve+roll >= 1 -> accept
    crt::Srand(1);
    CHECK_EQ(FindNearbyWealthyTarget(a, out), 1);
    CHECK_EQ(out.targetA, 1000);
    env.ratingCurve = -5.0;                         // curve+roll < 1 -> reject
    crt::Srand(1);
    CHECK_EQ(FindNearbyWealthyTarget(a, out), 0);
    SetAiActionFinderEnv(nullptr);
}

// Adjacency walks: threshold + state gate -----------------------------------
TEST(AiActionFinder, AdjacencyWalkSmall) {
    MockEnv env; SetAiActionFinderEnv(&env);
    // threshold seed=1 = RandU16(4)+2 = 2+2 = 4. Two entities; first dist<4,
    // second dist>=4 with a passing record.
    env.collectCount = 2;
    env.ids[0] = 10; env.dist[0] = 1;   // below threshold
    env.ids[1] = 11; env.dist[1] = 9;   // ok
    env.recs[0] = {10, 7710, 0, 1, 0, 0, 0, false};
    env.recs[1] = {11, 7711, 0, 1, 0, 0, 0, false};
    env.recN = 2;
    crt::Srand(1);
    AiActionActor a; AiActionResult out;
    CHECK_EQ(FindAdjacentEntitySmall(a, 5, out), 1);
    CHECK_EQ(out.targetA, 7711);
    CHECK_EQ((int)out.kind, 7);
    // state==13 on actor aborts.
    a.stateByte = 13;
    crt::Srand(1);
    CHECK_EQ(FindAdjacentEntitySmall(a, 5, out), 0);
    SetAiActionFinderEnv(nullptr);
}

// Adjacency: a bad state code (e.g. 13) on the candidate record is skipped ---
TEST(AiActionFinder, AdjacencyWalkSkipsBadState) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.collectCount = 2;
    env.ids[0] = 20; env.dist[0] = 30;
    env.ids[1] = 21; env.dist[1] = 30;
    env.recs[0] = {20, 7720, 0, 1, 13, 0, 0, false};  // state 13 -> skip
    env.recs[1] = {21, 7721, 0, 1, 0, 0, 0, false};   // ok
    env.recN = 2;
    crt::Srand(1);
    AiActionActor a; AiActionResult out;
    CHECK_EQ(FindAdjacentEntityLarge(a, 5, out), 1);
    CHECK_EQ(out.targetA, 7721);
    SetAiActionFinderEnv(nullptr);
}

// FindEligibleNeighbor: dist>=4 + office tier>=4 gate ------------------------
TEST(AiActionFinder, FindEligibleNeighbor) {
    MockEnv env; SetAiActionFinderEnv(&env);
    env.collectCount = 2;
    env.ids[0] = 30; env.dist[0] = 2;   // dist < 4 -> skip
    env.ids[1] = 31; env.dist[1] = 5;   // dist >= 4
    env.recs[0] = {30, 7730, 0, 1, 40, 0, 3, true};
    env.recs[1] = {31, 7731, 0, 1, 41, 0, 5, true};  // tier 5 >= 4 -> ok
    env.recN = 2;
    AiActionActor a; a.capacity = 2; AiActionResult out;
    CHECK_EQ(FindEligibleNeighbor(a, 5, out), 1);
    CHECK_EQ(out.targetA, 7731);
    // flags457 &2 on the record disqualifies.
    env.recs[1].flags457 = 2;
    CHECK_EQ(FindEligibleNeighbor(a, 5, out), 0);
    SetAiActionFinderEnv(nullptr);
}
