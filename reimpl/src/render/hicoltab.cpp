#include "render/hicoltab.h"
#include "render/colorformat.h"

namespace guild::render {

namespace {
// gilde.exe VIBE_Coord_ConvertX @0x5c6b08 truncates the FPU value toward zero
// after the caller has already added 0.5 — i.e. round-half-up for >=0 inputs.
inline int Chop(double v) { return (int)v; }
} // namespace

u16 HiColTabDirect(const HiColTab& tab, int idx) {
    int off = 0x7E00 + 2 * idx;            // *a3 + 2*i + 32256
    return (u16)(tab.data[off] | (tab.data[off + 1] << 8));
}

u16 HiColTabRamp(const HiColTab& tab, int idx, int light) {
    int off = 512 * light + 2 * idx;        // *a3 + 2*i + 512*L
    return (u16)(tab.data[off] | (tab.data[off + 1] << 8));
}

// gilde.exe 0x5d9db8 — VIBE_HiColTab_AddEntry.
// Linear search the existing 3-byte RGB list for (r,g,b); if found return its
// index. Otherwise append it, write the direct 565 value at 0x7E00+2*i, build a
// 63-step black->colour light ramp at 512*L+2*i, and decrement the free count.
u8 HiColTabAddEntry(HiColTab& tab, u8 r, u8 g, u8 b) {
    // search (256 - freeCount used slots)
    int used = 256 - tab.freeCount;
    int i = 0;
    for (; i < used; ++i) {
        if (tab.rgb[3 * i] == r && tab.rgb[3 * i + 1] == g && tab.rgb[3 * i + 2] == b)
            return (u8)i;
    }
    if (i != used)               // matches original "256 - a3[193] != i" guard
        return (u8)i;

    // append new entry
    u16 direct = (u16)PackColor(tab.fmt, r, g, b);
    int doff = 0x7E00 + 2 * i;
    tab.data[doff]     = (u8)direct;
    tab.data[doff + 1] = (u8)(direct >> 8);

    tab.rgb[3 * i]     = r;
    tab.rgb[3 * i + 1] = g;
    tab.rgb[3 * i + 2] = b;

    // light ramp: step 0..62, channel = round(channel/62 * step).
    for (int step = 0; step < 0x3F; ++step) {
        double t = (double)step;
        u8 br = (u8)Chop((double)b / 62.0 * t + 0.5);
        u8 rr = (u8)Chop((double)r / 62.0 * t + 0.5);
        u8 gg = (u8)Chop((double)g / 62.0 * t + 0.5);
        u16 v = (u16)PackColor(tab.fmt, rr, gg, br);
        int roff = 512 * step + 2 * i;
        tab.data[roff]     = (u8)v;
        tab.data[roff + 1] = (u8)(v >> 8);
    }

    --tab.freeCount;
    return (u8)i;
}

} // namespace guild::render
