// gilde.exe — outer frame-loop drivers (guild::app). See frame_drivers.h.
//
// 1:1 reconstructions of the spine's blocking RunFrameLoop driver functions. The
// only readability change vs. the originals is that the engine-global termination
// state (byte_67225C un-pause key, dword_75BF38 pending action, the blocking
// menu loop) is supplied by injected predicates so a headless caller terminates
// deterministically — exactly the pattern GameApp::Run already uses for the main
// session loop. The loop STRUCTURE, the feature masks, and the per-loop side
// effects are preserved verbatim.
#include "app/frame_drivers.h"

namespace guild::app {

// gilde.exe 0x50c720 — VIBE_GameLogic_RunFrameLoopWrapper.
int RunFrameLoopWrapper(GameApp& app, int maxFrames) {
    // do { result = RunFrameLoop(0x67FFF, ..); } while (result);
    int frames = 0;
    int result;
    do {
        result = app.RunFrameLoop(kMainSessionMask); // VIBE_GameLogic_RunFrameLoop
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames)
            break;
    } while (result);
    return frames;
}

// gilde.exe 0x56e7e0 — VIBE_GameLogic_RunPauseLoop.
int RunPauseLoop(GameApp& app, const std::function<int()>& keyState,
                 int maxFrames) {
    // dword_62EB4C = 1; Hud_SetStatusBannerText("Pause"); byte_63CC40 = 0;
    //   -> these are engine-state side effects applied by the host (banner text +
    //      the paused / owner-collect-suppress flags). The portable core is below.
    //
    // The paused-frame mask (gilde.exe 0x56e7e7..0x56e805):
    //   mov ecx, ds:dword_11BC2D0   ; the LAST published frame mask
    //   or  ecx, 100000h            ; set kInputSuppress (block HUD mouse etc.)
    //   and ch, 0DFh                ; clear 0x2000 (kOptionsAndPanels)
    // i.e. the previous frame's mask with input suppressed and the options/chat/
    // hotkey bit removed — NOT a zero mask. Computed ONCE (held in ecx across the
    // loop), then passed to every paused RunFrameLoop tick.
    const std::uint32_t pausedMask =
        (app.lastFeatureMask() | mask::kInputSuppress) & ~mask::kOptionsAndPanels;
    // while ( RunFrameLoop(pausedMask, ..) && byte_67225C != 57 ) ;
    int frames = 0;
    for (;;) {
        int result = app.RunFrameLoop(pausedMask);
        ++frames;
        // && byte_67225C != 57 : stop when the un-pause key (space, 57) is down.
        int key = keyState ? keyState() : 0;
        if (!result || key == kUnpauseKeyScancode)
            break;
        if (maxFrames >= 0 && frames >= maxFrames)
            break;
    }
    // Hud_SetStatusBannerText(byte_6252D8); dword_62EB4C = v4; byte_63CC40 = v5;
    //   -> restore the prior banner + paused/owner-collect state (host side effects).
    return frames;
}

// gilde.exe 0x527be8 — VIBE_GameLogic_RunEndRoundScreen.
int RunEndRoundScreen(GameApp& app, const std::function<int()>& pendingAction,
                      int maxFrames, bool* advanceRaised, bool* modeSwitchRaised) {
    // v1 = GameTick_Finalize(0,0,"Runde beenden"); Form_CenterChildWindows(v1);
    //   -> build + center the end-of-round form (GUI/HUD leaf, deferred).
    bool raised = false;
    bool modeSwitch = false; // ecx, zeroed at 0x527c00
    int frames = 0;
    int result;
    do {
        // 0x527c09..0x527c21:
        //   mov edx, ds:dword_75BF38 ; cmp edx, -1 ; jz skip
        //   cmp edx, 4BAh ; jnz +2 ; mov ecx, 1     <- latch mode-switch on 0x4BA
        //   mov ds:dword_631614, edi (=1)           <- raise the advance flag
        if (pendingAction) {
            const int pending = pendingAction();
            if (pending != -1) {
                if (pending == kEndRoundModeSwitchAction)
                    modeSwitch = true; // ecx = 1
                raised = true;         // dword_631614 = 1
            }
        }
        result = app.RunFrameLoop(kMainSessionMask); // RunFrameLoop(425983, v1, 1)
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames)
            break;
    } while (result);
    // Form_Destroy(v1);
    // if ( ecx ) { dword_63CC3C = 1; Hud_FindModeIndex(InitOrLoadSession); }
    //   -> the form destroy + mode-switch tail are GUI/HUD leaves (deferred); the
    //      DECISION (pending action == 0x4BA ever seen) is reported to the caller.
    if (advanceRaised)
        *advanceRaised = raised;
    if (modeSwitchRaised)
        *modeSwitchRaised = modeSwitch;
    return frames;
}

} // namespace guild::app
