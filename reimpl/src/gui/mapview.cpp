#include "gui/mapview.h"

#include <cmath>

namespace guild::gui {

int g_mapWidth  = 0; // dword_1233440
int g_mapHeight = 0; // dword_1233444

const CornerPlacement kMapCornerPlacements[4] = {
    {0, 0},     // VIBE_Object_AddToWindow(a1,   0,   0, a2+0)
    {0, 418},   // VIBE_Object_AddToWindow(a1, 418,   0, a2+1)
    {0, 120},   // VIBE_Object_AddToWindow(a1, 120,   0, a2+2)
    {579, 120}, // VIBE_Object_AddToWindow(a1, 120, 579, a2+3)
};

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
    return v3;
}

} // namespace guild::gui
