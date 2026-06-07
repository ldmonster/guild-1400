#include "render/shape.h"

#include <cstring>

namespace guild::render {

namespace {
inline u32 GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }
inline u16 GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }
} // namespace

// gilde.exe 0x5D70CC — VIBE_Shape_DecodeRle.
//   v11 = pixels + (widthPx*y + x)            ; current scanline dst
//   v5  = shape + 50                          ; -> current row's runCount
//   v4  = shape + 54                          ; -> current row's first run
//   for each row in [0, height):
//     result = v11                            ; reset dst to row start
//     for (i = 0; i < *v5; advance v4 past the run):
//       result += (*v4 >> 1)                  ; skip transparent pixels
//       copy v4[1] pixels from (v4+8) to result
//       v4 = v4 + 2*v4[1] + 8                  ; next run
//     v5 = v4; ++v4                            ; next row's runCount, runs follow
//     v11 += widthPx                           ; next scanline
//   return result
u16* ShapeDecodeRle(int x, int y, const u8* shape, const BlitTarget16& dst) {
    u16* rowStart = dst.pixels + (static_cast<ptrdiff_t>(dst.widthPx) * y + x);

    const u16 height = GetU16(shape + shape_rle_off::kHeight);

    // v5 -> current row's runCount (u32 at +0x32); v4 -> first run (+0x36).
    const u8* runCountPtr = shape + 0x32;
    const u8* run         = shape + 0x36;

    u16* result = nullptr;

    for (int rowIdx = 0; rowIdx < height; ++rowIdx) {
        result = rowStart;
        const u32 runs = GetU32(runCountPtr);
        for (u32 i = 0; i < runs; ++i) {
            const u32 skip    = GetU32(run);       // *v4
            const u32 nPixels = GetU32(run + 4);   // v4[1]
            result += (skip >> 1);                 // transparent gap
            const u8* px = run + 8;                // (v4 + 2) as u16* == run + 8 bytes
            for (u32 k = 0; k < nPixels; ++k) {
                *result++ = GetU16(px);
                px += 2;
            }
            run = run + 2u * nPixels + 8u;          // v4 += 2*v4[1] + 8
        }
        // `v5 = v4++`: v4/v5 are u32*. The next row's runCount is at v4's current
        // position; v4 then advances one dword (4 bytes) to the first run.
        runCountPtr = run;
        run = runCountPtr + 4;

        rowStart += dst.widthPx;
    }
    return result;
}

} // namespace guild::render
