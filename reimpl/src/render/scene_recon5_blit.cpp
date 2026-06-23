#include "render/scene_recon5_blit.h"

namespace guild::render {

// gilde.exe 0x5b8d18 — VIBE_TextureCache_ScaleBlitMip.
// Byte indices of the packed weight dword v20:  w0=(u8)v20, w1=BYTE1, w2=BYTE2,
// w3=HIBYTE. Tap c, channel ch (0=R,1=G,2=B) byte is taps.tap[c][ch].
i32 ScaleBlitMip(u16* dst, const u32* weights, i32 dim, BlitTaps& taps,
                 const ChannelLuts& luts, const BlitStrides& strides) {
    const i32 v4 = strides.srcColStride;   // dword_1404E64
    i32 result = 0;

    const u8* t0 = taps.tap[0];            // *a4
    const u8* t1 = taps.tap[1];            // a4[1]
    const u8* t2 = taps.tap[2];            // a4[2]
    const u8* t3 = taps.tap[3];            // a4[3]

    // Fast (point-sample) path: original tests a4[1]==*a4 && a4[1]==a4[2] &&
    // a4[3]==a4[2]  — all four tap row cursors identical.
    if (t1 == t0 && t1 == t2 && t3 == t2) {
        for (i32 row = 0; row < dim; ++row) {
            u16* out = dst;                          // v7 = a1
            const u8* s = taps.tap[0];               // v9 = *v26
            for (i32 col = 0; col < dim; ++col) {
                u32 r = s[0];                        // *v9
                u32 g = luts.g[s[1]];                // dword_1404660[v9[1]]
                u32 rl = luts.r[r];                  // dword_1404260[r]
                u32 v31 = g + rl;
                u32 b = s[2];                        // v9[2]
                s += v4;                             // v9 += v4
                *out++ = (u16)((u16)luts.b[b] + (u16)v31); // 1404A60[b] + (WORD)v31
            }
            // a1 advances by dstRowStride bytes; weight set base advances.
            dst = reinterpret_cast<u16*>(
                reinterpret_cast<u8*>(dst) + strides.dstRowStride);
            taps.tap[0] += strides.weightAdvance;    // *v26 += dword_1404E68
            result = strides.weightAdvance;
        }
        return result;
    }

    // General bilinear (4-tap) path.
    result = dim;
    for (i32 row = 0; row < dim; ++row) {
        u16* out = dst;                              // v30 = a1
        const u32* w = weights;                      // v28 = a2
        const u8* p0 = taps.tap[0];                  // v12 = *v26
        const u8* p1 = taps.tap[1];                  // v13 = v26[1]
        const u8* p2 = taps.tap[2];                  // v14 = v26[2]
        const u8* p3 = taps.tap[3];                  // v15 = v26[3]
        for (i32 col = 0; col < dim; ++col) {
            u32 v20 = *w;                            // packed weight quad
            w = reinterpret_cast<const u32*>(
                reinterpret_cast<const u8*>(w) + v4); // v28 += v4
            u32 w0 = (u8)v20;                        // (u8)v20      -> p0
            u32 w1 = (u8)(v20 >> 8);                 // BYTE1(v20)   -> p1
            u32 w2 = (u8)(v20 >> 16);                // BYTE2(v20)   -> p2
            u32 w3 = (u8)(v20 >> 24);                // HIBYTE(v20)  -> p3

            // channel G (index 1): >>8 then truncate to u8 (the (u8)(... >>8))
            u32 cG = (u8)((u16)(w3 * p3[1] + w2 * p2[1] + w1 * p1[1] + w0 * p0[1]) >> 8);
            // channel B (index 2): u16 sum >>8 (stored in a u8, v32)
            u32 cB = (u8)((u16)(w2 * p2[2] + w1 * p1[2] + w0 * p0[2] + w3 * p3[2]) >> 8);
            // channel R (index 0): remapped directly via R LUT (v16)
            u32 cR = (u8)((u16)(w3 * p3[0] + w1 * p1[0] + w0 * p0[0] + w2 * p2[0]) >> 8);
            u32 v16 = luts.r[cR];

            p1 += v4; p0 += v4; p2 += v4; p3 += v4;  // advance taps by srcColStride

            // *v30++ = LOWORD(1404A60[B]) + LOWORD(1404660[G]) + v16
            *out++ = (u16)((u16)luts.b[cB] + (u16)luts.g[cG] + (u16)v16);
        }
        // Advance dest row, weight source row, and tap row cursors (original
        // recomputes v26[0..3] += weightAdvance, a2 += srcRowStride).
        dst = reinterpret_cast<u16*>(
            reinterpret_cast<u8*>(dst) + strides.dstRowStride);   // dword_1404E70
        weights = reinterpret_cast<const u32*>(
            reinterpret_cast<const u8*>(weights) + strides.srcRowStride); // E60
        result = strides.weightAdvance;              // dword_1404E68
        taps.tap[0] += strides.weightAdvance;        // *v26  += E68
        taps.tap[1] += strides.weightAdvance;        // v26[1] += E68 (v19)
        taps.tap[2] += result;                       // v26[2] = v17 + result
        taps.tap[3] += result;                       // v26[3] = v18 + result
    }
    return result;
}

} // namespace guild::render
