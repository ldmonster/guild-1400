// Unit tests for gui_dialogs5 (VIBE_Panel_* / VIBE_Window_* builders).
// Each test installs a recording hooks struct so the deterministic control flow
// and widget-tree mutations are observable without the engine.
#include "test.h"

#include "gui/gui_dialogs5.h"
#include "gui/object.h"   // g_widgets, ResetWidgets, Widget_AllocSlot
#include "gui/window.h"   // g_currentWindowId

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

// ---------------------------------------------------------------------------
// A recording hooks instance. Static so the function pointers can address its
// members; reset between tests.
// ---------------------------------------------------------------------------
struct Rec {
    std::vector<std::string> formNames;
    std::vector<int> selectWin;
    std::vector<unsigned> richIds;
    int  addToWindowCalls = 0;
    int  addTextLabelCalls = 0;
    int  setEnabledCalls = 0;
    int  setColorCalls = 0;
    int  destroyCalls = 0;
    int  sliderPanelCalls = 0;
    int  radioCreateCalls = 0;
    int  radioAddCalls = 0;
    int  frameLoopReturn = 0;   // 0 => loop exits immediately
    int  frameLoopCalls = 0;
    int  lastClicked = -1;
    int  hoverObject = -1;
    int  mouseRelease = 0;
    int  formIdToReturn = 100;
    int  priceRowCount = 0;
    int  collectCount = 0;
    int  testFlagDword = 1;
    int  personRecs = 0;        // how many persons personQueryBegin/IterNext yields
    int  personFaction = 7;     // FindActiveByEntity returns this faction (!= city => row)
    short personRecBuf[4] = {0,0,0,0};
} g_rec;

GuiDialogs5Hooks g_h;

int  HGameTick(i16, i16, const char* n) { if (n) g_rec.formNames.push_back(n); return g_rec.formIdToReturn; }
void HCenter(int) {}
void HSelect(int, int s) { g_rec.selectWin.push_back(s); }
int  HGetWinId(int, int) { return 7; }
int  HGetChild(int, int, int) { return 9; }
int  HDestroy(int) { ++g_rec.destroyCalls; return -1; }
void HSetVis(int, int) {}
void HSetChildVis(int, int) {}
int  HRich(unsigned id, int, int, int, int) { g_rec.richIds.push_back(id); return 0; }
void HFmt(char* o, const char*, int, int, int) { if (o) std::strcpy(o, "x"); }
void HSync(int) {}
void HSlider(int, int, int, int, int, i32*, i32*, i32*) { ++g_rec.sliderPanelCalls; }
int  HPriceRows(const void*, int, i32* out) { for (int i = 0; i < g_rec.priceRowCount; ++i) out[i] = 1000 + i; return g_rec.priceRowCount; }
int  HTiledBar(int, int, int, const void*, int) { return 42; }
void HButtonRow(int, int, i32* out, int n, const void*) { for (int i = 0; i < n; ++i) out[i] = 200 + i; }
int  HAddWin(int, int) { ++g_rec.addToWindowCalls; return Widget_AllocSlot(); }
int  HAddLabel(i16, i16, int, const char*) { ++g_rec.addTextLabelCalls; return Widget_AllocSlot(); }
int  HAddField(i16, i16, i16, i16, unsigned, int) { return Widget_AllocSlot(); }
void HSetColor(int, int) { ++g_rec.setColorCalls; }
void HSetEnabled(int, int) { ++g_rec.setEnabledCalls; }
void HSetVOT(int, int, int, int, int) {}
int  HGetData(int) { return 0; }
int  HAddChild(int, i16, i16, int, int, int) { return Widget_AllocSlot(); }
void HRemoveIf(int, int, int) {}
int  HDestroyType(int, int, int) { return 0; }
void HScroll(int, int, int) {}
int  HRadioCreate(int, int) { ++g_rec.radioCreateCalls; return 3; }
void HRadioAdd(int, int) { ++g_rec.radioAddCalls; }
void HRadioFree() {}
const void* HPersonBegin(int, int, int, unsigned short) { return g_rec.personRecs > 0 ? g_rec.personRecBuf : nullptr; }
const void* HPersonNext() { if (--g_rec.personRecs > 0) return g_rec.personRecBuf; return nullptr; }
const short* HPersonActive(const short*) { static short rec[120]; rec[0] = (short)g_rec.personFaction; return rec; }
int  HWealth(unsigned short, const short*) { return 1000; }
int  HMoneyCoord(int m, unsigned char) { return m; }
int  HCollect(unsigned short, int, i32* out) { for (int i = 0; i < g_rec.collectCount; ++i) out[i] = 500 + i; return g_rec.collectCount; }
int  HFindBld(int) { return 1; }
int  HMapCat(int) { return 0; }
int  HUpgrade(int) { return 2; }
void HWorth(const void*, unsigned short, int* out) { if (out) std::memset(out, 0, 16 * sizeof(int)); }
int  HGesetz(unsigned char, void*) { return 0; }
int  HVariant(int, int) { return 0; }
int  HInvIdx(short, void* o) { if (o) std::memset(o, 0, 32); return 1; }
int  HInvSlot(short) { return 0; }
void HUseAction(int, void*) {}
void HPopulate(int, i32*, int, int, int) {}
int  HPopList(int, int) { return 0; }
void HBeginDelta(int, int) {}
void HAppendCopied(unsigned, unsigned, const void*, int) {}
int  HState23() { return 11; }
int  HPktStatus(int) { return 1; }
void HFlagBlob(int, void*) {}
void HAmtRefresh() {}
void HSlotReset(void*, int) {}
void HDragText() {}
void HDragReset() {}
void HDragSprite(int, int) {}
void HCoordX() {}
void HLight(int, int, void*) {}
int  HTestFlag(int) { return g_rec.testFlagDword; }
int  HDispatch(int, int, int, int) { return 1; }
void HSelUpdate(int, int) {}
void HMenuPlayer(int, int) {}
void HMenuProf(int, int) {}
int  HFrameLoop(int, int, const void*) { ++g_rec.frameLoopCalls; int r = g_rec.frameLoopReturn; g_rec.frameLoopReturn = 0; return r; }
int  HReadRelease() { return g_rec.mouseRelease; }
int  HReadWheel() { return 0; }
int  HReadClicked() { return g_rec.lastClicked; }
int  HReadHover() { return g_rec.hoverObject; }

void InstallRec() {
    g_rec = Rec{};
    g_h = GuiDialogs5Hooks{
        &HGameTick, &HCenter, &HSelect, &HGetWinId, &HGetChild, &HDestroy, &HSetVis, &HSetChildVis,
        &HRich, &HFmt, &HSync, &HSlider, &HPriceRows, &HTiledBar, &HButtonRow,
        &HAddWin, &HAddLabel, &HAddField, &HSetColor, &HSetEnabled, &HSetVOT, &HGetData,
        &HAddChild, &HRemoveIf, &HDestroyType, &HScroll,
        &HRadioCreate, &HRadioAdd, &HRadioFree,
        &HPersonBegin, &HPersonNext, &HPersonActive, &HWealth, &HMoneyCoord,
        &HCollect, &HFindBld, &HMapCat, &HUpgrade, &HWorth, &HGesetz, &HVariant,
        &HInvIdx, &HInvSlot, &HUseAction, &HPopulate, &HPopList,
        &HBeginDelta, &HAppendCopied, &HState23, &HPktStatus, &HFlagBlob, &HAmtRefresh, &HSlotReset,
        &HDragText, &HDragReset, &HDragSprite, &HCoordX, &HLight,
        &HTestFlag, &HDispatch, &HSelUpdate, &HMenuPlayer, &HMenuProf,
        &HFrameLoop, &HReadRelease, &HReadWheel, &HReadClicked, &HReadHover,
    };
    ResetWidgets();
    ResetGuiDialogs5();        // resets module tables AND restores default hooks...
    SetGuiDialogs5Hooks(&g_h); // ...so install the recording hooks AFTER the reset.
    g_currentWindowId = 0;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(GuiDialogs5, DefaultsAreInert) {
    SetGuiDialogs5Hooks(nullptr);
    ResetGuiDialogs5();
    const GuiDialogs5Hooks* d = GuiDialogs5Hooks_Default();
    CHECK(d != nullptr);
    // Default gameTickFinalize == -1 ("no form"); RunBuildingList exits at once.
    int r = Panel_RunBuildingList(0);
    CHECK_EQ(r, -1);            // formDestroy default returns -1
}

TEST(GuiDialogs5, BuildBuildingListLoadsFormAndSlider) {
    InstallRec();
    g_rec.formIdToReturn = 55;
    g_rec.priceRowCount = 3;
    int form = Panel_BuildBuildingList(0);
    CHECK_EQ(form, 55);
    CHECK_EQ(g_rec.sliderPanelCalls, 1);
    CHECK_EQ(g_buildingRowCount, 3);
    CHECK_EQ(g_buildingRowIds[0], 1000);
    CHECK_EQ(g_buildingRowIds[2], 1002);
    CHECK(!g_rec.formNames.empty());
    if (!g_rec.formNames.empty()) CHECK_EQ(g_rec.formNames[0], std::string("panel\\geb_liste_2"));
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildMoneyInfoClearsRowsAndAddsRows) {
    InstallRec();
    g_rec.formIdToReturn = 77;
    g_rec.collectCount = 2;
    int form = Panel_BuildMoneyInfo(0);
    CHECK_EQ(form, 77);
    // marker rows cleared to -1.
    CHECK_EQ(g_moneyInfoRows[0], -1);
    CHECK_EQ(g_moneyInfoRows[7], -1);
    // two collected buildings => two AddToWindow + two child windows.
    CHECK_EQ(g_rec.addToWindowCalls, 2);
    CHECK_EQ(g_rec.sliderPanelCalls, 1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildMasterListEmptyReturnsMinusOne) {
    InstallRec();
    g_rec.personRecs = 0;        // no persons
    int form = Panel_BuildMasterList(5);
    CHECK_EQ(form, -1);          // empty => Form_Destroy + return -1
    CHECK_EQ(g_rec.destroyCalls, 1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildMasterListWithForeignMasters) {
    InstallRec();
    g_rec.formIdToReturn = 88;
    g_rec.personRecs = 2;        // two persons
    g_rec.personFaction = 9;     // != city(5) => both become rows
    int form = Panel_BuildMasterList(5);
    CHECK_EQ(form, 88);          // non-empty => returns the form id
    CHECK_EQ(g_rec.destroyCalls, 0);
    // each row: 1 AddToWindow + 2 text labels.
    CHECK(g_rec.addToWindowCalls >= 1);
    CHECK(g_rec.addTextLabelCalls >= 2);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildMasterListSkipsOwnFaction) {
    InstallRec();
    g_rec.formIdToReturn = 88;
    g_rec.personRecs = 2;
    g_rec.personFaction = 5;     // == city(5) => skipped, none added
    int form = Panel_BuildMasterList(5);
    CHECK_EQ(form, -1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunBuildingDetailRendersStatLines) {
    InstallRec();
    g_rec.formIdToReturn = 33;
    char rec[600];
    std::memset(rec, 0, sizeof(rec));
    rec[0] = 4;                  // building type 4 => the v24-special line
    int r = Panel_RunBuildingDetail(rec, 0);
    CHECK_EQ(r, -1);             // default destroy
    // upgrade line (0xA0) is always rendered.
    bool hasA0 = false;
    for (unsigned id : g_rec.richIds) if (id == 0xA0u) hasA0 = true;
    CHECK(hasA0);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunBuildingDetailNullRecordSafe) {
    InstallRec();
    int r = Panel_RunBuildingDetail(nullptr, 0);   // must not deref null
    CHECK_EQ(r, -1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunBuildingListDispatchesDetailOnHover) {
    InstallRec();
    g_rec.priceRowCount = 1;     // one price row id == 1000
    g_rec.lastClicked = 1;       // != -1
    g_rec.hoverObject = 1000;    // matches row id => fires detail
    g_rec.frameLoopReturn = 1;   // one extra iteration then exit
    int r = Panel_RunBuildingList(0);
    CHECK_EQ(r, -1);
    CHECK(g_rec.frameLoopCalls >= 1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunApBuyForceQuitOnRelease) {
    InstallRec();
    g_rec.mouseRelease = 1;
    int r = Panel_RunApBuy(0);
    CHECK_EQ(r, -1);
    CHECK_EQ(g_forceQuitLatch, 1);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunUseObjectAbsentInventoryReturnsZero) {
    InstallRec();
    g_rec.testFlagDword = 1;
    short item[4] = {12, 0, 0, 0};
    // override: inventory index lookup returns 0 => early return.
    g_h.inventoryFindSlotIndexByItemId = [](short, void*) { return 0; };
    SetGuiDialogs5Hooks(&g_h);
    int r = Panel_RunUseObject(item, 0);
    CHECK_EQ(r, 0);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunUseObjectCommitsOnConfirm) {
    InstallRec();
    short item[4] = {12, 0, 0, 0};
    g_rec.lastClicked = 1210;    // confirm
    int r = Panel_RunUseObject(item, 0);
    CHECK_EQ(r, 0);              // returns 0 (action queued)
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, ChooseWappenBuildsEightRadioButtons) {
    InstallRec();
    g_rec.formIdToReturn = 21;
    int r = Panel_RunChooseWappen(3);
    CHECK_EQ(r, -1);
    CHECK_EQ(g_rec.radioCreateCalls, 1);
    CHECK_EQ(g_rec.radioAddCalls, 8);    // 8 crest buttons
    CHECK_EQ(g_rec.addToWindowCalls, 8);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, ChooseProfessionLaysOutColumns) {
    InstallRec();
    unsigned short a1[8] = {1,0,0,0,0,0,0,0};
    unsigned short a2[8] = {2,0,0,0,0,0,0,0};
    short a3[8] = {0};
    int r = Panel_ChooseProfession(a1, a2, a3);
    CHECK_EQ(r, 0);
    // v34 goes 4->52 in steps of 4 => 12 widgets added.
    CHECK_EQ(g_rec.addToWindowCalls, 12);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunPlayerStatsGatedOnHandlerFlag) {
    InstallRec();
    g_rec.testFlagDword = 0;     // handler flag clear => returns 0 immediately
    unsigned char r = Panel_RunPlayerStats(0);
    CHECK_EQ((int)r, 0);
    CHECK(g_rec.formNames.empty());
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, RunPlayerStatsComposesThreePanels) {
    InstallRec();
    g_rec.testFlagDword = 1;
    g_rec.formIdToReturn = 60;
    g_rec.collectCount = 1;
    g_rec.priceRowCount = 1;
    g_rec.personRecs = 1;
    g_rec.personFaction = 9;     // foreign => master list non-empty
    unsigned char r = Panel_RunPlayerStats(5);
    CHECK_EQ((int)r, 1);         // dispatch byte (HDispatch returns 1)
    // loaded the player_stats form + the three sub-panel forms.
    CHECK(g_rec.formNames.size() >= 4);
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, SetCityNameCaptionCopiesAndPatches) {
    InstallRec();
    char win[400];
    std::memset(win, 0, sizeof(win));
    std::strcpy(win + 48, "OldCity");
    char name[64];
    std::strcpy(name, "NewName");
    int r = Window_SetCityNameCaption(win, name);
    CHECK_EQ(r, 1);
    // record fields patched.
    CHECK_EQ(*reinterpret_cast<i16*>(win + 10), (i16)18);
    // caption field +48 restored from scratch (unchanged round-trip).
    CHECK_EQ(std::string(win + 48), std::string("OldCity"));
    CHECK_EQ(std::string(name), std::string("NewName"));
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildLawSealsEarlyLawHitsTiledBar) {
    InstallRec();
    g_h.gesetzGetRecord = [](unsigned char, void* out) { if (out) std::memset(out, 0, 12); return 1; };
    SetGuiDialogs5Hooks(&g_h);
    int row[16];
    std::memset(row, 0, sizeof(row));
    row[0] = 7;                  // != -1
    reinterpret_cast<char*>(row)[16] = 3;   // <= 4 => tiled-bar branch
    int* res = Panel_BuildLawSeals(row);
    CHECK_EQ((int)(std::intptr_t)res, 42);  // HTiledBar returns 42
    SetGuiDialogs5Hooks(nullptr);
}

TEST(GuiDialogs5, BuildLawSealsInvalidRowReturnsSelf) {
    InstallRec();
    int row[16];
    std::memset(row, 0, sizeof(row));
    row[0] = -1;                 // invalid => returns row unchanged
    int* res = Panel_BuildLawSeals(row);
    CHECK_EQ(res, row);
    SetGuiDialogs5Hooks(nullptr);
}
