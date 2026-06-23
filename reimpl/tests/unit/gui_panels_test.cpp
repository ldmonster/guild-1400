// Unit tests for the GUI info/stat/utility panel modules (guild::gui).
// Golden vectors computed in python from the recovered logic of:
//   VIBE_StatPanel_DrawGraph* / DrawBarChart / RenderChart  @0x55e408..0x55ee70
//   VIBE_StatPanel_ShowCityStatistics                       @0x55f4ac
//   VIBE_Book_Open/TurnPage/RefreshVisiblePages             @0x4be204..0x4be60c
//   VIBE_SaveBrowser_EnumerateSaveFiles/LoadSlotMetadata    @0x569530/0x569d00
//   VIBE_ChatConsole_BuildWindow / DebugList_AppendId       @0x4bfc48/0x53629c
//   VIBE_DebugWindow_ShowMemoryInfo / DamageLabel_Register  @0x536658/0x4bad5c
#include "gui/book.h"
#include "gui/chatconsole.h"
#include "gui/debugwindow.h"
#include "gui/savebrowser.h"
#include "gui/statchart.h"
#include "gui/statpanel.h"

#include "tests/framework/test.h"

#include <string>

using namespace guild::gui;

// ---------------------------------------------------------------------------
// StatChart — data-point computation (line + bar series), golden coordinates.
// ---------------------------------------------------------------------------
TEST(GuiPanelChart, ColumnStep) {
    CHECK_EQ(ChartColumnStep(425), 20);   // (425-105)/16
    CHECK_EQ(ChartColumnStep(105), 0);
    CHECK_EQ(ChartColumnStep(265), 10);   // (265-105)/16
}

TEST(GuiPanelChart, ConvertXTruncatesTowardZero) {
    // VIBE_Coord_ConvertX @0x5c6b08 sets RC=0b11 (toward zero) + frndint -> truncate.
    // This pins the regression away from round-to-nearest(-even).
    CHECK_EQ(static_cast<int>(ConvertX(3.5)), 3);
    CHECK_EQ(static_cast<int>(ConvertX(3.9)), 3);
    CHECK_EQ(static_cast<int>(ConvertX(2.5)), 2);  // nearbyint would give 2 (even) here
    CHECK_EQ(static_cast<int>(ConvertX(0.5)), 0);  // nearbyint -> 0, but trunc proves it
    CHECK_EQ(static_cast<int>(ConvertX(1.5)), 1);  // nearbyint -> 2, trunc -> 1 (distinct)
    CHECK_EQ(static_cast<int>(ConvertX(-1.5)), -1);
    CHECK_EQ(static_cast<int>(ConvertX(-3.9)), -3);
    CHECK_EQ(static_cast<int>(ConvertX(0.0)), 0);
}

TEST(GuiPanelChart, LineSeriesPoints) {
    float series[16] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 0.9f, 0.8f,
                        0.7f, 0.6f, 0.5f,  0.4f, 0.3f,  0.2f, 0.1f, 0.05f};
    auto pts = ComputeLineSeriesPoints(series, 425, 350);
    const int gx[16] = {45, 65, 85, 105, 125, 145, 165, 185,
                        205, 225, 245, 265, 285, 305, 325, 345};
    // ConvertX @0x5c6b08 TRUNCATES toward zero (verified wave-17); the series*height
    // product is widened to double to match the x87 extended intermediate.
    const int gy[16] = {328, 295, 248, 168, 88, 8, 40, 71,
                        104, 135, 168, 199, 231, 263, 295, 311};
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(pts[k].x, gx[k]);
        CHECK_EQ(pts[k].y, gy[k]);
    }
}

TEST(GuiPanelChart, LineSeriesFloorClamp) {
    // Negative samples are clamped to the floor (flt_641DA8 == 0) -> y == baseline.
    float series[16] = {-1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    auto pts = ComputeLineSeriesPoints(series, 425, 350);
    CHECK_EQ(pts[0].y, 350 - 22);  // baseline (no negative dip)
    CHECK_EQ(pts[1].y, 350 - 22);
}

TEST(GuiPanelChart, BarAxisLadder) {
    CHECK_EQ(BarAxisMaximum(0), 500);
    CHECK_EQ(BarAxisMaximum(500), 1000);
    CHECK_EQ(BarAxisMaximum(1200), 2000);
    CHECK_EQ(BarAxisMaximum(20000), 20000);  // == 20000: not < any of idx0..7 -> peak
    CHECK_EQ(BarAxisMaximum(15000), 20000);  // 15000 < 20000 (idx7) -> 20000
    // DrawBarChart @0x55e408 scans only the first 8 ladder entries (cmp esi,20h);
    // the 9th (50000) is dead, so peaks >= 20000 yield the peak itself, not 50000.
    CHECK_EQ(BarAxisMaximum(60000), 60000);  // >= 20000 -> peak passes through
}

TEST(GuiPanelChart, BarSeriesPoints) {
    i32 bars[16] = {100, 250, 500, 800, 1200, 900, 700, 400,
                    300, 200, 150, 100, 80,   60,  40,  20};
    i32 axisMax = 0;
    auto pts = ComputeBarSeriesPoints(bars, 425, 350, &axisMax);
    CHECK_EQ(axisMax, 2000);  // peak 1200 -> 2000
    const int gy[16] = {312, 288, 248, 200, 136, 184, 216, 264,
                        280, 296, 304, 312, 315, 318, 321, 324};
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(pts[k].x, 45 + k * 20);
        CHECK_EQ(pts[k].y, gy[k]);
    }
}

TEST(GuiPanelChart, GridlineLayout) {
    // ConvertX TRUNCATES (verified wave-17): RenderChart @0x55eff7 / 0x55f054.
    auto g = ComputeGridlineX(425);
    const int gx[4] = {8, 106, 205, 304};
    for (int k = 0; k < 4; ++k)
        CHECK_EQ(g[k], gx[k]);
    auto l = ComputeGridLabelX(425);
    const int lx[4] = {5, 103, 202, 301};
    for (int k = 0; k < 4; ++k)
        CHECK_EQ(l[k], lx[k]);
}

// ---------------------------------------------------------------------------
// StatPanel — graph-series plotters (wave-20): DrawGraphSeriesA/B/C @0x55e778/
// 0x55e9b4/0x55ebf0 and DrawGraphLine @0x55e600.  Golden coords computed from the
// recovered arithmetic (ConvertX truncates; series*height widened to double).
// ---------------------------------------------------------------------------
TEST(GuiPanelStat, GraphSeriesPoints) {
    // A/B/C share this body; same data/window as the line-series golden -> same y.
    float series[16] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 0.9f, 0.8f,
                        0.7f, 0.6f, 0.5f,  0.4f, 0.3f,  0.2f, 0.1f, 0.05f};
    auto pts = ComputeGraphSeriesPoints(series, 425, 350);
    const int gx[16] = {45, 65, 85, 105, 125, 145, 165, 185,
                        205, 225, 245, 265, 285, 305, 325, 345};
    const int gy[16] = {328, 295, 248, 168, 88, 8, 40, 71,
                        104, 135, 168, 199, 231, 263, 295, 311};
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(pts[k].x, gx[k]);
        CHECK_EQ(pts[k].y, gy[k]);
    }
}

TEST(GuiPanelStat, GraphSeriesFloorClamp) {
    // Samples < 0 clamp to flt_641DA8 (== 0) -> y == baseline (windowH-22).
    float series[16] = {-1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    auto pts = ComputeGraphSeriesPoints(series, 425, 350);
    CHECK_EQ(pts[0].y, 350 - 22);
    CHECK_EQ(pts[1].y, 350 - 22);
}

TEST(GuiPanelStat, GraphSeriesX87Product) {
    // The series*height product MUST be formed in double (x87-80bit), not float32.
    // 0.1f as a double is 0.10000000149..; height=320 -> double product 32.00000047..,
    // so base(328) - product = 295.99999.. -> trunc 295.  Computing (float)(320*0.1f)
    // first gives exactly 32.0 -> 296.0 -> trunc 296.  Pin the double-widened (295).
    float series[16] = {0.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pts = ComputeGraphSeriesPoints(series, 425, 350);
    CHECK_EQ(pts[0].y, 295);
}

TEST(GuiPanelStat, GraphLinePoints) {
    // DrawGraphLine overlays two series (FAC + F98); NO per-sample clamp, baseline
    // is a float == windowH-22.  Negative FAC dips ABOVE baseline (y > base).
    float fac[16] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.5f, 0.3f,
                     0.1f, 0.0f, -0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float f98[16] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f,
                     0.2f, 0.1f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    auto lp = ComputeGraphLinePoints(fac, f98, 425, 350);
    const int facY[16] = {328, 263, 199, 135, 71, 8, 168, 231,
                          295, 328, 360, 328, 328, 328, 328, 328};
    const int f98Y[16] = {8, 40, 71, 104, 135, 168, 199, 231,
                          263, 295, 311, 328, 328, 328, 328, 328};
    const int gx[16] = {45, 65, 85, 105, 125, 145, 165, 185,
                        205, 225, 245, 265, 285, 305, 325, 345};
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(lp.seriesFAC[k].x, gx[k]);
        CHECK_EQ(lp.seriesF98[k].x, gx[k]);
        CHECK_EQ(lp.seriesFAC[k].y, facY[k]);
        CHECK_EQ(lp.seriesF98[k].y, f98Y[k]);
    }
    // No clamp: the -0.1 FAC sample at k=10 plots BELOW baseline (y=360 > 328).
    CHECK_EQ(lp.seriesFAC[10].y, 360);
}

// ---------------------------------------------------------------------------
// StatPanel — city-statistics content build (readouts + legend + mask).
// ---------------------------------------------------------------------------
TEST(GuiPanelStat, CityReadouts) {
    DemandSnapshot snap;
    snap.v[1] = 2.5f;  // demand
    snap.v[2] = 0.6f;  // supply
    snap.v[3] = 1.2f;  // price
    snap.v[9] = 0.4f;  // growth
    auto r = ComputeCityStatReadouts(snap);
    // ConvertX TRUNCATES toward zero (verified wave-17, ShowCityStatistics @0x55f4ac).
    CHECK_EQ(r.floorMark, 0);
    CHECK_EQ(r.growthPhraseId, 6900);  // trunc((1.4)*2.5)=trunc(3.5)=3 ; +6897
    CHECK_EQ(r.supplyPhraseId, 6906);  // trunc(0.6*5)=3 ; +6903
    CHECK_EQ(r.demandValue, 250);      // trunc(2.5*100)=250
    CHECK_EQ(r.priceValue, 120);       // trunc(1.2*100)=120
}

TEST(GuiPanelStat, CityReadoutsClampAndFloor) {
    DemandSnapshot snap;
    snap.v[9] = 10.0f;  // growth huge -> phrase clamps to 4
    snap.v[2] = 5.0f;   // supply 25 -> clamps to 4
    snap.v[1] = -1.0f;  // demand floored to 0
    snap.v[3] = -2.0f;  // price floored to 0
    auto r = ComputeCityStatReadouts(snap);
    CHECK_EQ(r.growthPhraseId, 6897 + 4);
    CHECK_EQ(r.supplyPhraseId, 6903 + 4);
    CHECK_EQ(r.demandValue, 0);
    CHECK_EQ(r.priceValue, 0);
}

TEST(GuiPanelStat, LegendLayout) {
    auto rows = ComputeLegendLayout();
    CHECK_EQ((int)rows.size(), 5);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(rows[i].checkboxX, 5);
        CHECK_EQ(rows[i].checkboxY, 30 * i + 5);
        CHECK_EQ(rows[i].labelX, 25);
        CHECK_EQ(rows[i].labelY, 30 * i + 4);
        CHECK_EQ(rows[i].swatchYBegin, 30 * i + 9);
        CHECK_EQ(rows[i].swatchYEnd, 30 * i + 19);
        CHECK_EQ(rows[i].messageId, 6912 + i);
        CHECK_EQ(rows[i].seriesBit, 1 << i);
    }
    // Swatch colours (dword_5526F4).
    CHECK_EQ((int)rows[0].color[0], 0x33);
    CHECK_EQ((int)rows[1].color[1], 0xEE);
    CHECK_EQ((int)rows[4].color[2], 0xEE);
}

TEST(GuiPanelStat, LegendMaskRoundtrip) {
    std::array<bool, 5> checked = {true, false, true, false, true};
    CHECK_EQ(LegendStateToMask(checked), 21);  // 1|4|16
    auto back = MaskToLegendState(21);
    for (int i = 0; i < 5; ++i)
        CHECK(back[i] == checked[i]);
}

// ---------------------------------------------------------------------------
// Book — pagination (spread visibility + flip clamping).
// ---------------------------------------------------------------------------
TEST(GuiPanelBook, OpenAndVisible) {
    Book b;
    CHECK(Book_Open(b, 6));
    CHECK_EQ((int)b.pageCount, 6);
    CHECK_EQ((int)b.currentPage, 0);
    CHECK_EQ((int)b.pageForms.size(), 6);
    auto vis = Book_VisiblePages(b);
    CHECK_EQ((int)vis.size(), 2);
    CHECK_EQ(vis[0], 0);
    CHECK_EQ(vis[1], 1);
    CHECK(Book_IsPageVisible(b, 1));
    CHECK(!Book_IsPageVisible(b, 2));
}

TEST(GuiPanelBook, OpenEmptyRejected) {
    Book b;
    CHECK(!Book_Open(b, 0));
}

TEST(GuiPanelBook, TurnForwardBackwardClamp) {
    Book b;
    Book_Open(b, 6);  // pages 0..5
    // forward: current+2 < 6 -> ok at 0 (->2), ok at 2 (->4)? 2+2=4<6 ok.
    CHECK(Book_TurnPageForward(b));
    CHECK_EQ((int)b.currentPage, 2);
    CHECK(Book_TurnPageForward(b));
    CHECK_EQ((int)b.currentPage, 4);
    // 4+2=6 >= 6 -> clamped.
    CHECK(!Book_TurnPageForward(b));
    CHECK_EQ((int)b.currentPage, 4);
    // backward from 4: 4-2=2>=0 ok.
    CHECK(Book_TurnPageBackward(b));
    CHECK_EQ((int)b.currentPage, 2);
    CHECK(Book_TurnPageBackward(b));
    CHECK_EQ((int)b.currentPage, 0);
    // 0-2 < 0 -> clamped.
    CHECK(!Book_TurnPageBackward(b));
    CHECK_EQ((int)b.currentPage, 0);
}

TEST(GuiPanelBook, VisibleAtEnd) {
    Book b;
    Book_Open(b, 5);  // pages 0..4
    b.currentPage = 4;
    auto vis = Book_VisiblePages(b);
    CHECK_EQ((int)vis.size(), 1);  // page 5 doesn't exist
    CHECK_EQ(vis[0], 4);
}

// ---------------------------------------------------------------------------
// SaveBrowser — enumerate + slot list build.
// ---------------------------------------------------------------------------
TEST(GuiPanelSave, EnumerateMatchExt) {
    std::vector<std::string> files = {"GAME1.SAV", "notes.txt", "Game2.sav",
                                      "auto.SAV", "readme"};
    auto e = SaveBrowser_EnumerateSaveFiles("saves", files, ".SAV");
    CHECK_EQ((int)e.size(), 3);  // GAME1.SAV, Game2.sav (no-case), auto.SAV
    CHECK(e[0].displayName == "GAME1.SAV");
    CHECK(e[0].fullPath == "saves/GAME1");      // ext stripped from path
    CHECK(e[1].fullPath == "saves/Game2");
    CHECK(e[2].fullPath == "saves/auto");
}

TEST(GuiPanelSave, BuildSlots) {
    std::vector<SaveMetadata> saves;
    saves.push_back({"saves/A", "Alpha", false, true, 0});
    saves.push_back({"saves/B", "Beta", false, true, 2});   // explicit slot 2
    saves.push_back({"saves/C", "Bad", true, true, 1});     // excluded -> dropped
    saves.push_back({"saves/D", "Delta", false, false, 3}); // load failed -> dropped
    saves.push_back({"saves/E", "Echo", false, true, -1});  // next free
    auto slots = SaveBrowser_BuildSlots(saves);
    CHECK_EQ((int)slots.size(), 16);
    // Slot 0 = Alpha, slot 2 = Beta, Echo lands in slot 1 (next free).
    CHECK(slots[0].occupied && slots[0].label == "Alpha");
    CHECK_EQ(slots[0].y, 0);
    CHECK(slots[2].occupied && slots[2].label == "Beta");
    CHECK_EQ(slots[2].y, 260);  // 130*2
    CHECK(slots[1].occupied && slots[1].label == "Echo");
    // Excluded/failed never appear.
    int occupied = 0;
    for (auto& s : slots)
        if (s.occupied)
            ++occupied;
    CHECK_EQ(occupied, 3);
    // Remaining slots are empty placeholders with correct Y.
    CHECK(!slots[3].occupied);
    CHECK_EQ(slots[15].y, 130 * 15);
}

// ---------------------------------------------------------------------------
// ChatConsole — line buffer append/scroll + input/submit + channel state.
// ---------------------------------------------------------------------------
TEST(GuiPanelChat, AssembleLine) {
    // colour index already biased (palette - 1342).
    CHECK(ChatConsole_AssembleLine(5, "hi") == std::string("$5FF hi$A"));
    CHECK(ChatConsole_AssembleLine(0, "") == std::string("$0FF $A"));
}

TEST(GuiPanelChat, ChannelTemplate) {
    ChatConsole c;
    for (int i = 0; i < kChatChannels; ++i)
        CHECK_EQ(c.Channel(i), -1);  // template all -1
    c.SetChannel(2, 77);
    CHECK_EQ(c.Channel(2), 77);
}

TEST(GuiPanelChat, AppendAndVisibleWindow) {
    ChatConsole c(/*visibleRows=*/3, /*scrollback=*/100);
    for (int i = 0; i < 5; ++i)
        c.AppendLine("L" + std::to_string(i));  // L0..L4
    CHECK_EQ(c.LineCount(), 5);
    auto v = c.VisibleLines();  // last 3 -> L2,L3,L4
    CHECK_EQ((int)v.size(), 3);
    CHECK(v[0] == "L2");
    CHECK(v[1] == "L3");
    CHECK(v[2] == "L4");
}

TEST(GuiPanelChat, ScrollUpDown) {
    ChatConsole c(3, 100);
    for (int i = 0; i < 6; ++i)
        c.AppendLine("L" + std::to_string(i));  // L0..L5
    c.ScrollUp(2);  // show L1,L2,L3
    auto v = c.VisibleLines();
    CHECK(v[0] == "L1" && v[2] == "L3");
    c.ScrollUp(100);  // clamp to top: L0,L1,L2
    v = c.VisibleLines();
    CHECK(v[0] == "L0" && v[2] == "L2");
    c.ScrollDown(100);  // back to bottom: L3,L4,L5
    v = c.VisibleLines();
    CHECK(v[0] == "L3" && v[2] == "L5");
}

TEST(GuiPanelChat, ScrollbackCap) {
    ChatConsole c(3, /*scrollback=*/4);
    for (int i = 0; i < 7; ++i)
        c.AppendLine("L" + std::to_string(i));  // L0..L6, keep last 4
    CHECK_EQ(c.LineCount(), 4);
    CHECK(c.Line(0) == "L3");
    CHECK(c.Line(3) == "L6");
}

TEST(GuiPanelChat, SubmitClearsInput) {
    ChatConsole c(8, 100);
    c.SetInput("hello world");
    std::string line = c.Submit(7);
    CHECK(line == std::string("$7FF hello world$A"));
    CHECK(c.Input().empty());
    CHECK_EQ(c.LineCount(), 1);
    CHECK(c.Line(0) == line);
}

// ---------------------------------------------------------------------------
// DebugIdList — append cap.
// ---------------------------------------------------------------------------
TEST(GuiPanelDebugList, AppendCap) {
    DebugIdList list;
    for (int i = 0; i < kDebugListCapacity; ++i)
        CHECK(list.Append(i * 7));
    CHECK_EQ(list.Count(), 128);
    CHECK(!list.Append(999));   // full -> reject
    CHECK_EQ(list.Count(), 128);
    CHECK_EQ(list.At(0), 0);
    CHECK_EQ(list.At(127), 127 * 7);
}

// ---------------------------------------------------------------------------
// DebugWindow — memory info running total.
// ---------------------------------------------------------------------------
TEST(GuiPanelDebugMem, MemoryInfoTotal) {
    MemoryInfoSizes s;
    s.textures = 1000;
    s.objectInstances = 2000;
    s.stockObjects = 500;
    s.supermap = 300;
    s.floor = 200;
    s.animations = 400;
    s.animationsTotal = 9999;   // does NOT feed total
    s.characterCount = 42;
    s.playerCount = 3;
    auto lines = DebugWindow_BuildMemoryInfo(s);
    // Find the "Total" line.
    i32 total = -1;
    for (auto& l : lines)
        if (l.label == "Total")
            total = l.bytes;
    CHECK_EQ(total, 1000 + 2000 + 500 + 300 + 200 + 400);  // 4400
    // counts present and not in total.
    bool hasChar = false, hasPlayer = false;
    for (auto& l : lines) {
        if (l.label == "Charactercount") { hasChar = true; CHECK_EQ(l.bytes, 42); }
        if (l.label == "Playercount")    { hasPlayer = true; CHECK_EQ(l.bytes, 3); }
    }
    CHECK(hasChar && hasPlayer);
}

// ---------------------------------------------------------------------------
// DamageLabelTable — insert / find / LRU-evict.
// ---------------------------------------------------------------------------
TEST(GuiPanelDamage, RegisterAndFind) {
    DamageLabelTable t;
    int s0 = t.Register(/*owner=*/10, /*payload=*/100, "hit", /*now=*/1);
    CHECK_EQ(s0, 0);
    // Same owner -> same slot, no new entry.
    int s0b = t.Register(10, 999, "ignored", 2);
    CHECK_EQ(s0b, 0);
    CHECK_EQ(t.UsedCount(), 1);
    CHECK_EQ(t.Slot(0).payload, 100);  // not overwritten
    int s1 = t.Register(11, 200, "miss", 3);
    CHECK_EQ(s1, 1);
    CHECK_EQ(t.UsedCount(), 2);
    CHECK_EQ(t.Find(11), 1);
    CHECK_EQ(t.Find(99), -1);
}

TEST(GuiPanelDamage, FullTableEvict) {
    DamageLabelTable t;
    for (int i = 0; i < kDamageLabelSlots; ++i)
        CHECK(t.Register(1000 + i, i, "e", /*now=*/i + 1) >= 0);
    CHECK_EQ(t.UsedCount(), 64);
    // New owner, now larger than all timestamps -> evict the last older slot (63).
    int slot = t.Register(2000, 555, "new", /*now=*/100);
    CHECK_EQ(slot, 63);
    CHECK_EQ(t.Slot(63).owner, 2000);
    // now == 0: no slot older than 0 -> rejected.
    DamageLabelTable t2;
    for (int i = 0; i < kDamageLabelSlots; ++i)
        t2.Register(1000 + i, i, "e", /*now=*/i + 1);
    CHECK_EQ(t2.Register(3000, 1, "x", /*now=*/0), -1);
}
