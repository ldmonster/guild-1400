// gilde.exe 0x52d684 — VIBE_Menu_RunChooseHistory golden control-flow tests.
// A scriptable mock drives the reconstruction headless and asserts the 1:1 behaviour:
// the seed maps mode->radio index, a mode pick maps to the History flag (1/2/0), the
// dialog cancel stays on the screen, a confirmed dialog runs the character spine and
// returns its v8, and the back button / window-close cancel.
#include "tests/framework/test.h"
#include "gui/choosehistory_run.h"
#include <vector>
using namespace guild;
using namespace guild::gui;

namespace {
struct FrameInput { bool windowClosed=false; int key=0; int button=0; int clicked=0; };

struct Mock : CharHistoryRunHooks {
    int formId=11, base=200, group=21;
    std::vector<FrameInput> frames;
    int dialogResult = kHistoryDialogCancel;   // default: dialog cancelled
    int spineResult  = 0;                       // v8 returned by the spine
    // observations
    int lastSeedSel=-99, lastFlagSet=-99, lastReselect=-99;
    int dialogCalls=0, spineCalls=0, radioFrees=0, formDestroys=0;
    std::vector<int> selectionUpdates;

    int  FormLoad(const char*) override { return formId; }
    void FormCenter(int) override {}
    void FormSelectWindow(int,int) override {}
    int  RenderTitle(int) override { return base; }
    int  ChildObjectId(int, int objIndex) override { return objIndex; }   // ids = base+k
    int  RadioGroupCreate(int,int) override { return group; }
    void SelectionUpdate(int, int v) override { selectionUpdates.push_back(v);
        if (lastSeedSel==-99) lastSeedSel=v; else lastReselect=v; }
    int  RunFrameLoop(int f) override { return (f < (int)frames.size()) ? 1 : 0; }
    void ReadInput(int) override {}
    bool WindowClosed(int f) override { return at(f).windowClosed; }
    int  KeyCode(int f) override { return at(f).key; }
    int  ButtonId(int f) override { return at(f).button; }
    int  ClickedWidgetId(int f) override { return at(f).clicked; }
    void SetHistoryFlag(int flag) override { lastFlagSet=flag; }
    void FormSetVisible(int,int) override {}
    int  RunHistoryDialog() override { ++dialogCalls; return dialogResult; }
    int  RunCharacterSpine() override { ++spineCalls; return spineResult; }
    void RadioFree(int) override { ++radioFrees; }
    void FormDestroy(int) override { ++formDestroys; }

    FrameInput at(int f) const { return (f>=0 && f<(int)frames.size()) ? frames[f] : FrameInput{}; }
};
} // namespace

// Seed: stored mode 0/1/2 -> radio index 2/0/1 (the first Selection_Update).
TEST(ChooseHistoryRun, SeedMapsModeToIndex) {
    const int expect[3] = {2, 0, 1};
    for (int mode = 0; mode < 3; ++mode) {
        Mock m; m.frames = { FrameInput{true,0,0,0} };  // close immediately after seed
        auto* prev = Menu_SetChooseHistoryHooks(&m);
        ChooseHistoryState st; st.historyMode = mode;
        ChooseHistoryRecord rec;
        Menu_RunChooseHistory(st, &rec, 8);
        Menu_SetChooseHistoryHooks(prev);
        CHECK_EQ(m.lastSeedSel, expect[mode]);
        CHECK(rec.cancelled);
        CHECK_EQ(m.radioFrees, 1); CHECK_EQ(m.formDestroys, 1);
    }
}

// Picking mode member k (id=base+k) with OK maps to History flag {1,2,0} and re-selects.
TEST(ChooseHistoryRun, ModePickSetsHistoryFlag) {
    const int flagFor[3] = {1, 2, 0};
    const int reselFor[3] = {0, 1, 2};   // ChooseHistory_FlagToIndex(flag)
    for (int k = 0; k < 3; ++k) {
        Mock m; m.dialogResult = 0; m.spineResult = 1;   // dialog confirms, spine starts
        m.frames = { FrameInput{false, 0, kChooseHistoryConfirmId, m.base + k} };
        auto* prev = Menu_SetChooseHistoryHooks(&m);
        ChooseHistoryState st; st.historyMode = 0;
        ChooseHistoryRecord rec;
        int r = Menu_RunChooseHistory(st, &rec, 8);
        Menu_SetChooseHistoryHooks(prev);
        CHECK_EQ(m.lastFlagSet, flagFor[k]);
        CHECK_EQ(st.historyFlag, flagFor[k]);
        CHECK_EQ(m.lastReselect, reselFor[k]);
        CHECK_EQ(r, 1);                  // spine started
        CHECK(rec.committedMode); CHECK(rec.dialogRan); CHECK(rec.spineRan);
    }
}

// A cancelled history dialog (0xFF) does NOT run the spine and stays on the screen.
TEST(ChooseHistoryRun, DialogCancelStaysNoSpine) {
    Mock m; m.dialogResult = kHistoryDialogCancel;
    // frame 0: pick mode 0 (dialog cancels -> stay); frame 1: back.
    m.frames = { FrameInput{false, 0, kChooseHistoryConfirmId, m.base + 0},
                 FrameInput{false, 0, kChooseHistoryBackId, 0} };
    auto* prev = Menu_SetChooseHistoryHooks(&m);
    ChooseHistoryState st;
    ChooseHistoryRecord rec;
    int r = Menu_RunChooseHistory(st, &rec, 8);
    Menu_SetChooseHistoryHooks(prev);
    CHECK_EQ(m.dialogCalls, 1);
    CHECK_EQ(m.spineCalls, 0);    // dialog cancelled -> spine never ran
    CHECK_EQ(r, 0);
    CHECK(rec.committedMode);     // the flag was still set (History_SetActiveFlag)
    CHECK(!rec.spineRan);
}

// Dialog confirmed but the character spine aborts (v8 == 0) -> return 0.
TEST(ChooseHistoryRun, SpineAbortReturnsZero) {
    Mock m; m.dialogResult = 0; m.spineResult = 0;   // dialog ok, spine aborts
    m.frames = { FrameInput{false, 0, kChooseHistoryConfirmId, m.base + 1} };
    auto* prev = Menu_SetChooseHistoryHooks(&m);
    ChooseHistoryState st;
    ChooseHistoryRecord rec;
    int r = Menu_RunChooseHistory(st, &rec, 8);
    Menu_SetChooseHistoryHooks(prev);
    CHECK_EQ(r, 0);
    CHECK(rec.spineRan);
}

// The back button (1155) cancels with no commit.
TEST(ChooseHistoryRun, BackCancels) {
    Mock m; m.frames = { FrameInput{false, 0, kChooseHistoryBackId, 0} };
    auto* prev = Menu_SetChooseHistoryHooks(&m);
    ChooseHistoryState st;
    ChooseHistoryRecord rec;
    int r = Menu_RunChooseHistory(st, &rec, 8);
    Menu_SetChooseHistoryHooks(prev);
    CHECK_EQ(r, 0);
    CHECK(rec.cancelled);
    CHECK(!rec.committedMode);
    CHECK_EQ(m.dialogCalls, 0);
}
