// misc_recon4_leaves.cpp — see header for provenance and inert-hook notes.
#include "sim/misc_recon4_leaves.h"
#include <cstring>  // memcpy

namespace guild::sim {

// ===========================================================================
// 0x41e57c — VIBE_State_Finalize
// ===========================================================================
i32 StateFinalize(const StateEnv& env, int idx, StateOutputs* out) {
    // 0x41e599: if ( *(u32*)(84*idx + bank + 60) != 7 ) return 0;
    if (env.recordTag60(env.ctx, idx) != 7)
        return 0;

    // 0x41e5a9: dword_62D244 = VIBE_State_Update(idx); dword_62D248 = (edx)
    i32 edx = 0;
    i32 recordPtr = env.stateUpdate(env.ctx, idx, &edx);

    // 0x41e5c3: dword_69FFB0 = *(u16*)(recordPtr + 46)
    u16 lineHeight = env.recordLineHeight(env.ctx, recordPtr);

    i32 spacingA, spacingB;
    // 0x41e5f0: if ( edx == base || edx == base+2 ) {8,2} else {4,2}
    if (edx == env.baseStateIndex || edx == env.baseStateIndex + 2) {
        spacingA = 8;
        spacingB = 2;
    } else {
        spacingA = 4;
        spacingB = 2;
    }

    if (out) {
        out->recordPtr  = recordPtr;
        out->recordEdx  = edx;
        out->lineHeight = lineHeight;
        out->spacingA   = spacingA;
        out->spacingB   = spacingB;
    }
    return recordPtr;
}

// ===========================================================================
// 0x429290 — VIBE_Rain_Destroy
// ===========================================================================
u8* RainDestroy(RainObject* obj, const RainFreeHooks& hooks) {
    // 0x429295: if ( result ) ...
    if (!obj || !obj->base)
        return obj ? obj->base : nullptr;

    u8* base = obj->base;
    // 0x429298: v4 = *(u32*)(base+16);  if (v4) { Free(v4); *(base+16)=0; }
    if (obj->sub) {
        if (hooks.freeDebug) hooks.freeDebug(hooks.ctx, obj->sub);  // 0x4292a1
        obj->sub = nullptr;                                          // 0x4292a6
    }
    // 0x4292af: return VIBE_Memory_FreeDebug(base, ...)
    if (hooks.freeDebug) hooks.freeDebug(hooks.ctx, base);
    return base;
}

// ===========================================================================
// 0x54f884 — VIBE_DragSlot_ResetGridTable
// ===========================================================================
void DragSlotResetGridTable(DragGridRow* rows) {
    // for i in 0..31:
    for (int i = 0; i < kGridRows; ++i) {
        DragGridRow& r = rows[i];
        r.header[0] = 0;    // *v1 = 0
        r.header[1] = -1;   // v1[1] = -1
        r.header[2] = -1;   // v1[2] = -1
        r.header[3] = -1;   // v1[3] = -1

        // inner: result = i<<6; do { result += 8; cell[result]=-1; flag[result]=0; }
        //        while ( result != 48 + 64*i );
        // i.e. byte-offsets 8,16,24,32,40,48 within this row -> cell index 1..6.
        int result = i << 6;          // base byte offset (start of row)
        const int stop = 48 + (i << 6);
        do {
            result += 8;
            int cellIdx = (result - (i << 6)) / 8;  // 1..6 within the row
            r.cellId[cellIdx]   = -1;
            r.cellFlag[cellIdx] = 0;
        } while (result != stop);
    }
}

// ===========================================================================
// 0x423648 / 0x42395c — clipped surface blit
// ===========================================================================
int ResultFinalize(Surface* src, Surface* dst,
                   int dstY, int dstX, int dstW, int dstH,
                   int srcY, int srcX, bool alpha,
                   const BlitHooks& hooks) {
    // Register map from the decompile:
    //   a1 = dstY,  a2 = dstX,  a3->v32 = dstW, a4->v31 = dstH
    //   a5 = src,   a6->v9 = srcY, a7->v11 = srcX,  a8 = dst
    int a1 = dstY, a2 = dstX, v32 = dstW, v31 = dstH;
    int v9 = srcY, v11 = srcX;

    // --- clamp source X (a7/v11) against dst.clipMinX (a8+40) --------------
    int v12 = dst->clipMinX;                 // *(a8+40)
    if (v11 < v12) {
        int v22 = v12 - v11;                 // 0x4238b4
        a2 += v22;                            // dst X shifts
        v32 -= v22;
        v11 = dst->clipMinX;
    }
    // --- clamp source Y (a6/v9) against dst.clipMinY (a8+36) ---------------
    int v13 = dst->clipMinY;                 // *(a8+36)
    if (v9 < v13) {
        int v14 = v13 - v9;
        a1 += v14;
        v31 -= v14;
        v9 = dst->clipMinY;
    }
    // --- clamp dst X (a2) against src.clipMinX (a5+40) ---------------------
    int v15 = src->clipMinX;                 // *(a5+40)
    if (a2 < v15) {
        v32 -= v15 - a2;
        a2 = src->clipMinX;
    }
    // --- clamp dst Y (a1) against src.clipMinY (a5+36) ---------------------
    int v16 = src->clipMinY;                 // *(a5+36)
    if (a1 < v16) {
        v31 -= v16 - a1;
        a1 = src->clipMinY;
    }
    // --- clamp widths/heights against the far clip edges -------------------
    if (v11 + v32 > dst->clipMaxX) v32 = dst->clipMaxX - v11;   // a8+48
    if (a2 + v32  > src->clipMaxX) v32 = src->clipMaxX - a2;    // a5+48
    if (v9 + v31  > dst->clipMaxY) v31 = dst->clipMaxY - v9;    // a8+44
    if (a1 + v31  > src->clipMaxY) v31 = src->clipMaxY - a1;    // a5+44

    // 0x42370b: empty rect -> nothing copied
    if (v32 <= 0 || v31 <= 0)
        return 0;

    // Row-base byte offsets into each surface's pixel buffer.
    int srcBpp = src->bppByte >> 3;          // (int)*(u8*)(a5+20) >> 3
    int dstBpp = dst->bppByte >> 3;
    long v29 = (long)a1 * srcBpp + (long)a2 * src->strideBytes;   // src base
    long v30 = (long)v9 * dstBpp + (long)v11 * dst->strideBytes;  // dst base
    int v28 = dst->lockState;                // *(a8+52)
    int v27 = src->lockState;                // *(a5+52)

    int copyBytes = v31 * srcBpp;            // bytes per copied row

    auto softwareCopy = [&]() {
        // 0x42390f..0x42392f: for v18 in 0..v32-1 copy one row.
        for (int v18 = 0; v18 < v32; ++v18) {
            u8* d = dst->pixels + v30 + (long)v18 * dst->strideBytes;
            const u8* s = src->pixels + (long)v18 * src->strideBytes + v29;
            std::memcpy(d, s, (size_t)copyBytes);
        }
    };

    // 0x42375d: if both have ddObj and neither is "decompressed" -> GPU path,
    // else software path.
    bool gpuPathEligible = dst->ddObj && src->ddObj &&
                           !dst->decompressed && !src->decompressed;

    if (!gpuPathEligible) {
        // 0x4238d9: decompress both, then software copy.
        if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, dst, a2);
        if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, src, 0);
        softwareCopy();
    } else {
        // 0x423786: if locked, finalize the decompression of each.
        if (v28 && hooks.decompressFinalize) hooks.decompressFinalize(hooks.ctx, dst);
        if (v27 && hooks.decompressFinalize) hooks.decompressFinalize(hooks.ctx, src);

        // Build the DD blit rects (dst v24, src v25 in the decompile).
        int srcRect[4] = { a1, a2, a1 + v31, a2 + v32 };  // v25[0..3]
        int dstRect[4] = { v9, v11, v9 + v31, v11 + v32 }; // v24[0..3]

        // 0x4237fd: COM blit. Nonzero return -> 0x4238d0 return 0.
        bool blitFailed = false;
        if (hooks.gpuBlit)
            blitFailed = !hooks.gpuBlit(hooks.ctx, dst, dstRect, src, srcRect, alpha);
        else
            blitFailed = true;   // no GPU backend -> behave as E_NOTIMPL fallthrough

        if (hooks.gpuBlit && !blitFailed) {
            // GPU reported success (original returns 0 at 0x4238d0).
            return 0;
        }
        // 0x423812: dword_7626C8 == E_NOTIMPL(0x80004001) -> software fallback.
        if (hooks.gpuNotImplemented || !hooks.gpuBlit) {
            dst->decompressed = 1;  // *(a8+60)=1
            src->decompressed = 1;  // *(a5+60)=1
            if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, dst, /*v18*/0);
            if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, src, 0);
            softwareCopy();
        }
    }

    // LABEL_33: post-copy lock/finalize bookkeeping.
    if (v28) { if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, dst, v32); }
    else     { if (hooks.decompressFinalize) hooks.decompressFinalize(hooks.ctx, dst); }
    if (v27) { if (hooks.decompressBlob) hooks.decompressBlob(hooks.ctx, src, v32); }
    else     { if (hooks.decompressFinalize) hooks.decompressFinalize(hooks.ctx, src); }

    return 1;
}

int ResultHandlerInteraction(Surface* src, Surface* dst,
                             int dstY, int dstX, int dstW, int dstH,
                             int srcY, int srcX, const BlitHooks& hooks) {
    // 0x42397a: VIBE_Result_Finalize(..., a9=0)
    return ResultFinalize(src, dst, dstY, dstX, dstW, dstH, srcY, srcX,
                          /*alpha=*/false, hooks);
}

} // namespace guild::sim
