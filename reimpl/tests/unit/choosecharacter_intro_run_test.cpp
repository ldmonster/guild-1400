// gilde.exe 0x52e4e0 — VIBE_Menu_ChooseCharacterIntroVariant golden control-flow tests.
// A scriptable mock CharIntroRunHooks drives the reconstructed function headless and we
// assert the exact 1:1 behaviour: a radio commit (OK/Enter) returns 1 and persists the
// 0..4 variant; the back button / window-close return 0 and leave the seed unchanged.
#include "tests/framework/test.h"
#include "gui/choosecharacter_intro_run.h"
#include <vector>
using namespace guild;
using namespace guild::gui;

namespace {
struct FrameInput { bool windowClosed=false; int key=0; int button=0; int clicked=0; };

struct MockHooks : CharIntroRunHooks {
    int formId = 7, base = 100, group = 9;
    std::vector<FrameInput> frames;
    int formLoads=0, radioFrees=0, formDestroys=0, lastSelectionValue=-99;

    int  FormLoad(const char*) override { ++formLoads; return formId; }
    void FormCenter(int) override {}
    int  RenderTitle(int) override { return base; }
    int  ChildObjectId(int, int objIndex) override { return objIndex; }      // ids = base+k
    int  RadioGroupCreate(int, int) override { return group; }
    void SelectionUpdate(int, int value) override { lastSelectionValue = value; }
    int  RunFrameLoop(int frame) override { return (frame < (int)frames.size()) ? 1 : 0; }
    void ReadInput(int) override {}
    bool WindowClosed(int f) override { return at(f).windowClosed; }
    int  KeyCode(int f) override { return at(f).key; }
    int  ButtonId(int f) override { return at(f).button; }
    int  ClickedWidgetId(int f) override { return at(f).clicked; }
    void RadioFree(int) override { ++radioFrees; }
    void FormDestroy(int) override { ++formDestroys; }

    FrameInput at(int f) const { return (f>=0 && f<(int)frames.size()) ? frames[f] : FrameInput{}; }
};
} // namespace

// Clicking radio member k (id = base+k) + OK button 1210 commits variant k and returns 1.
TEST(CharIntroRun, OkCommitsClickedVariant) {
    for (int k = 0; k < kCharIntroVariantCount; ++k) {
        MockHooks m;
        // frame 0: idle; frame 1: OK pressed on radio k.
        m.frames = { {}, FrameInput{false, 0, kCharIntroConfirmId, m.base + k} };
        CharIntroRunHooks* prev = Menu_SetCharIntroHooks(&m);
        CharIntroState st; st.introVariant = 0;
        CharIntroRecord rec;
        int r = Menu_RunChooseCharacterIntroVariant(st, &rec, 16);
        Menu_SetCharIntroHooks(prev);
        CHECK_EQ(r, 1);
        CHECK_EQ(st.introVariant, k);
        CHECK(rec.confirmed);
        CHECK_EQ(rec.chosenVariant, k);
        CHECK_EQ(m.formLoads, 1);
        CHECK_EQ(m.radioFrees, 1);
        CHECK_EQ(m.formDestroys, 1);
    }
}

// The seed selection is pre-applied to the radio group and round-trips on cancel.
TEST(CharIntroRun, SeedPreselectedAndRoundTrips) {
    MockHooks m;
    m.frames = { {}, {}, FrameInput{false, 0, kCharIntroBackId, 0} };  // back button on frame 2
    CharIntroRunHooks* prev = Menu_SetCharIntroHooks(&m);
    CharIntroState st; st.introVariant = 3;     // seed
    CharIntroRecord rec;
    int r = Menu_RunChooseCharacterIntroVariant(st, &rec, 16);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(m.lastSelectionValue, 3);          // Selection_Update(group, seed)
    CHECK_EQ(r, 0);                             // back -> cancel
    CHECK_EQ(st.introVariant, 3);               // unchanged
    CHECK(rec.cancelled);
    CHECK(!rec.confirmed);
}

// Window close (dword_672230) ends the loop with return 0 and no commit.
TEST(CharIntroRun, WindowCloseCancels) {
    MockHooks m;
    m.frames = { {}, FrameInput{true, 0, 0, 0} };   // window closed on frame 1
    CharIntroRunHooks* prev = Menu_SetCharIntroHooks(&m);
    CharIntroState st; st.introVariant = 2;
    CharIntroRecord rec;
    int r = Menu_RunChooseCharacterIntroVariant(st, &rec, 16);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, 0);
    CHECK_EQ(st.introVariant, 2);
    CHECK(rec.cancelled);
}

// Enter key (byte_67225C == 28) on a radio commits exactly like the OK button.
TEST(CharIntroRun, EnterKeyCommits) {
    MockHooks m;
    m.frames = { FrameInput{false, kCharIntroEnterKey, 0, m.base + 4} };  // Enter on radio 4
    CharIntroRunHooks* prev = Menu_SetCharIntroHooks(&m);
    CharIntroState st; st.introVariant = 0;
    CharIntroRecord rec;
    int r = Menu_RunChooseCharacterIntroVariant(st, &rec, 16);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(st.introVariant, 4);
    CHECK(rec.confirmed);
}

// OK with NO radio active (clicked id not a member) STILL commits — the disasm's commit
// path (0x52e6c7 cmp esi,id4; jnz loc_52E64C) reaches loc_52E64C even when nothing matched,
// so eax=1 / dword_631614=1 fire unconditionally and the current selection (the seed, since
// no member was active this frame) is carried through to byte_12335BA. Only the radio VALUE
// is gated on a match; the commit itself is not.
TEST(CharIntroRun, OkWithoutSelectionStillCommitsSeed) {
    MockHooks m;
    m.frames = { FrameInput{false, 0, kCharIntroConfirmId, 999} };  // OK but clicked id 999
    CharIntroRunHooks* prev = Menu_SetCharIntroHooks(&m);
    CharIntroState st; st.introVariant = 1;
    CharIntroRecord rec;
    int r = Menu_RunChooseCharacterIntroVariant(st, &rec, 4);
    Menu_SetCharIntroHooks(prev);
    CHECK_EQ(r, 1);                 // OK always commits (loc_52E64C unconditional)
    CHECK_EQ(st.introVariant, 1);   // unchanged (no member matched -> dword_63C744 kept seed)
    CHECK(rec.confirmed);
    CHECK_EQ(rec.chosenVariant, 1); // carries the seed
}
