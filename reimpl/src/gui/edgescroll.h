#pragma once
// ===========================================================================
// guild::gui — map drag-scroll state machine + HUD edge-scroll re-attach, 1:1.
//
// Two reconstructions:
//
//   0x543994  VIBE_MapView_UpdateScrollState — the MAP-VIEW drag-pan state machine.
//     A press inside the map (dword_672238 == the LMB-held flag) latches a drag:
//     it snapshots the current scroll offset + the dead-band box (dword_62D0C4..D0),
//     reprograms the dead-band so the cursor can drag the full world extent, and on
//     subsequent held frames slides the scroll offset by the cursor delta. Release
//     restores the snapshotted dead-band. Reconstructed byte-for-byte, including the
//     two VIBE_Coord_ConvertX truncations (no-ops on integer-valued deltas — verified
//     against the fild/ConvertX/fistp sequence in the disasm).
//
//   0x4bc07c  VIBE_Hud_UpdateEdgeScroll — the 3D-view edge re-attach finisher (despite
//     the name it is NOT the smooth pan). It tracks dword_62D4E8 (a "camera detached /
//     edge-snap pending" latch); when the camera node's pose reaches the snapped
//     target (within tolerance 0.1) it frees the node anim, CLEARS the detach flags
//     (dword_62D4E4/E8 -> the re-attach), updates the 3D listener and resets the
//     selection. The actual node math / listener / anim-free are coupled leaves routed
//     through hooks; the latch logic + tolerance gate are reconstructed 1:1.
//
// Provenance (get_global_value verified):
//   dword_62D0C4 = 0x316  dead-band hi-X     dword_62D0C8 = 0x257  dead-band hi-Y
//   dword_62D0CC = 0      dead-band lo-X      dword_62D0D0 = 0      dead-band lo-Y
//   dword_62D0D4 = 1      dead-band "armed"   dword_63D4FC = 0      drag-latched flag
//   512 (0x200) viewport W   360 (0x168) viewport H
//   dword_1233440/444 world bitmap w/h (runtime; g_mapWidth/g_mapHeight in gui::)
// ===========================================================================
#include "guild/common/types.h"

namespace guild::gui {

using guild::i16;
using guild::i32;

// ---------------------------------------------------------------------------
// 0x543994 — VIBE_MapView_UpdateScrollState.
//
// The dead-band box (dword_62D0C4 hiX, C8 hiY, CC loX, D0 loY, D4 armed) is shared
// engine state; the per-view scroll record holds the offset (offX at +600, offY at
// +584 in the original). The drag snapshot lives in dword_12334D8..EC and the press
// anchor in dword_63D4F4/F8.
//
// We model all of that as an explicit struct so the state machine is testable; the
// host maps the fields to the real globals/record.
// ---------------------------------------------------------------------------
inline constexpr int kMapViewWidth  = 512;   // 0x200
inline constexpr int kMapViewHeight = 360;   // 0x168

struct MapScrollState {
    // The per-view scroll record (a1 in the original): offsets at +600 / +584.
    int offX = 0;   // *(a1+600)  scrollX (left edge of viewport in world px)
    int offY = 0;   // *(a1+584)  scrollY (top  edge)

    // Shared dead-band box.
    int bandHiX  = 0x316; // dword_62D0C4
    int bandHiY  = 0x257; // dword_62D0C8
    int bandLoX  = 0;     // dword_62D0CC
    int bandLoY  = 0;     // dword_62D0D0
    int bandArmed = 1;    // dword_62D0D4

    // Drag latch + snapshot.
    int latched   = 0;    // dword_63D4FC  (1 while dragging)
    int anchorX   = 0;    // dword_63D4F4  (cursor x at press, >>16)
    int anchorY   = 0;    // dword_63D4F8  (cursor y at press, >>16)
    int snapOffX  = 0;    // dword_12334E8  (offX at press)
    int snapOffY  = 0;    // dword_12334EC  (offY at press)
    int snapBandHiX = 0;  // dword_12334D8
    int snapBandHiY = 0;  // dword_12334DC
    int snapBandLoX = 0;  // dword_12334E0
    int snapBandLoY = 0;  // dword_12334E4

    int worldW = 0;       // dword_1233440
    int worldH = 0;       // dword_1233444
};

// 0x543994 inputs: the press flag (dword_672238) and the two packed fixed-point
// cursor globals (unk_67220E x, dword_672210 y; used >>16). `cursorX0/Y0` is the
// drag-origin cursor (dword_672170 / dword_672174, also >>16) used for the per-frame
// delta. (In the original these are the same input subsystem coords; modeled
// explicitly to keep the >>16 truncation visible and pinnable.)
// Returns 1 when the offset changed this frame, else 0 (matching the original's eax).
int MapView_UpdateScrollState(MapScrollState& s, int pressFlag,
                              int cursorX16, int cursorY16,
                              int dragOriginX16, int dragOriginY16);

// ---------------------------------------------------------------------------
// 0x4bc07c — VIBE_Hud_UpdateEdgeScroll (the edge-snap re-attach finisher).
//
// State:
//   detached (dword_62D4E8) : 1 while the camera is detached for an edge-snap.
//   detach2  (dword_62D4E4) : the paired mirror cleared with it.
//   The target pose mirrors are dword_11BC2D4 (pos[3]) / dword_11BC2E4 (rot[3]); the
//   live node pose is at *(node+76) (pos) / *(node+132) (rot). The "settled" test is
//   two VectorWithinTolerance(node_pose, target, 0.1).
//
// When detached and the node has settled (or always, on the non-detached path when
// the pose matches), the function:
//   - frees the node anim (Anim_FreeObjAnimData @0x5cec00),
//   - clears detached/detach2 (the RE-ATTACH),
//   - writes the target pose back onto the node (SetPosition / SetWorldTranslation),
//   - updates the 3D listener (Sound3d_UpdateListener_b5d5c @0x4b5d5c),
//   - and if a selection is live (dword_11BC274) resets it (Selection_Reset @0x4b9444).
//
// The node/listener/anim/selection edges are coupled leaves routed through hooks; the
// latch + tolerance branch logic is reconstructed 1:1.
// ---------------------------------------------------------------------------
struct EdgeScrollState {
    int detached = 0;   // dword_62D4E8
    int detach2  = 0;   // dword_62D4E4
    int selectionLive = 0; // dword_11BC274
    // The settle test result the host supplies (VectorWithinTolerance over the node
    // pose vs the target mirrors, tolerance 0.1). `settled` == both axes within tol.
    bool settled = false;
};

struct EdgeScrollHooks {
    // VIBE_Anim_FreeObjAnimData @0x5cec00.
    void (*freeObjAnim)() = nullptr;
    // VIBE_Object_SetPosition @0x5af38c + VIBE_Object_SetWorldTranslation @0x5af50c.
    void (*setNodePose)() = nullptr;
    // VIBE_Sound3d_UpdateListener_b5d5c @0x4b5d5c.
    void (*updateListener)() = nullptr;
    // VIBE_Selection_Reset @0x4b9444.
    void (*selectionReset)() = nullptr;
};

// 0x4bc07c. Steps the edge-snap re-attach latch. Reproduces the decompile's branch
// order: if detached and settled -> free+clear+repose; else (detached, not settled)
// -> free+clear; then the selection-reset tail. Returns the resulting `detached`
// value (for test pinning).
int Hud_UpdateEdgeScroll(EdgeScrollState& s, const EdgeScrollHooks& h);

} // namespace guild::gui
