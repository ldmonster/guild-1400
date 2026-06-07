#pragma once
#include "guild/common/types.h"
#include "render/types.h"
#include <vector>

// High-colour remap tables from gilde.exe (d3_ts:HiColTab). For a small set of
// distinct RGB colours the engine builds a 0x8200-byte block holding:
//   * a 256-entry direct RGB->565/555 lookup at byte offset 0x7E00, and
//   * 63 light-ramp tables (each 256 entries, 512 bytes) at offsets
//     512*L (L = 0..62) giving the colour scaled from black to full over the
//     ramp — used by the RLE light-table sprite blitter.
// All packing uses the active ColorFormat (RGB565 by default).
namespace guild::render {

// One HiColTab bank (gilde.exe: 776-byte record + a 0x8200 data block).
struct HiColTab {
    std::vector<u8> data;     // 0x8200 bytes (the *a3 block)
    u8  rgb[256 * 3] = {};    // packed 3-byte RGB list (record +4..+772)
    int freeCount = 256;      // record +772 (a3[193]); decremented per entry
    ColorFormat fmt = Format565();

    HiColTab() : data(0x8200, 0) {}
};

// gilde.exe 0x5d9db8 — VIBE_HiColTab_AddEntry (r@al, g@dl, tab@ecx, b@bl).
// Returns the index assigned to (r,g,b) (existing or newly added). Builds the
// direct 565 value and the 63-step light ramp for the new entry.
u8 HiColTabAddEntry(HiColTab& tab, u8 r, u8 g, u8 b);

// Read the direct 565/555 value stored for entry `idx` (offset 0x7E00 + 2*idx).
u16 HiColTabDirect(const HiColTab& tab, int idx);

// Read the light-ramp 565/555 value for entry `idx` at light step L (0..62),
// stored at byte offset 512*L + 2*idx.
u16 HiColTabRamp(const HiColTab& tab, int idx, int light);

} // namespace guild::render
