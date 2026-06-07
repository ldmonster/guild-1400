// End-to-end flow across gui_dialogs4: load a form, mark it dirty + render, focus
// an edit field, type into it, then drag-release to drop focus — exercising
// Form_MarkDirtyAndRender -> Widget_SetFocus -> Widget_HandleKeyInput ->
// Widget_ProcessMouseDrag against the shared Form/Window/Widget model.
#include "test.h"

#include "gui/gui_dialogs4.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {
std::uintptr_t HandleOf(void* p) { return reinterpret_cast<std::uintptr_t>(p); }

int FormLoader(i16, i16, const char* name) {
    return (name && std::strcmp(name, "panel\\edit") == 0) ? 2 : 0;
}
int g_scancodeReturn = 'Z';
int CharToScancode(int, int, int, int) { return g_scancodeReturn; }
int g_surfaceSeq = 0;
int SurfaceCreate(void*, int, int) { return ++g_surfaceSeq; }
}

TEST(GuiDialogs4E2E, LoadFocusTypeReleaseFlow) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.gameTickFinalize = &FormLoader;
    h.inputCharToScancode = &CharToScancode;
    h.surfaceCreate = &SurfaceCreate;
    g_surfaceSeq = 0;
    SetGuiDialogs4Hooks(&h);

    // ---- Stage 1: a form (id 2) with one window (slot 3), backing widget 1, and
    // one child edit widget (slot 12). Mark it dirty + render. ----
    Form& f = g_forms[2];
    f = Form{};
    f.windowCount() = 1;
    f.windowId(0) = 3;

    Window& win = g_windows[3];
    win = Window{};
    win.backWidget() = 1;
    win.objCount() = 1;
    WindowChildList(3)[0] = 12;       // child edit widget slot 12

    g_widgets[1] = Widget{};
    g_widgets[12] = Widget{};

    int fid = Form_MarkDirtyAndRender(0, 0, "panel\\edit");
    CHECK_EQ(fid, 2);
    CHECK_EQ(g_widgets[1].at<i32>(52), 1);   // backing dirty
    CHECK_EQ(g_widgets[12].at<i32>(52), 1);  // edit child dirty
    CHECK_EQ(f.shownFlag(), 1);
    CHECK_EQ(g_surfaceSeq, 2);               // two render surfaces created

    // ---- Stage 2: focus the edit child. Its data record lives at a heap-backed
    // handle; +304 = backing slot 12, +38 = 0 (plain field, no caret widget). ----
    static unsigned char rec[400];
    std::memset(rec, 0, sizeof(rec));
    *reinterpret_cast<i32*>(rec + 304) = 12;  // record backed by widget slot 12
    rec[38] = 0;
    std::uintptr_t recH = HandleOf(rec);
    SetWidgetData(12, rec);                   // widget 12 -> this data record (+12 pointer)

    Widget_SetFocus(12);
    CHECK_EQ(g_focusWidget, recH);
    CHECK_EQ(g_widgets[12].at<i32>(40), 1);   // highlighted

    // ---- Stage 3: type a printable char into the (empty) text field. cap from
    // +36 (text-flag path uses +36 byte when flag&1==0). Set cap = 8. ----
    rec[36] = 8;          // capacity
    g_pendingKey = 4;     // a printable scancode routed through the text-insert branch
    g_pendingMouseX = 0;
    g_scancodeReturn = 'Z';
    Widget_HandleKeyInput();
    // The first character was inserted via CharToScancode.
    CHECK_EQ(reinterpret_cast<char*>(rec + 40)[0], 'Z');
    CHECK_EQ((int)g_pendingKey, 0); // consumed

    // ---- Stage 4: mouse release drops focus and clears the highlight. ----
    g_mouseRelease = 1;
    g_mouseDown = 0;
    Widget_ProcessMouseDrag();
    CHECK_EQ((unsigned long long)g_focusWidget, 0ull); // focus dropped
    CHECK_EQ(g_widgets[12].at<i32>(40), 0);            // highlight cleared
    SetGuiDialogs4Hooks(nullptr);
}

TEST(GuiDialogs4E2E, HoverUpdateTracksAndScrolls) {
    ResetGuiDialogs4();
    SetGuiDialogs4Hooks(nullptr); // inert defaults

    // No hover candidate, no hit-test slot -> returns -1 and leaves hoverSlot reset.
    g_hitTestSlot4 = -1;
    int r = Widget_HoverUpdate();
    CHECK_EQ(r, -1);
    CHECK_EQ(g_hoverSlot, -1);

    // With a hit-test slot present, HoverUpdate forwards it as the result.
    ResetGuiDialogs4();
    g_hitTestSlot4 = 21;
    int r2 = Widget_HoverUpdate();
    CHECK_EQ(r2, 21);
}
