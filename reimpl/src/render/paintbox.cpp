#include "render/paintbox.h"

namespace guild::render {

// gilde.exe 0x41e920 — VIBE_Paintbox_DrawScaledRegion.
// The original clips against the paintbox surface dimensions (stored as 16.16
// fixed-point, read via >>16) inset by the brush size, then plots a brush-shaped
// cluster of pixels. Brush size comes from the global byte_62D28C (here pb.brush).
void PaintboxDrawScaledRegion(PaintboxState& pb, int x, int y, u8 r, u8 g, u8 b) {
    Surface* s = pb.surface;
    if (!s)
        return;
    int border = pb.border;
    int w = s->width;
    int h = s->height;
    // clip: border <= x <= w-border && border <= y <= h-border
    if (!(border <= x && (w - border) >= x && y >= border && y <= (h - border)))
        return;

    if (pb.brush == 0) {
        SurfaceSetPixelRgb(s, x, y, r, g, b);
        return;
    }

    int ym1 = y - 1, yp1 = y + 1, xp1 = x + 1, xm1 = x - 1;
    if (pb.brush <= 1) { // 5-pixel plus
        SurfaceSetPixelRgb(s, xm1, y,   r, g, b);
        SurfaceSetPixelRgb(s, xp1, y,   r, g, b);
        SurfaceSetPixelRgb(s, x,   yp1, r, g, b);
        SurfaceSetPixelRgb(s, x,   ym1, r, g, b);
        SurfaceSetPixelRgb(s, x,   y,   r, g, b);
    } else if (pb.brush == 2) { // 13-pixel diamond
        SurfaceSetPixelRgb(s, xm1,   y,     r, g, b);
        SurfaceSetPixelRgb(s, xp1,   y,     r, g, b);
        SurfaceSetPixelRgb(s, x - 2, y,     r, g, b);
        SurfaceSetPixelRgb(s, x + 2, y,     r, g, b);
        SurfaceSetPixelRgb(s, x,     y,     r, g, b);
        SurfaceSetPixelRgb(s, x,     yp1,   r, g, b);
        SurfaceSetPixelRgb(s, x,     ym1,   r, g, b);
        SurfaceSetPixelRgb(s, x,     y + 2, r, g, b);
        SurfaceSetPixelRgb(s, x,     y - 2, r, g, b);
        SurfaceSetPixelRgb(s, xp1,   yp1,   r, g, b);
        SurfaceSetPixelRgb(s, xm1,   ym1,   r, g, b);
        SurfaceSetPixelRgb(s, xm1,   yp1,   r, g, b);
        SurfaceSetPixelRgb(s, xp1,   ym1,   r, g, b);
    }
}

// gilde.exe 0x41ec40 — VIBE_Paintbox_DrawLine. Bresenham; same structure as
// VIBE_Surface_DrawLine but plotting through DrawScaledRegion (brush-aware).
void PaintboxDrawLine(PaintboxState& pb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b) {
    int x = x0;
    if (y0 == y1) {
        if (x1 >= x) { for (int i = x1 - x; i > 0; ++x) { --i; PaintboxDrawScaledRegion(pb, x, y0, r, g, b); } }
        else { int xv = x1; for (int j = x - x1; j > 0; ++xv) { --j; PaintboxDrawScaledRegion(pb, xv, y0, r, g, b); } }
    } else if (x == x1) {
        if (y0 <= y1) { int yy = y0; for (int k = y1 - y0; k > 0; ) { --k; PaintboxDrawScaledRegion(pb, x1, yy, r, g, b); ++yy; } }
        else { int yy = y1; for (int m = y0 - y1; m > 0; ++yy) { --m; PaintboxDrawScaledRegion(pb, x1, yy, r, g, b); } }
    } else {
        int dx, sx;
        if (x >= x1) { dx = x - x1; sx = -1; } else { dx = x1 - x; sx = 1; }
        int dy, sy;
        if (y0 >= y1) { dy = y0 - y1; sy = -1; } else { dy = y1 - y0; sy = 1; }
        int yy = y0;
        PaintboxDrawScaledRegion(pb, x, yy, r, g, b);
        if (dx <= dy) {
            int err = 2 * dx - dy;
            while (yy != y1) {
                yy += sy;
                if (err < 0) err += 2 * dx; else { x += sx; err += 2 * (dx - dy); }
                PaintboxDrawScaledRegion(pb, x, yy, r, g, b);
            }
        } else {
            int err = 2 * dy - dx;
            while (x != x1) {
                x += sx;
                if (err < 0) err += 2 * dy; else { yy += sy; err += 2 * (dy - dx); }
                PaintboxDrawScaledRegion(pb, x, yy, r, g, b);
            }
        }
    }
}

// gilde.exe 0x41ee9c — VIBE_Paintbox_Clear.
void PaintboxClear(PaintboxState& pb) {
    if (pb.surface)
        SurfaceColorFill(pb.surface, 0, 0, 0);
}

} // namespace guild::render
