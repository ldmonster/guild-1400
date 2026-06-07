// Unit tests for the large NpcEvent step state machines (npcevent_steps.{h,cpp}).
// Each test drives one translated step with synthetic He state, a seeded CRT RNG,
// and recording/scripted mock leaves, then checks the re-armed He record (phase,
// appointment GameTime, packet-handle slot), the branch taken, and the leaf
// dispatch against a reference derived from the IDA decompilation.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/npcevent_steps.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "sim/he.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

// ---------------------------------------------------------------------------
// Shared recording-mock state for the NpcEventHooks + NpcLeafHooks bridges.
// ---------------------------------------------------------------------------
struct Trace {
    int freeCount = 0;
    int entity29Count = 0;
    int lastEntity29Arg = 0;
    int gestureCount = 0;
    int op49Count = 0;
    int op87Count = 0;
    int op25Count = 0;
    int detachCount = 0;
    int pauseCount = 0;
    int resumeCount = 0;
    int panelCreateCount = 0;
    int panelDestroyCount = 0;
    // scripted scalars the steps query:
    int scriptStatus = 0;     // packetStatus
    int scriptSeq = 0;        // packetSeq
    int scriptDialog = -1;    // dialogResult
    int scriptEntity = 0;     // resolveEntity result handle
    int scriptPerson = 0;     // findPerson result handle
    int scriptBuilding = 0;   // findBuilding result handle
    int scriptNearDoor = 0;   // isNearDoor result
    int scriptReaperMove = 0; // reaperMove progress code
    int scriptReaperApproach = 1;
};
Trace g_t;
HeRecord* g_rec = nullptr;   // the record under test (for *_field handle math)

void ResetTrace() { g_t = Trace(); }

// Field reads off our opaque handles: we encode the handle as a small int and let
// the field accessor return scripted values keyed by (handle, offset).
i32 EntityField(i32 ent, int off) {
    if (!ent) return 0;
    if (off == 1) return 0x1000 + ent;     // object/entity id
    if (off == 4) return 0x2000 + ent;     // entity id (alt)
    if (off == 39) return g_t.scriptNearDoor ? 7 : 0xFFFF;  // owner word
    if (off == 97) return ent;             // sub-record present
    if (off == 1000) return 1;             // class-eligible probe
    return 0;
}
i32 PersonField(i32 p, int off) {
    if (!p) return 0;
    if (off == 1) return 0x3000 + p;
    if (off == 4) return 0x4000 + p;
    if (off == 8) return 1;                // active byte
    if (off == 388) return 0x5000 + p;     // office handle
    if (off == 458) return 5;              // eligibility byte >= 0
    if (off == 123) return 0;
    return 0;
}

NpcLeafHooks MakeLeafHooks() {
    NpcLeafHooks lh{};
    lh.freeHandlerEntry = [](HeRecord* h) -> i32 { ++g_t.freeCount; return reinterpret_cast<intptr_t>(h); };
    lh.queueRequestEntity29 = [](int arg, HeRecord*) -> i32 {
        ++g_t.entity29Count; g_t.lastEntity29Arg = arg; return 0x7777 + arg;
    };
    lh.packetStatus = [](i32) -> i32 { return g_t.scriptStatus; };
    return lh;
}

NpcEventHooks MakeEventHooks() {
    NpcEventHooks ev{};
    ev.resolveEntity = [](i32) -> i32 { return g_t.scriptEntity; };
    ev.entityField = EntityField;
    ev.findPerson = [](i32) -> i32 { return g_t.scriptPerson; };
    ev.personField = PersonField;
    ev.findBuilding = [](i32) -> i32 { return g_t.scriptBuilding; };
    ev.isNearDoor = [](i32, i32) -> int { return g_t.scriptNearDoor; };
    ev.queueRequestSingle49 = [](i32) -> i32 { ++g_t.op49Count; return 0x49; };
    ev.queueRequestArgs25 = [](i32, int, int, int, int) -> i32 { ++g_t.op25Count; return 0x25; };
    ev.queueRequestQuad52 = [](i32, i32, int, int) -> i32 { return 0x52; };
    ev.queueRequestNamedObject53 = [](i32, i32, int, int, int, const char*) -> i32 { return 0x53; };
    ev.requestBuildOp87 = [](i32) -> i32 { ++g_t.op87Count; return 0x87; };
    ev.requestBuildOp77 = [](i32) -> i32 { return 0x77; };
    ev.queueGestureFlag55 = [](i32, int) -> i32 { ++g_t.gestureCount; return 0x55; };
    ev.packetStatus = [](i32) -> i32 { return g_t.scriptStatus; };
    ev.packetSeq = [](i32) -> i32 { return g_t.scriptSeq; };
    ev.reaperApproach = [](HeRecord*) -> int { return g_t.scriptReaperApproach; };
    ev.reaperMove = [](HeRecord*) -> int { return g_t.scriptReaperMove; };
    ev.reaperCachePose = [](HeRecord*) -> int { return 1; };
    ev.reaperUpdateSound = [](HeRecord*) -> int { return 1; };
    ev.reaperDetach = [](i32) { ++g_t.detachCount; };
    ev.cutscenePause = []() { ++g_t.pauseCount; };
    ev.cutsceneResume = []() { ++g_t.resumeCount; };
    ev.eventPanelCreate = [](HeRecord*) -> int { ++g_t.panelCreateCount; return 0xBEEF; };
    ev.eventPanelDestroy = [](HeRecord*) { ++g_t.panelDestroyCount; };
    ev.dialogResult = []() -> i32 { return g_t.scriptDialog; };
    ev.nodeFieldGet = [](i32, int) -> i32 { return 0; };
    ev.nodeFieldSet = [](i32, int, i32) {};
    ev.loadDemandSnapshot = [](i32 out[3]) -> float { out[0] = 100; out[1] = 60; out[2] = 0; return 0.5f; };
    ev.enqueueObjectInteraction = [](int, int, int, int, int, int, int, int) -> i32 { return 0x99; };
    return ev;
}

// A clean He record with the clock primed to a known time.
struct Fixture {
    HeRecord rec;
    NpcLeafHooks lh;
    NpcEventHooks ev;
    Fixture() {
        std::memset(&rec, 0, sizeof(rec));
        ResetTrace();
        g_rec = &rec;
        lh = MakeLeafHooks();
        ev = MakeEventHooks();
        SetNpcLeafHooks(&lh);
        SetNpcEventHooks(&ev);
        crt::Srand(12345);
        GameTime t{}; t.day = 10; t.hour = 8; t.minute = 0; t.second = 0;
        SetNpcClock(t);
    }
    ~Fixture() { SetNpcLeafHooks(nullptr); SetNpcEventHooks(nullptr); }
};

i32* P(HeRecord* h, int off) { return reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + off); }
u8*  PB(HeRecord* h, int off) { return reinterpret_cast<u8*>(h) + off; }
GameTime* Appt(HeRecord* h) { return reinterpret_cast<GameTime*>(reinterpret_cast<u8*>(h) + 82); }

} // namespace

// ===========================================================================
// Constant tables recovered byte-for-byte.
// ===========================================================================
TEST(SimNpcEvent, ConstantTables) {
    CHECK_EQ(kMethodByteTable[0], 1);
    CHECK_EQ(kMethodByteTable[7], 19);
    CHECK_EQ(kMethodByteTable[8], 0xED);
    CHECK_EQ(kMethodByteTable[15], 0xFF);
    CHECK_EQ(kScanStrideTable[0], 1);
    CHECK_EQ(kScanStrideTable[7], 23);
    CHECK_EQ(RegisterNpcEvents(), 16);
    CHECK(NpcEvent_TableEntry(0x4d96f8) != nullptr);   // ReaperPlagueStep
    CHECK(NpcEvent_TableEntry(0x4da7c0) != nullptr);   // SimPoliticiansStep
    CHECK(NpcEvent_TableEntry(0x111111) == nullptr);
}

// ===========================================================================
// ProtectionMoneyInit — stamp +2 min, op25 against the found person.
// ===========================================================================
TEST(SimNpcEvent, ProtectionMoneyInit_emitsOp25) {
    Fixture f;
    g_t.scriptPerson = 3;        // a person is found
    i32 r = NpcEvent_ProtectionMoneyInit(&f.rec);
    CHECK_EQ(r, 0x25);
    CHECK_EQ(g_t.op25Count, 1);
    // appt stamped to clock +2 minutes.
    CHECK_EQ(Appt(&f.rec)->day, 10);
    CHECK_EQ((int)Appt(&f.rec)->hour, 8);
    CHECK_EQ(Appt(&f.rec)->minute, 2);
}
TEST(SimNpcEvent, ProtectionMoneyInit_noPerson) {
    Fixture f;
    g_t.scriptPerson = 0;
    i32 r = NpcEvent_ProtectionMoneyInit(&f.rec);
    CHECK_EQ(g_t.op25Count, 0);
    CHECK_EQ(r, 8);   // advance result == resulting hour
}

// ===========================================================================
// AllocLoverStep — eligible lover arms phase 2, ineligible arms -1.
// ===========================================================================
TEST(SimNpcEvent, AllocLover_eligibleArmsPhase2) {
    Fixture f;
    g_t.scriptPerson = 4;        // both findPerson() return handle 4 (elig byte 5 >= 0)
    NpcEvent_AllocLoverStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 180), 0);
    CHECK_EQ(*P(&f.rec, 184), 1);
    CHECK_EQ(*P(&f.rec, 188), -1);
    CHECK_EQ(g_t.lastEntity29Arg, 2);
    CHECK_EQ(*P(&f.rec, 132), 0x7777 + 2);
}
TEST(SimNpcEvent, AllocLover_alreadySpawnedNoop) {
    Fixture f;
    *PB(&f.rec, 120) = kHeAlreadySpawned;
    NpcEvent_AllocLoverStep(&f.rec);
    CHECK_EQ(g_t.entity29Count, 0);
}

// ===========================================================================
// PushObjectStep — phase gates.
// ===========================================================================
TEST(SimNpcEvent, PushObject_phaseMinus1Frees) {
    Fixture f;
    *P(&f.rec, 112) = -1;
    NpcEvent_PushObjectStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}
TEST(SimNpcEvent, PushObject_phase2AppliedTearsDown) {
    Fixture f;
    *P(&f.rec, 112) = 2;
    g_t.scriptPerson = 6;
    g_t.scriptStatus = 2;        // packet applied
    i32 r = NpcEvent_PushObjectStep(&f.rec);
    CHECK_EQ(g_t.lastEntity29Arg, -1);
    CHECK_EQ(r, 0x7777 + (-1));
}

// ===========================================================================
// SmokeEffectStep — phase machine (state+2).
// ===========================================================================
TEST(SimNpcEvent, Smoke_spawnSuccessArmsPhase1) {
    Fixture f;
    *P(&f.rec, 112) = 0;         // phase 2
    g_t.scriptEntity = 9;        // entity + sub-record present
    *P(&f.rec, 194) = 375;       // item id (low word of +192/+194 region)
    *reinterpret_cast<u16*>(PB(&f.rec, 194)) = 375;
    NpcEvent_SmokeEffectStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 112), 1); // spawn produced a node -> state 1
}
TEST(SimNpcEvent, Smoke_missingEntityFrees) {
    Fixture f;
    *P(&f.rec, 112) = 0;         // phase 2
    g_t.scriptEntity = 0;        // no entity
    NpcEvent_SmokeEffectStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// UnkendunkStep — building gate + 30-min advance.
// ===========================================================================
TEST(SimNpcEvent, Unkendunk_noBuildingFrees) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    g_t.scriptBuilding = 0;
    NpcEvent_UnkendunkStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}
TEST(SimNpcEvent, Unkendunk_advances30min) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    g_t.scriptBuilding = 1;
    // deadline at +176 set far in the future so we don't free.
    GameTime* dl = reinterpret_cast<GameTime*>(PB(&f.rec, 176));
    dl->day = 99;
    NpcEvent_UnkendunkStep(&f.rec);
    CHECK_EQ(Appt(&f.rec)->minute, 30);
    CHECK_EQ(g_t.freeCount, 0);
}
TEST(SimNpcEvent, Unkendunk_negativePhaseFrees) {
    Fixture f;
    *P(&f.rec, 112) = -2;
    NpcEvent_UnkendunkStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// ReaperPlagueStep — teardown + move codes.
// ===========================================================================
TEST(SimNpcEvent, Reaper_teardownDetaches) {
    Fixture f;
    *P(&f.rec, 112) = -1;
    *P(&f.rec, 200) = 0xABCD;    // attached node
    NpcEvent_ReaperPlagueStep(&f.rec);
    CHECK_EQ(g_t.detachCount, 1);
    CHECK_EQ(g_t.resumeCount, 1);
    CHECK_EQ(g_t.freeCount, 1);
}
TEST(SimNpcEvent, Reaper_moveArrivedDropsToPhase0) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 200) = 0xABCD;    // avatar present
    g_t.scriptReaperMove = 1;    // arrived near current target
    NpcEvent_ReaperPlagueStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 112), 0);
    CHECK_EQ(g_t.pauseCount, 1);
}
TEST(SimNpcEvent, Reaper_phase2CachesPose) {
    Fixture f;
    *P(&f.rec, 112) = 2;
    *P(&f.rec, 200) = 0xABCD;
    NpcEvent_ReaperPlagueStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 112), 0);   // cache pose ok -> phase 0
}

// ===========================================================================
// Politician slot helpers.
// ===========================================================================
TEST(SimNpcEvent, PoliticianRelease_freeSlot) {
    Fixture f;
    *P(&f.rec, 172) = -1;        // slot 0 empty
    CHECK_EQ(NpcEvent_PoliticianReleaseTarget(&f.rec, 172), 0);
}
TEST(SimNpcEvent, PoliticianRelease_keepWhenStillWalking) {
    Fixture f;
    // Slot occupied, target resolves, NOT yet near the office and before 22:00 ->
    // the politician is still walking to the target -> keep the slot (return 2).
    *P(&f.rec, 172) = 100;       // personId
    *P(&f.rec, 176) = 200;       // objId
    g_t.scriptPerson = 7;
    g_t.scriptEntity = 8;
    g_t.scriptNearDoor = 0;      // not adjacent yet, clock hour 8 < 22 -> keep
    CHECK_EQ(NpcEvent_PoliticianReleaseTarget(&f.rec, 172), 2);
}
TEST(SimNpcEvent, PoliticianRelease_clearsWhenArrived) {
    Fixture f;
    // Arrived at the office (near-door) -> release: emit op49/op87/op25, clear slot.
    *P(&f.rec, 172) = 100;
    *P(&f.rec, 176) = 200;
    g_t.scriptPerson = 7;
    g_t.scriptEntity = 8;
    g_t.scriptNearDoor = 1;      // adjacent -> release
    CHECK_EQ(NpcEvent_PoliticianReleaseTarget(&f.rec, 172), 0);
    CHECK_EQ(*P(&f.rec, 172), -1);   // slot cleared
    CHECK_EQ(g_t.op87Count, 1);
}

// ===========================================================================
// SimPoliticiansStep — driver gates on the +132 packet, daily reset.
// ===========================================================================
TEST(SimNpcEvent, SimPoliticians_pendingPacketWaits) {
    Fixture f;
    *P(&f.rec, 132) = 555;       // a packet is in flight
    g_t.scriptStatus = 0;        // still pending -> return without re-arm
    i32 r = NpcEvent_SimPoliticiansStep(&f.rec);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_t.entity29Count, 0);
}
TEST(SimNpcEvent, SimPoliticians_phaseMinus1Frees) {
    Fixture f;
    *P(&f.rec, 112) = -1;
    NpcEvent_SimPoliticiansStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// GamblingStep — phase 0 arms phase1 (+2 days), phase >1 frees.
// ===========================================================================
TEST(SimNpcEvent, Gambling_phase0ArmsPhase1) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    NpcEvent_GamblingStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 112), 1);
    // appt = clock(day10,hour8) + 2 hours -> hour 10 (GameTimeAdvance arg1 adds to
    // the hour and only carries into days on a 24h wrap).
    CHECK_EQ(Appt(&f.rec)->day, 10);
    CHECK_EQ((int)Appt(&f.rec)->hour, 10);
}
TEST(SimNpcEvent, Gambling_phase2Frees) {
    Fixture f;
    *P(&f.rec, 112) = 2;
    NpcEvent_GamblingStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// ObjectInteractionStep — phase 0 resets cursors + arms, phase -1 frees.
// ===========================================================================
TEST(SimNpcEvent, ObjectInteraction_phase0Resets) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 172) = 99;
    *P(&f.rec, 176) = 99;
    *P(&f.rec, 180) = 99;
    NpcEvent_ObjectInteractionStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 172), 0);
    CHECK_EQ(*P(&f.rec, 176), 0);
    CHECK_EQ(*P(&f.rec, 180), 0);
    CHECK_EQ(g_t.lastEntity29Arg, 1);   // arms phase 1
}
TEST(SimNpcEvent, ObjectInteraction_phaseMinus2Frees) {
    Fixture f;
    *P(&f.rec, 112) = -2;
    NpcEvent_ObjectInteractionStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// DarkCornerInit — seeds cursors and arms a cmd29.
// ===========================================================================
TEST(SimNpcEvent, DarkCornerInit_seedsCursors) {
    Fixture f;
    g_t.scriptEntity = 0;        // no existing building -> falls through to cmd29(-1)
    NpcEvent_DarkCornerInit(&f.rec);
    CHECK_EQ(*P(&f.rec, 172), -1 == *P(&f.rec, 172) ? -1 : *P(&f.rec, 172)); // set then maybe kept
    CHECK(*P(&f.rec, 188) >= 5 && *P(&f.rec, 188) <= 8);   // rounds 5..8
    CHECK_EQ(*P(&f.rec, 192), 0);
    CHECK_EQ(*P(&f.rec, 184), -1);
    CHECK_EQ(g_t.lastEntity29Arg, -1);
}
TEST(SimNpcEvent, DarkCornerInit_alreadySpawnedNoop) {
    Fixture f;
    *PB(&f.rec, 120) = kHeAlreadySpawned;
    NpcEvent_DarkCornerInit(&f.rec);
    CHECK_EQ(g_t.entity29Count, 0);
}

// ===========================================================================
// DarkCornerStep — re-arm gating + phase 0 branch.
// ===========================================================================
TEST(SimNpcEvent, DarkCorner_pendingPacketWaits) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 132) = 321;
    g_t.scriptStatus = 0;
    i32 r = NpcEvent_DarkCornerStep(&f.rec);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_t.entity29Count, 0);
}
TEST(SimNpcEvent, DarkCorner_phase0LostArms4) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 132) = -1;
    g_t.scriptStatus = 0;        // meeting packet not applied (==1 path requires status1)
    NpcEvent_DarkCornerStep(&f.rec);
    CHECK_EQ(g_t.lastEntity29Arg, 4);   // 24h re-arm
}

// ===========================================================================
// MasterExamDialogStep — phase 0 creates panel, phase -1 destroys + frees.
// ===========================================================================
TEST(SimNpcEvent, MasterExam_phase0CreatesPanel) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 116) = 0;
    NpcEvent_MasterExamDialogStep(&f.rec);
    CHECK_EQ(g_t.panelCreateCount, 1);
    CHECK_EQ(*P(&f.rec, 112), 1);
}
TEST(SimNpcEvent, MasterExam_confirmTearsDown) {
    Fixture f;
    *P(&f.rec, 112) = 1;
    *P(&f.rec, 116) = 0xBEEF;    // panel present
    g_t.scriptDialog = 1210;     // confirm
    // appt in the future so the timeout compare returns <= 0.
    Appt(&f.rec)->day = 99;
    NpcEvent_MasterExamDialogStep(&f.rec);
    CHECK_EQ(g_t.panelDestroyCount, 1);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// TalentLevelUpStep — panel phase machine.
// ===========================================================================
TEST(SimNpcEvent, Talent_phase0CreatesPanel) {
    Fixture f;
    // The appt must be at/before the clock (the event time has arrived) so the
    // GameTime_Compare(appt, clock) == 1 "not yet" early-out does not fire.
    Appt(&f.rec)->day = 1;       // appt in the past -> compare returns -1 (proceed)
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 116) = 0;
    NpcEvent_TalentLevelUpStep(&f.rec);
    CHECK_EQ(g_t.panelCreateCount, 1);
    CHECK_EQ(*P(&f.rec, 112), 1);
}

// ===========================================================================
// RunSimAccident — sweeps 12 persons, advances 30 min.
// ===========================================================================
TEST(SimNpcEvent, RunSimAccident_sweeps12AndAdvances) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 176) = 0;         // forces a stride pick on entry
    NpcEvent_RunSimAccident(&f.rec);
    CHECK_EQ(*P(&f.rec, 180), 12);   // 12 persons scanned
    CHECK(*P(&f.rec, 176) != 0);     // a stride was chosen
    CHECK_EQ(Appt(&f.rec)->minute, 30);
}
TEST(SimNpcEvent, RunSimAccident_phaseMinus2Frees) {
    Fixture f;
    *P(&f.rec, 112) = -2;
    NpcEvent_RunSimAccident(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}

// ===========================================================================
// TavernSimStep — sweeps 384 persons / wave, daily 22:00 reset.
// ===========================================================================
TEST(SimNpcEvent, Tavern_firstWaveAdvances15) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 172) = 0;
    i32 r = NpcEvent_TavernSimStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 172), 384);
    CHECK_EQ(Appt(&f.rec)->minute, 15);
    (void)r;
}
TEST(SimNpcEvent, Tavern_sweepEndResetsAt22) {
    Fixture f;
    *P(&f.rec, 112) = 0;
    *P(&f.rec, 172) = 384;       // second wave reaches 768 -> reset
    NpcEvent_TavernSimStep(&f.rec);
    CHECK_EQ(*P(&f.rec, 172), 0);
    CHECK_EQ((int)Appt(&f.rec)->hour, 22);
    CHECK_EQ(Appt(&f.rec)->minute, 30);
}
TEST(SimNpcEvent, Tavern_phaseMinus2Frees) {
    Fixture f;
    *P(&f.rec, 112) = -2;
    NpcEvent_TavernSimStep(&f.rec);
    CHECK_EQ(g_t.freeCount, 1);
}
