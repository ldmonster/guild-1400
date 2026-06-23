#pragma once
// =============================================================================
// shape_showbank_misc_recon — VIBE_Velocity_Apply (gilde.exe 0x5d883c).
//
//   int __userpurge VIBE_Velocity_Apply@<eax>(
//       int a1@<eax>, int a2@<edx>, int bank@<ecx>, int a4@<ebx>,
//       unsigned __int8 shapeNr);
//
// Despite the worklist name, 0x5d883c is the "shp_ShowShapeFromBank" sibling of
// the variants already in render/sprite_scale.cpp (0x5D861C / 0x5D86C4) and
// render/animation_decode.cpp (0x5D85B8): it validates the requested shape
// index against the bank's shape count, temporarily forces the shape's frame
// flag to 2, dispatches the frame through VIBE_FrameData_Process (0x5d781c),
// then restores the flag.  a1/a2/a4 are the draw parameters forwarded verbatim.
//
// Bank record layout (byte offsets in the original):
//   +0x0a  char  name[]               (used only in the error message)
//   +0x2a  u16   shapeCount
//   +0x45  u32   shapeOffset[shapeNr] (stride 4: at bank + 4*shapeNr + 0x45)
// Shape record (at bank + shapeOffset):
//   +0x0d  u8    frameFlag            (saved, forced to 2, restored)
//
// VIBE_FrameData_Process already exists in the reimpl (render/animation_decode);
// it is reached through a hook so this leftover file is self-contained.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

// FrameData_Process hook: VIBE_FrameData_Process(a1, a2, shapePtr, a4).
using FrameDataProcessFn = void (*)(int a1, int a2, u8* shape, int a4);
// Error sink hook for the invalid-shape diagnostic (original: VIBE_Crt_Sprintf_0
// into a 128-byte buffer with "shp_ShowShapeFromBank:Shapenr is invalidate! Bank:%s").
using ShapeErrorFn = void (*)(const char* bankName);

// gilde.exe 0x5d883c — VIBE_Velocity_Apply ("shp_ShowShapeFromBank").
//   Returns 1 on success, 0 if `bank` is null or `shapeNr` exceeds the count.
int Shape_ShowFromBank(int a1, int a2, u8* bank, int a4, u8 shapeNr,
                       FrameDataProcessFn process, ShapeErrorFn onError);

} // namespace guild::render
