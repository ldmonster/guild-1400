#include "gui/options_screens.h"

// guild::gui — Game / Graphics / Sound options sub-screens.
//
// gilde.exe 0x56cc44 / 0x56c21c / 0x56c808.  See options_screens.h for the recovery
// notes.  The three runners share a fixed shape:
//   GameTick_Finalize(form) -> PositionChildWindows -> SelectWindow(2)+RenderRichString
//   (title) -> SelectWindow(1) + per-child GetChildObjectId/SetValueOrText/SetScrollLimit
//   (+AppendWideLines for dropdowns) -> SelectWindow(3) + Hud_BuildButtonRow (OK/Cancel)
//   -> frame loop -> on OK: read back GetDataPtr per child, persist + apply -> Form_Destroy.
// We translate the data-driven layout tables, the OK save-back maps, and the commit
// dispatch; the form/render/audio/render calls live in unowned modules (sink + the real
// runner wiring).

namespace guild::gui {

// ---------------------------------------------------------------------------
// Layout tables — the build-loop arguments, byte-for-byte.
// ---------------------------------------------------------------------------

// gilde.exe 0x56c920..0x56ca24 — four range-127 volume sliders + the range-4 / 5-line
// music-frequency dropdown.
const SfxWidgetDesc kSfxWidgets[kSfxWidgetCount] = {
    {SfxField::kMasterVol, 127, kScrollLimitSlider,   0}, // child 0  byte_1233550
    {SfxField::kSfxVol,    127, kScrollLimitSlider,   0}, // child 1  byte_1233551
    {SfxField::kMsxVol,    127, kScrollLimitSlider,   0}, // child 2  byte_1233552
    {SfxField::kSpeechVol, 127, kScrollLimitSlider,   0}, // child 3  byte_1233553
    {SfxField::kMsxFreq,     4, kScrollLimitDropdown, 5}, // child 4  byte_1233554 (5 lines)
};

// gilde.exe 0x56c390..0x56c657 — the resolution dropdown (child 0) + eight gfx-detail
// dropdowns / the gamma slider (child 7).
const GfxWidgetDesc kGfxWidgets[kGfxWidgetCount] = {
    {GfxField::kResolution,     1, kScrollLimitDropdown, 3}, // child 0 cur_res (range=v30)
    {GfxField::kDetails,        2, kScrollLimitDropdown, 3}, // child 1 (3 lines dword_8C98B0)
    {GfxField::kTextureScale,   2, kScrollLimitDropdown, 3}, // child 2
    {GfxField::kFloorLod,       2, kScrollLimitDropdown, 3}, // child 3
    {GfxField::kFloorMipmap,    1, kScrollLimitDropdown, 2}, // child 4 (2 lines dword_8C98D0)
    {GfxField::kLodHandling,    1, kScrollLimitDropdown, 2}, // child 5
    {GfxField::kShadowDetail,   2, kScrollLimitDropdown, 3}, // child 6
    {GfxField::kGamma,        100, kScrollLimitSlider,   0}, // child 7 slider, base 50
    {GfxField::kCameraLimits,   2, kScrollLimitDropdown, 3}, // child 8
};

// gilde.exe 0x56cde3..0x56d0c5 — four value sliders + the dropdowns.  Note the non-
// contiguous form child indices (skips 7/8, jumps to 9..12).
const GameWidgetDesc kGameWidgets[kGameWidgetCount] = {
    {GameField::kSpeed,         0, 160, kScrollLimitSlider,   0}, // dword_1233558
    {GameField::kScrollSpeed,   1, 500, kScrollLimitSlider,   0}, // dword_1233560
    {GameField::kMouseSpeed,    2, 100, kScrollLimitSlider,   0}, // dword_1233564
    {GameField::kCameraSpeed,   3, 100, kScrollLimitSlider,   0}, // byte_123355C
    {GameField::kInvertMouse,   4,   1, kScrollLimitDropdown, 2}, // byte_1233568 (hidden)
    {GameField::kNachtwaechter, 5,   1, kScrollLimitDropdown, 2}, // byte_123356A
    {GameField::kShowCursorTxt, 6,   1, kScrollLimitDropdown, 2}, // byte_123356B
    {GameField::kDifficulty,    9,   4, kScrollLimitDropdown, 5}, // byte_12335B8 (5 lines)
    {GameField::kHints,        10,   1, kScrollLimitDropdown, 2}, // byte_12335B9
    {GameField::kPanelHelp,    11,   1, kScrollLimitDropdown, 2}, // byte_12335BB
    {GameField::kPanelMode,    12,   1, kScrollLimitDropdown, 2}, // byte_12335BC
};

// ---------------------------------------------------------------------------
// Gamma inversion — gilde.exe 0x56c5df (seed) / 0x56c79d (save).
// Seed:  SetValueOrText(min=50, max=100, value = 100 - saved).
// Save:  byte_123351C = 50 - (live - 50)  == 100 - live.
// ---------------------------------------------------------------------------
int Gfx_GammaSeed(int saved) { return 100 - saved; }              // 0x56c5df
int Gfx_GammaSave(int live)  { return 50 - (live - 50); }         // 0x56c79d (= 100 - live)

// ---------------------------------------------------------------------------
// Save-back maps.
// ---------------------------------------------------------------------------

// gilde.exe 0x56cbee — straight write of all five slider values.
void Sfx_SaveBack(const int values[kSfxWidgetCount], config::SoundSettings& snd) {
    snd.masterVol = static_cast<guild::u8>(values[static_cast<int>(SfxField::kMasterVol)]);
    snd.sfxVol    = static_cast<guild::u8>(values[static_cast<int>(SfxField::kSfxVol)]);
    snd.msxVol    = static_cast<guild::u8>(values[static_cast<int>(SfxField::kMsxVol)]);
    snd.speechVol = static_cast<guild::u8>(values[static_cast<int>(SfxField::kSpeechVol)]);
    snd.msxFreq   = static_cast<guild::u8>(values[static_cast<int>(SfxField::kMsxFreq)]);
}

// gilde.exe 0x56c735 — each detail dropdown -> its byte_1233514+N global; the gamma
// slider (child 7) is stored inverted.  The resolution dropdown (child 0) is handled by
// Gfx_Commit (it gates a re-read, not a struct field here).
void Gfx_SaveBack(const int values[kGfxWidgetCount], config::GfxSettings& gfx) {
    gfx.details         = static_cast<guild::u8>(values[static_cast<int>(GfxField::kDetails)]);
    gfx.textureScale    = static_cast<guild::u8>(values[static_cast<int>(GfxField::kTextureScale)]);
    gfx.lodHandling     = static_cast<guild::u8>(values[static_cast<int>(GfxField::kLodHandling)]);
    gfx.shadowDetail    = static_cast<guild::u8>(values[static_cast<int>(GfxField::kShadowDetail)]);
    gfx.floorMipmapping = static_cast<guild::u8>(values[static_cast<int>(GfxField::kFloorMipmap)]);
    gfx.cameraLimits    = static_cast<guild::u8>(values[static_cast<int>(GfxField::kCameraLimits)]);
    gfx.floorLod        = static_cast<guild::u8>(values[static_cast<int>(GfxField::kFloorLod)]);
    // byte_123351C (the GfxSettings::fogPlane slot) holds the inverted gamma slider.
    gfx.fogPlane        = static_cast<guild::u8>(Gfx_GammaSave(values[static_cast<int>(GfxField::kGamma)]));
}

// gilde.exe 0x56d184 — slider + dropdown write-back; invert_mouse forced to 0.
// NOTE: `values[]` is indexed by BUILD ORDER (the kGameWidgets row, 0..10), NOT by the
// GameField enum value (which carries the sparse form child index 0,1,2,3,4,5,6,9,..12).
void Game_SaveBack(const int values[kGameWidgetCount], config::GameSettings& game) {
    game.speed         = values[0]; // dword_1233558 speed
    game.scrollSpeed   = values[1]; // dword_1233560 scroll
    game.mouseSpeed    = values[2]; // dword_1233564 mouse
    game.cameraSpeed   = static_cast<guild::u8>(values[3]); // byte_123355C camera
    game.invertMouse   = 0;                                 // byte_1233568 = 0 (forced)
    game.nachtwaechter = static_cast<guild::u8>(values[5]); // byte_123356A child 5
    game.showCursorTxt = static_cast<guild::u8>(values[6]); // byte_123356B child 6
    game.difficulty    = static_cast<guild::u8>(values[7]); // byte_12335B8 child 9
    game.hints         = static_cast<guild::u8>(values[8]); // byte_12335B9 child 10
    game.panelHelp     = static_cast<guild::u8>(values[9]); // byte_12335BB child 11
    game.panelMode     = static_cast<guild::u8>(values[10]);// byte_12335BC child 12
}

// ---------------------------------------------------------------------------
// Command sink + commit dispatch.
// ---------------------------------------------------------------------------
namespace {
OptionsScreenSink g_defaultSink;
OptionsScreenSink* g_sink = &g_defaultSink;
} // namespace

void Options_SetSink(OptionsScreenSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x56cbd2 — if accepted: save back, WriteGfxSettings, ApplyVolumeSettings.
OptionsResult Sfx_Commit(bool accepted, const int values[kSfxWidgetCount],
                         config::SoundSettings& snd) {
    OptionsResult r;
    r.accepted = accepted;
    if (accepted) {
        Sfx_SaveBack(values, snd);
        g_sink->WriteGfxSettings();
        g_sink->ApplyVolumeSettings();
    }
    return r;
}

// gilde.exe 0x56c726 — if accepted: save back + WriteGfxSettings + ApplyGfxSettings.
// Separately (0x56c7b1): if the resolution dropdown value changed AND accepted, persist
// cur_res and re-read the gfx+sound settings (resolution apply path).
OptionsResult Gfx_Commit(bool accepted, const int values[kGfxWidgetCount], int resValue,
                         int resSaved, config::GfxSettings& gfx) {
    OptionsResult r;
    r.accepted = accepted;
    if (accepted) {
        Gfx_SaveBack(values, gfx);
        g_sink->WriteGfxSettings();
        g_sink->ApplyGfxSettings();
    }
    // 0x56c7c2: if (GetDataPtr(child0) != byte_63D724 && accepted) { write cur_res; reread }
    if (accepted && resValue != resSaved) {
        gfx.curRes = static_cast<guild::u8>(resValue);
        g_sink->WriteGfxSettings();
        g_sink->ReloadResolution();
        r.resChanged = true;
    }
    return r;
}

// gilde.exe 0x56d166 — on Cancel, return immediately (no save).  On OK: save back; if the
// difficulty dropdown changed, write byte_12335B8 + SetObjectsVisible(form,0), and — ONLY
// when byte_63CC40 (in-game) — rebuild the groundplan + fade; always WriteGfxSettings +
// ApplyCameraAndScroll.
//
// Disasm note (0x56d207..0x56d24c): the comparison is `live(child9) != prior byte_12335B8`
// (prior == the seeded value), independent of byte_63CC40.  Only the groundplan-rebuild
// branch (DestroyWindow/CreateWindow/FadeOutToBlack/dword_631DB4=1) is gated on byte_63CC40.
// The runner encodes the change-detection in `diffSaved`: in-game it passes the prior
// difficulty so a real change fires RebuildGroundplan(); out-of-game it passes diffValue so
// the branch is suppressed (matching the original — no groundplan rebuild out-of-game).
// Game_SaveBack already writes game.difficulty unconditionally, so the saved struct value is
// identical to the original whether or not the change branch runs.
// BOUNDARY: the original's out-of-game `SetObjectsVisible(form,0)` on a difficulty change has
// no observable effect (no groundplan present) and no sink hook; not modeled here.
OptionsResult Game_Commit(bool accepted, const int values[kGameWidgetCount], int diffValue,
                          int diffSaved, config::GameSettings& game) {
    OptionsResult r;
    r.accepted = accepted;
    if (!accepted) return r; // 0x56d166: !v6 -> Form_Destroy, no persist
    Game_SaveBack(values, game);
    // 0x56d214: if (GetDataPtr(child9 difficulty) != prior) -> rebuild groundplan path.
    if (diffValue != diffSaved) {
        game.difficulty = static_cast<guild::u8>(diffValue);
        g_sink->RebuildGroundplan();
    }
    g_sink->WriteGfxSettings();
    g_sink->ApplyCameraAndScroll();
    return r;
}

} // namespace guild::gui
