#include "gui/loading.h"

namespace guild::gui {

// gilde.exe 0x52ee84 — VIBE_Loading_ShowProgressScreen.
//   form = (word_63C740 & 4) ? "Misc\Loading_net" : "Misc\Loading_game";
//   GameTick_Finalize(0,0,form); LayoutBounds(...); PositionCentered(win1, 1);
//   SelectWindow(form, 1); AddSliderToWindow(0,0,0, 582,582, 114,66, curWin);
//   RenderUpdates(); return TimeBase_RegisterProc(.., 7);
LoadingScreenPlan Loading_BuildProgressScreen(int worldFlags) {
    LoadingScreenPlan p{};
    p.form = Loading_FormFor(worldFlags);   // net vs single-player form
    p.barWindowSlot = kLoadingBarSlot;      // 1
    p.sliderSpan = kLoadingSliderSpan;      // 582
    p.sliderGfxA = kLoadingSliderGfxA;      // 114
    p.sliderGfxB = kLoadingSliderGfxB;      // 66
    p.procId = kLoadingProcId;              // 7
    return p;
}

// gilde.exe 0x52effc — VIBE_Loading_UpdateProgressBar.
//   obj = GetChildObjectId(form,1,0); SetValueOrText(obj, 0, 582, 582*pct/100).
LoadingBarUpdate Loading_BuildBarUpdate(int pct) {
    LoadingBarUpdate u{};
    u.min = 0;
    u.max = kLoadingBarWidth;                       // 582
    u.value = Loading_BarValueForPercent(pct);      // 582 * pct / 100
    return u;
}

// gilde.exe 0x52f0bc — VIBE_Loading_FadeOutAndClose.
//   UnregisterProc(proc); Fade_Register(0,0,.., "BLACK", 30, 1); run loop until done;
//   Form_Destroy(form); ColorFill(primary).
LoadingFadePlan Loading_BuildFadeOut() {
    LoadingFadePlan p{};
    p.color = kLoadingFadeColor;   // "BLACK"
    p.frames = kLoadingFadeFrames; // 30
    p.unregisterProc = true;
    return p;
}

} // namespace guild::gui
