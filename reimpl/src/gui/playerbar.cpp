#include "gui/playerbar.h"

namespace guild::gui {

PlayerBarSlot g_playerBarSlots[kPlayerBarSlots];

void ResetPlayerBar() {
    // PlayerBar_Create init loop @0x4b1d17: for i in [0,320) step 10 set the parallel
    // arrays to (-1,-1,-1,0xFFFF,-1,-1) and the marker byte to 0.
    for (int i = 0; i < kPlayerBarSlots; ++i) {
        g_playerBarSlots[i].objId     = kPlayerBarFree; // dword_11BB708 = 0xFFFF
        g_playerBarSlots[i].button    = -1;
        g_playerBarSlots[i].icon      = -1;
        g_playerBarSlots[i].animA     = -1; // dword_11BB700 = -1
        g_playerBarSlots[i].animB     = -1; // dword_11BB704 = -1
        g_playerBarSlots[i].label     = -1; // dword_11BB70C = -1
        g_playerBarSlots[i].subWindow = -1; // dword_11BB710 = -1
        g_playerBarSlots[i].toggle    = 0;
    }
}

// gilde.exe 0x4b11e4 — slot layout (78px row pitch; the AddToWindow x/y constants).
PlayerBarLayout PlayerBar_SlotLayout(int slotIndex) {
    PlayerBarLayout L{};
    int y = kPlayerBarRowPitch * slotIndex; // 78 * v45
    L.rowY       = y;
    L.iconX      = kPlayerBarIconX;          // AddToWindow(win, 78*i, 6, 1403)
    L.iconY      = y;
    L.spriteX    = kPlayerBarSpriteX;        // AddToWindow(win, 78*i + 17, 8, gfx)
    L.spriteY    = y + kPlayerBarSpriteDY;
    L.labelX     = kPlayerBarLabelX;         // AddTextLabel(0, 78*i + 4, win)
    L.labelY     = y + kPlayerBarLabelDY;
    L.labelWidth = kPlayerBarLabelWidth;     // width 95 stored to +20
    L.subWinX    = kPlayerBarSubWinX;        // AddChildWindow(6, 78*i + 63, 15, 80)
    L.subWinY    = y + kPlayerBarSubWinDY;
    L.subWinW    = kPlayerBarSubWinW;
    L.subWinH    = kPlayerBarSubWinH;
    return L;
}

// gilde.exe 0x4b11e4 (the v43 de-dup scan: dword_11BB730[v44] == v15).
int PlayerBar_FindSlot(u16 objId) {
    for (int i = 0; i < kPlayerBarSlots; ++i) {
        if (g_playerBarSlots[i].objId == objId)
            return i;
    }
    return -1;
}

// gilde.exe 0x4b11e4 (the v45 first-free scan: while dword_11BB730[v46] != 0xFFFF).
int PlayerBar_FindFreeSlot() {
    int slot = 0; // v45
    if (g_playerBarSlots[0].objId != kPlayerBarFree) {
        // v46 = 10*slot; guard `v46 < 320` == `slot < 32` protects the array read.
        do {
            ++slot;
        } while (slot < kPlayerBarSlots && g_playerBarSlots[slot].objId != kPlayerBarFree);
    }
    // Original: the v46 loop guard is `v46 < 320` (== slot < 32), so a full bar exits
    // the scan with slot == 32. The original's own `if (v45 > 32) break;` never fires as
    // the full-detector (that role is the earlier `if (v43 < 32) goto LABEL_22` de-dup
    // guard, which skips the assign entirely when the bar is full). Isolated here, "full"
    // is slot == 32 -> -1, which is the only safe in-bounds result for this helper.
    if (slot >= kPlayerBarSlots)
        return -1;
    return slot;
}

int PlayerBar_AssignSlot(u16 objId) {
    int existing = PlayerBar_FindSlot(objId);
    if (existing >= 0)
        return existing;
    int slot = PlayerBar_FindFreeSlot();
    if (slot < 0)
        return -1;
    g_playerBarSlots[slot].objId  = objId; // dword_11BB730[10*v45] = v15
    g_playerBarSlots[slot].toggle = 0;     // byte_11BB73C[..] = 0
    return slot;
}

// gilde.exe 0x4b11e4 — pct = (int)(ratio * 100.0)  (dbl_61DD00 = 100.0).
int PlayerBar_OutputRatioPercent(double ratio) {
    return (int)(ratio * kOutputRatioScale);
}

} // namespace guild::gui
