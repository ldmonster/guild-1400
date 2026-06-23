// End-to-end flow for world/location3: a thief-guild "case the joint" session.
// One installed hooks backend drives several dialogs in sequence the way the
// game would: scout (burglary cased -> pickpocket) and a guard counter-raid,
// verifying the recovered batch logic threads through each dialog correctly.
#include "world/location3.h"

#include <climits>
#include <vector>

#include "test.h"

using namespace guild::world;

namespace {

// A single backend recording the full session: every batch queued, every
// message, every voice ack, across all dialogs run in this scenario.
struct Session {
    LocationDialogHooks hooks{};
    struct Batch { int code; int count; std::vector<std::int32_t> ids; };
    std::vector<Batch> batches;
    std::vector<int> messages;
    int voices = 0;
    int changeActions = 0;
    std::vector<std::int32_t> frames;
    std::size_t fi = 0;

    static Session* g;
    Session() {
        g = this;
        hooks.openForm    = [](const char*) -> std::int32_t { return 1; };
        hooks.destroyForm = [](std::int32_t) {};
        hooks.frameStep   = [](std::int32_t) -> std::int32_t {
            if (g->fi >= g->frames.size()) return INT32_MIN;
            return g->frames[g->fi++];
        };
        hooks.queueBatch  = [](int code, int count, const std::int32_t* ids, int n) {
            g->batches.push_back({code, count, std::vector<std::int32_t>(ids, ids + n)});
        };
        hooks.showMessage = [](int t) { g->messages.push_back(t); };
        hooks.playFavorVoice = [] { ++g->voices; };
        hooks.changePlayerAction = [](std::int32_t, std::int32_t) { ++g->changeActions; };
        SetLocationDialogHooks(&hooks);
    }
    ~Session() { SetLocationDialogHooks(nullptr); }
    void scriptConfirm() { frames = {1}; fi = 0; }
};
Session* Session::g = nullptr;

SelectionTable MakeSel(std::initializer_list<std::tuple<bool, bool, std::int32_t>> rows) {
    SelectionTable s{};
    int i = 0;
    for (auto& r : rows) {
        s[i].present = std::get<0>(r);
        s[i].active  = std::get<1>(r);
        s[i].id      = std::get<2>(r);
        ++i;
    }
    return s;
}

SlotTableView MakeSlots(std::vector<std::uint8_t> occ, std::vector<std::int32_t> id) {
    return {std::move(occ), std::move(id)};
}

} // namespace

// Full thief session: burglary (no prior request, two thieves selected) then a
// pickpocket re-issue when a request already exists, ending with the guard
// counter-raid on the player's buildings.
TEST(Location3E2E, ThiefAndGuardSession) {
    Session s;
    auto thieves = MakeSel({{true, true, 1001}, {true, true, 1002},
                            {true, false, 1003}});

    // 1) Burglary: no existing request -> queue action 72 with both thieves.
    s.scriptConfirm();
    DialogOutcome burg = SelectionBatchDialog(DialogAction::ThievesGuildBurg, 8,
                                              /*requestExists=*/false, thieves);
    CHECK(burg.committed);
    CHECK_EQ(burg.action, 72);
    CHECK_EQ(burg.count, 2);

    // 2) Pickpocket on the same target where a request already exists -> the
    //    dialog re-issues per-thief actions instead of a new batch.
    DialogOutcome pick = SelectionBatchDialog(DialogAction::Pickpocket, 6,
                                              /*requestExists=*/true, thieves);
    CHECK(!pick.committed);
    CHECK_EQ(s.changeActions, 2);          // one per active thief

    // 3) Guard counter-raid on 7 player buildings -> caps batch at 6 + warns.
    s.fi = 0; s.frames = {1};              // re-arm one confirm frame
    auto buildings = MakeSlots({1, 1, 1, 1, 1, 1, 1},
                               {10, 11, 12, 13, 14, 15, 16});
    DialogOutcome raid = GuardRaidDialog(buildings);
    CHECK(raid.committed);
    CHECK_EQ(raid.action, 68);
    CHECK_EQ(raid.count, 6);

    // Session assertions: exactly two batches queued (burglary 72, raid 68);
    // the re-issue path queued none. Two favor-voice acks (one per real batch).
    CHECK_EQ(s.batches.size(), 2u);
    CHECK_EQ(s.batches[0].code, 72);
    CHECK_EQ(s.batches[0].ids.size(), 2u);
    CHECK_EQ(s.batches[1].code, 68);
    CHECK_EQ(s.batches[1].ids.size(), 6u);   // capped
    CHECK_EQ(s.voices, 2);
    CHECK_EQ(s.messages.size(), 1u);          // the >6 too-many warning
    CHECK_EQ(s.messages[0], 5630);
}

// Robber-camp economy flow: a "full" camp aborts the standard raid before any
// form opens; an under-capacity camp with a chosen raider queues action 98;
// the camp-wide raid then queues all occupied camp ids as action 117.
TEST(Location3E2E, RobberCampFlow) {
    Session s;

    // Full table -> standard raid aborts, message shown, no form/batch.
    auto full = MakeSlots(std::vector<std::uint8_t>(4, 0),
                          std::vector<std::int32_t>(4, 0));
    auto raiders = MakeSel({{true, true, 555}, {true, true, 556}});
    DialogOutcome std0 = RobberCampStandard(9, true, /*requestExists=*/false,
                                            /*targetBusy=*/true, full, raiders);
    CHECK(!std0.opened);
    CHECK_EQ(s.batches.size(), 0u);
    CHECK_EQ(s.messages.size(), 1u);

    // Under-capacity camp -> standard raid queues a single-raider batch (98).
    s.scriptConfirm();
    auto camp = MakeSlots({1, 0, 1}, {70, 0, 72});
    DialogOutcome std1 = RobberCampStandard(9, true, /*requestExists=*/false,
                                            /*targetBusy=*/true, camp, raiders);
    CHECK(std1.committed);
    CHECK_EQ(std1.action, 98);
    CHECK_EQ(s.batches.size(), 1u);
    CHECK_EQ(s.batches[0].code, 98);
    CHECK_EQ(s.batches[0].ids.size(), 1u);
    CHECK_EQ(s.batches[0].ids[0], 555);

    // Camp-wide raid -> all occupied camp ids as action 117.
    s.fi = 0; s.frames = {1};
    DialogOutcome raid = RobberCampRaid(/*hasTarget=*/true, /*targetBusy=*/true, camp);
    CHECK(raid.committed);
    CHECK_EQ(raid.action, 117);
    CHECK_EQ(raid.count, 2);
    CHECK_EQ(s.batches.size(), 2u);
    CHECK_EQ(s.batches[1].code, 117);
    std::vector<std::int32_t> want{70, 72};
    CHECK(s.batches[1].ids == want);
}
