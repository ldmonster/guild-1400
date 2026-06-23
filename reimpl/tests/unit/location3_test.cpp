// Unit tests for world/location3 — the thief/guard/robber-camp dialog bodies.
#include "world/location3.h"

#include <climits>
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

// A scripted hooks instance recording everything the dialogs do.
struct Recorder {
    LocationDialogHooks hooks{};
    int formsOpened = 0, formsDestroyed = 0;
    int batchCode = -1, batchCount = -1;
    std::vector<std::int32_t> batchIds;
    int voiceCalls = 0;
    std::vector<int> messages;
    int changeActions = 0;
    // frameStep script: list of ids to return on successive frames.
    std::vector<std::int32_t> frames;
    std::size_t frameIdx = 0;

    static Recorder* g;
    Recorder() {
        g = this;
        hooks.openForm    = [](const char*) -> std::int32_t { ++g->formsOpened; return 7; };
        hooks.destroyForm = [](std::int32_t) { ++g->formsDestroyed; };
        hooks.frameStep   = [](std::int32_t) -> std::int32_t {
            if (g->frameIdx >= g->frames.size()) return INT32_MIN;
            return g->frames[g->frameIdx++];
        };
        hooks.queueBatch  = [](int code, int count, const std::int32_t* ids, int n) {
            g->batchCode = code; g->batchCount = count;
            g->batchIds.assign(ids, ids + n);
        };
        hooks.showMessage = [](int t) { g->messages.push_back(t); };
        hooks.playFavorVoice = [] { ++g->voiceCalls; };
        hooks.changePlayerAction = [](std::int32_t, std::int32_t) { ++g->changeActions; };
        SetLocationDialogHooks(&hooks);
    }
    ~Recorder() { SetLocationDialogHooks(nullptr); }
    void confirm() { frames = {1}; frameIdx = 0; }  // one frame: click "yes"
    void cancel()  { frames = {2}; frameIdx = 0; }  // click something else then end
};
Recorder* Recorder::g = nullptr;

} // namespace

// ---- FirstOccupiedSlot / SlotTableFull ------------------------------------
TEST(Location3, FirstOccupiedFindsFirstSetSlot) {
    auto t = MakeSlots({0, 1, 0, 1, 1, 0, 1}, {100, 101, 102, 103, 104, 105, 106});
    CHECK_EQ(FirstOccupiedSlot(t), 1);
    CHECK(!SlotTableFull(t));
}

TEST(Location3, FirstOccupiedSlotZero) {
    auto t = MakeSlots({1, 0, 0}, {5, 6, 7});
    CHECK_EQ(FirstOccupiedSlot(t), 0);  // !byte_12CEA98[0] guard => 0
}

TEST(Location3, EmptyTableIsFull) {
    auto t = MakeSlots(std::vector<std::uint8_t>(10, 0), std::vector<std::int32_t>(10, 0));
    CHECK_EQ(FirstOccupiedSlot(t), kMaxSlots);
    CHECK(SlotTableFull(t));
}

// ---- CollectOccupiedIds (golden vectors) ----------------------------------
TEST(Location3, CollectOccupiedCap4) {
    auto t = MakeSlots({0, 1, 0, 1, 1, 0, 1}, {100, 101, 102, 103, 104, 105, 106});
    std::vector<std::int32_t> out;
    int n = CollectOccupiedIds(t, 4, out);
    CHECK_EQ(n, 4);                                   // 4 occupied total
    CHECK_EQ(out.size(), 4u);
    CHECK_EQ(out[0], 101); CHECK_EQ(out[1], 103);
    CHECK_EQ(out[2], 104); CHECK_EQ(out[3], 106);
}

TEST(Location3, CollectOccupiedCapStopsAtCap) {
    auto t = MakeSlots({0, 1, 0, 1, 1, 0, 1}, {100, 101, 102, 103, 104, 105, 106});
    std::vector<std::int32_t> out;
    int n = CollectOccupiedIds(t, 2, out);            // count is capped (v15<cap)
    CHECK_EQ(n, 2);
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0], 101); CHECK_EQ(out[1], 103);
    // the uncapped total is available separately (for the >6 warning)
    CHECK_EQ(CountOccupiedSlots(t), 4);
}

// ---- CollectSelectionIds (golden vectors) ---------------------------------
TEST(Location3, CollectSelectionSkipsInactiveAndEmpty) {
    auto sel = MakeSel({{true, true, 10}, {true, false, 11}, {false, false, 0},
                        {true, true, 12}, {true, true, 13}, {true, true, 14},
                        {true, true, 15}, {true, true, 16}});
    std::vector<std::int32_t> out;
    int n = CollectSelectionIds(sel, 6, out);
    CHECK_EQ(n, 6);
    std::vector<std::int32_t> want{10, 12, 13, 14, 15, 16};
    CHECK(out == want);
    CHECK(AnySelectionActive(sel));
}

TEST(Location3, CollectSelectionCapStopsCollecting) {
    auto sel = MakeSel({{true, true, 10}, {true, true, 11}, {true, true, 12}});
    std::vector<std::int32_t> out;
    int n = CollectSelectionIds(sel, 2, out);
    CHECK_EQ(n, 2);
    CHECK_EQ(out.size(), 2u);
}

TEST(Location3, NoSelectionActive) {
    auto sel = MakeSel({{true, false, 1}, {false, false, 0}});
    CHECK(!AnySelectionActive(sel));
}

TEST(Location3, TooManyGate) {
    CHECK(!TooManyParticipants(6));
    CHECK(TooManyParticipants(7));
}

// ---- DialogAction constants (recovered byte codes) ------------------------
TEST(Location3, ActionCodes) {
    CHECK_EQ(static_cast<int>(DialogAction::ThiefKidnap), 60);
    CHECK_EQ(static_cast<int>(DialogAction::ThiefBurglary), 63);
    CHECK_EQ(static_cast<int>(DialogAction::ThiefSpyBuilding), 64);
    CHECK_EQ(static_cast<int>(DialogAction::GuardArrest), 67);
    CHECK_EQ(static_cast<int>(DialogAction::GuardRaid), 68);
    CHECK_EQ(static_cast<int>(DialogAction::ThievesGuildBurg), 72);
    CHECK_EQ(static_cast<int>(DialogAction::Pickpocket), 73);
    CHECK_EQ(static_cast<int>(DialogAction::PickpocketSelect), 97);
    CHECK_EQ(static_cast<int>(DialogAction::RobberCampStandard), 98);
    CHECK_EQ(static_cast<int>(DialogAction::GuardDetain), 100);
    CHECK_EQ(static_cast<int>(DialogAction::GuardCustoms), 101);
    CHECK_EQ(static_cast<int>(DialogAction::RobberCampRaid), 117);
}

// ---- Inert hooks: dialogs open and close without a confirm ----------------
TEST(Location3, InertHooksRobberCampRaidNoCommit) {
    SetLocationDialogHooks(nullptr);
    auto t = MakeSlots({1, 1}, {5, 6});
    DialogOutcome o = RobberCampRaid(/*hasTarget=*/true, /*targetBusy=*/true, t);
    CHECK(o.opened);
    CHECK(!o.committed);   // default frameStep ends the loop -> never confirmed
}

// ---- RobberCampStandard gates ---------------------------------------------
TEST(Location3, RobberCampStandardNoTarget) {
    Recorder rec;
    auto t = MakeSlots({1}, {9});
    auto sel = MakeSel({{true, true, 42}});
    DialogOutcome o = RobberCampStandard(0, /*hasTarget=*/false,
                                         /*requestExists=*/false, /*targetBusy=*/true, t, sel);
    CHECK(!o.opened);
    CHECK(!o.committed);
    CHECK_EQ(rec.formsOpened, 0);
}

TEST(Location3, RobberCampStandardExistingRequestAborts) {
    Recorder rec;
    auto t = MakeSlots({1}, {9});
    auto sel = MakeSel({{true, true, 42}});
    // gilde.exe 0x512712: matching handler found -> msg 5790, return before form.
    DialogOutcome o = RobberCampStandard(1, true, /*requestExists=*/true, true, t, sel);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
    CHECK_EQ(rec.messages[0], 5790);
    CHECK_EQ(rec.formsOpened, 0);
}

TEST(Location3, RobberCampStandardBusyGate) {
    Recorder rec;
    auto t = MakeSlots({1}, {9});
    auto sel = MakeSel({{true, true, 42}});
    // gilde.exe 0x512748: !IsAnimalTargetBusy -> msg 5782, return before form.
    DialogOutcome o = RobberCampStandard(1, true, false, /*targetBusy=*/false, t, sel);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
    CHECK_EQ(rec.messages[0], 5782);
    CHECK_EQ(rec.formsOpened, 0);
}

TEST(Location3, RobberCampStandardFullAborts) {
    Recorder rec;
    auto t = MakeSlots(std::vector<std::uint8_t>(5, 0), std::vector<std::int32_t>(5, 0));
    auto sel = MakeSel({{true, true, 42}});
    DialogOutcome o = RobberCampStandard(1, true, false, true, t, sel);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);   // "full" message shown
    CHECK_EQ(rec.formsOpened, 0);
}

TEST(Location3, RobberCampStandardConfirmQueuesOneId) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({1, 0}, {9, 0});
    auto sel = MakeSel({{true, false, 1}, {true, true, 77}, {true, true, 88}});
    DialogOutcome o = RobberCampStandard(1, true, false, /*targetBusy=*/true, t, sel);
    CHECK(o.opened);
    CHECK(o.committed);
    CHECK_EQ(o.action, 98);
    CHECK_EQ(rec.batchCode, 98);
    CHECK_EQ(rec.batchIds.size(), 1u);   // breaks on first active
    CHECK_EQ(rec.batchIds[0], 77);
    CHECK_EQ(rec.voiceCalls, 1);
    CHECK_EQ(rec.formsDestroyed, 1);
}

// ---- RobberCampRaid -------------------------------------------------------
TEST(Location3, RobberCampRaidConfirmQueuesAll) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({0, 1, 1, 0, 1}, {0, 201, 202, 0, 204});
    DialogOutcome o = RobberCampRaid(/*hasTarget=*/true, /*targetBusy=*/true, t);
    CHECK(o.committed);
    CHECK_EQ(o.action, 117);
    CHECK_EQ(rec.batchCode, 117);
    CHECK_EQ(o.count, 3);
    std::vector<std::int32_t> want{201, 202, 204};
    CHECK(rec.batchIds == want);
}

TEST(Location3, RobberCampRaidEmptyShowsMessage) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({0, 0, 0}, {0, 0, 0});
    DialogOutcome o = RobberCampRaid(/*hasTarget=*/true, /*targetBusy=*/true, t);
    CHECK(o.opened);
    CHECK(!o.committed);
    CHECK_EQ(rec.messages.size(), 1u);
}

TEST(Location3, RobberCampRaidBusyGate) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({1, 1}, {1, 2});
    // gilde.exe 0x512ed7: !IsAnimalTargetBusy -> msg 5791, return before form.
    DialogOutcome o = RobberCampRaid(/*hasTarget=*/true, /*targetBusy=*/false, t);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
    CHECK_EQ(rec.messages[0], 5791);
}

TEST(Location3, RobberCampRaidNoTargetNoOpen) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({1, 1}, {1, 2});
    DialogOutcome o = RobberCampRaid(/*hasTarget=*/false, /*targetBusy=*/true, t);
    CHECK(!o.opened);
    CHECK_EQ(rec.formsOpened, 0);
}

// ---- ThiefBurglaryDialog gates --------------------------------------------
TEST(Location3, ThiefBurglaryBusyGate) {
    Recorder rec;
    auto t = MakeSlots({1}, {3});
    DialogOutcome o = ThiefBurglaryDialog(/*targetBusy=*/false, true, t);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
    CHECK_EQ(rec.messages[0], 5791);
}

TEST(Location3, ThiefBurglarySecurityGate) {
    Recorder rec;
    auto t = MakeSlots({1}, {3});
    DialogOutcome o = ThiefBurglaryDialog(true, /*securityOk=*/false, t);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
}

TEST(Location3, ThiefBurglaryConfirmQueues) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({1, 0, 1}, {11, 0, 13});
    DialogOutcome o = ThiefBurglaryDialog(true, true, t);
    CHECK(o.committed);
    CHECK_EQ(o.action, 63);
    CHECK_EQ(o.count, 2);
    std::vector<std::int32_t> want{11, 13};
    CHECK(rec.batchIds == want);
}

// ---- GuardArrestDialog (cap 4 + pad) --------------------------------------
TEST(Location3, GuardArrestPadsToFour) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({0, 1, 1, 0}, {0, 50, 51, 0});
    DialogOutcome o = GuardArrestDialog(t);
    CHECK(o.committed);
    CHECK_EQ(o.action, 67);
    CHECK_EQ(o.count, 2);
    CHECK_EQ(rec.batchIds.size(), 4u);   // padded to 4
    CHECK_EQ(rec.batchIds[0], 50); CHECK_EQ(rec.batchIds[1], 51);
    CHECK_EQ(rec.batchIds[2], -1); CHECK_EQ(rec.batchIds[3], -1);
}

TEST(Location3, GuardArrestFullAborts) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots(std::vector<std::uint8_t>(3, 0), std::vector<std::int32_t>(3, 0));
    DialogOutcome o = GuardArrestDialog(t);
    CHECK(!o.opened);
    CHECK_EQ(rec.messages.size(), 1u);
}

// ---- GuardRaidDialog (cap 6 + too-many warning) ---------------------------
TEST(Location3, GuardRaidWarnsWhenManyBuildings) {
    Recorder rec;
    rec.confirm();
    // 7 occupied buildings: batch caps at 6 ids, then warns (>6).
    auto t = MakeSlots({1, 1, 1, 1, 1, 1, 1},
                       {1, 2, 3, 4, 5, 6, 7});
    DialogOutcome o = GuardRaidDialog(t);
    CHECK(o.committed);
    CHECK_EQ(o.action, 68);
    CHECK_EQ(rec.batchIds.size(), 6u);    // capped at 6
    CHECK_EQ(o.count, 6);
    CHECK_EQ(rec.messages.size(), 1u);    // too-many warning (5630)
    CHECK_EQ(rec.messages[0], 5630);
}

TEST(Location3, GuardRaidNoWarnWhenFew) {
    Recorder rec;
    rec.confirm();
    auto t = MakeSlots({1, 1, 1}, {1, 2, 3});
    DialogOutcome o = GuardRaidDialog(t);
    CHECK(o.committed);
    CHECK_EQ(o.count, 3);
    CHECK_EQ(rec.messages.size(), 0u);    // no warning
}

// ---- SelectionBatchDialog (burglary / pickpocket) -------------------------
TEST(Location3, SelectionBatchExistingRequestReissues) {
    Recorder rec;
    auto sel = MakeSel({{true, true, 1}, {true, true, 2}, {true, false, 3}});
    DialogOutcome o = SelectionBatchDialog(DialogAction::Pickpocket, 6,
                                           /*requestExists=*/true, sel);
    CHECK(o.opened);
    CHECK(!o.committed);                  // no new batch
    CHECK_EQ(rec.changeActions, 2);       // one per active selection
    CHECK_EQ(rec.batchCount, -1);         // queueBatch never called
}

TEST(Location3, SelectionBatchQueuesWhenNoneExisting) {
    Recorder rec;
    auto sel = MakeSel({{true, true, 100}, {true, true, 101}, {false, false, 0}});
    DialogOutcome o = SelectionBatchDialog(DialogAction::ThievesGuildBurg, 8,
                                           /*requestExists=*/false, sel);
    CHECK(o.committed);
    CHECK_EQ(o.action, 72);
    CHECK_EQ(o.count, 2);
    std::vector<std::int32_t> want{100, 101};
    CHECK(rec.batchIds == want);
    CHECK_EQ(rec.voiceCalls, 1);
}

TEST(Location3, SelectionBatchEmptyShowsMessage) {
    Recorder rec;
    auto sel = MakeSel({{true, false, 1}});
    DialogOutcome o = SelectionBatchDialog(DialogAction::Pickpocket, 6, false, sel);
    CHECK(!o.committed);
    CHECK_EQ(rec.messages.size(), 1u);
}
