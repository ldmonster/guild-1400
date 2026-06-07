#include "render/surface.h"
#include <cstdlib>
#include <cstring>

namespace guild::render {

// gilde.exe 0x42311c — VIBE_Surface_Create (system-memory branch).
// Original: memcpy a 64-byte template, then for the sw path compute pitch =
// (bpp>>3)*width and allocate pitch*height bytes for the pixel buffer; widthPx
// = pitch/(bpp>>3); clip rect = [0,width) x [0,height); store template[1]/[2]
// (width/height) into the clip-right/bottom fields.
Surface* SurfaceCreate(int width, int height, u8 bpp, const ColorFormat& fmt) {
    Surface* s = (Surface*)std::calloc(1, sizeof(Surface));
    if (!s)
        return nullptr;
    s->width   = width;
    s->height  = height;
    s->bpp     = bpp;
    int bytespp = (int)bpp >> 3;
    s->pitch   = bytespp * width;
    s->pixels  = (u8*)std::calloc((size_t)s->pitch * (size_t)height, 1);
    if (!s->pixels) {
        std::free(s);
        return nullptr;
    }
    s->widthPx = (bytespp != 0) ? s->pitch / bytespp : width;
    s->clipX0 = 0;
    s->clipY0 = 0;
    s->clipX1 = width;
    s->clipY1 = height;
    s->fmt = fmt;
    return s;
}

// gilde.exe 0x4234b0 — VIBE_Surface_Destroy.
int SurfaceDestroy(Surface* s) {
    if (!s)
        return 0;
    if (!s->shared) {
        if (s->pixels) {
            std::free(s->pixels);
            s->pixels = nullptr;
        }
    }
    std::free(s);
    return 1;
}

// gilde.exe 0x423c14 — VIBE_Surface_Clone. Same geometry; copy pixels.
Surface* SurfaceClone(const Surface* s) {
    if (!s)
        return nullptr;
    Surface* d = SurfaceCreate(s->width, s->height, s->bpp, s->fmt);
    if (!d)
        return nullptr;
    std::memcpy(d->pixels, s->pixels, (size_t)s->pitch * (size_t)s->height);
    return d;
}

// gilde.exe 0x423620 — VIBE_Surface_GetCaps (software path: copy caps).
bool SurfaceGetCaps(const Surface* s, u32* outCaps) {
    if (!s || !outCaps)
        return false;
    *outCaps = s->caps;
    return true;
}

// gilde.exe 0x423e5c — VIBE_Surface_SetPixelRgb.
// Register mapping: x@eax, y@edx, g@cl, r@bl, b(arg), surface(arg). Clips to
// [clipX0,clipX1) x [clipY0,clipY1). 8bpp writes luma (b+r+g)/3; 24bpp writes
// R,G,B bytes; 15/16/32bpp writes the packed native value.
int SurfaceSetPixelRgb(Surface* s, int x, int y, u8 r, u8 g, u8 b) {
    if (!s || x < s->clipX0 || x >= s->clipX1 || y < s->clipY0 || y >= s->clipY1)
        return 0;

    int bytespp = (int)s->bpp >> 3;
    u32 packed = PackColor(s->fmt, r, g, b);
    u8 bppv = s->bpp;
    int row = s->widthPx * y;

    if (bppv < 0x10) {
        if (bppv < 8)
            return 1;
        if (bppv <= 8) { // 8 bpp luma
            s->pixels[row + x] = (u8)((b + r + g) / 3);
            return 1;
        }
        if (bppv != 15)
            return 1;
        ((u16*)s->pixels)[row + x] = (u16)packed; // 15 bpp
        return 1;
    }
    if (bppv <= 0x10) { // 16 bpp
        ((u16*)s->pixels)[row + x] = (u16)packed;
        return 1;
    }
    if (bppv < 0x18)
        return 1;
    if (bppv > 0x18) {
        if (bppv != 32)
            return 1;
        ((u32*)s->pixels)[x + row] = packed; // 32 bpp
        return 1;
    }
    // 24 bpp: store R,G,B
    int col = bytespp * x;
    u8* p = s->pixels + row * bytespp + col;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    return 1;
}

// gilde.exe 0x423d74 — VIBE_Surface_GetPixelRgb. out = {R,G,B}.
void SurfaceGetPixelRgb(const Surface* s, int x, int y, u8 out[3]) {
    if (!s) { out[0] = out[1] = out[2] = 0; return; }
    int bytespp = (int)s->bpp >> 3;
    u8 bppv = s->bpp;
    int row = s->widthPx * y;
    if (bppv < 0x10) {
        if (bppv == 15) {
            UnpackColor(s->fmt, ((u16*)s->pixels)[x + row], out[0], out[1], out[2]);
        } else {
            // 8 bpp: original returns the raw byte in all three slots via the
            // luma path's stored value (no palette lookup in this helper).
            u8 v = s->pixels[row + x];
            out[0] = out[1] = out[2] = v;
        }
        return;
    }
    if (bppv <= 0x10) { // 16 bpp
        UnpackColor(s->fmt, ((u16*)s->pixels)[x + row], out[0], out[1], out[2]);
        return;
    }
    if (bppv >= 0x18) {
        if (bppv <= 0x18) { // 24 bpp
            int col = bytespp * x;
            u8* p = (u8*)s->pixels + row * bytespp + col;
            out[0] = p[0];
            out[1] = p[1];
            out[2] = p[2];
        } else if (bppv == 32) {
            u8* p = (u8*)s->pixels + 4 * (x + row);
            out[0] = p[0];
            out[1] = p[1];
            out[2] = p[2];
        }
    }
}

// gilde.exe 0x423ffc — VIBE_Surface_DrawHLine. len pixels from (x,y) rightward.
int SurfaceDrawHLine(Surface* s, int x, int y, int len, u8 r, u8 g, u8 b) {
    int result = 0;
    if (len > 0) {
        int xx = x;
        int end = x + len;
        do {
            result = SurfaceSetPixelRgb(s, xx, y, r, g, b);
            ++xx;
        } while (xx < end);
    }
    return result;
}

// gilde.exe 0x424044 — VIBE_Surface_DrawLine. Bresenham; horizontal/vertical
// fast paths then the general octant loop. Endpoints inclusive on the start,
// the loop walks to the far endpoint exactly as the original.
int SurfaceDrawLine(Surface* s, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b) {
    int x = x0;
    int result = 0;

    if (y0 == y1) { // horizontal
        if (x1 >= x) {
            for (int i = x1 - x; i > 0; ++x) { --i; result = SurfaceSetPixelRgb(s, x, y0, r, g, b); }
        } else {
            int xv = x1;
            for (int j = x - x1; j > 0; ++xv) { --j; result = SurfaceSetPixelRgb(s, xv, y0, r, g, b); }
        }
    } else if (x == x1) { // vertical
        if (y0 <= y1) {
            int yy = y0;
            for (int k = y1 - y0; k > 0; ) { --k; result = SurfaceSetPixelRgb(s, x1, yy, r, g, b); ++yy; }
        } else {
            int yy = y1;
            for (int m = y0 - y1; m > 0; ++yy) { --m; result = SurfaceSetPixelRgb(s, x1, yy, r, g, b); }
        }
    } else { // general Bresenham
        int dx, sx;
        if (x >= x1) { dx = x - x1; sx = -1; } else { dx = x1 - x; sx = 1; }
        int dy, sy;
        if (y0 >= y1) { dy = y0 - y1; sy = -1; } else { dy = y1 - y0; sy = 1; }

        int yy = y0;
        SurfaceSetPixelRgb(s, x, yy, r, g, b);
        if (dx <= dy) {
            int err = 2 * dx - dy;
            while (yy != y1) {
                yy += sy;
                if (err < 0) { err += 2 * dx; }
                else { x += sx; err += 2 * (dx - dy); }
                result = SurfaceSetPixelRgb(s, x, yy, r, g, b);
            }
        } else {
            int err = 2 * dy - dx;
            while (x != x1) {
                x += sx;
                if (err < 0) { err += 2 * dy; }
                else { yy += sy; err += 2 * (dy - dx); }
                result = SurfaceSetPixelRgb(s, x, yy, r, g, b);
            }
        }
    }
    return result;
}

// gilde.exe 0x4242d4 — VIBE_Surface_DrawRectOutline.
// Register mapping: x@eax, y@edx, h@ecx, w@ebx, surface(arg). Outer loop walks
// the height drawing the left (x) and right (x+w-1) vertical edges; inner loop
// walks the width drawing the top (y) and bottom (y+h-1) horizontal edges.
// Skips entirely when b==0 (the original gates on a7 != 0). Note the original
// requires h>=1 to enter and w>0 for the horizontal edges.
int SurfaceDrawRectOutline(Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    int result = 0;
    if (b) {
        if (h >= 1) {
            for (int row = y; row < y + h; ++row) {
                SurfaceSetPixelRgb(s, x, row, r, g, b);
                result = SurfaceSetPixelRgb(s, x + w - 1, row, r, g, b);
            }
            if (w > 0) {
                for (int colx = x; colx < x + w; ++colx) {
                    SurfaceSetPixelRgb(s, colx, y, r, g, b);
                    result = SurfaceSetPixelRgb(s, colx, h + y - 1, r, g, b);
                }
            }
        }
    }
    return result;
}

// Software path of VIBE_Surface_ColorFill (0x423b6c): the original zero-fills
// the whole buffer (VIBE_Light_SetGrayColorThunk(0, pitch*height) memsets 0).
// We expose the colour for convenience; the byte-faithful default is r=g=b=0.
void SurfaceColorFill(Surface* s, u8 r, u8 g, u8 b) {
    if (!s)
        return;
    if (r == 0 && g == 0 && b == 0) {
        std::memset(s->pixels, 0, (size_t)s->pitch * (size_t)s->height);
        return;
    }
    for (int yy = 0; yy < s->height; ++yy)
        for (int xx = 0; xx < s->width; ++xx)
            SurfaceSetPixelRgb(s, xx, yy, r, g, b);
}

} // namespace guild::render
