// Unit tests for the .form/.gfx loader, the window render-update bookkeeping, and
// the markup object-build half.  (gui/form_loader, gui/window_render, gui/markup_build)
#include "tests/framework/test.h"

#include "gui/form_loader.h"
#include "gui/window_render.h"
#include "gui/markup_build.h"
#include "gui/widget_create.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/form.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::gui;

// ---- Test-local stubs for the renderer/property edges --------------------------------
// (The production weak symbols are overridden here with counting/behavioural defs.)
namespace guild::gui {
static int g_stateHelperCalls = 0;
static std::vector<int> g_stateHelperIdx;
void RegisterGfxState(int gfxIndex) { ++g_stateHelperCalls; g_stateHelperIdx.push_back(gfxIndex); }

static int g_layoutBoundsCalls = 0;
void Widget_LayoutBounds(int, int, int) { ++g_layoutBoundsCalls; }
static int g_refreshTextCalls = 0;
void Widget_RefreshText(int) { ++g_refreshTextCalls; }

// Property edges: report a stable width and a non-zero _BUTTON_RED id.
i16 Property_Get(const char* text, int) { return static_cast<i16>(text ? std::strlen(text) : 0); }
int Property_Validate(const char* name) {
    return (name && std::strcmp(name, "_BUTTON_RED") == 0) ? 7 : 0;
}
} // namespace guild::gui

namespace {
// Build a synthetic .gfx buffer: u32 count + count*84-byte records.
std::vector<u8> MakeGfxBuffer(const std::vector<std::array<u8, 84>>& recs) {
    std::vector<u8> b;
    u32 n = static_cast<u32>(recs.size());
    b.push_back(static_cast<u8>(n & 0xFF));
    b.push_back(static_cast<u8>((n >> 8) & 0xFF));
    b.push_back(static_cast<u8>((n >> 16) & 0xFF));
    b.push_back(static_cast<u8>((n >> 24) & 0xFF));
    for (const auto& r : recs) b.insert(b.end(), r.begin(), r.end());
    return b;
}
std::array<u8, 84> MakeRec(i32 flags68, i32 scene56, i16 w78, i16 h82) {
    std::array<u8, 84> r{};
    r.fill(0);
    r[68] = static_cast<u8>(flags68);
    std::memcpy(&r[56], &scene56, 4);
    // width/height are stored as 16.16 dwords; HIWORD is the pixel value.
    i32 wpk = static_cast<i32>(w78) << 16;
    i32 hpk = static_cast<i32>(h82) << 16;
    std::memcpy(&r[78], &wpk, 4);
    std::memcpy(&r[82], &hpk, 2); // +82 word only fits 2 (record ends at 84); keep low word
    return r;
}
void FreshGui() {
    ResetGuiState();
    ResetWidgetCreate();
    ResetGfxObjects();
    ResetMarkupBuild();
    g_stateHelperCalls = 0; g_stateHelperIdx.clear();
    g_layoutBoundsCalls = 0; g_refreshTextCalls = 0;
}
} // namespace

// ===== .form / .gfx loader ============================================================

TEST(GuiFormLoader, ParsesHeaderAndRecords) {
    FreshGui();
    std::vector<std::array<u8, 84>> recs = {
        MakeRec(0x00, 0,    10, 12),
        MakeRec(0x01, 555,  20, 22),  // flag 0x1 + scene != 0 -> RegisterGfxState
        MakeRec(0x01, 0,    30, 32),  // flag 0x1 but scene == 0 -> NO state
    };
    auto buf = MakeGfxBuffer(recs);

    bool ok = Form_LoadFromBuffer(buf.data(), buf.size());
    CHECK(ok);
    CHECK_EQ(g_gfxObjectCount, 3);

    // Records copied byte-for-byte.
    CHECK_EQ(static_cast<int>(g_gfxObjects[1].flags()), 0x01);
    CHECK_EQ(g_gfxObjects[1].sceneHandle(), 555);
    CHECK_EQ(g_gfxObjects[1].width() >> 16, 20);

    // Only the record with both flag 0x1 and a non-zero scene handle registers a state.
    CHECK_EQ(g_stateHelperCalls, 1);
    CHECK(g_stateHelperIdx.size() == 1 && g_stateHelperIdx[0] == 1);
}

TEST(GuiFormLoader, InitTablesStampsIndices) {
    FreshGui();
    Form_InitTables();
    // Each slot's dword[0] == its own index (the recycled-id baseline).
    CHECK_EQ(g_forms[0].dw[0], 0);
    CHECK_EQ(g_forms[47].dw[0], 47);
    CHECK_EQ(g_windows[0].at<i32>(0), 0);
    CHECK_EQ(g_windows[95].at<i32>(0), 95);
    CHECK_EQ(g_widgets[0].marker(), 0);
    CHECK_EQ(g_widgets[511].marker(), 511);
}

TEST(GuiFormLoader, RejectsTooManyObjects) {
    FreshGui();
    // Declare a count above the 2048 cap; the body must NOT be read.
    u8 hdr[4];
    u32 n = 2049;
    std::memcpy(hdr, &n, 4);
    bool ok = Form_LoadFromBuffer(hdr, sizeof(hdr));
    CHECK(!ok);
    CHECK_EQ(g_gfxObjectCount, 0);
}

TEST(GuiFormLoader, RejectsShortBuffer) {
    FreshGui();
    // Header says 2 records but the buffer only has 1.
    std::vector<std::array<u8, 84>> recs = { MakeRec(0, 0, 1, 1) };
    auto buf = MakeGfxBuffer(recs);
    // Patch the count to 2.
    buf[0] = 2;
    bool ok = Form_LoadFromBuffer(buf.data(), buf.size());
    CHECK(!ok);
}

// ===== Window render-update bookkeeping ==============================================

namespace {
// Build a minimal enabled window with `n` plain children at known geometry.
int MakeWindow(i16 x, i16 y, i16 w, i16 h) {
    return Window_Create(x, y, w, h, 0);
}
} // namespace

TEST(GuiWindowRender, ApplyScrollOffsetStepsDownAndClamps) {
    FreshGui();
    int slot = MakeWindow(0, 0, 100, 100);
    Window& win = g_windows[slot];

    win.raw[608] = 4;                 // scrollStep = 4
    win.at<i32>(580) = 20;            // scrollMaxY = 20
    win.at<i32>(584) = 0;             // scrollY = 0
    win.at<i32>(592) = 10;            // scrollVelY = 10 (scroll down by 10)

    // One tick: scrollY += 4, vel -> 6.
    Window_ApplyScrollOffset(slot);
    CHECK_EQ(win.at<i32>(584), 4);
    CHECK_EQ(win.at<i32>(592), 6);
    CHECK_EQ(win.at<i32>(588), 4);    // prevY snapshot

    // Two more ticks consume the velocity (4 + 4 -> stops with vel 0 at 12).
    Window_ApplyScrollOffset(slot);   // scrollY 8, vel 2
    Window_ApplyScrollOffset(slot);   // scrollY 12 then folds -2 -> 10, vel 0
    CHECK_EQ(win.at<i32>(592), 0);    // velocity exhausted
    CHECK_EQ(win.at<i32>(584), 10);   // lands exactly on the 10 px requested
}

TEST(GuiWindowRender, ApplyScrollOffsetClampsAtTop) {
    FreshGui();
    int slot = MakeWindow(0, 0, 100, 100);
    Window& win = g_windows[slot];
    win.raw[608] = 8;
    win.at<i32>(584) = 4;             // scrollY = 4
    win.at<i32>(592) = -10;           // negative velocity (scroll up)
    Window_ApplyScrollOffset(slot);
    // 4 - 8 = -4 -> clamped to 0; velocity zeroed.
    CHECK_EQ(win.at<i32>(584), 0);
    CHECK_EQ(win.at<i32>(592), 0);
}

TEST(GuiWindowRender, NormalizeSpriteWidthsEqualisesSprites) {
    FreshGui();
    int slot = MakeWindow(0, 0, 400, 400);
    Window& win = g_windows[slot];

    // Three children: two sprites of differing width + one non-sprite.
    int a = Object_AddToWindow(slot, 0, 0, 0); g_widgets[a].type() = 9;  g_widgets[a].w() = 40;
    int b = Object_AddToWindow(slot, 0, 0, 0); g_widgets[b].type() = 9;  g_widgets[b].w() = 200;
    int c = Object_AddToWindow(slot, 0, 0, 0); g_widgets[c].type() = 0x43; g_widgets[c].w() = 999;

    Window_NormalizeSpriteWidths(slot);

    // widest sprite = 200, +8 = 208 (>=128). Both sprites become 208, non-sprite untouched.
    CHECK_EQ(g_widgets[a].w(), 208);
    CHECK_EQ(g_widgets[b].w(), 208);
    CHECK_EQ(g_widgets[c].w(), 999);
    CHECK_EQ(g_widgets[a].at<i32>(88), 1); // width-overridden flag set on sprites
    CHECK_EQ(g_refreshTextCalls, 2);       // one per sprite
    (void)win;
}

TEST(GuiWindowRender, NormalizeAppliesMinimumWidth) {
    FreshGui();
    int slot = MakeWindow(0, 0, 400, 400);
    int a = Object_AddToWindow(slot, 0, 0, 0); g_widgets[a].type() = 9; g_widgets[a].w() = 10;
    Window_NormalizeSpriteWidths(slot);
    CHECK_EQ(g_widgets[a].w(), 128); // 10+8=18 -> floored to the 128 minimum
}

// ===== Markup object-build half ======================================================

TEST(GuiMarkupBuild, RedButtonCreatesSprite) {
    FreshGui();
    int slot = MakeWindow(10, 20, 300, 200);
    auto objs = BuildMarkupIntoWindow(slot, "$ia[OK]");
    CHECK_EQ(static_cast<int>(objs.size()), 1);
    CHECK(objs[0].kind == MarkupObjectKind::RedButton);
    CHECK_EQ(g_widgets[objs[0].widgetIdx].type(), static_cast<u8>(kTypeSprite));
    CHECK(!objs[0].radio);
}

TEST(GuiMarkupBuild, RadioButtonSetsRadioFlag) {
    FreshGui();
    int slot = MakeWindow(0, 0, 300, 200);
    auto objs = BuildMarkupIntoWindow(slot, "$in[Pick]");
    CHECK_EQ(static_cast<int>(objs.size()), 1);
    CHECK(objs[0].radio);
    CHECK((g_widgets[objs[0].widgetIdx].at<u8>(444) & 0x10) != 0);
}

TEST(GuiMarkupBuild, EmbeddedObjectConsumesPendingId) {
    FreshGui();
    int slot = MakeWindow(0, 0, 300, 200);
    std::vector<int> pending = { 42 };
    auto objs = BuildMarkupIntoWindow(slot, "$ic", pending);
    CHECK_EQ(static_cast<int>(objs.size()), 1);
    CHECK(objs[0].kind == MarkupObjectKind::Embedded);
    CHECK_EQ(objs[0].objId, 42);
    // selector 'c' -> clickable button (+68=1, +72=0).
    CHECK_EQ(g_widgets[objs[0].widgetIdx].at<i32>(68), 1);
    CHECK_EQ(g_widgets[objs[0].widgetIdx].at<i32>(72), 0);
}

TEST(GuiMarkupBuild, EditFieldCreatesInputField) {
    FreshGui();
    int slot = MakeWindow(0, 0, 300, 200);
    auto objs = BuildMarkupIntoWindow(slot, "$t");
    CHECK_EQ(static_cast<int>(objs.size()), 1);
    CHECK(objs[0].kind == MarkupObjectKind::InputField);
    CHECK_EQ(g_widgets[objs[0].widgetIdx].type(), static_cast<u8>(kTypeAnim)); // 'A' input record
}

TEST(GuiMarkupBuild, PlainTextCreatesNoObjects) {
    FreshGui();
    int slot = MakeWindow(0, 0, 300, 200);
    auto objs = BuildMarkupIntoWindow(slot, "Hello $F2 world $A more");
    CHECK_EQ(static_cast<int>(objs.size()), 0); // layout-only tokens + text: no widgets
}
