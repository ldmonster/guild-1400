#include "render/quant.h"
#include <cstring>

namespace guild::render {

QuantState::QuantState() {
    std::memset(spreadHi, 0, sizeof(spreadHi));
    std::memset(spreadMid, 0, sizeof(spreadMid));
    std::memset(spreadLo, 0, sizeof(spreadLo));
    std::memset(sq, 0, sizeof(sq));
    sqBase = &sq[255];
    std::memset(palR, 0, sizeof(palR));
    std::memset(palG, 0, sizeof(palG));
    std::memset(palB, 0, sizeof(palB));
}

// gilde.exe 0x602aa4 — VIBE_Quant_InitLookupTables.
// First loop builds three bit-spread tables. In the original word_1409508 is
// written at v0 (pre-increment) while word_14090EE/word_14092EE are written at
// v0 after the ++ (index i+1); the consumers read the latter two through aliases
// one word later (word_14090F0/word_14092F0), so the value computed at iteration
// i is what is read for input byte i. We therefore index all three tables by i
// directly (behaviour-identical to what BuildHistogram/closest-search observe).
//
// Second loop builds sq[d] = d*d for d in [-255,255] and sets sqBase = &sq[255].
void QuantInitLookupTables(QuantState& q) {
    for (int i = 0; i < 256; ++i) {
        int v2 = i & 0x80;
        int v3 = i & 0x40;
        int v4 = i & 0x20;
        int v9 = i & 0x10;
        int v5 = i & 8;
        q.spreadHi[i]  = (u16)((v5 >> 1) | (2 * v9)  | (32 * v3) | (v2 << 7) | (8 * v4));
        q.spreadMid[i] = (u16)((v5 >> 3) | (32 * v2) | (8 * v3)  | (2 * v4)  | (v9 >> 1));
        q.spreadLo[i]  = (u16)((v5 >> 2) | v9        | (4 * v4)  | (16 * v3) | (v2 << 6));
    }
    for (int d = -255; d <= 255; ++d)
        q.sq[d + 255] = d * d;
    q.sqBase = &q.sq[255];
}

// gilde.exe 0x603a30 — VIBE_Quant_FindClosestColor.
// Bias each channel to the centre of its top-5-bit bucket ((c & 0xF8) + 4) then
// find the palette entry minimising the sum of squared per-channel distances,
// using the sq LUT. Initial best distance is 200000 (the original's sentinel).
unsigned QuantFindClosestColor(const QuantState& q, u8 r, u8 g, u8 b) {
    int rr = (r & 0xF8) + 4;
    int gg = (g & 0xF8) + 4;
    int bb = (b & 0xF8) + 4;
    unsigned best = 0;
    int bestDist = 200000;
    for (int i = 0; i < q.palCount; ++i) {
        int d = q.sqBase[(int)q.palB[i] - bb]
              + q.sqBase[(int)q.palG[i] - gg]
              + q.sqBase[(int)q.palR[i] - rr];
        if (d < bestDist) {
            best = (unsigned)i;
            bestDist = d;
        }
    }
    return best;
}

// gilde.exe 0x602a70 — VIBE_Quant_CopyPaletteEntries. Planar R[256] G[256] B[256].
void QuantCopyPaletteEntries(const QuantState& q, u8* out) {
    for (int i = 0; i < 256; ++i) {
        out[i]       = q.palR[i];
        out[i + 256] = q.palG[i];
        out[i + 512] = q.palB[i];
    }
}

} // namespace guild::render
