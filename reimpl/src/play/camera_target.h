#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::play — camera world-target resolve + coord-state push (gilde.exe).
//
//   gilde.exe 0x4c0864 — VIBE_Camera_ComputeWorldTarget (__usercall fn(mask@ax))
//   gilde.exe 0x5d8ae8 — VIBE_Coord_Push                (__usercall eax=fn(
//                                                          a@eax,b@edx,d@ecx,c@ebx))
//
// ComputeWorldTarget projects the camera's focus into integer world/screen-space
// target coords (dword_69FFA4 = X-target, dword_69FFA0 = Y-target), branching on
// the camera mode flag word (`mask`, the camera struct's flag field, 16-bit):
//
//   if (mask & 0x40):                       // perspective/free camera active
//       px = flt_13FC778 * 0.5 + flt_13FCF48   // half-extent biased centre X
//       py = flt_13FC77C * 0.5 + flt_13FCD14   //                         Y
//       if (mask & 0x100):                  // offset-target sub-mode
//           A4 = trunc(py - 6.0)            //  - flt_61E514 * 1.0
//           A0 = trunc(px + 10.0)           //  + flt_61E518 * 1.0
//       else:
//           A4 = trunc(py)
//           A0 = trunc(px)
//   else:                                   // orthographic / map camera
//       A4 = dword_69FFBC >> 17  (arithmetic, == (packed >> 16) >> 1)
//       A0 = (*(int*)((char*)&dword_69FFB8 + 2)) >> 17
//
// The float->int conversions go through VIBE_Coord_ConvertX @0x5c6b08 (FRNDINT
// with RC=truncate-toward-zero) then fistp — i.e. trunc(), NOT round-to-even.
// The >> 17 path is a signed arithmetic shift of a 16.16 fixed-point coord
// (>>16 to integer, >>1 to halve), read from a packed dword pair.
//
// These read/write engine globals; this module models them as an explicit
// struct so the math is testable and the live wiring can pass the real globals
// (see CameraTargetGlobals). The "register-arg only" ConvertX side effects are
// inlined as guild::util::ConvertX (truncate).
// =============================================================================
namespace guild::play {

// Recovered float constants (verified via get_global_value):
//   flt_61E510 = 0x3F000000 = 0.5
//   flt_61E514 = 0x40C00000 = 6.0
//   flt_61E518 = 0x41200000 = 10.0
//   flt_62D224 = 0x3F800000 = 1.0
constexpr float kCamHalf      = 0.5f;  // flt_61E510
constexpr float kCamOffsetY   = 6.0f;  // flt_61E514 (subtracted from py)
constexpr float kCamOffsetX   = 10.0f; // flt_61E518 (added to px)
constexpr float kCamOne       = 1.0f;  // flt_62D224

// The engine inputs/outputs ComputeWorldTarget touches. In the live engine these
// are scattered globals; bundled here so the math is 1:1 and unit-testable.
struct CameraTargetGlobals {
    // perspective-path inputs (flt_13FC778 / flt_13FC77C / flt_13FCF48 /
    // flt_13FCD14): the camera view half-extents and the centre bias.
    float viewHalfX;   // flt_13FC778
    float viewHalfY;   // flt_13FC77C
    float centerX;     // flt_13FCF48
    float centerY;     // flt_13FCD14
    // ortho-path inputs: packed 16.16 fixed-point camera position (the dword at
    // dword_69FFBC and the misaligned read at &dword_69FFB8+2).
    i32   packedX;     // dword_69FFBC
    i32   packedY;     // *(int*)((char*)&dword_69FFB8 + 2)
    // outputs:
    i32   targetX;     // dword_69FFA4
    i32   targetY;     // dword_69FFA0
};

// gilde.exe 0x4c0864 — VIBE_Camera_ComputeWorldTarget. Updates g.targetX/targetY
// from `mask` (the camera flag word) and the input fields, exactly as the binary.
void ComputeWorldTarget(CameraTargetGlobals& g, u16 mask);

// gilde.exe 0x5d8ae8 — VIBE_Coord_Push. Stores four ints into the clip/coord
// state globals (dword_64A1B4/B8/BC/C0) and returns the first.
//   dword_64A1B8 = b; dword_64A1BC = c; dword_64A1C0 = d; dword_64A1B4 = a;
// In the original these are register args (eax=a, edx=b, ecx=d, ebx=c). The
// common caller intent is Coord_Push(x0, y0, x1, y1) installing a clip window
// (see render/surface_blit.cpp / gui groundplan); the field order below matches
// the binary's stores exactly.
struct CoordState {
    i32 a;   // dword_64A1B4 (result, == first arg @eax)
    i32 b;   // dword_64A1B8 (@edx)
    i32 c;   // dword_64A1BC (@ebx)
    i32 d;   // dword_64A1C0 (@ecx)
};

// Returns `a` (the original returns its eax arg unchanged).
i32 CoordPush(CoordState& s, i32 a, i32 b, i32 c, i32 d);

} // namespace guild::play
