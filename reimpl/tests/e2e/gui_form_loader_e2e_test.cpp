// End-to-end: load a synthetic .form/.gfx catalogue, build a dialog (a form owning a
// couple of windows with inline widgets created via markup) through the real
// widget-create leaves, and verify the resulting form -> window -> object tree against
// a reference.  Exercises gui/form_loader + gui/markup_build + gui/window_render +
// the widget-create leaves together.
#include "tests/framework/test.h"

#include "gui/form_loader.h"
#include "gui/markup_build.h"
#include "gui/window_render.h"
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

// ---- Renderer/property edge stubs (shared with the unit TU's needs) -----------------
namespace guild::gui {
i16 Property_Get(const char* text, int) { return static_cast<i16>(text ? std::strlen(text) : 0); }
int Property_Validate(const char* name) {
    return (name && std::strcmp(name, "_BUTTON_RED") == 0) ? 7 : 0;
}
void RegisterGfxState(int) {}
void Widget_LayoutBounds(int, int, int) {}
void Widget_RefreshText(int) {}
} // namespace guild::gui

namespace {
std::array<u8, 84> Rec(i32 flags68, i32 scene56) {
    std::array<u8, 84> r{}; r.fill(0);
    r[68] = static_cast<u8>(flags68);
    std::memcpy(&r[56], &scene56, 4);
    return r;
}
std::vector<u8> GfxBuffer(const std::vector<std::array<u8, 84>>& recs) {
    std::vector<u8> b;
    u32 n = static_cast<u32>(recs.size());
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>((n >> (8 * i)) & 0xFF));
    for (const auto& r : recs) b.insert(b.end(), r.begin(), r.end());
    return b;
}
} // namespace

TEST(GuiFormLoaderE2E, LoadFormBuildDialogVerifyTree) {
    ResetGuiState();
    ResetWidgetCreate();
    ResetGfxObjects();
    ResetMarkupBuild();

    // ---- 1. Load a synthetic .gfx catalogue (initialises the form/window/widget
    //         tables AND populates the 84-byte object table). ----
    std::vector<std::array<u8, 84>> recs = {
        Rec(0x00, 0),     // obj 0: passive
        Rec(0x01, 1234),  // obj 1: drawable -> needs a scene-state
        Rec(0x01, 0),     // obj 2: flag but no scene -> no state
        Rec(0x00, 99),    // obj 3
    };
    auto buf = GfxBuffer(recs);
    bool ok = Form_LoadFromBuffer(buf.data(), buf.size());
    CHECK(ok);
    CHECK_EQ(g_gfxObjectCount, 4);
    // The table baseline init stamped every slot with its own index.
    CHECK_EQ(g_forms[3].dw[0], 3);
    CHECK_EQ(g_windows[5].at<i32>(0), 5);

    // ---- 2. Build a "dialog": a form owning two windows. ----
    // Window A: a title + an OK red button + a Cancel red button via markup.
    int winA = Window_Create(20, 20, 240, 120, 0);
    CHECK(winA >= 0);
    auto aObjs = BuildMarkupIntoWindow(winA, "$ia[OK]$ia[Cancel]");
    CHECK_EQ(static_cast<int>(aObjs.size()), 2);

    // Window B: an embedded object (id 1234, the drawable gfx) + an input field.
    int winB = Window_Create(20, 150, 240, 80, 0);
    CHECK(winB >= 0);
    std::vector<int> pendingB = { 1234 };
    auto bObjs = BuildMarkupIntoWindow(winB, "$ic$t", pendingB);
    CHECK_EQ(static_cast<int>(bObjs.size()), 2);

    // Wire the two windows into form id 2 (form's window-id table + count).
    int formId = 2;
    g_forms[formId].windowCount() = 2;
    g_forms[formId].windowId(0) = winA;
    g_forms[formId].windowId(1) = winB;

    // ---- 3. Verify the form -> window -> object tree against the reference. ----
    // Form selection resolves window B (group 1).
    g_currentFormId = formId;
    CHECK(Form_SelectWindow(formId, 1) == 1);
    CHECK_EQ(g_currentWindowId, winB);

    // Window A has exactly the two red-button sprites as children.
    CHECK_EQ(static_cast<int>(g_windows[winA].objCount()), 2);
    i32* listA = WindowChildList(winA);
    CHECK_EQ(g_widgets[listA[0]].type(), static_cast<u8>(kTypeSprite));
    CHECK_EQ(g_widgets[listA[1]].type(), static_cast<u8>(kTypeSprite));
    CHECK_EQ(listA[0], aObjs[0].widgetIdx);
    CHECK_EQ(listA[1], aObjs[1].widgetIdx);
    // Children are owned by window A.
    CHECK_EQ(g_widgets[listA[0]].ownerWindow(), winA);

    // Window B: child 0 is the embedded clickable object, child 1 the 'A' input field.
    CHECK_EQ(static_cast<int>(g_windows[winB].objCount()), 2);
    i32* listB = WindowChildList(winB);
    CHECK(bObjs[0].kind == MarkupObjectKind::Embedded);
    CHECK_EQ(bObjs[0].objId, 1234);
    CHECK_EQ(g_widgets[listB[0]].at<i32>(68), 1); // 'c' selector -> clickable
    CHECK_EQ(g_widgets[listB[1]].type(), static_cast<u8>(kTypeAnim)); // input field

    // Object accessors resolve through the form: window B group 1, local object 0.
    int childId = Form_GetChildObjectId(formId, 1, 0);
    CHECK_EQ(childId, listB[0]);

    // ---- 4. Drive a render-update pass: scroll window B and confirm bookkeeping. ----
    g_windows[winB].raw[608] = 5;            // scrollStep
    g_windows[winB].at<i32>(580) = 40;       // scrollMaxY
    g_windows[winB].at<i32>(592) = 5;        // scroll down by 5
    int sx = Window_ApplyScrollOffset(winB);
    CHECK_EQ(g_windows[winB].at<i32>(584), 5); // scrollY advanced one step
    CHECK_EQ(sx, 0);                            // no horizontal scroll
    CHECK_EQ(g_windows[winB].at<i32>(588), 5);  // prevY snapshot updated

    // RenderUpdates dispatch returns 0 when no frame is pending (default stub).
    CHECK_EQ(Window_RenderUpdates(), 0);
}
