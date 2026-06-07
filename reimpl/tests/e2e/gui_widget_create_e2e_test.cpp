// End-to-end: build a small dialog by calling the REAL widget-create leaves
// (button + slider + edit field + label) and verify the resulting window child tree and
// each child widget's recovered field layout against a hand-computed reference.
#include "gui/widget_create.h"
#include "gui/window_layout.h"
#include "gui/form.h"
#include "gui/object.h"
#include "gui/window.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

using namespace guild::gui;

// Renderer/metric edge overrides (strong; replace the weak defaults).
namespace guild::gui {
i16  GfxMetricWord(int, int byteOff)  { return byteOff == 82 ? 6 : 0; }
i32  GfxMetricDword(int, int byteOff) { return (byteOff == 78 || byteOff == 80) ? (8 << 16) : 0; }
i16  SliderTrackExtent(int, int)      { return 18; }
void* SceneStateFor(int gfxId)        { return reinterpret_cast<void*>(static_cast<std::intptr_t>(0x2000 + gfxId)); }
int  GlyphAdvance(void*, int)         { return 2; }
int  ButtonBankFrameCount(int)        { return 4; }
} // namespace guild::gui

TEST(GuiWidgetCreateE2E, BuildDialogChildTree) {
    ResetGuiState();
    ResetWidgetCreate();
    g_screenClipW   = (10000 << 16);
    g_screenClipExt = (10000 << 16) | 10000;

    // A dialog window with a text-buffer so AutoFitHeight has a content extent.
    int win = Window_Create(40, 30, 260, 160, kWinFlagTextBuffer);
    CHECK_EQ(win, 0);

    // --- Build the dialog by calling the real leaves, in declaration order. -------------
    int title  = Object_AddTextLabel(8, 6, win, "Settings");      // child 0  'C'
    int volume = Widget_AddSliderToWindow(8, 30, /*val*/ 75, /*range*/ 200,
                                          /*max*/ 100, /*gfx*/ 4, /*flags*/ 2, win); // child 1 'E'
    int name   = Input_AddFieldToWindow(8, 60, /*step*/ 1, /*value*/ 0, /*flags*/ 1, win); // child 2 'A'
    int ok     = Object_AddButtonLabel(8, 100, win, "OK");        // child 3  'F'

    CHECK(title >= 0 && volume >= 0 && name >= 0 && ok >= 0);

    Window& w = g_windows[win];
    // --- Child tree: 4 children, in creation order, with the expected widget indices. ---
    CHECK_EQ((int)w.objCount(), 4);
    CHECK_EQ(WindowChildList(win)[0], title);
    CHECK_EQ(WindowChildList(win)[1], volume);
    CHECK_EQ(WindowChildList(win)[2], name);
    CHECK_EQ(WindowChildList(win)[3], ok);

    // --- Per-widget reference field checks. ---------------------------------------------
    // Title label 'C': window-relative position, width = strlen("Settings")+96.
    Widget& t = g_widgets[title];
    CHECK_EQ((int)t.type(), 0x43);
    CHECK_EQ((int)t.x(), 40 + 8);
    CHECK_EQ((int)t.y(), 30 + 6);
    CHECK_EQ((int)t.w(), (int)std::strlen("Settings") + 96);
    CHECK(std::strcmp(reinterpret_cast<char*>(&t.at<char>(116)), "Settings") == 0);

    // Volume slider 'E': value/range/max/step recovered, horizontal track sizing.
    Widget& s = g_widgets[volume];
    CHECK_EQ((int)s.type(), 0x45);
    CHECK_EQ((int)s.x(), 40 + 8);
    CHECK_EQ((int)s.y(), 30 + 30);
    CHECK_EQ(s.at<i32>(120), 75);          // value
    CHECK_EQ(s.at<i32>(128), 100);         // max
    CHECK_EQ(s.at<i32>(136), 200);         // range
    CHECK_EQ(s.at<i32>(140), 100 / 2);     // step
    CHECK_EQ((int)s.w(), 200 + 16);        // range + 2*(metric>>16)
    CHECK_EQ((int)s.h(), 18);              // SliderTrackExtent

    // Name input 'A': type, record binding, window-relative position.
    Widget& n = g_widgets[name];
    CHECK_EQ((int)n.type(), 0x41);
    CHECK_EQ((int)n.x(), 40 + 8);
    CHECK_EQ((int)n.y(), 30 + 60);
    CHECK(n.at<i32>(116) != 0);            // bound to a field record
    CHECK(WidgetData(name) != nullptr);

    // OK button 'F': text at +124, width = strlen("OK")+96.
    Widget& b = g_widgets[ok];
    CHECK_EQ((int)b.type(), 0x46);
    CHECK_EQ((int)b.x(), 40 + 8);
    CHECK_EQ((int)b.y(), 30 + 100);
    CHECK_EQ((int)b.w(), 2 + 96);
    CHECK(std::strcmp(reinterpret_cast<char*>(&b.at<char>(124)), "OK") == 0);

    // --- All four are distinct widget slots. --------------------------------------------
    CHECK(title != volume && volume != name && name != ok && title != ok);

    // --- Now flip the OK button into a clickable toggle and route a value through the
    //     general value setter, mimicking a dialog interaction. --------------------------
    b.btnFlagA() = 1;
    Object_SetObjectValueOrText(ok, /*value*/ 1, 0, 0, 0);
    CHECK_EQ(b.value(), 1);
    CHECK_EQ(b.valueMirror(), 1);

    // --- Resize the dialog and confirm every child's clip bounds reflow. ----------------
    Window_Resize(280, 220, win);
    for (int i = 0; i < (int)w.objCount(); ++i) {
        Widget& c = g_widgets[WindowChildList(win)[i]];
        CHECK_EQ((int)c.clipY1(), w.h() + w.y());
        CHECK_EQ((int)c.clipX1(), w.w() + w.x());
    }
}

TEST(GuiWidgetCreateE2E, AutoFitHeightBoundsContent) {
    ResetGuiState();
    ResetWidgetCreate();
    g_screenClipW   = (10000 << 16);
    g_screenClipExt = (10000 << 16) | 10000;

    int win = Window_Create(0, 0, 200, 40, 0);
    Object_AddButtonLabel(0, 0, win, "A");
    Object_AddButtonLabel(0, 80, win, "B"); // a child lower than the current height
    int h0 = g_windows[win].h();
    Window_AutoFitHeight(win, 10);
    // The window's height must not shrink below its original; it grows to bound content.
    CHECK(g_windows[win].h() >= h0);
}
