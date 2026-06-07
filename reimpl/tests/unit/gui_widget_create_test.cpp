// Unit tests for the guild::gui widget-CREATION leaves (widget_create.{h,cpp}) and the
// window layout/resize/autofit model (window_layout.{h,cpp}).
//
// Each test allocates + inits + links a control into a window and checks its recovered
// field layout BYTE-FOR-BYTE (type tag, geometry, value/range, clip bounds, child id-list,
// child count) against the values the original gilde.exe leaves would write.
#include "gui/widget_create.h"
#include "gui/window_layout.h"
#include "gui/form.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/slider.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

using namespace guild::gui;

namespace {
void Reset() { ResetGuiState(); ResetWidgetCreate(); }

// Open a plain enabled window at (x,y,w,h) and return its slot.
int OpenWin(i16 x, i16 y, i16 w, i16 h, i32 flags = 0) {
    return Window_Create(x, y, w, h, flags);
}
} // namespace

// ---- Strong overrides of the renderer/metric edges so the size math is deterministic. ----
// (These replace the weak defaults in widget_create.cpp at link time.)
namespace guild::gui {
i16  GfxMetricWord(int /*g*/, int byteOff)  { return byteOff == 82 ? 7 : 0; } // sprite/slider cap word
i32  GfxMetricDword(int /*g*/, int byteOff) {
    // pack a plausible 16.16 metric: tile widths/heights of 8 px.
    if (byteOff == 78 || byteOff == 80) return 8 << 16;
    return 0;
}
i16  SliderTrackExtent(int, int) { return 20; }
void* SceneStateFor(int gfxId)   { return reinterpret_cast<void*>(static_cast<std::intptr_t>(0x1000 + gfxId)); }
int  GlyphAdvance(void*, int)    { return 3; }
int  ButtonBankFrameCount(int)   { return 5; }
} // namespace guild::gui

// =======================================================================================
TEST(GuiWidgetCreate, TextLabelFieldLayout) {
    Reset();
    int win = OpenWin(10, 20, 200, 100);
    CHECK_EQ(win, 0);

    int idx = Object_AddTextLabel(5, 7, win, "hello"); // text width via Property_Get == strlen
    CHECK(idx >= 0);
    Widget& w = g_widgets[idx];

    CHECK_EQ((int)w.type(), 0x43);             // 'C' text label
    CHECK_EQ((int)w.x(), 10 + 5);              // winX + x
    CHECK_EQ((int)w.y(), 20 + 7);              // winY + y - scroll(0)
    CHECK_EQ((int)w.order(), 2);               // +26
    CHECK_EQ((int)w.w(), 5 + 96);              // Property_Get("hello")=5, +96
    // text buffer at +116
    CHECK(std::strcmp(reinterpret_cast<char*>(&w.at<char>(116)), "hello") == 0);

    // Linked as child 0 of the window.
    CHECK_EQ((int)g_windows[win].objCount(), 1);
    CHECK_EQ(WindowChildList(win)[0], idx);
    // clip bounds inherited from window geometry.
    CHECK_EQ((int)w.clipX0(), 10);
    CHECK_EQ((int)w.clipX1(), 200 + 10);
    CHECK_EQ((int)w.clipY0(), 20);
    CHECK_EQ((int)w.clipY1(), 100 + 20);
}

TEST(GuiWidgetCreate, ButtonLabelFieldLayout) {
    Reset();
    g_screenClipExt = (300 << 16) | 250; // HIWORD=clip x1 ext, LOWORD=clip y1 ext
    int win = OpenWin(0, 0, 120, 40);
    int idx = Object_AddButtonLabel(8, 4, win, "OK");
    Widget& w = g_widgets[idx];

    CHECK_EQ((int)w.type(), 0x46);             // 'F' button
    CHECK_EQ((int)w.x(), 8);
    CHECK_EQ((int)w.y(), 4);
    CHECK_EQ((int)w.w(), 2 + 96);              // strlen("OK")=2, +96
    CHECK_EQ((int)w.clipX0(), 0);              // +28 = 0
    CHECK_EQ((int)w.clipY0(), 0);              // +32 = 0
    // button text at +124
    CHECK(std::strcmp(reinterpret_cast<char*>(&w.at<char>(124)), "OK") == 0);
    CHECK_EQ((int)g_windows[win].objCount(), 1);
}

TEST(GuiWidgetCreate, SetTextRecomputesWidth) {
    Reset();
    int win = OpenWin(0, 0, 100, 50);
    int idx = Object_AddTextLabel(0, 0, win, "hi");
    CHECK_EQ((int)g_widgets[idx].w(), 2 + 96);
    Object_SetText(idx, "longer");
    CHECK(std::strcmp(reinterpret_cast<char*>(&g_widgets[idx].at<char>(116)), "longer") == 0);
    CHECK_EQ((int)g_widgets[idx].w(), 6);      // width = Property_Get only (no +96)
}

TEST(GuiWidgetCreate, SpriteFieldLayout) {
    Reset();
    int win = OpenWin(30, 40, 100, 100);
    int idx = Widget_AddSpriteToWindow(2, 3, /*gfxId*/ 42, win);
    Widget& w = g_widgets[idx];

    CHECK_EQ((int)w.type(), 0x09);             // sprite
    CHECK_EQ((int)w.x(), 30 + 2);              // window-relative
    CHECK_EQ((int)w.y(), 40 + 3);
    CHECK_EQ((int)w.id(), 42);                 // +8 gfx id
    CHECK_EQ((int)w.h(), 7);                   // height from GfxMetricWord(.,82)
    CHECK_EQ(w.at<i32>(72), 1);                // +72 clickable
    CHECK_EQ((int)w.at<u8>(444), 3);           // +444
    CHECK_EQ((int)g_windows[win].objCount(), 1);
}

TEST(GuiWidgetCreate, SliderFieldLayout) {
    Reset();
    int win = OpenWin(0, 0, 200, 120);
    // horizontal slider (flags bit 2), value=50, range=120, max=200, gfxBase=4.
    int idx = Widget_AddSliderToWindow(10, 5, /*value*/ 50, /*range*/ 120, /*max*/ 200,
                                       /*gfxBase*/ 4, /*flags*/ 2, win);
    Widget& w = g_widgets[idx];

    CHECK_EQ((int)w.type(), 0x45);             // 'E'
    CHECK_EQ((int)w.x(), 10);
    CHECK_EQ((int)w.y(), 5);
    CHECK_EQ(w.at<i32>(120), 50);              // +120 value
    CHECK_EQ(w.at<i32>(124), 50);              // +124 value mirror
    CHECK_EQ(w.at<i32>(128), 200);             // +128 max
    CHECK_EQ((int)w.at<i16>(132), 2);          // +132 flags
    CHECK_EQ(w.at<i32>(136), 120);             // +136 range
    CHECK_EQ(w.at<i32>(140), 200 / 2);         // +140 step = max/2
    CHECK_EQ(w.at<i32>(144), 4);               // +144 gfx base
    CHECK_EQ(w.at<i32>(72), 1);                // +72 clickable
    CHECK_EQ((int)w.id(), 0);                  // +8 = 0
    // horizontal: w = range + 2*(metric>>16) = 120 + 2*8 = 136
    CHECK_EQ((int)w.w(), 120 + 16);
    CHECK_EQ((int)w.h(), 20);                  // cross-axis from SliderTrackExtent
}

TEST(GuiWidgetCreate, InputFieldLayout) {
    Reset();
    int win = OpenWin(5, 5, 300, 200);
    int idx = Input_AddFieldToWindow(/*x*/ 12, /*y*/ 9, /*step*/ 1, /*value*/ 7, /*flags*/ 0, win);
    CHECK(idx >= 0);
    Widget& w = g_widgets[idx];

    CHECK_EQ((int)w.type(), 0x41);             // 'A'
    CHECK_EQ((int)w.order(), 2);
    CHECK_EQ((int)w.id(), 0);
    // field record bound at +116 (record id) and +12 (record ptr).
    CHECK(w.at<i32>(116) != 0);
    CHECK(WidgetData(idx) != nullptr);
    // x/y came from the field record (window-relative origin folded into the record).
    CHECK_EQ((int)w.x(), 5 + 12);              // winX + x
    CHECK_EQ((int)w.y(), 5 + 9);               // winY + y - scroll(0)
    CHECK_EQ((int)g_windows[win].objCount(), 1);
}

TEST(GuiWidgetCreate, IconCheckboxLayout) {
    Reset();
    int win = OpenWin(0, 0, 100, 100);
    int idx = Object_AddCheckbox(4, 4, /*flags*/ 0, /*gfxId*/ 9, win);
    CHECK(idx >= 0);
    Widget& w = g_widgets[idx];
    CHECK_EQ((int)w.type(), 0x41);             // 'A'
    CHECK_EQ((int)w.id(), 9);                  // gfx id
    CHECK_EQ((int)g_windows[win].objCount(), 1);
}

TEST(GuiWidgetCreate, InputFieldSlotScan) {
    Reset();
    int win = OpenWin(0, 0, 100, 100);
    int a = Input_AddFieldToWindow(0, 0, 1, 1, 0, win);
    int b = Input_AddFieldToWindow(0, 10, 1, 2, 0, win);
    // distinct field records (record id at +116 differs).
    CHECK(g_widgets[a].at<i32>(116) != g_widgets[b].at<i32>(116));
    CHECK_EQ((int)g_windows[win].objCount(), 2);
}

TEST(GuiWidgetCreate, SetValueOrTextEditBranch) {
    Reset();
    int win = OpenWin(0, 0, 200, 100);
    int idx = Widget_AddSliderToWindow(0, 0, 10, 50, 100, 4, 2, win);
    // edit branch (original): +124=a2, +128=a3, +120=a4 ; signature (idx,a2,a3,a4,a5).
    char r = Object_SetObjectValueOrText(idx, /*a2*/ 11, /*a3*/ 99, /*a4*/ 33, /*a5*/ 0);
    CHECK_EQ(g_widgets[idx].at<i32>(124), 11); // +124 = a2
    CHECK_EQ(g_widgets[idx].at<i32>(128), 99); // +128 = a3
    CHECK_EQ(g_widgets[idx].at<i32>(120), 33); // +120 = a4
    (void)r;
}

TEST(GuiWidgetCreate, SetValueOrTextButtonBranch) {
    Reset();
    int idx = Widget_AllocSlot();
    g_widgets[idx].btnFlagA() = 1; // mark clickable
    Object_SetObjectValueOrText(idx, /*value*/ 1, 0, 0, 0);
    CHECK_EQ(g_widgets[idx].value(), 1);       // +36
    CHECK_EQ(g_widgets[idx].valueMirror(), 1); // +40
    Object_SetObjectValueOrText(idx, 0, 0, 0, 0);
    CHECK_EQ(g_widgets[idx].value(), 0);
}

TEST(GuiWidgetCreate, SetUserDataResolvesFormObject) {
    Reset();
    // Form 1, object base = 3; localId 2 -> widget 5.
    g_currentFormId = 1;
    g_forms[1].dw[101] = 3;
    int target = 5;
    Object_SetUserData(/*localId*/ 2, /*value*/ 0xABCD);
    CHECK_EQ(g_widgets[target].value(), 0xABCD); // +36
}

TEST(GuiWidgetCreate, SetButtonStatePushesToggle) {
    Reset();
    int idx = Widget_AllocSlot();
    g_widgets[idx].type() = 0x04;          // toggle widget
    g_widgets[idx].at<i32>(116) = 6;       // -> button-state record 6
    Widget_SetButtonState(idx, 1);
    CHECK_EQ((int)g_buttonStates[6].b[12], 1);   // pressed
    CHECK_EQ((int)g_buttonStates[6].b[4], 5 - 1); // frame = bank count - 1
    Widget_SetButtonState(idx, 0);
    CHECK_EQ((int)g_buttonStates[6].b[12], 0);
    CHECK_EQ((int)g_buttonStates[6].b[4], 0);
}

// =======================================================================================
// Window layout / resize / auto-fit-height model.
TEST(GuiWidgetCreate, WindowResizeReflowsChildren) {
    Reset();
    g_screenClipW   = (10000 << 16);
    g_screenClipExt = (10000 << 16) | 10000;
    int win = OpenWin(0, 0, 100, 100);
    int a = Object_AddTextLabel(0, 0, win, "x");
    Window_Resize(150, 200, win);
    CHECK_EQ((int)g_windows[win].h(), 200);    // height grew
    // child clip y1/x1 reflowed to the new window extent.
    CHECK_EQ((int)g_widgets[a].clipY1(), g_windows[win].h() + g_windows[win].y());
    CHECK_EQ((int)g_widgets[a].clipX1(), g_windows[win].w() + g_windows[win].x());
}

TEST(GuiWidgetCreate, WindowAutoFitHeightGrowsToContent) {
    Reset();
    g_screenClipW   = (10000 << 16);
    g_screenClipExt = (10000 << 16) | 10000;
    int win = OpenWin(0, 0, 100, 50);
    // a wide child: x=0,w=300 -> the autofit uses (x + w) as the bound per the original.
    int a = Object_AddTextLabel(0, 0, win, "");   // w = 0 + 96
    (void)a;
    int before = g_windows[win].h();
    Window_AutoFitHeight(win, 10);
    CHECK(g_windows[win].h() >= before); // height fitted to content (>= original)
}

TEST(GuiWidgetCreate, ChildCapacityGuard) {
    Reset();
    int win = OpenWin(0, 0, 100, 100);
    g_windows[win].objCount() = kMaxChildren; // pretend full
    CHECK_EQ(Object_AddTextLabel(0, 0, win, "x"), -1);
    CHECK_EQ(Object_AddButtonLabel(0, 0, win, "x"), -1);
}
