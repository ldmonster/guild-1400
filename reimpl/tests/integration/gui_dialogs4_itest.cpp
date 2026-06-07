// Integration test: gui_dialogs4's Widget_AddPersonRow composed against the REAL
// reconstructed siblings it wires in the live process —
//   * world::MoneyFormatWithSeparators  (src/world/money_format.cpp)
//   * gui::Object_AddTextLabel / Object_AddToWindow / Widget_AllocSlot
//     (src/gui/widget_create.cpp, window.cpp, object.cpp)
//   * gui::Widget_LayoutBounds via the LayoutBounds bridge (src/gui/window_render.cpp)
// — NOT mocks.  Only the un-reconstructed market-price lookup is forwarded through a
// hook, exactly as the live wiring forwards VIBE_Building_LookupCachedMarketPrice.
#include "test.h"

#include "gui/gui_dialogs4.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/widget_create.h"
#include "world/money_format.h"

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {
// The only un-reconstructed leaf: the cached market price. In the live process this
// is VIBE_Building_LookupCachedMarketPrice; here we forward a fixed price so the rest
// of the row build — including the REAL money formatter and REAL label builder — runs
// end to end. We assert the value threaded all the way into the label text.
double FixedPrice(int itemType, unsigned char rate) {
    (void)itemType; (void)rate;
    return 12345.0; // -> "12.345" + currency glyph through the real formatter
}
}

TEST(GuiDialogs4Itest, AddPersonRowFormatsPriceThroughRealMoneyAndLabel) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.lookupCachedMarketPrice = &FixedPrice; // the lone deferred leaf
    SetGuiDialogs4Hooks(&h);

    // Prepare a real, enabled window with a real child id list (the model the
    // sibling Object_AddToWindow / Object_AddTextLabel mutate).
    const int winSlot = 4;
    g_currentWindowId = winSlot;
    g_windows[winSlot] = Window{};
    Window& win = g_windows[winSlot];
    win.enabled() = 1;                 // WindowReady() gate
    win.x() = 10; win.y() = 20; win.w() = 100; win.h() = 200;
    win.objCount() = 0;
    std::memset(WindowChildList(winSlot), 0, sizeof(i32) * 64);
    // Give the window a real backing widget so Object_AddToWindow's parent-clip
    // inheritance reads a valid slot.
    win.backWidget() = 0;
    g_widgets[0] = Widget{};

    // Build the row: gfx id 7, rate 1.
    int midSlot = Widget_AddPersonRow(/*x=*/5, /*gfxId=*/7, /*y2=*/30, winSlot, /*rate=*/1);

    // The middle gfx cell is clickable (+72 == 1) — set by AddPersonRow.
    CHECK(midSlot >= 0);
    if (midSlot >= 0) {
        CHECK_EQ(g_widgets[midSlot].at<i32>(72), 1);
    }

    // The window grew by three gfx cells + one text label = 4 children, all real
    // slots produced by the sibling allocators.
    CHECK_EQ((int)win.objCount(), 4);

    // The 4th child is the money label. Its +116 text region holds the EXACT string
    // the REAL world::MoneyFormatWithSeparators produced for 12345 @ rate 1.
    int labelSlot = WindowChildList(winSlot)[3];
    std::string expected = world::MoneyFormatWithSeparators(12345, 1);
    CHECK(expected.size() >= 6); // "12.345" + glyph
    const char* labelText = reinterpret_cast<const char*>(&g_widgets[labelSlot].at<char>(116));
    CHECK_EQ(std::strcmp(labelText, expected.c_str()), 0);

    // AddPersonRow overrides the label width to 36 and type byte at +112 to 'C'.
    CHECK_EQ((int)g_widgets[labelSlot].w(), 36);
    CHECK_EQ((int)g_widgets[labelSlot].at<i16>(112), 67);
    CHECK_EQ(g_widgets[labelSlot].at<i32>(92), 1);

    SetGuiDialogs4Hooks(nullptr);
}

TEST(GuiDialogs4Itest, LayoutBoundsBridgeForwardsToRealSibling) {
    // The LayoutBounds bridge calls the REAL gui::Widget_LayoutBounds symbol (the
    // shared renderer edge defined in src/gui/window_render.cpp — currently an inert
    // weak stub for the deferred render cluster). This proves the cross-module link
    // resolves against the genuine sibling (not a local mock) and that the bridge
    // threads the widget id back, exactly as VIBE_Widget_SetFocus's caret path uses it.
    ResetGuiDialogs4();
    const int slot = 8;
    g_widgets[slot] = Widget{};
    int rv = Widget_LayoutBoundsBridge(/*x=*/55, /*y=*/66, slot);
    CHECK_EQ(rv, slot); // bridge returns the widget id through the real-sibling call
}

TEST(GuiDialogs4Itest, MoneyFormatterRoundTripsThroughRow) {
    // Cross-check several amounts: AddPersonRow must place exactly what the REAL
    // money formatter yields, for the same amount/rate, into the label buffer.
    struct Case { int amount; unsigned char rate; };
    const Case cases[] = {{0, 1}, {999, 1}, {1000, 1}, {1234567, 1}};
    for (const Case& c : cases) {
        ResetGuiDialogs4();
        GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
        double price = c.amount;
        // capture-less lambda -> function pointer
        static double s_price; s_price = price;
        h.lookupCachedMarketPrice = [](int, unsigned char) -> double { return s_price; };
        SetGuiDialogs4Hooks(&h);

        const int winSlot = 6;
        g_currentWindowId = winSlot;
        g_windows[winSlot] = Window{};
        Window& win = g_windows[winSlot];
        win.enabled() = 1; win.x() = 0; win.y() = 0; win.w() = 80; win.h() = 80;
        win.objCount() = 0; win.backWidget() = 0;
        std::memset(WindowChildList(winSlot), 0, sizeof(i32) * 64);
        g_widgets[0] = Widget{};

        Widget_AddPersonRow(0, c.amount % 1000, 0, winSlot, c.rate);
        int labelSlot = WindowChildList(winSlot)[3];
        std::string expected = world::MoneyFormatWithSeparators(c.amount, c.rate);
        const char* got = reinterpret_cast<const char*>(&g_widgets[labelSlot].at<char>(116));
        CHECK_EQ(std::strcmp(got, expected.c_str()), 0);
        SetGuiDialogs4Hooks(nullptr);
    }
}
