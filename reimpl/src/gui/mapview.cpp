#include "gui/mapview.h"

#include "gui/window.h"   // Object_AddToWindow (0x41ae10)
#include "gui/zorder.h"   // ZOrder_RemoveObject (0x41aa50)

#include <cmath>

namespace guild::gui {

int g_mapWidth  = 0; // dword_1233440
int g_mapHeight = 0; // dword_1233444

// gilde.exe dword_12334F0 / dword_12334F4 — the last clamped scroll offset, written
// UNCONDITIONALLY at the tail of VIBE_MapView_StepScrollOffset @0x543c7c/0x543c82
// (dword_12334F4 = v4 (clamped Y), dword_12334F0 = v1 (clamped X)).  These are
// write-only in the original binary (the only xref to each is this store; nothing
// reads them), so they are dead scratch — but they ARE an observable side effect of
// every step, so we reproduce them faithfully.
int g_mapLastClampedX = 0; // dword_12334F0
int g_mapLastClampedY = 0; // dword_12334F4

// (mx,my) == (0,0) snapshot of the four Object_AddToWindow(win, y@dx, x@ax, gfx) calls.
const CornerPlacement kMapCornerPlacements[4] = {
    {0, 0},     // obj0: x=mx(0),  y=my(0)
    {0, 418},   // obj1: x=mx(0),  y=0x1A2 (418)
    {0, 120},   // obj2: x=mx(0),  y=0x78  (120)
    {579, 120}, // obj3: x=0x243 (579), y=0x78 (120)
};

// gilde.exe 0x5437d8 — VIBE_MapView_AddCornerObjects.
// Places the four corner objects (gfxBase+0..+3) on `win`, each removed from the
// z-order immediately, in the original call ORDER. (mx,my) is the cursor offset the
// original reads off the packed mouse globals; obj0..2 use x=mx, obj0 uses y=my,
// obj1/obj2 use y=418/120, obj3 uses x=579,y=120.
int MapView_AddCornerObjects(int win, int gfxBase, int cursorX, int cursorY) {
    const int mx = cursorX, my = cursorY;
    int r;

    // obj0: Object_AddToWindow(win, my, mx, gfxBase+0); ZOrder_RemoveObject(r)
    r = Object_AddToWindow(win, static_cast<i16>(my), static_cast<i16>(mx), gfxBase + 0);
    ZOrder_RemoveObject(r);
    // obj1: y = 0x1A2 (418)
    r = Object_AddToWindow(win, static_cast<i16>(0x1A2), static_cast<i16>(mx), gfxBase + 1);
    ZOrder_RemoveObject(r);
    // obj2: y = 0x78 (120)
    r = Object_AddToWindow(win, static_cast<i16>(0x78), static_cast<i16>(mx), gfxBase + 2);
    ZOrder_RemoveObject(r);
    // obj3: x = 0x243 (579), y = 0x78 (120)
    r = Object_AddToWindow(win, static_cast<i16>(0x78), static_cast<i16>(0x243), gfxBase + 3);
    ZOrder_RemoveObject(r);

    // The original tail-returns ZOrder_RemoveObject's eax; our reconstructed
    // ZOrder_RemoveObject (gui/zorder.cpp) is void, so we return the last placed
    // widget index `r` instead (the value passed to the final RemoveObject). The
    // return value is unused by the sole caller (VIBE_MapView_PanelDispatcher
    // @0x5441d0 calls it as a statement), so this is observationally equivalent.
    return r;
}

// gilde.exe 0x5440b4 — VIBE_MapView_ComputeMarkerScreenPos.
// The original reads the marker world coords as ints at +8/+12, the camera origin as
// an int at *(camera+32), and the map size from dword_1233440/dword_1233444; it writes
// the screen coords as floats at +16/+20.  Faithful transcription:
//   v4  = cameraOriginX * 0.5
//   dx  = (worldX - v4) * 5.33
//   dz  = (worldZ - v4) * 5.33
//   bx  = (|dx| / (mapW * 0.5)) * 51.0
//   bz  = (|dz| / (0.5 * mapH)) * 51.0
//   sx  = dx + (dx<0 ? -bx : +bx)
//   sz  = dz + (dz>=0 ? +bz : -bz)
//   screenX = mapW/2 + sx + panX
//   screenY = mapH/2 + sz + panY
int MapView_ComputeMarkerScreenPos(MapMarker& m, int panX, int panY, int cameraOriginX) {
    double v4 = (double)cameraOriginX * kMarkerOriginScale;          // dbl_624058
    float v14 = (float)(((double)(int)m.worldX - v4) * kMarkerWorldScale); // dx
    float v12 = (float)(((double)(int)m.worldZ - v4) * kMarkerWorldScale); // dz

    float v8 = (float)(std::fabs((double)v14) / ((double)g_mapWidth * kMarkerPerspScale));
    float v9 = (float)(std::fabs((double)v12) / (kMarkerPerspScale * (double)g_mapHeight));

    float v10 = v8 * kMarkerPerspBow; // flt_624068
    float v15 = (v14 < 0.0f) ? (v14 - v10) : (v14 + v10);

    float v11 = v9 * kMarkerPerspBow;
    float v13 = (v12 >= 0.0f) ? (v12 + v11) : (v12 - v11);

    int halfH = g_mapHeight / 2;
    m.screenX = (float)((double)(g_mapWidth / 2) + (double)v15 + (double)panX);
    m.screenY = (float)((double)halfH + (double)v13 + (double)panY);
    return halfH;
}

// gilde.exe 0x543bd0 — VIBE_MapView_StepScrollOffset.
// v1/v2 start at the current offset; the cursor nudges by +/-10 within the drag
// dead-band; the held edge keys nudge by +/-10 (mutually exclusive, up>down>left>
// right priority); finally clamp to [0, world-512] x [0, world-360].
int MapView_StepScrollOffset(ScrollOffset& off, int mouseX, int mouseY,
                             int boundLoX, int boundHiX, int boundLoY, int boundHiY,
                             bool edgeUp, bool edgeDown, bool edgeLeft, bool edgeRight) {
    int v1 = off.x; // *(a1+600)
    int v2 = off.y; // *(a1+584)
    int v3 = 0;

    // Horizontal cursor pan: past the low bound move toward it; at/over the high
    // bound move away.
    if (mouseX > boundLoX) {
        if (mouseX >= boundHiX)
            v1 += 10;
    } else {
        v1 -= 10;
    }
    // Vertical cursor pan.
    if (mouseY > boundLoY) {
        if (mouseY >= boundHiY)
            v2 += 10;
    } else {
        v2 -= 10;
    }

    // Held edge-scroll keys (byte_671E28 up, _30 down, _2B left, _2D right).
    if (edgeUp) {
        v2 -= 10;
    } else if (edgeDown) {
        v2 += 10;
    } else if (edgeLeft) {
        v1 -= 10;
    } else if (edgeRight) {
        v1 += 10;
    }

    // Clamp X to [0, world-512].
    if (v1 < 0)
        v1 = 0;
    if (g_mapWidth - kMapViewW < v1)
        v1 = g_mapWidth - kMapViewW;
    // Clamp Y to [0, world-360].
    if (v2 < 0)
        v2 = 0;
    int v4 = g_mapHeight - kMapViewH;
    if (g_mapHeight - kMapViewH >= v2)
        v4 = v2;

    if (v1 != off.x || v4 != off.y) {
        off.x = v1;
        v3 = 1;
        off.y = v4;
    }
    // Unconditional tail stores (0x543c7c/0x543c82): the clamped offset is mirrored
    // into the dead scratch globals regardless of whether anything changed.
    g_mapLastClampedY = v4; // dword_12334F4 = ecx (v4)
    g_mapLastClampedX = v1; // dword_12334F0 = edx (v1)
    return v3;
}

} // namespace guild::gui
