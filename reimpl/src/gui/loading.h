#pragma once
// guild::gui — the loading-progress screen build sequence (companion to loading_screen.h).
//
// loading_screen.h already recovers the loading-screen constants (form names, net flag,
// bar width 582, the pct->bar math, fade color/frames) and the two pure helpers
// (Loading_FormFor / Loading_BarValueForPercent).  This file adds the remaining
// *build-sequence* recovery for the three loading functions, which loading_screen.h did
// not model:
//
//   VIBE_Loading_ShowProgressScreen @0x52ee84 — picks the form, centers window 1, selects
//       it, and adds a 582-wide progress-bar slider via
//       VIBE_Widget_AddSliderToWindow(0,0,0, 582,582, 114,66, curWin), then registers a
//       per-frame proc with id 7 (VIBE_TimeBase_RegisterProc(.., 7)).
//   VIBE_Loading_UpdateProgressBar  @0x52effc — SelectWindow(form,0)+SelectWindow(form,1);
//       GetChildObjectId(form,1,0) -> SetValueOrText(obj, 0, 582, 582*pct/100); RenderUpdates.
//   VIBE_Loading_FadeOutAndClose    @0x52f0bc — UnregisterProc; Fade_Register(.., "BLACK",
//       30, 1); run the frame loop until the fade completes; Form_Destroy; ColorFill.
//
// The slider widget, frame loop, fade engine and surface ops live in other clusters and
// are forward-declared / modelled here as a plan + the recovered constants.

#include "gui/loading_screen.h"

namespace guild::gui {

// Slider gfx ids in the AddSliderToWindow(0,0,0, 582,582, 114,66, win) call.
inline constexpr int kLoadingSliderSpan = 582; // the 582,582 span pair
inline constexpr int kLoadingSliderGfxA = 114;
inline constexpr int kLoadingSliderGfxB = 66;
inline constexpr int kLoadingProcId     = 7;   // TimeBase_RegisterProc id (== kLoadingProc)

// The build plan ShowProgressScreen assembles (the byte-stable part).
struct LoadingScreenPlan {
    const char* form = nullptr;  // Loading_FormFor(worldFlags)
    int barWindowSlot = kLoadingBarSlot;        // SelectWindow(form, 1)
    int sliderSpan = kLoadingSliderSpan;        // 582
    int sliderGfxA = kLoadingSliderGfxA;        // 114
    int sliderGfxB = kLoadingSliderGfxB;        // 66
    int procId = kLoadingProcId;                // 7
};

// gilde.exe 0x52ee84 — assemble the loading-screen plan for `worldFlags` (word_63C740).
LoadingScreenPlan Loading_BuildProgressScreen(int worldFlags);

// gilde.exe 0x52effc — the value-or-text args UpdateProgressBar passes for `pct`%:
//   SetValueOrText(obj, min=0, max=582, value=582*pct/100).  Returns {min,max,value}.
struct LoadingBarUpdate { int min; int max; int value; };
LoadingBarUpdate Loading_BuildBarUpdate(int pct);

// The fade plan FadeOutAndClose assembles.
struct LoadingFadePlan {
    const char* color = kLoadingFadeColor;  // "BLACK"
    int frames = kLoadingFadeFrames;        // 30
    bool unregisterProc = true;             // UnregisterProc(proc) before the fade
};
// gilde.exe 0x52f0bc — the fade-out close plan.
LoadingFadePlan Loading_BuildFadeOut();

} // namespace guild::gui
