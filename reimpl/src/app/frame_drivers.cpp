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
    // while ( RunFrameLoop(0, ..) && byte_67225C != 57 ) ;
    int frames = 0;
    for (;;) {
        // The original passes the (zero-initialised) mask v2: paused frames pump
        // input/HUD but simulate nothing.
        int result = app.RunFrameLoop(/*featureMask=*/0u);
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
                      int maxFrames, bool* advanceRaised) {
    // v1 = GameTick_Finalize(0,0,"Runde beenden"); Form_CenterChildWindows(v1);
    //   -> build + center the end-of-round form (GUI/HUD leaf, deferred).
    bool raised = false;
    int frames = 0;
    int result;
    do {
        // if ( dword_75BF38 != -1 ) dword_631614 = 1;
        //   a posted pending end-of-round action raises the "advance" flag.
        if (pendingAction && pendingAction() != -1)
            raised = true; // dword_631614 = 1
        result = app.RunFrameLoop(kMainSessionMask); // RunFrameLoop(425983, v1, 1)
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames)
            break;
    } while (result);
    // Form_Destroy(v1); if (...) { dword_63CC3C = 1; return Hud_FindModeIndex(..); }
    //   -> destroy the form + the mode-switch tail (GUI/HUD leaves, deferred).
    if (advanceRaised)
        *advanceRaised = raised;
    return frames;
}

} // namespace guild::app
