// smallleaves_wave22_test.cpp — golden vectors for the wave-22 small reachable
// leaves reconstructed 1:1 from gilde.exe:
//   render::Coord_Transform          0x5d8b00
//   render::Resource_FlushAndFree    0x5d9104
//   sim::CutsceneSet/GetRandSeed     0x4ac9a0 / 0x4ac9c0
//   play::Input_SetIconTextById      0x40fb4c
//   gui::BeginDragText               0x4ad508
//   gui::MapView_AddCornerObjects    0x5437d8  (placement table)
#include "test.h"

#include "render/coord_transform_leaf.h"
#include "sim/cutscene_rand.h"
#include "play/input_icon_text.h"
#include "gui/dragtext.h"
#include "gui/mapview.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;

// ---------------------------------------------------------------------------
// render::Coord_Transform @0x5d8b00
//   if (base==0) return 0; else return base + *(i32*)(base + 0x45 + 4*index)
//   golden bytes: 85 c0 75 01 c3 / 81 e2 ff ff 00 00 / 03 44 90 45 (add eax,[eax+edx*4+45h])
// ---------------------------------------------------------------------------
TEST(CoordTransformW22, NullBasePassthrough) {
    unsigned char mem[256] = {0};
    CHECK_EQ(render::Coord_Transform(0, 0, mem), 0);
    CHECK_EQ(render::Coord_Transform(0, 7, mem), 0);
}

TEST(CoordTransformW22, FieldAddAtOffset45) {
    // Layout a record at base 0x10: the i32 field at 0x10 + 0x45 + 4*index.
    unsigned char mem[512] = {0};
    const i32 base = 0x10;
    auto put = [&](int byteOff, i32 v) { std::memcpy(mem + byteOff, &v, 4); };

    put(base + 0x45 + 0, 100);   // index 0
    put(base + 0x45 + 4, 200);   // index 1
    put(base + 0x45 + 8, -50);   // index 2 (signed add)

    CHECK_EQ(render::Coord_Transform(base, 0, mem), base + 100);
    CHECK_EQ(render::Coord_Transform(base, 1, mem), base + 200);
    CHECK_EQ(render::Coord_Transform(base, 2, mem), base - 50);
    CHECK_EQ(render::kCoordFieldBase, 0x45);
    CHECK_EQ(render::kCoordFieldStride, 4);
}

// ---------------------------------------------------------------------------
// render::Resource_FlushAndFree @0x5d9104 — returns FreeEntryData's result
// (captured in edx across File_FreeStream), calling both in order.
// golden tail bytes: 89 d0 5b c3  (mov eax,edx / pop ebx / retn)
// ---------------------------------------------------------------------------
namespace {
std::vector<const char*> g_flushOrder;
i32 g_lastA1, g_lastA2;
i32 FakeFreeEntry(i32 a1, i32 a2) { g_flushOrder.push_back("entry"); g_lastA1=a1; g_lastA2=a2; return 0x1234; }
i32 FakeFreeStream(i32 a1) { g_flushOrder.push_back("stream"); g_lastA1=a1; return 0x9999; }
}

TEST(ResourceFlushW22, ReturnsEntryResultNotStream) {
    g_flushOrder.clear();
    render::ResourceFlushHooks h{ &FakeFreeEntry, &FakeFreeStream };
    i32 r = render::Resource_FlushAndFree(0x55, 1, h);
    CHECK_EQ(r, 0x1234);                 // FreeEntryData result, NOT FreeStream's 0x9999
    CHECK_EQ((int)g_flushOrder.size(), 2);
    CHECK(std::strcmp(g_flushOrder[0], "entry") == 0);
    CHECK(std::strcmp(g_flushOrder[1], "stream") == 0);
    CHECK_EQ(g_lastA1, 0x55);            // both called with a1
}

TEST(ResourceFlushW22, NullHooksSafe) {
    render::ResourceFlushHooks h{};
    CHECK_EQ(render::Resource_FlushAndFree(1, 1, h), 0);
}

// ---------------------------------------------------------------------------
// sim::CutsceneSet/GetRandSeed @0x4ac9a0 / 0x4ac9c0 — shared global seed word
// dword_11B4E38, with a "cut_(get)randseed: %i" trace side effect.
// ---------------------------------------------------------------------------
namespace {
std::vector<std::string> g_randLog;
int FakeRandLog(const char* fmt, i32 v) {
    g_randLog.push_back(std::string(fmt));
    return (int)std::strlen(fmt) + 4;   // arbitrary deterministic char count
}
}

TEST(CutsceneRandW22, SetGetRoundTripsGlobal) {
    sim::SetCutsceneRandLogHook(&FakeRandLog);
    g_randLog.clear();

    sim::CutsceneSetRandSeed(0x1357);
    CHECK_EQ(sim::g_cutsceneRandSeed, 0x1357);   // dword_11B4E38 stored
    CHECK_EQ(sim::CutsceneGetRandSeed(), 0x1357); // reloaded + returned

    // Set's return value mirrors the sprintf char count.
    CHECK_EQ(sim::CutsceneSetRandSeed(42), (int)std::strlen("cut_randseed: %i") + 4);

    // Trace format strings (exact, from get_bytes @0x61d8ec/0x61d900).
    CHECK(g_randLog[0] == "cut_randseed: %i");
    CHECK(g_randLog[1] == "cut_getrandseed: %i");
    sim::SetCutsceneRandLogHook(nullptr);         // restore default
}

TEST(CutsceneRandW22, SingleGlobalNoSecondState) {
    // Setting via the free function must be observable through the same global the
    // snapshot/restore callers read.
    sim::CutsceneSetRandSeed(-1);
    CHECK_EQ(sim::g_cutsceneRandSeed, -1);
    sim::CutsceneSetRandSeed(0);
}

// ---------------------------------------------------------------------------
// play::Input_SetIconTextById @0x40fb4c — splice a string into an edit buffer,
// replacing the previous insertion, then re-measure.
// ---------------------------------------------------------------------------
namespace {
i16 FakeMeasure(const char* s, int font) { return (i16)((int)std::strlen(s) * 6 + font); }
}

TEST(IconTextW22, SpliceAndMeasure) {
    // Edit buffer holds "[]REST\0"; we insert "AB" at base, replacing nothing first.
    unsigned char buf[64] = {0};
    const char* rest = "REST";
    std::memcpy(buf, rest, std::strlen(rest) + 1);   // "REST\0"

    play::IconTextObject obj{};
    obj.editBuf = buf;
    obj.insertedLen = 0;       // nothing inserted yet
    obj.font = 3;

    play::IconTextUnderlying u{};
    u.present = true;
    u.regionBase = 0;          // base address of the region == 0 (buf maps to addr 0)
    u.text = (const char*)buf; // strlen of current region

    u16 w = play::Input_SetIconTextById(obj, /*editBufBase=*/0, u, "AB", &FakeMeasure);

    // The new string is spliced at the front: "AB" + "REST".
    CHECK(std::strncmp((const char*)buf, "ABREST", 6) == 0);
    CHECK_EQ(obj.insertedLen, 2);
    // measured width = strlen("AB")*6 + font(3) = 15.
    CHECK_EQ((int)w, 2 * 6 + 3);
    CHECK_EQ((int)obj.measuredW, 15);
}

TEST(IconTextW22, NoBufferGate) {
    play::IconTextObject obj{};   // editBuf == nullptr -> skip
    play::IconTextUnderlying u{}; u.present = true;
    CHECK_EQ((int)play::Input_SetIconTextById(obj, 0, u, "X", &FakeMeasure), 0);
    // also gated when underlying absent
    unsigned char buf[8] = {0};
    obj.editBuf = buf;
    play::IconTextUnderlying u2{}; u2.present = false;
    CHECK_EQ((int)play::Input_SetIconTextById(obj, 0, u2, "X", &FakeMeasure), 0);
}

// ---------------------------------------------------------------------------
// gui::BeginDragText @0x4ad508 — arm a text drag.
// ---------------------------------------------------------------------------
namespace {
std::vector<const char*> g_dragOrder;
std::string g_tooltip;
void HkSprite() { g_dragOrder.push_back("sprite"); }
void HkResetTbl() { g_dragOrder.push_back("table"); }
void HkResetBtn() { g_dragOrder.push_back("buttons"); }
void HkTooltip(const char* s) { g_dragOrder.push_back("tooltip"); g_tooltip = s ? s : ""; }
}

TEST(BeginDragTextW22, CopiesStateAndOrdersLeaves) {
    g_dragOrder.clear();
    gui::DragTextHooks h{ &HkSprite, &HkResetTbl, &HkResetBtn, &HkTooltip };
    gui::DragTextState st{};

    i32 ret = gui::BeginDragText(st, "Bread", /*cursorX=*/100, /*cursorY=*/200,
                                 /*gameTick=*/777, h);

    // String copied into the active buffer.
    CHECK(std::strcmp(st.activeText, "Bread") == 0);
    // Drag box: x = cursorX-8, y = cursorY, others 0, active=1, tick latched.
    CHECK_EQ(st.boxX, 100 - 8);
    CHECK_EQ(st.boxY, 200);
    CHECK_EQ(st.boxX2, 0);
    CHECK_EQ(st.boxY2, 0);
    CHECK_EQ(st.active, 1);
    CHECK_EQ(st.startTick, 777);
    CHECK_EQ(ret, 200);                 // returns boxY (dword_62D0C8)
    // tooltip received the drag text.
    CHECK(g_tooltip == "Bread");
    // Leaf call order: sprite, table, buttons (before copy), then tooltip.
    CHECK_EQ((int)g_dragOrder.size(), 4);
    CHECK(std::strcmp(g_dragOrder[0], "sprite") == 0);
    CHECK(std::strcmp(g_dragOrder[1], "table") == 0);
    CHECK(std::strcmp(g_dragOrder[2], "buttons") == 0);
    CHECK(std::strcmp(g_dragOrder[3], "tooltip") == 0);
}

// ---------------------------------------------------------------------------
// gui::MapView_AddCornerObjects @0x5437d8 — placement table (mx=my=0 snapshot).
//   obj0: x=0,   y=0     gfx+0
//   obj1: x=0,   y=418   gfx+1
//   obj2: x=0,   y=120   gfx+2
//   obj3: x=579, y=120   gfx+3
// ---------------------------------------------------------------------------
TEST(MapCornerW22, PlacementTable) {
    CHECK_EQ((int)gui::kMapCornerPlacements[0].x, 0);
    CHECK_EQ((int)gui::kMapCornerPlacements[0].y, 0);
    CHECK_EQ((int)gui::kMapCornerPlacements[1].x, 0);
    CHECK_EQ((int)gui::kMapCornerPlacements[1].y, 418);  // 0x1A2
    CHECK_EQ((int)gui::kMapCornerPlacements[2].x, 0);
    CHECK_EQ((int)gui::kMapCornerPlacements[2].y, 120);  // 0x78
    CHECK_EQ((int)gui::kMapCornerPlacements[3].x, 579);  // 0x243
    CHECK_EQ((int)gui::kMapCornerPlacements[3].y, 120);
}
