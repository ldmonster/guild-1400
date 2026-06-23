// gilde.exe 0x52bcd4 — VIBE_Menu_RunChooseCharacter dynasty-scene run-loop golden tests.
// A scriptable mock feeds per-frame actor picks; the loop fills the six dynasty slots
// (male->even, female->odd) via gui/charcreate's ApplyActorClick, then chains
// Talent -> Profession -> Preview. Verifies completion, the chain branches, and cancel.
#include "tests/framework/test.h"
#include "gui/choosecharacter_run.h"
#include <vector>
using namespace guild;
using namespace guild::gui;

namespace {
struct Mock : CharSceneRunHooks {
    std::vector<ChooseCharActor> picks;       // per frame (kNone = no pick)
    std::vector<bool> windowClosed;
    bool talentOK=true, professionOK=true, previewOK=true;
    int setup=0, teardown=0; std::vector<int> placedSlots;
    void SceneSetup() override { ++setup; }
    void SceneTeardown() override { ++teardown; }
    int  RunFrameLoop(int) override { return 1; }   // engine: nonzero until close armed
    bool WindowClosed(int f) override { return f>=0 && f<(int)windowClosed.size() && windowClosed[f]; }
    ChooseCharActor PickActor(int f) override { return (f>=0&&f<(int)picks.size())?picks[f]:ChooseCharActor::kNone; }
    void PlayActorAnim(ChooseCharActor, int slot) override { placedSlots.push_back(slot); }
    bool ChooseCharacterTalent() override { return talentOK; }
    bool ChooseProfession() override { return professionOK; }
    bool BuildCharacterPreviewScene() override { return previewOK; }
};
// A male+female sequence that fills slots 0..5 in order.
const std::vector<ChooseCharActor> kFillSeq = {
    ChooseCharActor::kDiebMann,        // male  -> slot 0
    ChooseCharActor::kZigeunerinFrau,  // female-> slot 1
    ChooseCharActor::kHandwerkerMann,  // male  -> slot 2
    ChooseCharActor::kHandwerkerinFrau,// female-> slot 3
    ChooseCharActor::kOffizierMann,    // male  -> slot 4
    ChooseCharActor::kBuergerinFrau,   // female-> slot 5
};
} // namespace

TEST(ChooseCharacterRun, FillsDynastyThenStarts) {
    Mock m; m.picks = kFillSeq;
    auto* prev = Menu_SetChooseCharacterHooks(&m);
    ChooseCharacterState st; ChooseCharacterRecord rec;
    int r = Menu_RunChooseCharacter(st, &rec, 32);
    Menu_SetChooseCharacterHooks(prev);
    CHECK_EQ(r, 1);                       // talent+profession+preview ok -> start
    CHECK(rec.completed); CHECK(rec.started);
    CHECK_EQ(rec.actorsPlaced, 6);
    CHECK_EQ(m.setup, 1); CHECK_EQ(m.teardown, 1);
    // The six slots filled in order 0..5.
    for (int i = 0; i < 6; ++i) CHECK_EQ(m.placedSlots[i], i);
    // Slot 0 (Dieb, code 2) + slot 4 (Offizier, code 3) recorded.
    CHECK_EQ(st.dynasty.slot[0], 2);
    CHECK_EQ(st.dynasty.slot[4], 3);
}

TEST(ChooseCharacterRun, WrongParityClickIgnored) {
    Mock m;
    // First click a FEMALE while slot 0 wants a male -> ignored; then the proper sequence.
    m.picks = { ChooseCharActor::kZigeunerinFrau };
    m.picks.insert(m.picks.end(), kFillSeq.begin(), kFillSeq.end());
    auto* prev = Menu_SetChooseCharacterHooks(&m);
    ChooseCharacterState st; ChooseCharacterRecord rec;
    int r = Menu_RunChooseCharacter(st, &rec, 32);
    Menu_SetChooseCharacterHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(rec.actorsPlaced, 6);        // the mismatched click placed nothing
}

TEST(ChooseCharacterRun, TalentCancelAborts) {
    Mock m; m.picks = kFillSeq; m.talentOK = false;
    auto* prev = Menu_SetChooseCharacterHooks(&m);
    ChooseCharacterState st; ChooseCharacterRecord rec;
    int r = Menu_RunChooseCharacter(st, &rec, 32);
    Menu_SetChooseCharacterHooks(prev);
    CHECK_EQ(r, 0);
    CHECK(rec.completed); CHECK(rec.talentRan);
    CHECK(!rec.started);
}

TEST(ChooseCharacterRun, WindowCloseBeforeCompleteCancels) {
    Mock m; m.picks = { ChooseCharActor::kDiebMann };   // one fill, then close
    m.windowClosed = { false, true };
    auto* prev = Menu_SetChooseCharacterHooks(&m);
    ChooseCharacterState st; ChooseCharacterRecord rec;
    int r = Menu_RunChooseCharacter(st, &rec, 32);
    Menu_SetChooseCharacterHooks(prev);
    CHECK_EQ(r, 0);
    CHECK(!rec.completed); CHECK(rec.cancelled);
}

TEST(ChooseCharacterRun, ProfessionFailRetriesThenStarts) {
    // Profession returns false the first completion frame, true after -> the loop retries.
    struct M2 : Mock { int profCalls=0; bool ChooseProfession() override { return ++profCalls >= 2; } };
    M2 m; m.picks = kFillSeq;
    auto* prev = Menu_SetChooseCharacterHooks(&m);
    ChooseCharacterState st; ChooseCharacterRecord rec;
    int r = Menu_RunChooseCharacter(st, &rec, 32);
    Menu_SetChooseCharacterHooks(prev);
    CHECK_EQ(r, 1);                 // retried profession, then started
    CHECK(m.profCalls >= 2);
}
