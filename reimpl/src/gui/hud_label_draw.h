#pragma once
// guild::gui — the HUD floating-text *drawer* functions: the per-frame passes that walk
// a table of live objects/labels, project each to screen space, and blit a centred text
// caption over it.  This is the orchestration half that sits on top of the pure
// placement / expiry / scale math already recovered in gui/hud_labels.{h,cpp}; this
// module REUSES those helpers (Hud_CenteredLabelPlacement, DamageLabel_ExpireSweep,
// Hud_StatusBannerX / _Scale / _Expired, Hud_NameInputCaptionPlacement) verbatim.
//
// Translated 1:1:
//   VIBE_Hud_DrawObjectNameLabels  @0x4bbaec — scene-object name captions (1600/50 walk)
//   VIBE_Hud_DrawAnimalLabels      @0x4bbbbc — animal-species captions (QueryFind walk)
//   VIBE_Hud_DrawCharacterLabels   @0x4bbc8c — player-character name captions (8208/171)
//   VIBE_Hud_DrawDamageLabels      @0x4bb7a0 — combat damage numbers (4288/67, expiry)
//   VIBE_Hud_DrawStatusBanner      @0x4bcb74 — the transient status-line banner (350-tick)
//   VIBE_Hud_DrawNameInputCaption  @0x4bcafc — the "type a name" caption over an anchor
//
// The renderer/world edges the originals call (VIBE_State_Finalize / Animation_Apply /
// State_GetCurrent for the glyph blit, VIBE_Object_ComputeScreenBounds for the 3D
// projection, and the GameObject_QueryFind iterator) are NOT translated here; per the
// project boundary they are forward-declared as a mockable HudLabelSink so the table
// walk / projection-gate / placement logic is exercisable headlessly and byte-exact.

#include "gui/hud_labels.h"   // ScreenBounds / LabelPlacement + the recovered math helpers
#include "gui/hud.h"          // DamageLabelEntry / g_damageLabels table (owned by hud.cpp)
#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// The glyph-blit context id passed to VIBE_State_Finalize before each pass.
//   * the four world-label passes call State_Finalize(67) (the world-overlay layer)
//   * the status banner calls State_Finalize(66) (a separate banner layer)
// ===========================================================================
inline constexpr int kHudLabelStateLayer  = 67; // VIBE_State_Finalize(67)
inline constexpr int kStatusBannerStateLayer = 66; // VIBE_State_Finalize(66)

// The damage-label vertical inset: the original blits at y = bounds.top - 64
// (v5 = v10 - 64), one notch higher than the other captions (which sit at bounds.top).
inline constexpr int kDamageLabelTopInset = 64; // v10 - 64
// Damage labels are 64px narrower-centred than the shared helper? No — they use the
// same `+ (right-left)/2 - 80` x as the others; only the y differs.  Recovered below.

// ---------------------------------------------------------------------------
// One emitted glyph-blit record: exactly the arguments the originals feed to
// VIBE_Animation_Apply(x, y, width, ctx, text, charHeight) followed by
// VIBE_State_GetCurrent(x, y, ...).  `width` is always kHudLabelWidth (160).
// ---------------------------------------------------------------------------
struct LabelDraw {
    int         x;        // Animation_Apply arg1 (centred left x)
    int         y;        // Animation_Apply arg2 (top / banner / anchor y)
    int         width;    // Animation_Apply arg3 (160 for every caption)
    int         charH;    // Animation_Apply arg6 (40 / 32 / 168)
    std::string text;     // Animation_Apply arg5 (the caption string)
};

// ---------------------------------------------------------------------------
// One live object the drawers walk.  The originals read a scene record by raw offset;
// we model just the fields each pass touches: the projection input (handle != 0 gate +
// the {left,top,right,bottom} box ComputeScreenBounds would write) and the caption text.
// `projectable` folds the `ComputeScreenBounds(...) != 0` (on-screen) result so the
// table walk + the per-entry gates stay 1:1 without the real 3D projector.
// ---------------------------------------------------------------------------
struct HudLabelObject {
    bool         present;     // the `dword_...[i]` != 0 / record-exists gate
    bool         projectable; // ComputeScreenBounds returned true (object is on-screen)
    ScreenBounds bounds;      // the projected box (only read when projectable)
    std::string  text;        // the caption to blit
};

// ---------------------------------------------------------------------------
// Mockable draw edge.  Each Emit corresponds to the original's
// VIBE_Animation_Apply + VIBE_State_GetCurrent pair; Layer corresponds to the
// VIBE_State_Finalize(layer) call at the head of each pass.
// ---------------------------------------------------------------------------
struct HudLabelSink {
    virtual ~HudLabelSink() = default;
    virtual void Layer(int stateLayer) { (void)stateLayer; }   // VIBE_State_Finalize
    virtual void Emit(const LabelDraw& d) { (void)d; }         // Animation_Apply+GetCurrent
};

// ===========================================================================
// The translated drawer passes.  Each returns the number of captions emitted.
// ===========================================================================

// gilde.exe 0x4bbaec — VIBE_Hud_DrawObjectNameLabels.
// State_Finalize(67); for each present object that is on-screen, blit its name centred
// over its box at charH 40.  (The original's trailing SceneGraph_TraverseTree gate-label
// pass is a separate function, VIBE_Hud_DrawGateLabel, not reproduced here.)
int Hud_DrawObjectNameLabels(const std::vector<HudLabelObject>& objects,
                             HudLabelSink& sink);

// gilde.exe 0x4bbbbc — VIBE_Hud_DrawAnimalLabels.
// Gated on a "building selected" precondition (selectedBuilding); when set, walks the
// animal query result and blits each species name centred at charH 40.
int Hud_DrawAnimalLabels(bool selectedBuilding,
                         const std::vector<HudLabelObject>& animals,
                         HudLabelSink& sink);

// gilde.exe 0x4bbc8c — VIBE_Hud_DrawCharacterLabels.
// Walks the player-character slot table; for each active slot whose owner is visible,
// blits the character name at its anchor (x = anchorX>>16, y = (anchorY>>16) - 20) at
// charH 32.  The anchors are supplied pre-projected (the original reads packed 16.16
// world positions off the character record).
inline constexpr int kCharacterLabelTopInset = 20; // (anchorY >> 16) - 20
struct HudCharacterLabel {
    bool        active;   // slot state == 1 (dword_676BF0[i] == 1)
    bool        visible;  // the `*(v5 + dword_69FFB4 + 52) == 0` not-occluded gate
    int         anchorX;  // packed 16.16 world x (the original >> 16's it)
    int         anchorY;  // packed 16.16 world y
    std::string text;     // the character name
};
int Hud_DrawCharacterLabels(const std::vector<HudCharacterLabel>& chars,
                            HudLabelSink& sink);

// gilde.exe 0x4bb7a0 — VIBE_Hud_DrawDamageLabels.
// Runs the expiry sweep (DamageLabel_ExpireSweep over the shared g_damageLabels table),
// then State_Finalize(67) and blits each still-live, on-screen damage label centred over
// its target box at y = top - 64, charH 40.  The per-label projection input is supplied
// in `targets` (parallel to g_damageLabels by slot index): present[i] mirrors
// g_damageLabels[i].inUse and supplies the projected box + the formatted number text.
int Hud_DrawDamageLabels(int now,
                         const std::vector<HudLabelObject>& targets,
                         HudLabelSink& sink);

// gilde.exe 0x4bcb74 — VIBE_Hud_DrawStatusBanner.
// When `bannerText` is non-empty, State_Finalize(66), blit the banner at
// x = rightEdge - 300, width 600 (note: NOT 160 — the banner uses a wide field),
// charH 40, then expire it (clear the text) once startTick + 350 <= now.
// Returns 1 if a banner was drawn this frame, else 0.  `outExpired` reports whether the
// lifetime elapsed (so the caller can clear byte_11B6B20, matching the original).
inline constexpr int kStatusBannerWidth = 600;  // Animation_Apply arg3 for the banner
inline constexpr int kStatusBannerCharH = 40;   // Animation_Apply arg6 for the banner
int Hud_DrawStatusBanner(const std::string& bannerText, int startTick, int now,
                         int rightEdge, int modeFlags, HudLabelSink& sink,
                         bool* outExpired);

// gilde.exe 0x4bcafc — VIBE_Hud_DrawNameInputCaption.
// Gated on two flags (inputActive && anchorValid); when both set, State_Finalize(67) and
// blit the caption at x = (anchorX2>>16) - 54, y = (anchorY>>16) + 52, width 160,
// charH 168.  Returns 1 if drawn, else 0.
int Hud_DrawNameInputCaption(bool inputActive, bool anchorValid,
                             int anchorX2Packed, int anchorYPacked,
                             const std::string& caption, HudLabelSink& sink);

} // namespace guild::gui
