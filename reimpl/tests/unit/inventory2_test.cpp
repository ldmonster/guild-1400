// Unit tests for the inventory2 grid-UI cluster (src/sim/inventory2.cpp): the grid
// surface create/destroy, the slot-window builder, the slot-icon reconciliation, the
// per-workstation item-grid layout, and the use-object menu dispatch. Cross-module
// renderer / window / widget / scene-graph / object / surface / text calls are
// captured through a recording Inventory2Hooks mock. Golden values for the use-speed
// scale (0.1f), the output-ratio percentage (*100), the production fill (1-prog/cap),
// and the icon layout coordinates were computed with python3 (see implementer report).
#include "test.h"

#include "sim/inventory2.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i16;
using guild::u8;

namespace {

// A simple item world: each "item" is an index into this vector.
struct FakeItem { i16 type; int count; int flag33; int progress28; u8 mat; };

// ---- recording mock for the inventory2 hooks ------------------------------
struct Rec {
    // surface
    int  grayA = 0, grayGray = 0, grayCalls = 0;
    int  surfW = 0, surfH = 0, surfFmt = 0; int surfRet = 0; int surfCalls = 0;
    int  destroyedSurface = -1; int surfDestroyRet = 0;
    // window/object
    std::vector<int> destroyedWidgets;
    int  addChildRet = 0; int addChildCalls = 0;
    int  lastColorWidget = -1, lastColor = -1;
    int  addIconRet = 100; std::vector<int> iconX, iconY, iconId;
    int  enableCalls = 0; int lastEnableWidget = -1, lastEnableVal = -1;
    std::vector<float> fills; int clearProgressCalls = 0; int markCalls = 0;
    // count text
    int  countTextCalls = 0; int lastCountWidget = -1, lastCountType = -1, lastCountN = -1;
    // object data
    void* dataToReturn = nullptr;
    int  setValueCalls = 0; int lastSetValueN = -1;
    // drag
    std::vector<int> storeIcons, addIcons;
    // text/hud
    std::vector<unsigned> richIds; std::vector<int> richB;
    std::vector<int> hudN;
    // HandleUseChoice
    int  queuedActor = -1, queuedCode = -1, queuedArg = -1; int queueCalls = 0;
    int  renderMsgId = -1; int showCalls = 0; char showText[64] = {0};
    // building (grid)
    float ratio = 0.0f; int group = 0; int f131 = 0, f130 = 0;
    // query iteration
    std::vector<FakeItem>* world = nullptr;
    int   iterPos = 0; int querySub = -2;  // sub used to decide iteration vs single
    void* containerBase = nullptr; void* lastQueryContainer = nullptr;
    bool  singleResolveFails = false;      // force the slot re-resolve (sub0) to fail
};

Rec* g_rec = nullptr;

void* itemPtr(int i) { return reinterpret_cast<void*>(static_cast<intptr_t>(i + 1)); }
int   itemIdx(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)) - 1; }

void  HGray(int a, int g) { g_rec->grayA = a; g_rec->grayGray = g; ++g_rec->grayCalls; }
int   HSurfCreate(int w, int h, int f) { g_rec->surfW=w; g_rec->surfH=h; g_rec->surfFmt=f; ++g_rec->surfCalls; return g_rec->surfRet; }
int   HSurfDestroy(int s) { g_rec->destroyedSurface = s; return g_rec->surfDestroyRet; }
int   HWinAdd(int,int,int,int,int,int) { ++g_rec->addChildCalls; return g_rec->addChildRet; }
int   HObjAdd(int, int) { return g_rec->addChildRet; }
int   HIconAdd(int x,int y,int,int id,int) { g_rec->iconX.push_back(x); g_rec->iconY.push_back(y); g_rec->iconId.push_back(id); return g_rec->addIconRet; }
void  HWidgetDestroy(int w) { g_rec->destroyedWidgets.push_back(w); }
void  HEnable(int w,int v) { ++g_rec->enableCalls; g_rec->lastEnableWidget=w; g_rec->lastEnableVal=v; }
void  HColor(int w,int c) { g_rec->lastColorWidget=w; g_rec->lastColor=c; }
void  HForm(int,int) {}
void  HCountText(int w,int t,int n) { ++g_rec->countTextCalls; g_rec->lastCountWidget=w; g_rec->lastCountType=t; g_rec->lastCountN=n; }
void  HFill(int,float f) { g_rec->fills.push_back(f); }
void  HClearProg(int) { ++g_rec->clearProgressCalls; }
void  HMark(int) { ++g_rec->markCalls; }
void* HGetData(int) { return g_rec->dataToReturn; }
void  HSetValue(int,void*,int n) { ++g_rec->setValueCalls; g_rec->lastSetValueN=n; }

void* HQuery(void* base, int /*kind*/, int sub, int type) {
    g_rec->lastQueryContainer = base;
    g_rec->querySub = sub;
    if (sub == 5) {  // iteration start over the whole world (mat filtering is caller-side)
        g_rec->iterPos = 0;
        if (g_rec->singleResolveFails) return nullptr;  // empty container (item gone)
        return (g_rec->world && !g_rec->world->empty()) ? itemPtr(0) : nullptr;
    }
    // single-resolve by type (sub0): present unless forced to fail.
    if (g_rec->singleResolveFails) return nullptr;
    for (auto& it : *g_rec->world) if (it.type == type) return itemPtr(1);
    return nullptr;
}
void* HIter() {
    ++g_rec->iterPos;
    if (g_rec->iterPos >= (int)g_rec->world->size()) return nullptr;
    return itemPtr(g_rec->iterPos);
}
i16  HType(void* p) { return (*g_rec->world)[itemIdx(p)].type; }
int  HCount(void* p) { return (*g_rec->world)[itemIdx(p)].count; }
int  HFlag33(void* p) { return (*g_rec->world)[itemIdx(p)].flag33; }
int  HProg28(void* p) { return (*g_rec->world)[itemIdx(p)].progress28; }
u8   HMat(void* p) { return (*g_rec->world)[itemIdx(p)].mat; }
void HStore(int id, void*) { g_rec->storeIcons.push_back(id); }
void HAdd(int id, void*) { g_rec->addIcons.push_back(id); }
void HRich(unsigned id,int,int b) { g_rec->richIds.push_back(id); g_rec->richB.push_back(b); }
void HHud(int,int,int n) { g_rec->hudN.push_back(n); }
void HQueue(int a,int c,int arg) { ++g_rec->queueCalls; g_rec->queuedActor=a; g_rec->queuedCode=c; g_rec->queuedArg=arg; }
void HMsg(char* out,int id) { g_rec->renderMsgId=id; std::strcpy(out, "USE"); }
void HShow(char* t, char*) { ++g_rec->showCalls; std::strncpy(g_rec->showText, t, 63); }
float HRatio(void*) { return g_rec->ratio; }
int  HGroup(void*) { return g_rec->group; }
int  HF131(void*) { return g_rec->f131; }
int  HF130(void*) { return g_rec->f130; }
void* HContainer(void* b) { return g_rec->containerBase ? g_rec->containerBase : b; }

Inventory2Hooks MakeHooks() {
    Inventory2Hooks h{};
    h.lightSetGray = HGray; h.surfaceCreate = HSurfCreate; h.surfaceDestroy = HSurfDestroy;
    h.windowAddChild = HWinAdd; h.objectAddToWindow = HObjAdd; h.inputAddIcon = HIconAdd;
    h.widgetDestroy = HWidgetDestroy; h.objectSetEnabled = HEnable; h.objectSetColor = HColor;
    h.formSelectWindow = HForm; h.widgetSetCountText = HCountText; h.widgetSetFill = HFill;
    h.widgetClearProgress = HClearProg; h.widgetMarkSlot = HMark;
    h.objectGetData = HGetData; h.objectSetValue = HSetValue;
    h.queryItems = HQuery; h.iterItems = HIter;
    h.itemType = HType; h.itemCount = HCount; h.itemFlag33 = HFlag33;
    h.itemProgress28 = HProg28; h.itemMatByte = HMat;
    h.dragStore = HStore; h.dragAdd = HAdd;
    h.renderRich = HRich; h.hudTiledRow = HHud;
    h.queueRequest = HQueue; h.renderMessage = HMsg; h.showUseObject = HShow;
    h.buildingOutputRatio = HRatio; h.buildingGroup = HGroup;
    h.buildingField131 = HF131; h.buildingField130 = HF130; h.buildingContainer = HContainer;
    return h;
}

struct Scoped {
    Inventory2Hooks h;
    Rec rec;
    Scoped() { h = MakeHooks(); g_rec = &rec; SetInventory2Hooks(&h); }
    ~Scoped() { SetInventory2Hooks(nullptr); g_rec = nullptr; }
};

// Build a stock table (InvGridSlot) with type at +0 and a capacity dword at +4.
InvGridSlot MakeSlot(i16 type, int cap) {
    InvGridSlot s{}; s.type = type; std::memcpy(s.pad2 + 2, &cap, sizeof(int)); return s;
}

}  // namespace

// ===========================================================================
// CreateGridSurface / DestroyGridSurface
// ===========================================================================
TEST(Inventory2Surface, CreateRecordsSurface) {
    Scoped sc; sc.rec.surfRet = 777;
    InvUiState ui;
    int r = InventoryCreateGridSurface(ui);
    CHECK_EQ(r, 777);
    CHECK_EQ(ui.gridSurface, 777);
    CHECK_EQ(sc.rec.grayA, 0);
    CHECK_EQ(sc.rec.grayGray, 64);
    CHECK_EQ(sc.rec.surfW, 1280);
    CHECK_EQ(sc.rec.surfH, 1280);
    CHECK_EQ(sc.rec.surfFmt, 11);
}

TEST(Inventory2Surface, DestroyClearsAndDestroys) {
    Scoped sc; sc.rec.surfDestroyRet = 9;
    InvUiState ui; ui.gridSurface = 555;
    int r = InventoryDestroyGridSurface(ui);
    CHECK_EQ(r, 9);
    CHECK_EQ(sc.rec.destroyedSurface, 555);
    CHECK_EQ(ui.gridSurface, 0);
}

TEST(Inventory2Surface, DestroyNoopWhenAbsent) {
    Scoped sc; sc.rec.destroyedSurface = -1;
    InvUiState ui; ui.gridSurface = 0;
    int r = InventoryDestroyGridSurface(ui);
    CHECK_EQ(r, 0);
    CHECK_EQ(sc.rec.destroyedSurface, -1);  // surfaceDestroy never called
}

// ===========================================================================
// OpenSlotWindow
// ===========================================================================
TEST(Inventory2Open, BuildsWindowWhenClosed) {
    Scoped sc; sc.rec.addChildRet = 42;
    InvUiState ui;  // slotWindow == -1 (closed)
    int r = InventoryOpenSlotWindow(ui, 7, 10, 3, 20, 0x1234);
    CHECK_EQ(r, 7);                       // returns the parent result
    CHECK_EQ(ui.slotWindow, 42);          // the AddChildWindow id
    CHECK_EQ(sc.rec.lastColor, 67);       // active window coloured 67
    // title rich-string rendered with the supplied template
    CHECK(!sc.rec.richIds.empty());
    CHECK_EQ(sc.rec.richIds[0], 0x1234u);
    // all 6 slots cleared
    for (int i = 0; i < kInvSlotCount; ++i) { CHECK_EQ(ui.iconWidget[i], -1); CHECK(ui.iconItem[i] == nullptr); }
}

TEST(Inventory2Open, NoopWhenAlreadyOpen) {
    Scoped sc; sc.rec.addChildRet = 42;
    InvUiState ui; ui.slotWindow = 5;     // already open
    int r = InventoryOpenSlotWindow(ui, 7, 10, 3, 20, 0x1234);
    CHECK_EQ(r, 7);
    CHECK_EQ(ui.slotWindow, 5);           // unchanged
    CHECK_EQ(sc.rec.addChildCalls, 0);    // nothing built
}

// ===========================================================================
// RefreshSlots
// ===========================================================================
TEST(Inventory2Refresh, EarlyOutWhenNoPerson) {
    Scoped sc;
    InvUiState ui;  // personTable == nullptr
    std::vector<FakeItem> world; sc.rec.world = &world;
    int r = InventoryRefreshSlots(ui, nullptr, 0, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(sc.rec.addChildCalls, 0);
}

TEST(Inventory2Refresh, GoneItemFreesSlot) {
    Scoped sc;
    InvUiState ui; ui.personTable = reinterpret_cast<void*>(1);
    std::vector<FakeItem> world; sc.rec.world = &world;
    world.push_back({7, 1, 0, 0, 0});         // the slot's (now stale) item, type 7
    ui.iconItem[0] = itemPtr(0); ui.iconWidget[0] = 200;
    sc.rec.singleResolveFails = true;         // the slot re-resolve fails -> item gone
    int r = InventoryRefreshSlots(ui, nullptr, 0, 0);
    CHECK_EQ(r, 0);
    CHECK(ui.iconItem[0] == nullptr);         // slot freed
    CHECK_EQ(ui.iconWidget[0], -1);
    CHECK_EQ((int)sc.rec.destroyedWidgets.size(), 1);
    CHECK_EQ(sc.rec.destroyedWidgets[0], 200);
}

TEST(Inventory2Refresh, PresentItemSetsCountText) {
    Scoped sc;
    InvUiState ui; ui.personTable = reinterpret_cast<void*>(1);
    std::vector<FakeItem> world; sc.rec.world = &world;
    world.push_back({7, 4, 0, 0, 0});         // present, type 7, count 4
    ui.iconItem[0] = itemPtr(0); ui.iconWidget[0] = 200;
    int r = InventoryRefreshSlots(ui, nullptr, 0, 0);
    (void)r;
    CHECK(sc.rec.countTextCalls >= 1);
    CHECK_EQ(sc.rec.lastCountType, 7);
    CHECK_EQ(sc.rec.lastCountN, 4);
}

TEST(Inventory2Refresh, ProductionFillGolden) {
    Scoped sc;
    InvUiState ui; ui.personTable = reinterpret_cast<void*>(1);
    std::vector<FakeItem> world; sc.rec.world = &world;
    // slot 0 holds item type 5, count 4, in production (flag33), progress 40.
    world.push_back({5, 4, 1, 40, 0});    // idx 0
    ui.iconItem[0] = itemPtr(0); ui.iconWidget[0] = 300;
    sc.rec.dataToReturn = reinterpret_cast<void*>(0xABCD);
    // stock table: type 5 with capacity 80 -> fill = 1 - 40/80 = 0.5
    std::vector<InvGridSlot> stock = { MakeSlot(5, 80) };
    int r = InventoryRefreshSlots(ui, stock.data(), (int)stock.size(), 0);
    CHECK_EQ(r, 1);                       // had data -> staged
    CHECK_EQ(sc.rec.lastEnableVal, 0);    // in production -> disabled
    CHECK(!sc.rec.fills.empty());
    CHECK_EQ(sc.rec.fills[0], 0.5f);      // golden: 1 - 40/80
    CHECK(!sc.rec.storeIcons.empty());
    CHECK_EQ(sc.rec.storeIcons[0], 5 + kSlotIconBase);  // 211
}

TEST(Inventory2Refresh, NotInProductionEnables) {
    Scoped sc;
    InvUiState ui; ui.personTable = reinterpret_cast<void*>(1);
    std::vector<FakeItem> world; sc.rec.world = &world;
    world.push_back({9, 2, 0, 0, 0});     // not in production
    ui.iconItem[0] = itemPtr(0); ui.iconWidget[0] = 301;
    std::vector<InvGridSlot> stock = { MakeSlot(9, 50) };
    int r = InventoryRefreshSlots(ui, stock.data(), (int)stock.size(), 0);
    (void)r;
    CHECK_EQ(sc.rec.lastEnableVal, 1);    // enabled
    CHECK(sc.rec.clearProgressCalls >= 1);
}

// ===========================================================================
// RenderItemGrid
// ===========================================================================
TEST(Inventory2Grid, OutputRatioPercentGolden) {
    Scoped sc;
    InvUiState ui; ui.wsBuilding[0] = reinterpret_cast<void*>(0x1000);
    void* live[1] = { reinterpret_cast<void*>(0x1000) };  // station still live
    std::vector<FakeItem> world; sc.rec.world = &world;   // no items
    sc.rec.containerBase = reinterpret_cast<void*>(0x2000);
    sc.rec.ratio = 0.999f;                                // -> pct 99
    int r = InventoryRenderItemGrid(ui, live, 1, 0, 0);
    (void)r;
    CHECK_EQ(ui.countWindow, 1);          // one shown station counted
    // first rich call is the ratio percentage template aC1n2I (0x12C1), b = pct.
    CHECK(!sc.rec.richIds.empty());
    CHECK_EQ(sc.rec.richIds[0], 0x12C1u);
    CHECK_EQ(sc.rec.richB[0], 99);        // golden: (int)(0.999*100)
}

TEST(Inventory2Grid, TearsDownDeadStation) {
    Scoped sc;
    InvUiState ui;
    ui.wsBuilding[0] = reinterpret_cast<void*>(0x1000);
    ui.wsLabelA[0] = 11; ui.wsLabelB[0] = 12; ui.wsWindow[0] = 13;
    void* live[1] = { reinterpret_cast<void*>(0x9999) };  // 0x1000 NOT live
    std::vector<FakeItem> world; sc.rec.world = &world;
    sc.rec.group = 0;                                     // live one isn't group 10..12
    int r = InventoryRenderItemGrid(ui, live, 1, 0, 0);
    (void)r;
    CHECK(ui.wsBuilding[0] == nullptr);
    CHECK_EQ(ui.wsLabelA[0], -1);
    CHECK_EQ(ui.wsLabelB[0], -1);
    CHECK_EQ(ui.wsWindow[0], -1);
    CHECK_EQ((int)sc.rec.destroyedWidgets.size(), 3);     // three widgets destroyed
}

TEST(Inventory2Grid, AddsNewGroupStation) {
    Scoped sc; sc.rec.addChildRet = 70;
    InvUiState ui;                        // no stations shown
    void* live[1] = { reinterpret_cast<void*>(0x1000) };
    std::vector<FakeItem> world; sc.rec.world = &world;
    sc.rec.group = 11;                    // in [10,12] -> a column is added
    sc.rec.f131 = 33;
    int r = InventoryRenderItemGrid(ui, live, 1, 0, 0);
    (void)r;
    CHECK(ui.wsBuilding[0] == live[0]);   // bound into column 0
    CHECK_EQ(ui.wsWindow[0], 70);
    CHECK_EQ(sc.rec.lastColor, 67);       // station window coloured 67
    // station label rich-string 0x12BE with field131
    bool sawLabel = false;
    for (size_t i = 0; i < sc.rec.richIds.size(); ++i)
        if (sc.rec.richIds[i] == 0x12BEu && sc.rec.richB[i] == 33) sawLabel = true;
    CHECK(sawLabel);
}

TEST(Inventory2Grid, RestagesPopulatedSlots) {
    Scoped sc;
    InvUiState ui;
    ui.wsBuilding[0] = reinterpret_cast<void*>(0x1000);
    ui.wsSlotWidget[0][0] = 500; ui.wsSlotType[0][0] = 17;
    void* live[1] = { reinterpret_cast<void*>(0x1000) };
    // The slot's item (type 17) is still present so the pass-1 prune leaves it alone.
    std::vector<FakeItem> world; sc.rec.world = &world;
    world.push_back({17, 1, 0, 0, 0});
    sc.rec.dataToReturn = reinterpret_cast<void*>(0xDEAD);  // slot has data
    int r = InventoryRenderItemGrid(ui, live, 1, 0, 0);
    CHECK_EQ(r, 1);                       // staged
    CHECK(!sc.rec.addIcons.empty());
    if (!sc.rec.addIcons.empty())
        CHECK_EQ(sc.rec.addIcons[0], 17 + kSlotIconBase);  // 223
}

// ===========================================================================
// HandleUseChoice
// ===========================================================================
TEST(Inventory2Use, ConsumeQueuesScaledCommand) {
    Scoped sc;
    ItemUseRec item{ /*actor4*/ 88, /*kind2*/ 0, /*magnitude28*/ 100 };
    UseMenuRec menu{ /*choice12*/ 1, /*flag16*/ 0 };
    int r = ItemHandleUseChoice(&item, &menu, nullptr);
    CHECK_EQ(r, 1);
    CHECK_EQ(sc.rec.queueCalls, 1);
    CHECK_EQ(sc.rec.queuedActor, 88);
    CHECK_EQ(sc.rec.queuedCode, 28);
    CHECK_EQ(sc.rec.queuedArg, 10);       // golden: (int)(100 * 0.1f) == 10
}

TEST(Inventory2Use, ConsumeScaleTruncates) {
    Scoped sc;
    ItemUseRec item{ 1, 0, 9 };           // 9 * 0.1 = 0.9 -> truncates to 0
    UseMenuRec menu{ 1, 0 };
    ItemHandleUseChoice(&item, &menu, nullptr);
    CHECK_EQ(sc.rec.queuedArg, 0);
    ItemUseRec item2{ 1, 0, 255 };        // 255 * 0.1 = 25.5 -> 25
    UseMenuRec menu2{ 1, 0 };
    ItemHandleUseChoice(&item2, &menu2, nullptr);
    CHECK_EQ(sc.rec.queuedArg, 25);
}

TEST(Inventory2Use, Kind6ShowsPanel) {
    Scoped sc;
    char glyphs[16] = {0};
    ItemUseRec item{ 5, /*kind2*/ 6, 0 };
    UseMenuRec menu{ /*choice12*/ 0, 0 };
    int r = ItemHandleUseChoice(&item, &menu, glyphs);
    CHECK_EQ(r, 1);
    CHECK_EQ(menu.flag16, 1);             // panel branch sets flag16
    CHECK_EQ(sc.rec.renderMsgId, 3253);   // message id
    CHECK_EQ(sc.rec.showCalls, 1);
    CHECK_EQ(sc.rec.queueCalls, 0);       // no consume queued
}

TEST(Inventory2Use, NonKind6NonChoiceIsNoop) {
    Scoped sc;
    ItemUseRec item{ 5, /*kind2*/ 3, 0 }; // kind != 6, choice 0
    UseMenuRec menu{ 0, 0 };
    int r = ItemHandleUseChoice(&item, &menu, nullptr);
    CHECK_EQ(r, 1);
    CHECK_EQ(menu.flag16, 0);             // unchanged
    CHECK_EQ(sc.rec.showCalls, 0);
    CHECK_EQ(sc.rec.queueCalls, 0);
}
