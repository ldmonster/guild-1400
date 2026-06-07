#pragma once
// guild::gui — tooltip lifecycle dispatcher (the per-frame state machine).
//
// VIBE_Tooltip_DispatchByType @0x4f7424 runs once per frame. tooltip.{h,cpp} already
// reproduces its *classification* core (which scene-table range the hovered widget's
// +736 reference falls into → object/building/upgrade/person/contact). This module
// reproduces the surrounding *lifecycle* the GUI owns: the show/hide bookkeeping around
// the single tooltip form handle (dword_633908) — when to destroy the current tooltip,
// when to (re)build it, and the visibility flags it toggles.
//
// The builders themselves (VIBE_Tooltip_Build*) assemble a .form from the world/economy
// databases and are deferred to those clusters; here they are represented by the
// TooltipKind the dispatcher decided to build, recorded through a sink so the control
// flow can be exercised in isolation.

#include "gui/tooltip.h"

namespace guild::gui {

// Mutable runtime state the dispatcher reads and writes. Each field maps to one of the
// dword_6xxxxx globals the original threads through VIBE_Tooltip_DispatchByType. Pointer-
// like handles use -1 ("none") exactly as the binary does.
struct TooltipDispatchState {
    // --- the hovered-widget / scene inputs (set by other code each frame) ---
    int  hoveredTooltipId = -1;   // dword_75BF3C : id of the widget under the cursor (-1 none)
    int  scenePickActive  = 0;    // dword_672238 : a 3D scene pick is active this frame
    int  contactReqId     = -1;   // dword_631724 : pending contact-tooltip request id (0 none)
    void* contactReqPtr   = nullptr; // dword_63172C : pending contact record ptr (its first word=0 cancels)
    int  hoverWidgetIndex = 0;    // dword_62D22C : index of the widget whose +736 ref is read
    int  hoverSceneRefRaw = 0;    // dword_75BF46 high words: last scene coord (for ConvertY)

    // --- the live tooltip the dispatcher manages ---
    int  formHandle    = -1;      // dword_633908 : current tooltip form (-1 = none shown)
    int  formAnchorId  = -1;      // dword_633910 : the form's anchor object id
    int  shownForId    = -1;      // dword_63390C : tooltip id the current form was built for
    int  builtForReqId = 0;       // dword_633914 : contact req id the current form was built for
    int  visibleFlag   = 0;       // dword_633918 : 1 while a tooltip is being shown

    // --- output flags the original sets (mirrored for inspection) ---
    int  redrawFlag    = 0;       // dword_62D0D4 : force-redraw request
    int  raiseFlag     = 0;       // dword_62D314 : keep tooltip raised to front
};

// What VIBE_Tooltip_DispatchByType did this frame (for tests/inspection).
enum class TooltipAction {
    kNone,         // nothing changed
    kHidden,       // an existing tooltip was torn down
    kBuilt,        // a new tooltip form was built and raised
};

// Sink the dispatcher routes the runtime calls through (form destroy / build / raise).
// The default implementation records nothing useful; tests supply their own to drive and
// observe the lifecycle without the GUI runtime.
struct TooltipDispatchSink {
    virtual ~TooltipDispatchSink() = default;
    // VIBE_Form_Destroy(handle) — tear down a tooltip form.
    virtual void Destroy(int handle) { (void)handle; }
    // The relevant VIBE_Tooltip_Build*; returns the new form handle (or -1 on failure).
    // `anchorId` receives the form's anchor object id (dword_676A64[171*handle]).
    virtual int Build(TooltipKind kind, const TooltipSubject& subject, int& anchorId) {
        (void)kind; (void)subject; anchorId = 0; return -1;
    }
    // VIBE_Form_RaiseWindows(handle) — bring the shown tooltip to front.
    virtual void Raise(int handle) { (void)handle; }
};

// gilde.exe 0x4f7424 — one dispatch pass. `tables`/`sceneRef` feed the classification
// (Tooltip_ClassifySubject) exactly as tooltip.cpp models it; `s` is mutated state.
// Returns what happened this frame.
TooltipAction Tooltip_Dispatch(TooltipDispatchState& s, TooltipDispatchSink& sink,
                               const TooltipTables& tables, const u8* sceneRef);

} // namespace guild::gui
