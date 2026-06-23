#pragma once
// play::SessionTick — the thin per-frame session adapter that drives the
// reconstructed CONTINUOUS game-clock chain of gilde.exe (see
// src/sim/game_clock_tick.h for the full recovered architecture + addresses).
//
// It strings together, with the original cadence and order:
//
//   crt::TimeBase            (winmm dispatcher, fptc @0x44e130; 14 ms ticks
//                             from StartTimer(0xE,0) @0x527e52)
//   sim::ClockComputeGameTimeOfDay  (clock proc @0x527778; registered at
//                             interval 71 @0x52f339, i.e. fires every 994 ms,
//                             advancing the MASTER clock qword_122F840)
//   sim::QueueRequestPerm30 -> sim::ExAdvanceGameTick  (the master->world
//                             time-sync command, RunFrameLoop @0x4c0b8c +
//                             executor @0x498954: commits the WORLD clock
//                             sim::g_tickClock == qword_13CE852 and runs the
//                             per-tick world cascade — character/AI updates)
//   sim::ClockDayEndPending  (day-end gate, RunFrameLoop @0x4c1324)
//   day rollover             (InitOrLoadSession @0x53449c../0x5345bf..:
//                             fast-forward to 23:00 in +30 min commits, +24 h,
//                             set 06:00:00, resume clock)
//
// ---------------------------------------------------------------------------
// PER-FRAME CALL CONTRACT for the live session (wave-2 wiring):
//
//   play::SessionTick tick;                 // once per session
//   tick.SetGameSpeedLevel(2);              // optional; dword_1233558 = 40*lvl
//   tick.SyncClocksToDayStart();            // turn start (sysmsg-3 step)
//   tick.BeginDay();                        // clears latches, UNPAUSES clock
//   each rendered frame:
//       auto r = tick.OnFrame(elapsedMs);   // real wall ms since last frame
//       // -> HUD reads tick.worldTime() (day/hour/minute/second)
//       if (r.dayEnded) {
//           tick.FastForwardToDayEnd();     // 30-min commits up to 23:00
//           /* run the existing play::RunGameDay machinery here — the
//              original runs VIBE_GameLogic_RunTurnTransition @0x534587 */
//           tick.RollToNextDay();           // +24h, 06:00:00, clock resumes
//           tick.SyncClocksToDayStart();    // next turn's sysmsg-3
//           tick.BeginDay();
//       }
//
// The world-update fan-out per committed tick goes through the Apply6 cascade
// hook (sim::SetTickCascadeHook / sim::Apply6_TickGates — see
// src/sim/command_apply6.h); a headless session leaves it inert.
// ---------------------------------------------------------------------------
//
// Global-state note: the binary keeps this state in globals (qword_122F840,
// qword_13CE852, dword_1233558, dword_11AA488, dword_63CC3C, the TimeBase
// proc table). One SessionTick instance per process mirrors that; the world
// clock is the shared sim::g_tickClock so every other reconstructed consumer
// of qword_13CE852 observes the same record.

#include "crt/time.h"
#include "sim/command.h"
#include "sim/game_clock_tick.h"
#include "sim/gametime.h"

namespace guild::play {

class SessionTick {
public:
    struct FrameResult {
        int timeBaseTicks = 0;     // fptc fires consumed this frame (14 ms each)
        int clockFires = 0;        // clock-proc fires (994 ms cadence)
        int timeSyncCommits = 0;   // opcode-30 world commits applied this frame
        bool commandWindowRan = false; // the 7-tick command pump window ran
        bool dayEnded = false;     // 0x4c1324 gate fired (clock now paused)
    };

    SessionTick();
    ~SessionTick();
    SessionTick(const SessionTick&) = delete;
    SessionTick& operator=(const SessionTick&) = delete;

    // ---- day lifecycle ----------------------------------------------------
    // Turn-start clock sync — InitOrLoadSession @0x533a54 (v97 = world clock;
    // GameTime_Set(&v97, 6,0,0); QueueRequestFlagBlob32(3, &v97)) applied with
    // the executor semantics of VIBE_Command_ExSysMessage case 3 @0x498ba3:
    // pause the clock proc if running, write BOTH qword_13CE852 and
    // qword_122F840, unpause. (Direct-applied here; the apply5 wire model of
    // case 3 keeps its own record — see progress/session-tick.md named gaps.)
    void SyncClocksToDayStart();

    // Top-of-round reset + live-scene clock start:
    //   dword_11AA480 = 0; dword_63CC3C = 0   (InitOrLoadSession @0x534474/7a)
    //   SetProcInterval(clockProc, 0)         (Scene_RunMainFrameLoop @0x50f1a6)
    void BeginDay();

    // Per-frame drive; elapsedMs = real milliseconds since the previous call.
    FrameResult OnFrame(u32 elapsedMs);

    // Post-frame-loop fast-forward — InitOrLoadSession @0x53449c..0x534581:
    // pause the clock; unless (word_63C740 & 8), advance a copy of the world
    // clock to 23:00 in +30 minute steps, committing each via the real
    // opcode-30 pipeline (each commit runs the world cascade). Returns the
    // number of 30-minute commits.
    int FastForwardToDayEnd();

    // Day rollover — InitOrLoadSession @0x5345bf..0x534671 (single-player):
    // pause; fast-forward to 23:00 (+30 min commits); +24 hours @0x534634;
    // GameTime_Set(06:00:00) @0x534659; final commit @0x534665; resume clock
    // @0x534671. No-op when not single-player (the network host drives time).
    void RollToNextDay();

    // ---- state / wiring ----------------------------------------------------
    const sim::GameTime& worldTime() const;                    // qword_13CE852
    const sim::GameTime& masterTime() const { return clock_.master; } // 122F840

    // dword_1233558 = 40 * level, level clamped to 0..4 (0x4ff8b1/0x4ff8ee).
    void SetGameSpeedLevel(int level);
    int GameSpeedLevel() const { return clock_.gameSpeed / sim::kGameSpeedStep; }

    void SetSessionFlags(u16 flags) { clock_.sessionFlags = flags; } // word_63C740
    u16 sessionFlags() const { return clock_.sessionFlags; }
    void SetSessionLive(bool v) { sessionLive_ = v; }      // byte_63CC40
    void SetSinglePlayer(bool v) { singlePlayer_ = v; }    // dword_764CE0 == -1
    void SetRoundEndRequested(bool v) { roundEndRequested_ = v; } // dword_11AA480

    bool dayEndLatched() const { return dayEndLatched_; }  // dword_63CC3C
    bool clockPaused() const;                              // dword_B537D0 slot
    u32 tickCounter() const { return timeBase_.ThrottledTicks(); } // dword_62EB38

    crt::TimeBase& timeBase() { return timeBase_; }
    sim::CommandQueue& commandQueue() { return queue_; }
    sim::ClockGlobals& clock() { return clock_; }

    // Internal: target of the TimeBase proc trampoline (the registered
    // 0x527778 slot). Public so the file-static trampoline can reach it.
    void FireClockProc();

private:
    // Commit a 14-byte time image through the REAL command pipeline:
    // QueueRequestPerm30 @0x494a50 (gated on GameTime_Compare(t, world) != 0)
    // -> FlushSendQueue (standalone loopback) -> ExecCommands ->
    // ExAdvanceGameTick @0x498954. Returns true when the world clock advanced
    // (the original spins on GetPacketStatusById; standalone applies inline).
    bool CommitTimePacket(const sim::GameTime& t);

    crt::TimeBase timeBase_;       // the fptc dispatcher (no platform; manual)
    sim::CommandQueue queue_;      // the opcode-30 pipeline (standalone)
    sim::ClockGlobals clock_;      // master clock + speed + flags

    u32 msRemainder_ = 0;          // sub-14ms carry between frames
    u32 nextCmdTick_ = 0;          // dword_11AA488 (command window threshold)
    bool dayEndLatched_ = false;   // dword_63CC3C
    bool roundEndRequested_ = false; // dword_11AA480
    bool sessionLive_ = true;      // byte_63CC40
    bool singlePlayer_ = true;     // dword_764CE0 == -1
    int clockFireCount_ = 0;       // observable fire counter
};

} // namespace guild::play
