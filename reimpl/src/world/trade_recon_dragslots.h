#pragma once
// Trade-panel drag-slot grid layout — gilde.exe (transport / trade panel).
//
// Translated functions (faithful coordinate-layout core):
//   VIBE_TradePanel_LayoutDragSlotsVariant 0x50b350  (row pitch 28, slider mode 3)
//   VIBE_TradePanel_LayoutDragSlotsWide    0x50c140  (row pitch 35, slider mode by *a5)
//   VIBE_TradeTransport_OpenPanelMode2_Thunk 0x54012c (thunk -> dispatcher(.,2))
//   VIBE_TradeTransport_OpenPanelMode4_Thunk 0x54013c (thunk -> dispatcher(.,4))
//
// Both Layout functions lay out the four "cargo column" drag-slot widgets of a
// trade/transport panel and the 16 per-good slider rows that stack under them.
// The two variants share IDENTICAL grid math; they differ only in
//   - the per-row vertical pitch fed into the column y-offset accumulator
//     (Variant: 28, Wide: 35), and
//   - the slider-row build mode (Variant always 3; Wide picks 1/2/3 from the
//     first word of an extra argument: 475 -> 1, 476 -> 2, otherwise 3).
//
// The faithful, testable core here is the pure coordinate computation that the
// originals perform over a family of parallel arrays. The live object/widget/
// form/dragslot tables (VIBE_Object_AddToWindow, VIBE_Widget_InitFromState,
// VIBE_Widget_LayoutBounds, VIBE_TradePanel_BuildSliderRow, VIBE_DragSlot_*) are
// GUI/entity-coupled leaves; they are routed through inert-default hooks so the
// branch structure and the computed (x,y) offsets are reproduced 1:1 without the
// live process state. See world/caravan_cargo.* for the cargo-value/load core and
// world/tradetransport.* for the cart cost/route rules core.
//
// Original parallel-array layout (each logical "column slot" k=0..3 lives at the
// dword index 5*k, i.e. the arrays are addressed with a stride of 5 dwords):
//   dword_122EE00[5k]  prevCount   (last frame's column item count)
//   dword_122EE04[5k]  newCount    (this frame's column item count)
//   dword_122EE08[5k]  objId       (-1 == not yet allocated)
//   dword_122EE14[5k]  colCarry    (per-column carry; walked as the slider-row
//                                   loop advances columns)
//   dword_122EDF8[5k]  xOff        (computed centered x offset)
//   dword_122EDFC[5k]  yOff        (computed stacked y offset)
// Slider-row output arrays (stride 14 dwords, row r=0..15 at index 14*r):
//   dword_122E098[14r] sliderX     (= 68*(rowInCol-1) + xOff[col])
//   dword_122E09C[14r] sliderY     (= yOff[col])
#include <array>

#include "guild/common/types.h"

namespace guild::world {

// Number of cargo "columns" laid out (the do/while bound: index 0,5,10,15 -> 4).
constexpr int kDragColumns = 4;
// Number of slider rows stacked under the columns (the v15 < 16 / j < 16 bound).
constexpr int kDragSliderRows = 16;

// Horizontal step between adjacent slider rows within one column (68 in both
// variants: `68 * (v13 - 1)`), and the widget-template threshold (>=2) below
// which a column is hidden instead of laid out.
constexpr int kSliderColPitch = 68;
constexpr int kColumnVisibleThreshold = 2;

// Per-row vertical pitch fed into the y-offset accumulator. Recovered from the
// `v7 += N` / `v8 += N` row-loop increment of each variant.
constexpr int kRowPitchVariant = 28; // 0x50b350: v7 += 28
constexpr int kRowPitchWide    = 35; // 0x50c140: v8 += 35

// Slider build mode selector words for the Wide variant (`*a5`). 475 -> mode 1,
// 476 -> mode 2, anything else -> mode 3 (0x50c311 / 0x50c394).
constexpr int kWideModeWordBuy  = 475;
constexpr int kWideModeWordSell = 476;

// Widget-template base index added to a column's item count before the template
// lookups (`v9 + 1715` / `v10 + 1715`).
constexpr int kWidgetTemplateBase = 1715;

// Inputs the originals read from the live tables, gathered into a plain struct so
// the math is reproduced exactly without the process heap. All values are the
// already-decoded integers the originals use after their `>> 16` / record reads.
struct DragLayoutInputs {
    // Form/window record (dword_67EB80[238 * dword_676A64[171*a1 + a2]]):
    //   panelW = *(int*)(rec + 6) >> 16       (high word at byte offset 6)
    //   formX  = *((u16*)rec + 2)             (word at byte offset 4)
    //   formY  = *((u16*)rec + 3)             (word at byte offset 6, low word)
    i32 panelW = 0;
    i32 formX  = 0;
    i32 formY  = 0;

    // Per-row stride from the widget data block: *(int*)(dword_62D204+144308) >> 16.
    i32 rowStride = 0;

    // This-frame item count per column (dword_122EE04[5k]). Determines visibility
    // (>= kColumnVisibleThreshold) and the widget-template width lookup.
    std::array<i32, kDragColumns> newCount{{0, 0, 0, 0}};

    // Per-column widget-template width: *(int*)(84*(newCount+1715)+dword_62D204+78) >> 16.
    // Only consulted for columns with newCount >= kColumnVisibleThreshold.
    std::array<i32, kDragColumns> templateW{{0, 0, 0, 0}};

    // Per-column carry value (dword_122EE14[5k]) consumed by the slider-row walk
    // when advancing past a filled column (the `while (v17 < 1)` skip).
    std::array<i32, kDragColumns> colCarry{{0, 0, 0, 0}};
};

// Persisted per-column state that survives across calls (the original keeps it in
// the dword_122EE00 / dword_122EE08 / dword_122EDF8 / dword_122EDFC globals).
struct DragLayoutState {
    std::array<i32, kDragColumns> prevCount{{0, 0, 0, 0}}; // reset() sets to -1
    std::array<i32, kDragColumns> objId{{-1, -1, -1, -1}};
    std::array<i32, kDragColumns> xOff{{0, 0, 0, 0}};
    std::array<i32, kDragColumns> yOff{{0, 0, 0, 0}};

    void reset() {
        prevCount = {-1, -1, -1, -1};
        objId     = {-1, -1, -1, -1};
        xOff      = {0, 0, 0, 0};
        yOff      = {0, 0, 0, 0};
    }
};

// One slider-row placement the layout produces (the dword_122E098/09C writes plus
// the column/row it was derived from and the mode that BuildSliderRow is called
// with for that row).
struct DragSliderPlacement {
    i32 sliderX = 0;   // dword_122E098[14r] = 68*(rowInCol-1) + xOff[col]
    i32 sliderY = 0;   // dword_122E09C[14r] = yOff[col]
    i32 column  = 0;   // resolved source column index (0..3)
    i32 rowInCol = 0;  // 1-based row position within that column (v13/v15)
    i32 buildMode = 3; // mode passed to VIBE_TradePanel_BuildSliderRow for this row
};

// Full result of one layout pass.
struct DragLayoutResult {
    // Per-column resolved offsets (only meaningful for visible columns; matches the
    // dword_122EDF8/DFC contents the original leaves for downstream reads).
    std::array<i32, kDragColumns> xOff{{0, 0, 0, 0}};
    std::array<i32, kDragColumns> yOff{{0, 0, 0, 0}};
    // Per-column visibility: false -> the original hides the widget (field 52 = 1);
    // true -> it lays the widget out at (formX+xOff, formY+yOff) (field 52 = 0).
    std::array<bool, kDragColumns> visible{{false, false, false, false}};
    // Absolute laid-out widget origin for visible columns: the 16-bit sum
    // sext16(u16(formX/Y) + u16(off)) — the original adds the two low words and
    // sign-extends the SUM (0x50b48b/0x50b49d, 0x50c282/0x50c294).
    std::array<i32, kDragColumns> widgetX{{0, 0, 0, 0}};
    std::array<i32, kDragColumns> widgetY{{0, 0, 0, 0}};
    // The 16 slider-row placements, in order.
    std::array<DragSliderPlacement, kDragSliderRows> rows{};
};

// gilde.exe 0x50b350 — VIBE_TradePanel_LayoutDragSlotsVariant (row pitch 28).
// Lays the four cargo columns out (centered x, stacked y) and walks the 16 slider
// rows, all with build mode 3. `commitDragSlots` reproduces the trailing `if (a4)`
// branch that rebuilds the drag-slot store table (routed through hooks).
DragLayoutResult TradePanelLayoutDragSlotsVariant(const DragLayoutInputs& in,
                                                  DragLayoutState& st,
                                                  bool commitDragSlots);

// gilde.exe 0x50c140 — VIBE_TradePanel_LayoutDragSlotsWide (row pitch 35).
// Identical layout math with a 35-pixel row pitch; `modeWord` is the original's
// `*a5` that selects the per-row slider build mode (475 -> 1, 476 -> 2, else 3).
DragLayoutResult TradePanelLayoutDragSlotsWide(const DragLayoutInputs& in,
                                               DragLayoutState& st,
                                               bool commitDragSlots, i32 modeWord);

// ---- Inert-default hooks for the GUI/entity-coupled leaves --------------------
// These mirror the original side-effecting calls. Defaults are no-ops so the pure
// layout math runs headless and is golden-vector testable; the real panel wiring
// installs backends that forward to the live object/widget/dragslot tables.

// VIBE_Object_AddToWindow(dword_62D230, 0) — allocate a column widget object.
// Returns the new object id stored into dword_122EE08[5k].
using DragAddObjectHook = i32 (*)();
void DragSetAddObjectHook(DragAddObjectHook hook);

// VIBE_Widget_InitFromState(objId, newCount + 1715) — re-skin a column widget when
// its item count changed.
using DragInitWidgetHook = void (*)(i32 objId, i32 templateIndex);
void DragSetInitWidgetHook(DragInitWidgetHook hook);

// VIBE_Widget_LayoutBounds(x, y, objId) — place a visible column widget.
using DragLayoutBoundsHook = void (*)(i32 x, i32 y, i32 objId);
void DragSetLayoutBoundsHook(DragLayoutBoundsHook hook);

// Column widget hidden/shown toggle (the `field 52 = 0/1` write keyed off
// visibility).
using DragSetVisibleHook = void (*)(i32 objId, bool visible);
void DragSetVisibleHook_(DragSetVisibleHook hook);

// VIBE_TradePanel_BuildSliderRow(a1, a2, a4, rowRec, mode, a6) — build one slider
// row. Called once per slider row with the resolved build mode.
using DragBuildSliderRowHook = void (*)(i32 rowIndex, i32 buildMode);
void DragSetBuildSliderRowHook(DragBuildSliderRowHook hook);

// The trailing `if (a4)` drag-slot store rebuild: VIBE_DragSlot_ResetTable then
// two passes that VIBE_DragSlot_StoreItem each non-empty cargo slot.
using DragCommitStoreHook = void (*)();
void DragSetCommitStoreHook(DragCommitStoreHook hook);

} // namespace guild::world
