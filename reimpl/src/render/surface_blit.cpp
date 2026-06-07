#include "render/surface_blit.h"

namespace guild::render {

// gilde.exe 0x41EF40 — VIBE_Paintbox_DrawShape.
//   if (window has paintbox)            // v4[160] != 0
//     if (window has surface)           // v4[10] != 0
//       if (VIBE_State_Update(surface)) // begin/validate draw
//         VIBE_DecompressState_Blob(surface)   // Lock
//         VIBE_Animation_Basic(x, y, surface, bank, shapeIndex)
//         VIBE_Decompression_Finalize(surface) // Unlock
// (The two error branches just log "Window has no paintbox!" / "Invalidate window!"
//  and draw nothing.) Returns whether a blit happened.
bool PaintboxDrawShape(int x, int y, const u8* bank, int shapeIndex,
                       ISurfaceLock& lock, FrameBlitState st) {
    if (!bank)
        return false;               // no paintbox -> log + return (no draw)
    if (!lock.Lock(st))             // DecompressState_Blob failed
        return false;
    int drawn = AnimationBasic(x, y, bank, shapeIndex, st);
    lock.Unlock();                  // Decompression_Finalize
    return drawn != 0;
}

// gilde.exe 0x41EFC4 — VIBE_Paintbox_DrawShapeDirect.
//   VIBE_State_Update(surface); VIBE_Animation_Basic(x, y, surface, bank, idx);
// The original does NOT explicitly unlock here (the surface stays locked for a
// subsequent direct draw); we still bracket with Lock for a self-contained mock.
bool PaintboxDrawShapeDirect(int x, int y, const u8* bank, int shapeIndex,
                             ISurfaceLock& lock, FrameBlitState st) {
    if (!bank)
        return false;
    if (!lock.Lock(st))
        return false;
    int drawn = AnimationBasic(x, y, bank, shapeIndex, st);
    lock.Unlock();
    return drawn != 0;
}

// gilde.exe 0x41F00C — VIBE_Paintbox_DrawShapeClipped.
//   VIBE_State_Update(surface);
//   VIBE_Coord_Push(clipX0, clipY0, clipX1, clipY1);   // install clip window
//   VIBE_Animation_Basic(x, y, surface, bank, idx);
bool PaintboxDrawShapeClipped(int x, int y, const u8* bank, int shapeIndex,
                              int clipX0, int clipY0, int clipX1, int clipY1,
                              ISurfaceLock& lock, FrameBlitState st) {
    if (!bank)
        return false;
    if (!lock.Lock(st))
        return false;
    // VIBE_Coord_Push: install the blit clip rectangle for the RLE blitters.
    st.clipX0 = clipX0;
    st.clipY0 = clipY0;
    st.clipX1 = clipX1;
    st.clipY1 = clipY1;
    int drawn = AnimationBasic(x, y, bank, shapeIndex, st);
    lock.Unlock();
    return drawn != 0;
}

} // namespace guild::render
