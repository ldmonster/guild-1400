#include "render/animation_decode.h"

#include <cstring>

namespace guild::render {

namespace {
inline u32 GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }
inline u16 GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }
inline i32 GetI32(const u8* b) { i32 v; std::memcpy(&v, b, 4); return v; }

// Read a u32 from a byte cursor and advance it by 4.
inline u32 Take32(const u8*& p) { u32 v = GetU32(p); p += 4; return v; }
} // namespace

// gilde.exe 0x5D7420 — VIBE_FrameData_Interpolate. Uncompressed block blit with
// X/Y clipping and three raster modes (frame +0x0D). Source is a flat width*height
// u16 array at frame +0x32; pixels equal to 0 are transparent (skipped).
u16 FrameDataInterpolate(int x, int y, const u8* frame, const FrameBlitState& st) {
    int width  = GetU16(frame + frame_off::kWidth);    // v4 = *(frame+6)
    int height = GetU16(frame + frame_off::kHeight);   // v29 = *(frame+10)
    int mode   = frame[frame_off::kMode];              // *(frame+13)

    int leftSkip  = 0;  // v30 (clipped-off columns on the left)
    int rightSkip = 0;  // v33 (clipped-off columns on the right)
    int topSkip   = 0;  // v6  (clipped-off rows on the top)
    bool clipped  = false;  // v5

    // ---- X clip (against [clipX0, clipX1)) ----
    if (x < st.clipX0) {
        leftSkip = st.clipX0 - x;
        x = st.clipX0;
        width -= leftSkip;
        clipped = true;
    } else {
        if (x > st.clipX1)
            return (u16)x;                  // original returns a1 (the x register)
        int xEnd = x + width;
        if (xEnd < st.clipX0)
            return (u16)x;
        if (xEnd > st.clipX1) {
            clipped = true;
            rightSkip = xEnd - st.clipX1;
            width = st.clipX1 - x;
        }
    }

    // ---- Y clip (against [clipY0, clipY1)) ----
    if (y >= st.clipY0) {
        if (y > st.clipY1 || y + height < st.clipY0)
            return (u16)x;                  // a1 (possibly already left-clipped)
        if (y + height > st.clipY1) {
            clipped = true;
            height = st.clipY1 - y;
        }
    } else {
        topSkip = st.clipY0 - y;
        height -= topSkip;
        y = st.clipY0;
        clipped = true;
    }

    u16* d = st.dest + (static_cast<ptrdiff_t>(st.destStridePx) * y + x);  // v9
    const u8* srcBase = frame + frame_off::kPayload;                       // v10 (u16*)
    const int srcW = GetU16(frame + frame_off::kWidth);                    // source row width (u16s)

    if (clipped) {
        // Source starts topSkip full rows down, then leftSkip into the row.
        const u8* rowSrc = srcBase + 2 * (static_cast<ptrdiff_t>(topSkip) * srcW);
        for (int r = 0; r < height; ++r) {
            const u8* s = rowSrc + 2 * leftSkip;       // &i[v32]
            for (int c = 0; c < width; ++c) {
                u16 sp = GetU16(s);
                if (sp) {
                    if (mode == 0)        *d = sp;
                    else if (mode == 2)   *d = (u16)(((*d) >> 1) & st.darkMask16);
                    else if (mode == 3 && st.remapTable) *d = st.remapTable[*d];
                }
                ++d;
                s += 2;
            }
            d += st.destStridePx - width;               // v9 += stride - v4
            rowSrc = s + 2 * rightSkip;                  // i = &j[v31]
        }
    } else {
        const u8* s = srcBase;                           // v10 walked contiguously
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                u16 sp = GetU16(s);
                if (sp) {
                    if (mode == 0)        *d = sp;
                    else if (mode == 2)   *d = (u16)(((*d) >> 1) & st.darkMask16);
                    else if (mode == 3 && st.remapTable) *d = st.remapTable[*d];
                }
                ++d;
                s += 2;
            }
            d += st.destStridePx - width;
        }
    }

    return GetU16(frame + frame_off::kHeight);
}

// ---------------------------------------------------------------------------
// RLE blit helpers.  A row's stream is: i32 runCount, then runCount runs, each
//   { i32 skipBytes; i32 nPixels; u16 px[nPixels] }
// skipBytes advances the *byte* destination pointer; nPixels pixels follow.
// The per-row offset table at frame+rowTableOff gives the byte offset (relative
// to the frame base) where each row's runCount begins.
//
// All four FrameTable_* originals share the same Y-clip preamble:
//   - require height(+0x0A)+y > clipY0  &&  y < clipY1
//   - destStep (between scanlines) = 2*(destStridePx - width)
//   - rows = height; if y < clipY0, skip (clipY0-y) rows via the offset table and
//     reduce the row count; if y+height > clipY1, reduce rows by the overflow.
//   - first row's runs begin at frame + rowTable[skippedRows].
// We factor that preamble; the per-mode inner loops differ per function.
// ---------------------------------------------------------------------------
namespace {
struct RlePreamble {
    bool   draw = false;
    int    rows = 0;
    int    width = 0;
    int    destStep = 0;          // pixel step between scanlines (stride - width)
    u16*   dst = nullptr;
    const u8* cursor = nullptr;   // -> first row's runCount
};

RlePreamble RleSetup(int x, int y, const u8* frame, const FrameBlitState& st) {
    RlePreamble p;
    int width  = GetU16(frame + frame_off::kWidth);   // *(frame+6)
    int height = GetU16(frame + frame_off::kHeight);  // *(frame+10)
    if (!(height + y > st.clipY0 && y < st.clipY1))
        return p;

    p.width    = width;
    p.destStep = st.destStridePx - width;             // (destStridePx - width); *2 applied at use

    int rowTableOff = (int)GetU32(frame + frame_off::kRowTableOff);  // *(frame+42)
    const u8* rowTable = frame + rowTableOff;

    int rows = height;             // dword_64AAxx (visible-row count)
    // gilde.exe 0x5FC200/0x5FBC10/0x5FBFD4/0x5FBB24: the first-row byte offset
    // DEFAULTS to 50 (the payload start: `dword_64AAC0 = 50`); the row table is
    // consulted ONLY when the top is clipped (`if (clipY0 - y > 0) dword_64AAC0 =
    // *(rowTable + 4*top)`). It is NOT read in the unclipped case.
    int firstByteOff = (int)frame_off::kPayload;  // 50
    int yy = y;
    int top = st.clipY0 - y;
    if (top > 0) {
        firstByteOff = (int)GetU32(rowTable + 4 * top);   // *(rowTable + 4*top)
        rows -= top;
        yy = st.clipY0;
    }
    int bottom = (y + height) - st.clipY1;                 // overflow past clipY1
    if (bottom > 0)
        rows -= bottom;
    if (rows <= 0)
        return p;

    p.rows   = rows;
    p.dst    = st.dest + (static_cast<ptrdiff_t>(st.destStridePx) * yy + x);
    p.cursor = frame + firstByteOff;
    p.draw   = true;
    return p;
}
} // namespace

// gilde.exe 0x5FC200 — VIBE_FrameTable_Validate. RLE blit, Y-clipped only.
void FrameTableValidate(int x, int y, const u8* frame, const FrameBlitState& st) {
    RlePreamble p = RleSetup(x, y, frame, st);
    if (!p.draw)
        return;
    int mode = frame[frame_off::kMode];
    const int step = 2 * p.destStep;        // byte step is applied to a u16* below as pixels
    const u8* c = p.cursor;
    u16* d = p.dst;

    for (int r = 0; r < p.rows; ++r) {
        int runCount = (int)Take32(c);
        for (int run = 0; run < runCount; ++run) {
            int skipBytes = (int)Take32(c);
            u32 n = Take32(c);
            d = (u16*)((u8*)d + skipBytes);            // advance dst by skip BYTES
            if (n) {
                switch (mode) {
                case 1:   // 50% additive blend: dst = src + ((dst>>1)&mask)
                case 4: {
                    if (n & 1) { *d = (u16)(GetU16(c) + (((*d) >> 1) & 0x3DEF)); c += 2; ++d; }
                    for (u32 i = n >> 1; i; --i) {
                        u32 dd; std::memcpy(&dd, d, 4);
                        u32 ss = GetU32(c);
                        dd = ss + ((dd >> 1) & 0x3DEFBDEFu);
                        std::memcpy(d, &dd, 4);
                        d += 2; c += 4;
                    }
                    break;
                }
                case 2: {  // darken in place; source words are consumed (skipped)
                    const u8* save = c;
                    if (n & 1) { *d = (u16)(st.darkMask16 & ((*d) >> 1)); ++d; }
                    for (u32 i = n >> 1; i; --i) {
                        u32 dd; std::memcpy(&dd, d, 4);
                        dd = st.darkMask32 & (dd >> 1);
                        std::memcpy(d, &dd, 4);
                        d += 2;
                    }
                    // gilde.exe 0x5FC200 case 2: the source skip re-reads the
                    // run count as a 16-bit word (`mov cx, [esi-4]; shl; add`),
                    // so only the LOW 16 bits of n advance the cursor.
                    c = save + 2u * (n & 0xFFFFu);
                    break;
                }
                case 3: {  // palette remap of the existing dst pixels
                    for (u32 i = 0; i < n; ++i) {
                        u16 dv = *d;
                        *d++ = st.remapTable ? st.remapTable[dv] : dv;
                    }
                    c += 2u * (n & 0xFFFFu);  // 16-bit count re-read (see case 2)
                    break;
                }
                case 5: {  // solid fill
                    if (n & 1) { *d++ = st.fillColor16; }
                    for (u32 i = n >> 1; i; --i) {
                        u16 fc = st.fillColor16;
                        std::memcpy(d, &fc, 2); std::memcpy(d + 1, &fc, 2);
                        d += 2;
                    }
                    c += 2u * (n & 0xFFFFu);  // 16-bit count re-read (see case 2)
                    break;
                }
                default: { // mode 0: straight copy
                    if (n & 1) { *d++ = GetU16(c); c += 2; }
                    u32 w = n >> 1;
                    std::memcpy(d, c, 4u * w);
                    c += 4u * w; d += 2u * w;
                    break;
                }
                }
            }
        }
        d += step / 2;   // next scanline (step is in bytes; /2 -> pixels)
    }
}

// gilde.exe 0x5FBC10 — VIBE_FrameTable_Next. RLE blit, X clipped per pixel against
// [clipX0, clipX1) (tracked by a running screen-X counter) plus Y clipped.
void FrameTableNext(int x, int y, const u8* frame, const FrameBlitState& st) {
    RlePreamble p = RleSetup(x, y, frame, st);
    if (!p.draw)
        return;
    int mode = frame[frame_off::kMode];
    const int stepPx = p.destStep;       // pixels between scanlines
    const u8* c = p.cursor;
    u16* d = p.dst;

    int xCur;                            // dword_64AAA8 (running screen X)
    const int xStart = x;                // dword_64AAB0

    for (int r = 0; r < p.rows; ++r) {
        xCur = xStart;
        int runCount = (int)Take32(c);
        for (int run = 0; run < runCount; ++run) {
            int skipBytes = GetI32(c);
            u32 n = GetU32(c + 4);
            d = (u16*)((u8*)d + skipBytes);
            xCur += (int)((u32)skipBytes >> 1);  // shr (UNSIGNED) skip/2 pixels
            c += 8;
            if (mode == 0 || mode > 5) {
                for (u32 i = 0; i < n; ++i) {
                    if (xCur > st.clipX0 && xCur < st.clipX1)
                        *d = GetU16(c);
                    ++d; c += 2; ++xCur;
                }
            } else if (mode == 2) {
                if (n & 1) { *d = (u16)(st.darkMask16 & ((*d) >> 1)); ++d; }
                for (u32 i = n >> 1; i; --i) {
                    if (xCur > st.clipX0 && xCur < st.clipX1) {
                        u32 dd; std::memcpy(&dd, d, 4);
                        dd = st.darkMask32 & (dd >> 1);
                        std::memcpy(d, &dd, 4);
                    }
                    d += 2; ++xCur;
                }
                c += 2u * (n & 0xFFFFu);  // 16-bit count re-read (mov cx,[esi-4])
            } else if (mode == 3) {
                for (u32 i = 0; i < n; ++i) {
                    if (xCur > st.clipX0 && xCur < st.clipX1) {
                        u16 dv = *d;
                        *d = st.remapTable ? st.remapTable[dv] : dv;
                    }
                    ++xCur; ++d;
                }
                c += 2u * (n & 0xFFFFu);  // 16-bit count re-read
            } else if (mode == 5) {
                for (u32 i = 0; i < n; ++i) {
                    if (xCur > st.clipX0 && xCur < st.clipX1)
                        *d = st.fillColor16;
                    ++xCur; ++d;
                }
                c += 2u * (n & 0xFFFFu);  // 16-bit count re-read
            } else { // modes 1 and 4: additive blend (no X-clip in the original)
                if (n & 1) { *d = (u16)(GetU16(c) + (((*d) >> 1) & 0x3DEF)); c += 2; ++d; }
                for (u32 i = n >> 1; i; --i) {
                    u32 dd; std::memcpy(&dd, d, 4);
                    u32 ss = GetU32(c);
                    dd = ss + ((dd >> 1) & 0x3DEFBDEFu);
                    std::memcpy(d, &dd, 4);
                    d += 2; c += 4;
                }
            }
        }
        d += stepPx;
    }
}

// gilde.exe 0x5FBFD4 — VIBE_FrameTable_Bounds. RLE blit, Y-clipped, modes 0/1/2/3
// plus a default path that reads 8bpp indices and looks them up in indexTable.
void FrameTableBounds(int x, int y, const u8* frame, const FrameBlitState& st) {
    RlePreamble p = RleSetup(x, y, frame, st);
    if (!p.draw)
        return;
    int mode = frame[frame_off::kMode];
    const int stepPx = p.destStep;
    const u8* c = p.cursor;
    u16* d = p.dst;

    for (int r = 0; r < p.rows; ++r) {
        int runCount = (int)Take32(c);
        for (int run = 0; run < runCount; ++run) {
            if (mode == 1) {
                int skipBytes = GetI32(c); u32 n = GetU32(c + 4); c += 8;
                d = (u16*)((u8*)d + skipBytes);
                if (n) {
                    if (n & 1) { *d = (u16)(GetU16(c) + (((*d) >> 1) & 0x3DEF)); c += 2; ++d; }
                    for (u32 i = n >> 1; i; --i) {
                        u32 dd; std::memcpy(&dd, d, 4); u32 ss = GetU32(c);
                        dd = ss + ((dd >> 1) & 0x3DEFBDEFu);
                        std::memcpy(d, &dd, 4); d += 2; c += 4;
                    }
                }
            } else if (mode == 2) {
                // gilde.exe 0x5FBFD4 case 2: the darken masks are the IMMEDIATES
                // 0x3DEF / 0x3DEFBDEF here (NOT the word_1406944/dword_1406930
                // globals that Validate/Next use).
                int skipBytes = GetI32(c); u32 n = GetU32(c + 4); c += 8;
                d = (u16*)((u8*)d + skipBytes);
                if (n) {
                    if (n & 1) { *d = (u16)(((*d) >> 1) & 0x3DEF); ++d; }
                    for (u32 i = n >> 1; i; --i) {
                        u32 dd; std::memcpy(&dd, d, 4);
                        dd = (dd >> 1) & 0x3DEFBDEFu;
                        std::memcpy(d, &dd, 4); d += 2;
                    }
                    c += 2u * (n & 0xFFFFu);  // 16-bit count re-read
                }
            } else if (mode == 3) {
                int skipBytes = GetI32(c); u32 n = GetU32(c + 4); c += 8;
                d = (u16*)((u8*)d + skipBytes);
                if (n) {
                    for (u32 i = 0; i < n; ++i) {
                        u16 dv = *d;
                        *d++ = st.remapTable ? st.remapTable[dv] : dv;
                    }
                    c += 2u * (n & 0xFFFFu);  // 16-bit count re-read
                }
            } else {
                // default (incl. mode 0): 8bpp index -> 16bpp via indexTable; here
                // a run carries `n` BYTE indices (1 byte each). The run's skip
                // field counts PIXELS in THIS path: gilde.exe 0x5fc0c6 doubles it
                // (`mov ecx,[esi]; shl ecx,1; add edi,ecx`), unlike modes 1/2/3
                // which add the raw byte skip.
                int skipBytes = GetI32(c); u32 n = GetU32(c + 4); c += 8;
                d = (u16*)((u8*)d + 2 * (ptrdiff_t)skipBytes);
                for (u32 i = 0; i < n; ++i) {
                    u8 idx = *c++;
                    *d++ = st.indexTable ? st.indexTable[idx] : idx;
                }
            }
        }
        d += stepPx;
    }
}

// gilde.exe 0x5FBB24 — VIBE_FrameTable_Index. The plain (no X-clip) copy path.
// UNLIKE the other three blitters this one addresses an 8bpp destination: the
// binary computes dest = surface + y*stride + x with NO doubling anywhere
// (`v8 = a1 + a4; v15 = v9 * dword_64A1C8 + v8`) and the between-row step is
// stride - width BYTES (`LOWORD(v7) = dword_64A1C8 - a3[3]`, not shifted).
// Pixels are BYTES here (odd: one byte; pairs: a 16-bit word = 2 pixels).
void FrameTableIndex(int x, int y, const u8* frame, const FrameBlitState& st) {
    int width  = GetU16(frame + frame_off::kWidth);   // a3[3]
    int height = GetU16(frame + frame_off::kHeight);  // a3[5]
    if (!(height + y > st.clipY0 && y < st.clipY1))
        return;

    const int stepBytes = st.destStridePx - width;    // v7 (byte step, NOT doubled)
    int rows = height;                                // dword_64AA98
    int firstByteOff = (int)frame_off::kPayload;      // dword_64AA94 = 50 default
    int yy = y;
    int top = st.clipY0 - y;
    if (top > 0) {
        const u8* rowTable = frame + GetU32(frame + frame_off::kRowTableOff);
        firstByteOff = (int)GetU32(rowTable + 4 * top);
        rows -= top;
        yy = st.clipY0;
    }
    int bottom = (y + height) - st.clipY1;
    if (bottom > 0)
        rows -= bottom;
    if (rows <= 0)
        return;

    const u8* c = frame + firstByteOff;
    u8* d = (u8*)st.dest + (static_cast<ptrdiff_t>(st.destStridePx) * yy + x);

    for (int r = 0; r < rows; ++r) {
        int runCount = (int)Take32(c);
        for (int run = 0; run < runCount; ++run) {
            int skipBytes = GetI32(c); u32 n = GetU32(c + 4); c += 8;
            d += skipBytes;
            if (n) {
                if (n & 1) { *d = *c; ++d; ++c; }
                for (u32 i = n >> 1; i; --i) {
                    std::memcpy(d, c, 2);
                    c += 2; d += 2;
                }
            }
        }
        d += stepBytes;
    }
}

// gilde.exe 0x5D781C — VIBE_FrameData_Process. Dispatcher (1:1 flow):
//   if (byte_140694B) return 0;                             (global disable gate)
//   compFlag(+0x26) == -1 => colorDepth 0 -> 0; <=1 -> Interpolate; ==2 -> 1.
//   else RLE: X early-outs (x > clipX1 / x+width < clipX0 -> 0), install the
//   dest stride (dword_64A1C8 = surface+16), then dispatch on colorDepth(+0x0C):
//     0  -> HIBYTE(dword_1406947) ? Bounds (8bpp index->16) : Index (8bpp dest)
//     1  -> x < clipX0 ? Next : (x+width <= clipX1 ? Validate : Next)
//     2  -> return 1 with no blit
//     >2 -> restore stride, return 0
bool FrameDataProcess(int x, int y, const u8* frame, const FrameBlitState& st) {
    if (!frame)
        return false;
    if (st.disabled)                       // byte_140694B
        return false;

    i32 compFlag = GetI32(frame + frame_off::kCompFlag);
    if (compFlag == -1) {
        u8 depth = frame[frame_off::kColorDepth];
        if (depth == 0)
            return false;
        if (depth <= 1) {
            FrameDataInterpolate(x, y, frame, st);
            return true;
        }
        return depth == 2;
    }

    // RLE path X early-outs (gilde.exe 0x5d7863..: `if (a1 > dword_64A1BC)
    // return 0; if (a1 + width < dword_64A1B4) return 0;`).
    if (x > st.clipX1)
        return false;
    int width = GetU16(frame + frame_off::kWidth);
    if (x + width < st.clipX0)
        return false;

    u8 depth = frame[frame_off::kColorDepth];
    if (depth == 0) {
        // 8bpp shape: HIBYTE(dword_1406947) selects the index->16 Bounds path.
        if (st.index16Sel)
            FrameTableBounds(x, y, frame, st);
        else
            FrameTableIndex(x, y, frame, st);
        return true;
    }
    if (depth > 1)
        return depth == 2;   // 2 => handled (no blit); >2 => not drawn

    // depth == 1 (16bpp RLE).
    if (x < st.clipX0) {
        FrameTableNext(x, y, frame, st);
        return true;
    }
    if (x + width <= st.clipX1)
        FrameTableValidate(x, y, frame, st);
    else
        FrameTableNext(x, y, frame, st);
    return true;
}

// gilde.exe 0x5D85B8 — VIBE_Animation_Basic ("shp_ShowShapeFromBank").
//   if (!bank) return 0;
//   if (n > *(u16*)(bank+42)) return 0;     // shape index out of range
//   shape = *(u32*)(bank + 4*n + 69) + bank ; FrameData_Process(x, y, shape, surf)
int AnimationBasic(int x, int y, const u8* bank, int n, const FrameBlitState& st) {
    if (!bank)
        return 0;
    u16 count = GetU16(bank + 0x2A);              // *(u16*)(bank+42)
    if (n > (int)count)
        return 0;
    u32 off = GetU32(bank + 4 * n + 0x45);        // *(u32*)(bank + 4*n + 69)
    const u8* shape = bank + off;
    FrameDataProcess(x, y, shape, st);
    return 1;
}

} // namespace guild::render
