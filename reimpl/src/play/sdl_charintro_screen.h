#pragma once
// =============================================================================
// guild::play — NATIVE "choose character intro variant" screen (the DIFFICULTY pick).
//
//   VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0 — the screen the new-game funnel
//   runs immediately after the city is confirmed (before RunChooseHistory). Form
//   "menu\\choosecharacter_intro"; its title + options are the rich-text markup
//   VIBE_Text_RenderRichString(0x16CC) == text entry `_M0_DIFFICULTY+0`:
//       $[Уровень сложности]  (heading)
//       Пожалуйста, выберите уровень сложности.  (prompt)
//       %ia[очень легкий] %ia[легкий] %ia[нормальный] %ia[тяжелый] %ia[очень тяжелый]
//       %in[назад]   (the non-selecting "back" — the radio group's 6th member)
//   i.e. the five difficulty levels stored in byte_12335BA (0..4) + a back option,
//   exactly the radio-of-6 the headless reconstruction (gui/choosecharacter_intro_run)
//   drives. This module is the native (Vulkan/SDL) FRONT of that screen, the analogue
//   of sdl_options_screen for gui/options_run: it renders the REAL localized text over
//   a panel and runs the SDL frame loop, mirroring the 1:1 commit logic (OK/Enter on a
//   difficulty row -> variant 0..4 + return; back row / ESC -> cancel).
//
// Backend-agnostic: per-frame render into a 32bpp scratch -> BlitToDevice ->
// dev.present(); read plat.getMouse()/keyDown; left-click EDGE selects; ESC = back;
// cfg.maxFrames bounds it for headless tests; cfg.frameCapMs sleeps.
//
// DEFERRED (named, not faked — rule 8): the exact form-window backing sprite (the
// screen renders the real text over a styled parchment plate + the real menu backdrop
// when gilde.gfx is present, not the pixel-exact form-window chrome).
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

class MenuAssets;  // play/menu_assets.h (real gilde.gfx backdrop, optional)

struct CharIntroConfig {
    std::string gameDir;          // mounted game dir (textbin + gfx); empty = no assets
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;          // -1 = until pick/back/close (tests bound it)
    int frameCapMs = 16;          // per-frame sleep; 0 = uncapped
    int seedVariant = 0;          // current difficulty (byte_12335BA) 0..4 (pre-selected)
};

struct CharIntroResult {
    bool confirmed = false;       // a difficulty was picked -> proceed (return 1)
    bool back      = false;       // back row / ESC / window-close -> cancel (return 0)
    int  variant   = 0;           // chosen difficulty 0..4 (== seedVariant on cancel)
    int  framesPresented = 0;
    int  hoveredRow = -1;
    bool quitByWindow = false;
    bool usedRealText = false;    // the real _M0_DIFFICULTY markup was found + rendered
};

// The screen's content, extracted from the `_M0_DIFFICULTY+0` markup (real localized
// text). `selectable[k]` is false only for the trailing "back" option (%in).
struct CharIntroContent {
    std::string heading;
    std::string prompt;
    std::vector<std::string> options;     // difficulty labels + the back label
    std::vector<bool>        selectable;  // %ia -> true, %in -> false
};

// Pure markup parser (no I/O) for the `_M0_DIFFICULTY` rich-text string — exposed so
// the extraction is unit-testable without assets. Recognizes the `$[heading]`, the
// `$X` control tokens (skipped) and the `%i<sel>[label]` option tokens.
CharIntroContent ParseDifficultyMarkup(const std::string& markup);

// Load the real `_M0_DIFFICULTY+0` markup from `<gameDir>/Resources/textbin_deutsch.BIN`
// (by NAME, so index alignment is irrelevant) and parse it. Returns false if the text
// archive / entry is absent (the caller then shows English fallback labels).
bool LoadCharIntroContent(const std::string& gameDir, CharIntroContent& out);

// Per-row layout of the difficulty panel (design 800x600 form window 128,72,441,490,
// scaled to W x H). Shared by the renderer and the hit-test so they never drift.
struct CharIntroLayout {
    int px, py, pw, ph;            // panel rect
    int cx, titleY, promptY;       // centre x + title/prompt baselines
    int rowsY0, rowH, rowX, rowW;  // option-row grid
    int rowCount = 0;
    void RowRect(int i, int& rx, int& ry, int& rw, int& rh) const;
    int  HitRow(int mx, int my) const;  // -1 if none
};
CharIntroLayout CharIntroComputeLayout(int W, int H, int rowCount);

// Render one frame of the difficulty screen into a 32bpp ARGB scratch (W*H), pure (no
// device/input) so tests + tooling can capture it. `assets` (may be null) supplies the
// real menu backdrop; `hoveredRow`/`seedVariant` drive the row highlight.
void RenderCharIntroFrame(std::uint32_t* scratch, int W, int H, const CharIntroContent& content,
                          int hoveredRow, int seedVariant, MenuAssets* assets);

// Render + run the difficulty screen until the user picks a difficulty (confirm) or
// presses the back row / ESC / closes the window (cancel). `device` MUST be init()'d to
// cfg.fbW x cfg.fbH; `plat`'s window MUST exist.
CharIntroResult RunCharIntroScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   const CharIntroConfig& cfg);

} // namespace guild::play
