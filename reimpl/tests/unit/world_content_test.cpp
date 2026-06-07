// Unit tests for the world scripted-content / chronicle cores:
//   * event_fire   : trigger gate, fire-damage arithmetic, voice tier, ignite/burn
//   * history_chronicle : Notify text-id selectors, plague RNG (crt golden),
//                         chronicle add/scan/format
//   * mission_rules: descriptor lookup-by-value, reward resolve, history-reward
//                    selection/mode, completion/result outcome decode
//   * family_query : descendant + heir-line on a synthetic tree
// Golden RNG vectors computed with python against the crt LCG (seed 1).
#include "tests/framework/test.h"

#include <cstring>

#include "world/event_fire.h"
#include "world/history_chronicle.h"
#include "world/history.h"
#include "world/mission_rules.h"
#include "world/event.h"
#include "world/family_query.h"
#include "world/stammbaum.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

// ===========================================================================
// event_fire
// ===========================================================================
namespace {
// Mock command hook recording every mutation the event performs.
struct MockFireHooks : FireEventHooks {
    int  spawnCount   = 0;
    i32  nextCmdId    = 100;
    int  flagCalls    = 0;
    int  lastFlag     = -999;
    int  advanceHours = 0;
    int  chronicleId  = -1;
    i32  chronicleOwner = -1;
    int  randSeq[8]   = {0,0,0,0,0,0,0,0};
    int  randIdx      = 0;

    i32 SpawnFire(i32) override { ++spawnCount; return nextCmdId++; }
    void SetFireFlag(i32, int flag) override { ++flagCalls; lastFlag = flag; }
    void AdvanceTime(int hours) override { advanceHours += hours; }
    void RecordChronicle(i32 owner, int textId) override {
        chronicleOwner = owner; chronicleId = textId;
    }
    int RandomModulo(int n) override {
        int v = randSeq[randIdx % 8];
        ++randIdx;
        return n ? (v % n) : 0;
    }
};
} // namespace

TEST(WorldContentFire, TriggerGate) {
    FireEvent ev;
    FireEventInit(&ev, 7, 42, 1000);
    // No pending command -> body always runs.
    CHECK(FireEventShouldRunBody(ev, false));
    CHECK(FireEventShouldRunBody(ev, true));
    // Pending command on an authoritative entry: body waits for resolution.
    ev.pendingCmd = 555;
    CHECK(!FireEventShouldRunBody(ev, false));
    CHECK(FireEventShouldRunBody(ev, true));
    // Non-authoritative entry ignores the pending command.
    ev.flags = 0;
    CHECK(FireEventShouldRunBody(ev, false));
}

TEST(WorldContentFire, DamageArithmeticGolden) {
    // base = (int)(300 * 0.16666667 * 2.0) == 100  (python golden).
    CHECK_EQ(FireEventComputeDamage(1000, 300, false, 0), 900);
    // factor = 1 + 50*0.002*0.7 = 1.07 ; 1000 - 100*1.07 = 893.
    CHECK_EQ(FireEventComputeDamage(1000, 300, true, 50), 893);
    // Over-damage drives the value negative (destroyed).
    CHECK_EQ(FireEventComputeDamage(40, 300, false, 0), -60);
    // Zero output -> no damage.
    CHECK_EQ(FireEventComputeDamage(500, 0, false, 0), 500);
}

TEST(WorldContentFire, VoiceTier) {
    CHECK_EQ(FireEventVoiceTier(-5), 2);
    CHECK_EQ(FireEventVoiceTier(0),  2);
    CHECK_EQ(FireEventVoiceTier(7),  1);
    CHECK_EQ(FireEventVoiceTier(10), 1);
    CHECK_EQ(FireEventVoiceTier(20), 0);
    CHECK_EQ(FireEventVoiceTier(25), 0);
    CHECK_EQ(FireEventVoiceTier(26), -1);
    CHECK_EQ(FireEventVoiceTier(900), -1);
}

TEST(WorldContentFire, IgniteSpawnsCappedAndFlagged) {
    FireEvent ev;
    FireEventInit(&ev, 1, 9, 1000);
    MockFireHooks h;
    h.randSeq[0] = 0; h.randSeq[1] = 1; h.randSeq[2] = 0;  // flag = rand%2 + 1
    // 3 candidate buildings -> 3 spawns, subPhase bumped 0->1.
    int spawned = FireEventIgnite(&ev, 3, h);
    CHECK_EQ(spawned, 3);
    CHECK_EQ(h.spawnCount, 3);
    CHECK_EQ(ev.subPhase, 1);
    CHECK_EQ(ev.spawnIds[0], 100);
    CHECK_EQ(ev.spawnIds[2], 102);
    CHECK_EQ(ev.spawnIds[3], kFireNone);
    CHECK_EQ(h.lastFlag, 1);   // last rand was 0 -> flag 1

    // Cap at kFireMaxSpawns.
    FireEventInit(&ev, 1, 9, 1000);
    MockFireHooks h2;
    int s2 = FireEventIgnite(&ev, 99, h2);
    CHECK_EQ(s2, kFireMaxSpawns);

    // Non-authoritative entry spawns nothing but still bumps subPhase.
    FireEventInit(&ev, 1, 9, 1000);
    ev.flags = 0;
    MockFireHooks h3;
    CHECK_EQ(FireEventIgnite(&ev, 5, h3), 0);
    CHECK_EQ(h3.spawnCount, 0);
    CHECK_EQ(ev.subPhase, 1);
}

TEST(WorldContentFire, BurnTickDamageTimeAndChronicle) {
    FireEvent ev;
    FireEventInit(&ev, 1, 42, 1000);
    ev.subPhase = 1;  // mid-burn (< 3 -> loops)
    MockFireHooks h;
    int tier = FireEventBurnTick(&ev, 300, false, 0, h, 7328);
    CHECK_EQ(ev.value, 900);            // 1000 - 100
    CHECK_EQ(h.advanceHours, 4);        // GameTime_Advance(...,4)
    CHECK_EQ(ev.step, 0);              // subPhase<3 -> loop back to ignition
    CHECK_EQ(tier, -1);                // prev-base = 900 -> no line
    CHECK_EQ(h.chronicleId, -1);       // not destroyed -> no chronicle entry

    // A tick that destroys the building records a chronicle entry, tier 2.
    FireEventInit(&ev, 1, 42, 40);
    ev.subPhase = 1;
    MockFireHooks h2;
    int tier2 = FireEventBurnTick(&ev, 300, false, 0, h2, 7328);
    CHECK_EQ(ev.value, -60);
    CHECK_EQ(tier2, 2);                // prev-base = 40-100 = -60 -> tier 2
    CHECK_EQ(h2.chronicleId, 7328);
    CHECK_EQ(h2.chronicleOwner, 42);
}

// ===========================================================================
// history_chronicle
// ===========================================================================
TEST(WorldContentChronicle, NotifyTextIds) {
    CHECK_EQ(ChronicleWanderATextId(6), 7319);
    CHECK_EQ(ChronicleWanderATextId(7), 7319);
    CHECK_EQ(ChronicleWanderATextId(5), -1);
    CHECK_EQ(ChronicleWanderBTextId(6), 7320);
    CHECK_EQ(ChronicleWanderBTextId(2), -1);

    CHECK_EQ(ChronicleAttackDefenderTextId(7), 7323);
    CHECK_EQ(ChronicleAttackDefenderTextId(3), -1);
    CHECK_EQ(ChronicleAttackSuffixTextId(false), 7325);
    CHECK_EQ(ChronicleAttackSuffixTextId(true),  7326);
}

TEST(WorldContentChronicle, WanderPairGate) {
    int s, o;
    // Both parties non-7 -> two lines.
    CHECK_EQ(ChronicleWanderPairLines(6, 6, &s, &o), 2);
    CHECK_EQ(s, 7321);
    CHECK_EQ(o, 7322);
    // Self is 7 -> only the other line.
    CHECK_EQ(ChronicleWanderPairLines(7, 6, &s, &o), 1);
    CHECK_EQ(s, -1);
    CHECK_EQ(o, 7322);
    // Both 7 -> no lines.
    CHECK_EQ(ChronicleWanderPairLines(7, 7, &s, &o), 0);
}

TEST(WorldContentChronicle, PlagueRngGolden) {
    // python golden (crt seed 1): outbreak r = randnext()%3 = 2 -> 7331.
    crt::Srand(1);
    CHECK_EQ(ChroniclePlagueOutbreakTextId(), 7331);
    // Three consecutive outbreaks from a fresh seed 1: 7331, 7330, 7329.
    crt::Srand(1);
    CHECK_EQ(ChroniclePlagueOutbreakTextId(), 7331);
    CHECK_EQ(ChroniclePlagueOutbreakTextId(), 7330);
    CHECK_EQ(ChroniclePlagueOutbreakTextId(), 7329);
    // spread step: r = randnext()%2 = 0 -> 7332 (kind 6); draw consumed regardless.
    crt::Srand(1);
    CHECK_EQ(ChroniclePlagueSpreadStepTextId(6), 7332);
    // spread step gate fails for unimportant kind, but the draw still happens.
    crt::Srand(1);
    CHECK_EQ(ChroniclePlagueSpreadStepTextId(3), -1);
    // spread: r = randnext()%2 = 0 -> 7335.
    crt::Srand(1);
    CHECK_EQ(ChroniclePlagueSpreadTextId(), 7335);
}

TEST(WorldContentChronicle, AddScanFormat) {
    Chronicle c;
    // Add out of order; entries are kept ascending by day.
    c.Add({120, 5, 1400, 7000, nullptr});
    c.Add({100, 4, 1400, 7001, nullptr});
    c.Add({116, 4, 1400, 7002, nullptr});  // day 116 = day 117 - 1
    CHECK_EQ(c.Count(), 3);
    CHECK_EQ(c.At(0).day, 100);
    CHECK_EQ(c.At(1).day, 116);
    CHECK_EQ(c.At(2).day, 120);

    // ScanNextForward(117) emits the entry dated to day 116 (the "yesterday" window).
    int idx = c.ScanNextForward(117);
    CHECK(idx >= 0);
    CHECK_EQ(c.At(idx).textId, 7002);
    // No entry dated to 99 (the day-100 entry is "today", not yesterday).
    CHECK_EQ(c.ScanNextForward(100), -1);

    // Dated-text format: round-trips through HistoryParseDate's year offset.
    char buf[16];
    c.FormatDate(1, buf);   // day-of-month from 116 -> 16, month 4, year 1400
    CHECK_EQ(std::strcmp(buf, "16.04.1400"), 0);
    ParsedDate pd;
    CHECK(HistoryParseDate(buf, &pd));
    CHECK_EQ(pd.day, 16);
    CHECK_EQ(pd.month, 4);
    CHECK_EQ(pd.year, 1400);
    CHECK_EQ(pd.yearOffset, 0);
}

// ===========================================================================
// mission_rules
// ===========================================================================
namespace {
void BuildMissionTable() {
    EventTableReset();
    // Three descriptors with distinct +4 value bytes and +0x0C params.
    g_eventTable[0].value = 14; g_eventTable[0].paramB = 1000;
    g_eventTable[1].value = 17; g_eventTable[1].paramB = 2000;
    g_eventTable[2].value = 20; g_eventTable[2].paramB = 3000;
    g_eventTableCount = 3;
}
} // namespace

TEST(WorldContentMission, DescriptorLookupByValue) {
    BuildMissionTable();
    CHECK_EQ(MissionFindDescriptorByValue(14), 0);
    CHECK_EQ(MissionFindDescriptorByValue(17), 1);
    CHECK_EQ(MissionFindDescriptorByValue(20), 2);
    CHECK_EQ(MissionFindDescriptorByValue(99), -1);

    MissionRewardInfo info;
    CHECK(MissionResolveReward(17, &info));
    CHECK_EQ(info.descriptorIndex, 1);
    CHECK_EQ(info.nameTextId, 18);     // value(17) + 1
    CHECK_EQ(info.bodyTextId, 19);     // value(17) + 2
    CHECK_EQ(info.voiceIndex, 2000);   // paramB (+0x0C)
    CHECK(!MissionResolveReward(99, &info));
}

TEST(WorldContentMission, HistoryRewardSelectionAndMode) {
    CHECK_EQ(MissionHistoryRewardSelection(0), 0);
    CHECK_EQ(MissionHistoryRewardSelection(1), 2);
    CHECK_EQ(MissionHistoryRewardSelection(2), 3);
    CHECK_EQ(MissionHistoryRewardSelection(3), 4);
    CHECK_EQ(MissionHistoryRewardSelection(4), 5);
    CHECK_EQ(MissionHistoryRewardSelection(5), 0);  // all consumed -> reset

    CHECK_EQ(MissionHistoryRewardMode(0), -1);
    CHECK_EQ(MissionHistoryRewardMode(1), 0);
    CHECK_EQ(MissionHistoryRewardMode(5), 4);
    CHECK_EQ(MissionHistoryRewardMode(6), -1);
}

TEST(WorldContentMission, OutcomeDecode) {
    CHECK((MissionDecodeCompletion(1) == MissionCompletionOutcome::kFailure));
    CHECK((MissionDecodeCompletion(2) == MissionCompletionOutcome::kInfo));
    CHECK((MissionDecodeCompletion(3) == MissionCompletionOutcome::kLoadSession));
    CHECK((MissionDecodeCompletion(0) == MissionCompletionOutcome::kNone));
    CHECK((MissionDecodeCompletion(9) == MissionCompletionOutcome::kNone));
    CHECK(MissionResultIsLoadSession(2));
    CHECK(!MissionResultIsLoadSession(1));
}

// ===========================================================================
// family_query
// ===========================================================================
namespace {
// Synthetic dynasty:
//   1 -- spouse 2
//   |
//   +-- 3 (child)  -- spouse 4
//   |   +-- 5 (grandchild)
//   |   +-- 6 (grandchild)
//   +-- 7 (child)
static FamilyRecord MakeRec(i32 id, i32 father, i32 mother, i32 spouse,
                            std::initializer_list<i32> kids) {
    FamilyRecord r;
    std::memset(&r, 0, sizeof(r));
    r.id = id; r.father = father; r.mother = mother; r.spouse = spouse;
    for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    int i = 0;
    for (i32 k : kids) r.children[i++] = k;
    return r;
}
} // namespace

TEST(WorldContentFamily, DescendantAndHeirLine) {
    FamilyRecord recs[] = {
        MakeRec(1, kFamilyNone, kFamilyNone, 2, {3, 7}),
        MakeRec(2, kFamilyNone, kFamilyNone, 1, {3, 7}),
        MakeRec(3, 1, 2, 4, {5, 6}),
        MakeRec(4, kFamilyNone, kFamilyNone, 3, {5, 6}),
        MakeRec(5, 3, 4, kFamilyNone, {}),
        MakeRec(6, 3, 4, kFamilyNone, {}),
        MakeRec(7, 1, 2, kFamilyNone, {}),
    };
    FamilyTree tree{recs, 7};

    // Ancestor (existing) sanity, then descendant dual.
    CHECK(FamilyIsAncestorOf(tree, 1, 5));      // 1 -> 3 -> 5
    CHECK(FamilyIsDescendantOf(tree, 1, 5));    // dual
    CHECK(FamilyIsDescendantOf(tree, 3, 6));
    CHECK(!FamilyIsDescendantOf(tree, 5, 1));   // not a descendant
    CHECK(!FamilyIsDescendantOf(tree, 7, 5));   // siblings' lines don't cross

    // Collect all descendants of 1: children 3,7 then grandchildren 5,6 (BFS).
    i32 out[8];
    int n = FamilyCollectDescendants(tree, 1, out, 8);
    CHECK_EQ(n, 4);
    CHECK_EQ(out[0], 3);
    CHECK_EQ(out[1], 7);
    CHECK_EQ(out[2], 5);
    CHECK_EQ(out[3], 6);

    // Leaf person has no descendants.
    CHECK_EQ(FamilyCollectDescendants(tree, 5, out, 8), 0);

    // Heir line of 1: eldest-child chain 1 -> 3 -> 5.
    int hl = FamilyHeirLine(tree, 1, out, 8);
    CHECK_EQ(hl, 2);
    CHECK_EQ(out[0], 3);   // eldest child of 1
    CHECK_EQ(out[1], 5);   // eldest child of 3
    // Heir line of a childless person is empty.
    CHECK_EQ(FamilyHeirLine(tree, 7, out, 8), 0);
}
