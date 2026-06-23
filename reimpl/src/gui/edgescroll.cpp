// ===========================================================================
// guild::gui — map drag-scroll state machine + HUD edge-scroll re-attach, 1:1.
// See edgescroll.h for the provenance manifest and per-function decompile notes.
// ===========================================================================
#include "gui/edgescroll.h"
#include "util/coord.h"   // ConvertX (truncate toward zero)

namespace guild::gui {

// ===========================================================================
// 0x543994 — VIBE_MapView_UpdateScrollState.
//
// Disasm-faithful three-block state machine. The deltas in the held-drag block go
// through VIBE_Coord_ConvertX (fild -> ConvertX -> fistp): ConvertX truncates st0
// toward zero, but the inputs are integer-valued (a difference of two >>16 ints), so
// the truncation is the identity here. We keep the ConvertX call in the trace so the
// fidelity is explicit and a non-integer input would still truncate identically.
// ===========================================================================
int MapView_UpdateScrollState(MapScrollState& s, int pressFlag,
                              int cursorX16, int cursorY16,
                              int dragOriginX16, int dragOriginY16) {
    int changed = 0;   // edx, returned in eax on the held path

    // ---- Block 1: press latch (0x5439ad) ----
    if (pressFlag && !s.latched) {
        s.anchorX = cursorX16 >> 16;       // dword_63D4F4 = unk_67220E >> 16
        s.anchorY = cursorY16 >> 16;       // dword_63D4F8 = dword_672210 >> 16
        s.snapOffX = s.offX;               // dword_12334E8 = *(a1+600)
        s.snapOffY = s.offY;               // dword_12334EC = *(a1+584)
        s.snapBandHiX = s.bandHiX;         // dword_12334D8 = dword_62D0C4
        s.snapBandHiY = s.bandHiY;         // dword_12334DC = dword_62D0C8
        s.snapBandLoX = s.bandLoX;         // dword_12334E0 = dword_62D0CC
        s.snapBandLoY = s.bandLoY;         // dword_12334E4 = dword_62D0D0
        s.bandLoX = s.anchorX - s.snapOffX;                       // dword_62D0CC
        s.bandHiX = s.worldW - kMapViewWidth + s.anchorX - s.snapOffX;  // 62D0C4
        s.bandLoY = s.anchorY - s.snapOffY;                       // dword_62D0D0
        s.bandArmed = 0;                                          // dword_62D0D4 = 0
        s.latched = 1;                                            // dword_63D4FC = 1
        s.bandHiY = s.worldH - kMapViewHeight + s.anchorY - s.snapOffY; // 62D0C8
    }

    // ---- Block 2: release restore (0x543b4c) ----
    if (!pressFlag && s.latched) {
        s.bandHiX = s.snapBandHiX;     // dword_62D0C4 = dword_12334D8
        s.bandHiY = s.snapBandHiY;     // dword_62D0C8 = dword_12334DC
        s.bandLoX = s.snapBandLoX;     // dword_62D0CC = dword_12334E0
        s.bandLoY = s.snapBandLoY;     // dword_62D0D0 = dword_12334E4
        s.anchorX = pressFlag;         // dword_63D4F4 = dword_672238 (== 0 here)
        s.bandArmed = 1;               // dword_62D0D4 = 1
        s.anchorY = pressFlag;         // dword_63D4F8 = dword_672238 (== 0 here)
        // VIBE_Coord_ConvertY(...) — a coord-trunc shim on cursor scratch; no state
        // effect modeled (its result is discarded by the caller). [0x543b9b]
        s.latched = 0;                 // dword_63D4FC = 0
        return 0;                      // eax = 0
    }

    // ---- Block 3: held drag (0x543abb) ----
    if (pressFlag) {
        // deltaX = trunc((dragOriginX>>16) - anchorX), deltaY likewise.
        double dx = (double)((dragOriginX16 >> 16) - s.anchorX);   // dword_672170>>16
        double dy = (double)((dragOriginY16 >> 16) - s.anchorY);   // dword_672174>>16
        int deltaX = (int)guild::util::ConvertX(dx);   // fild->ConvertX->fistp
        int deltaY = (int)guild::util::ConvertX(dy);
        int newOffX = s.snapOffX + deltaX;             // dword_12334E8 + v12
        int newOffY = s.snapOffY + deltaY;             // dword_12334EC + v11
        if (newOffX != s.offX || newOffY != s.offY)
            changed = 1;
        s.offX = newOffX;     // *(a1+600)
        s.offY = newOffY;     // *(a1+584)
    }
    return changed;
}

// ===========================================================================
// 0x4bc07c — VIBE_Hud_UpdateEdgeScroll.
//
// The decompile's control flow distilled to its observable effects:
//   if (detached):
//      if (settled): free anim; clear detached/detach2; repose node; goto LABEL_3;
//      else:          free anim; clear detached/detach2;   (falls through)
//   if (!<commit> && detached): if (selectionLive) reset; return;
//   LABEL_3 / settled-on-attached path: update listener; (clear an aux node hook);
//      if (selectionLive) reset.
//
// The non-detached branch (the second VectorWithinTolerance pair at 0x4bc25c) also
// early-returns when the node already matches the target. Here `settled` carries the
// host's tolerance result; the listener update + selection reset happen on commit,
// and the detach flags are cleared (the RE-ATTACH). Returns the resulting detached
// flag for pinning.
// ===========================================================================
int Hud_UpdateEdgeScroll(EdgeScrollState& s, const EdgeScrollHooks& h) {
    if (s.detached) {
        // dword_62D4E8 set: an edge-snap is in progress.
        if (h.freeObjAnim) h.freeObjAnim();          // Anim_FreeObjAnimData (both paths)
        s.detached = 0;                               // dword_62D4E8 = 0  (re-attach)
        s.detach2  = 0;                               // dword_62D4E4 = 0
        if (s.settled) {
            // Settled: write the target pose back onto the node, update listener.
            if (h.setNodePose) h.setNodePose();       // SetPosition + SetWorldTranslation
        }
        if (h.updateListener) h.updateListener();     // Sound3d_UpdateListener_b5d5c
        if (s.selectionLive && h.selectionReset) h.selectionReset();  // Selection_Reset
        return s.detached;
    }

    // Not detached: if the node already matches the target, only the selection-reset
    // tail runs; otherwise update the listener (the snap completed last frame).
    if (s.settled) {
        if (s.selectionLive && h.selectionReset) h.selectionReset();
        return s.detached;
    }
    if (h.updateListener) h.updateListener();
    if (s.selectionLive && h.selectionReset) h.selectionReset();
    return s.detached;
}

} // namespace guild::gui
