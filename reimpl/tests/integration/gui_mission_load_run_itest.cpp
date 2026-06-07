// Integration tests: drive scripted click edges against the mission dialog widget ids and
// assert the EXACT global-flag mutations + the OK/confirm result, deterministically.
#include "test.h"

#include "gui/mission_load_run.h"

#include <cstring>
#include <string>

using namespace guild::gui;

namespace {

// A scripted dialog driver. Models the form object +36 "checked" state per row, plays a
// scripted hover/result per frame, and runs a fixed number of frames.
struct ScriptedDialog : MissionDialogHooks {
    int slot = 0;
    int checked[64] = {0};        // +36 per object slot
    int totalFrames = 0;
    int frame = 0;

    // Per-frame script:
    int  hoverSeq[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
    int  resultSeq[8];            // dword_75BF38
    bool closeSeq[8] = {false};
    int  scriptLen = 0;

    int enableEnabledTo = -99;    // last ObjectSetEnabled(v15, X)

    ScriptedDialog() { for (int i=0;i<8;++i) resultSeq[i] = -1; }

    int  FormGetChildObjectId(int) override { return slot++; }
    void SetRowChecked(int s, int v) override { if (s>=0&&s<64) checked[s]=v; }
    int  GetRowChecked(int s) override { return (s>=0&&s<64)?checked[s]:0; }
    void ObjectSetEnabled(int, int on) override { enableEnabledTo = on; }
    int  RunFrameLoop() override { return frame < totalFrames ? 1 : 0; }

    int  HoverId(int f) override { return (f>=0&&f<scriptLen)?hoverSeq[f]:-1; }
    int  DialogResult(int f) override { return (f>=0&&f<scriptLen)?resultSeq[f]:-1; }
    bool CloseRequested(int f) override { return (f>=0&&f<scriptLen)?closeSeq[f]:false; }
};

} // namespace

// Click the CONFIRM object (v15) with one row checked -> success (return 1) + latched
// selection + the implied menu-side flags.
TEST(MissionDialogIT, ConfirmWithSelection_ReturnsSuccessAndFlags) {
    ScriptedDialog d;
    // Seed row 2 as checked at build time.
    int seed[5] = {0,0,1,0,0};

    // We need the object slots assigned by FormGetChildObjectId: rows 0..4 -> slots 0..4,
    // enable(v15) -> 5, OK(v24) -> 6. Pre-seed the checked state for those row slots.
    // (SetRowChecked at build time copies seed[] into +36; ScriptedDialog records it.)
    d.totalFrames = 1;
    d.scriptLen = 1;
    d.hoverSeq[0] = 5;        // hover == v15 (the confirm/enable object)
    d.resultSeq[0] = 0;       // dword_75BF38 != -1 -> dispatch active

    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&d);
    MissionDialogResult res;
    int r = Menu_BuildChooseMissionDialog(seed, res, /*maxFrames*/ 4);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 1);                       // confirm with >=1 checked -> v25 = 1
    CHECK_EQ(res.selectedCount, 1);
    CHECK_EQ(res.selection[2], 1);        // row 2 latched
    CHECK_EQ(res.selection[0], 0);
    CHECK_EQ(res.dword_631614, 1);        // confirm path sets close flag

    // The menu-side contract implied by a nonzero return (@0x52a5c5).
    CHECK(res.impliesMissionFlags);
    CHECK_EQ(res.word_63C740, 137);
    CHECK_EQ(res.byte_63CC1D, 1);
}

// Click CONFIRM with NO rows checked -> stays open (return 0), no menu-side flags.
TEST(MissionDialogIT, ConfirmWithoutSelection_NoSuccess) {
    ScriptedDialog d;
    int seed[5] = {0,0,0,0,0};
    d.totalFrames = 1;
    d.scriptLen = 1;
    d.hoverSeq[0] = 5;        // v15
    d.resultSeq[0] = 0;

    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&d);
    MissionDialogResult res;
    int r = Menu_BuildChooseMissionDialog(seed, res, 4);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 0);                       // v20 == 0 -> v25 stays 0
    CHECK_EQ(res.selectedCount, 0);
    CHECK_EQ(res.dword_631614, 1);        // confirm path still set close flag
    CHECK(!res.impliesMissionFlags);      // but no success -> no 137/1/1 contract
}

// Click the OK button (v24) -> only sets close flag, does NOT return success.
TEST(MissionDialogIT, OkButton_SetsCloseOnly_NoSuccess) {
    ScriptedDialog d;
    int seed[5] = {0,0,1,0,0};       // even with a checked row...
    d.totalFrames = 1;
    d.scriptLen = 1;
    d.hoverSeq[0] = 6;       // hover == v24 (OK)
    d.resultSeq[0] = 0;

    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&d);
    MissionDialogResult res;
    int r = Menu_BuildChooseMissionDialog(seed, res, 4);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 0);                  // OK branch never sets v25
    CHECK_EQ(res.dword_631614, 1);  // but it does arm close
    CHECK(!res.impliesMissionFlags);
}

// Close request (ESC / dword_672230 / dword_75BF38==1155) arms dword_631614.
TEST(MissionDialogIT, CloseRequest_ArmsCloseFlag) {
    ScriptedDialog d;
    int seed[5] = {0,0,0,0,0};
    d.totalFrames = 1;
    d.scriptLen = 1;
    d.closeSeq[0] = true;    // dword_672230 || byte_67225C==1 || dword_75BF38==1155
    d.resultSeq[0] = -1;     // dispatch inactive

    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&d);
    MissionDialogResult res;
    int r = Menu_BuildChooseMissionDialog(seed, res, 4);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(res.dword_631614, 1);
    CHECK(!res.impliesMissionFlags);
}

// Determinism: identical script -> identical result twice.
TEST(MissionDialogIT, Deterministic) {
    auto run = [](MissionDialogResult& res) {
        ScriptedDialog d;
        int seed[5] = {1,0,1,0,0};
        d.totalFrames = 1; d.scriptLen = 1;
        d.hoverSeq[0] = 5; d.resultSeq[0] = 0;
        MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&d);
        int r = Menu_BuildChooseMissionDialog(seed, res, 4);
        Menu_SetMissionDialogHooks(prev);
        return r;
    };
    MissionDialogResult a, b;
    int ra = run(a), rb = run(b);
    CHECK_EQ(ra, rb);
    CHECK_EQ(a.selectedCount, b.selectedCount);
    CHECK_EQ(a.selection[0], b.selection[0]);
    CHECK_EQ(a.selection[2], b.selection[2]);
    CHECK_EQ(a.word_63C740, b.word_63C740);
}

// Map_LoadCityFile network-flag dispatch is deterministic and flag-faithful.
TEST(MissionDialogIT, LoadCity_FlagPicksExtension_Deterministic) {
    struct CapHooks : MapLoadCityHooks {
        char path[256] = {0};
        void SaveWriteGameFile(const char* p, const char*, int, int) override {
            std::snprintf(path, sizeof(path), "%s", p);
        }
    };
    for (int rep = 0; rep < 2; ++rep) {
        CapHooks c0, c1;
        MapLoadCityHooks* prev = Map_SetLoadCityHooks(&c0);
        Map_LoadCityFile(0, "Stadt", nullptr);
        Map_SetLoadCityHooks(&c1);
        Map_LoadCityFile(1, "Stadt", nullptr);
        Map_SetLoadCityHooks(prev);
        CHECK_EQ(std::string(c0.path), std::string("gamedata/cities/Stadt.CTY"));
        CHECK_EQ(std::string(c1.path), std::string("gamedata/cities/Stadt.NET"));
    }
}
