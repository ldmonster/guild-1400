#pragma once
// guild::gui — the player's owned-object slot bar (bottom HUD strip).
//
// gilde.exe 0x4b11e4 — VIBE_PlayerBar_BuildContent rebuilds, each frame, the row of up
// to 32 owned-object slots: for each visible scene object owned by the player it
// allocates a slot, lays out an icon + a quantity/name label + an output-ratio sub-bar
// at a fixed per-slot pitch, and sets the slot's toggle value.  The slot bookkeeping
// lives in eight parallel stride-10 arrays (dword_11BB720..744, base dword_11BB6F8),
// each holding 32 slots.
//
// This module recovers the LAYOUT (per-slot y pitch + the icon/label/sub-window
// offsets) and the slot-table allocate/find/reset bookkeeping byte-for-byte, plus the
// output-ratio percentage math.  The per-frame scene-table walk that decides *which*
// objects get a slot, and the actual sprite/3D-object creation, are owned by the
// scene/render clusters and are forward-declared; here the content build is driven by
// a supplied list of owned-object ids so the layout is testable in isolation.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Slot table — eight parallel stride-10 arrays of 32 slots (dword_11BB6F8 base).
//   +0 (dword_11BB6F8) marker (init 0)
//   +1 (dword_11BB700) icon-anim widget A  (init -1)   -> dword_11BB728 in body
//   +2 (dword_11BB704) icon-anim widget B  (init -1)   -> dword_11BB72C
//   +3 (dword_11BB708) value word          (init 0xFFFF)-> dword_11BB730 (object id)
//   +4 (dword_11BB70C) (init -1)                        -> dword_11BB734 (label)
//   +5 (dword_11BB710) (init -1)                        -> dword_11BB720 (toggle btn)
// The body uses dword_11BB720 (button), 724 (icon), 728/72C (anim A/B), 730 (objId),
// 734 (label), 738 (sub-window), 740 (toggle flag).  We name them by role.
// ---------------------------------------------------------------------------
inline constexpr int kPlayerBarSlots = 32;
inline constexpr u16 kPlayerBarFree  = 0xFFFF; // empty slot marker (objId field)

// Per-slot layout pitch + offsets (recovered from the AddToWindow calls in the body):
//   row pitch       : 78 px               (78 * slotIndex)
//   icon button x/y : x=6,  y=78*i        (1403 icon)
//   icon sprite x/y : x=8,  y=78*i + 17
//   name label  x/y : x=0,  y=78*i + 4    (width 95)
//   sub-window  x/y : x=15, y=78*i + 63   (w=80, h=6) for output-bar buildings
inline constexpr int kPlayerBarRowPitch    = 78;
inline constexpr int kPlayerBarIconX       = 6;
inline constexpr int kPlayerBarSpriteX     = 8;
inline constexpr int kPlayerBarSpriteDY    = 17;
inline constexpr int kPlayerBarLabelX      = 0;
inline constexpr int kPlayerBarLabelDY     = 4;
inline constexpr int kPlayerBarLabelWidth  = 95;
inline constexpr int kPlayerBarSubWinX     = 15;
inline constexpr int kPlayerBarSubWinDY    = 63;
inline constexpr int kPlayerBarSubWinW     = 80;
inline constexpr int kPlayerBarSubWinH     = 6;

// Output-ratio percentage scale (dbl_61DD00 = 100.0): pct = (int)(ratio * 100.0).
inline constexpr double kOutputRatioScale = 100.0;

struct PlayerBarSlot {
    u16 objId;     // dword_11BB730 (0xFFFF == free)
    int button;    // dword_11BB720 toggle button widget
    int icon;      // dword_11BB724 icon widget
    int animA;     // dword_11BB728
    int animB;     // dword_11BB72C
    int label;     // dword_11BB734
    int subWindow; // dword_11BB738 (-1 == none)
    int toggle;    // dword_11BB740 (toggle value snapshot)
    // Resolved layout for this slot (computed by PlayerBar_SlotLayout).
};
extern PlayerBarSlot g_playerBarSlots[kPlayerBarSlots];

// Reset the slot table to the PlayerBar_Create init state (objId=0xFFFF, widget
// fields = -1, toggle = 0).  Mirrors the init loop at 0x4b1d17.
void ResetPlayerBar();

// Per-slot resolved layout (screen-relative positions within the bar window).
struct PlayerBarLayout {
    int rowY;     // 78 * slot
    int iconX, iconY;
    int spriteX, spriteY;
    int labelX, labelY, labelWidth;
    int subWinX, subWinY, subWinW, subWinH;
};

// gilde.exe 0x4b11e4 — compute slot `slotIndex`'s layout (the AddToWindow coordinate
// math).  Pure function of the slot index; matches the offsets used in BuildContent.
PlayerBarLayout PlayerBar_SlotLayout(int slotIndex);

// gilde.exe 0x4b11e4 (the de-dup + free-slot scan).  Find the slot already holding
// `objId` (stride-10 scan for dword_11BB730 == objId); returns its index, or -1.
int PlayerBar_FindSlot(u16 objId);

// gilde.exe 0x4b11e4 (the v45 first-free scan: dword_11BB730 != 0xFFFF until a free
// slot).  Returns the lowest free slot index (objId == 0xFFFF), or -1 when the bar is
// full (>32).
int PlayerBar_FindFreeSlot();

// gilde.exe 0x4b11e4 — assign object `objId` to its slot: reuse the existing slot if
// present, else take the first free slot, set objId, clear the toggle.  Returns the
// slot index used, or -1 when full.
int PlayerBar_AssignSlot(u16 objId);

// gilde.exe 0x4b11e4 — output-ratio label percentage: pct = (int)(ratio * 100.0).
// `ratio` is VIBE_Building_ComputeOutputRatio's [0..1] value; returns the integer
// percent shown in the "%s %i%%" label.
int PlayerBar_OutputRatioPercent(double ratio);

} // namespace guild::gui
