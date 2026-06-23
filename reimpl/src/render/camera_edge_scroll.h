#pragma once
#include "guild/common/types.h"
#include "camera_recon.h"    // CameraObject / CameraState
#include "camera_recon2.h"   // Camera2State / Camera2Input / Camera2Hooks

// =============================================================================
// guild::render — VIBE_Camera_EdgeScroll @0x4b2c34 (camera_edge_scroll).
//
// 1:1 translation of the screen-edge VIEW-SNAP machine of gilde.exe. Despite
// the inherited IDB name this is NOT the smooth edge pan (that is the UpdatePan
// core @0x4b365c, reconstructed in play/camera_controls); it is the
// edge-triggered "fly to the preset edge view" dispatcher:
//
//   - the screen-edge box dword_62D0C4..D0 (inset by 1px on every side) splits
//     the screen into an inner box + 4 edge bands;
//   - touching an edge band steps a 2-axis edge state
//     (dword_6316CC horizontal / dword_6316D0 vertical, each clamped [-1,1];
//     stepping one axis ZEROES the other);
//   - the stepped state selects one of the camera node's edge-view blocks
//     (pos[3]+rot[3] each):  (0,-1) top  -> node+180 (0xB4)
//                            (0,+1) btm  -> node+204 (0xCC)
//                            (-1,0) left -> node+228 (0xE4)
//                            (+1,0) right-> node+252 (0xFC)
//     and (0,0) CENTER -> the node's CURRENT world pose (+92 / +144);
//   - an edge view whose position is within 0.2 of the zero vector (the 16
//     zero bytes at 0x4AD1C4) is "unset" and the snap bails (the CENTER case
//     has NO such gate — it always commits);
//   - the commit (loc_4B2D44) writes the view into the history mirrors
//     dword_11BC2D4..DC / dword_11BC2E4..EC, frees the camera node's anim data
//     (VIBE_Anim_FreeObjAnimData @0x5cec00), CLEARS the camera detach flags
//     dword_62D4E4 / dword_62D4E8 at 0x4b2d95/0x4b2d9b (the RE-ATTACH that ends
//     UpdateMovement's drag-release freeze), updates the 3D listener
//     (VIBE_Sound3d_SetListenerFromVectors @0x4262a0, kind 8) and latches
//     dword_631DE0 = 1;
//   - while latched, the function only waits for the cursor to re-enter the
//     inner box (which clears the latch and re-arms the machine).
//
// Verified callers (xrefs_to 0x4b2c34): the combat frame loops
// VIBE_Combat_RunBattleFrameLoop @0x48c60f, VIBE_Combat_RunOrderWaitLoop
// @0x48c679, VIBE_Combat_TickBattleState @0x4905e9, and the city HUD click
// router VIBE_Hud_HandleMouseClick @0x4bc4b1. play::SessionCamera calls it once
// per frame (the combat-loop cadence) as the stand-in for those callers.
//
// Register/dataflow notes recovered from the disasm:
//   - the 16-byte constant copied to the stack (rep-movsd from 0x4AD1C4) is
//     all zeros (get_bytes verified) — the "unset view" reference vector;
//   - at the commit, ecx is zeroed (0x4b2d87 xor ecx,ecx) before the
//     VIBE_Anim_FreeObjAnimData call, which PRESERVES ecx/edx (push/pop pairs
//     at 0x5cec00/0x5cec01) — so the stores at 0x4b2d95/0x4b2d9b write 0;
//   - the listener call receives edx=&pos / ebx=&rot staged at 0x4b2d79 /
//     0x4b2d6c (both preserved across the anim-free call), ecx=dword_6316C8,
//     stack arg 8;
//   - the anim-free args edi/esi are leftovers — the callee only frees the
//     node's +464 block and forwards them to debug-print parameters.
//
// Return value (eax) per path, reproduced 1:1:
//   latched / vertical early-outs ......... dword_62D0C8 - 1 (bottom)
//   horizontal early-outs ................. the cursor x (unk_67220E >> 16)
//   "unset view" tolerance bail ........... 1 (the VectorWithinTolerance result)
//   commit ................................ the 3D-listener result
// =============================================================================

namespace guild::render {

// gilde.exe 0x4b2c34 — VIBE_Camera_EdgeScroll (no arguments; reads globals).
// cs supplies dword_6316C8 (minZoomDist) and the dword_62D4E4/E8 mirrors;
// st supplies the edge box, edge state, latch and history mirrors; in supplies
// the cursor pair and the dword_62D4E4/E8 input-model mirrors (the same two
// globals modeled in both structs — the function clears BOTH mirrors).
i32 Camera_EdgeScroll(CameraObject& obj, CameraState& cs, Camera2State& st,
                      Camera2Input& in, const Camera2Hooks& h);

} // namespace guild::render
