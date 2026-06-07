// Integration test for guild::gui gui_dialogs7.
//
// REAL SIBLING WIRING: Panel_ShowUseObject / Window_ShowProgressForm call the
// already-reconstructed form sibling gui::Form_SelectWindow (form.cpp 0x41e4cc)
// DIRECTLY in the production path, and ShowUseObject also forwards its
// formCenterChildWindows hook into the REAL gui::Form_CenterChildWindows
// (form_lifecycle.cpp 0x41d6ac). Here we wire a real Form record (g_forms) +
// real Window pool (g_windows), forward those hooks into the live siblings, drive
// Panel_ShowUseObject, and assert that the REAL Form_SelectWindow resolved the
// form's window-id table into the engine's current-window globals
// (g_currentWindowId / g_currentWindow) — i.e. the cross-module GUI form/window
// data model flowed exactly as it would in the live process.
#include "test.h"

#include "gui/gui_dialogs7.h"
#include "gui/form.h"           // g_forms, g_currentFormId, g_currentWindowId, Form_SelectWindow
#include "gui/form_lifecycle.h" // Form_CenterChildWindows (real)
#include "gui/window.h"         // g_windows, g_currentWindow
#include "gui/object.h"         // g_widgets

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

constexpr int kFormId  = 4;
constexpr int kWinSlot = 7;

int g_frameBudget = 0;
int g_useObjForm = -1;

// gameTickFinalize stand-in: returns the prebuilt form id (the loader builds the
// panel\useobj form in the live game; here the form already exists in g_forms).
int ItGameTick(i16, i16, const char*) { return g_useObjForm; }

// Forward the centering hook into the REAL reconstructed sibling.
int ItFormCenter(int formId) { Form_CenterChildWindows(formId); return 0; }

int ItFrameLoop(int, int, const void*) { return g_frameBudget-- > 0 ? 1 : 0; }
int ItMouseRelease() { return 0; }
int ItText(unsigned, unsigned) { return 0; }
void ItDrag(int, int) {}

GuiDialogs7Hooks MakeWiredHooks() {
    GuiDialogs7Hooks h = *GuiDialogs7Hooks_Default();
    h.gameTickFinalize = ItGameTick;
    h.formCenterChildWindows = ItFormCenter; // -> REAL gui::Form_CenterChildWindows
    h.gameLogicRunFrameLoop = ItFrameLoop;
    h.readMouseRelease = ItMouseRelease;
    h.textRenderRichString = ItText;
    h.dragCursorSetSprite = ItDrag;
    return h;
}

} // namespace

TEST(GuiDialogs7It, ShowUseObjectDrivesRealFormSelectWindow) {
    ResetGuiDialogs7();
    // Build a real form: 1 child window whose logical slot maps to window kWinSlot.
    g_forms[kFormId] = Form{};
    g_forms[kFormId].windowCount() = 1;
    g_forms[kFormId].windowId(0)   = kWinSlot;
    g_currentFormId  = 0;
    g_currentWindowId = -1;
    g_currentWindow   = nullptr;
    // Give the target window real dimensions so the (inert) centering walk has a
    // valid backing-widget index (0) to inspect.
    g_windows[kWinSlot] = Window{};
    g_windows[kWinSlot].w() = 200;
    g_windows[kWinSlot].h() = 100;
    g_windows[kWinSlot].backWidget() = 0;
    g_widgets[0] = Widget{};

    g_useObjForm = kFormId;
    g_frameBudget = 1;
    GuiDialogs7Hooks hooks = MakeWiredHooks();
    const GuiDialogs7Hooks* prev = SetGuiDialogs7Hooks(&hooks);

    int r = Panel_ShowUseObject("a scroll", nullptr);

    SetGuiDialogs7Hooks(prev);

    // The REAL Form_SelectWindow(form, 0) must have resolved the current window from
    // the form's window-id table.
    CHECK_EQ(g_currentFormId, kFormId);
    CHECK_EQ(g_currentWindowId, kWinSlot);
    CHECK(g_currentWindow == &g_windows[kWinSlot]);
    CHECK_EQ(g_useObjLastClicked, -1);
    CHECK_EQ(r, 0);
}

TEST(GuiDialogs7It, ShowProgressFormRoutesThroughRealFormSelectWindow) {
    ResetGuiDialogs7();
    // Window_ShowProgressForm selects slot 2 of form 0; build that real form.
    g_forms[0] = Form{};
    g_forms[0].windowCount() = 3;
    g_forms[0].windowId(2)   = 9;
    g_currentFormId  = 0;
    g_currentWindowId = -1;
    g_currentWindow   = nullptr;
    g_windows[9] = Window{};

    GuiDialogs7Hooks hooks = MakeWiredHooks();
    const GuiDialogs7Hooks* prev = SetGuiDialogs7Hooks(&hooks);

    Window_ShowProgressForm();

    SetGuiDialogs7Hooks(prev);

    // The real Form_SelectWindow(0, 2) resolved window slot 2 -> window id 9.
    CHECK_EQ(g_currentWindowId, 9);
    CHECK(g_currentWindow == &g_windows[9]);
}
