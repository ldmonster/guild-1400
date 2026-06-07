// Unit tests for the modal message-box family (src/gui/message_box.cpp).
//
// gilde.exe 0x4ad6f0/0x4acbd0/0x569a30/0x4ad9dc/0x4adc60 — VIBE_Dialog_ShowMessageBox*.
//
// Golden vectors are derived directly from the Hex-Rays decompiles:
//   * form-name selection by kind-flag priority (Autosize>Big>VeryBig>NonePerga>default);
//   * palette clamp (& 0xC7) and the 0x08 override to 464838;
//   * reentrancy-guard short-circuit (returns 0, no host calls);
//   * OK (1210 on this window) -> radio-group selection + 1; Cancel/Esc/right -> 0;
//   * Run/Modeless always return 0 even on OK (faithful: result local never written).

#include "test.h"

#include "gui/message_box.h"
#include "gui/window.h"
#include "gui/radiogroup.h"
#include "gui/object.h"
#include "gui/input.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A scripted mock host: records the form name it loaded, the palette/ttl seen, and feeds
// a queue of per-frame inputs to the modal loop.
struct MockHost : MessageBoxHost {
    // Recorded state.
    const char* loadedForm = nullptr;
    bool        selectedWindow1 = false;
    int         textRendered = -1;
    int         bodyRendered = -1;
    int         headerRendered = -1;
    bool        synced = false;
    bool        centered = false;
    int         raiseCount = 0;
    int         sliderInset = -1;
    bool        destroyed = false;
    i32         seenPalette = 0;
    unsigned    seenTtl = 0;

    int  formId = 7;
    int  sliderRet = 99;

    // Scripted frames.
    std::vector<FrameInput> frames;
    size_t frameIdx = 0;

    int LoadForm(const char* name, bool selWin1) override {
        loadedForm = name; selectedWindow1 = selWin1; return formId;
    }
    void RenderText(int, int t) override { textRendered = t; }
    void RenderBodyText(int, int t) override { bodyRendered = t; }
    void SyncWindowColors(int) override { synced = true; }
    void CenterWindows(int) override { centered = true; }
    void RaiseWindows(int) override { ++raiseCount; }
    int  AddSlider(int, int inset) override { sliderInset = inset; return sliderRet; }
    void SetSliderValue(int, int, int) override {}
    void DestroyForm(int) override { destroyed = true; }

    bool RunFrame(int, i32 palette, unsigned ttl, FrameInput& in) override {
        seenPalette = palette; seenTtl = ttl;
        if (frameIdx >= frames.size()) return false;
        in = frames[frameIdx++];
        return true;
    }
};

// Build a window with `nButtons` clickable child widgets (type != '@') plus the
// window-backing '@' widget, and make it the current window.  Returns the window slot.
int BuildButtonWindow(int nButtons) {
    ResetWindows();
    ResetRadioGroups();
    ResetInputState();

    int slot = Window_Create(0, 0, 200, 120, 0);  // allocates backing '@' widget + caches current
    // Add nButtons label/button children (type 'C' by default -> not '@').
    for (int i = 0; i < nButtons; ++i) {
        int w = Object_AddToWindow(slot, 10, 10, 0);
        if (w >= 0) {
            g_widgets[w].type() = kTypeLabel;   // 0x43 'C' : a non-'@' child
            g_widgets[w].btnFlagA() = 1;        // make it a clickable button
        }
    }
    return slot;
}

} // namespace

// ---------------------------------------------------------------------------
// Form-name selection priority (matches SelectMessageForm / dialog.cpp).
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, FormSelectionPriority) {
    BuildButtonWindow(2);
    {
        MockHost h; h.frames.clear();
        MessageBox_Show(h, kMsgFlagAutosize | kMsgFlagBig, 0, 1);
        CHECK(std::string(h.loadedForm) == "misc\\Messagebox_Autosize");
        CHECK(!h.selectedWindow1);
    }
    BuildButtonWindow(2);
    { MockHost h; MessageBox_Show(h, kMsgFlagBig, 0, 1);
      CHECK(std::string(h.loadedForm) == "misc\\Messagebox_BIG"); }
    BuildButtonWindow(2);
    { MockHost h; MessageBox_Show(h, kMsgFlagVeryBig, 0, 1);
      CHECK(std::string(h.loadedForm) == "misc\\Messagebox_VERY_BIG"); }
    BuildButtonWindow(2);
    { MockHost h; MessageBox_Show(h, kMsgFlagNonePerga, 0, 1);
      CHECK(std::string(h.loadedForm) == "misc\\Messagebox_NONE_PERGA");
      CHECK(h.selectedWindow1); }     // 0x100 path pre-selects window 1
    BuildButtonWindow(2);
    { MockHost h; MessageBox_Show(h, 0, 0, 1);
      CHECK(std::string(h.loadedForm) == "misc\\Messagebox"); }
}

// ---------------------------------------------------------------------------
// Palette: (snapshot & 0xC7), overridden to 464838 by flag 0x08.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, PaletteClampAndOverride) {
    BuildButtonWindow(1);
    g_msgPalette = 0xFF;            // 0xFF & 0xC7 == 0xC7
    { MockHost h; MessageBox_Show(h, 0, 0, 1); CHECK_EQ(h.seenPalette, (guild::i32)0xC7); }
    BuildButtonWindow(1);
    g_msgPalette = 0xFF;
    { MockHost h; MessageBox_Show(h, kMsgFlagPalette8, 0, 1);
      CHECK_EQ(h.seenPalette, (guild::i32)464838); }
    g_msgPalette = 0;
}

// ---------------------------------------------------------------------------
// Reentrancy guard: if the guard byte is already set, returns 0 and touches nothing.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, ReentrancyGuardBlocks) {
    BuildButtonWindow(1);
    g_msgGuardNormal = 1;
    MockHost h;
    int r = MessageBox_Show(h, 0, 0, 1);
    CHECK_EQ(r, 0);
    CHECK(h.loadedForm == nullptr); // no form loaded
    CHECK(!h.destroyed);
    g_msgGuardNormal = 0;           // restore
}

// ---------------------------------------------------------------------------
// OK on this window returns (group selection + 1).  We build 3 buttons; the group
// starts selected = 0 (Selection_Update(group,0)), so OK -> 0+1 == 1.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, OkReturnsSelectionPlusOne) {
    int slot = BuildButtonWindow(3);
    MockHost h;
    MessageBoxHost::FrameInput f{};
    f.clickedId = kIdOk;            // 1210
    f.clickedWindow = slot;        // == g_currentWindowId
    h.frames.push_back(f);
    int r = MessageBox_Show(h, 0, 0, 42);
    CHECK_EQ(r, 1);                // selected (0) + 1
    CHECK_EQ(h.textRendered, 42);
    CHECK(h.centered);
    CHECK(h.destroyed);
    CHECK_EQ(g_msgGuardNormal, (guild::u8)0); // guard cleared on exit
}

// OK on a DIFFERENT window id is ignored (the box keeps running until the queue drains).
TEST(GuiMessageBox, OkOnOtherWindowIgnored) {
    int slot = BuildButtonWindow(2);
    MockHost h;
    MessageBoxHost::FrameInput f{};
    f.clickedId = kIdOk; f.clickedWindow = slot + 50; // wrong window
    h.frames.push_back(f);         // one frame, then queue drains -> loop ends
    int r = MessageBox_Show(h, 0, 0, 1);
    CHECK_EQ(r, 0);                // never matched OK -> default 0
}

// ---------------------------------------------------------------------------
// Cancel / Esc / right-click -> 0.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, CancelReturnsZero) {
    int slot = BuildButtonWindow(2);
    { MockHost h; MessageBoxHost::FrameInput f{};
      f.clickedId = kIdCancel; f.clickedWindow = slot; h.frames.push_back(f);
      CHECK_EQ(MessageBox_Show(h, 0, 0, 1), 0); }
    BuildButtonWindow(2);
    { MockHost h; MessageBoxHost::FrameInput f{}; f.escKey = true; h.frames.push_back(f);
      CHECK_EQ(MessageBox_Show(h, 0, 0, 1), 0); }
    BuildButtonWindow(2);
    { MockHost h; MessageBoxHost::FrameInput f{}; f.rightClick = true; h.frames.push_back(f);
      CHECK_EQ(MessageBox_Show(h, 0, 0, 1), 0); }
}

// ---------------------------------------------------------------------------
// Raise flag (0x40) calls RaiseWindows each frame.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, RaiseFlagCallsRaisePerFrame) {
    int slot = BuildButtonWindow(1);
    MockHost h;
    MessageBoxHost::FrameInput idle{}; idle.clickedWindow = slot; // no click
    h.frames.push_back(idle);
    h.frames.push_back(idle);
    MessageBoxHost::FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot;
    h.frames.push_back(ok);
    MessageBox_Show(h, kMsgFlagRaise, 0, 1);
    CHECK_EQ(h.raiseCount, 3);     // raised on all three frames
}

// ---------------------------------------------------------------------------
// Slider flag (sign bit) adds a slider with inset 24 (normal) / 32 (modeless).
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, SliderInsetByVariant) {
    BuildButtonWindow(1);
    { MockHost h; MessageBox_Show(h, (guild::i16)0x8000, 0, 1); // sign bit set
      CHECK_EQ(h.sliderInset, kMsgSliderInsetY); }              // 24
    BuildButtonWindow(1);
    { MockHost h; MessageBox_ShowModeless(h, (char)0x80, 0, 1); // signed char < 0
      CHECK_EQ(h.sliderInset, kMsgSliderInsetYModeless); }      // 32
}

// ---------------------------------------------------------------------------
// Big uses its own guard; result tracking identical to normal.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, BigTracksResult) {
    int slot = BuildButtonWindow(2);
    MockHost h; MessageBoxHost::FrameInput f{}; f.clickedId = kIdOk; f.clickedWindow = slot;
    h.frames.push_back(f);
    CHECK_EQ(MessageBox_ShowBig(h, kMsgFlagBig, 0, 1), 1);
    CHECK_EQ(g_msgGuardBig, (guild::u8)0);
    CHECK(std::string(h.loadedForm) == "misc\\Messagebox_BIG");
}

// ---------------------------------------------------------------------------
// Run / Modeless ALWAYS return 0 even on OK (faithful quirk: result never written).
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, RunAlwaysZeroOnOk) {
    int slot = BuildButtonWindow(2);
    MockHost h; MessageBoxHost::FrameInput f{}; f.clickedId = kIdOk; f.clickedWindow = slot;
    h.frames.push_back(f);
    CHECK_EQ(MessageBox_Run(h, 0, 0, 1), 0);
    CHECK(h.destroyed);
}
TEST(GuiMessageBox, ModelessAlwaysZeroOnOkAndTtl1) {
    int slot = BuildButtonWindow(2);
    MockHost h; MessageBoxHost::FrameInput f{}; f.clickedId = kIdOk; f.clickedWindow = slot;
    h.frames.push_back(f);
    CHECK_EQ(MessageBox_ShowModeless(h, 0, 0, 1), 0);
    CHECK_EQ(h.seenTtl, 1u);       // modeless ttl is fixed at 1
}

// ---------------------------------------------------------------------------
// Green: renders header (window 0) + body (window 2), syncs colors, tracks result,
// and (unlike normal) does NOT gate OK on window id.
// ---------------------------------------------------------------------------
TEST(GuiMessageBox, GreenHeaderBodyAndResult) {
    int slot = BuildButtonWindow(2);
    MockHost h;
    MessageBoxHost::FrameInput f{}; f.clickedId = kIdOk; f.clickedWindow = slot + 99; // wrong window...
    h.frames.push_back(f);
    int r = MessageBox_ShowGreen(h, /*body=*/200, /*flags=*/0, /*group=*/0, /*header=*/100);
    CHECK_EQ(r, 1);                // ...still returns selection+1 (no window gate in Green)
    CHECK_EQ(h.textRendered, 100); // header
    CHECK_EQ(h.bodyRendered, 200); // body into window 2
    CHECK(h.synced);
    CHECK(std::string(h.loadedForm) == "misc\\MessageBoxGreen");
}

// Green with header == 0 skips the header render.
TEST(GuiMessageBox, GreenSkipsZeroHeader) {
    BuildButtonWindow(1);
    MockHost h;
    h.frames.clear(); // loop ends immediately -> cancel result 0
    int r = MessageBox_ShowGreen(h, 300, 0, 0, /*header=*/0);
    CHECK_EQ(r, 0);
    CHECK_EQ(h.textRendered, -1);  // header never rendered
    CHECK_EQ(h.bodyRendered, 300);
}
