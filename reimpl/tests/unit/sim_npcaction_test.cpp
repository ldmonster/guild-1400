// Unit tests for the NpcAction / NpcEvent behavior layer (gilde.exe VIBE_NpcAction_*
// / VIBE_NpcEvent_* / VIBE_Npc_AdjustRelationByMood). Golden values for the RNG-
// driven steps are computed with the same LCG the CRT uses (state = state*
// 1103515245 + 12345; RandNext = (state>>16)&0x7FFF; RandomModulo = RandNext()%n).
#include "tests/framework/test.h"

#include "sim/he.h"
#include "sim/npcaction.h"
#include "sim/npcevent.h"
#include "sim/gametime.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

HeRecord MakeHe() {
    HeRecord h;
    std::memset(&h, 0, sizeof(h));
    return h;
}

GameTime MakeClock(i32 day, u16 hour, i32 minute, i32 second) {
    GameTime t{}; t.day = day; t.hour = hour; t.minute = minute; t.second = second;
    return t;
}

// Recording leaf-hook mock.
struct Recorder {
    int cmd29Calls = 0;
    int lastCmd29Arg = 0;
    i32 nextHandle = 0;
    int freeCalls = 0;
    int op93Calls = 0;
    i32 op93Id = 0; int op93Kind = 0; u8 op93Amount = 0;
    int invCount = -1;   // -1 == absent
    i32 lawId = 51;
};
Recorder g_rec;

i32 HookCmd29(int arg, HeRecord*) { g_rec.cmd29Calls++; g_rec.lastCmd29Arg = arg; return g_rec.nextHandle; }
i32 HookFree(HeRecord*) { g_rec.freeCalls++; return 0; }
i32 HookStatus(i32) { return 1; }
i32 HookLaw() { return g_rec.lawId; }
int HookInv(int, i32* count) { if (g_rec.invCount < 0) return 0; *count = g_rec.invCount; return 1; }
void HookShuffle(int n, i32* dst) { for (int i = 0; i < n; ++i) dst[i] = i; } // identity perm
void HookOp93(i32 id, int kind, i32, u8 amount) {
    g_rec.op93Calls++; g_rec.op93Id = id; g_rec.op93Kind = kind; g_rec.op93Amount = amount;
}

NpcLeafHooks MakeHooks() {
    NpcLeafHooks h{};
    h.queueRequestEntity29 = HookCmd29;
    h.freeHandlerEntry = HookFree;
    h.packetStatus = HookStatus;
    h.lawBaseTextId = HookLaw;
    h.findInventorySlot = HookInv;
    h.shuffleDwords = HookShuffle;
    h.requestBuildOp93 = HookOp93;
    return h;
}

void Install() {
    g_rec = Recorder{};
    static NpcLeafHooks hooks;
    hooks = MakeHooks();
    SetNpcLeafHooks(&hooks);
}

} // namespace

// --- He record layout ---------------------------------------------------------
TEST(SimNpcActionLayout, HandlerOffsets) {
    CHECK_EQ(offsetof(HeRecord, id), 4u);
    CHECK_EQ(offsetof(HeRecord, cityIndex), 8u);
    CHECK_EQ(offsetof(HeRecord, cityId), 12u);
    CHECK_EQ(offsetof(HeRecord, savedTime), 68u);
    CHECK_EQ(offsetof(HeRecord, apptTime), 82u);
    CHECK_EQ(offsetof(HeRecord, state), 112u);
    CHECK_EQ(offsetof(HeRecord, flags), 120u);
    CHECK_EQ(offsetof(HeRecord, reqHandle), 132u);
    CHECK_EQ(offsetof(HeRecord, counter), 172u);
    CHECK_EQ(offsetof(HeRecord, deadline), 176u);
    CHECK_EQ(sizeof(GameTime), 14u);   // appointment image size
}

// --- dispatch table -----------------------------------------------------------
TEST(SimNpcActionDispatch, TableByteFaithful) {
    CHECK_EQ(kNpcActionTableSize, 69);
    // Spot-check the recovered jump-table addresses (funcs_5766CB @0x63d964).
    CHECK_EQ(kNpcActionTableAddrs[0], 0x5712a0u);
    CHECK_EQ(kNpcActionTableAddrs[45], 0x575028u);  // VIBE_DebugCmd_RetZero slot
    CHECK_EQ(kNpcActionTableAddrs[68], 0x576618u);
    // Entries 0x35..0x37 alias the same target (0x57477c) in the original.
    CHECK_EQ(kNpcActionTableAddrs[53], kNpcActionTableAddrs[54]);
    CHECK_EQ(kNpcActionTableAddrs[54], kNpcActionTableAddrs[55]);
}

TEST(SimNpcActionDispatch, GuardsAndRouting) {
    Install();
    HeRecord h = MakeHe();
    // day < 8 guard returns 0 regardless of type.
    SetNpcClock(MakeClock(3, 0, 0, 0));
    CHECK_EQ(NpcAction_Dispatch(&h), 0);
    // null record -> -1.
    SetNpcClock(MakeClock(10, 0, 0, 0));
    CHECK_EQ(NpcAction_Dispatch(nullptr), -1);
    // type >= 0x45 -> -2 (type read from record+4 word).
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(&h) + 4) = 0x45;
    CHECK_EQ(NpcAction_Dispatch(&h), -2);
    // type 45 maps to the RetZero leaf -> 0.
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(&h) + 4) = 45;
    CHECK_EQ(NpcAction_Dispatch(&h), 0);
    // a deferred entry (type 0) returns 0 (inert no-op leaf).
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(&h) + 4) = 0;
    CHECK_EQ(NpcAction_Dispatch(&h), 0);
}

// --- timestamp / appointment helpers -----------------------------------------
TEST(SimNpcActionTime, StampAndRequestEntity) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 8, 30, 5));
    He_Flags(&h) = 0;                       // no cmd29
    NpcAction_StampTimeAndRequestEntity(&h);
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 8);
    CHECK_EQ(He_ApptTime(&h).minute, 30);
    CHECK_EQ(g_rec.cmd29Calls, 0);
    // with flag 0x02 set -> requests a cmd29 (arg -1).
    He_Flags(&h) = kHeNeedsCmd29;
    NpcAction_StampTimeAndRequestEntity(&h);
    CHECK_EQ(g_rec.cmd29Calls, 1);
    CHECK_EQ(g_rec.lastCmd29Arg, -1);
}

TEST(SimNpcActionTime, CopyAndClearTargetCoord) {
    Install();
    HeRecord h = MakeHe();
    He_SavedTime(&h) = MakeClock(2, 4, 6, 8);
    SetNpcClock(MakeClock(99, 1, 1, 1));
    NpcAction_CopyTargetCoord(&h);          // +82 <- +68
    CHECK_EQ(He_ApptTime(&h).day, 2);
    CHECK_EQ(He_ApptTime(&h).hour, 4);
    NpcAction_ClearTargetCoord(&h);         // +82 <- clock
    CHECK_EQ(He_ApptTime(&h).day, 99);
    CHECK_EQ(He_ApptTime(&h).hour, 1);
}

TEST(SimNpcActionTime, SetTargetCityRef) {
    Install();
    SetNpcCityIdResolver([](u16 idx) -> i32 { return 1000 + idx; });
    HeRecord h = MakeHe();
    CHECK_EQ(NpcAction_SetTargetCityRef(&h, 7), 1007);
    CHECK_EQ(He_CityIndex(&h), 7);
    CHECK_EQ(He_CityId(&h), 1007);
    CHECK_EQ(NpcAction_SetTargetCityRef(&h, 0xFFFF), -1);
    CHECK_EQ(He_CityId(&h), -1);
    SetNpcCityIdResolver(nullptr);
}

TEST(SimNpcActionTime, ResetToState0) {
    Install();
    HeRecord h = MakeHe();
    He_State(&h) = 99;
    SetNpcClock(MakeClock(10, 5, 0, 0));
    int hr = NpcAction_ResetToState0(&h);   // +2 into the hour-accumulator
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 7);      // 5 + 2
    CHECK_EQ(hr, 7);                        // returns the resulting hour
    CHECK_EQ(He_State(&h), 0);
}

TEST(SimNpcActionTime, AddTimeToActionDuration) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 0, 0, 0));    // hour 0
    crt::Srand(123);
    int r = NpcAction_AddTimeToActionDuration(&h);
    // golden: hour += mod(21)=7 ; minute := mod(0x3B)=45
    CHECK_EQ(He_ApptTime(&h).hour, 7);
    CHECK_EQ(He_ApptTime(&h).minute, 45);
    CHECK_EQ(He_ApptTime(&h).second, 0);
    CHECK_EQ(r, 45);
}

// --- walk-begin family --------------------------------------------------------
TEST(SimNpcActionWalk, BeginFamilyDeltas) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcAction_BeginWalkPhase4(&h);          // +4 into the hour-accumulator (8->12)
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 12);
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcAction_BeginWalkAndFace(&h);         // +1 second
    CHECK_EQ(He_ApptTime(&h).second, 1);
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcAction_BeginGenericWalkStep(&h);
    CHECK_EQ(He_ApptTime(&h).second, 1);
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcAction_BeginCombatWalkStep(&h);
    CHECK_EQ(He_ApptTime(&h).second, 1);
}

// --- idle anim arming ---------------------------------------------------------
TEST(SimNpcActionIdle, BeginIdleAnim) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 8, 30, 0));
    crt::Srand(12345);
    NpcAction_BeginIdleAnim(&h);
    // golden: v2=mod(30)=18 -> addDays(1) into hour-accumulator (8->9, same day),
    // +(18-20)=-2 minutes (30->28). => day 10, hour 9, minute 28.
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 9);
    CHECK_EQ(He_ApptTime(&h).minute, 28);
}

TEST(SimNpcActionIdle, BeginIdleWaitState) {
    Install();
    HeRecord h = MakeHe();
    g_rec.nextHandle = 0x4242;
    SetNpcClock(MakeClock(10, 8, 0, 0));
    crt::Srand(5);
    He_Flags(&h) = 0;
    NpcAction_BeginIdleWaitState(&h);
    // appt: +1 to hour-accumulator (8->9, same day); deadline: +24h (wraps a day);
    // wait counter = mod(4)+19 = 22; handle stored.
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 9);
    CHECK_EQ(He_Deadline(&h).day, 11);      // 10 + 24h carry
    CHECK_EQ(He_WaitCounter(&h), 22);
    CHECK_EQ(He_ReqHandle(&h), 0x4242);
    CHECK_EQ(g_rec.cmd29Calls, 1);
    // already-spawned flag short-circuits the whole setup.
    HeRecord h2 = MakeHe();
    He_Flags(&h2) = kHeAlreadySpawned;
    g_rec.cmd29Calls = 0;
    NpcAction_BeginIdleWaitState(&h2);
    CHECK_EQ(g_rec.cmd29Calls, 0);
    CHECK_EQ(He_ApptTime(&h2).day, 0);      // untouched
}

// --- social greet / flirt / compliment + mood adjuster ------------------------
TEST(SimNpcActionSocial, ResolveTrioMoodDeltas) {
    Install();
    HeRecord actor = MakeHe();
    *(reinterpret_cast<u8*>(&actor) + 2) = 0;    // rank != 6 -> Person path
    HeRecord target = MakeHe();
    He_Id(&target) = 777;
    crt::Srand(2024);
    CHECK_EQ(NpcAction_ResolveTargetAndGreet(&actor, &target), 1);
    CHECK_EQ(g_rec.op93Calls, 1);
    CHECK_EQ(g_rec.op93Kind, 1);                 // greet -> mood kind 1
    CHECK_EQ(g_rec.op93Id, 777);
    // flirt -> kind 3, compliment -> kind 4.
    crt::Srand(2024);
    NpcAction_ResolveTargetAndFlirt(&actor, &target);
    CHECK_EQ(g_rec.op93Kind, 3);
    crt::Srand(2024);
    NpcAction_ResolveTargetAndCompliment(&actor, &target);
    CHECK_EQ(g_rec.op93Kind, 4);
    // no target -> returns 0, no command.
    g_rec.op93Calls = 0;
    CHECK_EQ(NpcAction_ResolveTargetAndGreet(&actor, nullptr), 0);
    CHECK_EQ(g_rec.op93Calls, 0);
}

TEST(SimNpcActionSocial, AdjustRelationByMood) {
    Install();
    HeRecord person = MakeHe();
    He_Id(&person) = 55;
    *(reinterpret_cast<u8*>(&person) + 128 + 1) = 0;  // mood byte at +128+kind
    crt::Srand(2024);
    int rc = NpcAdjustRelationByMood(&person, 1);
    // golden: rf=0.061401, amp=0.799997, inc=11.46 -> delta 11.
    CHECK_EQ(rc, 1);
    CHECK_EQ(g_rec.op93Calls, 1);
    CHECK_EQ(g_rec.op93Amount, 11);
    // kind >= 5 rejected.
    CHECK_EQ(NpcAdjustRelationByMood(&person, 5), 0);
    // already at ceiling -> 0, no command.
    *(reinterpret_cast<u8*>(&person) + 128 + 2) = 252;
    g_rec.op93Calls = 0;
    CHECK_EQ(NpcAdjustRelationByMood(&person, 2), 0);
    CHECK_EQ(g_rec.op93Calls, 0);
}

// --- NpcEvent setup / reset ---------------------------------------------------
TEST(SimNpcEvent, RestorePoseResetClearsStateAndWait) {
    Install();
    HeRecord h = MakeHe();
    He_SavedTime(&h) = MakeClock(3, 9, 0, 0);
    He_State(&h) = 7;
    He_WaitCounter(&h) = 5;
    NpcEvent_RestorePoseReset(&h);
    CHECK_EQ(He_ApptTime(&h).day, 3);            // +82 <- +68
    CHECK_EQ(He_State(&h), 0);
    CHECK_EQ(He_WaitCounter(&h), 0);
}

TEST(SimNpcEvent, RestorePoseSetRandom) {
    Install();
    HeRecord h = MakeHe();
    He_SavedTime(&h) = MakeClock(4, 12, 0, 0);
    crt::Srand(7);
    int r = NpcEvent_RestorePoseSetRandom(&h);
    CHECK_EQ(r, 4);                              // mod(6)=4
    CHECK_EQ(He_WaitCounter(&h), 4);
    CHECK_EQ(He_ApptTime(&h).day, 4);
}

TEST(SimNpcEvent, SetupDuration10Reset) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 22, 33, 44));
    He_Counter(&h) = 99; He_WaitCounter(&h) = 9; He_Deadline(&h).day = 5;
    NpcEvent_SetupDuration10Reset(&h);
    CHECK_EQ(He_ApptTime(&h).hour, 10);          // hour forced to 10
    CHECK_EQ(He_ApptTime(&h).minute, 0);
    CHECK_EQ(He_ApptTime(&h).second, 0);
    CHECK_EQ(He_Counter(&h), 0);
    CHECK_EQ(He_Deadline(&h).day, 0);
    CHECK_EQ(He_WaitCounter(&h), 0);
}

TEST(SimNpcEvent, InitRandomDurationEntity) {
    Install();
    g_rec.nextHandle = 0xABCD;
    g_rec.lawId = 51;
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 0, 0, 0));
    He_Flags(&h) = 0;
    NpcEvent_InitRandomDurationEntity(&h);
    CHECK_EQ(He_Counter(&h), 51);                // law base text id
    CHECK_EQ(He_WaitCounter(&h), 0xFFFFu);       // -1
    CHECK_EQ(He_ReqHandle(&h), (i32)0xABCD);
    CHECK_EQ(g_rec.cmd29Calls, 1);
    // already-spawned short-circuit.
    HeRecord h2 = MakeHe();
    He_Flags(&h2) = kHeAlreadySpawned;
    g_rec.cmd29Calls = 0;
    NpcEvent_InitRandomDurationEntity(&h2);
    CHECK_EQ(g_rec.cmd29Calls, 0);
}

TEST(SimNpcEvent, QueueState9Entity) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 15, 0, 0));
    NpcEvent_QueueState9Entity(&h);
    CHECK_EQ(He_ApptTime(&h).day, 11);           // +24h carry
    CHECK_EQ(He_ApptTime(&h).hour, 9);           // forced to 9
    CHECK_EQ(He_ApptTime(&h).minute, 0);
    CHECK_EQ(g_rec.cmd29Calls, 1);
    CHECK_EQ(g_rec.lastCmd29Arg, 0);
}

TEST(SimNpcEvent, ResetAndQueueEntity) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcEvent_ResetAndQueueEntity(&h);
    CHECK_EQ(He_SavedTime(&h).day, 10);          // saved <- clock
    CHECK_EQ(He_ApptTime(&h).minute, 1);         // +1 minute
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(&h) + 212), -1);
    CHECK_EQ(*(reinterpret_cast<u8*>(&h) + 220), 0);
    CHECK_EQ(g_rec.cmd29Calls, 1);
}

TEST(SimNpcEvent, SetupRandomDurationReset) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 0, 0, 0));
    crt::Srand(99);
    int m = NpcEvent_SetupRandomDurationReset(&h);
    // golden: hour = mod(3)+9 = 11 ; minute = 15*mod(4) = 15.
    CHECK_EQ(He_ApptTime(&h).hour, 11);
    CHECK_EQ(He_ApptTime(&h).minute, 15);
    CHECK_EQ(m, 15);
    CHECK_EQ(He_State(&h), 0);
    CHECK_EQ(He_Deadline(&h).day, 0);
}

TEST(SimNpcEvent, SetupTargetTimestampFreesWhenAbsent) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 8, 0, 0));
    g_rec.invCount = -1;                          // item 376 absent
    NpcEvent_SetupTargetTimestamp(&h);
    CHECK_EQ(g_rec.freeCalls, 1);                 // handler freed
    // present with count 30 -> deadline = clock + 30 minutes.
    Install();
    g_rec.invCount = 30;
    SetNpcClock(MakeClock(10, 8, 0, 0));
    NpcEvent_SetupTargetTimestamp(&h);
    CHECK_EQ(g_rec.freeCalls, 0);
    CHECK_EQ(He_Deadline(&h).day, 10);
    CHECK_EQ(He_Deadline(&h).minute, 30);
}

TEST(SimNpcEvent, InitTargetSlotsState12) {
    Install();
    HeRecord h = MakeHe();
    SetNpcClock(MakeClock(10, 0, 0, 0));
    NpcEvent_InitTargetSlotsState12(&h);
    // identity shuffle -> slots become 0..5, then each += 1 in place -> 1..6.
    for (int i = 0; i < 6; ++i)
        CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(&h) + 176 + 4 * i), i + 1);
    CHECK_EQ(He_ApptTime(&h).hour, 9);
    CHECK_EQ(He_State(&h), 12);
}

// --- gametime helpers used by the steps ---------------------------------------
TEST(SimNpcGameTime, CompareAndDiff) {
    GameTime a = MakeClock(10, 8, 0, 0);
    GameTime b = MakeClock(10, 8, 30, 0);
    CHECK_EQ(GameTimeCompare(&a, &b), -1);
    CHECK_EQ(GameTimeCompare(&b, &a), 1);
    CHECK_EQ(GameTimeCompare(&a, &a), 0);
    CHECK_EQ(GameTimeDiffMinutes(&a, &b), 30);
    GameTime c = MakeClock(11, 9, 0, 0);
    CHECK_EQ(GameTimeDiffMinutes(&a, &c), 1440 + 60);  // +1 day +1 hour
}
