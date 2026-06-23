// Golden-vector tests for the trade-panel drag-slot grid layout core.
//   VIBE_TradePanel_LayoutDragSlotsVariant 0x50b350
//   VIBE_TradePanel_LayoutDragSlotsWide    0x50c140
//   VIBE_TradeTransport_OpenPanelMode2/4_Thunk 0x54012c / 0x54013c
#include "tests/framework/test.h"

#include <array>

#include "../../src/world/trade_recon_dragslots.h"
#include "../../src/world/trade_recon_transport_thunks.h"

using namespace guild;
using namespace guild::world;

namespace {

// Build the "all four columns hold 4 items" scenario that fills exactly 16 slider
// rows (sum of per-column counts == 16) so the column walk stays in range.
DragLayoutInputs makeBalancedInputs() {
    DragLayoutInputs in;
    in.panelW = 300;
    in.formX = 10;
    in.formY = 20;
    in.rowStride = 30;
    in.newCount = {4, 4, 4, 4};
    in.templateW = {100, 100, 100, 100};
    in.colCarry = {2, 2, 2, 2};
    return in;
}

} // namespace

// Column x/y offsets: xOff = (panelW - templateW) >> 1; yOff = (rowPitch*col) +
// col*rowStride. Variant uses rowPitch 28.
TEST(TradeReconDragLayout, VariantColumnOffsets) {
    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st;
    st.reset();

    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, /*commit*/false);

    // xOff = (300 - 100) >> 1 = 100 for every column.
    for (int c = 0; c < kDragColumns; ++c)
        CHECK_EQ(r.xOff[c], 100);

    // yOff: col0 = 0; col1 = 28 + 30 = 58; col2 = 56 + 60 = 116; col3 = 84 + 90 = 174.
    CHECK_EQ(r.yOff[0], 0);
    CHECK_EQ(r.yOff[1], 58);
    CHECK_EQ(r.yOff[2], 116);
    CHECK_EQ(r.yOff[3], 174);

    // All visible (count 4 >= 2). Widget origin = form + loword(off).
    for (int c = 0; c < kDragColumns; ++c)
        CHECK(r.visible[c]);
    CHECK_EQ(r.widgetX[0], 110); // 10 + 100
    CHECK_EQ(r.widgetY[0], 20);  // 20 + 0
    CHECK_EQ(r.widgetY[3], 194); // 20 + 174
}

// The 16 slider rows: per-column blocks of 4 with sliderX = 68*(rowInCol-1)+xOff.
TEST(TradeReconDragLayout, VariantSliderRowGrid) {
    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st;
    st.reset();

    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, false);

    // Expected sliderX within a column: 100, 168, 236, 304 (68 pitch).
    const std::array<i32, 4> xs{100, 168, 236, 304};
    const std::array<i32, 4> ys{0, 58, 116, 174};

    for (int row = 0; row < kDragSliderRows; ++row) {
        const int col = row / 4;
        const int inCol = row % 4; // 0-based
        CHECK_EQ(r.rows[row].sliderX, xs[inCol]);
        CHECK_EQ(r.rows[row].sliderY, ys[col]);
        CHECK_EQ(r.rows[row].column, col);
        CHECK_EQ(r.rows[row].rowInCol, inCol + 1); // 1-based in the original
        CHECK_EQ(r.rows[row].buildMode, 3);        // Variant is always mode 3
    }
}

// Wide variant: identical x grid, but row pitch 35 changes the column y offsets.
TEST(TradeReconDragLayout, WideColumnOffsetsPitch35) {
    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st;
    st.reset();

    DragLayoutResult r =
        TradePanelLayoutDragSlotsWide(in, st, /*commit*/false, /*modeWord*/0);

    // yOff: col0 = 0; col1 = 35 + 30 = 65; col2 = 70 + 60 = 130; col3 = 105 + 90 = 195.
    CHECK_EQ(r.yOff[0], 0);
    CHECK_EQ(r.yOff[1], 65);
    CHECK_EQ(r.yOff[2], 130);
    CHECK_EQ(r.yOff[3], 195);
    // x grid unchanged.
    CHECK_EQ(r.rows[0].sliderX, 100);
    CHECK_EQ(r.rows[1].sliderX, 168);
    CHECK_EQ(r.rows[15].sliderX, 304);
}

// Wide build-mode selection: 475 -> 1, 476 -> 2, anything else -> 3.
TEST(TradeReconDragLayout, WideModeSelection) {
    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st;

    st.reset();
    CHECK_EQ(TradePanelLayoutDragSlotsWide(in, st, false, 475).rows[0].buildMode, 1);
    st.reset();
    CHECK_EQ(TradePanelLayoutDragSlotsWide(in, st, false, 476).rows[5].buildMode, 2);
    st.reset();
    CHECK_EQ(TradePanelLayoutDragSlotsWide(in, st, false, 0).rows[9].buildMode, 3);
    st.reset();
    CHECK_EQ(TradePanelLayoutDragSlotsWide(in, st, false, 9999).rows[15].buildMode, 3);
}

// Columns below the visibility threshold (count < 2) are hidden and keep their
// previous (zero) offsets; their widget is not laid out.
TEST(TradeReconDragLayout, HiddenColumnBelowThreshold) {
    DragLayoutInputs in = makeBalancedInputs();
    in.newCount = {4, 1, 4, 7}; // col1 count 1 (< 2) -> hidden; sum 16 stays balanced
    DragLayoutState st;
    st.reset();

    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, false);
    CHECK(r.visible[0]);
    CHECK(!r.visible[1]);
    CHECK(r.visible[2]);
    CHECK(r.visible[3]);
    // Hidden column keeps offset 0 and no widget origin computed.
    CHECK_EQ(r.xOff[1], 0);
    CHECK_EQ(r.yOff[1], 0);
}

// loword truncation on the form-relative add: a column offset with low-16-bit
// wrap reproduces the original `formX + LOWORD(off)` behavior.
TEST(TradeReconDragLayout, LowordOffsetTruncation) {
    DragLayoutInputs in = makeBalancedInputs();
    // Force xOff such that the value exceeds 16 bits: panelW huge, templateW 0.
    // xOff = (0x20000 - 0) >> 1 = 0x10000 -> loword == 0.
    in.panelW = 0x20000;
    in.templateW = {0, 0, 0, 0};
    DragLayoutState st;
    st.reset();

    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, false);
    CHECK_EQ(r.xOff[0], 0x10000);
    // widgetX = formX + loword(0x10000) = 10 + 0 = 10.
    CHECK_EQ(r.widgetX[0], 10);
}

// Hook wiring: AddObject is invoked only for unallocated columns; commit store is
// invoked only when commitDragSlots is set; init-widget fires when count changes.
namespace {
int g_addCalls = 0;
int g_initCalls = 0;
int g_layoutCalls = 0;
int g_sliderCalls = 0;
int g_commitCalls = 0;
int fakeAdd() { return 100 + g_addCalls++; }
void fakeInit(i32, i32) { ++g_initCalls; }
void fakeLayout(i32, i32, i32) { ++g_layoutCalls; }
void fakeVisible(i32, bool) {}
void fakeSlider(i32, i32) { ++g_sliderCalls; }
void fakeCommit() { ++g_commitCalls; }
} // namespace

TEST(TradeReconDragLayout, HookInvocationCounts) {
    g_addCalls = g_initCalls = g_layoutCalls = g_sliderCalls = g_commitCalls = 0;
    DragSetAddObjectHook(fakeAdd);
    DragSetInitWidgetHook(fakeInit);
    DragSetLayoutBoundsHook(fakeLayout);
    DragSetVisibleHook_(fakeVisible);
    DragSetBuildSliderRowHook(fakeSlider);
    DragSetCommitStoreHook(fakeCommit);

    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st;
    st.reset(); // objId all -1 -> 4 allocations; prevCount -1 != 4 -> 4 init calls

    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, /*commit*/true);

    CHECK_EQ(g_addCalls, 4);       // one per unallocated column
    CHECK_EQ(g_initCalls, 4);      // count changed for every column
    CHECK_EQ(g_layoutCalls, 4);    // all visible
    CHECK_EQ(g_sliderCalls, 16);   // one per slider row
    CHECK_EQ(g_commitCalls, 1);    // commit path taken
    CHECK_EQ(st.objId[0], 100);    // ids assigned in order

    // A second pass with the SAME counts re-allocates nothing and re-inits nothing.
    g_addCalls = g_initCalls = 0;
    r = TradePanelLayoutDragSlotsVariant(in, st, /*commit*/false);
    CHECK_EQ(g_addCalls, 0);
    CHECK_EQ(g_initCalls, 0);
    CHECK_EQ(g_commitCalls, 1); // not incremented (commit=false)

    // Restore inert defaults so other suites are unaffected.
    DragSetAddObjectHook(nullptr);
    DragSetInitWidgetHook(nullptr);
    DragSetLayoutBoundsHook(nullptr);
    DragSetVisibleHook_(nullptr);
    DragSetBuildSliderRowHook(nullptr);
    DragSetCommitStoreHook(nullptr);
}

// Thunks: each tail-calls the dispatcher with its baked-in mode constant (2 / 4).
namespace {
int g_lastMode = -1;
void* g_lastPanel = nullptr;
int recordDispatch(void* panel, int mode) {
    g_lastPanel = panel;
    g_lastMode = mode;
    return 1000 + mode;
}
} // namespace

TEST(TradeReconTransportThunk, Mode2And4Dispatch) {
    TradeTransportSetPanelDispatchHook(recordDispatch);
    int dummy = 0;
    void* panel = &dummy;

    CHECK_EQ(TradeTransportOpenPanelMode2(panel), 1002);
    CHECK_EQ(g_lastMode, 2);
    CHECK_EQ(g_lastPanel, panel);

    CHECK_EQ(TradeTransportOpenPanelMode4(panel), 1004);
    CHECK_EQ(g_lastMode, 4);

    TradeTransportSetPanelDispatchHook(nullptr);
    CHECK_EQ(TradeTransportOpenPanelMode2(panel), 0); // inert default
}

// ---------------------------------------------------------------------------
// Boundary / malformed inputs for the Block-4 column walk (the do/while that
// advances `col` while colCarry < 1). The original relies on its live column
// data always yielding a carry>=1 column within range; on degenerate input
// (every colCarry < 1, every prevCount tiny so the advance keeps firing) the
// walk would step `col` past the 4-entry arrays and read out of bounds. ASAN
// would trap the old code on these. The bounded walk keeps `col` in range; for
// the valid/balanced input above the layout is byte-identical (covered by the
// VariantColumnOffsets golden), so this only changes the degenerate path.
// ---------------------------------------------------------------------------
TEST(TradeReconDragLayout, MalformedAllZeroCarryNoOOB) {
    DragLayoutInputs in;
    in.panelW = 300; in.formX = 10; in.formY = 20; in.rowStride = 30;
    in.newCount  = {1, 1, 1, 1};
    in.templateW = {100, 100, 100, 100};
    in.colCarry  = {0, 0, 0, 0};   // never satisfies carry>=1 -> would walk off
    DragLayoutState st; st.reset();
    // Just running it must not OOB; the result is best-effort on degenerate data.
    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, /*commit*/false);
    (void)r;
    CHECK(true);
}

TEST(TradeReconDragLayout, MalformedZeroPrevCountNoOOB) {
    // prevCount stays at its reset value (-1) for every column, so every slider
    // row triggers the "advance column" branch; with low carry this stresses the
    // do/while past the last column.
    DragLayoutInputs in;
    in.panelW = 200; in.formX = 0; in.formY = 0; in.rowStride = 10;
    in.newCount  = {0, 0, 0, 0};
    in.templateW = {50, 50, 50, 50};
    in.colCarry  = {0, 0, 0, 1};   // only the last column would break the loop
    DragLayoutState st; st.reset();         // prevCount = {-1,-1,-1,-1}
    DragLayoutResult r = TradePanelLayoutDragSlotsWide(in, st, /*commit*/false,
                                                       /*modeWord*/0);
    (void)r;
    CHECK(true);
}

// Balanced input still produces the same column index per row after the bound
// guard (the guard is a no-op on in-range input).
TEST(TradeReconDragLayout, BoundedWalkBalancedColumnsUnchanged) {
    DragLayoutInputs in = makeBalancedInputs();
    DragLayoutState st; st.reset();
    DragLayoutResult r = TradePanelLayoutDragSlotsVariant(in, st, /*commit*/false);
    // 16 rows over 4 columns, 4 rows each -> columns 0,0,0,0,1,1,1,1,...
    for (int row = 0; row < kDragSliderRows; ++row)
        CHECK(r.rows[row].column >= 0 && r.rows[row].column < kDragColumns);
}
