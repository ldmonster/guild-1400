#include "render/render_leaves.h"

#include <cstddef>
#include <cstdint>

namespace guild::render {

namespace {

// Field position of a contiguous channel mask = number of trailing zero bits.
// Mirrors the originals' `for (pos=0; (m&1)==0; ++pos) m>>=1;` prologue verbatim.
inline int MaskPos(u32 mask) {
    int pos = 0;
    for (; (mask & 1) == 0 && mask != 0; ++pos) mask >>= 1;
    return pos;
}
// Set-bit count of the (low-shifted) remainder; the originals run a second loop
// `while (m) { ++bits; m >>= 1; }` after the trailing-zero pass.
inline int MaskBits(u32 mask) {
    for (; (mask & 1) == 0 && mask != 0; ) mask >>= 1;  // skip trailing zeros
    int bits = 0;
    for (; mask; ++bits) mask >>= 1;
    return bits;
}

// The original ComputeChannelShifts (0x4358d8) writes a field-position (pos) and a
// precision drop (8 - set-bit-count) per mask. PackColorToPixel re-extracts a
// channel from `value` then re-deposits it: (value & mask) >> pos << prec.
inline u32 RepackChannel(u32 value, u32 mask) {
    const int pos  = MaskPos(mask);
    const int prec = 8 - MaskBits(mask);
    return ((value & mask) >> pos) << prec;
}

} // namespace

// gilde.exe 0x4359b0 — VIBE_Render_PackColorToPixel
//   v5 = value & maskHi; cs(maskHi, mask1, .., mask0, ..) recovers pos/prec per
//   mask; then HIWORD = (v5>>posHi<<precHi), BYTE1 = (value&mask1)>>pos1<<prec1,
//   LOBYTE = (value&mask0)>>pos0<<prec0. Faithful 1:1; byte truncation preserved.
i32 PackColorToPixel(i32 value, u32 maskHi, u32 mask0, u32 mask1) {
    const u32 v = static_cast<u32>(value);
    const u8 byte2 = static_cast<u8>(RepackChannel(v, maskHi));  // -> HIWORD low byte (bits 16..23)
    const u8 byte1 = static_cast<u8>(RepackChannel(v, mask1));   // -> bits 8..15
    const u8 byte0 = static_cast<u8>(RepackChannel(v, mask0));   // -> bits 0..7
    u32 result = (static_cast<u32>(byte2) << 16)
               | (static_cast<u32>(byte1) << 8)
               |  static_cast<u32>(byte0);
    return static_cast<i32>(result);
}

// gilde.exe 0x432770 — VIBE_Render_DepthToModeFlag
i32 DepthToModeFlag(i32 depthBits) {
    switch (depthBits) {
        case 8:  return 2048;
        case 16: return 1024;
        case 24: return 512;
        case 32: return 256;
        default: return 0;
    }
}

// gilde.exe 0x436078 — VIBE_Render_StretchAverage24
//   eax=dst(v13), edx=src(a2). 24bpp (B,G,R) box-average down-sample. For each dst
//   pixel, average every src pixel in its [rowSpan)x[colSpan) footprint. The src
//   row span is [src.h*v10/dst.h, src.h*(v10+1)/dst.h); the col span similarly.
//   Channels accumulate as raw bytes (v4=B, v22=G, v3=R) and divide by the count.
u8* StretchAverage24(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    u8* lastRow = nullptr;                                    // tracks the original's `result`
    u32 srcRowStart = 0;                                      // v9
    for (u32 v10 = 0; v10 < static_cast<u32>(dst.height); ++v10) {
        const u32 v11 = srcRowStart;                          // previous src row boundary
        srcRowStart = static_cast<u32>(src.height) * (v10 + 1) / static_cast<u32>(dst.height);
        const u32 rowSpan = srcRowStart - v11;                // v12
        if (static_cast<u32>(dst.width) == 0) continue;       // if (v13[3])
        u8* d = dst.pixels + static_cast<size_t>(dst.pitch) * v10;  // v17
        lastRow = d;
        u32 srcColStart = 0;                                  // v14
        for (u32 v16 = 0; v16 < static_cast<u32>(dst.width); ++v16) {
            const u32 colPrev = srcColStart;                  // v18
            srcColStart = static_cast<u32>(src.width) * (v16 + 1) / static_cast<u32>(dst.width);
            const u32 colSpan = srcColStart - colPrev;        // v20
            u32 accB = 0, accG = 0, accR = 0;                 // v4, v22, v3
            u32 count = 0;                                    // v5
            if (rowSpan != 0) {
                const i32 srcPitch = src.pitch;               // v19
                const u8* rowBase = src.pixels + static_cast<size_t>(srcPitch) * v11; // v6
                for (u32 r = 0; r < rowSpan; ++r) {
                    if (colSpan != 0) {
                        const u8* p = rowBase + 3 * colPrev;  // v7 = v6 + 3*v18
                        for (u32 c = 0; c < colSpan; ++c) {
                            accB += p[0];
                            accG += p[1];
                            accR += p[2];
                            ++count;
                            p += 3;
                        }
                    }
                    rowBase += srcPitch;
                }
            }
            d[0] = static_cast<u8>(accB / count);             // *v17 = v4 / v5
            d[1] = static_cast<u8>(accG / count);             // v17[1]
            d[2] = static_cast<u8>(accR / count);             // v17[2] = v3 / v5
            d += 3;
        }
    }
    return lastRow;
}

// gilde.exe 0x436228 — VIBE_Render_StretchAverage32
//   eax=dst(a1), edx=src(a2). 32bpp per-channel box-average. i/j/k = trailing-zero
//   positions of src R/G/B masks (a2[22..24]); accumulate masked-shifted channels
//   then reassemble: (R/cnt<<i)|(G/cnt<<j)|(B/cnt<<k).
i32* StretchAverage32(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    const int i = MaskPos(src.rMask);   // a2[22]
    const int j = MaskPos(src.gMask);   // a2[23]
    const int k = MaskPos(src.bMask);   // a2[24]
    const u32 mR = src.rMask, mG = src.gMask, mB = src.bMask;

    i32* result = reinterpret_cast<i32*>(dst.pixels);
    u32 srcRowStart = 0;                                      // v11
    for (u32 v12 = 0; v12 < static_cast<u32>(dst.height); ++v12) {
        const u32 v13 = srcRowStart;
        srcRowStart = static_cast<u32>(src.height) * (v12 + 1) / static_cast<u32>(dst.height);
        if (static_cast<u32>(dst.width) == 0) continue;
        i32* d = reinterpret_cast<i32*>(dst.pixels + static_cast<size_t>(dst.pitch) * v12); // v16
        const u32 rowSpan = srcRowStart - v13;                // v14
        u32 srcColStart = 0;                                  // v17
        for (u32 v18 = 0; v18 < static_cast<u32>(dst.width); ++v18) {
            const u32 colPrev = srcColStart;                  // v19
            srcColStart = static_cast<u32>(src.width) * (v18 + 1) / static_cast<u32>(dst.width);
            const u32 colSpan = srcColStart - colPrev;        // v21
            u32 accR = 0, accG = 0, accB = 0;                 // v9, v6, v8
            u32 count = 0;                                    // v27
            if (rowSpan != 0) {
                const i32 srcPitch = src.pitch;               // v20
                const u8* rowBase = src.pixels + static_cast<size_t>(srcPitch) * v13; // v23
                for (u32 r = 0; r < rowSpan; ++r) {
                    if (colSpan != 0) {
                        const u32* p = reinterpret_cast<const u32*>(rowBase) + colPrev; // v10
                        for (u32 c = 0; c < colSpan; ++c) {
                            const u32 px = *p++;
                            accR += (mR & px) >> i;
                            accG += (mG & px) >> j;
                            accB += (mB & px) >> k;
                            ++count;
                        }
                    }
                    rowBase += srcPitch;
                }
            }
            *d++ = static_cast<i32>(((accG / count) << j)
                                  | ((accR / count) << i)
                                  | ((accB / count) << k));
        }
    }
    return result;
}

// gilde.exe 0x436504 — VIBE_Render_StretchInterpolate16
//   eax=dst(a1), edx=src(a2). 16bpp bilinear up-sample. For each src texel the
//   four-tap neighbourhood (self, +1 col, +half-row, +half-row+1 col) is decoded
//   per-channel; the dst footprint [colSpan)x[rowSpan) is bilinearly filled with
//   integer-divided increments. Mirrors the original's nested accumulators exactly.
u16* StretchInterpolate16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    const int i = MaskPos(src.rMask);
    const int j = MaskPos(src.gMask);
    const int k = MaskPos(src.bMask);
    const u32 mR = src.rMask, mG = src.gMask, mB = src.bMask;

    u16* result = reinterpret_cast<u16*>(dst.pixels);
    u32 dstRowStart = 0;                                      // v29
    for (u32 v30 = 0; v30 < static_cast<u32>(src.height); ++v30) {
        const u32 v33 = dstRowStart;
        dstRowStart = (v30 + 1) * static_cast<u32>(dst.height) / static_cast<u32>(src.height); // v29
        const i32 srcRowBase = src.pitch * static_cast<i32>(v30) + 0; // byte offset into src row (v36 base)
        const u8* srcRowBytes = src.pixels + srcRowBase;
        const u16* v35 = reinterpret_cast<const u16*>(srcRowBytes); // walking src texel
        if (static_cast<u32>(src.width) == 0) continue;
        const u32 v31 = dstRowStart - v33;                    // dst row span
        u32 dstColStart = 0;                                  // v34
        for (u32 v37 = 0; v37 < static_cast<u32>(src.width); ++v37) {
            const u32 v49 = dstColStart;                      // dst col start
            const u32 srcW = static_cast<u32>(src.width);     // v6
            const u32 v7 = dstColStart;
            dstColStart = (v37 + 1) * static_cast<u32>(dst.width) / srcW; // v34
            const u32 v62 = dstColStart - v7;                 // dst col span
            const u32 v8 = (v37 + 1 < srcW) ? 1u : 0u;        // +1 col tap (clamped)
            // +half-row tap: 0 on last src row, else (src.pitch>>1) texels
            const i32 v9 = (v30 + 1 >= static_cast<u32>(src.height)) ? 0 : (src.pitch >> 1);

            const u16* rowPtr = reinterpret_cast<const u16*>(src.pixels + srcRowBase); // v36 as u16*
            // tap 0 (self)
            const u32 t0 = *v35;
            const u32 v48 = (t0 & mR) >> i;
            const u32 v46 = (t0 & mG) >> j;
            const u32 v43 = (t0 & mB) >> k;
            // tap 1 (+1 col)
            const u32 t1 = rowPtr[v8 + v37];
            const u32 v44 = (t1 & mR) >> i;
            const u32 v47 = (t1 & mG) >> j;
            const u32 v45 = (t1 & mB) >> k;
            // tap 2 (+half-row)
            const u32 v11 = v37 + static_cast<u32>(v9);
            const u32 t2 = rowPtr[v11];
            const u32 v13 = (t2 & mR) >> i;
            const u32 v14 = (t2 & mG) >> j;
            const u32 v32 = (t2 & mB) >> k;
            // tap 3 (+half-row +1 col)
            const u32 t3 = rowPtr[v11 + v8];
            const u32 v16 = (t3 & mR) >> i;
            const u32 v17 = (t3 & mG) >> j;
            const u32 v19 = (t3 & mB) >> k;

            if (v31 != 0) {
                const u32 v39 = v14 - v46;                    // G col-3 - col-1 (mod 2^32)
                const u32 v42 = v13 - v48;                    // R
                const u32 v40 = v17 - v47;
                const u32 v41 = v19 - v45;
                u32 v55 = 0, v26 = 0, v53 = 0, v51 = 0, v52 = 0, v54 = 0; // row accumulators
                u32 v27 = v33;                                // dst row index
                do {
                    const u32 v61 = v26 / v31 + v48;          // R left edge
                    const u32 v59 = v55 / v31 + v46;          // G
                    const u32 v60 = v53 / v31 + v43;          // B
                    u16* dstRow = reinterpret_cast<u16*>(dst.pixels + dst.pitch * static_cast<i32>(v27)); // a1[9]+a1[4]*v27
                    if (v62 != 0) {
                        // original: v21=B-accum, v22=R-accum, v24=G-accum; deposit
                        // = (v24<<j)|(v22<<i)|(v21<<k).
                        u32 v21 = 0, v22 = 0, v24 = 0;
                        u16* p = dstRow + v49;                // v23
                        const u32 incB = v51 / v31 + v45 - v60; // v21 += ...
                        const u32 incR = v52 / v31 + v44 - v61; // v22 += ...
                        const u32 incG = v54 / v31 + v47 - v59; // v24 += ...
                        for (u32 col = 0; col < v62; ++col) {
                            *p++ = static_cast<u16>(((v24 / v62 + v59) << j)
                                                  | ((v22 / v62 + v61) << i)
                                                  | ((v21 / v62 + v60) << k));
                            v21 += incB;
                            v22 += incR;
                            v24 += incG;
                        }
                    }
                    ++v27;
                    v51 += v41;
                    v54 += v40;
                    v52 += v16 - v44;
                    v53 += v32 - v43;
                    v55 += v39;
                    v26 += v42;
                } while (v27 < dstRowStart);
            }
            ++v35;
        }
    }
    return result;
}

// gilde.exe 0x436aa4 — VIBE_Render_StretchInterpolate24
//   eax=dst(a1), edx=src(a2). 24bpp (B,G,R) bilinear up-sample. Channels are raw
//   bytes (no masks). Same four-tap bilinear structure as the 16/32 variants.
u8* StretchInterpolate24(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    u8* result = nullptr;
    u32 dstRowStart = 0;                                      // v18 boundary
    for (u32 i = 0; i < static_cast<u32>(src.height); ++i) {
        const u32 v24 = dstRowStart;
        dstRowStart = (i + 1) * static_cast<u32>(dst.height) / static_cast<u32>(src.height); // v18
        const i32 srcRowBase = src.pitch * static_cast<i32>(i);   // v26
        const u8* v27 = src.pixels + srcRowBase;              // walking src texel (3 bytes)
        if (static_cast<u32>(src.width) == 0) continue;
        const u32 v20 = dstRowStart - v24;                    // dst row span
        u32 dstColStart = 0;                                  // v25
        for (u32 v29 = 0; v29 < static_cast<u32>(src.width); ++v29) {
            const u32 v38 = dstColStart;
            const u32 srcW = static_cast<u32>(src.width);     // v3
            const u32 v4 = dstColStart;
            dstColStart = (v29 + 1) * static_cast<u32>(dst.width) / srcW; // v25
            const u32 v5 = dstColStart - v4;                  // dst col span
            const i32 v6 = (i + 1 >= static_cast<u32>(src.height)) ? 0 : (src.pitch >> 1); // half-row byte offset

            const u8* rowBase = src.pixels + srcRowBase;      // v26 base
            // tap 0 (self)
            const u32 v37 = v27[0];                           // B
            const u32 v32 = v27[1];                           // G
            const u32 v33 = v27[2];                           // R
            // tap 1 (+1 col) at 3*(v29 + (v29+1<w))
            const u32 v7 = 3 * (v29 + ((v29 + 1 < srcW) ? 1u : 0u));
            const u32 v34 = rowBase[v7 + 0];
            const u32 v36 = rowBase[v7 + 1];
            const u32 v35 = rowBase[v7 + 2];
            // tap 2 (+half-row) at v6 + 3*v29
            const u8* p8 = rowBase + v6 + 3 * v29;
            const u32 v9 = p8[0];
            const u32 v10 = p8[1];
            const u32 v22 = p8[2];
            // tap 3 (+half-row +1 col) at v6 + v7
            const u8* p11 = rowBase + v6 + v7;
            const u32 v21 = p11[0];
            const u32 v23 = p11[1];
            const u32 p11_2 = p11[2];

            if (v20 != 0) {
                const u32 v30 = v9 - v37;                     // B diff (tap2-tap0)
                const u32 v31 = p11_2 - v35;                  // R diff (tap3-tap1)
                u32 v42 = 0, v45 = 0, v44 = 0, v41 = 0, v16 = 0, v43 = 0; // row accumulators
                u32 v17 = v24;                                // dst row index
                do {
                    const u32 v48 = v45 / v20 + v37;          // B left edge
                    const u32 v46 = v16 / v20 + v32;          // G
                    const u32 v47 = v44 / v20 + v33;          // R
                    u8* v40 = dst.pixels + dst.pitch * static_cast<i32>(v17); // a1[4]*v17 + a1[9]
                    if (v5 != 0) {
                        // original: v13=B-accum, v15=G-accum, v12=R-accum.
                        u32 v13 = 0, v15 = 0, v12 = 0;
                        u8* p = v40 + 3 * v38;                // v14
                        const u32 incG = v41 / v20 + v36 - v46; // v15 += ...
                        const u32 incR = v43 / v20 + v35 - v47; // v12 += ...
                        const u32 incB = v42 / v20 + v34 - v48; // v13 += ...
                        for (u32 col = 0; col < v5; ++col) {
                            p[0] = static_cast<u8>(v13 / v5 + v48); // B
                            p[1] = static_cast<u8>(v15 / v5 + v46); // G
                            p[2] = static_cast<u8>(v12 / v5 + v47); // R
                            p += 3;
                            v15 += incG;
                            v12 += incR;
                            v13 += incB;
                        }
                    }
                    ++v17;
                    v43 += v31;
                    v41 += v23 - v36;
                    v42 += v21 - v34;
                    v44 += v22 - v33;
                    v16 += v10 - v32;
                    v45 += v30;
                } while (v17 < dstRowStart);
            }
            v27 += 3;
            result = src.pixels;  // original returns `++v29` cast; keep a non-null sentinel
        }
    }
    return result;
}

// gilde.exe 0x436f30 — VIBE_Render_StretchInterpolate32
//   eax=dst(a1), edx=src(a2). 32bpp per-channel (mask-decoded) bilinear up-sample.
//   Same four-tap structure as the 16bpp variant but reading 32-bit texels.
i32* StretchInterpolate32(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    const int i = MaskPos(src.rMask);
    const int j = MaskPos(src.gMask);
    const int k = MaskPos(src.bMask);
    const u32 mR = src.rMask, mG = src.gMask, mB = src.bMask;

    i32* result = reinterpret_cast<i32*>(dst.pixels);
    u32 dstRowStart = 0;                                      // v22
    for (u32 v23 = 0; v23 < static_cast<u32>(src.height); ++v23) {
        const u32 v28 = dstRowStart;
        dstRowStart = (v23 + 1) * static_cast<u32>(dst.height) / static_cast<u32>(src.height); // v22
        const i32 srcRowBase = src.pitch * static_cast<i32>(v23); // v30
        const u32* v31 = reinterpret_cast<const u32*>(src.pixels + srcRowBase); // walking src texel
        if (static_cast<u32>(src.width) == 0) continue;
        const u32 v24 = dstRowStart - v28;                    // dst row span
        u32 dstColStart = 0;                                  // v29
        for (u32 v32 = 0; v32 < static_cast<u32>(src.width); ++v32) {
            const u32 v38 = dstColStart;
            const u32 srcW = static_cast<u32>(src.width);     // v6
            const u32 v7 = dstColStart;
            dstColStart = (v32 + 1) * static_cast<u32>(dst.width) / srcW; // v29
            const u32 v59 = dstColStart - v7;                 // dst col span
            const u32 v8 = (v32 + 1 < srcW) ? 1u : 0u;        // +1 col
            const i32 v10 = (v23 + 1 >= static_cast<u32>(src.height)) ? 0 : (src.pitch >> 1); // half-row in texels

            const u32* rowBase = reinterpret_cast<const u32*>(src.pixels + srcRowBase); // v30 as u32*
            // tap 0 (self)
            const u32 t0 = *v31;
            const u32 v37 = (mR & t0) >> i;
            const u32 v39 = (mG & t0) >> j;
            const u32 v40 = (mB & t0) >> k;
            // tap 1 (+1 col)
            const u32 t1 = rowBase[v8 + v32];
            const u32 v42 = (mR & t1) >> i;
            const u32 v43 = (mG & t1) >> j;
            const u32 v41 = (mB & t1) >> k;
            // tap 2 (+half-row)
            const u32 v13 = v32 + static_cast<u32>(v10);
            const u32 t2 = rowBase[v13];
            const u32 t2r = (mR & t2) >> i;
            const u32 v26 = (mG & t2) >> j;
            const u32 v25 = (mB & t2) >> k;
            // tap 3 (+half-row +1 col)
            const u32 t3 = rowBase[v8 + v13];
            const u32 v27 = (mR & t3) >> i;
            const u32 v17 = (mG & t3) >> j;
            const u32 t3b = (mB & t3) >> k;

            if (v24 != 0) {
                const u32 v36 = t2r - v37;                    // R diff (tap2-tap0)
                const u32 v34 = v17 - v43;                    // G diff (tap3-tap1)
                const u32 v35 = t3b - v41;                    // B diff (tap3-tap1)
                u32 v48 = 0, v51 = 0, v50 = 0, v47 = 0, v46 = 0, v49 = 0; // row accumulators
                u32 v52 = v28;                                // dst row index
                do {
                    const u32 v56 = v51 / v24 + v37;          // R left edge
                    const u32 v57 = v48 / v24 + v39;          // G
                    const u32 v58 = v50 / v24 + v40;          // B
                    u32* v45 = reinterpret_cast<u32*>(dst.pixels + dst.pitch * static_cast<i32>(v52)); // a1[4]*v52+a1[9]
                    if (v59 != 0) {
                        // original: v18=G-accum, v19=B-accum, v20=R-accum; deposit
                        // = (v19<<k)|(v20<<i)|(v18<<j).
                        u32 v18 = 0, v19 = 0, v20 = 0;
                        u32* p = v45 + v38;                   // v21 base
                        const u32 incR = v47 / v24 + v42 - v56; // v20 += ...
                        const u32 incB = v49 / v24 + v41 - v58; // v19 += ...
                        const u32 incG = v46 / v24 + v43 - v57; // v18 += ...
                        for (u32 col = 0; col < v59; ++col) {
                            *p++ = ((v19 / v59 + v58) << k)
                                 | ((v20 / v59 + v56) << i)
                                 | ((v18 / v59 + v57) << j);
                            v20 += incR;
                            v19 += incB;
                            v18 += incG;
                        }
                    }
                    ++v52;
                    v49 += v35;
                    v46 += v34;
                    v47 += v27 - v42;
                    v50 += v25 - v40;
                    v48 += v26 - v39;
                    v51 += v36;   // R row accumulator (was missing)
                } while (v52 < dstRowStart);
            }
            ++v31;
        }
    }
    return result;
}

} // namespace guild::render
