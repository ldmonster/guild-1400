#pragma once
// guild::gui — modal messagebox / generic dialog model.
//
// The messagebox family (VIBE_Dialog_ShowMessageBox @0x4ad6f0, ShowMessageBoxBig
// @0x4acbd0, RunMessageBox @0x569a30, and the Green/Modeless/Simple variants) all share
// one shape:
//   1. pick a .form resource by the kind flag bits;
//   2. render the message text, center the form's windows;
//   3. build a radio group from the form's button widgets (every child that is not the
//      window-backing widget, type '@'/0x40);
//   4. select button 0, then run a modal frame loop until OK (1210), Cancel (1155),
//      a right-click, or a timeout closes it;
//   5. return (selected button index + 1) for OK, or 0 for Cancel/right-click.
//
// The .form loader, the rich-text renderer, the optional progress slider, and the modal
// frame loop belong to the io/text/sim clusters and are deferred. The pure model the GUI
// owns — the form-name selection, the button-group build, and the result resolution — is
// reproduced here.

#include "gui/types.h"

namespace guild::gui {

// Messagebox kind flag bits (the `a1@<dx>` argument of ShowMessageBox).
inline constexpr int kMsgFlagAutosize = 0x02;  // misc\Messagebox_Autosize
inline constexpr int kMsgFlagPalette8 = 0x08;  // override palette (v20 = 464838)
inline constexpr int kMsgFlagBig      = 0x10;  // misc\Messagebox_BIG
inline constexpr int kMsgFlagVeryBig  = 0x20;  // misc\Messagebox_VERY_BIG
inline constexpr int kMsgFlagAutoClose= 0x04;  // close when the timeout elapses
inline constexpr int kMsgFlagRaise    = 0x40;  // keep the box raised each frame
inline constexpr int kMsgFlagNonePerga= 0x100; // misc\Messagebox_NONE_PERGA (+ win 1)
inline constexpr int kMsgFlagSlider   = 0x80;  // a1 < 0 (sign bit): add a progress slider

// gilde.exe 0x4ad6f0 / 0x569a30 — the .form resource name chosen by the kind flags.
// Priority: Autosize(2) > Big(0x10) > VeryBig(0x20) > NonePerga(0x100) > default.
const char* Dialog_FormForFlags(int flags);

// gilde.exe 0x4ad6f0 (button-group build loop) — build a radio group from window
// `winSlot`'s children, adding every child whose widget type is not '@' (0x40, the
// window-backing widget). Returns the created radio-group index (or -1 on failure).
int Dialog_BuildButtonGroup(int winSlot);

// gilde.exe 0x4ad6f0 (result resolution) — map the modal exit condition to a return
// value: OK (clickedId == 1210) -> selectedIndex + 1; Cancel (1155) or right-click -> 0.
// Returns -1 when neither end condition holds (loop continues).
int Dialog_ResolveResult(int clickedId, int selectedIndex, bool rightClick);

} // namespace guild::gui
