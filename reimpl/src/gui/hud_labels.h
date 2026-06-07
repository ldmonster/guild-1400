#pragma once
// guild::gui — HUD floating-text label overlay (the "what is this" captions that hover
// over in-world objects, animals, characters, gates, the damage numbers, the
// name-input caption, and the status banner).
//
// Every one of these drawers shares the same shape: walk a table of live objects,
// project each to screen space via VIBE_Object_ComputeScreenBounds (a 4-int
// {left, top, right, bottom} box), then blit a fixed 160px-wide text label CENTRED
// over the box and one line up.  This module recovers that LAYOUT/PLACEMENT/EXPIRY
// math byte-for-byte:
//
//   * the centred-caption placement  x = left + (right-left)/2 - 80 ; y = top
//     (VIBE_Hud_DrawObjectNameLabels @0x4bbaec, DrawAnimalLabels @0x4bbbbc,
//      DrawCharacterLabels @0x4bbc8c, DrawGateLabel @0x4bb8e4, DrawDamageLabels
//      @0x4bb7a0 — all use the same `bounds[0] + (bounds[2]-bounds[0])/2 - 80`).
//   * the damage-label expiry sweep (timestamp + 300 < now => free) — the first loop
//     of VIBE_Hud_DrawDamageLabels @0x4bb7a0.
//   * the status-banner / name-input caption timeout + placement
//     (VIBE_Hud_DrawStatusBanner @0x4bcb74, DrawNameInputCaption @0x4bcafc).
//   * the player-owned object flag-marking pass (VIBE_Hud_MarkOwnedObjects @0x4bacb4).
//
// The glyph blit (VIBE_Animation_Apply / VIBE_State_GetCurrent), the 3D projection
// (VIBE_Object_ComputeScreenBounds), and the world iteration (GameObject_QueryFind)
// are the renderer/world cluster's job; here we recover the pure placement and the
// table walk, and route the blit through a mockable label sink.

#include "gui/types.h"
#include <vector>

namespace guild::gui {

// ===========================================================================
// Centred-caption placement — the math shared by every HUD label drawer.
// The original computes, from a projected bounds box {left, top, right, bottom}:
//     v5 = bounds[0] + (bounds[2] - bounds[0]) / 2 - 80;   // centred left x
//     v6 = bounds[1];                                       // top y
//     VIBE_Animation_Apply(v5, v6, 160, ..., text, charHeight);
// i.e. a 160px-wide label centred horizontally over the box, top-aligned.
// ===========================================================================
inline constexpr int kHudLabelWidth   = 160; // a3 to VIBE_Animation_Apply
inline constexpr int kHudLabelHalf    = 80;  // kHudLabelWidth / 2 (the `- 80` inset)
inline constexpr int kHudLabelCharH40 = 40;  // glyph height for object/animal/damage/gate
inline constexpr int kHudLabelCharH32 = 32;  // glyph height for character labels

// Projected screen bounds, the 4-int box VIBE_Object_ComputeScreenBounds writes:
//   [0] left, [1] top, [2] right, [3] bottom.
struct ScreenBounds { int left, top, right, bottom; };

struct LabelPlacement { int x; int y; };

// gilde.exe 0x4bbaec/.../0x4bb7a0 — the `bounds[0] + (bounds[2]-bounds[0])/2 - 80`
// + `bounds[1]` placement common to every centred HUD caption.
LabelPlacement Hud_CenteredLabelPlacement(const ScreenBounds& b);

// ===========================================================================
// Damage-label expiry sweep — gilde.exe 0x4bb7a0 (the first loop of
// VIBE_Hud_DrawDamageLabels).
//   for (i = 0; i != 4288; i += 67)
//     if (entry.inUse && entry.timestamp + 300 < now) entry.inUse = 0;
// Operates on the shared g_damageLabels table (owned by hud.cpp).  Returns the
// number of labels expired this sweep.
// ===========================================================================
inline constexpr int kDamageLabelLifetime = 300; // +300 < dword_62EB38 expiry window
int DamageLabel_ExpireSweep(int now);

// ===========================================================================
// Status banner — gilde.exe 0x4bcb74 (VIBE_Hud_DrawStatusBanner).
// A transient banner shown for ~350 ticks.  The original places it at
// x = rightEdge - 300, scales an animation by one of two factors depending on a
// flag bit, and clears its visible byte once `startTick + 350 <= now`.
// ===========================================================================
inline constexpr int   kStatusBannerLifetime = 350; // dword_631678 + 350 <= now
inline constexpr int   kStatusBannerRightInset = 300; // dword_69FFA4 - 300
inline constexpr float  kStatusBannerScaleNarrow = 505.0f;  // flt_61E194 (flag 0x8000 set)
inline constexpr float  kStatusBannerScaleWide   = 2047.0f; // flt_61E190 (flag clear)

// gilde.exe 0x4bcb74 — banner left x.  rightEdge is dword_69FFA4.
inline int Hud_StatusBannerX(int rightEdge) { return rightEdge - kStatusBannerRightInset; }

// gilde.exe 0x4bcb74 — the `dword_11BC2D0 & 0x8000` scale selector.
inline float Hud_StatusBannerScale(int modeFlags) {
    return (modeFlags & 0x8000) ? kStatusBannerScaleNarrow : kStatusBannerScaleWide;
}

// gilde.exe 0x4bcb74 / 0x4bcc0c — the banner's lifetime test.
//   visible while (startTick + 350 > now); expires (returns true) when reached.
inline bool Hud_StatusBannerExpired(int startTick, int now) {
    return startTick + kStatusBannerLifetime <= now;
}

// ===========================================================================
// Name-input caption — gilde.exe 0x4bcafc (VIBE_Hud_DrawNameInputCaption).
// The "type a name" caption floats above the named object's world anchor.  The
// original reads a packed 16.16 world position (dword_75BF46 hi-word = anchor) and
// places the caption at:
//     x = ((anchorHi2 >> 16)) - 54
//     y = ((anchorPos >> 16)) + 52
// width 160, glyph height 168.  We recover the offsets.
// ===========================================================================
inline constexpr int kNameInputCaptionDX = 54; // anchorX2 - 54
inline constexpr int kNameInputCaptionDY = 52; // anchorY + 52
inline constexpr int kNameInputCaptionH  = 168; // glyph height arg

// gilde.exe 0x4bcafc — caption placement from the two packed 16.16 anchor words.
//   anchorX2Packed = *(int*)((char*)&dword_75BF46 + 2)  (the x2 word)
//   anchorPacked   = dword_75BF46                       (the y word)
LabelPlacement Hud_NameInputCaptionPlacement(int anchorX2Packed, int anchorPacked);

// ===========================================================================
// Player-owned object marking — gilde.exe 0x4bacb4 (VIBE_Hud_MarkOwnedObjects).
// Walks a query result of owned-building records; for each, sets bit 0 of its
// +32 "flags" field when one of the live record-slots references it (matching the
// owner handle at record+1 against the building's +28 handle) and that slot's flag
// word has bit 0 set.  We model this as a pure pass over supplied tables so the
// flag-set logic is testable without the world iterator.
// ===========================================================================
// One live record-slot in the dword_12CE910/.. stride-268 table.
struct OwnerSlot {
    bool   active;     // word_12CE910[..] != -1 && byte_12CE912[..*2] == 1
    int    recordPtr;  // dword_12CEA7C[..]  (0 => skip)
    int    ownerHandle;// *(recordPtr + 1)   (matched against building+28)
    int    flagWord;   // dword_12CEAC4[..]  (bit 0 must be set)
};

// One owned-building record being marked.
struct OwnedObject {
    int  handle;  // building + 28 (the matched handle)
    int  flags;   // building + 32 (we OR bit 0 in when a slot matches)
};

// gilde.exe 0x4bacb4 — for each object, clear flags then OR in bit 0 if any active
// slot references it (slot.ownerHandle == obj.handle && (slot.flagWord & 1)).
// Mutates `objects` in place; returns the number of objects that ended up flagged.
int Hud_MarkOwnedObjects(std::vector<OwnedObject>& objects,
                         const std::vector<OwnerSlot>& slots);

} // namespace guild::gui
