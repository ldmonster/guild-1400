#pragma once
// gilde.exe — outer frame-loop drivers (guild::app).
//
// The spine's blocking game/menu loops are a family of tiny functions that all
// repeatedly call VIBE_GameLogic_RunFrameLoop until it returns 0 (the "no logic
// step ran" / quit sentinel). The per-frame work is in RunFrameLoop itself
// (GameApp::RunFrameLoop, src/app/frameloop.cpp); these are the OUTER loops that
// drive it under a fixed feature mask plus a per-loop termination predicate.
//
// Faithful 1:1 reconstruction of:
//   0x50c720  VIBE_GameLogic_RunFrameLoopWrapper  (the main playable-session loop:
//                                                  do { RunFrameLoop(0x67FFF) }
//                                                  while(result) — runs the full
//                                                  feature set every tick)
//   0x56e7e0  VIBE_GameLogic_RunPauseLoop         (the Pause-banner loop: shows the
//                                                  "Pause" status banner, runs
//                                                  RunFrameLoop with a 0 mask until
//                                                  the result drops or the un-pause
//                                                  key (scancode 57 = space) is hit,
//                                                  then restores the prior banner)
//   0x527be8  VIBE_GameLogic_RunEndRoundScreen    (the "Runde beenden" end-of-round
//                                                  screen loop: the full 0x67FFF
//                                                  feature mask until quit; the form
//                                                  build/center/destroy + mode-switch
//                                                  tail are render/HUD leaves, see
//                                                  the comment in the .cpp)
//
// Because the originals' termination state lives in engine globals the host
// drives, each driver takes a small predicate/limit so a headless caller (or a
// test) can run a deterministic bounded loop in place of the original's blocking
// global-driven one, exactly as GameApp::Run already does for the session loop.
#include "app/gamelogic.h"

#include <functional>

namespace guild::app {

// The main playable-session feature mask the wrapper passes every tick.
// gilde.exe: VIBE_GameLogic_RunFrameLoop(425983, a1, a2); 425983 == 0x67FFF ==
// bits 0..14 (all the per-frame world/HUD/script/tooltip/weather work) plus bits
// 17 (network command pump) and 18 (day-cycle brightness + outdoor music). No
// suppressor bits, no combat-scroll (0x80000), no input-suppress (0x100000).
constexpr std::uint32_t kMainSessionMask = 425983u; // 0x67FFF

// The un-pause keyboard scancode the pause loop watches (gilde.exe byte_67225C
// compared against 57; set 0x39 = the space-bar make code).
constexpr int kUnpauseKeyScancode = 57;

// gilde.exe 0x50c720 — VIBE_GameLogic_RunFrameLoopWrapper.
//   do
//     result = VIBE_GameLogic_RunFrameLoop(425983, ebx, edi);
//   while ( result );
// The original ebx/edi register args are the session/form context the spine set
// up; here the loop drives GameApp::RunFrameLoop under the main session mask.
// `maxFrames` (<0 = unbounded, the original behaviour) caps the loop so a
// headless caller terminates deterministically; returns the number of frames run.
int RunFrameLoopWrapper(GameApp& app, int maxFrames = -1);

// gilde.exe 0x56e7e0 — VIBE_GameLogic_RunPauseLoop.
//   dword_62EB4C = 1;                              // enter paused state
//   VIBE_Hud_SetStatusBannerText("Pause");
//   byte_63CC40 = 0;                               // suppress owner-collect
//   mask = (dword_11BC2D0 | 0x100000) & ~0x2000;   // 0x56e7e7/0x56e7f2/0x56e805
//   while ( VIBE_GameLogic_RunFrameLoop(mask, ..) && byte_67225C != 57 ) ;
//   VIBE_Hud_SetStatusBannerText(byte_6252D8);     // restore prior banner
//   dword_62EB4C = v4; byte_63CC40 = v5;           // restore prior state
// The paused mask is the LAST published frame mask (dword_11BC2D0, i.e.
// GameApp::lastFeatureMask()) with kInputSuppress (0x100000) OR'd in and
// kOptionsAndPanels (0x2000) cleared — computed once before the loop and reused
// every paused tick. The HUD banner set/restore and the byte_63CC40
// owner-collect suppress are engine-state side effects the host applies; the
// reconstructed core is the paused-frame pump loop + its key-gated termination.
// `keyState()` returns the current make-code each iteration (the host's
// byte_67225C); the loop ends when it equals kUnpauseKeyScancode (57) or
// RunFrameLoop returns 0. `maxFrames` (<0 = unbounded) bounds it; returns frames.
int RunPauseLoop(GameApp& app, const std::function<int()>& keyState,
                 int maxFrames = -1);

// The pending-action id that latches the end-round mode switch (gilde.exe
// 0x527c14: cmp edx, 4BAh — dword_75BF38 == 0x4BA = 1210).
constexpr int kEndRoundModeSwitchAction = 0x4BA;

// gilde.exe 0x527be8 — VIBE_GameLogic_RunEndRoundScreen.
//   v1 = VIBE_GameTick_Finalize(0, 0, "Runde beenden");  // build the end screen
//   VIBE_Form_CenterChildWindows(v1);
//   ecx = 0;                                             // 0x527c00
//   do {
//     if ( dword_75BF38 != -1 ) {
//       if ( dword_75BF38 == 0x4BA ) ecx = 1;            // 0x527c14..0x527c1c
//       dword_631614 = 1;                                // 0x527c21
//     }
//   } while ( VIBE_GameLogic_RunFrameLoop(425983, v1, 1) );
//   VIBE_Form_Destroy(v1);
//   if ( ecx ) { dword_63CC3C = 1; Hud_FindModeIndex(InitOrLoadSession); }
// The form build/center/destroy and the mode-switch tail are GUI/HUD leaves
// (deferred). The reconstructed core is the loop body: each tick, if a pending
// end-of-round action is posted (`pendingAction()` != -1, the original's
// dword_75BF38 probe) raise the "advance" flag — and if that pending action id is
// exactly kEndRoundModeSwitchAction (0x4BA) latch the mode-switch flag (the
// original's ecx) — then run a full-feature frame; the loop ends when
// RunFrameLoop returns 0. Returns the number of frames run; sets `*advanceRaised`
// (optional) if the pending-action flag was ever raised and `*modeSwitchRaised`
// (optional) if the 0x4BA action was ever seen (the post-loop
// dword_63CC3C = 1 + Hud_FindModeIndex(InitOrLoadSession) trigger).
int RunEndRoundScreen(GameApp& app, const std::function<int()>& pendingAction,
                      int maxFrames = -1, bool* advanceRaised = nullptr,
                      bool* modeSwitchRaised = nullptr);

} // namespace guild::app
