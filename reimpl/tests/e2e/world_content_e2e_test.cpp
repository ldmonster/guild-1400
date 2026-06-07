// End-to-end: drive a whole scripted "Brand" (fire/raid) event chain over a
// synthetic world and verify the resulting handler state, the chronicle, and the
// emitted (mock) commands against a hand-computed reference. Also exercises the
// chronicle scan + dated-text format and the family heir-line on the same flow.
#include "tests/framework/test.h"

#include <cstring>

#include "world/event_fire.h"
#include "world/history_chronicle.h"
#include "world/history.h"
#include "world/mission_rules.h"
#include "world/event.h"
#include "world/family_query.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

namespace {
// A mock command/chronicle backend that logs every mutation, plus a Chronicle the
// event writes destruction entries into.
struct WorldBackend : FireEventHooks {
    Chronicle    chronicle;
    int          spawns       = 0;
    i32          spawnLog[32] = {0};
    int          spawnLogN    = 0;
    int          flagSets     = 0;
    int          totalHours   = 0;
    i32          curDay       = 0;       // the "game clock" advanced by the event
    // RNG is the real crt LCG (seeded by the test) so SetFireFlag draws match.

    i32 SpawnFire(i32 building) override {
        i32 id = 200 + spawns;
        if (spawnLogN < 32) spawnLog[spawnLogN++] = id;
        ++spawns;
        (void)building;
        return id;
    }
    void SetFireFlag(i32, int) override { ++flagSets; }
    void AdvanceTime(int hours) override { totalHours += hours; curDay += hours; }
    void RecordChronicle(i32 owner, int textId) override {
        // Date the chronicle entry to the current game day.
        chronicle.Add({curDay, 6, 1400, textId, "Brand"});
        (void)owner;
    }
    int RandomModulo(int n) override { return n ? (crt::RandNext() % n) : 0; }
};
} // namespace

TEST(WorldContentE2E, FireEventChain) {
    crt::Srand(1);
    WorldBackend be;
    be.curDay = 116;   // event begins on game day 116

    // --- Ignition phase (step 0): authoritative, 2 candidate buildings. ---
    FireEvent ev;
    FireEventInit(&ev, /*eventId*/ 3, /*owner*/ 42, /*value*/ 250);
    CHECK_EQ(ev.step, 0);
    CHECK(FireEventShouldRunBody(ev, false));   // no pending cmd

    int spawned = FireEventIgnite(&ev, 2, be);
    // Reference: 2 spawns (cmd ids 200, 201), subPhase 0->1, 2 flag sets.
    CHECK_EQ(spawned, 2);
    CHECK_EQ(be.spawns, 2);
    CHECK_EQ(be.flagSets, 2);
    CHECK_EQ(ev.subPhase, 1);
    CHECK_EQ(ev.spawnIds[0], 200);
    CHECK_EQ(ev.spawnIds[1], 201);

    // --- Burn phase (step 1), looped until subPhase reaches 3. ---
    // Each tick: dailyHourOutput 300 -> base 100 ; no catalyst -> value -= 100.
    // Start value 250. The handler is now in the burn phase (step 1).
    int tier;
    ev.step = 1;
    // Tick 1: 250 -> 150 ; tier from prev-base = 250-100=150 -> -1 ; subPhase 1<3 loop.
    ev.subPhase = 1;
    tier = FireEventBurnTick(&ev, 300, false, 0, be, /*chronicleTextId*/ 7328);
    CHECK_EQ(ev.value, 150);
    CHECK_EQ(tier, -1);
    CHECK_EQ(ev.step, 0);            // looped
    CHECK_EQ(be.chronicle.Count(), 0);

    // Tick 2: 150 -> 50 ; tier 150-100=50 -> -1.
    ev.subPhase = 2;
    tier = FireEventBurnTick(&ev, 300, false, 0, be, 7328);
    CHECK_EQ(ev.value, 50);
    CHECK_EQ(tier, -1);

    // Tick 3 (final, subPhase 3): 50 -> -50 destroyed ; tier 50-100=-50 -> 2 ;
    // chronicle entry recorded ; step NOT reset (subPhase==3 -> event ends).
    ev.step = 1;                     // re-entered the burn phase for the final tick
    ev.subPhase = 3;
    tier = FireEventBurnTick(&ev, 300, false, 0, be, 7328);
    CHECK_EQ(ev.value, -50);
    CHECK_EQ(tier, 2);
    CHECK(ev.step != 0);             // subPhase>=3 -> no loop back, step stays 1
    CHECK_EQ(be.chronicle.Count(), 1);

    // Three burn ticks advanced the clock 4 hours each.
    CHECK_EQ(be.totalHours, 12);

    // --- Teardown: cancel the two outstanding spawn commands. ---
    int cancelled = FireEventTeardown(&ev, be);
    CHECK_EQ(cancelled, 2);
    CHECK_EQ(ev.step, kFireNone);
    CHECK_EQ(ev.spawnIds[0], kFireNone);

    // --- Verify the chronicle: the destruction entry is dated to game day 128 ---
    // (116 + 3 ticks * 4 hours == 116 + 12).
    CHECK_EQ(be.chronicle.At(0).day, 128);
    CHECK_EQ(be.chronicle.At(0).textId, 7328);

    // Dated-text format + parse round-trip on that entry.
    char date[16];
    be.chronicle.FormatDate(0, date);
    ParsedDate pd;
    CHECK(HistoryParseDate(date, &pd));
    CHECK_EQ(pd.month, 6);
    CHECK_EQ(pd.year, 1400);

    // Chronological scan: an observer on game day 129 sees "yesterday's" entry (128).
    CHECK_EQ(be.chronicle.ScanNextForward(129), 0);
    // On day 128 itself, that entry is "today", not emitted.
    CHECK_EQ(be.chronicle.ScanNextForward(128), -1);
}

TEST(WorldContentE2E, PlagueAndFamilySuccessionFlow) {
    // A plague outbreak chronicle line (seeded golden), recorded into the same log,
    // then an heir-line query picks the successor for the affected dynast.
    crt::Srand(1);
    Chronicle log;
    int outbreakId = ChroniclePlagueOutbreakTextId();   // seed 1 -> 7331
    CHECK_EQ(outbreakId, 7331);
    log.Add({200, 7, 1401, outbreakId, "Pest"});

    // Spread step to an important kind: next draw (%2) -> 7332.
    int stepId = ChroniclePlagueSpreadStepTextId(7);
    CHECK_EQ(stepId, 7332);
    log.Add({201, 7, 1401, stepId, "Pest"});

    CHECK_EQ(log.Count(), 2);
    CHECK_EQ(log.ScanNextForward(201), 0);   // day-200 entry is "yesterday" of 201

    // Family heir-line for the dynast who died: eldest-child chain.
    FamilyRecord recs[3];
    std::memset(recs, 0, sizeof(recs));
    for (auto& r : recs) for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    recs[0].id = 10; recs[0].children[0] = 11;          // dynast 10 -> heir 11
    recs[1].id = 11; recs[1].father = 10; recs[1].children[0] = 12;  // 11 -> 12
    recs[2].id = 12; recs[2].father = 11;
    FamilyTree tree{recs, 3};

    i32 line[4];
    int n = FamilyHeirLine(tree, 10, line, 4);
    CHECK_EQ(n, 2);
    CHECK_EQ(line[0], 11);
    CHECK_EQ(line[1], 12);
    CHECK(FamilyIsDescendantOf(tree, 10, 12));
}
