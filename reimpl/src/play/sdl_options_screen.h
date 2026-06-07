#pragma once
// =============================================================================
// guild::play — NATIVE OPTIONS SCREENS (Game / Gfx / Sfx pages).
//
// The real main menu's Game-Options / Gfx-Options / Sfx-Options buttons map to
// MainMenuItem kGameOptions/kGfxOptions/kSfxOptions, which the original boot spine
// dispatches to three form-driven screens:
//   * VIBE_Menu_RunOptionsGame @0x56cc44  (form "menu\options_game",  title 0x1866)
//   * VIBE_Menu_RunOptionsGfx  @0x56c21c  (form "menu\options_gfx",   title 0x1867)
//   * VIBE_Menu_RunOptionsSfx  @0x56c808  (form "menu\options_sfx",   title 0x1868)
// Each builds a column of form widgets bound to a settings byte block, runs the
// frame loop until OK (apply) or Cancel (discard), then on OK copies the widget
// values back into the settings globals and persists them via
// VIBE_Config_WriteGfxSettings @0x56af54 (WritePrivateProfileStringA into the
// [Gfx]/[Sound]/[Game] sections of gilde.INI).
//
// This module is the native (Vulkan/SDL) reconstruction of one such page: it
// renders a faithful options panel (over the real gilde.gfx menu background when
// present) listing that page's real INI-backed settings with toggle / cycle /
// step controls, mutates the reconstructed config struct in place, and returns
// {back, changed, the modified settings}. Backend-agnostic: testable headless
// with a MemoryGraphicsDevice + scripted IPlatform; drives real Vulkan+SDL
// unchanged. No Wine. Mirrors the RunSdlMenu screen contract.
// =============================================================================
#include "config/ini.h"   // GfxSettings / SoundSettings / GameSettings
#include <cstdint>
#include <string>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

// Which options page this screen renders. 1:1 with the three VIBE_Menu_RunOptions*
// entry points / MainMenuItem kGameOptions / kGfxOptions / kSfxOptions.
enum class OptionsPage {
    kGame = 0,   // VIBE_Menu_RunOptionsGame
    kGfx,        // VIBE_Menu_RunOptionsGfx
    kSfx,        // VIBE_Menu_RunOptionsSfx
};

struct OptionsConfig {
    OptionsPage page = OptionsPage::kGame;
    std::string gameDir;            // real assets root; empty -> asset-less panel
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;            // -1 = until Back/ESC/close (tests bound it)
    int frameCapMs = 16;            // per-frame sleep (~60fps); 0 = uncapped

    // The reconstructed config the page reads + modifies. The caller seeds these
    // from config::ReadGfxAndSoundSettings (gilde.INI) and reads them back after.
    config::GfxSettings   gfx;
    config::SoundSettings sound;
    config::GameSettings  game;
};

struct OptionsResult {
    bool back = true;               // user left the screen (Back/OK or ESC/Cancel)
    bool changed = false;           // at least one setting differs from the seed
    bool cancelled = false;         // left via ESC/Cancel (true) vs Back/OK (false)

    // The (possibly) modified settings — copy them back into your StartupConfig.
    config::GfxSettings   gfx;
    config::SoundSettings sound;
    config::GameSettings  game;

    int  framesPresented = 0;
    int  hoveredRow = -1;           // last hovered control row (0..rows-1)
    int  lastToggledRow = -1;       // last row whose control the user actuated
    bool quitByWindow = false;
    bool quitByEsc = false;         // ESC -> cancel (discard changes)
};

// A single control row on the panel (one INI-backed setting). Exposed so tests
// can locate a row's hit position and assert the real label / value text.
enum class OptionKind {
    kToggle,   // on/off boolean (cycles 0<->1)
    kCycle,    // small enumerated set (e.g. quality 0..N, sample-rate index)
    kStep,     // numeric slider stepped by a fixed amount (volumes, speeds)
};

struct OptionRow {
    const char* label = "";   // English label (the real INI key's meaning)
    const char* key   = "";   // the exact gilde.INI key it persists to
    OptionKind  kind  = OptionKind::kToggle;
    int value = 0;            // current value (snapshot, for layout/labels)
    int minV = 0, maxV = 1;   // inclusive value range
    int step = 1;            // step amount for kStep / kCycle wrap
};

// Build the row list for a page from the given settings (pure; no I/O). Lets the
// renderer and tests share one source of truth for layout + which keys appear.
std::vector<OptionRow> OptionsRowsFor(OptionsPage page,
                                      const config::GfxSettings& gfx,
                                      const config::SoundSettings& snd,
                                      const config::GameSettings& game);

// Render + run one options page until the user presses Back/OK (apply) or
// ESC/Cancel/window-close (discard). On apply the returned settings carry the
// modifications and `changed` reflects whether any value differs from the seed.
// `device` MUST be init()'d to cfg.fbW x cfg.fbH; `plat`'s window MUST exist.
OptionsResult RunOptionsScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const OptionsConfig& cfg);

} // namespace guild::play
