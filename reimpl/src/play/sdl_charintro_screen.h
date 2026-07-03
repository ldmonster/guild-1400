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
class MenuFont;    // play/menu_assets.h (the baked-gold _FONT / black _FONT+2)

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
    int px, py, pw, ph;            // panel rect (form window backing; unused for art path)
    int cx, titleY, promptY;       // centre x + title/prompt CENTRE-y
    int rowsY0, rowH, rowX, rowW;  // option-row grid (rowsY0 = top of button 0; rowH = pitch)
    int btnH = 0;                  // button face height (rowH is the row pitch)
    int rowCount = 0;
    void RowRect(int i, int& rx, int& ry, int& rw, int& rh) const;
    int  HitRow(int mx, int my) const;  // -1 if none
};
CharIntroLayout CharIntroComputeLayout(int W, int H, int rowCount);

// Render one frame of the difficulty screen into a 32bpp ARGB scratch (W*H), pure (no
// device/input) so tests + tooling can capture it. `backdrop` (may be null, W*H ARGB) is
// the pre-rendered New-Game desk scene drawn behind the parchment form; when absent the
// `assets` menu backdrop (or a flat fill) is used. `gameDir` supplies the parchment-form
// texture (Textures.BIN). `hoveredRow`/`seedVariant` drive the row highlight.
void RenderCharIntroFrame(std::uint32_t* scratch, int W, int H, const CharIntroContent& content,
                          int hoveredRow, int seedVariant, MenuAssets* assets,
                          const std::uint32_t* backdrop = nullptr,
                          const std::string& gameDir = std::string());

// Render + run the difficulty screen until the user picks a difficulty (confirm) or
// presses the back row / ESC / closes the window (cancel). `device` MUST be init()'d to
// cfg.fbW x cfg.fbH; `plat`'s window MUST exist.
CharIntroResult RunCharIntroScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   const CharIntroConfig& cfg);

// ---------------------------------------------------------------------------
// Choose-history screen (VIBE_Menu_RunChooseHistory) — the New-Game sub-screen reached
// after the difficulty pick. Same desk backdrop + parchment form + black _FONT+2 title,
// but with a multi-line WRAPPED body paragraph and N per-label-width red buttons.
// ---------------------------------------------------------------------------
struct NewGameButtonRect { int x = 0, y = 0, w = 0, h = 0; };

// Per-button rects for a New-Game radio screen (choose-history / tasks): every button
// equalized to the WIDEST gold-_FONT label, centred on screen, stacked from `btnTop0Design`
// (design-space top y of button 0) at a 40px pitch. Shared by the renderer and the run-loop
// hit-test so they never drift. `assets` supplies the gold _FONT metrics. The default
// `btnTop0Design` (324) is the choose-history column; the tasks screen passes 246.
std::vector<NewGameButtonRect>
ChooseHistoryButtonRects(int W, int H, const CharIntroContent& content, MenuAssets* assets,
                         int btnTop0Design = 324);

// Hit-test the radio buttons; returns the row index under (mx,my) or -1.
int ChooseHistoryHitRow(const std::vector<NewGameButtonRect>& rects, int mx, int my);

// Render one frame of a New-Game radio screen into a 32bpp ARGB scratch (W*H). Same
// backdrop/parchment/title conventions as RenderCharIntroFrame; `content.heading` is the
// title, `content.prompt` the wrapped body, `content.options` the equal-width buttons
// stacked from `btnTop0Design` (324 = history, 246 = tasks).
void RenderChooseHistoryFrame(std::uint32_t* scratch, int W, int H,
                              const CharIntroContent& content, int hoveredRow, int seedRow,
                              MenuAssets* assets, const std::uint32_t* backdrop = nullptr,
                              const std::string& gameDir = std::string(),
                              int btnTop0Design = 324);

// Shared New-Game parchment chrome (history/tasks/player): composite `backdrop` (or the
// `assets` menu bg), draw the parchment sheet to the centred form rect (top/height in
// 800x600 design units), then an optional black _FONT+2 `title` (centre-y) + wrapped `body`
// (top-y, line pitch, wrap width). Page-specific widgets are the caller's job afterward.
void RenderNewGameParchmentChrome(std::uint32_t* scratch, int W, int H,
                                  const std::uint32_t* backdrop, MenuAssets* assets,
                                  const std::string& gameDir, int parchTopDesign, int parchHDesign,
                                  int parchWDesign,
                                  const std::string& title, int titleCyDesign,
                                  const std::string& body, int bodyTopDesign,
                                  int bodyLineHDesign, int bodyWrapWDesign);

// Draw a single red _BUTTON_RED 3-slice bar + centred gold _FONT label (e.g. the wizard's back).
void DrawNewGameButton(std::uint32_t* dst, int W, int H, MenuAssets& assets,
                       int x, int y, int w, int h, bool hover, const std::string& label);

// The parchment title font (_FONT+2, natively black) for callers needing matching gothic
// text (e.g. the player wizard's "Имя: …" input line). Null if assets absent. Cached.
const MenuFont* NewGameTitleFont(const std::string& gameDir);

} // namespace guild::play
