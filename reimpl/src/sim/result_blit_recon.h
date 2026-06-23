#pragma once
// gilde.exe — the "Result"/blit helpers behind VIBE_Form_BroadcastClickResult /
// VIBE_DecompressGameState / VIBE_Entity_InteractionLogic. namespace guild::sim.
//
// Despite the VIBE_Result_* naming, the decompile shows these are a clipped
// 16-bpp software-bitmap layer, NOT generic result/error codes:
//   VIBE_Result_Handler_Validate  @0x423fbc — plot one 16-bit pixel into a DIB.
//   VIBE_Result_Handler_Broadcast @0x423fd0 — clip a pixel against the DIB bounds
//       and active clip rect, then plot it (calls Validate).
//   VIBE_Result_Broadcast         @0x423980 — clip a source rect against a target
//       surface and blit it through the target device's vtable (slot +20), then
//       finalize/advance the decompression state. The blit + decompress leaves
//       are injected (inert by default); the clip arithmetic is reproduced 1:1.
//
// Recovered DIB context layout (dword indices of the a3/a5 struct):
//   +0x04 [1]  width            +0x08 [2]  height
//   +0x10 [4]  stride (px/row)  +0x1C [7]  pixel buffer (u16*)
//   +0x24 [9]  clip x0          +0x28 [10] clip y0
//   +0x2C [11] clip x1 (also src width on a5)
//   +0x30 [12] clip y1 (also src height on a5)
//   +0x20 [8]  blit-data ptr (a5)   +0x34 [13] decompress flag (a5)
//   +0x3C [15] skip-blit flag (a5)
#include "guild/common/types.h"

namespace guild::sim {

// 16-bpp DIB context. Field names follow the recovered dword indices above.
struct ResultBitmap {
    i32 _0    = 0;     // [0]
    i32 width = 0;     // [1] +4
    i32 height = 0;    // [2] +8
    i32 _3 = 0;        // [3]
    i32 stride = 0;    // [4] +16  pixels per row
    i32 _5 = 0, _6 = 0;
    u16* pixels = nullptr; // [7] +28
    i32 _8 = 0;            // [8] +32  (blit-data ptr on the source ctx)
    i32 clipX0 = 0;    // [9]  +36
    i32 clipY0 = 0;    // [10] +40
    i32 clipX1 = 0;    // [11] +44 (also src width on the source ctx)
    i32 clipY1 = 0;    // [12] +48 (also src height on the source ctx)
    i32 _13 = 0;       // [13] +52 (decompress flag on the source ctx)
    i32 _14 = 0;
    i32 _15 = 0;       // [15] +60 (skip-blit flag on the source ctx)
};

// VIBE_Result_Handler_Validate @0x423fbc — write `value` at (x,y):
//   pixels[stride*y + x] = value;   return 1;
// No bounds checking (the caller clips). Faithful 1:1.
int Result_Handler_Validate(i32 x, i32 y, ResultBitmap* ctx, u16 value);

// VIBE_Result_Handler_Broadcast @0x423fd0 — bounds + clip-rect test, then plot.
//   if (0<=x<width && 0<=y<height && clipX0<=x<clipX1 && clipY0<=y<clipY1)
//       return Validate(x,y,ctx,value);
//   return 0;
int Result_Handler_Broadcast(i32 x, i32 y, ResultBitmap* ctx, u16 value);

// VIBE_Result_Broadcast @0x423980 — clip a (dstX,dstY,dstW,dstH)/(srcX,srcY) blit
// of source surface `src` against `src`'s dimensions, then call the target
// device's blit (vtable slot +20). Returns 0 if fully clipped/handled, 1 on a
// completed blit, matching the original's two return points. Leaves injected:
//   blit(target, dstRect[4], srcData, srcRect[4]) — the device blit (vtable +20).
//   decompressBlob(src, width)  — VIBE_DecompressState_Blob @0x423500.
//   decompressFinalize(src)     — VIBE_Decompression_Finalize @0x4235dc.
struct ResultBlitLeaves {
    // Returns nonzero on blit failure (the original treats a nonzero return as
    // "abort, return 0"); rect arrays are {x0,y0,x1,y1}.
    int (*blit)(void* target, const i32 dstRect[4], i32 srcData,
                const i32 srcRect[4]) = nullptr;
    void (*decompressBlob)(ResultBitmap* src, i32 width) = nullptr;
    void (*decompressFinalize)(ResultBitmap* src) = nullptr;
};

int Result_Broadcast(i32 dstX, i32 dstY, i32 dstW, i32 dstH,
                     ResultBitmap* src, i32 srcX, i32 srcY,
                     void* target, const ResultBlitLeaves& leaves);

} // namespace guild::sim
