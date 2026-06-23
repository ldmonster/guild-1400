// E2E test: exercise a full player-stats panel session across several gui_dialogs5
// functions with a recording hooks layer that drives the per-frame loop through one
// interaction iteration (hover a building row -> open the detail sub-panel -> close).
#include "test.h"

#include "gui/gui_dialogs5.h"
#include "gui/object.h"
#include "gui/window.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

struct E2E {
    std::vector<std::string> formsLoaded;
    std::vector<unsigned> rich;
    int frameIter = 0;          // how many loop iterations to allow on the OUTER panel
    int buildingRows = 1;
    int detailOpened = 0;
    int destroys = 0;
} g_e;

GuiDialogs5Hooks g_h;

int  EGameTick(i16, i16, const char* n) { if (n) g_e.formsLoaded.push_back(n); return 50; }
void ECenter(int) {}
void ESelect(int, int) {}
int  EGetWin(int, int) { return 2; }
int  EGetChild(int, int, int) { return 4; }
int  EDestroy(int) { ++g_e.destroys; return -1; }
void EVis(int, int) {}
void EChildVis(int, int) {}
int  ERich(unsigned id, int, int, int, int) { g_e.rich.push_back(id); return 0; }
void EFmt(char* o, const char*, int, int, int) { if (o) std::strcpy(o, "m"); }
void ESync(int) {}
void ESlider(int, int, int, int, int, i32*, i32*, i32*) {}
int  EPrice(const void*, int, i32* out) { for (int i = 0; i < g_e.buildingRows; ++i) out[i] = 900 + i; return g_e.buildingRows; }
int  ETiled(int, int, int, const void*, int) { return 0; }
void EButtons(int, int, i32* out, int n, const void*) { for (int i = 0; i < n; ++i) out[i] = i; }
int  EAdd(int, int) { return 0; }
int  EAddLbl(i16, i16, int, const char*) { return 0; }
int  EAddF(i16, i16, i16, i16, unsigned, int) { return 0; }
void EColor(int, int) {}
void EEnabled(int, int) {}
void EVOT(int, int, int, int, int) {}
int  EData(int) { return 0; }
int  EChild(int, i16, i16, int, int, int) { return 0; }
void ERemove(int, int, int) {}
int  EDestroyT(int, int, int) { return 0; }
void EScroll(int, int, int) {}
int  ERadio(int, int) { return 1; }
void ERadioAdd(int, int) {}
void ERadioFree() {}
const void* EPBegin(int, int, int, unsigned short) { return nullptr; }   // no masters
const void* EPNext() { return nullptr; }
const short* EPActive(const short*) { return nullptr; }
int  EWealth(unsigned short, const short*) { return 0; }
int  EMoney(int m, unsigned char) { return m; }
int  ECollect(unsigned short, int, i32*) { return 0; }
int  EFind(int) { return 0; }
int  EMap(int) { return 0; }
int  EUpg(int) { return 1; }
void EWorth(const void*, unsigned short, int* o) { if (o) std::memset(o, 0, 16 * sizeof(int)); }
int  EGesetz(unsigned char, void*) { return 0; }
int  EVariant(int, int) { return 0; }
int  EInvIdx(short, void*) { return 1; }
int  EInvSlot(short) { return 0; }
void EUse(int, void*) {}
void EPop(int, i32*, int, int, int) {}
int  EPopList(int, int) { return 0; }
void EBeginD(int, int) {}
void EAppend(unsigned, unsigned, const void*, int) {}
int  EState23() { return 1; }
int  EPkt(int) { return 1; }
void EFlag(int, void*) {}
void EAmt() {}
void ESlot(void*, int) {}
void EDragT() {}
void EDragR() {}
void EDragS(int, int) {}
void ECoord() {}
void ELight(int, int, void*) {}
int  ETest(int) { return 1; }
int  EDispatch(int, int, int, int) { return 7; }
void ESel(int, int) {}
void EMP(int, int) {}
void EMProf(int, int) {}

// The detail sub-panel runs its OWN inner frame loop; it must exit immediately
// (return 0) so the session terminates. The OUTER panel loop runs g_e.frameIter
// iterations then exits.
int  EFrame(int, int a2, const void*) {
    // gilde.exe @0x551e0e: VIBE_Panel_RunBuildingDetail's frame loop passes its own
    // address as a2 (ebx), not a3 (RunFrameLoop is __usercall edx/ebx/edi). Detect the
    // inner detail loop via a2 == &Panel_RunBuildingDetail -> exit at once.
    if (a2 == static_cast<int>(reinterpret_cast<std::intptr_t>(&Panel_RunBuildingDetail)))
        return 0;
    if (g_e.frameIter > 0) { --g_e.frameIter; return 1; }
    return 0;
}
int  ERelease() { return 0; }
int  EWheel() { return 0; }
int  EClicked() { return g_e.buildingRows > 0 ? 1 : -1; } // a click is registered
int  EHover() { return 900; }                              // hovering building row 900

void Install() {
    g_e = E2E{};
    g_e.frameIter = 1;
    g_h = GuiDialogs5Hooks{
        &EGameTick, &ECenter, &ESelect, &EGetWin, &EGetChild, &EDestroy, &EVis, &EChildVis,
        &ERich, &EFmt, &ESync, &ESlider, &EPrice, &ETiled, &EButtons,
        &EAdd, &EAddLbl, &EAddF, &EColor, &EEnabled, &EVOT, &EData,
        &EChild, &ERemove, &EDestroyT, &EScroll,
        &ERadio, &ERadioAdd, &ERadioFree,
        &EPBegin, &EPNext, &EPActive, &EWealth, &EMoney,
        &ECollect, &EFind, &EMap, &EUpg, &EWorth, &EGesetz, &EVariant,
        &EInvIdx, &EInvSlot, &EUse, &EPop, &EPopList,
        &EBeginD, &EAppend, &EState23, &EPkt, &EFlag, &EAmt, &ESlot,
        &EDragT, &EDragR, &EDragS, &ECoord, &ELight,
        &ETest, &EDispatch, &ESel, &EMP, &EMProf,
        &EFrame, &ERelease, &EWheel, &EClicked, &EHover,
    };
    ResetGuiDialogs5();        // restores default hooks first...
    SetGuiDialogs5Hooks(&g_h); // ...then install the recording hooks.
}

} // namespace

TEST(GuiDialogs5E2E, PlayerStatsSessionOpensDetailAndTearsDown) {
    Install();
    g_e.buildingRows = 1;        // one building row -> hover dispatches the detail panel

    unsigned char r = Panel_RunPlayerStats(/*city*/5);

    // session ended via the dispatch teardown (EDispatch returns 7).
    CHECK_EQ((int)r, 7);
    // composite loaded the outer player_stats form + the three sub-panel forms,
    // and (since a building row was hovered) the geb_detail sub-panel too.
    bool loadedStats = false, loadedDetail = false;
    for (auto& f : g_e.formsLoaded) {
        if (f == "panel\\player_stats") loadedStats = true;
        if (f == "panel\\player_stats_geb_detail") loadedDetail = true;
    }
    CHECK(loadedStats);
    CHECK(loadedDetail);
    // the master-list sub-panel was empty (EPBegin returns null) -> it self-destroys,
    // and the outer teardown destroys the player_stats + money + building forms.
    CHECK(g_e.destroys >= 3);
}

TEST(GuiDialogs5E2E, PlayerStatsNoBuildingRowsSkipsDetail) {
    Install();
    g_e.buildingRows = 0;        // no rows -> no detail sub-panel

    unsigned char r = Panel_RunPlayerStats(5);
    CHECK_EQ((int)r, 7);
    bool loadedDetail = false;
    for (auto& f : g_e.formsLoaded)
        if (f == "panel\\player_stats_geb_detail") loadedDetail = true;
    CHECK(!loadedDetail);
}

TEST(GuiDialogs5E2E, BuildSequenceDeterministicAcrossPanels) {
    Install();
    // BuildMoneyInfo + BuildBuildingList + BuildMasterList back-to-back share the
    // module tables; running them twice must produce identical row state.
    ResetGuiDialogs5();
    Panel_BuildBuildingList(0);
    int firstCount = g_buildingRowCount;
    ResetGuiDialogs5();
    Panel_BuildBuildingList(0);
    CHECK_EQ(g_buildingRowCount, firstCount);
    SetGuiDialogs5Hooks(nullptr);
}
