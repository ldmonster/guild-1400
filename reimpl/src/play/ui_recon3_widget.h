#pragma once
// ui_recon3 — UI primitive cluster (VIBE_Widget_/Form_/Dialog_/Panel_/Window_).
//
// The bulk of these five families was already reconstructed under src/gui/ (window
// layout, scroll clamping, form lifecycle, dialog checks, panel run-loops, etc.).
// A cluster-wide sweep (every VIBE_Widget_*/Form_*/Dialog_*/Panel_*/Window_* symbol
// in gilde.exe) found exactly ONE family member with no existing reconstruction:
//
//   gilde.exe 0x410178 — VIBE_Widget_Free_Thunk
//
// It is a pure 2-instruction tail-call wrapper:
//     0x410178  call VIBE_Widget_DestroyByType   ; 0x414f98
//     0x41017d  retn
// i.e. a __thiscall/__usercall-agnostic trampoline that forwards its (untouched)
// register + stack arguments straight into VIBE_Widget_DestroyByType and returns
// its result. The original ships this thunk so VIBE_TradeTransport_PanelDispatcher
// (0x54014c) can take its address as a uniform teardown callback at three sites
// (0x5408b5, 0x5408d3, 0x541026).
//
// VIBE_Widget_DestroyByType (@0x414f98) is itself a render/free leaf (per-type
// widget surface teardown); the data-model side is already reconstructed as
// guild::gui::Widget_DestroyByType in src/gui/form_lifecycle.{h,cpp}. We REUSE it
// here (no redefinition — ODR) and forward 1:1 through it, exactly as the binary's
// thunk forwards into 0x414f98.

#include "gui/types.h"

namespace guild::gui {

// Reused target (defined in gui/form_lifecycle.cpp). Declared there as:
//   void Widget_DestroyByType(int widgetIdx, int a2, int a3);
void Widget_DestroyByType(int widgetIdx, int a2, int a3);

} // namespace guild::gui

namespace guild::play {

// gilde.exe 0x410178 — VIBE_Widget_Free_Thunk
// Tail-call trampoline: forwards (widgetIdx, a2, a3) into VIBE_Widget_DestroyByType
// (0x414f98) unchanged and returns. The original is `call; retn` with no argument
// massaging, so the reconstruction is a straight pass-through.
int VIBE_Widget_Free_Thunk(int widgetIdx, int a2, int a3);

} // namespace guild::play
