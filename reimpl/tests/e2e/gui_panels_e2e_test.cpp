// End-to-end tests for the GUI panel cluster (guild::gui):
//   1. Build the city-statistics panel for a synthetic demand snapshot: capture the
//      readouts + legend "widget tree" through the command sink and verify the chart
//      data points the same snapshot would plot.
//   2. Drive the chat console: append lines, scroll, submit, and verify the visible
//      window and assembled wire line.
//   3. Build a save-browser slot grid from synthetic metadata and a book pagination
//      walk end-to-end.
#include "gui/book.h"
#include "gui/chatconsole.h"
#include "gui/savebrowser.h"
#include "gui/statchart.h"
#include "gui/statpanel.h"

#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {
// A capturing sink that records the "widget tree" the panel builder would emit.
struct CapturingSink : StatPanelCommandSink {
    bool sawReadouts = false;
    bool sawLegend = false;
    bool sawChart = false;
    CityStatReadouts readouts{};
    std::array<LegendRow, kLegendRows> legend{};
    i32 chartMask = 0;
    DemandSnapshot chartSnap{};

    void RenderChart(i32 mask, const DemandSnapshot& s) override {
        sawChart = true;
        chartMask = mask;
        chartSnap = s;
    }
    void EmitReadouts(const CityStatReadouts& r) override {
        sawReadouts = true;
        readouts = r;
    }
    void EmitLegend(const std::array<LegendRow, kLegendRows>& l) override {
        sawLegend = true;
        legend = l;
    }
};
} // namespace

TEST(GuiPanelsE2E, StatPanelBuildAndChart) {
    DemandSnapshot snap;
    snap.v[1] = 3.0f;   // demand -> 300
    snap.v[2] = 0.4f;   // supply
    snap.v[3] = 0.5f;   // price -> 50
    snap.v[9] = 1.0f;   // growth
    // Fill the chart series part deterministically.
    for (int i = 0; i < 15; ++i)
        snap.v[i] = 0.0f;
    snap.v[1] = 3.0f;
    snap.v[2] = 0.4f;
    snap.v[3] = 0.5f;
    snap.v[9] = 1.0f;

    CapturingSink sink;
    StatPanel_SetCommandSink(&sink);

    // Series selection: bar + series-C (mask 0x05).
    StatPanel_ShowCityStatistics(snap, kSeriesBar | kSeriesC);

    // The full "widget tree" was emitted.
    CHECK(sink.sawReadouts);
    CHECK(sink.sawLegend);
    CHECK(sink.sawChart);

    // Readouts match the recovered scaling.
    CHECK_EQ(sink.readouts.demandValue, 300);   // 3.0 * 100
    CHECK_EQ(sink.readouts.priceValue, 50);      // 0.5 * 100
    CHECK_EQ(sink.readouts.floorMark, 0);
    // growth = round((1.0+1.0)*2.5) = 5 -> clamps to 4 -> 6901
    CHECK_EQ(sink.readouts.growthPhraseId, 6901);
    // supply = round(0.4*5) = 2 -> 6905
    CHECK_EQ(sink.readouts.supplyPhraseId, 6905);

    // Legend is a five-row tree with monotonically increasing row Y.
    CHECK_EQ((int)sink.legend.size(), 5);
    for (int i = 1; i < 5; ++i)
        CHECK(sink.legend[i].checkboxY > sink.legend[i - 1].checkboxY);

    // The chart mask reached the renderer unchanged.
    CHECK_EQ(sink.chartMask, kSeriesBar | kSeriesC);

    // Now verify the data points the same series would plot.  Use a known float series
    // (e.g. the snapshot replicated as the 16-sample chart input) and a 425x350 chart.
    float series[16];
    for (int k = 0; k < 16; ++k)
        series[k] = 0.0625f * static_cast<float>(k);  // 0, .0625, … ramp
    auto pts = ComputeLineSeriesPoints(series, 425, 350);
    // Step 20, baseline 328, height 320.  Point 0 at floor, point 15 at .9375.
    CHECK_EQ(pts[0].x, 45);
    CHECK_EQ(pts[0].y, 328);                 // 0 -> baseline
    CHECK_EQ(pts[15].x, 45 + 15 * 20);       // 345
    CHECK_EQ(pts[15].y, static_cast<int>(328.0 - 320.0 * (0.0625 * 15)));  // 28

    // Widget-tree-style invariant: every plotted x is strictly increasing.
    for (int k = 1; k < 16; ++k)
        CHECK(pts[k].x > pts[k - 1].x);

    StatPanel_SetCommandSink(nullptr);  // restore default
}

TEST(GuiPanelsE2E, ChatConsoleFlow) {
    ChatConsole c(/*visibleRows=*/4, /*scrollback=*/64);
    // Restore channel toggles, then post a sequence of lines.
    c.SetChannel(0, 5);
    c.SetChannel(3, 9);
    CHECK_EQ(c.Channel(0), 5);
    CHECK_EQ(c.Channel(1), -1);

    for (int i = 0; i < 10; ++i)
        c.AppendLine("msg" + std::to_string(i));  // msg0..msg9

    // Newest four visible.
    auto v = c.VisibleLines();
    CHECK_EQ((int)v.size(), 4);
    CHECK(v[0] == "msg6");
    CHECK(v[3] == "msg9");

    // Scroll up two, window shifts.
    c.ScrollUp(2);
    v = c.VisibleLines();
    CHECK(v[0] == "msg4");
    CHECK(v[3] == "msg7");

    // Submit a new line: it appends, auto-scrolls to bottom, clears input.
    c.SetInput("gg");
    std::string wire = c.Submit(/*colourIndex=*/12);
    CHECK(wire == std::string("$12FF gg$A"));
    v = c.VisibleLines();
    CHECK(v[3] == wire);          // newest visible again
    CHECK_EQ(c.LineCount(), 11);
    CHECK(c.Input().empty());
}

TEST(GuiPanelsE2E, SaveBrowserAndBook) {
    // Enumerate -> build slots end to end.
    std::vector<std::string> files = {"slot0.SAV", "x.dat", "slot1.sav"};
    auto entries = SaveBrowser_EnumerateSaveFiles("usr/saves", files, ".SAV");
    CHECK_EQ((int)entries.size(), 2);

    std::vector<SaveMetadata> meta;
    for (std::size_t i = 0; i < entries.size(); ++i)
        meta.push_back({entries[i].fullPath, entries[i].displayName, false, true,
                        static_cast<int>(i)});
    auto slots = SaveBrowser_BuildSlots(meta);
    CHECK(slots[0].occupied && slots[0].label == "slot0.SAV");
    CHECK(slots[1].occupied && slots[1].label == "slot1.sav");
    CHECK(!slots[2].occupied);
    CHECK_EQ(slots[1].y, 130);

    // Book: open 8 pages and walk forward to the end, then back.
    Book b;
    CHECK(Book_Open(b, 8));
    int flips = 0;
    while (Book_TurnPageForward(b))
        ++flips;
    // 8 pages: 0->2->4->6 (6+2=8 not <8 stops). 3 flips, current 6.
    CHECK_EQ(flips, 3);
    CHECK_EQ((int)b.currentPage, 6);
    auto vis = Book_VisiblePages(b);
    CHECK_EQ(vis[0], 6);
    CHECK_EQ(vis[1], 7);
    int back = 0;
    while (Book_TurnPageBackward(b))
        ++back;
    CHECK_EQ(back, 3);
    CHECK_EQ((int)b.currentPage, 0);
}
