// Integration test: drive a gui_dialogs5 panel builder against a REAL reconstructed
// sibling — gui::Widget_AllocSlot (VIBE_Widget_AllocSlot @0x412dac, src/gui/object.cpp)
// and the real g_widgets pool — exactly as the live wiring does. No mocks for the
// allocator: the panel builder's objectAddToWindow / objectAddTextLabel /
// windowAddChildWindow hooks forward into the real allocator, and we assert that the
// build laid out distinct, in-use widget slots in the shared g_widgets array.
#include "test.h"

#include "gui/gui_dialogs5.h"
#include "gui/object.h"   // g_widgets, g_widgetCache, Widget_AllocSlot, ResetWidgets
#include "gui/window.h"   // g_currentWindowId

#include <cstring>
#include <set>

using namespace guild;
using namespace guild::gui;

namespace {

// Track which slots the real allocator handed out so we can verify them.
std::set<int>* g_allocated = nullptr;

// Hooks that forward the widget-creation leaves into the REAL Widget_AllocSlot.
int RealAddToWindow(int /*curWin*/, int /*y*/) {
    int s = Widget_AllocSlot();           // REAL sibling
    if (s >= 0 && g_allocated) g_allocated->insert(s);
    return s;
}
int RealAddTextLabel(i16, i16, int, const char*) {
    int s = Widget_AllocSlot();
    if (s >= 0 && g_allocated) g_allocated->insert(s);
    return s;
}
int RealAddField(i16, i16, i16, i16, unsigned, int) {
    int s = Widget_AllocSlot();
    if (s >= 0 && g_allocated) g_allocated->insert(s);
    return s;
}
int RealAddChild(int, i16, i16, int, int, int) {
    int s = Widget_AllocSlot();
    if (s >= 0 && g_allocated) g_allocated->insert(s);
    return s;
}

// Minimal collect-count hook so BuildMoneyInfo lays out a couple of rows.
int CollectTwo(unsigned short, int, i32* out) { out[0] = 11; out[1] = 22; return 2; }
int FindBld(int) { return 1; }
int FormId(i16, i16, const char*) { return 5; }
int WinId(int, int) { return 3; }

} // namespace

TEST(GuiDialogs5Itest, BuildMoneyInfoAllocatesRealWidgetSlots) {
    ResetWidgets();              // clean real pool
    ResetGuiDialogs5();
    g_currentWindowId = 0;

    std::set<int> allocated;
    g_allocated = &allocated;

    // Start from the module defaults, then forward the widget-creation leaves into
    // the real allocator and provide the two collected buildings.
    GuiDialogs5Hooks h = *GuiDialogs5Hooks_Default();
    h.objectAddToWindow   = &RealAddToWindow;
    h.objectAddTextLabel  = &RealAddTextLabel;
    h.inputAddFieldToWindow = &RealAddField;
    h.windowAddChildWindow = &RealAddChild;
    h.gameTickFinalize    = &FormId;
    h.formGetWindowId     = &WinId;
    h.buildingCollectByCityHandle = &CollectTwo;
    h.buildingFindById    = &FindBld;
    const GuiDialogs5Hooks* prev = SetGuiDialogs5Hooks(&h);

    int form = Panel_BuildMoneyInfo(/*city*/0);
    CHECK_EQ(form, 5);

    // Two buildings => 2 row-objects + 2 child windows = 4 distinct real slots.
    CHECK_EQ((int)allocated.size(), 4);

    // Every handed-out slot is a REAL, in-use slot in the shared g_widgets pool,
    // and they are mutually distinct (the allocator never aliased).
    bool allInUse = true, allDistinct = true;
    int seen = 0;
    for (int s : allocated) {
        if (s < 0 || s >= kMaxWidgets) { allDistinct = false; continue; }
        if (!g_widgets[s].inUse()) allInUse = false;
        ++seen;
    }
    CHECK(allInUse);
    CHECK(allDistinct);
    CHECK_EQ(seen, 4);

    // The child-window cross-module write went into the real Widget records:
    // BuildMoneyInfo sets +20=69, +22=16 on each child window slot.
    int childWith69 = 0;
    for (int s : allocated) {
        if (s >= 0 && s < kMaxWidgets && g_widgets[s].at<i16>(20) == 69 &&
            g_widgets[s].at<i16>(22) == 16)
            ++childWith69;
    }
    CHECK_EQ(childWith69, 2);     // the two child windows

    g_allocated = nullptr;
    SetGuiDialogs5Hooks(prev);
    ResetWidgets();
}

TEST(GuiDialogs5Itest, ChooseWappenAllocatesEightThroughRealAllocator) {
    ResetWidgets();
    ResetGuiDialogs5();
    g_currentWindowId = 0;

    std::set<int> allocated;
    g_allocated = &allocated;

    GuiDialogs5Hooks h = *GuiDialogs5Hooks_Default();
    h.objectAddToWindow = &RealAddToWindow;
    h.gameTickFinalize  = &FormId;
    h.radioGroupCreate  = [](int, int) { return 3; };
    const GuiDialogs5Hooks* prev = SetGuiDialogs5Hooks(&h);

    Panel_RunChooseWappen(2);

    // Eight crest buttons => eight distinct real in-use slots.
    CHECK_EQ((int)allocated.size(), 8);
    bool allInUse = true;
    for (int s : allocated)
        if (s < 0 || s >= kMaxWidgets || !g_widgets[s].inUse()) allInUse = false;
    CHECK(allInUse);

    g_allocated = nullptr;
    SetGuiDialogs5Hooks(prev);
    ResetWidgets();
}
