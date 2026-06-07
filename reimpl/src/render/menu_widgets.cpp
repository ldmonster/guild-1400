// guild::render — 3-slice button + window-frame paint helpers. See header for the
// confirmed _BUTTON_RED slice layout and the frame composition rules.
#include "render/menu_widgets.h"

#include <algorithm>

namespace guild::render {
namespace {

inline bool InBounds(int x, int y, int W, int H) {
    return x >= 0 && y >= 0 && x < W && y < H;
}

// Blit a decoded shape 1:1 at (dx,dy) into the WxH ARGB buffer, honouring
// per-pixel transparency (A==0 skipped) and clipping.  Returns pixels written.
int BlitShape(u32* dst, int W, int H, int dx, int dy, const DecodedShape& s) {
    if (!dst || s.width <= 0 || s.height <= 0) return 0;
    int drawn = 0;
    for (int row = 0; row < s.height; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= H) continue;
        const u32* src = s.argb.data() + (std::size_t)row * s.width;
        u32* drow = dst + (std::size_t)py * W;
        for (int col = 0; col < s.width; ++col) {
            const u32 px = src[col];
            if ((px & 0xFF000000u) == 0u) continue;  // transparent
            const int pxx = dx + col;
            if (pxx < 0 || pxx >= W) continue;
            drow[pxx] = px;
            ++drawn;
        }
    }
    return drawn;
}

// Blit a horizontal sub-span [sx0,sx0+spanW) of shape `s` (full height) at the
// destination column dx — used to tile/clamp the stretchable centre face.  Source
// column wraps within the shape width when spanW exceeds the shape.
int BlitShapeColumns(u32* dst, int W, int H, int dx, int dy,
                     const DecodedShape& s, int spanW) {
    if (!dst || s.width <= 0 || s.height <= 0 || spanW <= 0) return 0;
    int drawn = 0;
    for (int i = 0; i < spanW; ++i) {
        const int sxc = i % s.width;          // tile the centre across the span
        const int pxx = dx + i;
        if (pxx < 0 || pxx >= W) continue;
        for (int row = 0; row < s.height; ++row) {
            const int py = dy + row;
            if (py < 0 || py >= H) continue;
            const u32 px = s.argb[(std::size_t)row * s.width + sxc];
            if ((px & 0xFF000000u) == 0u) continue;
            dst[(std::size_t)py * W + pxx] = px;
            ++drawn;
        }
    }
    return drawn;
}

// Inert fallback: filled rect (fill) + 1px outline (line).
void FillRect(u32* dst, int W, int H, int x, int y, int w, int h, u32 fill,
              u32 line) {
    if (!dst) return;
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= H) continue;
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= W) continue;
            const bool edge = (row == 0 || row == h - 1 || col == 0 || col == w - 1);
            dst[(std::size_t)py * W + px] = edge ? line : fill;
        }
    }
}

} // namespace

ButtonSliceInfo DrawThreeSliceButton(u32* dst, int W, int H, int x, int y,
                                     int widthPx, const GfxArchive& arc,
                                     int gfxRecord, bool pressed) {
    ButtonSliceInfo info;
    if (!dst || widthPx <= 0) return info;

    // Slice shape indices: up-state {0,1,2} / down-state {3,4,5}.
    const int base = pressed ? 3 : 0;
    DecodedShape capL, capR, centre;
    const bool haveArt =
        arc.ok() && gfxRecord >= 0 && arc.ShapeCount(gfxRecord) >= base + 3 &&
        arc.DecodeShape(gfxRecord, base + 0, capL) &&
        arc.DecodeShape(gfxRecord, base + 1, capR) &&
        arc.DecodeShape(gfxRecord, base + 2, centre);

    if (!haveArt) {
        // Inert fallback: dark-red fill, light outline (matches the red button).
        const int h = 33;  // _BUTTON_RED nominal height
        FillRect(dst, W, H, x, y, widthPx, h,
                 pressed ? 0xFF601010u : 0xFF902020u, 0xFFD0D0D0u);
        info.real = false;
        info.capLeft = info.capRight = std::min(kButtonCapWidth, widthPx / 2);
        info.center = std::max(0, widthPx - 2 * info.capLeft);
        info.height = h;
        return info;
    }

    const int h = centre.height;
    const int cl = capL.width;
    const int cr = capR.width;

    // If the button is too narrow for both caps, clamp the caps and skip centre.
    int leftW = cl, rightW = cr, centreSpan = widthPx - cl - cr;
    if (centreSpan < 0) {
        // Caps overlap; draw left cap (clipped), then right cap flush — no centre.
        leftW = std::min(cl, widthPx);
        rightW = std::min(cr, widthPx - leftW);
        centreSpan = 0;
    }

    // Left cap at x.
    BlitShape(dst, W, H, x, y, capL);
    // Centre face stretched/tiled across the middle gap.
    if (centreSpan > 0)
        BlitShapeColumns(dst, W, H, x + cl, y, centre, centreSpan);
    // Right cap flush to the right edge.
    BlitShape(dst, W, H, x + widthPx - cr, y, capR);

    info.real = true;
    info.capLeft = leftW;
    info.capRight = rightW;
    info.center = centreSpan;
    info.height = h;
    return info;
}

FrameDrawInfo DrawWindowFrame(u32* dst, int W, int H, int x, int y, int w, int h,
                              const GfxArchive& arc, int frameRecord) {
    FrameDrawInfo info;
    if (!dst || w <= 0 || h <= 0) return info;

    const int shapes = (arc.ok() && frameRecord >= 0)
                           ? arc.ShapeCount(frameRecord)
                           : 0;

    // -------- inert fallback: plain filled rect + outline --------
    auto inert = [&]() {
        FillRect(dst, W, H, x, y, w, h, 0xFF202830u, 0xFFB0B0B0u);
        info.real = false;
        info.thickness = 1;
        info.tiled = false;
        return info;
    };
    if (shapes <= 0) return inert();

    // -------- tiled border path (e.g. _WIN_BORDER #0: 8x8 corners + edges) --------
    if (shapes >= 9) {
        // _WIN_BORDER layout (dims from real gilde.gfx):
        //   0 TL  1 TR  2 BL  3 BR (8x8 corners)
        //   5 top/bottom horizontal edge (8x2)
        //   9..12 left/right vertical edge (2x8)
        // We blit the four 8x8 corners and tile the edge strips between them.
        DecodedShape tl, tr, bl, br, hEdge, vEdge;
        const bool okCorners =
            arc.DecodeShape(frameRecord, 0, tl) &&
            arc.DecodeShape(frameRecord, 1, tr) &&
            arc.DecodeShape(frameRecord, 2, bl) &&
            arc.DecodeShape(frameRecord, 3, br) &&
            arc.DecodeShape(frameRecord, 5, hEdge) &&
            arc.DecodeShape(frameRecord, 9, vEdge);
        if (okCorners && tl.width > 0 && tl.height > 0) {
            const int cw = tl.width, ch = tl.height;
            // corners
            BlitShape(dst, W, H, x, y, tl);
            BlitShape(dst, W, H, x + w - tr.width, y, tr);
            BlitShape(dst, W, H, x, y + h - bl.height, bl);
            BlitShape(dst, W, H, x + w - br.width, y + h - br.height, br);
            // top + bottom horizontal edges (tile hEdge across the inner span)
            for (int ex = x + cw; ex < x + w - cw; ex += hEdge.width) {
                BlitShape(dst, W, H, ex, y, hEdge);
                BlitShape(dst, W, H, ex, y + h - hEdge.height, hEdge);
            }
            // left + right vertical edges (tile vEdge down the inner span)
            for (int ey = y + ch; ey < y + h - ch; ey += vEdge.height) {
                BlitShape(dst, W, H, x, ey, vEdge);
                BlitShape(dst, W, H, x + w - vEdge.width, ey, vEdge);
            }
            info.real = true;
            info.tiled = true;
            info.thickness = std::max({cw, ch, hEdge.height, vEdge.width});
            return info;
        }
        // multi-shape but not the expected layout -> fall through to ring path.
    }

    // -------- single-panel ring path (e.g. _MAIN_MENU_RAHMEN #1776) --------
    DecodedShape panel;
    if (!arc.DecodeShape(frameRecord, 0, panel) ||
        panel.width <= 0 || panel.height <= 0) {
        return inert();
    }

    // Border thickness sampled from the panel: clamp to a sane fraction of the
    // panel so we always pull real border pixels (the decorated rim).
    const int t = std::max(1, std::min({panel.width / 6, panel.height / 6, 24}));

    // Four corner blocks (t x t) taken from the panel's four corners.
    auto blitCorner = [&](int sx, int sy, int destX, int destY) {
        for (int row = 0; row < t; ++row) {
            const int py = destY + row;
            if (py < 0 || py >= H) continue;
            for (int col = 0; col < t; ++col) {
                const u32 px = panel.argb[(std::size_t)(sy + row) * panel.width +
                                          (sx + col)];
                if ((px & 0xFF000000u) == 0u) continue;
                const int pxx = destX + col;
                if (pxx < 0 || pxx >= W) continue;
                dst[(std::size_t)py * W + pxx] = px;
            }
        }
    };
    blitCorner(0, 0, x, y);                                   // TL
    blitCorner(panel.width - t, 0, x + w - t, y);            // TR
    blitCorner(0, panel.height - t, x, y + h - t);          // BL
    blitCorner(panel.width - t, panel.height - t, x + w - t, y + h - t); // BR

    // Top + bottom edges: sample the panel's top/bottom border rows, tiled across.
    for (int col = 0; col < w - 2 * t; ++col) {
        const int destX = x + t + col;
        const int srcX = t + (col % std::max(1, panel.width - 2 * t));
        for (int row = 0; row < t; ++row) {
            // top
            u32 ptop = panel.argb[(std::size_t)row * panel.width + srcX];
            int py = y + row;
            if (InBounds(destX, py, W, H) && (ptop & 0xFF000000u))
                dst[(std::size_t)py * W + destX] = ptop;
            // bottom
            u32 pbot = panel.argb[(std::size_t)(panel.height - t + row) * panel.width
                                  + srcX];
            py = y + h - t + row;
            if (InBounds(destX, py, W, H) && (pbot & 0xFF000000u))
                dst[(std::size_t)py * W + destX] = pbot;
        }
    }
    // Left + right edges: sample the panel's left/right border columns, tiled down.
    for (int row = 0; row < h - 2 * t; ++row) {
        const int destY = y + t + row;
        const int srcY = t + (row % std::max(1, panel.height - 2 * t));
        for (int col = 0; col < t; ++col) {
            u32 pleft = panel.argb[(std::size_t)srcY * panel.width + col];
            int pxx = x + col;
            if (InBounds(pxx, destY, W, H) && (pleft & 0xFF000000u))
                dst[(std::size_t)destY * W + pxx] = pleft;
            u32 pright = panel.argb[(std::size_t)srcY * panel.width +
                                    (panel.width - t + col)];
            pxx = x + w - t + col;
            if (InBounds(pxx, destY, W, H) && (pright & 0xFF000000u))
                dst[(std::size_t)destY * W + pxx] = pright;
        }
    }

    info.real = true;
    info.tiled = false;
    info.thickness = t;
    return info;
}

} // namespace guild::render
