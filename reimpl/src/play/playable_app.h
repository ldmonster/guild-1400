#pragma once
// play::PlayableApp — the top-level assembly that runs the reconstructed app spine
// as an actual program: init the spine, run live frames until the platform signals
// quit (window-close), then the 13-step shutdown. Thin wrapper over app::GameApp's
// public lifecycle (CreateMainWindow -> InitSubsystemsAndMovieDll ->
// InitDisplayAndPaths -> InitEngineAndScriptCommands -> RunFrameLoop* -> Shutdown).
//
// Header-only: no new translation unit / ODR surface. Headless tests drive it with
// a ScriptedPlatform; guild_run drives it with the SDL/Vulkan backends.
#include "app/gamelogic.h"
#include "shim/IPlatform.h"

#include <cstdint>

namespace guild::play {

// Live in-game per-frame feature mask — the original's RunFrameLoop(0x67FFF, ..)
// (the headless-suppress bit 0x10000 is intentionally NOT set: this is a frame
// that pumps input, steps the sim, and renders).
inline constexpr std::uint32_t kLiveFrameMask = 0x67FFFu;

class PlayableApp {
public:
    PlayableApp(app::GameApp& game, shim::IPlatform& plat) : game_(game), plat_(plat) {}

    // Bring up the spine exactly as VIBE_GameLogic_MainEntryAndShutdown does before
    // its menu/session loop. Returns false (and leaves the app un-inited) if any
    // init step fails.
    bool init(int displayMode = 1) {
        if (inited_) return true;
        if (!game_.CreateMainWindow(displayMode)) return false;
        if (!game_.InitSubsystemsAndMovieDll()) return false;
        if (!game_.InitDisplayAndPaths(displayMode)) return false;
        if (!game_.InitEngineAndScriptCommands()) return false;
        inited_ = true;
        return true;
    }

    // Run live frames until the platform requests quit (pumpMessages()==false, i.e.
    // window closed) or `maxFrames` is reached. maxFrames < 0 means run unbounded
    // until quit. Returns the number of frames run; quitRequested() reports whether
    // it stopped because of a quit (vs hitting maxFrames).
    int runUntilQuit(std::uint32_t mask = kLiveFrameMask, int maxFrames = 600) {
        quitRequested_ = false;
        int f = 0;
        while (maxFrames < 0 || f < maxFrames) {
            // RunFrameLoop pumps once at the top (inputLatchAndPump). On a quit
            // pump it sets game_.quitRequested() and renders nothing; we then stop
            // WITHOUT counting that frame (so frames == successful pumps).
            game_.RunFrameLoop(mask);
            if (game_.quitRequested()) { quitRequested_ = true; break; }
            ++f;
        }
        framesRun_ = f;
        return f;
    }

    bool quitRequested() const { return quitRequested_; }

    void shutdown() {
        if (shutdownDone_) return;
        game_.Shutdown();
        shutdownDone_ = true;
    }

    bool inited() const { return inited_; }
    int  framesRun() const { return framesRun_; }

private:
    app::GameApp& game_;
    shim::IPlatform& plat_;
    bool inited_ = false;
    bool shutdownDone_ = false;
    bool quitRequested_ = false;
    int  framesRun_ = 0;
};

} // namespace guild::play
