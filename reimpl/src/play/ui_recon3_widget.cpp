// ui_recon3 — VIBE_Widget_Free_Thunk reconstruction.
//
// gilde.exe 0x410178 — VIBE_Widget_Free_Thunk  (thunk: call 0x414f98 ; retn)
//
// See ui_recon3_widget.h for the cluster-sweep rationale (this is the only
// member of the Widget/Form/Dialog/Panel/Window families not already present
// elsewhere in src/). Pure pass-through into the existing
// guild::gui::Widget_DestroyByType.

#include "play/ui_recon3_widget.h"

namespace guild::play {

// gilde.exe 0x410178 — VIBE_Widget_Free_Thunk
//   0x410178  call VIBE_Widget_DestroyByType   ; 0x414f98
//   0x41017d  retn
// The thunk performs no argument transformation: the caller's arguments flow
// straight through to VIBE_Widget_DestroyByType and its return value is returned
// verbatim. VIBE_Widget_DestroyByType's reconstructed data-model entry point
// (guild::gui::Widget_DestroyByType) returns void in this build, so the thunk's
// `int` result is the (caller-ignored) eax left after the tail call; we mirror the
// observable side effects by forwarding and returning 0, matching the binary's
// behavior at the three call sites in VIBE_TradeTransport_PanelDispatcher (0x54014c)
// which discard the result.
int VIBE_Widget_Free_Thunk(int widgetIdx, int a2, int a3) {
    guild::gui::Widget_DestroyByType(widgetIdx, a2, a3); /*0x410178 -> 0x414f98*/
    return 0;                                            /*0x41017d retn*/
}

} // namespace guild::play
