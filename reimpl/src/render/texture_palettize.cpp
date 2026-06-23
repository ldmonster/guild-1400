#include "render/texture_palettize.h"

#include <cstring>
#include <vector>

// =============================================================================
// guild::render — the VIBE_Quant_* 24-bit palettizer, translated 1:1 from the
// gilde.exe decompile (addresses on every function). See the header for the
// call-tree provenance. Hex-Rays variable-to-meaning mapping is noted inline
// where the original code is register soup.
// =============================================================================
namespace guild::render {

namespace {

// 24-byte colour node (one per octree cell per level).
//   +0 sumR  +4 sumG  +8 sumB  +12 count  +16 total  +20 childMask  +21 palIdx
struct QuantNode {
    i32 sumR = 0, sumG = 0, sumB = 0;
    i32 count = 0;     // pixels merged INTO this node (leaf weight)
    i32 total = 0;     // subtree pixel count (the heap key)
    u8  mask = 0;      // live child bits
    u8  palIdx = 0;    // assigned palette index (TreeCollectPalette)
};

// Heap entry (4 bytes in the original: u8 level, u16 idx at +2), 1-based.
struct HeapEnt {
    u8  level = 0;
    u16 idx = 0;
};

// The persistent process state — mirrors the original globals exactly.
struct QuantState {
    bool tablesInit = false;          // dword_64ADB0
    u16 rTab[256];                    // word_1409508  (R bits -> 2,5,8,11,14)
    u16 gTab[256];                    // word_14092F0  (G bits -> 1,4,7,10,13)
    u16 bTab[256];                    // word_14090F0  (B bits -> 0,3,6,9,12)
    i32 squares[511];                 // dword_1409E0C[1..]; sq(d)=squares[d+255]
    u8  palR[256] = {};               // byte_1409708  (PERSISTENT across calls)
    u8  palG[256] = {};               // byte_1409808
    u8  palB[256] = {};               // byte_1409908
    u32 leafCount = 0;                // dword_1409A08
    u32 palCount = 0;                 // dword_140A214

    // Per-call tree + heap (AllocColorNodes/FreeColorNodes lifetime).
    std::vector<QuantNode> levels[6]; // dword_14094F0[0..5]: 8^L nodes
    std::vector<HeapEnt>   heap;      // dword_64ADB4 (slot 0 unused)
};

QuantState& S() {
    static QuantState s;
    return s;
}

// gilde.exe 0x602aa4 — VIBE_Quant_InitLookupTables.
void QuantInitLookupTables(QuantState& s) {
    for (i32 i = 0; i < 256; ++i) {
        s.rTab[i] = (u16)(((i & 8) >> 1) | (2 * (i & 0x10)) | (32 * (i & 0x40)) |
                          ((i & 0x80) << 7) | (8 * (i & 0x20)));
        s.bTab[i] = (u16)(((i & 8) >> 3) | ((i & 0x80) << 5) | (8 * (i & 0x40)) |
                          (2 * (i & 0x20)) | ((i & 0x10) >> 1));
        s.gTab[i] = (u16)(((i & 8) >> 2) | (i & 0x10) | (4 * (i & 0x20)) |
                          (16 * (i & 0x40)) | ((i & 0x80) << 6));
    }
    // squared-difference table: sq(d) for d in [-255, 255]
    // (dword_140A210 = &dword_1409E0C[1]; stores (d)^2 at base[d]).
    for (i32 d = -255; d <= 255; ++d)
        s.squares[d + 255] = d * d;
}
inline i32 Sq(const QuantState& s, i32 d) { return s.squares[d + 255]; }

// gilde.exe 0x602be0 — VIBE_Quant_AllocColorNodes (zeroed level arrays).
bool QuantAllocColorNodes(QuantState& s) {
    static const i32 kLevelSize[6] = {1, 8, 64, 512, 4096, 0x8000};
    for (i32 l = 0; l < 6; ++l)
        s.levels[l].assign((std::size_t)kLevelSize[l], QuantNode{});
    s.palCount = 0;                   // dword_140A214 = 0
    return true;                      // AllocZeroed cannot fail here
}

// gilde.exe 0x602c8c — VIBE_Quant_FreeColorNodes.
void QuantFreeColorNodes(QuantState& s) {
    for (auto& l : s.levels) {
        l.clear();
        l.shrink_to_fit();
    }
    s.heap.clear();
    s.heap.shrink_to_fit();
}

inline i32 HeapKey(const QuantState& s, const HeapEnt& e) {
    return s.levels[e.level][e.idx].total;   // node +16
}

// gilde.exe 0x602f3c — VIBE_Quant_HeapSiftDown(i). Min-heap by node +16.
void QuantHeapSiftDown(QuantState& s, u32 i) {
    u32 v1 = i;
    HeapEnt saved = s.heap[i];
    u32 half = s.leafCount >> 1;
    i32 key = HeapKey(s, saved);
    if (v1 <= half) {
        u32 c;
        do {
            c = 2 * v1;
            if (c < s.leafCount && HeapKey(s, s.heap[c + 1]) < HeapKey(s, s.heap[c]))
                ++c;
            if (key <= HeapKey(s, s.heap[c]))
                break;
            s.heap[v1] = s.heap[c];
            v1 = c;
        } while (c <= half);
    }
    s.heap[v1] = saved;
}

// gilde.exe 0x602d2c — VIBE_Quant_BuildHistogram(srcRGB, pixelCount).
// 15-bit interleaved histogram at level 5, leaf heap entries in ascending
// code order, ancestor subtree counts + child masks, then heapify.
bool QuantBuildHistogram(QuantState& s, const u8* src, u32 pixelCount) {
    s.heap.assign((std::size_t)0x8001 + 1, HeapEnt{});  // 0x20004-byte block
    auto& l5 = s.levels[5];
    const u8* p = src;
    for (u32 i = 0; i < pixelCount; ++i, p += 3) {
        // word_1409508[*v5] + word_14092F0[v5[1]] + word_14090F0[v5[2]]
        u32 code = (u32)s.rTab[p[0]] + s.gTab[p[1]] + s.bTab[p[2]];
        ++l5[code].count;             // node +12
    }
    s.leafCount = 0;                  // dword_1409A08
    u32 heapFill = 0;
    for (u32 code = 0; code < 0x8000; ++code) {
        i32 c = l5[code].count;
        if (!c)
            continue;
        ++heapFill;                   // entries written at +4, +8, ... (1-based)
        s.heap[heapFill] = HeapEnt{5, (u16)code};
        l5[code].total = c;           // node +16 = count
        // De-interleave the 5-bit channels back to byte values; +4 = the
        // bucket centre (each bucket spans 8).
        i32 r5 = (i32)((2 * (code & 4)) | ((code & 0x20) >> 1) | ((code & 0x100) >> 3) |
                       ((code & 0x800) >> 5) | ((code & 0x4000) >> 7));
        i32 g5 = (i32)((code & 0x10) | ((code & 0x80) >> 2) | ((code & 0x2000) >> 6) |
                       ((code & 0x400) >> 4) | (4 * (code & 2)));
        i32 b5 = (i32)((2 * (code & 8)) | ((code & 0x40) >> 1) | ((code & 0x200) >> 3) |
                       ((code & 0x1000) >> 5) | (8 * (code & 1)));
        l5[code].sumR = c * (r5 + 4);
        l5[code].sumG = c * (g5 + 4);
        l5[code].sumB = c * (b5 + 4);
        ++s.leafCount;
        // Ancestors: subtree counts + child-mask bits (levels 4..0).
        u32 v15 = code;
        for (i32 lvl = 4; lvl >= 0; --lvl) {
            u32 bit = v15 & 7;
            v15 >>= 3;
            s.levels[lvl][v15].total += c;
            s.levels[lvl][v15].mask |= (u8)(1u << bit);
        }
    }
    for (u32 i = s.leafCount; i; --i)
        QuantHeapSiftDown(s, i);
    return true;
}

// gilde.exe 0x603068 — VIBE_Quant_HeapReduceColors(maxColors).
void QuantHeapReduceColors(QuantState& s, u32 maxColors) {
    while (maxColors < s.leafCount) {
        HeapEnt top = s.heap[1];
        u32 idx = top.idx;
        u8 lvl = top.level;
        u32 parentIdx = idx >> 3;
        u8 pl = (u8)(lvl - 1);
        QuantNode& parent = s.levels[pl][parentIdx];
        QuantNode& child = s.levels[lvl][idx];
        if (parent.count) {
            // Parent already a leaf: pop the child entry.
            s.heap[1] = s.heap[s.leafCount];
            --s.leafCount;
        } else {
            // Parent becomes the leaf entry in place.
            s.heap[1] = HeapEnt{pl, (u16)parentIdx};
        }
        parent.count += child.count;
        parent.sumR += child.sumR;
        parent.sumG += child.sumG;
        parent.sumB += child.sumB;
        parent.mask &= (u8)~(1u << (idx & 7));
        QuantHeapSiftDown(s, 1);
    }
}

// gilde.exe 0x603180 — VIBE_Quant_TreeCollectPalette(idx, level).
// Children (bit 7 down to 0) first, then this node if it carries a count:
// palette entry = (sum + count/2) / count per channel.
void QuantTreeCollectPalette(QuantState& s, u32 idx, u32 level) {
    QuantNode& n = s.levels[level][idx];
    if (n.mask) {
        for (i32 bit = 7; bit >= 0; --bit)
            if (n.mask & (1u << bit))
                QuantTreeCollectPalette(s, 8 * idx + (u32)bit, level + 1);
    }
    if (n.count) {
        n.palIdx = (u8)s.palCount;
        u32 c = (u32)n.count;
        s.palR[s.palCount] = (u8)(((c >> 1) + (u32)n.sumR) / c);
        s.palG[s.palCount] = (u8)(((c >> 1) + (u32)n.sumG) / c);
        s.palB[s.palCount] = (u8)(((u32)n.sumB + (c >> 1)) / c);
        ++s.palCount;
    }
}

// gilde.exe 0x603a30 — VIBE_Quant_FindClosestColor(r,g,b). The query is
// bucketed to (c & 0xF8) + 4; squared distance over the collected leaves.
u32 QuantFindClosestColor(const QuantState& s, u8 r, u8 g, u8 b) {
    i32 qr = (r & 0xF8) + 4;
    i32 qg = (g & 0xF8) + 4;
    i32 qb = (b & 0xF8) + 4;
    u32 best = 0;
    i32 bestD = 200000;
    for (u32 i = 0; i < s.leafCount; ++i) {
        i32 d = Sq(s, (i32)s.palB[i] - qb) + Sq(s, (i32)s.palG[i] - qg) +
                Sq(s, (i32)s.palR[i] - qr);
        if (d < bestD) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

// gilde.exe 0x6033b4 — VIBE_Quant_MapImageToPalette(src, dst, rows, width,
// dither). Both arms.
bool QuantMapImageToPalette(QuantState& s, const u8* src, u8* dst, i32 rows,
                            i32 width, i32 dither) {
    // W10-TEX hardening: a degenerate (empty) image has no texels to map; both
    // arms would write nothing, and the dither arm's per-row pointer choreography
    // (a1 += 3*width-3, etc.) forms out-of-bounds / null-offset pointers that are
    // never dereferenced — UB under UBSAN. Bail before forming them. The original
    // engine never maps a 0-area texture (width==height, both > 0, gated upstream
    // in VIBE_Texture_LoadByName 0x5da714); the width>0/rows>0 path below is byte-
    // identical to the original.
    if (rows <= 0 || width <= 0)
        return true;
    if (!dither) {
        // ---- undithered arm: one index per occupied 15-bit histogram cell.
        std::vector<u8> tbl((std::size_t)0x8000, 0);
        for (u32 code = 0; code < 0x8000; ++code) {
            if (!s.levels[5][code].count)
                continue;
            u8 r5 = (u8)(((code & 0x20) >> 1) | ((code & 0x100) >> 3) |
                         ((code & 0x800) >> 5) | ((code & 0x4000) >> 7) |
                         (2 * (code & 4)));
            u8 g5 = (u8)((4 * (code & 2)) | (code & 0x10) | ((code & 0x80) >> 2) |
                         ((code & 0x2000) >> 6) | ((code & 0x400) >> 4));
            u8 b5 = (u8)((2 * (code & 8)) | ((code & 0x40) >> 1) |
                         ((code & 0x200) >> 3) | ((code & 0x1000) >> 5) |
                         (8 * (code & 1)));
            tbl[code] = (u8)QuantFindClosestColor(s, r5, g5, b5);
        }
        i32 n = rows * width;
        const u8* p = src;
        for (i32 i = 0; i < n; ++i, p += 3)
            dst[i] = tbl[(u32)s.rTab[p[0]] + s.gTab[p[1]] + s.bTab[p[2]]];
        return true;
    }

    // ---- dithered arm: serpentine Floyd–Steinberg with the original's exact
    // clamp tables, 16ths error accumulators (i16, "(acc+8)>>4" consumption),
    // the +-20 per-channel error clamp and the lazy RGB555 closest cache.
    //
    // v45/v60: the value clamp table (index -256..511 -> 0..255).
    u8 clampVal[768];
    for (i32 i = 0; i < 256; ++i) {
        clampVal[i] = 0;              // -256..-1
        clampVal[256 + i] = (u8)i;    // 0..255 identity
        clampVal[512 + i] = 255;      // 256..511
    }
    const u8* vClamp = clampVal + 256;
    // v46/v61: the error clamp table (index -256..255 -> -20..20).
    i8 clampErr[512];
    for (i32 i = 0; i < 256; ++i) {
        clampErr[i] = -20;            // -256..-1
        clampErr[256 + i] = 20;       // 0..255
    }
    for (i32 e = -20; e <= 20; ++e)
        clampErr[256 + e] = (i8)e;
    const i8* eClamp = clampErr + 256;
    // v57: 32768-entry lazy closest-colour cache, init -1.
    std::vector<i16> cache((std::size_t)0x8000, (i16)-1);
    // v49/v42: the two row error buffers, 3*(width+2) i16 each (the original
    // allocates 6*(width+2) bytes; the FIRST one is pre-zeroed, 0x603637).
    std::vector<i16> bufA((std::size_t)3 * (width + 2), 0);
    std::vector<i16> bufB((std::size_t)3 * (width + 2));  // covered by writes

    const u8* a1 = src;
    u8* a2 = dst;
    bool reverse = false;             // v50
    for (i32 row = 0; row < rows; ++row) {
        i16* cur;                     // v23 — current-row errors, read forward
        i16* nxt;                     // v24 — next-row errors, written backward
        i32 dir;                      // v56
        if (reverse) {
            nxt = bufA.data() + 3 * width;   // v24 = v43 + v49
            cur = bufB.data() + 3;           // v23 = v42 + 6
            dir = -1;
            a1 += 3 * width - 3;
            a2 += width - 1;
        } else {
            dir = 1;
            nxt = bufB.data() + 3 * width;   // v24 = v43 + v42
            cur = bufA.data() + 3;           // v23 = v47
        }
        nxt[2] = 0;                   // v24[2] = 0; v24[1] = v24[2]; *v24 = ...
        nxt[1] = 0;
        nxt[0] = 0;
        for (i32 j = 0; j < width; ++j) {
            // consume the stored errors (16ths, rounded)
            i32 vR = vClamp[(i32)a1[0] + (((i32)cur[0] + 8) >> 4)];   // v65
            i32 vG = vClamp[(i32)a1[1] + (((i32)cur[1] + 8) >> 4)];   // v26
            i32 vB = vClamp[(i32)a1[2] + (((i32)cur[2] + 8) >> 4)];   // v27
            u32 key = (u32)(vB >> 3) | (u32)((vR & 0xF8) << 7) |
                      (u32)(4 * (vG & 0xF8));                          // v58
            if (cache[key] < 0)
                cache[key] = (i16)QuantFindClosestColor(s, (u8)vR, (u8)vG, (u8)vB);
            i32 idx = cache[key];
            *a2 = (u8)idx;            // *a2 = *(BYTE*)v28
            // R (v66)
            i16 eR = (i16)eClamp[vR - (i32)s.palR[idx]];
            nxt[-3] = eR;                       // *(v24-3)
            nxt[3] = (i16)(nxt[3] + 3 * eR);    // v24[3]
            nxt[0] = (i16)(nxt[0] + 5 * eR);    // *v24
            cur[3] = (i16)(cur[3] + 7 * eR);    // ((i16*)v23)[3]
            // G (v30)
            i16 eG = (i16)eClamp[vG - (i32)s.palG[idx]];
            nxt[4] = (i16)(nxt[4] + 3 * eG);    // v24[4]
            nxt[-2] = eG;                       // *(v24-2)
            nxt[1] = (i16)(nxt[1] + 5 * eG);    // v24[1]
            cur += 3;                           // v23 += 6 bytes
            nxt -= 3;                           // v24 -= 3 words
            cur[1] = (i16)(cur[1] + 7 * eG);    // ((i16*)v23)[1] after advance
            // B (v31) — offsets relative to the ALREADY-retreated v24
            i16 eB = (i16)eClamp[vB - (i32)s.palB[idx]];
            nxt[2] = eB;                        // v24[2]  (plain store)
            nxt[8] = (i16)(nxt[8] + 3 * eB);    // v24[8]
            nxt[5] = (i16)(nxt[5] + 5 * eB);    // v24[5]
            cur[2] = (i16)(cur[2] + 7 * eB);    // ((i16*)v23)[2]
            a1 += 3 * dir;
            a2 += dir;
        }
        if (row % 2 == 1) {           // v54 % 2 == 1 — after a reverse row
            a1 += 3 * width + 3;
            a2 += width + 1;
        }
        reverse = !reverse;
    }
    return true;
}

// gilde.exe 0x602a70 — VIBE_Quant_CopyPaletteEntries (planar copy-out of the
// persistent palette arrays, all 256 entries).
void QuantCopyPaletteEntries(const QuantState& s, u8* out) {
    for (i32 k = 0; k < 256; ++k) {
        out[k] = s.palR[k];
        out[256 + k] = s.palG[k];
        out[512 + k] = s.palB[k];
    }
}

} // namespace

// gilde.exe 0x6029f0 — VIBE_Quant_BuildPalette.
bool QuantBuildPalette(const u8* rgb, i32 width, i32 height, i32 maxColors,
                       i32 dither, u8* outIndices, u8 outPalette[768]) {
    QuantState& s = S();
    if (!s.tablesInit) {              // dword_64ADB0 gate
        s.tablesInit = true;
        QuantInitLookupTables(s);
    }
    if (!QuantAllocColorNodes(s) ||
        !QuantBuildHistogram(s, rgb, (u32)(width * height))) {
        QuantFreeColorNodes(s);
        return false;
    }
    QuantHeapReduceColors(s, (u32)maxColors);
    QuantTreeCollectPalette(s, 0, 0);
    if (!QuantMapImageToPalette(s, rgb, outIndices, height, width, dither)) {
        QuantFreeColorNodes(s);
        return false;
    }
    QuantFreeColorNodes(s);
    QuantCopyPaletteEntries(s, outPalette);
    return true;
}

// The resolve-layer wiring point (see header): VIBE_Texture_LoadSoftPalettize
// @0x5da34c feeds a 24-bit source through Bmp_LoadBuffer(flags=7) ->
// Quant_BuildPalette(256, dither=1) and converts the planar palette to RGB
// triples (0x5f14dc). The HiColTab re-basing that follows in the original
// only renumbers indices into the shared shading banks; the per-texel COLOURS
// fixed here are the original's.
bool PalettizeDecodedBmp(DecodedBmp& bmp) {
    if (!bmp.ok || bmp.bpp <= 8 || bmp.width <= 0 || bmp.height <= 0)
        return false;
    if (!bmp.indices.empty())
        return false;                 // already palettized / 8-bit source
    const std::size_t n = (std::size_t)bmp.width * (std::size_t)bmp.height;
    if (bmp.rgba.size() < n * 4)
        return false;
    std::vector<u8> rgb(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = bmp.rgba[i * 4 + 0];
        rgb[i * 3 + 1] = bmp.rgba[i * 4 + 1];
        rgb[i * 3 + 2] = bmp.rgba[i * 4 + 2];
    }
    std::vector<u8> idx(n);
    u8 planar[768];
    if (!QuantBuildPalette(rgb.data(), bmp.width, bmp.height, 256, 1, idx.data(),
                           planar))
        return false;
    bmp.indices = std::move(idx);
    bmp.palette.resize(768);
    for (i32 k = 0; k < 256; ++k) {   // planar -> interleaved (0x5f14dc loop)
        bmp.palette[(std::size_t)k * 3 + 0] = planar[k];
        bmp.palette[(std::size_t)k * 3 + 1] = planar[256 + k];
        bmp.palette[(std::size_t)k * 3 + 2] = planar[512 + k];
    }

    // wave-5 W5-CKEY — COLOUR KEY AT INDEX 0 (the faithful 1:1 choice (a)).
    //
    // This source is >8bpp == colour-keyed (record +104 bit 2 in the bind sites;
    // VIBE_Texture_LoadByName @0x5dad52 with gate dword_140809C==0). The engine
    // makes its BLACK backdrop transparent via the DDraw KEYSRC on pal[0] ==
    // (0,0,0) (VIBE_Render_LoadAndStretchTexture @0x5dea50, v111 = 0 for a 24-bit
    // source) — a key on the SOURCE pixel being exactly black. Our software path
    // palettizes the source (the LoadSoftPalettize @0x5da34c arm that actually
    // runs, byte_649D70==0), and the octree quantizer biases pure (0,0,0) toward
    // its bucket centre (~4,4,4 -> 565 0x0020), so an exact-black key would NEVER
    // match the quantized texel. The software masked span (FillSpanTexturedMasked
    // @0x5F721A) keys on SOURCE INDEX 0; to make "source pixel was black" ==
    // "index 0" (so that index-0 rule reproduces the DDraw source-black key
    // exactly), we RESERVE palette index 0 for black here: every source pixel
    // that was exactly (0,0,0) is forced to index 0, index 0's colour is set to
    // exact black, and the colour that previously sat at 0 is relocated to the
    // black cell's old index (a label swap — no pixel changes colour except the
    // intended black->transparent). Sources with no black backdrop are
    // untouched. This is option (a) of the W5-CKEY analysis
    // (progress/colourkey-integration-wave5.md).
    {
        // Does the source contain exact (0,0,0)? Find the index those pixels
        // currently carry (the quantizer's near-black bucket).
        std::size_t firstBlackPixel = (std::size_t)-1;
        for (std::size_t i = 0; i < n; ++i) {
            if (bmp.rgba[i * 4 + 0] == 0 && bmp.rgba[i * 4 + 1] == 0 &&
                bmp.rgba[i * 4 + 2] == 0) {
                firstBlackPixel = i;
                break;
            }
        }
        if (firstBlackPixel != (std::size_t)-1) {
            const u8 blackIdx = bmp.indices[firstBlackPixel];
            if (blackIdx != 0) {
                // Swap palette labels 0 <-> blackIdx and remap all indices, so
                // the black bucket lands at index 0 (the masked span's key) and
                // the displaced colour keeps its pixels via blackIdx.
                for (int c = 0; c < 3; ++c)
                    std::swap(bmp.palette[(std::size_t)0 * 3 + c],
                              bmp.palette[(std::size_t)blackIdx * 3 + c]);
                for (std::size_t i = 0; i < n; ++i) {
                    u8& v = bmp.indices[i];
                    if (v == 0)            v = blackIdx;
                    else if (v == blackIdx) v = 0;
                }
            }
            // Pin index 0's colour to EXACT black so the resolved-value key
            // (FillSpanTexturedMasked colour-key mode) and any 565 read also see
            // pure 565 black (0x0000) — matching the DDraw key on (0,0,0).
            bmp.palette[0] = 0;
            bmp.palette[1] = 0;
            bmp.palette[2] = 0;
        }
    }
    return true;
}

} // namespace guild::render
