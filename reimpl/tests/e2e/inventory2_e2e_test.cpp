// E2E flow for the inventory2 grid-UI cluster (src/sim/inventory2.cpp): drive a full
// inventory-window lifecycle across the translated functions with a single recording
// Inventory2Hooks mock acting as a minimal "engine":
//   create the grid surface -> open the slot window -> add an item to the world and
//   refresh (it binds into a free slot) -> remove the item and refresh (the slot is
//   freed) -> render the workstation grid (a group-11 station appears) -> dispatch a
//   use-object "consume" choice -> destroy the surface.
// The whole flow is deterministic; the golden values (211 == type5 icon, arg 10 for a
// magnitude-100 consume) were computed by hand (see implementer report).
#include "test.h"

#include "sim/inventory2.h"

#include <vector>

using namespace guild::sim;
using guild::i16;
using guild::u8;

namespace {

struct E2EItem { i16 type; int count; int flag33; int progress28; u8 mat; };

struct Engine {
    std::vector<E2EItem> world;
    int   iterPos = 0;
    int   nextWidget = 1000;
    int   surface = 0;
    bool  resolveFail = false;
    // captured effects
    std::vector<int> destroyed;
    std::vector<int> dragStored;
    std::vector<int> dragAdded;
    int   queuedArg = -1, queuedCode = -1, queuedActor = -1;
    int   group = 0;
    void* dataReturn = nullptr;
};

Engine* g_eng = nullptr;

void* itemPtr(int i) { return reinterpret_cast<void*>(static_cast<intptr_t>(i + 1)); }
int   itemIdx(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)) - 1; }

void  EGray(int, int) {}
int   ESurfCreate(int, int, int) { g_eng->surface = 4242; return 4242; }
int   ESurfDestroy(int) { return 1; }
int   EWinAdd(int,int,int,int,int,int) { return g_eng->nextWidget++; }
int   EObjAdd(int, int) { return g_eng->nextWidget++; }
int   EIconAdd(int,int,int,int,int) { return g_eng->nextWidget++; }
void  EDestroy(int w) { g_eng->destroyed.push_back(w); }
void  EEnable(int,int) {}
void  EColor(int,int) {}
void  EForm(int,int) {}
void  ECount(int,int,int) {}
void  EFill(int,float) {}
void  EClear(int) {}
void  EMark(int) {}
void* EGetData(int) { return g_eng->dataReturn; }
void  ESetValue(int,void*,int) {}
void* EQuery(void* base, int, int sub, int type) {
    if (sub == 5) {
        g_eng->iterPos = 0;
        if (g_eng->resolveFail) return nullptr;   // empty container (item gone)
        return g_eng->world.empty() ? nullptr : itemPtr(0);
    }
    (void)base;
    if (g_eng->resolveFail) return nullptr;
    for (auto& it : g_eng->world) if (it.type == type) return itemPtr(0);
    return nullptr;
}
void* EIter() {
    ++g_eng->iterPos;
    if (g_eng->iterPos >= (int)g_eng->world.size()) return nullptr;
    return itemPtr(g_eng->iterPos);
}
i16  EType(void* p) { return g_eng->world[itemIdx(p)].type; }
int  ECnt(void* p) { return g_eng->world[itemIdx(p)].count; }
int  EFlag(void* p) { return g_eng->world[itemIdx(p)].flag33; }
int  EProg(void* p) { return g_eng->world[itemIdx(p)].progress28; }
u8   EMat(void* p) { return g_eng->world[itemIdx(p)].mat; }
void EStore(int id, void*) { g_eng->dragStored.push_back(id); }
void EAdd(int id, void*) { g_eng->dragAdded.push_back(id); }
void ERich(unsigned,int,int) {}
void EHud(int,int,int) {}
void EQueue(int a,int c,int arg) { g_eng->queuedActor=a; g_eng->queuedCode=c; g_eng->queuedArg=arg; }
void EMsg(char* out,int) { if (out) out[0]='\0'; }
void EShow(char*,char*) {}
float ERatio(void*) { return 0.5f; }
int  EGroup(void*) { return g_eng->group; }
int  EF(void*) { return 0; }
void* ECont(void* b) { return b; }

Inventory2Hooks MakeHooks() {
    Inventory2Hooks h{};
    h.lightSetGray=EGray; h.surfaceCreate=ESurfCreate; h.surfaceDestroy=ESurfDestroy;
    h.windowAddChild=EWinAdd; h.objectAddToWindow=EObjAdd; h.inputAddIcon=EIconAdd;
    h.widgetDestroy=EDestroy; h.objectSetEnabled=EEnable; h.objectSetColor=EColor;
    h.formSelectWindow=EForm; h.widgetSetCountText=ECount; h.widgetSetFill=EFill;
    h.widgetClearProgress=EClear; h.widgetMarkSlot=EMark;
    h.objectGetData=EGetData; h.objectSetValue=ESetValue;
    h.queryItems=EQuery; h.iterItems=EIter;
    h.itemType=EType; h.itemCount=ECnt; h.itemFlag33=EFlag; h.itemProgress28=EProg; h.itemMatByte=EMat;
    h.dragStore=EStore; h.dragAdd=EAdd;
    h.renderRich=ERich; h.hudTiledRow=EHud;
    h.queueRequest=EQueue; h.renderMessage=EMsg; h.showUseObject=EShow;
    h.buildingOutputRatio=ERatio; h.buildingGroup=EGroup;
    h.buildingField131=EF; h.buildingField130=EF; h.buildingContainer=ECont;
    return h;
}

}  // namespace

TEST(Inventory2E2E, FullWindowLifecycle) {
    Engine eng; g_eng = &eng;
    Inventory2Hooks h = MakeHooks();
    SetInventory2Hooks(&h);

    InvUiState ui;

    // 1) create the grid surface.
    int surf = InventoryCreateGridSurface(ui);
    CHECK_EQ(surf, 4242);
    CHECK_EQ(ui.gridSurface, 4242);

    // 2) open the slot window (was closed).
    int parent = InventoryOpenSlotWindow(ui, 1, /*a2*/10, /*a3*/2, /*a4*/20, /*a5*/0x100);
    CHECK_EQ(parent, 1);
    CHECK(ui.slotWindow != -1);          // window now open
    CHECK(ui.personTable != nullptr);    // person base resolved

    // re-opening is a no-op.
    int sw = ui.slotWindow;
    InventoryOpenSlotWindow(ui, 1, 10, 2, 20, 0x100);
    CHECK_EQ(ui.slotWindow, sw);

    // 3) add an item with data to the world and refresh -> staged into the drag system.
    eng.world.push_back({5, 4, 0, 0, 0});   // type 5, count 4
    eng.dataReturn = reinterpret_cast<void*>(0xBEEF);
    // place it in slot 0 (simulating it was already iterated in)
    ui.iconItem[0] = itemPtr(0); ui.iconWidget[0] = 900;
    int staged = InventoryRefreshSlots(ui, nullptr, 0, 0);
    CHECK_EQ(staged, 1);                  // had data -> staged
    CHECK(!eng.dragStored.empty());
    CHECK_EQ(eng.dragStored.back(), 5 + kSlotIconBase);  // 211

    // 4) the item is consumed/removed -> next refresh frees the slot.
    eng.resolveFail = true;
    int staged2 = InventoryRefreshSlots(ui, nullptr, 0, 0);
    CHECK_EQ(staged2, 0);
    CHECK(ui.iconItem[0] == nullptr);     // slot freed
    CHECK_EQ(ui.iconWidget[0], -1);
    CHECK(!eng.destroyed.empty());
    CHECK_EQ(eng.destroyed.back(), 900);  // its widget was destroyed
    eng.resolveFail = false;

    // 5) render the workstation grid: a group-11 station appears in column 0.
    eng.group = 11;
    void* live[1] = { reinterpret_cast<void*>(0x5000) };
    InventoryRenderItemGrid(ui, live, 1, 0, 0);
    CHECK(ui.wsBuilding[0] == live[0]);   // station bound
    CHECK(ui.wsWindow[0] != -1);

    // 6) dispatch a "consume" use-object choice (magnitude 100 -> arg 10).
    ItemUseRec item{ /*actor4*/ 77, /*kind2*/ 0, /*magnitude28*/ 100 };
    UseMenuRec menu{ /*choice12*/ 1, 0 };
    int rc = ItemHandleUseChoice(&item, &menu, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ(eng.queuedActor, 77);
    CHECK_EQ(eng.queuedCode, 28);
    CHECK_EQ(eng.queuedArg, 10);          // golden: (int)(100 * 0.1f)

    // 7) destroy the surface.
    int dr = InventoryDestroyGridSurface(ui);
    CHECK_EQ(dr, 1);
    CHECK_EQ(ui.gridSurface, 0);

    SetInventory2Hooks(nullptr);
    g_eng = nullptr;
}
