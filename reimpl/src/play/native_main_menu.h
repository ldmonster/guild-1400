#pragma once
// =============================================================================
// guild::play — drive the BYTE-FAITHFUL gui::Menu_RunMainMenu (@0x529d08) on the
// native Vulkan/SDL window.
//
// The 1:1 reconstruction gui::Menu_RunMainMenu builds the real widget column + runs
// the real per-frame click dispatch, reading the host through MainMenuRunHooks. This
// bridge supplies those host boundaries from the live backend: each loop frame it
// pumps SDL + reads the mouse (InitStateReader), hit-tests the cursor against the
// REAL built widget rects to produce the click-edge / hovered-widget-id the dispatch
// consumes (ClickEdge / HoverId / EscDown), and renders the real gilde.gfx menu art
// to the IGraphicsDevice + presents it (RunFrameLoop). The menu's sub-screen runners
// are wired to the native sdl_* screens (ChooseCity/character creation, options,
// credits). So guild_run --play runs the REAL main-menu logic on the real window.
// No Wine.
// =============================================================================
#include <string>
#include <utility>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; class IFileSystem; }

namespace guild::play {

struct NativeMenuResult {
    enum Action { kQuit, kPlayCity } action = kQuit;
    std::string cityPath;          // set when action == kPlayCity
    int  framesPresented = 0;
    bool quitByWindow = false;
};

// Run the real main menu on `device`/`plat` (already init'd; window created). `cities`
// is the {displayName, ctyPath} list for the New-Game city screen. maxFrames bounds
// the loop for headless tests (-1 = until quit / window close). Returns the chosen
// action (Quit, or a city to play).
NativeMenuResult RunNativeMainMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   shim::IFileSystem& fs, const std::string& gameDir,
                                   const std::vector<std::pair<std::string, std::string>>& cities,
                                   int fbW, int fbH, int frameCapMs, int maxFrames);

} // namespace guild::play
