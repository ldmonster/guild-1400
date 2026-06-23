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

#include "gui/newgame_setup.h"   // gui::NewGameParams — the full new-game block

namespace guild::shim { class IGraphicsDevice; class IPlatform; class IFileSystem; class IAudioDevice; }

namespace guild::play {

struct NativeMenuResult {
    // kLoadGame: the Load-Game screen (VIBE_Menu_RunLoadGame @0x56a270) confirmed a
    // save — savePath/saveLoadPath/saveName carry the pick (ADDITIVE; appended so the
    // existing kQuit/kPlayCity values and callers are untouched).
    enum Action { kQuit, kPlayCity, kLoadGame } action = kQuit;
    std::string cityPath;          // set when action == kPlayCity
    // The COMPLETE chosen new-game state (set when action == kPlayCity): the
    // city (cityName/cityFile), difficulty (byte_12335BA pick), history flag,
    // identity (firstName/familyName/gender/faith/wappen) and profession
    // (beruf byte + computed variant), with params.started armed by the
    // NewGame_Commit (the dword_122F528/dword_631614/word_63C740 arm). This is
    // the global parameter block (0x122F4A0..) the original's session start
    // (VIBE_Command_EnqueueInheritanceTransfer @0x5336f0) consumes — feed it to
    // play::ApplyNewGameParams after the world is loaded.
    gui::NewGameParams params;
    // Set when action == kLoadGame (the menu's Load Game pick):
    //   savePath     — the picked save's real-cased path, openable through the menu's
    //                  IFileSystem (e.g. "Resources/gamedata/Saves/Quicksave.SAV") —
    //                  feed it to play::LoadLiveWorld to start the session from the save.
    //   saveLoadPath — the original's byte_122F530 string the dispatch stamps 1:1
    //                  ("Gamedata\\Saves\\<name>.SAV", fmt @0x624f40).
    //   saveName     — the save header's in-game name (e.g. "QUICKSAVE").
    // The pick also ran the original's flag protocol: word_63C740 = 10 +
    // dword_631614 = 1 inside the load screen's gui run.
    std::string savePath;
    std::string saveLoadPath;
    std::string saveName;
    int  framesPresented = 0;
    bool quitByWindow = false;
};

// Screen rect of the main-menu button on design-row `designY` (a gui::MainMenu_ButtonY
// table value) for a `fbW`x`fbH` framebuffer. The menu is authored on an 800x600 canvas
// (window centred), scaled to the framebuffer; exposed so tests/tools hit-test exactly
// the layout the menu renders.
//
// `designW` is the button's design-space width.  In the original each button is
// laid out by VIBE_Object_RecomputeSize @0x41b164 (sprite kind 9): the width is
// VIBE_Property_Get(label) + capL(12) + capR(12) + 4 — i.e. it varies per label.
// Callers that have measured the label pass that width; the default 124 is the
// _BUTTON_RED 3-slice nominal (cap12 + centre100 + cap12) for centre/hit-test use
// when no label width is available.
struct MenuButtonRect { int x, y, w, h; };
MenuButtonRect MenuButtonScreenRect(int designY, int fbW, int fbH, int designW = 124);

// Run the real main menu on `device`/`plat` (already init'd; window created). `cities`
// is the {displayName, ctyPath} list for the New-Game city screen. maxFrames bounds
// the loop for headless tests (-1 = until quit / window close). `audio` (optional,
// already init'd) plays the looping CD menu track (mirrors gilde.exe 0x529d08's
// VIBE_Audio_LoadTrack); pass nullptr for silent. Returns the chosen action.
NativeMenuResult RunNativeMainMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   shim::IFileSystem& fs, const std::string& gameDir,
                                   const std::vector<std::pair<std::string, std::string>>& cities,
                                   int fbW, int fbH, int frameCapMs, int maxFrames,
                                   shim::IAudioDevice* audio = nullptr);

} // namespace guild::play
