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
#include <functional>
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
    // When the INI binding below is live (bindIni + an existing INI), the seed
    // is loaded from the file instead — the native equivalent of the original
    // seeding from the byte_1233xxx globals boot's ReadGfxAndSoundSettings filled.
    config::GfxSettings   gfx;
    config::SoundSettings sound;
    config::GameSettings  game;

    // ---- live INI binding (load seed + persist on OK) ----
    // The original persists every OK via VIBE_Config_WriteGfxSettings @0x56af54
    // (WritePrivateProfileStringA into gilde.INI). With bindIni=true (default)
    // and an existing <iniDir|gameDir>/<iniName>, the screen loads its seed from
    // that file (play::LoadSettings) and on OK persists through the real
    // serializer (play::SaveSettings — merge-preserving, rule 4 shim boundary).
    bool        bindIni = true;
    std::string iniDir;                 // empty -> gameDir
    std::string iniName = "Gilde.INI";

    // ---- Gfx page caps (mirrors the originals' globals) ----
    bool resCap1024 = true;   // byte_62D59A — device supports res index 1 (1024x768)
    bool resCap1280 = true;   // byte_62D59B — device supports res index 2 (1280x1024)
    bool inGame     = false;  // byte_63CC40 — in-game: the resolution row is hidden
                              // (0x56c667 SetVisibleRecursive(child0, 0))
};

struct OptionsResult {
    bool back = true;               // user left the screen (Back/OK or ESC/Cancel)
    bool changed = false;           // at least one setting differs from the seed
    bool cancelled = false;         // left via ESC/Cancel (true) vs Back/OK (false)

    // The (possibly) modified settings — copy them back into your StartupConfig.
    config::GfxSettings   gfx;
    config::SoundSettings sound;
    config::GameSettings  game;

    bool persisted = false;         // settings were written back to the bound INI
    bool resChanged = false;        // gfx only: the cur_res row differs from the
                                    // seed (the original then re-reads the INI —
                                    // 0x56c7e0 ReadGfxAndSoundSettings)

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
    int step = 1;             // step amount for kStep / kCycle wrap
    bool hidden = false;      // built but invisible (SetVisibleRecursive(w, 0));
                              // keeps its layout slot, never hit-tested
    // For discrete rows (kCycle / kToggle): the textbin option-list base key the
    // original appends with VIBE_Text_AppendWideLines — the slider shows option
    // `value` of this list ("_OPTIONEN_STUFEN" -> "_OPTIONEN_STUFEN+<value>").
    // Empty for numeric (kStep) rows, which show "%i" of the value.
    const char* optList = "";
};

// The originals' per-run caps the Gfx row table depends on (see OptionsConfig).
struct OptionsRowCaps {
    bool resCap1024 = true;   // byte_62D59A
    bool resCap1280 = true;   // byte_62D59B
    bool inGame     = false;  // byte_63CC40
};

// Build the row list for a page from the given settings (pure; no I/O). Lets the
// renderer and tests share one source of truth for layout + which keys appear.
// Rows are in the original forms' BUILD ORDER (VIBE_Form_GetChildObjectId child
// index order) with the exact SetValueOrText (min, max, seed) ranges.
std::vector<OptionRow> OptionsRowsFor(OptionsPage page,
                                      const config::GfxSettings& gfx,
                                      const config::SoundSettings& snd,
                                      const config::GameSettings& game,
                                      const OptionsRowCaps& caps = {});

// ---- apply sinks (the OK-path "apply" calls; wave-2 wiring points) ----
// On OK the originals persist (VIBE_Config_WriteGfxSettings) then apply:
//   Sfx  -> VIBE_Audio_ApplyVolumeSettings            @0x56c148
//   Gfx  -> VIBE_Render_ApplyGfxSettings              @0x56be58
//   Game -> VIBE_Config_ApplyCameraAndScrollSettings  @0x56c0cc
// The native screen routes those through this process-wide sink block so the
// changed settings reach the audio/render/camera clusters at the API level.
// Default: all empty (no-op). Install from app wiring.
struct OptionsApplySinks {
    std::function<void(const config::SoundSettings&)> applyVolumeSettings;
    std::function<void(const config::GfxSettings&)>   applyGfxSettings;
    std::function<void(const config::GameSettings&)>  applyCameraScroll;
};
OptionsApplySinks& OptionsApplyHooks();

// Render + run one options page until the user presses Back/OK (apply) or
// ESC/Cancel/window-close (discard). On apply the returned settings carry the
// modifications and `changed` reflects whether any value differs from the seed.
// `device` MUST be init()'d to cfg.fbW x cfg.fbH; `plat`'s window MUST exist.
OptionsResult RunOptionsScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const OptionsConfig& cfg);

} // namespace guild::play
