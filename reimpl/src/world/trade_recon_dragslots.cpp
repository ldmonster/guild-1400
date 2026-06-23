// Trade-panel drag-slot grid layout — faithful coordinate core (gilde.exe).
//   VIBE_TradePanel_LayoutDragSlotsVariant 0x50b350
//   VIBE_TradePanel_LayoutDragSlotsWide    0x50c140
// See trade_recon_dragslots.h for the array-layout / hook documentation.
#include "trade_recon_dragslots.h"

namespace guild::world {

namespace {

// ---- Hook storage (inert defaults) -------------------------------------------
i32  defAddObject() { return -1; }
void defInitWidget(i32, i32) {}
void defLayoutBounds(i32, i32, i32) {}
void defSetVisible(i32, bool) {}
void defBuildSliderRow(i32, i32) {}
void defCommitStore() {}

DragAddObjectHook      g_addObject     = defAddObject;
DragInitWidgetHook     g_initWidget    = defInitWidget;
DragLayoutBoundsHook   g_layoutBounds  = defLayoutBounds;
DragSetVisibleHook     g_setVisible    = defSetVisible;
DragBuildSliderRowHook g_buildSlider   = defBuildSliderRow;
DragCommitStoreHook    g_commitStore   = defCommitStore;

// LOWORD(x): the original adds the low 16 bits of the signed offset to the form
// origin. Reproduce the exact 16-bit truncation (sign-extended back to int as the
// `+ LOWORD(...)` to an int operand would be after the implicit widening).
inline i32 loword(i32 v) {
    return static_cast<i16>(static_cast<u16>(static_cast<u32>(v) & 0xFFFFu));
}

// The shared layout body. `rowPitch` is the per-row y accumulator step (28 / 35).
// `pickMode(rowIndex)` selects the slider build mode for each of the 16 rows.
template <typename ModePicker>
DragLayoutResult layoutCommon(const DragLayoutInputs& in, DragLayoutState& st,
                              bool commitDragSlots, int rowPitch,
                              ModePicker pickMode) {
    DragLayoutResult res;

    const i32 panelW = in.panelW;

    // --- Block 1: allocate any column widget that has no object yet -----------
    // for (v4 = 0; v4 != 20; v4 += 5)  // dword index, 4 columns
    for (int col = 0; col < kDragColumns; ++col) {
        if (st.objId[col] == -1)
            st.objId[col] = g_addObject(); // VIBE_Object_AddToWindow(dword_62D230, 0)
    }

    // --- Block 2: re-skin changed columns + compute centered x / stacked y ----
    // v5 = row index (0..3); v7 = y accumulator (starts 0, += rowPitch each row).
    int yAccum = 0;
    for (int col = 0; col < kDragColumns; ++col) {
        const i32 newCount = in.newCount[col]; // dword_122EE04[5*col]
        if (st.prevCount[col] != newCount)
            g_initWidget(st.objId[col], newCount + kWidgetTemplateBase);
        st.prevCount[col] = newCount; // dword_122EE00[5*col] = newCount

        if (newCount >= kColumnVisibleThreshold) {
            // xOff = (panelW - templateW) >> 1
            st.xOff[col] = (panelW - in.templateW[col]) >> 1;
            // yOff = v7 + col_rowIndex * rowStride
            st.yOff[col] = yAccum + col * in.rowStride;
        }
        yAccum += rowPitch;
    }

    // --- Block 3: hide invisible columns, lay out visible ones ----------------
    for (int col = 0; col < kDragColumns; ++col) {
        const bool visible = st.prevCount[col] >= kColumnVisibleThreshold;
        res.visible[col] = visible;
        res.xOff[col] = st.xOff[col];
        res.yOff[col] = st.yOff[col];
        if (!visible) {
            g_setVisible(st.objId[col], false); // field 52 = 1
        } else {
            g_setVisible(st.objId[col], true); // field 52 = 0
            const i32 wx = in.formX + loword(st.xOff[col]);
            const i32 wy = in.formY + loword(st.yOff[col]);
            res.widgetX[col] = wx;
            res.widgetY[col] = wy;
            g_layoutBounds(wx, wy, st.objId[col]);
        }
    }

    // --- Block 4: walk the 16 slider rows across the columns ------------------
    // v13 = rowInCol (1-based), v14 = current column, v15 = row index 0..15.
    int rowInCol = 0; // v13
    int col = 0;      // v14
    for (int r = 0; r < kDragSliderRows; ++r) {
        ++rowInCol;
        // if (v13 > dword_122EE00[5*v14])  -> advance to next non-skipped column
        if (col < kDragColumns && rowInCol > st.prevCount[col]) {
            rowInCol = 1;
            i32 carry;
            do {
                carry = in.colCarry[col]; // dword_122EE14[5*col]
                ++col;                    // v16 += 5; ++v14
                // The original relies on its column data always yielding a
                // carry>=1 column within range; on malformed input (every
                // colCarry < 1) this do/while would read past the 4-entry
                // arrays. Bound the walk so degenerate input cannot OOB; for
                // valid input a non-skipped column is always found first, so the
                // observable layout is unchanged.
            } while (carry < 1 && col < kDragColumns);
        }
        // Clamp the working column for the subsequent offset reads; valid input
        // never reaches here out of range.
        const int colIdx = col < kDragColumns ? col : kDragColumns - 1;
        // sliderX = 68*(v13-1) + dword_122EDF8[5*col]; sliderY = dword_122EDFC[5*col]
        const i32 sliderX = kSliderColPitch * (rowInCol - 1) + st.xOff[colIdx];
        const i32 sliderY = st.yOff[colIdx];
        const i32 mode = pickMode(r);

        DragSliderPlacement& p = res.rows[r];
        p.sliderX  = sliderX;
        p.sliderY  = sliderY;
        p.column   = col;
        p.rowInCol = rowInCol;
        p.buildMode = mode;

        g_buildSlider(r, mode); // VIBE_TradePanel_BuildSliderRow(..)
    }

    // --- Block 5: trailing drag-slot store rebuild (if a4) --------------------
    if (commitDragSlots)
        g_commitStore();

    return res;
}

} // namespace

// gilde.exe 0x50b350 — VIBE_TradePanel_LayoutDragSlotsVariant.
DragLayoutResult TradePanelLayoutDragSlotsVariant(const DragLayoutInputs& in,
                                                  DragLayoutState& st,
                                                  bool commitDragSlots) {
    return layoutCommon(in, st, commitDragSlots, kRowPitchVariant,
                        [](int) { return 3; }); // always mode 3
}

// gilde.exe 0x50c140 — VIBE_TradePanel_LayoutDragSlotsWide.
DragLayoutResult TradePanelLayoutDragSlotsWide(const DragLayoutInputs& in,
                                               DragLayoutState& st,
                                               bool commitDragSlots, i32 modeWord) {
    const int mode = (modeWord == kWideModeWordBuy)    ? 1
                     : (modeWord == kWideModeWordSell) ? 2
                                                       : 3;
    return layoutCommon(in, st, commitDragSlots, kRowPitchWide,
                        [mode](int) { return mode; });
}

// ---- Hook setters ------------------------------------------------------------
void DragSetAddObjectHook(DragAddObjectHook hook) { g_addObject = hook ? hook : defAddObject; }
void DragSetInitWidgetHook(DragInitWidgetHook hook) { g_initWidget = hook ? hook : defInitWidget; }
void DragSetLayoutBoundsHook(DragLayoutBoundsHook hook) { g_layoutBounds = hook ? hook : defLayoutBounds; }
void DragSetVisibleHook_(DragSetVisibleHook hook) { g_setVisible = hook ? hook : defSetVisible; }
void DragSetBuildSliderRowHook(DragBuildSliderRowHook hook) { g_buildSlider = hook ? hook : defBuildSliderRow; }
void DragSetCommitStoreHook(DragCommitStoreHook hook) { g_commitStore = hook ? hook : defCommitStore; }

} // namespace guild::world
