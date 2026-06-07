#pragma once
// =============================================================================
// guild::play — NATIVE MAIN-MENU SCREEN (the boot entry the game starts at).
//
// The real boot spine VIBE_GameLogic_MainEntryAndShutdown @0x534bbc is an outer
// loop that alternates VIBE_Menu_RunMainMenu @0x529d08 <-> a game session. This
// module is the native (Vulkan/SDL) front half: it RENDERS the real main menu
// (gui::RenderMainMenu — the canonical 8-button column: New Game / Load /
// Multiplayer / Game-Options / Gfx-Options / Sfx-Options / Credits / Quit, the
// byte-faithful x=32 + y-table layout) into an IGraphicsDevice each frame, reads
// the SDL pointer/keys via IPlatform, hit-tests the column, and on a click runs
// the REAL gui::MainMenu_Dispatch / MainMenu_Select transition. New Game opens a
// city-pick sub-screen (the shipped cities) and returns the chosen .cty so the
// caller can start the play session. Backend-agnostic (testable headless with a
// MemoryGraphicsDevice + ScriptedPlatform), drives real Vulkan+SDL unchanged.
// No Wine.
//
// guild_run drives the outer loop:  while (true) { m = RunSdlMenu(...);
//   if m.choice==kQuit break;  if m.choice==kNewGame RunSdlSession(m.cityPath); }
// =============================================================================
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

enum class SdlMenuChoice {
    kNone = 0,    // (internal)
    kQuit,        // Quit button / ESC / window close
    kNewGame,     // New Game -> the caller runs ChooseCity + character creation
    kLoadGame,    // Load Game -> the caller runs the load/save browser
    kOptions,     // Game/Gfx/Sfx Options -> the caller runs RunOptionsScreen(optionsPage)
    kCredits,     // Credits -> the caller runs RunCreditsScreen
};

struct SdlMenuConfig {
    std::string gameDir;
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;    // -1 = until choice/quit/close (headless tests bound it)
    int frameCapMs = 16;    // per-frame sleep (~60fps); 0 = uncapped
    // The selectable cities for New Game: {display name, .cty path relative to the
    // mounted game dir}. The caller (guild_run) enumerates them. Empty -> New Game
    // returns immediately with no cityPath (caller falls back to a default).
    std::vector<std::pair<std::string, std::string>> cities;
};

struct SdlMenuResult {
    SdlMenuChoice choice = SdlMenuChoice::kQuit;
    int  optionsPage = 0;       // for kOptions: 0=Game, 1=Gfx, 2=Sfx
    int  hoveredItem = -1;      // last hovered main-menu index (0..7)
    int  framesPresented = 0;
    bool quitByWindow = false;
    bool quitByEsc = false;
};

// Render + run the native main menu until the user picks New Game (with a city),
// Load, or Quit (button/ESC/window close). `device` MUST be init()'d to
// cfg.fbW x cfg.fbH (any bpp; the menu is composited 32bpp then converted to the
// device's bpp). `plat`'s window MUST already be created.
SdlMenuResult RunSdlMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                         const SdlMenuConfig& cfg);

} // namespace guild::play
