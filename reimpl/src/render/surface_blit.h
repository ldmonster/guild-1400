#pragma once
#include "guild/common/types.h"
#include "render/animation_decode.h"

// =============================================================================
// guild::render — Paintbox shape-draw dispatchers (d2_interface.c).
//
// These are the thin entry points the GUI/window layer calls to paint a sprite
// from a window's "paintbox" (its associated shape/animation bank). They are
// pure orchestration: LOCK the window's offscreen surface, blit the shape via
// VIBE_Animation_Basic, then UNLOCK. The lock/unlock are DirectDraw Lock/Unlock
// (VIBE_DecompressState_Blob @0x423500 = Lock vtbl+100, VIBE_Decompression_Finalize
// @0x4235dc = Unlock) — a present-layer / vendor boundary, so per the agent guide
// they are abstracted behind ISurfaceLock here; the test provides a memory mock.
//
//   0x41EF40  VIBE_Paintbox_DrawShape        (window-indexed; logs if no paintbox)
//   0x41EFC4  VIBE_Paintbox_DrawShapeDirect  (caller supplies the bank directly)
//   0x41F00C  VIBE_Paintbox_DrawShapeClipped (pushes a clip rect, then draws)
//
// In the original the active shape index is the global dword_62D2A4; here it is an
// explicit argument. The pixel work lives entirely in animation_decode.h.
// =============================================================================
namespace guild::render {

// Surface lock interface — the DDraw Lock/Unlock the dispatchers bracket the blit
// with. Lock returns the locked pixel buffer + row stride (in pixels) for the
// FrameBlitState; Unlock releases it. A memory-backed mock satisfies this in tests.
struct ISurfaceLock {
    virtual ~ISurfaceLock() = default;
    // Lock the offscreen surface; fill st.dest / st.destStridePx. Returns false if
    // the surface could not be locked (mirrors the original's error early-out).
    virtual bool Lock(FrameBlitState& st) = 0;
    virtual void Unlock() = 0;
};

// gilde.exe 0x41EF40 — VIBE_Paintbox_DrawShape. Looks up the active window's
//   paintbox bank; if present, Lock -> AnimationBasic(x, y, bank, shapeIndex) ->
//   Unlock. `bank`/`shapeIndex` stand in for the window's resolved paintbox and
//   global dword_62D2A4. Returns true if the shape was drawn.
bool PaintboxDrawShape(int x, int y, const u8* bank, int shapeIndex,
                       ISurfaceLock& lock, FrameBlitState st);

// gilde.exe 0x41EFC4 — VIBE_Paintbox_DrawShapeDirect. As above but the caller
//   supplies the bank directly (no window paintbox lookup / no logging).
bool PaintboxDrawShapeDirect(int x, int y, const u8* bank, int shapeIndex,
                             ISurfaceLock& lock, FrameBlitState st);

// gilde.exe 0x41F00C — VIBE_Paintbox_DrawShapeClipped. Sets the blit clip rect
//   (clipX0/Y0/X1/Y1) from the supplied rectangle before drawing — the original
//   calls VIBE_Coord_Push to install the clip window, then AnimationBasic.
bool PaintboxDrawShapeClipped(int x, int y, const u8* bank, int shapeIndex,
                              int clipX0, int clipY0, int clipX1, int clipY1,
                              ISurfaceLock& lock, FrameBlitState st);

} // namespace guild::render
