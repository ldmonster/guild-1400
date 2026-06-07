#include "test.h"

#include "ai/favorability.h"
#include "ai/needs.h"
#include "ai/intrigue.h"
#include "crt/rand.h"

#include <cmath>

using namespace guild::ai;
using guild::u8;
using guild::u32;
using guild::i32;
using guild::i16;

namespace {

// ---- needs pickers: capture EmitNeedDelta + AdjustStock --------------------
struct CaptureHook : NeedsCommandHook {
    int emitCount = 0;
    int stockCount = 0;
    i32 lastEntity = 0;
    u32 lastNeedWord = 0;
    i16 lastType = 0;
    i32 lastDelta = 0;
    void EmitNeedDelta(i32 entityId, u32 newNeedWord) override {
        ++emitCount; lastEntity = entityId; lastNeedWord = newNeedWord;
    }
    void AdjustStock(i16 type, i32 stockDelta) override {
        ++stockCount; lastType = type; lastDelta = stockDelta;
    }
};

// ---- favorability mock env -------------------------------------------------
struct MockEnv : FavorabilityEnv {
    // Two-person scenario keyed by id (1 = self, 2 = other).
    FavPersonFields Person(int id) override {
        FavPersonFields f;
        if (id == 1) {
            f.workstationBuildingPtr = 10;
            f.officeId = 5;
            f.titleId = 30;
            f.relationByteSelf = 0x0A000000;  // (>>24) == 10
            f.inventoryBase = 100;
            f.factionHigh = 7;
            f.guildBitsLow = 0;
            f.rankHigh = 1;
            f.spouseRecordPtr = 0;
            f.spousePartnerId = 0;
        } else { // other (id 2)
            f.officeId = 6;
            f.factionHigh = 7;  // same faction as self
            f.rankHigh = 1;
            f.spouseRecordPtr = 0;
        }
        return f;
    }
    OfficeDefinition Office(u8 officeId) override {
        OfficeDefinition d;
        if (officeId == 5) { d.kind = 4; d.tier = 0; d.weight = 0.0f; }
        else if (officeId == 6) { d.kind = 0; d.tier = 2; d.weight = 8.0f; }
        return d;
    }
    int WorkstationWorkers(int ptr) override {
        if (ptr == 10) return 3;   // self building
        if (ptr == 20) return 2;   // QueryByGoodType building
        if (ptr == 30) return 4;   // QueryBegin building
        return 0;
    }
    int QueryByGoodType(int /*otherId*/) override { return 20; }
    int QueryBeginWorkers(int /*otherId*/, int /*mapped*/, bool& gateOk) override {
        gateOk = true; return 30;
    }
    int GesetzState() override { return 0; }  // != 2 => no 0.75 scale
    int InventorySlot(int /*base*/, int /*item*/) override { return 1; }
};

bool nearly(double a, double b) { return std::fabs(a - b) < 1e-6; }

} // namespace

// === needs: golden vectors (fixed LCG seed) =================================

TEST(AiFavNeeds, GroupA_PicksNeed2_AtSeed12345) {
    guild::crt::Srand(12345);
    CaptureHook h;
    NeedAgent a; a.type = 7; a.id = 42; a.needWord = 0;
    u8 nid = PickRandomNeedAndClearGroup(a, &h);
    // golden: need 2, mag 4, amount 60, needWord 0x40.
    CHECK_EQ(nid, (u8)2);
    CHECK_EQ(a.needWord, (u32)0x40);
    CHECK_EQ(h.emitCount, 1);
    CHECK_EQ(h.stockCount, 1);
    CHECK_EQ(h.lastEntity, (i32)42);
    CHECK_EQ(h.lastNeedWord, (u32)0x40);
    CHECK_EQ(h.lastType, (i16)7);
    CHECK_EQ(h.lastDelta, (i32)-60);
}

TEST(AiFavNeeds, GroupA_PicksNeed3_WhenSlot0Blocked) {
    guild::crt::Srand(777);
    CaptureHook h;
    NeedAgent a; a.type = 1; a.id = 5; a.needWord = 0xF0; // slot0 (0xF0) ineligible
    u8 nid = PickRandomNeedAndClearGroup(a, &h);
    // golden: need 3, mag 2, amount 40, needWord 0x2f0.
    CHECK_EQ(nid, (u8)3);
    CHECK_EQ(a.needWord, (u32)0x2f0);
    CHECK_EQ(h.lastDelta, (i32)-40);
}

TEST(AiFavNeeds, GroupA_NoneEligible_AllNeedsSet) {
    guild::crt::Srand(2);
    CaptureHook h;
    NeedAgent a; a.type = 1; a.id = 5; a.needWord = 0xFFFFF; // every group bit set
    u8 nid = PickRandomNeedAndClearGroup(a, &h);
    CHECK_EQ(nid, (u8)0);
    CHECK_EQ(h.emitCount, 0);
    CHECK_EQ(h.stockCount, 0);
}

TEST(AiFavNeeds, GroupA_ZeroMag_NoEmit) {
    guild::crt::Srand(1);
    CaptureHook h;
    NeedAgent a; a.type = 1; a.id = 5; a.needWord = 0;
    // golden: picks need 4 but the magnitude draw is 0 -> bail, no emit.
    u8 nid = PickRandomNeedAndClearGroup(a, &h);
    CHECK_EQ(nid, (u8)0);
    CHECK_EQ(h.emitCount, 0);
}

TEST(AiFavNeeds, GroupB_PicksNeed5_AtSeed2) {
    guild::crt::Srand(2);
    CaptureHook h;
    NeedAgent a; a.type = 9; a.id = 3; a.needWord = 0;
    u8 nid = PickRandomNeedAndClearGroupB(a, &h);
    // golden: need 5, mag 1, amount 0 (scale 0), needWord 0x800000.
    CHECK_EQ(nid, (u8)5);
    CHECK_EQ(a.needWord, (u32)0x800000);
    CHECK_EQ(h.lastDelta, (i32)0);
}

TEST(AiFavNeeds, GroupB_PicksNeed6_WhenSlot0Blocked) {
    guild::crt::Srand(777);
    CaptureHook h;
    NeedAgent a; a.type = 9; a.id = 3; a.needWord = 0x1C000; // slot0 (0x1C000) ineligible
    u8 nid = PickRandomNeedAndClearGroupB(a, &h);
    // golden: need 6, mag 2, amount 16, needWord 0x5c000.
    CHECK_EQ(nid, (u8)6);
    CHECK_EQ(a.needWord, (u32)0x5c000);
    CHECK_EQ(h.lastDelta, (i32)-16);
}

TEST(AiFavNeeds, GroupB_ZeroMag_NoEmit) {
    guild::crt::Srand(12345);
    CaptureHook h;
    NeedAgent a; a.type = 9; a.id = 3; a.needWord = 0;
    // golden: need 5 but magnitude draw is 0 -> bail.
    u8 nid = PickRandomNeedAndClearGroupB(a, &h);
    CHECK_EQ(nid, (u8)0);
    CHECK_EQ(h.emitCount, 0);
}

TEST(AiFavNeeds, GroupA_NullHookStillSelects) {
    guild::crt::Srand(12345);
    NeedAgent a; a.type = 7; a.id = 42; a.needWord = 0;
    u8 nid = PickRandomNeedAndClearGroup(a, nullptr);
    CHECK_EQ(nid, (u8)2);
    CHECK_EQ(a.needWord, (u32)0x40);  // selection still mutates the need word
}

// === favorability ===========================================================

TEST(AiFavNeeds, Favorability_SelfIsAlwaysMax) {
    MockEnv env;
    CHECK(nearly(ComputePersonFavorability(3, 3, true, env), 100.0));
}

TEST(AiFavNeeds, Favorability_GoldenScenario) {
    MockEnv env;
    double r = ComputePersonFavorability(1, 2, true, env);
    // golden (python oracle): 90.72549337334931.
    CHECK(nearly(r, 90.72549337334931));
}

TEST(AiFavNeeds, Favorability_DifferentFactionSubtractsWeight) {
    struct E : MockEnv {
        FavPersonFields Person(int id) override {
            FavPersonFields f = MockEnv::Person(id);
            if (id == 2) f.factionHigh = 9;  // differ -> subtract weight
            return f;
        }
    } env;
    double same;
    { MockEnv m; same = ComputePersonFavorability(1, 2, true, m); }
    double diff = ComputePersonFavorability(1, 2, true, env);
    // Same faction adds 8, different subtracts 8 => 16 apart.
    CHECK(nearly(same - diff, 16.0));
}

TEST(AiFavNeeds, Favorability_ClampsToZeroFloor) {
    // Force a strongly negative score via guild-rank penalties and no bonuses.
    struct E : FavorabilityEnv {
        FavPersonFields Person(int id) override {
            FavPersonFields f;
            if (id == 1) {
                f.relationByteSelf = (int)0x80000000;  // (>>24) == -128 -> relTerm -1
                f.guildBitsLow = 0x1C000 | 0x1800000;   // both penalties active
                f.rankHigh = 1;
            } else { f.rankHigh = 2; }  // differ -> first penalty applies
            return f;
        }
        OfficeDefinition Office(u8) override { return {}; }
        int WorkstationWorkers(int) override { return 0; }
        int QueryByGoodType(int) override { return 0; }
        int QueryBeginWorkers(int, int, bool& g) override { g = false; return 0; }
        int GesetzState() override { return 0; }
        int InventorySlot(int, int) override { return 0; }
    } env;
    double r = ComputePersonFavorability(1, 2, false, env);
    CHECK(nearly(r, 0.0));
}

TEST(AiFavNeeds, AverageObjectFavorability_EmptyIsHalf) {
    MockEnv env;
    int ids[3] = {-1, -1, -1};
    auto resolve = [](int) { return -1; };
    CHECK(nearly(AverageObjectFavorability(1, ids, 3, env, resolve), 0.5));
}

TEST(AiFavNeeds, AverageObjectFavorability_AveragesAndScales) {
    MockEnv env;
    int ids[2] = {2, 2};
    auto resolve = [](int raw) { return raw; };  // identity
    // Each entry uses applyLaw=false; favorability(1,2,false) computed via oracle.
    double per = ComputePersonFavorability(1, 2, false, env);
    double expect = (float)((per + per) * 0.01 / 2.0);
    CHECK(nearly(AverageObjectFavorability(1, ids, 2, env, resolve), expect));
}

// === intrigue label evaluators ==============================================

TEST(AiFavNeeds, EvalSlanderLabel_AcceptReject) {
    CHECK_EQ(EvalSlanderLabel(true, 77), (u8)45);
    CHECK_EQ(EvalSlanderLabel(false, 77), (u8)77);
}
