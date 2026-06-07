// E2E test for gui::Menu_RunMainMenu @0x529d08 — a full scripted session:
//   fade in -> universe/scene loads -> form -> 8-button build -> New Game click
//   (EnterChooseCity returns true) -> session armed + close -> loop exits -> cleanup.
// Asserts the preamble + build + cleanup CALL ORDER (recorded via the trace) and that
// the run is deterministic (two identical runs produce the identical trace + state).
#include "test.h"

#include "gui/main_menu_run.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/radiogroup.h"
#include "gui/widget_create.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

void ResetAll() {
    ResetWidgets(); ResetWindows(); ResetWidgetCreate(); ResetRadioGroups();
}

// A full session: fade completes after 2 polls, CD music on, New Game clicked on frame 0
// with EnterChooseCity success, then the loop ends.
struct SessionHooks : MainMenuRunHooks {
    int fadePolls = 0;
    int newGameWidget = -1;
    bool chooseCityOK = true;

    // Fade completes after 2 RunFrameLoop spins (exercises the fade-in spin path).
    bool FadeComplete(void*) override { return fadePolls++ >= 2; }

    int  RandMod3() override { return 1; } // MauerUndTor
    int  AudioLoadTrack(const char*, int) override { return 777; } // a live track handle

    bool ClickEdge(int frame) override { return frame == 0; }
    int  HoverId(int frame) override { return frame == 0 ? newGameWidget : -1; }
    bool EnterChooseCity() override { return chooseCityOK; }

    int  RunFrameLoop() override { return 0; } // loop ends after frame 0
};

std::vector<std::string> RunOnce(MainMenuRunState& st, MainMenuRunRecord& rec,
                                 int newGameWidget) {
    ResetAll();
    SessionHooks h; h.newGameWidget = newGameWidget;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    Menu_RunMainMenu(st, &rec, /*maxFrames*/ 8);
    Menu_SetRunHooks(prev);
    std::vector<std::string> trace;
    for (int i = 0; i < rec.traceCount; ++i) trace.push_back(rec.trace[i]);
    return trace;
}

int idxOf(const std::vector<std::string>& t, const std::string& tag) {
    for (size_t i = 0; i < t.size(); ++i) if (t[i] == tag) return (int)i;
    return -1;
}

} // namespace

TEST(MainMenuRunE2E, FullSession_NewGame_PreambleBuildCleanupOrder) {
    // First learn the NewGame widget id.
    int newGameId;
    {
        ResetAll();
        SessionHooks h; h.newGameWidget = -2;
        MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
        MainMenuRunState st0; st0.cdMusic = 1;
        MainMenuRunRecord rec0;
        Menu_RunMainMenu(st0, &rec0, 8);
        Menu_SetRunHooks(prev);
        newGameId = rec0.idNewGame;
        CHECK(newGameId >= 0);
    }

    MainMenuRunState st; st.cdMusic = 1; st.missionMode = 0;
    MainMenuRunRecord rec;
    std::vector<std::string> t = RunOnce(st, rec, newGameId);

    // ---- session outcome ----
    CHECK(st.sessionFlags & kRunSessNewGame); // word_63C740 |= 1
    CHECK_EQ(st.close, 1);                     // dword_631614 = 1
    CHECK_EQ(st.quit, 0);
    CHECK_EQ(rec.cdTrackLoaded, 1);            // MauerUndTor
    CHECK(rec.buttonCount == 8);

    // ---- preamble order ----
    CHECK(idxOf(t, "PumpMessages") >= 0);
    CHECK(idxOf(t, "PumpMessages") < idxOf(t, "ScriptResetCurrentHandle"));
    CHECK(idxOf(t, "ScriptResetCurrentHandle") < idxOf(t, "FadeRegisterIn"));
    CHECK(idxOf(t, "FadeRegisterIn") < idxOf(t, "FadeInComplete"));
    CHECK(idxOf(t, "FadeInComplete") < idxOf(t, "Universe0"));
    CHECK(idxOf(t, "Universe0") < idxOf(t, "SceneChooseCity"));
    CHECK(idxOf(t, "SceneChooseCity") < idxOf(t, "Universe1"));
    CHECK(idxOf(t, "Universe1") < idxOf(t, "SceneSpieler"));
    CHECK(idxOf(t, "SceneSpieler") < idxOf(t, "SetupViewTransform"));
    CHECK(idxOf(t, "SetupViewTransform") < idxOf(t, "FormLoadMainMenu"));
    CHECK(idxOf(t, "FormLoadMainMenu") < idxOf(t, "FormPositionAndSelect"));
    CHECK(idxOf(t, "FormPositionAndSelect") < idxOf(t, "BuildBegin"));

    // ---- build order ----
    CHECK(idxOf(t, "BuildBegin") < idxOf(t, "BuildEnd"));
    CHECK(idxOf(t, "BuildEnd") < idxOf(t, "RadioGroupCreate"));
    CHECK(idxOf(t, "RadioGroupCreate") < idxOf(t, "CdTrackLoad"));
    CHECK(idxOf(t, "CdTrackLoad") < idxOf(t, "FadeRegisterOut"));
    CHECK(idxOf(t, "FadeRegisterOut") < idxOf(t, "Volume2000"));
    CHECK(idxOf(t, "Volume2000") < idxOf(t, "VersionLabel"));
    CHECK(idxOf(t, "VersionLabel") < idxOf(t, "LoopBegin"));

    // ---- dispatch + loop ----
    CHECK(idxOf(t, "LoopBegin") < idxOf(t, "EnterChooseCity:ok"));
    CHECK(idxOf(t, "EnterChooseCity:ok") < idxOf(t, "LoopEnd"));

    // ---- cleanup order ----
    CHECK(idxOf(t, "LoopEnd") < idxOf(t, "DestroyVersionLabel"));
    CHECK(idxOf(t, "DestroyVersionLabel") < idxOf(t, "RadioGroupFree"));
    CHECK(idxOf(t, "RadioGroupFree") < idxOf(t, "FormDestroy"));
    CHECK(idxOf(t, "FormDestroy") < idxOf(t, "CleanupUniverse0"));
    CHECK(idxOf(t, "CleanupUniverse0") < idxOf(t, "CleanupUniverse1"));
    CHECK(idxOf(t, "CleanupUniverse1") < idxOf(t, "Volume1000"));
    CHECK(idxOf(t, "Volume1000") < idxOf(t, "StopTrack")); // a track was loaded
    CHECK(idxOf(t, "StopTrack") < idxOf(t, "Volume5000"));
}

TEST(MainMenuRunE2E, Deterministic_TwoRunsIdentical) {
    int newGameId;
    {
        ResetAll();
        SessionHooks h; h.newGameWidget = -2;
        MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
        MainMenuRunState st0; st0.cdMusic = 1;
        MainMenuRunRecord rec0;
        Menu_RunMainMenu(st0, &rec0, 8);
        Menu_SetRunHooks(prev);
        newGameId = rec0.idNewGame;
    }

    MainMenuRunState a; a.cdMusic = 1;
    MainMenuRunState b; b.cdMusic = 1;
    MainMenuRunRecord ra, rb;
    std::vector<std::string> ta = RunOnce(a, ra, newGameId);
    std::vector<std::string> tb = RunOnce(b, rb, newGameId);

    CHECK_EQ(ta.size(), tb.size());
    bool same = (ta.size() == tb.size());
    for (size_t i = 0; same && i < ta.size(); ++i) same = same && (ta[i] == tb[i]);
    CHECK(same);

    // Identical state outcomes.
    CHECK_EQ(a.sessionFlags, b.sessionFlags);
    CHECK_EQ(a.close, b.close);
    CHECK_EQ(a.quit, b.quit);
    CHECK_EQ(ra.idNewGame, rb.idNewGame);
    CHECK_EQ(ra.frames, rb.frames);
}

TEST(MainMenuRunE2E, ReturnsFinalVolume5000) {
    ResetAll();
    SessionHooks h; h.newGameWidget = -2;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st;
    MainMenuRunRecord rec;
    int ret = Menu_RunMainMenu(st, &rec, 8);
    Menu_SetRunHooks(prev);
    CHECK_EQ(ret, 5000); // return VIBE_Audio_SetGlobalVolume(5000)
}
