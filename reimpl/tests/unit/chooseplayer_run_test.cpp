// gilde.exe 0x52ccd8 — VIBE_Menu_RunChoosePlayer page-wizard golden tests.
// A scriptable mock drives the page state machine headless: advancing through the six
// pages collects firstName/familyName/gender/faith/wappen and auto-commits on page 5;
// back steps a page and exits at page 0; the [Network] INI is seeded + written.
#include "tests/framework/test.h"
#include "gui/chooseplayer_run.h"
#include <string>
#include <vector>
using namespace guild;
using namespace guild::gui;

namespace {
struct Mock : ChoosePlayerRunHooks {
    std::vector<PlayerPageAction> actions;   // per frame
    std::string vorname="Hans", nachname="Fugger";
    int gender=1, faith=0, wappen=5;
    bool iniRead=false, iniWrote=false; ChoosePlayerState wrote;
    int formDestroys=0;

    int  FormLoad(const char*) override { return 3; }
    void FormDestroy(int) override { ++formDestroys; }
    void ReadIniDefaults(ChoosePlayerState& st) override { iniRead=true; st.firstName="SEED"; }
    void WriteIni(const ChoosePlayerState& st) override { iniWrote=true; wrote=st; }
    int  RunFrameLoop(int) override { return 1; }   // engine: nonzero until close armed
    PlayerPageAction PageAction(int, int f) override {
        return (f>=0 && f<(int)actions.size()) ? actions[f] : PlayerPageAction::kNone; }
    std::string GetText(int page) override { return page==0 ? vorname : nachname; }
    int GetChoice(int page) override { return page==2 ? gender : page==3 ? faith : wappen; }
};
constexpr auto ADV = PlayerPageAction::kAdvance;
constexpr auto BACK = PlayerPageAction::kBack;
constexpr auto NONE = PlayerPageAction::kNone;
} // namespace

// Advancing through all five value pages collects each value and auto-commits on page 5.
TEST(ChoosePlayerRun, FullWalkthroughCommits) {
    Mock m; m.actions = { ADV, ADV, ADV, ADV, ADV };   // pages 0..4 advance; page 5 auto-commits
    auto* prev = Menu_SetChoosePlayerHooks(&m);
    ChoosePlayerState st; ChoosePlayerRecord rec;
    int r = Menu_RunChoosePlayer(st, &rec, 16);
    Menu_SetChoosePlayerHooks(prev);
    CHECK_EQ(r, 1);
    CHECK(m.iniRead); CHECK(m.iniWrote); CHECK_EQ(m.formDestroys, 1);
    CHECK_EQ(st.firstName, std::string("Hans"));
    CHECK_EQ(st.familyName, std::string("Fugger"));
    CHECK_EQ(st.gender, 1);
    CHECK_EQ(st.faith, 0);
    CHECK_EQ(st.wappenIndex, 5);
    CHECK(rec.committed);
    CHECK_EQ(rec.maxPageReached, 5);
}

// Back from page 0 exits with result 0 (cancel), no commit.
TEST(ChoosePlayerRun, BackFromPageZeroCancels) {
    Mock m; m.actions = { BACK };
    auto* prev = Menu_SetChoosePlayerHooks(&m);
    ChoosePlayerState st; ChoosePlayerRecord rec;
    int r = Menu_RunChoosePlayer(st, &rec, 16);
    Menu_SetChoosePlayerHooks(prev);
    CHECK_EQ(r, 0);
    CHECK(rec.cancelled);
    CHECK_EQ(st.page, 0);
}

// Back steps one page at a time: advance twice (page 2), back once (page 1), then idle.
TEST(ChoosePlayerRun, BackStepsOnePage) {
    Mock m; m.actions = { ADV, ADV, BACK, NONE, NONE };  // 0->1->2->(back)1, never commits
    auto* prev = Menu_SetChoosePlayerHooks(&m);
    ChoosePlayerState st; ChoosePlayerRecord rec;
    int r = Menu_RunChoosePlayer(st, &rec, 8);
    Menu_SetChoosePlayerHooks(prev);
    CHECK_EQ(r, 0);                 // never reached page 5
    CHECK_EQ(st.page, 1);           // 0->1->2->1
    CHECK_EQ(rec.maxPageReached, 2);
}

// The INI seed is read before the wizard and the final state is written back.
TEST(ChoosePlayerRun, IniSeededAndWritten) {
    Mock m; m.actions = { ADV, ADV, ADV, ADV, ADV };
    auto* prev = Menu_SetChoosePlayerHooks(&m);
    ChoosePlayerState st; ChoosePlayerRecord rec;
    Menu_RunChoosePlayer(st, &rec, 16);
    Menu_SetChoosePlayerHooks(prev);
    CHECK(m.iniWrote);
    CHECK_EQ(m.wrote.firstName, std::string("Hans"));   // collected value, not the seed
    CHECK_EQ(m.wrote.wappenIndex, 5);
}
