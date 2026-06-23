// ui_recon5_panels.cpp — pure layout/gating/state math for the UI/panel cluster.
// 1:1 translations from gilde.exe (imagebase 0x400000). The modal Form shells,
// GPU blit, frame-loop pump and command-queue side effects are deferred (the
// callers in src/gui, src/world already route those through hook structs); this
// unit holds only the arithmetic / dispatch / gating that is subsystem-free.
#include "play/ui_recon5_panels.h"

namespace guild {
namespace play {

// ---------------------------------------------------------------------------
// gilde.exe 0x4b1ba8 — VIBE_PlayerBar_Create  (init loop @0x4b1d17)
//
//   for ( i = 0; i != 320; byte_11BB714[i*4] = 0 ) {
//       i += 10;
//       dword_11BB6F8[i] = -1;
//       dword_11BB700[i] = -1;
//       dword_11BB704[i] = -1;
//       dword_11BB708[i] = 0xFFFF;
//       dword_11BB70C[i] = -1;
//       dword_11BB710[i] = -1;
//   }
//
// Note the original increments i by 10 *before* the stores and zeroes
// byte_11BB714[i*4] at loop-tail; with i ending at 320 the loop body runs for
// i = 10,20,...,320 => 32 iterations. Each iteration is one logical slot. We
// reproduce 32 slots with the identical sentinel values (note: handle field is
// 0xFFFF, the others are -1).
int PlayerBarResetSlots(PlayerBarSlot* out, int cap) {
    int n = 0;
    for (int i = 0; i != 320 && n < cap; i += kPlayerBarSlotStride) {
        out[n].objA   = -1;
        out[n].objB   = -1;
        out[n].objC   = -1;
        out[n].handle = 0xFFFFu;
        out[n].objD   = -1;
        out[n].objE   = -1;
        out[n].flag   = 0;
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x548c54 — VIBE_ActionDialog_AbductChooseDestination  (gate FSM)
//
// Control flow:
//   if (!CheckSkillRequirement(rec,2)) return;            -> DenySkillLevel2
//   if (!a1) return;                                      -> DenyNullTarget
//   v4 = RunOfficeOverviewWindow(...); if (!v4) return;   -> DenyOfficePick
//   if (!RunOfficeOverviewWindow(...)) return;            -> DenyOfficeConfirm
//   count = CountMatchingEntities(...);
//   if (count <= 0) { messagebox(4969); goto edge-scroll; }-> NoEntities
//   if (!CheckSkillRequirement(rec,3)) { edge-scroll; }   -> DenySkillLevel3
//   ... open perga_rolle picker ...                       -> ShowPicker
AbductGate AbductEvalGate(bool skill2_ok, bool target_nonnull,
                          bool office_pick_ok, bool office_confirm_ok,
                          i32 entity_count, bool skill3_ok) {
    if (!skill2_ok)          return AbductGate::DenySkillLevel2;
    if (!target_nonnull)     return AbductGate::DenyNullTarget;
    if (!office_pick_ok)     return AbductGate::DenyOfficePick;
    if (!office_confirm_ok)  return AbductGate::DenyOfficeConfirm;
    if (entity_count <= 0)   return AbductGate::NoEntities;
    if (!skill3_ok)          return AbductGate::DenySkillLevel3;
    return AbductGate::ShowPicker;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x55bff8 — VIBE_InfoPanel_RunPlayerInfoWindow  (trait-row table)
//
// The block is gated on (*((_DWORD*)a1+11) != 0) (== word11 != 0 here). Each
// mask test, in order, appends one row when the masked bits are non-zero. The
// "advanceRow" column tracks which branches bump the row counter v90 by 1
// versus the two branches that use a bare ++v90 (functionally identical for our
// purposes but we keep the column to mirror the source shape exactly).
int InfoPanelTraitRows(bool traitsWordEnabled,
                       u32 word22, u32 word45, u32 word11,
                       u32 word23, u32 word47,
                       InfoPanelTraitRow* out, int cap) {
    int n = 0;
    if (!traitsWordEnabled)
        return 0;
    auto emit = [&](bool cond, u16 id, bool adv) {
        if (cond && n < cap) { out[n].textId = id; out[n].advanceRow = adv; ++n; }
    };
    emit((word22 & 0x0Fu)    != 0, 4657, true);  // 0x55c50b
    emit((word22 & 0xF0u)    != 0, 4658, true);  // 0x55c588
    emit((word45 & 0x0Fu)    != 0, 4659, true);  // 0x55c605
    emit((word45 & 0x30u)    != 0, 4660, false); // 0x55c683 (bare ++v90)
    emit((word11 & 0x1C000u) != 0, 4661, true);  // 0x55c6fc
    emit((word23 & 0x0Eu)    != 0, 4662, true);  // 0x55c77a
    emit((word23 & 0x70u)    != 0, 4663, false); // 0x55c7f8 (bare ++v90)
    emit((word23 & 0x180u)   != 0, 4664, true);  // 0x55c86f
    emit((word47 & 0x1Eu)    != 0, 4665, false); // 0x55c8ed (no advance)
    return n;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x55a9f8 — VIBE_MapTable_RunCityTowerScene  (corner interpolation)
//
//   v22 = rechtsUnten.x[+76] - linksOben.x[+76];
//   v20[0] = rechtsUnten.z[+84] - linksOben.z[+84];
//   v16 = linksOben.x; v18 = linksOben.z;
//   v7 = cornerU[i] * scaleU;  v8 = cornerV[i] * scaleV;
//   v16 = v7 * v22 + v16;      v18 = v8 * v20[0] + v18;
Vec2 CityTowerPennantPos(const Vec2& linksOben, const Vec2& rechtsUnten,
                         f32 cornerU, f32 cornerV, f32 scaleU, f32 scaleV) {
    const f32 dx = rechtsUnten.x - linksOben.x;
    const f32 dz = rechtsUnten.z - linksOben.z;
    Vec2 r;
    r.x = static_cast<f32>(static_cast<double>(cornerU * scaleU) * dx) + linksOben.x;
    r.z = static_cast<f32>(static_cast<double>(cornerV * scaleV) * dz) + linksOben.z;
    return r;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5441d0 — VIBE_MapView_PanelDispatcher
//
// Radio-group buttons: AddToWindow(window, 432, Y, spriteId) for 8 rows.
const i32 kMapViewRadioY[kMapViewRadioCount] = {
    88, 166, 218, 270, 354, 406, 458, 536
};
const i32 kMapViewRadioSprite[kMapViewRadioCount] = {
    1334, 1338, 1340, 1337, 1335, 1339, 1336, 1341
};

// Selection sort by screenY (+3), swapping whole 6-dword records.
//   for (i=0; i<count-1; i++)
//     for (j=i+1; j<count; j++)
//       if (m[j].screenY < m[i].screenY) swap(m[i], m[j]);
void MapViewSortMarkersByScreenY(MapMarker* m, int count) {
    for (int i = 0; i + 1 < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (m[j].screenY < m[i].screenY) {
                MapMarker tmp = m[i];
                m[i] = m[j];
                m[j] = tmp;
            }
        }
    }
}

// Scroll-edge key chain. The original tests, in order:
//   byte_671E28 -> dx=0  dy=-4
//   byte_671E30 -> dx=0  dy=+4
//   byte_671E2B -> dx=-4 dy=0
//   byte_671E2D -> dx=+4 dy=0
// Window_Scroll is called as Window_Scroll(yDelta, xDelta, window); the first
// pair maps to vertical, the latter to horizontal. We surface (dx,dy).
MapScrollDelta MapViewScrollDelta(bool up, bool down, bool left, bool right) {
    if (up)    return { 0, -4, true };
    if (down)  return { 0,  4, true };
    if (left)  return { -4, 0, true };
    if (right) return { 4,  0, true };
    return { 0, 0, false };
}

// Focus marker clamp. Source order (0x544cc7..):
//   if (x < 0) x = 0;            else x = x;
//   if (mapW-512 < x) x = mapW-512;
//   if (y < 0) y = 0;            else y = y;
//   if (mapH-360 < y) y = mapH-360;
MapClampPos MapViewClampFocus(i32 x, i32 y, i32 mapW, i32 mapH) {
    if (x < 0) x = 0;
    if (mapW - kMapViewClampMarginX < x) x = mapW - kMapViewClampMarginX;
    if (y < 0) y = 0;
    if (mapH - kMapViewClampMarginY < y) y = mapH - kMapViewClampMarginY;
    return { x, y };
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4ba614 — VIBE_Hud_UpdateSelectionAndTargets  (selection shadow)
//
// LABEL_9 loop (0x4ba6ae..0x4ba761): walks v4 in [0,768), reading the per-entry
// selection byte byte_12CEA98 (stride 536), and at loop tail copies it into the
// previous-frame array byte_11B4F1F[v4+1] (== byte_11B4F20[v4]). When an entry
// is selected, ++dword_6317B0 (the selected count). We reproduce the shadow and
// the count over a logical bool table (the per-entry mesh recolor + drag-slot
// add are deferred side effects).
int HudShadowSelection(const u8* cur, u8* prev, int count, int* selectedCount) {
    int sel = 0;
    int written = 0;
    for (int i = 0; i < count; ++i) {
        u8 c = cur[i];
        if (c) ++sel;
        prev[i] = c;       // byte_11B4F20[v4] = prev-frame value
        ++written;
    }
    if (selectedCount) *selectedCount = sel;
    return written;
}

} // namespace play
} // namespace guild
