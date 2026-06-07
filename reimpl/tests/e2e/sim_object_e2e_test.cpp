// End-to-end flow for the scene-object (GameObject) lifecycle.
//   src/sim/object.{h,cpp}  (+ entity.cpp, building.cpp)
//
// Scenario: a warehouse (building scene-node container) holds several item
// stacks; a crate (a child container) holds nested items. We add stock, merge
// stacks, move an item by remove+add, decrement to zero (auto-remove), then
// walk the whole tree and verify the structure, the stock totals, and the
// sequence of command-hook mutations against a hand-computed reference.
#include "sim/object.h"
#include "test.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Captured command-hook events (the mockable lockstep mutation channel).
struct CmdEvent { ObjectCmd cmd; i16 prototype; i32 location; i32 delta; };
std::vector<CmdEvent>* g_log = nullptr;

void HookFn(ObjectCmd cmd, int /*node*/, i16 prototype, i32 location, i32 delta) {
    if (g_log)
        g_log->push_back({cmd, prototype, location, delta});
}

void Reset() {
    ResetEntityArrays();
    ObjectResetContainerHeads();
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;
    g_sceneNodeCount = 0;
}

int PutSceneContainer(int i, i16 type, i32 id, i32 location) {
    g_sceneNodes[i].type = type;
    g_sceneNodes[i].id = id;
    g_sceneNodes[i].entityPtr = -1;   // +20 child head
    g_sceneNodes[i].childPtr  = -1;   // +63 sibling
    ObSetLocation(i, location);
    ObSetOwner(i, -1);
    ObSetAmount(i, 0);
    if (i + 1 > g_sceneNodeCount)
        g_sceneNodeCount = i + 1;
    return i;
}

// Total amount of a given prototype directly under a container's child head.
int SumProto(int head, i16 prototype) {
    int s = 0;
    for (int n = head; n >= 0; n = ObGetSibling(n))
        if (ObGetPrototype(n) == prototype)
            s += ObGetAmount(n);
    return s;
}

int ChildHead(int container) { return g_sceneNodes[container].entityPtr; }

} // namespace

TEST(SimObjectE2E, WarehouseTreeFlow) {
    Reset();
    std::vector<CmdEvent> log;
    g_log = &log;
    ObjectSetCommandHook(&HookFn);

    // --- Build the structure ------------------------------------------------
    // Warehouse: building scene container, id 1000, rooted on object 9000.
    g_objects[0].alive = 1; g_objects[0].id = 9000;
    int warehouse = PutSceneContainer(100, /*type*/29, /*id*/1000, /*loc*/9000);

    // Add three item stacks to the warehouse:
    //   grain (prot 40) x50, wood (prot 41) x30, currency (9) x500.
    int grain = GameObjectAddObjektToParent(1000, 40, 50);
    int wood  = GameObjectAddObjektToParent(1000, 41, 30);
    int money = GameObjectAddObjektToParent(1000, kObjProtCurrency, 500);
    CHECK(grain >= 0 && wood >= 0 && money >= 0);
    CHECK_EQ(grain, ChildHead(warehouse));      // first child is grain
    // Merge more grain into the existing stack (no new node).
    GameObjectAddObjektToParent(1000, 40, 25);  // grain now 75
    CHECK_EQ(ObGetAmount(grain), 75);

    // A crate (child container) inside the warehouse, id 1001.
    int crate = GameObjectAddObjekt(1000, /*prot*/200, /*amount*/1, -1);
    CHECK(crate >= 0);
    // The crate is the 4th child of the warehouse (grain, wood, money, crate).
    {
        int n = 0;
        for (int x = ChildHead(warehouse); x >= 0; x = ObGetSibling(x)) ++n;
        CHECK_EQ(n, 4);
    }
    // Put nested items in the crate: tools (prot 60) x12.
    i32 crateId = ObGetId(crate);
    int tools = GameObjectAddObjektToParent(crateId, 60, 12);
    CHECK(tools >= 0);
    CHECK_EQ(ChildHead(crate), tools);

    // --- Move an item: take 30 grain out of the warehouse, put into crate ----
    CHECK_EQ(GameObjectRemoveObjektAmount(1000, 40, 30), 1);   // grain 75 -> 45
    CHECK_EQ(ObGetAmount(grain), 45);
    int movedGrain = GameObjectAddObjektToParent(crateId, 40, 30);
    CHECK(movedGrain >= 0);
    CHECK_EQ(ObGetAmount(movedGrain), 30);

    // --- Decrement wood to exactly zero -> stack auto-removed ----------------
    CHECK_EQ(GameObjectDecrementObjektStock(1000, 41, 30), 1);
    CHECK_EQ(ObGetPrototype(wood), (i16)0);     // freed slot
    // Warehouse now has 3 direct children: grain, money, crate.
    {
        int n = 0;
        for (int x = ChildHead(warehouse); x >= 0; x = ObGetSibling(x)) ++n;
        CHECK_EQ(n, 3);
    }

    // --- Verify totals against the hand-computed reference -------------------
    // Warehouse direct children: grain 45, currency 500, crate(amount 1).
    CHECK_EQ(SumProto(ChildHead(warehouse), 40), 45);
    CHECK_EQ(SumProto(ChildHead(warehouse), kObjProtCurrency), 500);
    CHECK_EQ(GameObjectSumValuesAtLocation(1000), 500);   // currency sum
    CHECK_EQ(GameObjectCountAtLocation(1000), 1);         // 1 currency stack
    // Crate children: tools 12, grain 30.
    CHECK_EQ(SumProto(ChildHead(crate), 60), 12);
    CHECK_EQ(SumProto(ChildHead(crate), 40), 30);

    // Root-container walk: the crate's grain resolves up to object 9000.
    ObjectRec* root = GameObjectResolveRootContainer(movedGrain);
    CHECK(root != nullptr);
    CHECK_EQ(root->id, 9000);

    // --- Verify the emitted command sequence vs the reference ---------------
    // Expected, in order:
    //   kAdd      grain 40 @1000
    //   kAdd      wood  41 @1000
    //   kAdd      currency 9 @1000
    //   kAddAmount grain 40 @1000 (+25 merge)
    //   kAdd      crate 200 @1000
    //   kAdd      tools 60 @crateId
    //   kRemoveAmount grain 40 @1000 (-30)
    //   kAdd      movedGrain 40 @crateId
    //   kRemoveAmount wood 41 @1000 (-30)   [decrement]
    //   kRemove   wood 41 @1000             [auto-remove at zero]
    CHECK_EQ((int)log.size(), 10);
    CHECK(log[0].cmd == ObjectCmd::kAdd && log[0].prototype == 40);
    CHECK(log[1].cmd == ObjectCmd::kAdd && log[1].prototype == 41);
    CHECK(log[2].cmd == ObjectCmd::kAdd && log[2].prototype == kObjProtCurrency);
    CHECK(log[3].cmd == ObjectCmd::kAddAmount && log[3].prototype == 40
          && log[3].delta == 25);
    CHECK(log[4].cmd == ObjectCmd::kAdd && log[4].prototype == 200);
    CHECK(log[5].cmd == ObjectCmd::kAdd && log[5].prototype == 60
          && log[5].location == crateId);
    CHECK(log[6].cmd == ObjectCmd::kRemoveAmount && log[6].prototype == 40
          && log[6].delta == 30);
    CHECK(log[7].cmd == ObjectCmd::kAdd && log[7].prototype == 40
          && log[7].location == crateId);
    CHECK(log[8].cmd == ObjectCmd::kRemoveAmount && log[8].prototype == 41
          && log[8].delta == 30);
    CHECK(log[9].cmd == ObjectCmd::kRemove && log[9].prototype == 41);

    g_log = nullptr;
    ObjectSetCommandHook(nullptr);
}

TEST(SimObjectE2E, FreeSubtreeOnContainerRemoval) {
    Reset();
    g_objects[0].alive = 1; g_objects[0].id = 9000;
    int wh = PutSceneContainer(100, 29, 1000, 9000);

    // Crate with two nested items.
    int crate = GameObjectAddObjekt(1000, 200, 1, -1);
    i32 crateId = ObGetId(crate);
    int a = GameObjectAddObjektToParent(crateId, 60, 3);
    int b = GameObjectAddObjektToParent(crateId, 61, 4);
    CHECK(a >= 0 && b >= 0);

    int liveBefore = g_sceneNodeCount;
    // Remove the crate by prototype from the warehouse head -> frees crate + 2.
    int* head = reinterpret_cast<int*>(
        reinterpret_cast<u8*>(&g_sceneNodes[wh]) + kObChildHead);
    CHECK_EQ(GameObjectRemoveByProt(head, 200), 0);
    CHECK_EQ(ObGetPrototype(crate), (i16)0);
    CHECK_EQ(ObGetPrototype(a), (i16)0);
    CHECK_EQ(ObGetPrototype(b), (i16)0);
    CHECK_EQ(g_sceneNodeCount, liveBefore - 3);
    CHECK_EQ(ChildHead(wh), -1);                // warehouse now empty
}
