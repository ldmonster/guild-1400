#include "render/water_anim.h"

namespace guild::render {

// gilde.exe dword_5D93C8 (recovered via get_bytes @0x5d93c8). The engine reads
// dword_5D93C8[selector]; selector is the 4-bit nibble guarded nonzero, so only
// indices 1..10 are reachable in practice. Values: {15,13,11,9,8,7,5,3,2,1}.
// Index 0 is the table's preceding dword (unused here); indices 11..15 are never
// produced because the floor never sets a selector above 10 (single-digit speeds).
const u32 kWaterAnimSpeedTable[11] = {
    0,   // [0] unused (selector guarded nonzero)
    15,  // [1]
    13,  // [2]
    11,  // [3]
    9,   // [4]
    8,   // [5]
    7,   // [6]
    5,   // [7]
    3,   // [8]
    2,   // [9]
    1,   // [10]
};

// gilde.exe 0x5be428 — frame index:
//   v22 / (unsigned)dword_5D93C8[v4] % *(unsigned __int8 *)(v3 + 112)
// i.e. (time / speedTable[sel]) % memberCount, all unsigned.
i32 WaterTextureFrameIndex(u32 time, u8 speedSel, u8 memberCount) {
    u32 divisor = kWaterAnimSpeedTable[speedSel & 0x0Fu];
    if (divisor == 0) divisor = 1;        // defensive (engine never indexes 0)
    return (i32)((time / divisor) % (u32)memberCount);
}

} // namespace guild::render
