#pragma once
// guild::gui — the three options sub-screens: Game / Graphics / Sound.
//
// gilde.exe 0x56cc44 — VIBE_Menu_RunOptionsGame  (form "MENU\OPTIONS_GAME", title 6246)
// gilde.exe 0x56c21c — VIBE_Menu_RunOptionsGfx   (form "MENU\OPTIONS_GFX",  title 6247)
// gilde.exe 0x56c808 — VIBE_Menu_RunOptionsSfx   (form "MENU\OPTIONS_SFX",  title 6248)
//
// Each of the three runners loads its form, fetches its window-1 child slider/dropdown
// widgets (VIBE_Form_GetChildObjectId(form, 1, i)), seeds each from the matching
// config-backed setting global (config::GfxSettings / SoundSettings / GameSettings),
// sets the per-widget scroll-limit and (for dropdowns) appends the option-line text,
// builds the OK/Cancel button row, runs the shared frame loop, and on OK writes every
// slider's live value back to the setting global before re-persisting (WriteGfxSettings)
// and applying it (audio volume / render gfx / camera+scroll).
//
// What this module recovers byte-for-byte and exposes for isolated testing:
//   * the per-screen widget LAYOUT TABLE (child index -> setting field, value range,
//     scroll-limit, dropdown line count) — the build-loop arguments;
//   * the OK save-back MAPPING (which widget value lands in which setting field, incl.
//     the gfx brightness inversion 100-(v-50)+50 and the sfx volume scaling);
//   * the OK/Cancel/close dispatch and the "apply" actions, routed through a sink so the
//     dispatch + save-back are testable without the OS / render / audio clusters.
//
// ODR: the setting structs (config::GfxSettings / SoundSettings / GameSettings) and the
// writer (app/config_write) already exist — REUSED, not redefined.  The options sub-tab
// strip (gui/menu_sub) and the 7-button options column (gui/menu) already exist too.

#include "gui/types.h"
#include "config/ini.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Form identity (VIBE_GameTick_Finalize(0,0,name) + VIBE_Text_RenderRichString(title)).
// ---------------------------------------------------------------------------
inline constexpr const char* kOptionsGameForm = "menu\\options_game"; // 0x56cc5c
inline constexpr const char* kOptionsGfxForm  = "menu\\options_gfx";  // 0x56c247
inline constexpr const char* kOptionsSfxForm  = "menu\\options_sfx";  // 0x56c82f
inline constexpr int kOptionsGameTitle = 6246; // 0x56ccbb
inline constexpr int kOptionsGfxTitle  = 6247; // 0x56c29c
inline constexpr int kOptionsSfxTitle  = 6248; // 0x56c88d

// Shared scroll-limit codes passed to VIBE_Widget_SetScrollLimit.
inline constexpr int kScrollLimitSlider   = 2178; // horizontal value slider
inline constexpr int kScrollLimitDropdown = 130;  // discrete option list

// ===========================================================================
// Sound options — gilde.exe 0x56c808.
// Window-1 children 0..4: master/sfx/music/speech volume sliders (range 127) +
// the music-frequency dropdown (range 4, 5 lines).
// ===========================================================================
enum class SfxField {
    kMasterVol = 0, // child 0  byte_1233550  range 127  slider
    kSfxVol    = 1, // child 1  byte_1233551  range 127  slider
    kMsxVol    = 2, // child 2  byte_1233552  range 127  slider
    kSpeechVol = 3, // child 3  byte_1233553  range 127  slider
    kMsxFreq   = 4, // child 4  byte_1233554  range 4    dropdown (5 lines)
};
inline constexpr int kSfxWidgetCount = 5;

// Seed `s` widgets-> the build-loop SetValueOrText(range, value) args, byte-for-byte.
struct SfxWidgetDesc {
    SfxField field;     // which child / setting
    int      range;     // SetValueOrText `max` argument
    int      scrollLim; // SetScrollLimit code
    int      lines;     // dropdown line count (0 for a plain slider)
};
extern const SfxWidgetDesc kSfxWidgets[kSfxWidgetCount];

// ===========================================================================
// Graphics options — gilde.exe 0x56c21c.
// Window-1 children 0..8.  Child 0 is the resolution dropdown (seeded from the
// byte_62D59A/B device caps, NOT a saved field, and hidden when byte_63CC40);
// children 1..8 are the gfx-detail dropdowns + the gamma/brightness slider.
// ===========================================================================
// Index = the build/enum order; the trailing comment gives the form child index, the
// destination byte_1233514+N global and the matching config::GfxSettings field.
enum class GfxField {
    kResolution   = 0, // child 0  cur_res (byte_63D724)        range v30(1|2)  dropdown 3
    kDetails      = 1, // child 1  byte_1233514 details         range 2  dropdown 3
    kTextureScale = 2, // child 2  byte_1233515 textureScale    range 2  dropdown 3
    kFloorLod     = 3, // child 3  byte_123351A floorLod        range 2  dropdown 3
    kFloorMipmap  = 4, // child 4  byte_1233518 floorMipmapping range 1  dropdown 2
    kLodHandling  = 5, // child 5  byte_1233516 lodHandling     range 1  dropdown 2
    kShadowDetail = 6, // child 6  byte_1233517 shadowDetail    range 2  dropdown 3
    kGamma        = 7, // child 7  byte_123351C fogPlane (inv)  range 100 slider, base 50
    kCameraLimits = 8, // child 8  byte_1233519 cameraLimits    range 2  dropdown 3
};
inline constexpr int kGfxWidgetCount = 9;

struct GfxWidgetDesc {
    GfxField field;
    int      range;     // SetValueOrText `max`
    int      scrollLim;
    int      lines;     // dropdown line count (0 = slider)
};
extern const GfxWidgetDesc kGfxWidgets[kGfxWidgetCount];

// gilde.exe 0x56c5df / 0x56c79d — the brightness/gamma slider stores its value
// inverted around 50.  Seed (0x56c5df): SetValueOrText(min=50, max=100, value = 100 -
// byte_123351C).  Save (0x56c79d): byte_123351C = 50 - (live - 50)  (== 100 - live).
// Disasm-verified: the seed is `100 - saved` (NOT `100 - (saved+50)`); the slider range
// is [50,100] and the SAVED byte is the symmetric reflection about 50 (== 100-live).
int Gfx_GammaSeed(int saved);   // 0x56c5df: SetValueOrText(50, 100, 100 - saved)
int Gfx_GammaSave(int live);    // 0x56c79d: 50 - (live - 50)  == 100 - live

// ===========================================================================
// Game options — gilde.exe 0x56cc44.
// Window-1 children 0,1,2,3 are value sliders; 4,5,6,9,10,11,12 are dropdowns.
// (Child 4 "invert mouse" is created then force-hidden + saved as 0.)
// ===========================================================================
enum class GameField {
    kSpeed        = 0,  // child 0  dword_1233558  range 160  slider
    kScrollSpeed  = 1,  // child 1  dword_1233560  range 500  slider
    kMouseSpeed   = 2,  // child 2  dword_1233564  range 100  slider
    kCameraSpeed  = 3,  // child 3  byte_123355C   range 100  slider
    kInvertMouse  = 4,  // child 4  byte_1233568   range 1    dropdown (hidden, saved 0)
    kNachtwaechter= 5,  // child 5  byte_123356A   range 1    dropdown 2 lines
    kShowCursorTxt= 6,  // child 6  byte_123356B   range 1    dropdown 2 lines
    kDifficulty   = 9,  // child 9  byte_12335B8   range 4    dropdown 5 lines (apply!)
    kHints        = 10, // child 10 byte_12335B9   range 1    dropdown 2 lines
    kPanelHelp    = 11, // child 11 byte_12335BB   range 1    dropdown 2 lines
    kPanelMode    = 12, // child 12 byte_12335BC   range 1    dropdown 2 lines
};
inline constexpr int kGameWidgetCount = 11;

struct GameWidgetDesc {
    GameField field;
    int       childIndex; // VIBE_Form_GetChildObjectId(form,1,childIndex)
    int       range;      // SetValueOrText `max`
    int       scrollLim;
    int       lines;      // dropdown line count (0 = slider)
};
extern const GameWidgetDesc kGameWidgets[kGameWidgetCount];

// ---------------------------------------------------------------------------
// Save-back helpers.  Each runner reads back the widget values on OK and stores them
// into the supplied setting struct (the in-memory mirror of the byte_1233xxx globals),
// mirroring the assignment block at the bottom of each runner.  `values[i]` is the live
// VIBE_Object_GetDataPtr of the i-th widget in BUILD ORDER (== the kXxxWidgets[] row
// order).  For Sfx/Gfx the enum value equals the build index; for Game the enum value is
// the sparse form child index, so Game_SaveBack indexes by build order, not enum value.
// ---------------------------------------------------------------------------

// 0x56cbee block.  Sfx: master/sfx/music/speech/freq straight through.
void Sfx_SaveBack(const int values[kSfxWidgetCount], config::SoundSettings& snd);

// 0x56c735 block.  Gfx: details/texture/lod/fog/shadow/floorlod/floormip straight,
// gamma inverted (Gfx_GammaSave); the resolution (child 0) is handled separately.
void Gfx_SaveBack(const int values[kGfxWidgetCount], config::GfxSettings& gfx);

// 0x56d184 block.  Game: speed/scroll/mouse/camera + the dropdowns; invert_mouse forced
// to 0 (byte_1233568 = 0).  difficulty (child 9) drives the groundplan rebuild.
void Game_SaveBack(const int values[kGameWidgetCount], config::GameSettings& game);

// ---------------------------------------------------------------------------
// Command hook (mockable).  The OK path persists + applies the settings via the wider
// config / render / audio clusters; route them through a sink so the dispatch + save-back
// is testable in isolation.  The frame loop sets dword_631614 (close) on OK or Cancel.
// ---------------------------------------------------------------------------
struct OptionsScreenSink {
    virtual ~OptionsScreenSink() = default;
    virtual void WriteGfxSettings() {}            // VIBE_Config_WriteGfxSettings
    virtual void ApplyVolumeSettings() {}         // VIBE_Audio_ApplyVolumeSettings
    virtual void ApplyGfxSettings() {}            // VIBE_Render_ApplyGfxSettings
    virtual void ApplyCameraAndScroll() {}        // VIBE_Config_ApplyCameraAndScrollSettings
    virtual void ReloadResolution() {}            // VIBE_Config_ReadGfxAndSoundSettings (res change)
    virtual void RebuildGroundplan() {}           // difficulty change w/ byte_63CC40
};
void Options_SetSink(OptionsScreenSink* sink);

// The result of one options screen: whether OK (accept) was pressed and the form closed.
struct OptionsResult {
    bool accepted = false; // OK pressed (v5/v29/v6 != 0)
    bool resChanged = false; // gfx only: resolution dropdown differed -> ReloadResolution
};

// gilde.exe 0x56c808 — apply the Sfx OK-path: if `accepted`, save back to `snd`, then
// WriteGfxSettings + ApplyVolumeSettings.  Returns the result.
OptionsResult Sfx_Commit(bool accepted, const int values[kSfxWidgetCount],
                         config::SoundSettings& snd);

// gilde.exe 0x56c21c — apply the Gfx OK-path.  `resValue`/`resSaved` are the live and
// stored resolution-dropdown values (child 0); a difference triggers ReloadResolution.
OptionsResult Gfx_Commit(bool accepted, const int values[kGfxWidgetCount], int resValue,
                         int resSaved, config::GfxSettings& gfx);

// gilde.exe 0x56cc44 — apply the Game OK-path.  `diffValue`/`diffSaved` are the live and
// stored difficulty-dropdown values (child 9); a difference + byte_63CC40 rebuilds the
// groundplan.  Always WriteGfxSettings + ApplyCameraAndScroll on accept.
OptionsResult Game_Commit(bool accepted, const int values[kGameWidgetCount], int diffValue,
                          int diffSaved, config::GameSettings& game);

} // namespace guild::gui
