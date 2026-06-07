#include "gui/hud_labels.h"
#include "gui/hud.h"   // g_damageLabels / kDamageLabel* table (owned by hud.cpp)

namespace guild::gui {

// ---------------------------------------------------------------------------
// Centred-caption placement — gilde.exe 0x4bbaec (and the identical computation in
// DrawAnimalLabels 0x4bbbbc, DrawCharacterLabels 0x4bbc8c, DrawGateLabel 0x4bb8e4,
// DrawDamageLabels 0x4bb7a0).
//   v5 = v7 + (v9 - v7) / 2 - 80;   // bounds[0] + (bounds[2]-bounds[0])/2 - 80
//   v6 = v8;                        // bounds[1]
// VIBE_Object_ComputeScreenBounds writes {left=[0], top=[1], right=[2], bottom=[3]};
// the drawers read [0] as v7, [1] as v8, [2] as v9.  The `/2` is the original signed
// integer divide (truncates toward zero), preserved here for bit-exactness.
// ---------------------------------------------------------------------------
LabelPlacement Hud_CenteredLabelPlacement(const ScreenBounds& b) {
    LabelPlacement p;
    p.x = b.left + (b.right - b.left) / 2 - kHudLabelHalf;
    p.y = b.top;
    return p;
}

// ---------------------------------------------------------------------------
// Damage-label expiry sweep — gilde.exe 0x4bb7a0 (first loop).
//   for (i = 0; i != 4288; i += 67)
//     if (dword_11B73A8[i] && dword_11B73A4[i] + 300 < (unsigned)dword_62EB38)
//       dword_11B73A8[i] = 0;
// dword_11B73A8[i] is the entry's inUse/owner slot (+8 dwords); dword_11B73A4[i] is
// the timestamp (+4 dwords).  The comparison is UNSIGNED — preserve that so the
// tick wraparound matches the original exactly.
// ---------------------------------------------------------------------------
int DamageLabel_ExpireSweep(int now) {
    int expired = 0;
    unsigned un = static_cast<unsigned>(now);
    for (int i = 0; i < kDamageLabelCount; ++i) {
        if (g_damageLabels[i].inUse &&
            static_cast<unsigned>(g_damageLabels[i].timestamp + kDamageLabelLifetime) < un) {
            g_damageLabels[i].inUse = false;
            ++expired;
        }
    }
    return expired;
}

// ---------------------------------------------------------------------------
// Name-input caption placement — gilde.exe 0x4bcafc.
//   x = (*(int *)((char *)&dword_75BF46 + 2) >> 16) - 54;
//   y = (dword_75BF46 >> 16) + 52;
// Both anchor words are packed 16.16; the original takes the high (integer) half
// via an arithmetic >> 16 (sign-preserving), so use a signed shift here.
// ---------------------------------------------------------------------------
LabelPlacement Hud_NameInputCaptionPlacement(int anchorX2Packed, int anchorPacked) {
    LabelPlacement p;
    p.x = (anchorX2Packed >> 16) - kNameInputCaptionDX;
    p.y = (anchorPacked   >> 16) + kNameInputCaptionDY;
    return p;
}

// ---------------------------------------------------------------------------
// Player-owned object marking — gilde.exe 0x4bacb4.
// Original (per query-result object `i`):
//   *(i + 32) = 0;                                   // clear flags
//   for (j = 0; j != 205824; j += 268)               // scan live record-slots
//     if (word_12CE910[j] != -1 && byte_12CE912[j*2] == 1) {  // active slot
//       v3 = dword_12CEA7C[j/2];                      // slot record ptr
//       if (v3 && *(v3 + 1) == *(i + 28) && (dword_12CEAC4[j/2] & 1))
//         *(i + 32) |= 1;                             // mark owned
//     }
// We model the slot table + objects as supplied lists so the flag logic is testable
// without the GameObject iterator.  The `active` field folds the
// `word_12CE910 != -1 && byte_12CE912 == 1` precondition.
// ---------------------------------------------------------------------------
int Hud_MarkOwnedObjects(std::vector<OwnedObject>& objects,
                         const std::vector<OwnerSlot>& slots) {
    int markedCount = 0;
    for (OwnedObject& obj : objects) {
        obj.flags = 0;  // *(i + 32) = 0
        for (const OwnerSlot& slot : slots) {
            if (!slot.active)
                continue;
            if (slot.recordPtr == 0)  // if (v3) guard
                continue;
            if (slot.ownerHandle == obj.handle && (slot.flagWord & 1) != 0)
                obj.flags |= 1;  // *(i + 32) |= 1u
        }
        if ((obj.flags & 1) != 0)
            ++markedCount;
    }
    return markedCount;
}

} // namespace guild::gui
