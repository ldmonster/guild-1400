// play::SessionTick — per-frame driver of the reconstructed continuous
// game-clock chain. See session_tick.h for the call contract and
// src/sim/game_clock_tick.h for the recovered architecture.
//
// PROVENANCE (orchestration sites translated here):
//   0x527e52  StartTimer(0xE, 0)                  (App_InitSubsystemsAndMovieDll)
//   0x52f339  RegisterProc(clockProc, 71)         (Game_InitWorldAndSounds)
//   0x52f345  SetProcInterval(clockProc, 1)       (registered PAUSED, edx=1)
//   0x50f1a6  SetProcInterval(clockProc, 0)       (Scene_RunMainFrameLoop start)
//   0x4c0b8c  QueueRequestPerm30(&qword_122F840)  (GameLogic_RunFrameLoop)
//   0x4c0be5  Flush/Exec command window, dword_11AA488 = dword_62EB38 + 7
//   0x4c1324  day-end gate -> dword_63CC3C = 1, pause clock
//   0x533a54  turn-start sysmsg-3 clock sync; day fast-forward + rollover
//   0x498ba3  ExSysMessage case 3 (the sysmsg-3 executor semantics)
#include "play/session_tick.h"

#include <cstring>

#include "sim/command_apply6.h"
#include "sim/command_builders2.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// TimeBase proc trampoline. The binary registers the raw function pointer
// 0x527778 in the proc table (dword_B537C8); the clock state it touches is
// global. We mirror that: one active SessionTick services the registered slot.
// ---------------------------------------------------------------------------
namespace {
SessionTick* g_activeSessionTick = nullptr;
void SessionTickClockProc() {
    if (g_activeSessionTick)
        g_activeSessionTick->FireClockProc();
}
} // namespace

SessionTick::SessionTick() : timeBase_(nullptr) {
    queue_.Init();                       // VIBE_Command_QueueInitAndSync (init half)
    queue_.set_standalone(true);         // dword_764CE0 == -1
    sim::RegisterApplyHandlers6(queue_); // installs ExAdvanceGameTick @ opcode 0x1E

    clock_.world = &sim::g_tickClock;    // qword_13CE852 (the shared world clock)

    // 0x527e52: StartTimer(0xE, 0) — 14 ms tick period, continuous (a2==0 maps
    // to TIME_PERIODIC in timeSetEvent @0x44e28c).
    timeBase_.StartTimer(sim::kTimeBaseTickMs, 0);
    // 0x52f339: RegisterProc(clockProc, 71); 0x52f345: SetProcInterval(.., 1).
    timeBase_.RegisterProc(&SessionTickClockProc, sim::kClockProcIntervalTicks);
    timeBase_.SetProcPaused(&SessionTickClockProc, 1);

    g_activeSessionTick = this;
}

SessionTick::~SessionTick() {
    timeBase_.UnregisterProc(&SessionTickClockProc);
    if (g_activeSessionTick == this)
        g_activeSessionTick = nullptr;
}

const sim::GameTime& SessionTick::worldTime() const {
    return sim::g_tickClock;             // qword_13CE852
}

bool SessionTick::clockPaused() const {
    return const_cast<crt::TimeBase&>(timeBase_).IsProcPaused(&SessionTickClockProc) != 0;
}

void SessionTick::FireClockProc() {
    // The registered 0x527778 slot body (a fire counts whether or not the
    // 23:00 gate lets the advance through).
    ++clockFireCount_;
    sim::ClockComputeGameTimeOfDay(clock_);
}

// dword_1233558 = 40 * level — VIBE_Input_HandleGameSpeedKeys writes
// 40*(level+1) @0x4ff8b1 (bounded by level < 4 @0x4ff89b) and 40*(level-1)
// @0x4ff8ee (bounded by level > 0 @0x4ff8dc).
void SessionTick::SetGameSpeedLevel(int level) {
    if (level < 0)
        level = 0;
    if (level > sim::kGameSpeedMaxLevel)
        level = sim::kGameSpeedMaxLevel;
    clock_.gameSpeed = sim::kGameSpeedStep * level;
}

// ---------------------------------------------------------------------------
// Turn-start clock sync — InitOrLoadSession @0x533a54:
//   v97 = qword_13CE852; GameTime_Set(&v97, 6, 0, 0);
//   QueueRequestFlagBlob32(3, &v97)  ->  ExSysMessage case 3 @0x498ba3:
//     pause clock proc if running, write qword_13CE852 AND qword_122F840 from
//     the packet, unpause. (Applied with the executor's exact semantics; see
//     progress/session-tick.md for the apply5 wire-model gap note.)
// ---------------------------------------------------------------------------
void SessionTick::SyncClocksToDayStart() {
    sim::GameTime t = sim::g_tickClock;            // v97 = qword_13CE852
    sim::GameTimeSet(&t, sim::kDayStartHour, 0, 0); // GameTime_Set(&v97, 6,0,0)

    const int wasPaused = timeBase_.IsProcPaused(&SessionTickClockProc);
    if (!wasPaused)
        timeBase_.SetProcPaused(&SessionTickClockProc, 1); // 0x498c0a
    sim::g_tickClock = t;                          // 0x498bb7..0x498bdc
    clock_.master = t;                             // 0x498be2..0x498be5
    if (!wasPaused)
        timeBase_.SetProcPaused(&SessionTickClockProc, 0); // 0x498bf6
}

// Top-of-round reset (InitOrLoadSession @0x534474/0x53447a) + live-scene clock
// start (Scene_RunMainFrameLoop prologue @0x50f19f..0x50f1a6).
void SessionTick::BeginDay() {
    roundEndRequested_ = false;  // dword_11AA480 = 0
    dayEndLatched_ = false;      // dword_63CC3C = 0
    timeBase_.SetProcPaused(&SessionTickClockProc, 0); // SetProcInterval(.., 0)
}

// ---------------------------------------------------------------------------
// Commit a time image through the REAL opcode-30 pipeline.
//   QueueRequestPerm30 @0x494a50: payload a1@+0x10 a2@+0x14 a3@+0x18 a4@+0x1C,
//   gated on GameTime_Compare(t, &qword_13CE852) != 0;
//   FlushSendQueue @0x4934cc (standalone loopback) -> ExecCommands @0x494088
//   -> ExAdvanceGameTick @0x498954 (world commit + cascade). The original
//   spins on GetPacketStatusById (e.g. @0x5344ee); standalone applies inline.
// ---------------------------------------------------------------------------
bool SessionTick::CommitTimePacket(const sim::GameTime& t) {
    u8 img[sizeof(sim::GameTime)];
    static_assert(sizeof(sim::GameTime) == 14, "GameTime image must be 14 bytes");
    std::memcpy(img, &t, sizeof img);
    i32 a1, a2, a3;
    i16 a4;
    std::memcpy(&a1, img + 0, 4);   // v6 = *a1
    std::memcpy(&a2, img + 4, 4);   // v7 = *(a1+1)
    std::memcpy(&a3, img + 8, 4);   // v8 = *(a1+2)
    std::memcpy(&a4, img + 12, 2);  // v9 = *((WORD*)(a1+2)+2)

    const bool valid = sim::GameTimeCompare(&t, &sim::g_tickClock) != 0;
    const int before = sim::Apply6_GetLog().tickAdvanceCount;
    if (sim::QueueRequestPerm30(queue_, a1, a2, a3, a4, valid) < 0)
        return false;               // builder returned -1 (time unchanged)
    queue_.FlushSendQueue();
    queue_.ExecCommands();
    return sim::Apply6_GetLog().tickAdvanceCount != before;
}

// ---------------------------------------------------------------------------
// Per-frame drive, in the original frame order: timer ticks happen first (the
// winmm callback is asynchronous to the frame), then the RunFrameLoop blocks.
// ---------------------------------------------------------------------------
SessionTick::FrameResult SessionTick::OnFrame(u32 elapsedMs) {
    FrameResult r{};
    const int firesBefore = clockFireCount_;
    const int commitsBefore = sim::Apply6_GetLog().tickAdvanceCount;

    // fptc @0x44e130 every 14 ms (StartTimer(0xE, 0) @0x527e52). The clock
    // proc fires inside Tick() whenever dword_62EB38 % 71 == 0 and unpaused.
    msRemainder_ += elapsedMs;
    while (msRemainder_ >= sim::kTimeBaseTickMs) {
        msRemainder_ -= sim::kTimeBaseTickMs;
        timeBase_.Tick();
        ++r.timeBaseTicks;
    }
    r.clockFires = clockFireCount_ - firesBefore;

    // 0x4c0b8c — single-player master-clock broadcast:
    //   if (dword_764CE0 == -1 && dword_62EB38 > dword_11AA488 && byte_63CC40)
    //       QueueRequestPerm30(&qword_122F840);
    const u32 tc = timeBase_.ThrottledTicks();  // dword_62EB38
    if (singlePlayer_ && tc > nextCmdTick_ && sessionLive_) {
        u8 img[sizeof(sim::GameTime)];
        std::memcpy(img, &clock_.master, sizeof img);
        i32 a1, a2, a3;
        i16 a4;
        std::memcpy(&a1, img + 0, 4);
        std::memcpy(&a2, img + 4, 4);
        std::memcpy(&a3, img + 8, 4);
        std::memcpy(&a4, img + 12, 2);
        const bool valid =
            sim::GameTimeCompare(&clock_.master, &sim::g_tickClock) != 0;
        sim::QueueRequestPerm30(queue_, a1, a2, a3, a4, valid);
    }

    // 0x4c0be5 — the command pump window (live mask 0x67FFF has bit 0x20000):
    //   FlushSendQueue; ReceiveAndQueue (net leg; standalone loops back in
    //   Flush); ExecCommands; dword_11AA488 = dword_62EB38 + 7  @0x4c0bfe.
    if (tc > nextCmdTick_) {
        queue_.FlushSendQueue();    // 0x4c0be7
        queue_.ExecCommands();      // 0x4c0bf1
        nextCmdTick_ = tc + sim::kCommandWindowTicks;
        r.commandWindowRan = true;
    }
    r.timeSyncCommits = sim::Apply6_GetLog().tickAdvanceCount - commitsBefore;

    // 0x4c1324 — end-of-day gate. The live session mask 0x67FFF has bit
    // 0x10000 clear, so the gate is armed; menu-pop / net-wait suppression is
    // not modeled in the headless session (both inert-false).
    if (sim::ClockDayEndPending(/*headlessMask=*/false, /*menuPopPending=*/false,
                                /*netWaitBusy=*/false, roundEndRequested_,
                                dayEndLatched_, singlePlayer_,
                                sim::g_tickClock.hour, clock_.sessionFlags)) {
        dayEndLatched_ = true;                              // 0x4c1332
        timeBase_.SetProcPaused(&SessionTickClockProc, 1);  // 0x4c1338
        r.dayEnded = true;
    }
    return r;
}

// Post-frame-loop fast-forward — InitOrLoadSession @0x53449c..0x534581.
int SessionTick::FastForwardToDayEnd() {
    timeBase_.SetProcPaused(&SessionTickClockProc, 1);      // 0x53449c
    if ((clock_.sessionFlags & sim::kSessionFlagSkipPreTurnFastForward) != 0)
        return 0;                                           // 0x5344a1 test bh,8
    sim::GameTime local = sim::g_tickClock;                 // 0x5344ae movs x14
    int steps = 0;
    while (local.hour < sim::kDayEndHour) {                 // 0x5344bf
        sim::GameTimeAdvance(&local, 0, 0, sim::kDayFastForwardMinutes); // 0x5344db
        CommitTimePacket(local);                            // 0x5344e7 (+ack wait)
        ++steps;
    }
    return steps;
}

// Day rollover — InitOrLoadSession @0x5345bf..0x534671 (single-player branch).
void SessionTick::RollToNextDay() {
    if (!singlePlayer_)                                     // 0x5345bf
        return;
    timeBase_.SetProcPaused(&SessionTickClockProc, 1);      // 0x5345d1/0x5345e2
    sim::GameTime local = sim::g_tickClock;                 // 0x5345dd movs x14
    while (local.hour < sim::kDayEndHour) {                 // 0x5345ec
        sim::GameTimeAdvance(&local, 0, 0, sim::kDayFastForwardMinutes); // 0x534604
        CommitTimePacket(local);                            // 0x534610 (+ack wait)
    }
    sim::GameTimeAdvance(&local, sim::kDayRollAdvanceHours, 0, 0); // 0x534644
    sim::GameTimeSet(&local, sim::kDayStartHour, 0, 0);            // 0x534659
    CommitTimePacket(local);                                       // 0x534665
    timeBase_.SetProcPaused(&SessionTickClockProc, 0);             // 0x534671
}

} // namespace guild::play
