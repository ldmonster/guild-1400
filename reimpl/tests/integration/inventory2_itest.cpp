#include "test.h"

// Integration: drive inventory2's RefreshSlots reconciliation against the REAL
// reconstructed stock-table sibling (sim/inventory.cpp's
// InventoryFindSlotByItemId, gilde.exe 0x54f04c). NOT a mock: RefreshSlots calls
// the genuine FindSlotByItemId over an InvGridSlot stock table to locate the slot
// whose capacity dword (slot bytes +4..+7) is the production-fill divisor, and we
// assert the fill value the widget receives is derived from the slot the REAL
// scan returned. The Inventory2Hooks window/object/scene-graph/drag leaves have
// no reconstructed sibling (engine UI glue), so they are recording fakes; the
// slot resolution is the real sibling end to end.
#include "sim/inventory2.h"
#include "sim/inventory.h"   // REAL sibling: InventoryFindSlotByItemId, InvGridSlot
#include "sim/types.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A live item object the scene-graph hooks hand back. The originals read the type
// word, count, the in-production flag, and the production-elapsed dword off it.
struct ItemObj {
    i16 type;
    int count;
    int flag33;     // in-production flag
    int progress28; // production elapsed
};

// Recording UI state for the leaves with no reconstructed sibling.
struct UiRec {
    bool setFillCalled = false;  int fillWidget = -1; float fill = 0.0f;
    bool enabledCalled = false;  int enabledWidget = -1; int enabled = -1;
    bool clearProgress = false;
    int  countText = -1;
    bool dragStored = false; int dragIcon = -1;
} g_rec;

// The single live item for the "still present" query.
ItemObj* g_liveItem = nullptr;

void  RDestroy(int)                  {}
void  REnabled(int w, int e)         { g_rec.enabledCalled = true; g_rec.enabledWidget = w; g_rec.enabled = e; }
void  RSetFill(int w, float f)       { g_rec.setFillCalled = true; g_rec.fillWidget = w; g_rec.fill = f; }
void  RClearProgress(int)            { g_rec.clearProgress = true; }
void  RCountText(int, int, int c)    { g_rec.countText = c; }
void* RGetData(int)                  { static int data = 1; return &data; }   // non-null -> staged
void  RSetValue(int, void*, int)     {}
void  RDragStore(int icon, void*)    { g_rec.dragStored = true; g_rec.dragIcon = icon; }

// queryItems(kind=1, sub=0, type): pass-1 re-resolve. Return the live item iff its
// type matches what is in the slot (i.e. "still present").
void* RQueryItems(void*, int, int sub, int type) {
    if (sub == 0 && g_liveItem && g_liveItem->type == type) return g_liveItem;
    return nullptr;   // sub==5 pass-2 walk: empty (nothing new to bind)
}
void* RIterItems()                   { return nullptr; }
i16   RItemType(void* it)            { return it ? static_cast<ItemObj*>(it)->type : 0; }
int   RItemCount(void* it)           { return it ? static_cast<ItemObj*>(it)->count : 0; }
int   RItemFlag33(void* it)          { return it ? static_cast<ItemObj*>(it)->flag33 : 0; }
int   RItemProgress28(void* it)      { return it ? static_cast<ItemObj*>(it)->progress28 : 0; }
u8    RItemMatByte(void*)            { return 0; }

Inventory2Hooks MakeHooks() {
    Inventory2Hooks h{};
    // Fill EVERY slot with the inert default so no null deref (GetInventory2Hooks
    // returns the installed table verbatim, no per-field fallback).
    h.lightSetGray      = [](int,int){};
    h.surfaceCreate     = [](int,int,int){ return 0; };
    h.surfaceDestroy    = [](int){ return 0; };
    h.windowAddChild    = [](int,int,int,int,int,int){ return 0; };
    h.objectAddToWindow = [](int,int){ return 0; };
    h.inputAddIcon      = [](int,int,int,int,int){ return -1; };
    h.widgetDestroy     = &RDestroy;
    h.objectSetEnabled  = &REnabled;
    h.objectSetColor    = [](int,int){};
    h.formSelectWindow  = [](int,int){};
    h.widgetSetCountText = &RCountText;
    h.widgetSetFill     = &RSetFill;
    h.widgetClearProgress = &RClearProgress;
    h.widgetMarkSlot    = [](int){};
    h.objectGetData     = &RGetData;
    h.objectSetValue    = &RSetValue;
    h.queryItems        = &RQueryItems;
    h.iterItems         = &RIterItems;
    h.itemType          = &RItemType;
    h.itemCount         = &RItemCount;
    h.itemFlag33        = &RItemFlag33;
    h.itemProgress28    = &RItemProgress28;
    h.itemMatByte       = &RItemMatByte;
    h.dragStore         = &RDragStore;
    h.dragAdd           = [](int,void*){};
    h.renderRich        = [](unsigned,int,int){};
    h.hudTiledRow       = [](int,int,int){};
    h.queueRequest      = [](int,int,int){};
    h.renderMessage     = [](char* o,int){ if (o) o[0] = '\0'; };
    h.showUseObject     = [](char*,char*){};
    h.buildingOutputRatio = [](void*){ return 0.0f; };
    h.buildingGroup     = [](void*){ return 0; };
    h.buildingField131  = [](void*){ return 0; };
    h.buildingField130  = [](void*){ return 0; };
    h.buildingContainer = [](void*){ return static_cast<void*>(nullptr); };
    return h;
}

// Build an InvGridSlot stock table: slot[i].type, and the capacity divisor written
// into slot bytes +4..+7 (the dword SlotCapacityDword / the original reads as
// *((int*)slot + 1)). The FindSlotByItemId scan terminates at a zero-type successor.
void SetSlot(InvGridSlot& s, i16 type, int capacity) {
    std::memset(&s, 0, sizeof(s));
    s.type = type;
    std::memcpy(s.pad2 + 2, &capacity, sizeof(int));   // +0x04
}
} // namespace

// An occupied, in-production slot: RefreshSlots re-resolves the item (still present
// via the real query), then the REAL FindSlotByItemId locates the stock slot whose
// capacity dword drives the fill = 1 - progress/capacity.
TEST(Inventory2Itest, RefreshFillFromRealSlotCapacity) {
    static_assert(sizeof(InvGridSlot) == 24, "record buffer must match the real struct");

    ItemObj item{ /*type*/7, /*count*/3, /*flag33*/1, /*progress28*/25 };
    g_liveItem = &item;
    g_rec = UiRec{};

    Inventory2Hooks h = MakeHooks();
    SetInventory2Hooks(&h);

    InvUiState ui;
    ui.personTable = reinterpret_cast<void*>(static_cast<intptr_t>(1));  // non-null -> proceed
    ui.iconItem[0] = &item;          // slot 0 occupied by the live item
    ui.iconWidget[0] = 0xAB;

    // Real stock table: type 7 -> capacity 100; terminated by a zero-type slot.
    InvGridSlot stock[3];
    SetSlot(stock[0], 7, 100);
    SetSlot(stock[1], 0, 0);         // terminator
    SetSlot(stock[2], 0, 0);

    // Cross-check the real sibling directly: it finds the type-7 slot at index 0.
    InvGridSlot* found = InventoryFindSlotByItemId(stock, 3, 7);
    CHECK(found != nullptr);
    if (found) CHECK_EQ(static_cast<int>(found->type), 7);

    int staged = InventoryRefreshSlots(ui, stock, 3, /*a1*/0);

    CHECK_EQ(staged, 1);                 // had data -> staged into drag system
    CHECK(g_rec.setFillCalled);          // in-production -> fill bar set
    if (g_rec.setFillCalled) {
        CHECK_EQ(g_rec.fillWidget, 0xAB);
        // fill = 1 - 25/100 = 0.75 (divisor came from the real slot's capacity dword).
        CHECK(feq(g_rec.fill, 0.75f));
    }
    CHECK(g_rec.enabledCalled);
    if (g_rec.enabledCalled) CHECK_EQ(g_rec.enabled, 0);   // disabled while producing
    CHECK_EQ(g_rec.countText, 3);
    CHECK(g_rec.dragStored);
    if (g_rec.dragStored) CHECK_EQ(g_rec.dragIcon, 7 + kSlotIconBase);

    g_liveItem = nullptr;
    SetInventory2Hooks(nullptr);
}

// Item NOT in the real stock table => FindSlotByItemId returns null => the fill
// branch is skipped entirely (no enable/disable, no fill), proving the real scan's
// miss controls the flow. The item is still staged (it has data).
TEST(Inventory2Itest, RefreshNoSlotSkipsFillBranch) {
    ItemObj item{ /*type*/55, /*count*/2, /*flag33*/1, /*progress28*/10 };
    g_liveItem = &item;
    g_rec = UiRec{};

    Inventory2Hooks h = MakeHooks();
    SetInventory2Hooks(&h);

    InvUiState ui;
    ui.personTable = reinterpret_cast<void*>(static_cast<intptr_t>(1));
    ui.iconItem[0] = &item;
    ui.iconWidget[0] = 0xCD;

    // Stock table contains type 7 only; type 55 misses.
    InvGridSlot stock[2];
    SetSlot(stock[0], 7, 100);
    SetSlot(stock[1], 0, 0);

    CHECK(InventoryFindSlotByItemId(stock, 2, 55) == nullptr);   // real miss

    int staged = InventoryRefreshSlots(ui, stock, 2, 0);

    CHECK_EQ(staged, 1);                 // still staged (has data)
    CHECK(!g_rec.setFillCalled);         // no slot -> fill branch skipped
    CHECK(!g_rec.enabledCalled);         // no slot -> enable/disable skipped
    CHECK_EQ(g_rec.countText, 2);        // count text still refreshed

    g_liveItem = nullptr;
    SetInventory2Hooks(nullptr);
}

// Occupied slot, item NOT in production: the real slot is found, but the not-in-
// production branch enables the widget and clears the progress bar instead of
// filling it.
TEST(Inventory2Itest, RefreshIdleItemEnablesAndClears) {
    ItemObj item{ /*type*/7, /*count*/9, /*flag33*/0, /*progress28*/0 };
    g_liveItem = &item;
    g_rec = UiRec{};

    Inventory2Hooks h = MakeHooks();
    SetInventory2Hooks(&h);

    InvUiState ui;
    ui.personTable = reinterpret_cast<void*>(static_cast<intptr_t>(1));
    ui.iconItem[0] = &item;
    ui.iconWidget[0] = 0x10;

    InvGridSlot stock[2];
    SetSlot(stock[0], 7, 100);
    SetSlot(stock[1], 0, 0);

    int staged = InventoryRefreshSlots(ui, stock, 2, 0);

    CHECK_EQ(staged, 1);
    CHECK(!g_rec.setFillCalled);         // idle -> not a fill
    CHECK(g_rec.enabledCalled);
    if (g_rec.enabledCalled) CHECK_EQ(g_rec.enabled, 1);   // enabled (idle)
    CHECK(g_rec.clearProgress);

    g_liveItem = nullptr;
    SetInventory2Hooks(nullptr);
}

// personTable null -> RefreshSlots early-outs to 0 before touching the slots.
TEST(Inventory2Itest, RefreshNoPersonTableEarlyOut) {
    g_rec = UiRec{};
    Inventory2Hooks h = MakeHooks();
    SetInventory2Hooks(&h);

    InvUiState ui;            // personTable defaults to nullptr
    InvGridSlot stock[1];
    SetSlot(stock[0], 7, 100);

    int staged = InventoryRefreshSlots(ui, stock, 1, 0);
    CHECK_EQ(staged, 0);
    CHECK(!g_rec.setFillCalled);
    CHECK(!g_rec.enabledCalled);

    SetInventory2Hooks(nullptr);
}
