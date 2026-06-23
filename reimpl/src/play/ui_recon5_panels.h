#pragma once
// ui_recon5_panels.h — UI/panel cluster, pure layout/gating/state math extracted
// 1:1 from gilde.exe. The heavy modal/Form shells (Form window create/populate
// [rule 4 SDL], GPU blit [rule 3 Vulkan], frame-loop pump, command queue) are
// deferred behind the InertHooks struct (default no-ops) so this unit builds
// headless with no third-party libs. Everything here is the arithmetic /
// dispatch / gating that is provably independent of those subsystems.
//
// Reconstructed functions (see .cpp for full provenance):
//   0x5596ce VIBE_Hud_ClearStatusFlagBit2_Thunk
//   0x5596db VIBE_Hud_ClearStatusFlagBit4_Thunk
//   0x5596e8 VIBE_Hud_ClearStatusFlagBit8_Thunk
//   0x4b1ba8 VIBE_PlayerBar_Create               (slot-table init loop)
//   0x548c54 VIBE_ActionDialog_AbductChooseDestination (gating + cmd constants)
//   0x55bff8 VIBE_InfoPanel_RunPlayerInfoWindow   (bitfield->msgid + gating)
//   0x55a9f8 VIBE_MapTable_RunCityTowerScene      (4-corner pennant interp)
//   0x5441d0 VIBE_MapView_PanelDispatcher         (dispatch flags + sort + clamp)
//   0x4ba614 VIBE_Hud_UpdateSelectionAndTargets   (selection state reset)
//
// Types per project convention.
#include "guild/common/types.h"
#include <cstddef>

namespace guild {
namespace play {

using f32 = float;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;

// ---------------------------------------------------------------------------
// 0x5596ce / 0x5596db / 0x5596e8 — VIBE_Hud_ClearStatusFlagBit{2,4,8}_Thunk
//
//   LOBYTE(STACK[0x308]) &= ~N;  JUMPOUT(0x559533);
//
// Each thunk clears one bit of the HUD-mode status byte then jumps back into
// the shared HUD-mode handler at 0x559533. The pure part is the bit-clear on a
// u8 status byte. We model that as a value-in/value-out helper.
inline u8 HudClearStatusFlagBit2(u8 status) { return static_cast<u8>(status & ~2u); }
inline u8 HudClearStatusFlagBit4(u8 status) { return static_cast<u8>(status & ~4u); }
inline u8 HudClearStatusFlagBit8(u8 status) { return static_cast<u8>(status & ~8u); }

// ---------------------------------------------------------------------------
// 0x4b1ba8 — VIBE_PlayerBar_Create  (slot-table reset loop @0x4b1d17)
//
// On (re)creation of the player bar the parallel drag-slot tables are reset.
// The original walks i over [0,320) in steps of 10 (so 32 logical slots), and
// for each slot stores the sentinel/-1 init values into six parallel dword
// arrays plus zeroing a status byte. We reproduce the exact stride and the
// exact init values per slot.
struct PlayerBarSlot {
    i32 objA   = -1;       // dword_11BB6F8[i]
    i32 objB   = -1;       // dword_11BB700[i]
    i32 objC   = -1;       // dword_11BB704[i]
    u32 handle = 0xFFFFu;  // dword_11BB708[i]  (0xFFFF, not -1)
    i32 objD   = -1;       // dword_11BB70C[i]
    i32 objE   = -1;       // dword_11BB710[i]
    u8  flag   = 0;        // byte_11BB714[i*4]
};
inline constexpr int kPlayerBarSlotCount = 32;   // 320 / 10
inline constexpr int kPlayerBarSlotStride = 10;  // i += 10 each iter
// Fill `out` (>= kPlayerBarSlotCount entries) with the reset state. Returns count.
int PlayerBarResetSlots(PlayerBarSlot* out, int cap);

// ---------------------------------------------------------------------------
// 0x548c54 — VIBE_ActionDialog_AbductChooseDestination  (gating + constants)
//
// Two skill-requirement gates and an entity-count gate decide whether the
// destination picker is shown; the build-op uses fixed constants. We model the
// gate decision as a small enum so callers/tests can verify the control flow
// 1:1 without the Form shell.
enum class AbductGate {
    DenySkillLevel2,   // !CheckSkillRequirement(...,2)  -> return (no UI)
    DenyNullTarget,    // a1 == 0                        -> return
    DenyOfficePick,    // RunOfficeOverviewWindow #1 == 0
    DenyOfficeConfirm, // RunOfficeOverviewWindow #2 == 0
    NoEntities,        // CountMatchingEntities <= 0     -> messagebox 4969
    DenySkillLevel3,   // !CheckSkillRequirement(...,3)  -> edge-scroll, no picker
    ShowPicker         // all gates passed -> open perga_rolle picker
};
// Pure gate evaluation. Inputs are the already-computed predicate results in
// the exact order the original tests them.
AbductGate AbductEvalGate(bool skill2_ok, bool target_nonnull,
                          bool office_pick_ok, bool office_confirm_ok,
                          i32 entity_count, bool skill3_ok);
// Action constants written into the command request when the player confirms.
inline constexpr u8  kAbductActionKind   = 47;  // v22 = 47
inline constexpr u8  kAbductCmdTag       = 9;   // v25 = 9
inline constexpr i32 kAbductBuildOp      = 90;  // RequestBuildOp90
inline constexpr i32 kAbductBuildOpArg   = -3;  // RequestBuildOp90(.., -3)
inline constexpr u16 kAbductPickHeaderId = 4978;
inline constexpr u16 kAbductDestNameId   = 4979;
inline constexpr u16 kAbductNoneMsgId    = 4969;
inline constexpr u16 kAbductDoneMsgId    = 4981;
inline constexpr u32 kAbductFrameLoopId  = 423879;
// Rich-string template id selects singular/plural: base 4965, +1 when count==1.
inline u16 AbductListTextId(i32 entity_count) {
    return static_cast<u16>(4965 + (entity_count != 1 ? 1 : 0));
}

// ---------------------------------------------------------------------------
// 0x55bff8 — VIBE_InfoPanel_RunPlayerInfoWindow  (bitfield -> message id)
//
// The "traits/crimes" list section walks several bitfields on the person
// record and, for each non-zero mask, appends one text row. The pure logic is
// the mask -> (text id, advance row?) mapping in the exact order tested. We
// expose it as a table-driven evaluation that produces the ordered list of
// 4657..4665 message ids that would be emitted given the record's flag dwords.
struct InfoPanelTraitRow { u16 textId; bool advanceRow; };
// Inputs mirror the exact masks the original tests, in order:
//   a1[22]&0x0F        -> 4657 (adv)
//   a1[22]&0xF0        -> 4658 (adv)
//   a1[45]&0x0F        -> 4659 (adv)   (byte at +45)
//   a1[45]&0x30        -> 4660 (++)    (no separate row advance var)
//   a1[11]&0x1C000     -> 4661 (adv)
//   a1[23]&0x0E        -> 4662 (adv)
//   a1[23]&0x70        -> 4663 (++)
//   a1[23]&0x180       -> 4664 (adv)
//   a1[47]&0x1E        -> 4665 (no adv)
// `traitsWordEnabled` is `*((_DWORD*)a1+11) != 0` (the whole block is gated).
// Returns number of rows written to `out` (out must hold >= 9).
int InfoPanelTraitRows(bool traitsWordEnabled,
                       u32 word22, u32 word45, u32 word11,
                       u32 word23, u32 word47,
                       InfoPanelTraitRow* out, int cap);
// Profession-row birth/origin selection bases (used by $L%s rich rows):
//   sex bit a1[9]: father-origin base 370 (sex) / 294 (no sex)   (+356 path)
//   sex bit a1[9]: mother-origin base 498 (sex) / 471 (no sex)   (+357 path)
//   sex bit a1[9]: profession base   560 (sex) / 525 (no sex)
inline i32 InfoPanelFatherBase(bool sexBit) { return sexBit ? 370 : 294; }
inline i32 InfoPanelMotherBase(bool sexBit) { return sexBit ? 498 : 471; }
inline i32 InfoPanelProfBase  (bool sexBit) { return sexBit ? 560 : 525; }
// Age threshold text: qword>=117 -> 1067 else 1070.
inline i32 InfoPanelAgeTextId(i64 ageQuad) { return ageQuad >= 117 ? 1067 : 1070; }

// ---------------------------------------------------------------------------
// 0x55a9f8 — VIBE_MapTable_RunCityTowerScene  (4-corner pennant interpolation)
//
// Four pennants are placed by bilinear interpolation between the two map-table
// corner dummies (LINKS_OBEN, RECHTS_UNTEN). For corner i the offset is:
//   dx = (rightUnten.x - linksOben.x)
//   dz = (rightUnten.z - linksOben.z)        // [+84] minus [+84]
//   pos.x = linksOben.x + cornerU[i]*scaleU * dx
//   pos.z = linksOben.z + cornerV[i]*scaleV * dz
// where cornerU/V[i] come from dword_13CD6E0/13CD6E4 (per-corner 0/1 selectors,
// runtime data) and scaleU/scaleV from flt_624928/flt_62492C. We reproduce the
// arithmetic; selectors+scales are passed in. Pennant flag bits (object +529,
// +530) are set by the Form layer (deferred).
inline constexpr int kCityTowerPennantCount = 4;
struct Vec2 { f32 x; f32 z; };
Vec2 CityTowerPennantPos(const Vec2& linksOben, const Vec2& rechtsUnten,
                         f32 cornerU, f32 cornerV, f32 scaleU, f32 scaleV);
inline constexpr u32 kCityTowerFrameLoopId = 425983;

// ---------------------------------------------------------------------------
// 0x5441d0 — VIBE_MapView_PanelDispatcher  (dispatch structure)
//
// The dispatcher's behavior is driven by the al-mode bitmask (v256). These are
// the dispatch flag bits, recovered 1:1 from the `(v256 & X)` tests:
inline constexpr u8 kMapViewModeTooltip   = 0x10; // select window 2, raise z
inline constexpr u8 kMapViewModeAuflauer  = 0x04; // sp_AUFLAUERLEGEN markers
inline constexpr u8 kMapViewModeMissions  = 0x08; // dword_123343C mission markers
inline constexpr u8 kMapViewModeReturnObj = 0x01; // click -> return marker entity
inline constexpr u8 kMapViewModePersonSel = 0x02; // click -> person-selection sub
// Person query filter literal recovered from QueryBegin(1,2,5,9,4, player):
//   skip object types 68, 69; skip if (entity[90]&1); skip type 71 unless Begin.
inline constexpr u8 kMapViewSkipTypeA = 68;
inline constexpr u8 kMapViewSkipTypeB = 69;
inline constexpr u8 kMapViewTypeMoney = 71; // FormatGoldLabel vs name-copy branch
inline constexpr int kMapViewMaxMarkers = 256; // v13 >= 256 break
inline constexpr int kMapViewMarkerStride = 24; // bytes per marker entry (6 dwords)
inline constexpr u32 kMapViewFrameLoopId = 423879;

// Marker entry as sorted by the dispatcher (the 6-dword stride: obj, entity,
// screenX, screenY, worldX(float), worldZ(float)). Only screenY (+3) is the
// sort key.
struct MapMarker {
    i32 obj;     // +0
    i32 entity;  // +1
    i32 screenX; // +2
    i32 screenY; // +3  (sort key)
    f32 worldX;  // +4
    f32 worldZ;  // +5
};
// Selection-sort of markers ascending by screenY (the original's double loop at
// 0x54457e: swap when v178[j+3] < v178[i+3]). Stable behavior reproduced.
void MapViewSortMarkersByScreenY(MapMarker* m, int count);

// Scroll-edge key -> (dx,dy) mapping, recovered from the byte_671E28/2B/2D/30
// chain (checked in this order; first hit wins; step 4):
//   671E28 -> (0,-4)   671E30 -> (0,+4)
//   671E2B -> (-4,0)   671E2D -> (+4,0)
struct MapScrollDelta { i32 dx; i32 dy; bool active; };
MapScrollDelta MapViewScrollDelta(bool up, bool down, bool left, bool right);

// Corner-snap clamp of the focused-object marker into the visible map rect.
// Recovered from 0x544cc7..0x544d20:  x clamped to [0, mapW-512],
// y clamped to [0, mapH-360], lower clamp applied first then upper.
struct MapClampPos { i32 x; i32 y; };
MapClampPos MapViewClampFocus(i32 x, i32 y, i32 mapW, i32 mapH);
inline constexpr i32 kMapViewClampMarginX = 512;
inline constexpr i32 kMapViewClampMarginY = 360;
// Radio-group button Y coordinates (0x544d79..0x544e50), fixed x=432, count 8:
//   88,166,218,270,354,406,458,536  with sprite ids 1334,1338,1340,1337,
//   1335,1339,1336,1341.
inline constexpr int kMapViewRadioCount = 8;
extern const i32 kMapViewRadioY[kMapViewRadioCount];
extern const i32 kMapViewRadioSprite[kMapViewRadioCount];

// ---------------------------------------------------------------------------
// 0x4ba614 — VIBE_Hud_UpdateSelectionAndTargets  (selection state reset)
//
// Pure part: after the (deferred) target-issue branches, the function walks the
// 768-entry selection table (stride 536 bytes), shadows the per-entry selection
// byte into a previous-frame array (byte_11B4F1F/20), counts selected entries
// into dword_6317B0, and — if a clear condition holds — zeroes the table again.
// We reproduce the selection bookkeeping over a logical bool table.
inline constexpr int kHudSelectionCount  = 768;
inline constexpr int kHudSelectionStride  = 536; // bytes; logical index = byte/536
// Update prev[] from cur[], return number of currently-selected entries.
// Reproduces the loop body's shadow+count (the Mesh recolor side effect is
// deferred). `prev` and `cur` hold kHudSelectionCount bools.
int HudShadowSelection(const u8* cur, u8* prev, int count, int* selectedCount);
// Clear condition at 0x4ba777: if (mode2 && selectedCount) clear all -> 0.
inline bool HudShouldClearAfterCount(bool mode2, int selectedCount) {
    return mode2 && selectedCount != 0;
}

} // namespace play
} // namespace guild
