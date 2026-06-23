#include "sim/result_blit_recon.h"

// gilde.exe — clipped 16-bpp DIB blit/plot. See result_blit_recon.h for the
// recovered context layout and provenance. Each function is a 1:1 translation.

namespace guild::sim {

// gilde.exe 0x423fbc — VIBE_Result_Handler_Validate.
//   *(WORD*)(ctx[7] + 2*(ctx[4]*y + x)) = value; return 1;
int Result_Handler_Validate(i32 a1, i32 a2, ResultBitmap* a3, u16 a4) {
    a3->pixels[static_cast<std::size_t>(a3->stride * a2 + a1)] = a4;
    return 1;
}

// gilde.exe 0x423fd0 — VIBE_Result_Handler_Broadcast.
int Result_Handler_Broadcast(i32 a1, i32 a2, ResultBitmap* a3, u16 a4) {
    if (a1 >= 0 && a1 < a3->width && a2 >= 0 && a2 < a3->height
        && a1 >= a3->clipX0 && a1 < a3->clipX1
        && a2 >= a3->clipY0 && a2 < a3->clipY1) {
        return Result_Handler_Validate(a1, a2, a3, a4);
    }
    return 0;
}

// gilde.exe 0x423980 — VIBE_Result_Broadcast.
//   __userpurge eax(a1=dstX,edx=dstY,ecx=dstW,ebx=dstH, [a5]=src,a6=srcX,a7=srcY,a8=target)
int Result_Broadcast(i32 a1, i32 a2, i32 a3, i32 a4,
                     ResultBitmap* a5, i32 a6, i32 a7,
                     void* a8, const ResultBlitLeaves& leaves) {
    i32 v8 = a7;   // srcY working copy
    i32 v9 = a1;   // dstX working copy
    i32 v11 = a4;  // dstH working copy

    if (a2 < 0) {              // dstY negative: shrink dstW, clamp dstY
        a3 += a2;
        a2 = 0;
    }
    if (a1 < 0) {              // dstX negative: shrink dstH, clamp dstX
        v11 = a1 + a4;
        v9 = 0;
    }
    if (a6 < 0) {              // srcX negative
        v9 -= a6;
        v11 += a6;
        a6 = 0;
    }
    if (a7 < 0) {              // srcY negative
        a2 -= a7;
        a3 += a7;
        v8 = 0;
    }
    if (a2 + a3 > a5->clipY1)  // a5[12] = src height
        a3 = a5->clipY1 - a2;
    if (v9 + v11 > a5->clipX1) // a5[11] = src width
        v11 = a5->clipX1 - v9;
    if (!a3 || !v11)
        return 0;

    i32 v15 = a5->_13;         // a5[13] decompress flag
    if (a5->_8 && !a5->_15) {  // a5[8] blit-data ptr, a5[15] skip flag
        if (v15)
            if (leaves.decompressFinalize) leaves.decompressFinalize(a5);

        // v14 = src rect {srcX, srcY, srcX+v11, srcY+a3}
        // v13 = dst rect {dstX, dstY, dstX+v11, dstY+a3}
        i32 v14[4];
        i32 v13[4];
        v14[0] = v9;            // v14[0] = v9 (dstX working — original uses v9 here)
        v14[1] = a2;            // v14[1] = a2 (dstY)
        v13[1] = v8;            // v13[1] = v8 (srcY)
        v13[0] = a6;            // v13[0] = a6 (srcX)
        v14[3] = a3 + a2;       // v14[3]
        v14[2] = v11 + v9;      // v14[2]
        v13[2] = v11 + a6;      // v13[2]
        v13[3] = a3 + v8;       // v13[3]

        // (*(target_vtbl+20))(target, v13, a5[8], v14, &unk_1000000, 0)
        // The original passes v13 (the src-derived rect) and v14 (the dst-derived
        // rect); preserved in that argument order.
        if (leaves.blit && leaves.blit(a8, v13, a5->_8, v14))
            return 0;
    }

    if (v15) {
        if (leaves.decompressBlob) leaves.decompressBlob(a5, v11);
    } else {
        if (leaves.decompressFinalize) leaves.decompressFinalize(a5);
    }
    return 1;
}

} // namespace guild::sim
