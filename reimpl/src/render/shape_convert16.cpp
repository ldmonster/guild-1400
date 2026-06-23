#include "render/shape_convert16.h"

#include "render/render_leaves9.h"   // Leaves9Hooks / Shape_ConvertToNew (0x5d8080)
#include "render/shapebank.h"        // ShapeBankAddShape (0x5d8330) + bank offsets

#include <cstdlib>
#include <cstring>

// gilde.exe ts_texture.c — the 24bpp/8bpp -> 16bpp shape converters. Translated
// 1:1 from the Hex-Rays reference, cross-checked against the raw disassembly for
// the qmemcpy lengths (rep movsd/movsb with ecx preloaded: header = 0x32 bytes,
// bank header = 0x845 bytes, row staging = 2*n, row table = 4*height) and the
// register-staged pixel order (al = p[0] -> r, dl = p[1] -> g, bl = p[2] -> b).
namespace guild::render {

namespace {

inline u16  GetU16(const u8* b, size_t off) { u16 v; std::memcpy(&v, b + off, 2); return v; }
inline u32  GetU32(const u8* b, size_t off) { u32 v; std::memcpy(&v, b + off, 4); return v; }
inline void SetU16(u8* b, size_t off, u16 v) { std::memcpy(b + off, &v, 2); }
inline void SetU32(u8* b, size_t off, u32 v) { std::memcpy(b + off, &v, 4); }

// The originals' fixed stack staging buffers (see header: faithful bounds).
constexpr int kRowStagePx   = 1152;  // WORD v37[1152] @0x5d7c0c / row half of v24 @0x5d7924
constexpr int kRowTableRows = 864;   // DWORD v36[864] / v23[864]

// byte_1406530's zero-filled .bss image (the default when no palette was set).
const u8* ZeroPalette1024() {
    static const u8 kZero[1024] = {};
    return kZero;
}

} // namespace

// -----------------------------------------------------------------------------
// word_1406944 derivation (VIBE_Shape_InitColorMasks @0x5d4ad4; the same formula
// shape_recon_cluster.cpp ShapeInitColorMasksImpl reconstructs over its own
// state struct — re-stated here over ColorFormat, no shared symbol).
u16 ShapeConvertDarkMask(const ColorFormat& f) {
    return (u16)(((((u32)1 << (7 - f.bPrec)) - 1) << f.bPos)
               | ((((u32)1 << (7 - f.gPrec)) - 1) << f.gPos)
               | ((((u32)1 << (7 - f.rPrec)) - 1) << f.rPos));
}

ShapeConvertState& ActiveShapeConvertState() {
    static ShapeConvertState st = [] {
        ShapeConvertState s;
        s.fmt           = Format565();
        s.darkMask      = ShapeConvertDarkMask(s.fmt);   // 0x7BEF
        s.palette1024   = ZeroPalette1024();
        s.reportMessage = nullptr;
        return s;
    }();
    return st;
}

void SetActiveShapeConvertState(const ShapeConvertState& st) {
    ActiveShapeConvertState() = st;
    if (!ActiveShapeConvertState().palette1024)
        ActiveShapeConvertState().palette1024 = ZeroPalette1024();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x5d7c0c — VIBE_Shape_ConvertRgbTo16.
u8* ShapeConvertRgbTo16(const ShapeConvertState& st, const u8* shape) {
    const u32 srcSize  = GetU32(shape, 0);    // *(u32*)shape
    const u32 pixCount = GetU32(shape, 46);   // *(u32*)(shape+46)
    const u16 width    = GetU16(shape, 6);    // *(u16*)(shape+6)
    const u16 height   = GetU16(shape, 10);   // *(u16*)(shape+10)
    const bool dark    = shape[13] == 1;      // *(u8*)(shape+13) == 1

    u16 rowPx[kRowStagePx];                   // v37 (stack WORD[1152])
    u32 rowOff[kRowTableRows];                // v36 (stack DWORD[864])

    // W10-TEX hardening: the originals stage a row in WORD[1152] and the row
    // table in DWORD[864] stack arrays; a malformed bank with width/run > 1152
    // or height > 864 overran the stack in gilde.exe. The shipped assets never
    // exceed these (max 800x600), so the in-contract path below is byte-identical;
    // an out-of-contract shape is rejected (null) instead of corrupting the stack.
    if ((int)height > kRowTableRows)
        return nullptr;

    if (GetU32(shape, 38) == 0xFFFFFFFFu) {
        // ---- RAW full-bitmap branch (loc_5D7EEC) --------------------------
        if ((int)width > kRowStagePx)
            return nullptr;
        // alloc(*shape - 3*pix + 2*pix, "d2:shp:NewShape")
        u8* dst = static_cast<u8*>(std::malloc(srcSize - 3u * pixCount + 2u * pixCount));
        std::memcpy(dst, shape, 50);          // rep movs, ecx = 0x32
        SetU32(dst, 0, 50);                   // *(u32*)dst = 50
        dst[12] = 1;                          // depth = 1 (16bpp)
        SetU32(dst, 38, 0xFFFFFFFFu);         // spanFlag stays -1
        SetU32(dst, 46, 0);                   // opaque counter = 0
        const u8* srcPx = shape + 50;         // v38
        u8* out = dst + 50;                   // v41
        for (int row = 0; row < (int)height; ++row) {           // ebp < height
            for (int i = 0; i < (int)width; ++i) {              // esi < width
                // p = src + 3*(row*width + i); pack(al=p[0], dl=p[1], bl=p[2])
                const u8* p = srcPx + 3u * ((u32)row * width + (u32)i);
                u16 px = (u16)PackColor(st.fmt, p[0], p[1], p[2]);
                // NO 0 -> (5,5,5) replacement in the raw branch (disasm 0x5d8011..).
                if (dark)                                       // +13 == 1
                    px = (u16)((px >> 1) & st.darkMask);        // sar/and word_1406944
                rowPx[i] = px;
            }
            std::memcpy(out, rowPx, 2u * width);                // rep movs, 2*width
            out += 2u * width;
            SetU32(dst, 0, GetU32(dst, 0) + 2u * width);        // size += 2*width
        }
        SetU32(dst, 42, 0);                   // rowTableOffset = 0 (0x5d8062)
        return dst;
    }

    // ---- RLE branch ------------------------------------------------------
    // alloc(2*pix + *shape - 3*pix, "d2:shp:NewShape")
    u8* dst = static_cast<u8*>(std::malloc(2u * pixCount + srcSize - 3u * pixCount));
    std::memcpy(dst, shape, 50);              // rep movs, ecx = 0x32
    SetU32(dst, 0, 50);
    dst[12] = 1;                              // depth = 1
    SetU32(dst, 38, 0);                       // run counter
    SetU32(dst, 46, 0);                       // opaque pixel counter

    const u8* srcRow = shape + 50;            // v43 — src row cursor (@ runCount)
    u8* dstRow = dst + 50;                    // v6  — dst row cursor (@ runCount)
    u8* dstRun = dst + 54;                    // v45 — dst run cursor
    for (int row = 0; row < (int)height; ++row) {               // v42 < height
        const u32 runCount = GetU32(srcRow, 0);
        SetU32(dstRow, 0, runCount);                            // *(u32*)v6 = *v43
        rowOff[row] = (u32)(dstRow - dst);                      // v36[row] = v6 - dst
        SetU32(dst, 0, GetU32(dst, 0) + 4);                     // size += 4
        const u8* srcRun = srcRow + 4;                          // v5
        for (u32 r = 0; r < runCount; ++r) {                    // v44 < *v43
            const u32 skip = GetU32(srcRun, 0);
            const u32 n    = GetU32(srcRun, 4);
            if (n > (u32)kRowStagePx) { std::free(dst); return nullptr; }  // W10-TEX
            SetU32(dstRun, 0, 2u * skip / 3u);                  // *v45 = 2*skip/3u
            SetU32(dstRun, 4, n);                               // v45[1] = n
            const u8* p = srcRun + 8;                           // v13
            for (u32 i = 0; i < n; ++i, p += 3) {
                u16 px = (u16)PackColor(st.fmt, p[0], p[1], p[2]);
                if (px == 0)                                    // test ax,ax
                    px = (u16)PackColor(st.fmt, 5, 5, 5);       // mov ebx,5 ...
                if (dark)                                       // +13 == 1
                    px = (u16)((px >> 1) & st.darkMask);
                rowPx[i] = px;
            }
            std::memcpy(dstRun + 8, rowPx, 2u * n);             // rep movs, 2*n
            SetU32(dst, 38, GetU32(dst, 38) + 1);               // ++runTotal
            SetU32(dst, 46, GetU32(dst, 46) + n);               // opaque += n
            SetU32(dst, 0, GetU32(dst, 0) + 2u * n + 8);        // size += 2n+8
            srcRun += 3u * n + 8;                               // v5 += 3n, +8
            dstRun += 2u * n + 8;                               // v45 += 2n+8
        }
        dstRow = dstRun;                      // v6  = v45
        srcRow = srcRun;                      // v43 = v5 (next row's runCount)
        dstRun += 4;                          // ++v45 (past the runCount dword)
    }
    // Append the row-offset table (loc_5D7E95).
    const u32 sz = GetU32(dst, 0);
    SetU32(dst, 42, sz);                      // rowTableOffset = size
    SetU32(dst, 0, sz + 4u * height);         // size += 4*height
    std::memcpy(dst + sz, rowOff, 4u * height);                 // rep movs, 4*h
    return dst;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x5d7924 — VIBE_Shape_Convert8To16.
u8* ShapeConvert8To16(const ShapeConvertState& st, const u8* shape) {
    if (GetU32(shape, 38) == 0xFFFFFFFFu) {
        // VIBE_ErrorLog_ReportMessage(aShpConvert8to1); return 0;
        if (st.reportMessage)
            st.reportMessage("shp_Convert8To16: Converting of NoReadAndSkip "
                             "Shapes not surported...");
        return nullptr;
    }
    const u32 srcSize  = GetU32(shape, 0);
    const u32 pixCount = GetU32(shape, 46);
    const u16 height   = GetU16(shape, 10);
    const bool dark    = shape[13] == 1;
    const u8* pal      = st.palette1024 ? st.palette1024 : ZeroPalette1024();

    // W10-TEX hardening: faithful stack-buffer bound (see ShapeConvertRgbTo16).
    if ((int)height > kRowTableRows)
        return nullptr;

    // alloc(2*pix + *shape - pix, "d2:shp:NewShape")
    u8* dst = static_cast<u8*>(std::malloc(2u * pixCount + srcSize - pixCount));
    std::memcpy(dst, shape, 50);              // rep movs, ecx = 0x32
    SetU32(dst, 0, 50);
    dst[12] = 1;                              // depth = 1
    SetU32(dst, 38, 0);
    SetU32(dst, 46, 0);

    // 256-entry LUT from the palette QUADS (disasm: add ecx,4 — byte_1406530 is
    // RGBX, only bytes 0..2 read). The original stages it in the top half of the
    // same WORD[1408] stack array as the row pixels.
    u16 lut[256];
    for (int i = 0; i < 256; ++i) {
        const u8* q = pal + 4 * i;
        lut[i] = (u16)PackColor(st.fmt, q[0], q[1], q[2]);
    }

    u16 rowPx[kRowStagePx];                   // low half of v24
    u32 rowOff[kRowTableRows];                // v23

    const u8* srcRow = shape + 50;            // v28
    u8* dstRow = dst + 50;                    // v7
    u8* dstRun = dst + 54;                    // v29
    for (int row = 0; row < (int)height; ++row) {               // i < height
        const u32 runCount = GetU32(srcRow, 0);
        SetU32(dstRow, 0, runCount);
        rowOff[row] = (u32)(dstRow - dst);
        SetU32(dst, 0, GetU32(dst, 0) + 4);
        const u8* srcRun = srcRow + 4;                          // v4
        for (u32 r = 0; r < runCount; ++r) {
            const u32 skip = GetU32(srcRun, 0);
            const u32 n    = GetU32(srcRun, 4);
            if (n > (u32)kRowStagePx) { std::free(dst); return nullptr; }  // W10-TEX
            SetU32(dstRun, 0, 2u * skip);                       // *v29 = 2 * *v4
            SetU32(dstRun, 4, n);
            const u8* p = srcRun + 8;                           // v14
            for (u32 i = 0; i < n; ++i, ++p) {
                u16 px = lut[*p];                               // v24[idx + 1152]
                // NO 0 -> (5,5,5) replacement in the 8bpp converter (disasm).
                if (dark)
                    px = (u16)((px >> 1) & st.darkMask);
                rowPx[i] = px;
            }
            std::memcpy(dstRun + 8, rowPx, 2u * n);
            SetU32(dst, 38, GetU32(dst, 38) + 1);
            SetU32(dst, 46, GetU32(dst, 46) + n);
            SetU32(dst, 0, GetU32(dst, 0) + 2u * n + 8);
            srcRun += n + 8;                                    // v18 = v4 + n, +8
            dstRun += 2u * n + 8;
        }
        dstRow = dstRun;
        srcRow = srcRun;
        dstRun += 4;
    }
    const u32 sz = GetU32(dst, 0);
    SetU32(dst, 42, sz);
    SetU32(dst, 0, sz + 4u * height);
    std::memcpy(dst + sz, rowOff, 4u * height);
    return dst;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x5d80a8 — VIBE_ShapeBank_ConvertNew.
u8* ShapeBankConvertNew(u8* bank, i8 target, bool keepSource) {
    // if (!*(u32*)(bank+48)) return 0;
    if (GetU32(bank, bank_off::kWriteCursor) == 0) return nullptr;

    // Sum every shape's opaque-pixel count (+46) through the offset table (+0x45).
    const u16 count = GetU16(bank, bank_off::kShapeCount);      // +42
    u32 pixSum = 0;
    for (u16 i = 0; i < count; ++i)
        pixSum += GetU32(bank, GetU32(bank, bank_off::kOffsetTable + 4u * i) + 46);

    const u8 fmt = bank[bank_off::kColorDepth];                 // +52
    u8* nb = nullptr;
    if (fmt == 2 && target == 1) {
        // alloc(writeCursor - 3*pix + 2*pix, "d2:shp:NewBank")
        nb = static_cast<u8*>(std::malloc(
                 GetU32(bank, bank_off::kWriteCursor) - 3u * pixSum + 2u * pixSum));
    } else if (fmt == 0 && target == 1) {
        // alloc(writeCursor - pix + 2*pix, "d2:shp:NewBank")
        nb = static_cast<u8*>(std::malloc(
                 GetU32(bank, bank_off::kWriteCursor) - pixSum + 2u * pixSum));
    } else {
        return bank;                          // no conversion applies
    }

    std::memcpy(nb, bank, 0x845);             // rep movs, ecx = 0x845 (header region)
    SetU32(nb, bank_off::kWriteCursor, 0);    // +48 = 0  (ShapeBankAddShape re-inits)
    nb[bank_off::kColorDepth] = 1;            // +52 = 1
    SetU16(nb, bank_off::kShapeCount, 0);     // +42 = 0

    for (u16 j = 0; j < count; ++j) {
        // eax = ConvertToNew(bank + offsetTable[j], dl=1) — the 0x5d8080 driver
        // (render_leaves9), which dispatches into the two converters above via
        // the installed RenderLeaves9Hooks.
        u8* shp = static_cast<u8*>(Shape_ConvertToNew(
            bank + GetU32(bank, bank_off::kOffsetTable + 4u * j), 1));
        // The original calls AddShape(eax=shape) unconditionally; a null can only
        // arise from the unsupported 8bpp-raw shape, which dereferenced null (and
        // crashed) in gilde.exe — guarded here, same observable behavior on the
        // defined domain.
        if (shp) {
            ShapeBankAddShape(nb, shp);       // 0x5d8330
            std::free(shp);                   // VIBE_Memory_FreeDebug(converted)
        }
    }

    // Re-append trailing sequence data (+62 offset / +67 count, 8-byte records).
    if (GetU32(bank, 62) != 0) {
        const u32 wc = GetU32(nb, bank_off::kWriteCursor);      // [ebx+30h]
        SetU32(nb, 62, wc);                                     // [ebx+3Eh] = wc
        std::memcpy(nb + wc, bank + GetU32(bank, 62),           // rep movs,
                    8u * GetU16(bank, 67));                     // ecx = 8*count
    }

    if (!keepSource)                          // if (!ebx) FreeDebug(bank)
        std::free(bank);
    return nb;
}

// -----------------------------------------------------------------------------
// Rule-13 wiring: de-inert the leaves9 converter slots.
namespace {
void* HookConvertRgbTo16(void* shape) {
    return ShapeConvertRgbTo16(ActiveShapeConvertState(),
                               static_cast<const u8*>(shape));
}
void* HookConvert8To16(void* shape) {
    return ShapeConvert8To16(ActiveShapeConvertState(),
                             static_cast<const u8*>(shape));
}
} // namespace

void InstallShapeConvertersIntoLeaves9() {
    RenderLeaves9Hooks h = Leaves9Hooks();    // keep FrameDataProcess as wired
    h.ConvertRgbTo16 = &HookConvertRgbTo16;
    h.Convert8To16   = &HookConvert8To16;
    SetLeaves9Hooks(h);
}

} // namespace guild::render
