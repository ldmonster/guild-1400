#pragma once
// guild::gui — the loading-progress screen (gilde.exe 0x52ee84 / 0x52effc / 0x52f0bc).
//
//   VIBE_Loading_ShowProgressScreen  @0x52ee84 — set up the loading screen + progress bar.
//     form = (word_63C740 & 4) ? "Misc\Loading_net" : "Misc\Loading_game";  // net vs single
//     position centered; SelectWindow(form,1); AddSliderToWindow(0,0,0,582,582,114,66,win)
//     — a 582-wide progress bar; register a per-frame proc.
//   VIBE_Loading_UpdateProgressBar   @0x52effc — set the bar to `pct`%:
//     SelectWindow(form,0); SelectWindow(form,1); GetChildObjectId(form,1,0) ->
//     SetValueOrText(obj, 0, 582, 582*pct/100, ...);  Window_RenderUpdates().
//   VIBE_Loading_FadeOutAndClose     @0x52f0bc — unregister proc; fade to BLACK over 30
//     frames (running a frame loop until the fade completes); Form_Destroy; ColorFill.
//
// We recover the two form names (net / single-player), the progress-bar slider geometry
// (width 582), the percentage->bar math (582*pct/100), and the fade duration (30) + color
// ("BLACK").  The slider widget, frame loop and surface ops are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

inline constexpr const char* kFormLoadingNet  = "Misc\\Loading_net";
inline constexpr const char* kFormLoadingGame = "Misc\\Loading_game";

inline constexpr int kLoadingNetFlag = 0x4; // word_63C740 & 4 -> network loading screen
inline constexpr int kLoadingBarSlot = 1;   // SelectWindow(form, 1) — the progress bar
inline constexpr int kLoadingBarWidth = 582; // AddSliderToWindow span / SetValueOrText max
inline constexpr int kLoadingProc    = 7;    // TimeBase_RegisterProc id

inline constexpr const char* kLoadingFadeColor = "BLACK";
inline constexpr int kLoadingFadeFrames = 30; // Fade_Register duration

// gilde.exe 0x52ee84 — pick the loading form for the current mode.
inline const char* Loading_FormFor(int worldFlags) {
    return (worldFlags & kLoadingNetFlag) ? kFormLoadingNet : kFormLoadingGame;
}

// gilde.exe 0x52effc — the percentage -> bar-value math: 582 * pct / 100 (integer).
//   pct is clamped to [0,100] by the caller; the original does no clamp itself.
inline int Loading_BarValueForPercent(int pct) {
    return kLoadingBarWidth * pct / 100;
}

} // namespace guild::gui
