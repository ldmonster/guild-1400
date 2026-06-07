#include "render/colorformat.h"

namespace guild::render {

// gilde.exe 0x4358d8 — VIBE_Render_ComputeChannelShifts
// Original takes (rMask, gMask, bMask) by separate registers and writes six
// output bytes. First loop counts trailing zeros of each mask (field position);
// it shifts the mask right while the low bit is 0. Second loop counts the run of
// set bits; precision = 8 - setBits. (faithful 1:1, two explicit passes)
ColorFormat ComputeChannelShifts(u32 rMask, u32 gMask, u32 bMask) {
    // The original processes (a1=gMask, a2=rMask, a4=bMask) — see call site in
    // PackColorToPixel @0x4359b0. We pass r/g/b by name and map below.
    u32 g = gMask, r = rMask, b = bMask;

    u8 rPos = 0, gPos = 0, bPos = 0;
    for (int i = 0; i < 32; ++i) {
        if ((g & 1) == 0) { ++gPos; g >>= 1; }
        if ((r & 1) == 0) { ++rPos; r >>= 1; }
        if ((b & 1) == 0) { ++bPos; b >>= 1; }
    }
    u8 rSet = 0, gSet = 0, bSet = 0;
    for (int j = 0; j < 32; ++j) {
        if ((g & 1) == 1) { ++gSet; g >>= 1; }
        if ((r & 1) == 1) { ++rSet; r >>= 1; }
        if ((b & 1) == 1) { ++bSet; b >>= 1; }
    }
    ColorFormat f;
    f.gPos  = gPos;            // *a3 in original is green precision; positions in a5/a7/a9
    f.gPrec = (u8)(8 - gSet);
    f.rPos  = rPos;
    f.rPrec = (u8)(8 - rSet);
    f.bPos  = bPos;
    f.bPrec = (u8)(8 - bSet);
    return f;
}

// gilde.exe 0x434f30 — VIBE_Result_Handler_Final
u32 PackColor(const ColorFormat& f, u8 r, u8 g, u8 b) {
    return (u32)(((int)g >> f.gPrec << f.gPos)
               | ((int)r >> f.rPrec << f.rPos)
               | ((int)b >> f.bPrec << f.bPos));
}

// gilde.exe 0x434f7c — VIBE_Render_UnpackColor
void UnpackColor(const ColorFormat& f, u32 pixel, u8& r, u8& g, u8& b) {
    r = (u8)(pixel >> f.rPos << f.rPrec);
    g = (u8)(pixel >> f.gPos << f.gPrec);
    b = (u8)(pixel >> f.bPos << f.bPrec);
}

} // namespace guild::render
