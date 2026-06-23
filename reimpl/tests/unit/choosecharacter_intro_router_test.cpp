// gilde.exe 0x52e3d8 — VIBE_Menu_ChooseCharacterIntro (the spine router) golden tests.
// OK/Enter -> 1 (manual family tree -> dynasty scene); back(1155) -> 0 (automatic ->
// profession); window-close / ESC(1) -> -1 (abort).
#include "tests/framework/test.h"
#include "gui/choosecharacter_intro_run.h"
#include <vector>
using namespace guild;
using namespace guild::gui;

namespace {
struct FrameInput { bool windowClosed=false; int key=0; int button=0; int clicked=0; };
struct Mock : CharIntroRunHooks {
    int formId=5, base=300, group=7; std::vector<FrameInput> frames;
    int radioFrees=0, formDestroys=0;
    int  FormLoad(const char*) override { return formId; }
    void FormCenter(int) override {}
    int  RenderTitle(int) override { return base; }
    int  ChildObjectId(int, int o) override { return o; }
    int  RadioGroupCreate(int,int) override { return group; }
    void SelectionUpdate(int,int) override {}
    int  RunFrameLoop(int f) override { return (f < (int)frames.size()) ? 1 : 0; }
    void ReadInput(int) override {}
    bool WindowClosed(int f) override { return at(f).windowClosed; }
    int  KeyCode(int f) override { return at(f).key; }
    int  ButtonId(int f) override { return at(f).button; }
    int  ClickedWidgetId(int f) override { return at(f).clicked; }
    void RadioFree(int) override { ++radioFrees; }
    void FormDestroy(int) override { ++formDestroys; }
    FrameInput at(int f) const { return (f>=0&&f<(int)frames.size())?frames[f]:FrameInput{}; }
};
} // namespace

TEST(CharIntroRouter, OkReturnsManual) {
    Mock m; m.frames = { FrameInput{false,0,kCharIntroConfirmId,0} };
    auto* prev = Menu_SetCharIntroHooks(&m);
    CharIntroRecord rec; int r = Menu_RunChooseCharacterIntro(&rec, 8);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, kCharIntroChooseManual);   // 1
    CHECK(rec.confirmed);
    CHECK_EQ(m.radioFrees, 1); CHECK_EQ(m.formDestroys, 1);
}
TEST(CharIntroRouter, EnterReturnsManual) {
    Mock m; m.frames = { FrameInput{false,kCharIntroEnterKey,0,0} };
    auto* prev = Menu_SetCharIntroHooks(&m);
    CharIntroRecord rec; int r = Menu_RunChooseCharacterIntro(&rec, 8);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, kCharIntroChooseManual);
}
TEST(CharIntroRouter, BackReturnsAuto) {
    Mock m; m.frames = { FrameInput{false,0,kCharIntroBackId,0} };
    auto* prev = Menu_SetCharIntroHooks(&m);
    CharIntroRecord rec; int r = Menu_RunChooseCharacterIntro(&rec, 8);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, kCharIntroChooseAuto);     // 0
    CHECK(!rec.confirmed); CHECK(!rec.cancelled);
}
TEST(CharIntroRouter, WindowCloseReturnsAbort) {
    Mock m; m.frames = { FrameInput{true,0,0,0} };
    auto* prev = Menu_SetCharIntroHooks(&m);
    CharIntroRecord rec; int r = Menu_RunChooseCharacterIntro(&rec, 8);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, kCharIntroChooseBack);     // -1
    CHECK(rec.cancelled);
}
TEST(CharIntroRouter, DefaultNoInputIsAuto) {
    Mock m; m.frames = {};   // loop exits immediately (RunFrameLoop returns 0)
    auto* prev = Menu_SetCharIntroHooks(&m);
    CharIntroRecord rec; int r = Menu_RunChooseCharacterIntro(&rec, 8);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, kCharIntroChooseAuto);     // v2 inits 0
}
