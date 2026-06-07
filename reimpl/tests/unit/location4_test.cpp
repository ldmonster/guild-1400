// Unit tests for world/location4 — the kidnap/customs/detain/spy/dungeon-bribe/
// information/training dialog bodies. Golden vectors over the recovered gating +
// batch-assembly logic; the GUI form shell is scripted via a Recorder hook set.
#include "world/location4.h"

#include <climits>
#include <tuple>
#include <vector>

#include "test.h"

using namespace guild::world;

namespace {

SlotTableView MakeSlots(std::vector<std::uint8_t> occ, std::vector<std::int32_t> id) {
    SlotTableView t;
    t.occupied = std::move(occ);
    t.id = std::move(id);
    return t;
}

SelectionTable MakeSel(std::initializer_list<std::tuple<bool, bool, std::int32_t>> rows) {
    SelectionTable s{};
    int i = 0;
    for (auto& r : rows) {
        if (i >= kSelectionSlots) break;
        s[i].present = std::get<0>(r);
        s[i].active  = std::get<1>(r);
        s[i].id      = std::get<2>(r);
        ++i;
    }
    return s;
}

// Records the GUI-shell (location3 LocationDialogHooks) AND the location4-specific
// side-effect hooks, and scripts the frameStep click sequence.
struct Rec {
    LocationDialogHooks  gui{};
    LocationDialog4Hooks ex{};
    int formsOpened = 0, formsDestroyed = 0;
    int batchCode = -1, batchCount = -1;
    std::vector<std::int32_t> batchIds;
    int voiceCalls = 0;
    std::vector<int> messages;
    // location4-specific observations
    int buildOpTarget = -999, buildOpOp = -999;
    int violKind = -1, violSev = -1, violPerp = -1, violTarget = -1;
    int bribeJailer = -1, bribeCity = -1, bribeAmount = -1;
    int infoPerp = -1, infoBuilding = -999;
    int trainingCommits = 0;
    // configurable returns
    bool skillOk = true;
    int  existingHandlers = 0;
    std::int32_t selBuilding = -1;
    int  dragCount = 0;
    // frameStep script
    std::vector<std::int32_t> frames;
    std::size_t frameIdx = 0;

    static Rec* g;
    Rec() {
        g = this;
        gui.openForm    = [](const char*) -> std::int32_t { ++g->formsOpened; return 7; };
        gui.destroyForm = [](std::int32_t) { ++g->formsDestroyed; };
        gui.frameStep   = [](std::int32_t) -> std::int32_t {
            if (g->frameIdx >= g->frames.size()) return INT32_MIN;
            return g->frames[g->frameIdx++];
        };
        gui.queueBatch  = [](int code, int count, const std::int32_t* ids, int n) {
            g->batchCode = code; g->batchCount = count;
            g->batchIds.assign(ids, ids + n);
        };
        gui.showMessage = [](int t) { g->messages.push_back(t); };
        gui.playFavorVoice = [] { ++g->voiceCalls; };

        ex.checkSkillRequirement = [](int) -> bool { return g->skillOk; };
        ex.requestBuildOp = [](std::int32_t t, int op) { g->buildOpTarget = t; g->buildOpOp = op; };
        ex.evaluateViolation = [](int k, int s, std::int32_t p, std::int32_t t, int) {
            g->violKind = k; g->violSev = s; g->violPerp = p; g->violTarget = t; };
        ex.countExistingHandlers = []() -> int { return g->existingHandlers; };
        ex.enqueueBribe = [](std::int32_t j, std::int32_t c, int a) {
            g->bribeJailer = j; g->bribeCity = c; g->bribeAmount = a; };
        ex.selectedBuilding = []() -> std::int32_t { return g->selBuilding; };
        ex.queueInfoRequest = [](std::int32_t p, std::int32_t b) {
            g->infoPerp = p; g->infoBuilding = b; };
        ex.dragSlotCount = []() -> int { return g->dragCount; };
        ex.commitTrainingItem = [](int) { ++g->trainingCommits; };

        SetLocationDialogHooks(&gui);
        SetLocationDialog4Hooks(&ex);
    }
    ~Rec() { SetLocationDialogHooks(nullptr); SetLocationDialog4Hooks(nullptr); }
    void confirm() { frames = {1}; frameIdx = 0; }
    void cancel()  { frames = {2}; frameIdx = 0; }
};
Rec* Rec::g = nullptr;

} // namespace

// =========================================================================
// ThiefKidnapDialog (0x52554c)
// =========================================================================
TEST(Location4Kidnap, NoTargetReturnsEarly) {
    Rec r;
    auto o = ThiefKidnapDialog(/*hasTarget=*/false, false, false, 10, 20, MakeSlots({}, {}));
    CHECK(!o.opened);
    CHECK(!o.committed);
    CHECK_EQ(r.formsOpened, 0);
}

TEST(Location4Kidnap, SkillGateBlocks) {
    Rec r; r.skillOk = false;
    auto o = ThiefKidnapDialog(true, false, false, 10, 20, MakeSlots({}, {}));
    CHECK(!o.opened);
    CHECK_EQ(r.formsOpened, 0);
}

TEST(Location4Kidnap, CaptiveTargetReturnsEarly) {
    Rec r;
    auto o = ThiefKidnapDialog(true, /*captive=*/true, false, 10, 20, MakeSlots({}, {}));
    CHECK(!o.opened);
}

TEST(Location4Kidnap, BlockedFlagShowsMsg5577) {
    Rec r;
    auto o = ThiefKidnapDialog(true, false, /*blocked=*/true, 10, 20, MakeSlots({}, {}));
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
    if (!r.messages.empty()) CHECK_EQ(r.messages[0], kMsgKidnapBlocked);
}

TEST(Location4Kidnap, ConfirmQueuesBatchAndLawSideEffects) {
    Rec r; r.confirm();
    auto t = MakeSlots({0, 1, 1, 0, 1}, {100, 101, 102, 103, 104});
    auto o = ThiefKidnapDialog(true, false, false, /*perp=*/55, /*cityFirstId=*/777, t);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.action, 60);
    CHECK_EQ(o.count, 3);              // slots 1,2,4 occupied
    CHECK_EQ(r.batchCode, 60);
    CHECK_EQ(r.batchIds.size(), 3u);
    if (r.batchIds.size() == 3u) {
        CHECK_EQ(r.batchIds[0], 101);
        CHECK_EQ(r.batchIds[1], 102);
        CHECK_EQ(r.batchIds[2], 104);
    }
    CHECK_EQ(r.buildOpTarget, 777);
    CHECK_EQ(r.buildOpOp, -3);
    CHECK_EQ(r.violKind, 25);
    CHECK_EQ(r.violSev, 1);
    CHECK_EQ(r.violPerp, 55);
    CHECK_EQ(r.violTarget, 777);
    CHECK_EQ(r.voiceCalls, 1);
    CHECK_EQ(r.formsDestroyed, 1);
}

TEST(Location4Kidnap, ConfirmEmptyTableShowsMsgNoCommit) {
    Rec r; r.confirm();
    auto t = MakeSlots(std::vector<std::uint8_t>(5, 0), std::vector<std::int32_t>(5, 0));
    auto o = ThiefKidnapDialog(true, false, false, 1, 2, t);
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(o.count, 0);
    CHECK_EQ(r.batchCode, -1);          // no batch queued
    CHECK_EQ(r.messages.size(), 1u);    // dword_8C8E78
}

TEST(Location4Kidnap, CancelClosesNoBatch) {
    Rec r; r.cancel();
    auto t = MakeSlots({1, 1}, {9, 8});
    auto o = ThiefKidnapDialog(true, false, false, 1, 2, t);
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(r.formsDestroyed, 1);
}

// =========================================================================
// GuardCustomsDialog (0x5268a8)
// =========================================================================
TEST(Location4Customs, NoTargetEarly) {
    Rec r;
    auto o = GuardCustomsDialog(false, MakeSlots({1}, {1}), MakeSel({}));
    CHECK(!o.opened);
}

TEST(Location4Customs, FullTableShowsMsg) {
    Rec r;
    auto t = MakeSlots(std::vector<std::uint8_t>(10, 0), std::vector<std::int32_t>(10, 0));
    auto o = GuardCustomsDialog(true, t, MakeSel({}));
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
}

TEST(Location4Customs, ConfirmCollectsSelectionCap6) {
    Rec r; r.confirm();
    auto t = MakeSlots({1, 0}, {5, 6});                 // not full
    // 8 active selections; cap is 6.
    auto sel = MakeSel({{true,true,11},{true,true,12},{true,false,13},
                        {true,true,14},{true,true,15},{true,true,16},
                        {true,true,17},{true,true,18}});
    auto o = GuardCustomsDialog(true, t, sel);
    CHECK(o.opened);
    CHECK_EQ(o.action, 101);
    CHECK_EQ(o.count, 6);                                // capped at 6 active ids
    CHECK_EQ(r.batchCode, 101);
    CHECK_EQ(r.batchIds.size(), 6u);
    if (r.batchIds.size() == 6u) {
        CHECK_EQ(r.batchIds[0], 11);
        // active ids in order: 11,12,14,15,16,17 (id 13 inactive skipped); cap hits
        // at 6 BEFORE reaching 18, so the 6th collected is 17.
        CHECK_EQ(r.batchIds[5], 17);
    }
    CHECK_EQ(r.voiceCalls, 1);
}

TEST(Location4Customs, ConfirmNoSelectionNoBatch) {
    Rec r; r.confirm();
    auto t = MakeSlots({1, 0}, {5, 6});
    auto o = GuardCustomsDialog(true, t, MakeSel({{true,false,1}}));
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(r.batchCode, -1);
}

// =========================================================================
// GuardDetainDialog (0x526b34)
// =========================================================================
TEST(Location4Detain, AlreadyDetainedFlag) {
    Rec r;
    auto t = MakeSlots({1}, {1});
    auto o = GuardDetainDialog(true, /*alreadyDetained=*/true, t, MakeSel({}));
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
}

TEST(Location4Detain, ConfirmTakesFirstActiveSingle) {
    Rec r; r.confirm();
    auto t = MakeSlots({1, 0}, {1, 2});
    auto sel = MakeSel({{true,false,10},{true,true,20},{true,true,30}});
    auto o = GuardDetainDialog(true, false, t, sel);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.action, 100);
    CHECK_EQ(o.count, 1);                                // single, first active
    CHECK_EQ(r.batchIds.size(), 1u);
    if (!r.batchIds.empty()) CHECK_EQ(r.batchIds[0], 20);
    // 2 active -> ">1 selected" warning fired
    CHECK_EQ(r.messages.size(), 1u);
}

TEST(Location4Detain, ConfirmSingleActiveNoWarning) {
    Rec r; r.confirm();
    auto t = MakeSlots({1}, {1});
    auto sel = MakeSel({{true,true,42}});
    auto o = GuardDetainDialog(true, false, t, sel);
    CHECK(o.committed);
    CHECK_EQ(o.count, 1);
    CHECK_EQ(r.messages.size(), 0u);                     // only 1 active, no warn
}

// =========================================================================
// ThiefSpyBuildingDialog (0x524380)
// =========================================================================
TEST(Location4Spy, BusyGateBlocks) {
    Rec r;
    auto o = ThiefSpyBuildingDialog(true, /*targetBusy=*/false, 0, 4, MakeSlots({1}, {1}));
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
    if (!r.messages.empty()) CHECK_EQ(r.messages[0], 5791);
}

TEST(Location4Spy, CapacityGateBlocks) {
    Rec r; r.existingHandlers = 3;
    // existing(3) + occupied(2) = 5 >= 2*capacity(2)=4 -> msg 5641
    auto o = ThiefSpyBuildingDialog(true, true, /*occupied=*/2, /*capacity=*/2, MakeSlots({1}, {1}));
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
    if (!r.messages.empty()) CHECK_EQ(r.messages[0], 5641);
}

TEST(Location4Spy, ConfirmCollectsOccupiedCap8) {
    Rec r; r.confirm(); r.existingHandlers = 0;
    // 10 occupied slots; cap 8.
    std::vector<std::uint8_t> occ(10, 1);
    std::vector<std::int32_t> id;
    for (int i = 0; i < 10; ++i) id.push_back(200 + i);
    auto t = MakeSlots(occ, id);
    auto o = ThiefSpyBuildingDialog(true, true, /*occupied=*/0, /*capacity=*/100, t);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.action, 64);
    CHECK_EQ(o.count, 8);
    CHECK_EQ(r.batchIds.size(), 8u);
    if (r.batchIds.size() == 8u) {
        CHECK_EQ(r.batchIds[0], 200);
        CHECK_EQ(r.batchIds[7], 207);
    }
}

// =========================================================================
// DungeonBribeDialog (0x523d1c)
// =========================================================================
TEST(Location4Bribe, NoJailerOfficeEarly) {
    Rec r;
    auto o = DungeonBribeDialog(false, true, false, 1, 2, 100);
    CHECK(!o.opened);
    CHECK_EQ(r.formsOpened, 0);
}

TEST(Location4Bribe, NoJailerRecordShowsMsg) {
    Rec r;
    auto o = DungeonBribeDialog(true, /*record=*/false, false, 1, 2, 100);
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
}

TEST(Location4Bribe, PendingShowsMsg5573) {
    Rec r;
    auto o = DungeonBribeDialog(true, true, /*pending=*/true, 1, 2, 100);
    CHECK(!o.opened);
    CHECK_EQ(r.messages.size(), 1u);
    if (!r.messages.empty()) CHECK_EQ(r.messages[0], kMsgDungeonExists);
}

TEST(Location4Bribe, ConfirmEnqueuesBribe) {
    Rec r; r.confirm();
    auto o = DungeonBribeDialog(true, true, false, /*jailer=*/88, /*city=*/3, /*amount=*/4500);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.action, 57);
    CHECK_EQ(r.bribeJailer, 88);
    CHECK_EQ(r.bribeCity, 3);
    CHECK_EQ(r.bribeAmount, 4500);
    CHECK_EQ(r.batchCode, 57);
}

// =========================================================================
// ThiefInformationDialog (0x52481c)
// =========================================================================
TEST(Location4Info, ConfirmWithSelectionSubmits) {
    Rec r; r.confirm(); r.selBuilding = 314;
    auto o = ThiefInformationDialog(/*perp=*/77);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(r.infoPerp, 77);
    CHECK_EQ(r.infoBuilding, 314);
}

TEST(Location4Info, ConfirmNoSelectionNoSubmit) {
    Rec r; r.confirm(); r.selBuilding = -1;
    auto o = ThiefInformationDialog(77);
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(r.infoBuilding, -999);                     // queueInfoRequest never called
}

// =========================================================================
// ThiefTrainingDialog (0x5253c0)
// =========================================================================
TEST(Location4Training, SuppressedWhenActiveCharFlag) {
    Rec r;
    auto o = ThiefTrainingDialog(/*activeCharFlag=*/true);
    CHECK(!o.opened);
    CHECK_EQ(r.formsOpened, 0);
}

TEST(Location4Training, ConfirmCommitsUsedSlots) {
    Rec r; r.confirm(); r.dragCount = 4;
    auto o = ThiefTrainingDialog(false);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.count, 4);
    CHECK_EQ(r.trainingCommits, 4);
}

TEST(Location4Training, ConfirmEmptyGridNoCommit) {
    Rec r; r.confirm(); r.dragCount = 0;
    auto o = ThiefTrainingDialog(false);
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(r.trainingCommits, 0);
}
