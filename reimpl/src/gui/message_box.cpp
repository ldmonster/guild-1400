#include "gui/message_box.h"

#include "gui/dialog.h"
#include "gui/window.h"
#include "gui/radiogroup.h"
#include "gui/input.h"   // kIdOk / kIdCancel

namespace guild::gui {

// ===========================================================================
// Per-box reentrancy guards (own these; no other module defines them).
// ===========================================================================
u8 g_msgGuardNormal   = 0; // byte_631DA8
u8 g_msgGuardBig      = 0; // byte_631600
u8 g_msgGuardRun      = 0; // byte_63D730
u8 g_msgGuardGreen    = 0; // byte_631DA9
u8 g_msgGuardModeless = 0; // byte_631DAA

// ===========================================================================
// The live render palette (dword_11BC2D0).  Owned by the render cluster in the
// original; declared here as the single GUI-side mirror used by the box.  We
// keep our own definition because no translated module currently defines it
// (frame_drivers.* only read it via their own externs).
// ===========================================================================
i32 g_msgPalette = 0; // dword_11BC2D0 (snapshot the box reads & masks)

namespace {

// ---------------------------------------------------------------------------
// Shared form-name selection — identical to dialog.cpp's Dialog_FormForFlags,
// but the originals special-case the 0x100 "NONE_PERGA" path (which also selects
// window 1 before rendering).  Returns the .form name and reports whether the
// NONE_PERGA window-1 pre-select is needed.  Priority matches the decompiles:
//   Autosize(0x02) > Big(0x10) > VeryBig(0x20) > NonePerga(0x100) > default.
const char* SelectMessageForm(int flags, bool& selectWindow1) {
    selectWindow1 = false;
    if (flags & kMsgFlagAutosize)
        return "misc\\Messagebox_Autosize";
    if (flags & kMsgFlagBig)
        return "misc\\Messagebox_BIG";
    if (flags & kMsgFlagVeryBig)
        return "misc\\Messagebox_VERY_BIG";
    if (flags & kMsgFlagNonePerga) {
        selectWindow1 = true;
        return "misc\\Messagebox_NONE_PERGA";
    }
    return "misc\\Messagebox";
}

// The clamped palette: (dword_11BC2D0 & 0xC7), overridden to 464838 when flag 0x08.
i32 ResolvePalette(int flags) {
    i32 palette = g_msgPalette & kMsgPaletteMask; // LOBYTE(v20) = dword_11BC2D0 & 0xC7
    if (flags & kMsgFlagPalette8)                 // (a1 & 8) -> v20 = 464838
        palette = kMsgPaletteOverride;
    return palette;
}

// ---------------------------------------------------------------------------
// Shared modal core for ShowMessageBox / ShowMessageBoxBig.
//   * trackResult = true  : OK writes (group selection + 1); Cancel writes 0.
//   * sliderInsetY        : winH inset for the optional progress slider (24).
// Mirrors the decompiles at 0x4ad6f0 / 0x4acbd0 exactly (same body; the only
// machine-level difference is the guard byte and string copies).
// ---------------------------------------------------------------------------
int RunModalCore(MessageBoxHost& host, u8& guard, i16 kindFlags, char buttonGroupArg,
                 int textArg, bool trackResult, int sliderInsetY) {
    const int flags = static_cast<u16>(kindFlags); // a1 used both signed (<0) and as bits
    (void)buttonGroupArg;  // a2 -> RadioGroup_Create seed; Dialog_BuildButtonGroup fills it

    int result = 0;        // v21 / v20 — returned value (0 unless OK overwrites it)
    int sliderIdx = -1;    // v18 / v22 — slider widget index (-1 = none)

    if (guard)             // already showing this kind of box -> bail (return 0)
        return 0;

    const i32 palette = ResolvePalette(flags);

    bool selectWindow1 = false;
    const char* formName = SelectMessageForm(flags, selectWindow1);
    const int formId = host.LoadForm(formName, selectWindow1);

    host.RenderText(formId, textArg);     // VIBE_Text_RenderRichString
    host.CenterWindows(formId);           // VIBE_Form_CenterChildWindows

    guard = 1;                            // set the reentrancy guard

    // Build the radio group from the current window's non-'@' children, then select 0.
    const int group = Dialog_BuildButtonGroup(g_currentWindowId); // VIBE_RadioGroup_Create + AddButton
    Selection_Update(group, 0);

    // flag 0x80 (a1 < 0 as int16): add the progress slider.
    if (kindFlags < 0)
        sliderIdx = host.AddSlider(formId, sliderInsetY);

    // ttl = startTick + 500 (the v12 = v17 + 500 used by the auto-close test).
    // host.RunFrame supplies the per-frame tick; we replay the start tick via the
    // first frame's tick (the decompile reads dword_62EB38 at entry).
    unsigned startTick = 0;
    bool haveStart = false;

    MessageBoxHost::FrameInput in{};
    while (host.RunFrame(formId, palette, kMsgTtlSpan, in)) {
        if (!haveStart) { startTick = in.tick; haveStart = true; }
        const unsigned ttl = startTick + kMsgTtlSpan;

        bool close = false;

        if (flags & kMsgFlagRaise)        // flag 0x40: keep the box raised each frame
            host.RaiseWindows(formId);

        // OK pressed on THIS box's window (dword_75BF38 == 1210 && dword_75BF08 == curWin):
        if (in.clickedId == kIdOk && in.clickedWindow == g_currentWindowId) {
            if (trackResult)
                result = g_radioGroups[group].selected + 1; // dword_676588[group] + 1
            close = true;
        }
        // Cancel on this window (1155), or Esc (byte_67225C == 1): result 0.
        else if ((in.clickedId == kIdCancel && in.clickedWindow == g_currentWindowId) ||
                 in.escKey) {
            if (trackResult)
                result = 0;
            close = true;
        }

        if (in.rightClick)                // dword_672230: right-click also closes
            close = true;

        if (kindFlags < 0 && sliderIdx != -1) // progress slider: feed elapsed of 500
            host.SetSliderValue(sliderIdx, static_cast<int>(in.tick - startTick),
                                kMsgSliderRange);

        if ((flags & kMsgFlagAutoClose) && ttl < in.tick) // flag 0x04: auto-close on timeout
            close = true;

        if (close)
            break;
    }

    guard = 0;                            // clear the guard
    host.DestroyForm(formId);             // free radio surface + Form_Destroy
    return result;
}

} // namespace

// ===========================================================================
// gilde.exe 0x4ad6f0 — VIBE_Dialog_ShowMessageBox.
// ===========================================================================
int MessageBox_Show(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg) {
    return RunModalCore(host, g_msgGuardNormal, kindFlags, buttonGroupArg, textArg,
                        /*trackResult=*/true, kMsgSliderInsetY);
}

// ===========================================================================
// gilde.exe 0x4acbd0 — VIBE_Dialog_ShowMessageBoxBig (own guard byte_631600).
// ===========================================================================
int MessageBox_ShowBig(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg) {
    return RunModalCore(host, g_msgGuardBig, kindFlags, buttonGroupArg, textArg,
                        /*trackResult=*/true, kMsgSliderInsetY);
}

// ===========================================================================
// gilde.exe 0x569a30 — VIBE_Dialog_RunMessageBox (own guard byte_63D730).
// Faithful quirk: the OK/Cancel branches in this variant only set dword_631614
// (close), they never write the result local (v3 stays 0).  So the click on this
// box's window is NOT gated by window-id, and the function always returns 0.
// ===========================================================================
int MessageBox_Run(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg) {
    const int flags = static_cast<u16>(kindFlags);
    (void)buttonGroupArg; // a2 -> RadioGroup_Create seed

    if (g_msgGuardRun)
        return 0;

    const i32 palette = ResolvePalette(flags);

    bool selectWindow1 = false;
    const char* formName = SelectMessageForm(flags, selectWindow1);
    const int formId = host.LoadForm(formName, selectWindow1);

    host.RenderText(formId, textArg);
    host.CenterWindows(formId);

    g_msgGuardRun = 1;

    const int group = Dialog_BuildButtonGroup(g_currentWindowId);
    (void)group; // RunMessageBox does NOT call Selection_Update (matches the decompile)

    unsigned startTick = 0;
    bool haveStart = false;

    MessageBoxHost::FrameInput in{};
    while (host.RunFrame(formId, palette, kMsgTtlSpan, in)) {
        if (!haveStart) { startTick = in.tick; haveStart = true; }
        const unsigned ttl = startTick + kMsgTtlSpan;

        bool close = false;

        if (flags & kMsgFlagRaise)
            host.RaiseWindows(formId);

        // No window-id gate here, and no result write (v3 == 0 throughout).
        if (in.clickedId == kIdOk || in.clickedId == kIdCancel)
            close = true;
        if (in.rightClick)
            close = true;
        if ((flags & kMsgFlagAutoClose) && ttl < in.tick)
            close = true;

        if (close)
            break;
    }

    g_msgGuardRun = 0;
    host.DestroyForm(formId);
    return 0; // v3 — always 0
}

// ===========================================================================
// gilde.exe 0x4adc60 — VIBE_Dialog_ShowMessageBoxModeless (own guard byte_631DAA).
// The frame loop ttl argument is a fixed 1 (RunFrameLoop(palette, 1, form)) and the
// slider uses winH-32.  Returns 0 (v3 never written by the click branches).
// ===========================================================================
int MessageBox_ShowModeless(MessageBoxHost& host, char kindFlags, char buttonGroupArg,
                            int textArg) {
    const int flags = static_cast<u8>(kindFlags);
    const i16 wideFlags = static_cast<i16>(static_cast<signed char>(kindFlags)); // a1<0 sign
    (void)buttonGroupArg; // a2 -> RadioGroup_Create seed

    if (g_msgGuardModeless)
        return 0;

    const i32 palette = ResolvePalette(flags);

    // Modeless does NOT take the NONE_PERGA branch (its form-select has no 0x100 case).
    const char* formName;
    if (flags & kMsgFlagAutosize)      formName = "misc\\Messagebox_Autosize";
    else if (flags & kMsgFlagBig)      formName = "misc\\Messagebox_BIG";
    else if (flags & kMsgFlagVeryBig)  formName = "misc\\Messagebox_VERY_BIG";
    else                               formName = "misc\\Messagebox";
    const int formId = host.LoadForm(formName, /*selectWindow1=*/false);

    host.RenderText(formId, textArg);
    host.CenterWindows(formId);

    g_msgGuardModeless = 1;

    const int group = Dialog_BuildButtonGroup(g_currentWindowId);
    Selection_Update(group, 0);

    int sliderIdx = -1;
    if (wideFlags < 0)
        sliderIdx = host.AddSlider(formId, kMsgSliderInsetYModeless);

    unsigned startTick = 0;
    bool haveStart = false;

    MessageBoxHost::FrameInput in{};
    // ttl fixed at 1 in the original; auto-close compares the start-tick window.
    while (host.RunFrame(formId, palette, /*ttl=*/1, in)) {
        if (!haveStart) { startTick = in.tick; haveStart = true; }

        bool close = false;

        if (flags & kMsgFlagRaise)
            host.RaiseWindows(formId);

        if (in.clickedId == kIdOk || in.clickedId == kIdCancel)
            close = true;
        if (in.rightClick)
            close = true;
        if (wideFlags < 0 && sliderIdx != -1)
            host.SetSliderValue(sliderIdx, static_cast<int>(in.tick - startTick),
                                kMsgSliderRange);
        // flag 0x04: auto-close once the live tick passes the ttl (start + 500 here).
        if ((flags & kMsgFlagAutoClose) && (startTick + kMsgTtlSpan) < in.tick)
            close = true;

        if (close)
            break;
    }

    g_msgGuardModeless = 0;
    host.DestroyForm(formId);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4ad9dc — VIBE_Dialog_ShowMessageBoxGreen (own guard byte_631DA9).
// Fixed "misc\MessageBoxGreen" form.  Renders an optional header (window 0) then the
// body into window slot 2, recolors via Hud_SyncWindowColors, and (unlike Run/Modeless)
// DOES track the result like ShowMessage — but its OK branch is NOT window-id gated.
// ===========================================================================
int MessageBox_ShowGreen(MessageBoxHost& host, int bodyText, char kindFlags,
                         char buttonGroupArg, int headerText) {
    const int flags = static_cast<u8>(kindFlags);
    const i16 wideFlags = static_cast<i16>(static_cast<signed char>(kindFlags));
    (void)buttonGroupArg; // a3 -> RadioGroup_Create seed

    int result = 0;     // v20
    int sliderIdx = -1; // v22

    if (g_msgGuardGreen)
        return 0;

    i32 palette = g_msgPalette & kMsgPaletteMask;
    if (flags & kMsgFlagPalette8)
        palette = kMsgPaletteOverride;

    const int formId = host.LoadForm("misc\\MessageBoxGreen", /*selectWindow1=*/false);

    // Form_SelectWindow(form, 0); SyncWindowColors(currentWindow); then header into the
    // default window if present, body into window slot 2.
    host.SyncWindowColors(formId);
    if (headerText)                       // if (v9) RenderRichString(v9)
        host.RenderText(formId, headerText);
    host.RenderBodyText(formId, bodyText); // SelectWindow(form,2); RenderRichString(body)
    host.CenterWindows(formId);

    g_msgGuardGreen = 1;

    const int group = Dialog_BuildButtonGroup(g_currentWindowId);
    Selection_Update(group, 0);

    if (wideFlags < 0)
        sliderIdx = host.AddSlider(formId, kMsgSliderInsetY);

    unsigned startTick = 0;
    bool haveStart = false;

    MessageBoxHost::FrameInput in{};
    while (host.RunFrame(formId, palette, kMsgTtlSpan, in)) {
        if (!haveStart) { startTick = in.tick; haveStart = true; }
        const unsigned ttl = startTick + kMsgTtlSpan;

        bool close = false;

        if (flags & kMsgFlagRaise)
            host.RaiseWindows(formId);

        // Green's OK/Cancel branches do NOT compare the window id (no v19 gate).
        if (in.clickedId == kIdOk) {
            result = g_radioGroups[group].selected + 1; // dword_676588[group] + 1
            close = true;
        } else if (in.clickedId == kIdCancel || in.escKey) {
            result = 0;
            close = true;
        }

        if (in.rightClick)
            close = true;
        if (wideFlags < 0 && sliderIdx != -1)
            host.SetSliderValue(sliderIdx, static_cast<int>(in.tick - startTick),
                                kMsgSliderRange);
        if ((flags & kMsgFlagAutoClose) && ttl < in.tick)
            close = true;

        if (close)
            break;
    }

    g_msgGuardGreen = 0;
    host.DestroyForm(formId);
    return result;
}

} // namespace guild::gui
