// End-to-end tests: a full scripted options-screen session asserting the build + dispatch
// + cleanup call ORDER (recorded via the run hooks), deterministically.  Also exercises
// the Sfx live volume-preview drag branch (gilde.exe 0x56cbc2).

#include "test.h"

#include "gui/options_run.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// Order-tracing hook: records a tag for every host call so the e2e can assert the exact
// preamble->build->loop->commit->destroy sequence.
struct OrderHooks : OptionsRunHooks {
    std::vector<std::string> calls;
    int totalFrames = 2;
    int clickFrame = 0;
    int target = 0;           // 0 OK, 1 Cancel
    int okId = 900, cancelId = 901;

    // Sfx preview script:
    bool dragOn = false;      // make DragActive true
    int  dragHoverWid = -1;   // which widget the drag hovers
    int  previewCalls = 0, sampleCalls = 0;

    int FormLoad(const char* n) override { calls.push_back(std::string("Load:") + n); return 7; }
    void CameraComputeWorldTarget() override { calls.push_back("Camera"); }
    void FormPositionChildWindows(int) override { calls.push_back("PosChildren"); }
    void WindowPositionAtCoord(int, int c) override { calls.push_back("PosAt:" + std::to_string(c)); }
    void FormSelectWindow(int, int w) override { calls.push_back("Select:" + std::to_string(w)); }
    void HudSyncWindowColors() override { calls.push_back("HudColors"); }
    void DragCursorSetSprite() override { calls.push_back("DragCursor"); }
    void TextRenderRichString(int t) override { calls.push_back("Title:" + std::to_string(t)); }
    int FormGetChildObjectId(int, int, int ci) override { calls.push_back("Child:" + std::to_string(ci)); return 100 + ci; }
    void ObjectSetValueOrText(int, int, int, int) override { calls.push_back("SetValue"); }
    void WidgetSetScrollLimit(int, int) override { calls.push_back("ScrollLim"); }
    void TextAppendWideLines(int, int, int) override { calls.push_back("Lines"); }
    void ObjectSetVisibleRecursive(int, int) override { calls.push_back("Hide"); }
    void HudBuildButtonRow(int row[2]) override { calls.push_back("ButtonRow"); row[0] = okId; row[1] = cancelId; }
    int FormDestroy(int) override { calls.push_back("Destroy"); return 0; }

    int RunFrameLoop(int f) override { return f < totalFrames ? 1 : 0; }
    bool ClickEdge(int f) override { return f == clickFrame; }
    int HoverId(int f) override {
        if (dragOn && f != clickFrame) return dragHoverWid;
        if (f != clickFrame) return -1;
        return target == 0 ? okId : cancelId;
    }
    bool DragActive(int f) override { return dragOn && f != clickFrame; }
    void AudioPreviewVolume(int, int) override { ++previewCalls; }
    void AudioStartVoiceSample() override { ++sampleCalls; }
    int ObjectGetDataPtr(int) override { return 0; }
};

bool HasSubsequence(const std::vector<std::string>& v, const std::vector<std::string>& sub) {
    size_t j = 0;
    for (const auto& s : v) { if (j < sub.size() && s == sub[j]) ++j; }
    return j == sub.size();
}

} // namespace

// ---------------------------------------------------------------------------
// Full Gfx session: assert the preamble + window-select sequence + cleanup order.
// ---------------------------------------------------------------------------
TEST(GuiOptRunE2E, GfxFullOrder) {
    OrderHooks h; h.target = 0;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);

    OptionsRunState st; st.gfxResCap1 = true;
    OptionsRunRecord rec;
    OptionsResult r = Menu_RunOptionsGfx(st, &rec, 4);

    CHECK(r.accepted);
    // Preamble: Load -> Camera -> PosChildren -> PosAt:2 -> Select:0 -> HudColors ->
    // DragCursor -> Select:2 -> Title -> Select:1.
    std::vector<std::string> pre = {
        "Load:menu\\options_gfx", "Camera", "PosChildren", "PosAt:2", "Select:0",
        "HudColors", "DragCursor", "Select:2", "Title:6247", "Select:1",
    };
    CHECK(HasSubsequence(h.calls, pre));
    // Button row then loop then Destroy, in that order.
    CHECK(HasSubsequence(h.calls, {"ButtonRow", "Destroy"}));
    CHECK(HasSubsequence(h.calls, {"Select:3", "ButtonRow"}));
    // 9 children built.
    CHECK(HasSubsequence(h.calls, {"Child:0", "Child:1", "Child:8"}));

    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Sfx live-preview drag branch: dragging a volume slider fires the preview; dragging the
// sfx slider also plays a test sample.
// ---------------------------------------------------------------------------
TEST(GuiOptRunE2E, SfxLivePreviewOnDrag) {
    OrderHooks h;
    h.totalFrames = 3;
    h.clickFrame = 2;     // click OK on the last frame
    h.target = 0;
    h.dragOn = true;
    h.dragHoverWid = 100 + 1; // child 1 == sfx slider (FormGetChildObjectId returns 100+ci)

    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    OptionsRunState st;
    OptionsResult r = Menu_RunOptionsSfx(st, nullptr, 5);

    CHECK(r.accepted);
    // Frames 0 and 1 are drag frames -> preview each; sfx slider -> sample each.
    CHECK_EQ(h.previewCalls, 2);
    CHECK_EQ(h.sampleCalls, 2);

    Menu_SetOptionsRunHooks(prev);
}

// Dragging a NON-volume target (the freq dropdown) does not fire preview.
TEST(GuiOptRunE2E, SfxNoPreviewOnDropdown) {
    OrderHooks h;
    h.totalFrames = 2; h.clickFrame = 1; h.target = 1; // cancel
    h.dragOn = true; h.dragHoverWid = 100 + 4; // child 4 freq dropdown
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    OptionsRunState st;
    Menu_RunOptionsSfx(st, nullptr, 4);
    CHECK_EQ(h.previewCalls, 0);
    CHECK_EQ(h.sampleCalls, 0);
    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Full Game session order + deterministic re-run.
// ---------------------------------------------------------------------------
TEST(GuiOptRunE2E, GameFullOrderDeterministic) {
    auto run = [](std::vector<std::string>& out) {
        OrderHooks h; h.target = 0;
        OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
        OptionsRunState st;
        Menu_RunOptionsGame(st, nullptr, 4);
        out = h.calls;
        Menu_SetOptionsRunHooks(prev);
    };
    std::vector<std::string> a, b;
    run(a); run(b);
    CHECK(a == b);
    CHECK(HasSubsequence(a, {"Load:menu\\options_game", "Title:6246"}));
    // The invert-mouse child (form child 4) is built then hidden.
    CHECK(HasSubsequence(a, {"Child:4", "ScrollLim", "Hide"}));
    CHECK(HasSubsequence(a, {"ButtonRow", "Destroy"}));
}
