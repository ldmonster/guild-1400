#pragma once
#include "guild/common/types.h"
#include "camera_recon.h"    // CameraObject / CameraState / CameraInput / CameraHooks
#include "camera_recon2.h"   // Camera2State / Camera2Input / Camera2Hooks

// =============================================================================
// guild::render — VIBE_Camera_Update reconstruction (camera_update_recon).
//
// 1:1 translation of the per-frame camera DISPATCHER of gilde.exe:
//
//   0x4b4c68  VIBE_Camera_Update            (per-frame camera dispatch)
//   0x40da48  VIBE_Coord_ConvertY           (cursor coord write — see below)
//
// VIBE_Camera_Update is the function the live frame loop calls every frame to
// drive the city camera. Verified callers (xrefs_to 0x4b4c68):
//   0x50f1ea  VIBE_Scene_RunMainFrameLoop          (the per-frame call)
//   0x50da2e  VIBE_Building_LoadAndAlignGebaeudeModel
//
// Calling convention (recovered from disasm at 0x4b4c68 + both call sites):
// Hex-Rays shows `__thiscall(void* this)` because ecx flows into the
// VIBE_Camera_ClampToTerrainHeight call at 0x4b4d25 — but the function never
// LOADS ecx, and neither call site sets it: ecx is an indeterminate leftover
// register at both live call sites (same situation as the AnchorToTerrain
// first arg inside UpdateMovement, see camera_recon2). ClampToTerrainHeight
// only uses it as the terrain-sample x base, which is inert with the default
// terrainHeight hook. We surface it as the explicit `thisXLeftover` argument
// (pass 0 at the reconstructed call sites).
//
// Dispatch flow (control flow follows the disasm exactly):
//   if (!dword_13FCD1C || dword_62EB4C) {            // no camera / modal gate
//       dword_631628 = 0;
//       if (!dword_62D0D4) {                          // edge box not yet set up
//           dword_62D0C4 = (dword_69FFBC >> 16) - 8;  // right edge  = width-8
//           dword_62D0C8 = *(int*)(0x69FFBA) >> 16;   // bottom edge (overlapping
//                                                     //  dword read at 69FFB8+2)
//           dword_62D0CC = 0;                         // left edge
//           dword_62D0D4 = 1;                         // box initialized
//           dword_62D0D0 = 0;                         // top edge
//           VIBE_Coord_ConvertY(eax = unk_67220E>>16, // cursor write (x, x)
//                               dx  = low16 of it);
//       }
//       return 0;
//   }
//   if (!dword_11BC24C && dword_633908 == -1) {
//       if (!dword_672238)         dword_631628 = VIBE_Camera_UpdatePan();
//       if (!(dword_11BC2D0 & 0x8000)) VIBE_Camera_UpdateMovement();
//       if (byte_6316D8 && !byte_671D6F) VIBE_Camera_ClampToTerrainHeight(ecx);
//       if (dword_631628 && dword_631610 == dword_631618
//           && !dword_631744 && !dword_631748) {
//           rec = VIBE_Person_GetFamilyRecord(&word_12CE910[268*word_63CC5C]);
//           if (rec) { rec[33..35]=pos; rec[37..39]=world; rec[32]=dword_6316E0; }
//       }
//       dword_11BC2E4..EC = obj+132..140;   // world mirror
//       dword_11BC2D4..DC = obj+76..84;     // pos mirror
//   }
//   return dword_631628;
//
// Callee routing (rule 13 — wire what exists, hook what doesn't):
//   0x4b41a8 VIBE_Camera_UpdateMovement      -> Camera_UpdateMovement (camera_recon2,
//                                               called DIRECTLY)
//   0x4b2a0c VIBE_Camera_ClampToTerrainHeight-> Camera_ClampToTerrainHeight
//                                               (camera_recon, called DIRECTLY)
//   0x4b365c VIBE_Camera_UpdatePan           -> CameraUpdateHooks::updatePan.
//       The pan core IS reconstructed (play::CameraUpdatePan, camera_controls)
//       but lives in a different state model (CameraControl); the render layer
//       cannot depend on src/play, so the dispatcher routes it through a hook.
//       Inert default: return 0 (no pan this frame).
//   0x58c408 VIBE_Person_GetFamilyRecord     -> Camera2Hooks::personGetFamilyRecord
//                                               (inert default: nullptr).
//   0x40da48 VIBE_Coord_ConvertY             -> Camera_CursorCoordWrite below.
//
// NOTE on 0x40da48: despite the inherited IDB name "Coord_ConvertY", the
// decompile shows it is a CURSOR COORDINATE WRITE:
//   LOWORD/HIWORD(dword_672174) = (x, y);
//   LOWORD/HIWORD(dword_6721C4) = (x, y);
//   LOWORD/HIWORD(dword_672210) = (x, y);
//   if (!dword_62D0D4) return SetCursorPos(x, y);
//   return x;
// (Win32 SetCursorPos -> CameraUpdateHooks::setCursorPos per rule 4; inert
// default: no-op. dword_6721C4 has no consumer in the reconstructed tree and
// is not modeled.) The "trunc-toward-zero" reading used inside UpdateMovement
// (camera_recon2) belongs to VIBE_Coord_ConvertX @0x5c6b08; the 0x40da48 call
// in THIS function is the cursor write above, translated 1:1 here.
// =============================================================================

namespace guild::render {

// ---------------------------------------------------------------------------
// CameraUpdateInput — read-only process globals the dispatcher gates on
// (disjoint from CameraInput / Camera2Input; addresses per field).
// ---------------------------------------------------------------------------
struct CameraUpdateInput {
    i32 d62EB4C  = 0;    // dword_62EB4C  — modal/UI gate (set by MainWndProc,
                         //                 event panels, message boxes)
    i32 d11BC24C = 0;    // dword_11BC24C — camera-input freeze gate
    i32 d633908  = -1;   // dword_633908  — must be -1 (no active override)
    u32 d11BC2D0 = 0;    // dword_11BC2D0 — bit 0x8000 freezes UpdateMovement
    u8  byte6316D8 = 0;  // byte_6316D8   — terrain height clamp enabled
    // dword_69FFBC — packed screen extents the edge box is built from:
    //   right edge  = ((i32)d69FFBC >> 16) - 8        (HIWORD = width)
    //   bottom edge = (i32)(i16)(d69FFBC & 0xFFFF)    (LOWORD = height; the
    //   original reads it via the overlapping dword at 0x69FFBA >> 16)
    i32 d69FFBC = 0;
};

// ---------------------------------------------------------------------------
// CameraUpdateState — mutable process globals owned by the dispatcher.
// ---------------------------------------------------------------------------
struct CameraUpdateState {
    // dword_631628 — UpdatePan's "camera panned" result. PERSISTS across calls:
    // the gated main path returns it unchanged; only the no-camera/modal early
    // path zeroes it.
    i32 panResult = 0;
};

// ---------------------------------------------------------------------------
// CameraUpdateHooks — leaves routed per the table above. Inert by default.
// ---------------------------------------------------------------------------
struct CameraUpdateHooks {
    // 0x4b365c VIBE_Camera_UpdatePan -> int (nonzero == camera moved).
    // Inert default: return 0.
    i32 (*updatePan)(void* user) = nullptr;
    void* updatePanUser = nullptr;
    // Win32 SetCursorPos(x, y) inside 0x40da48 (rule 4: Win32 -> SDL at the
    // shim boundary; the adapter may wire it). Inert default: no-op.
    void (*setCursorPos)(i32 x, i32 y) = nullptr;
};

CameraUpdateHooks CameraUpdate_DefaultHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x40da48 — VIBE_Coord_ConvertY (cursor coordinate write).
// Writes the packed cursor mirrors; calls SetCursorPos when the edge box is
// not yet initialized (dword_62D0D4 == 0). Updates BOTH input models (in1/in2
// mirror the same dword_672174/672210/unk_67220E globals). Returns x (the
// original returns eax unchanged, or the SetCursorPos result).
// ---------------------------------------------------------------------------
i32 Camera_CursorCoordWrite(i32 x, i32 y, const Camera2State& st2,
                            Camera2Input& in2, CameraInput& in1,
                            const CameraUpdateHooks& uh);

// ---------------------------------------------------------------------------
// gilde.exe 0x4b4c68 — VIBE_Camera_Update (per-frame camera dispatcher).
// Returns dword_631628 (0 on the early path). `thisXLeftover` models the
// leftover ecx forwarded to ClampToTerrainHeight (see header comment; pass 0).
// ---------------------------------------------------------------------------
i32 Camera_Update(CameraObject& obj, CameraState& cs, Camera2State& st2,
                  Camera2Input& in2, CameraInput& in1, CameraUpdateState& us,
                  const CameraUpdateInput& uin, const CameraHooks& h1,
                  const Camera2Hooks& h2, const CameraUpdateHooks& uh,
                  i32 thisXLeftover);

} // namespace guild::render
