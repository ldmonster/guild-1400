// Unit tests for the scene-object (GameObject) record lifecycle.
//   src/sim/object.{h,cpp}  (+ src/sim/entity.cpp, building.cpp deps)
//   src/sim/objectsearch.{h,cpp}  (MatchEntityFilterWithStatus addition)
//
// Covers: FindFreeSlot; AddObjekt create + id assignment + field init;
// AddObjektToParent stack-merge; child/sibling tree linkage; RemoveByProt /
// RemoveById unlink + child-list free; RemoveObjektAmount clamp/over-draw;
// DecrementObjektStock create-then-decrement + remove-at-zero; resolve helpers;
// currency aggregation queries; the WithStatus filter gate.
#include "sim/object.h"
#include "sim/objectsearch.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Fresh world: clear arrays + module globals, mark loaded.
void Reset() {
    ResetEntityArrays();
    ObjectResetContainerHeads();
    ObjectSetCommandHook(nullptr);
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;
    g_sceneNodeCount = 0;
}

// Install a scene-node container at slot `i` (type!=0, id, empty child head).
// Returns the slot index. The container's child-list head is its +20 field.
int PutSceneContainer(int i, i16 type, i32 id) {
    g_sceneNodes[i].type = type;
    g_sceneNodes[i].id = id;
    g_sceneNodes[i].entityPtr = -1;   // +20 child head: empty
    g_sceneNodes[i].childPtr  = -1;   // +63 sibling: none
    // location/owner default fields:
    ObSetLocation(i, -1);
    ObSetOwner(i, -1);
    ObSetAmount(i, 0);
    if (i + 1 > g_sceneNodeCount)
        g_sceneNodeCount = i + 1;
    return i;
}

// Count nodes in a sibling chain.
int ChainLen(int head) {
    int n = 0;
    for (int x = head; x >= 0; x = ObGetSibling(x))
        ++n;
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SimObject, FindFreeSlotSkipsOccupied) {
    Reset();
    // Occupy slots 0..2 (prototype != 0).
    ObSetPrototype(0, 5); ObSetPrototype(1, 9); ObSetPrototype(2, 308);
    CHECK_EQ(GameObjectFindFreeSlot(), 3);
    // Free slot 1 -> it becomes the first free again.
    ObSetPrototype(1, 0);
    CHECK_EQ(GameObjectFindFreeSlot(), 1);
}

TEST(SimObject, AddObjektCreatesAndInits) {
    Reset();
    int container = PutSceneContainer(10, /*type*/100, /*id*/4242);

    int node = GameObjectAddObjekt(/*location*/4242, /*prototype*/700,
                                   /*amount*/25, /*ownerHint*/-1);
    CHECK(node >= 0);
    // Record fields.
    CHECK_EQ(ObGetPrototype(node), (i16)700);
    CHECK_EQ(ObGetId(node), 0);                 // first id from dword_649890 == 0
    CHECK_EQ(ObGetLocation(node), 4242);
    CHECK_EQ(ObGetAmount(node), 25);
    CHECK_EQ(ObGetChildHead(node), -1);
    CHECK_EQ(ObGetSibling(node), -1);
    CHECK_EQ((int)reinterpret_cast<u8*>(&g_sceneNodes[node])[kObFill], 100);
    // Linked as the container's first child.
    CHECK_EQ(g_sceneNodes[container].entityPtr, node);  // +20 head
    int countAfterOne = g_sceneNodeCount;

    // A second object appends to the sibling chain.
    int node2 = GameObjectAddObjekt(4242, 701, 5, -1);
    CHECK(node2 >= 0);
    CHECK_EQ(ObGetId(node2), 1);                 // id counter advanced
    CHECK_EQ(ObGetSibling(node), node2);         // appended after first
    CHECK_EQ(ChainLen(g_sceneNodes[container].entityPtr), 2);
    CHECK_EQ(g_sceneNodeCount, countAfterOne + 1);  // count tracks live nodes
}

TEST(SimObject, AddObjektRejectsBadInput) {
    Reset();
    PutSceneContainer(10, 100, 4242);
    CHECK_EQ(GameObjectAddObjekt(-1, 700, 1, -1), -1);     // location == -1
    g_sceneArrayLoaded = false;
    CHECK_EQ(GameObjectAddObjekt(4242, 700, 1, -1), -1);   // array not loaded
}

TEST(SimObject, AddObjektToParentMergesStacks) {
    Reset();
    int container = PutSceneContainer(10, 100, 7000);

    int a = GameObjectAddObjektToParent(7000, /*prot*/50, /*amount*/10);
    CHECK(a >= 0);
    CHECK_EQ(ObGetAmount(a), 10);
    // Same prototype again -> merges into the existing stack (no new node).
    int b = GameObjectAddObjektToParent(7000, 50, 7);
    CHECK_EQ(b, a);
    CHECK_EQ(ObGetAmount(a), 17);
    CHECK_EQ(ChainLen(g_sceneNodes[container].entityPtr), 1);
    // Different prototype -> a new stack node.
    int c = GameObjectAddObjektToParent(7000, 51, 3);
    CHECK(c >= 0);
    CHECK(c != a);
    CHECK_EQ(ChainLen(g_sceneNodes[container].entityPtr), 2);
}

TEST(SimObject, RemoveByProtUnlinksMiddle) {
    Reset();
    int container = PutSceneContainer(10, 100, 8000);
    int n1 = GameObjectAddObjekt(8000, 100, 1, -1);
    int n2 = GameObjectAddObjekt(8000, 200, 1, -1);
    int n3 = GameObjectAddObjekt(8000, 300, 1, -1);
    CHECK_EQ(ChainLen(g_sceneNodes[container].entityPtr), 3);

    int* head = reinterpret_cast<int*>(
        reinterpret_cast<u8*>(&g_sceneNodes[container]) + kObChildHead);
    // Remove the middle one (prototype 200).
    CHECK_EQ(GameObjectRemoveByProt(head, 200), 0);
    CHECK_EQ(ObGetPrototype(n2), (i16)0);        // slot freed
    CHECK_EQ(ObGetSibling(n1), n3);              // chain spliced
    CHECK_EQ(ChainLen(*head), 2);
    // Removing a non-existent prototype -> -2.
    CHECK_EQ(GameObjectRemoveByProt(head, 999), -2);
    // Remove the head (prototype 100).
    CHECK_EQ(GameObjectRemoveByProt(head, 100), 0);
    CHECK_EQ(*head, n3);
    CHECK_EQ(ChainLen(*head), 1);
}

TEST(SimObject, RemoveByIdAndChildListFree) {
    Reset();
    int container = PutSceneContainer(10, 100, 8100);
    int parent = GameObjectAddObjekt(8100, 100, 1, -1);
    // Give `parent` two children of its own (so removal frees the subtree).
    int childContainerId = ObGetId(parent);
    // parent is itself addressable as a scene node container? Its id resolves
    // via the scene array only if it has type!=0 (it does, prototype 100) and
    // is within the scanned count. Add children directly via the +20 head.
    int* parentHead = reinterpret_cast<int*>(
        reinterpret_cast<u8*>(&g_sceneNodes[parent]) + kObChildHead);
    // Manually attach two child nodes.
    int gc1 = GameObjectFindFreeSlot();
    ObSetPrototype(gc1, 9); ObSetAmount(gc1, 5);
    ObSetChildHead(gc1, -1); ObSetSibling(gc1, -1);
    ++g_sceneNodeCount;
    *parentHead = gc1;
    int gc2 = GameObjectFindFreeSlot();
    ObSetPrototype(gc2, 9); ObSetAmount(gc2, 7);
    ObSetChildHead(gc2, -1); ObSetSibling(gc2, -1);
    ++g_sceneNodeCount;
    ObSetSibling(gc1, gc2);
    (void)childContainerId;

    int* head = reinterpret_cast<int*>(
        reinterpret_cast<u8*>(&g_sceneNodes[container]) + kObChildHead);
    int before = g_sceneNodeCount;
    // Remove `parent` by id -> frees its 2-child subtree as well (3 freed).
    CHECK_EQ(GameObjectRemoveById(head, ObGetId(parent)), 0);
    CHECK_EQ(ObGetPrototype(parent), (i16)0);
    CHECK_EQ(ObGetPrototype(gc1), (i16)0);
    CHECK_EQ(ObGetPrototype(gc2), (i16)0);
    CHECK_EQ(g_sceneNodeCount, before - 3);
}

TEST(SimObject, RemoveObjektAmountClampAndRemove) {
    Reset();
    PutSceneContainer(10, 100, 8200);
    int n = GameObjectAddObjekt(8200, 60, /*amount*/10, -1);
    CHECK(n >= 0);
    // Over-draw: amount > stock -> no change, returns 0.
    CHECK_EQ(GameObjectRemoveObjektAmount(8200, 60, 11), 0);
    CHECK_EQ(ObGetAmount(n), 10);
    // Partial draw.
    CHECK_EQ(GameObjectRemoveObjektAmount(8200, 60, 4), 1);
    CHECK_EQ(ObGetAmount(n), 6);
    // Exact draw to zero -> stack removed.
    CHECK_EQ(GameObjectRemoveObjektAmount(8200, 60, 6), 1);
    CHECK_EQ(ObGetPrototype(n), (i16)0);     // freed
}

TEST(SimObject, DecrementObjektStockCreatesThenRemoves) {
    Reset();
    PutSceneContainer(10, 100, 8300);
    // No stack yet -> created with amount 0, then decremented by 0 stays 0 and
    // is removed (count hits exactly 0).
    CHECK_EQ(GameObjectDecrementObjektStock(8300, 70, 0), 1);
    // Now seed a stack of 5 and decrement by 2.
    int n = GameObjectAddObjekt(8300, 70, 5, -1);
    CHECK(n >= 0);
    CHECK_EQ(GameObjectDecrementObjektStock(8300, 70, 2), 1);
    CHECK_EQ(ObGetAmount(n), 3);
    // Decrement to exactly 0 -> removed.
    CHECK_EQ(GameObjectDecrementObjektStock(8300, 70, 3), 1);
    CHECK_EQ(ObGetPrototype(n), (i16)0);
}

TEST(SimObject, CurrencyAggregation) {
    Reset();
    int container = PutSceneContainer(10, 100, 8400);
    GameObjectAddObjekt(8400, kObjProtCurrency, 100, -1);
    GameObjectAddObjekt(8400, 55, 7, -1);                 // non-currency
    GameObjectAddObjekt(8400, kObjProtCurrency, 250, -1);
    int head = g_sceneNodes[container].entityPtr;
    CHECK_EQ(GameObjectSumChildMoney(head), 350);
    CHECK_EQ(GameObjectSumValuesAtLocation(8400), 350);
    CHECK_EQ(GameObjectCountAtLocation(8400), 2);
}

TEST(SimObject, ResolveRootContainerWalksUp) {
    Reset();
    // object/building root with id 9000.
    g_objects[0].alive = 1; g_objects[0].id = 9000;
    // scene node A: location points at the object 9000.
    PutSceneContainer(20, 100, 5000);
    ObSetLocation(20, 9000);
    // scene node B: located inside A (id 5000).
    PutSceneContainer(21, 100, 5001);
    ObSetLocation(21, 5000);

    ObjectRec* root = GameObjectResolveRootContainer(21);
    CHECK(root != nullptr);
    CHECK_EQ(root->id, 9000);

    // Node whose location is -1 -> nullptr.
    PutSceneContainer(22, 100, 5002);
    ObSetLocation(22, -1);
    CHECK(GameObjectResolveRootContainer(22) == nullptr);
}

TEST(SimObject, ResolveTypeFieldAAndOwner) {
    Reset();
    // person at slot 3, id 600, faction (+37) 4, owner (+39) 7.
    g_persons[3].marker = 0; g_persons[3].id = 600;
    g_persons[3].factionA = 4; g_persons[3].ownerPlayer = 7;
    g_personIds[3] = 600;
    // person 4 / 7 targets for owner-resolution.
    g_persons[4].marker = 0; g_persons[4].id = 44; g_personIds[4] = 44;
    g_persons[7].marker = 0; g_persons[7].id = 77; g_personIds[7] = 77;
    // object at slot 1, id 800, faction (+37)=4, owner(+39)=7.
    g_objects[1].alive = 1; g_objects[1].id = 800;
    const u16 fac = 4, own = 7;
    std::memcpy(reinterpret_cast<u8*>(&g_objects[1]) + 37, &fac, 2);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[1]) + 39, &own, 2);

    // scene node whose owner id (+10) points at the person 600.
    PutSceneContainer(30, 100, 5100);
    ObSetOwner(30, 600);
    CHECK_EQ(GameObjectResolveTypeFieldA(30), g_persons[3].marker);  // person path
    CHECK(GameObjectResolveOwnerOrParentA(30) == &g_persons[3]);

    // scene node whose owner id points at the object 800 -> faction person 4.
    PutSceneContainer(31, 100, 5101);
    ObSetOwner(31, 800);
    CHECK_EQ(GameObjectResolveTypeFieldA(31), (i16)4);              // object faction
    CHECK(GameObjectResolveOwnerOrParentA(31) == &g_persons[4]);   // index 4
    CHECK(GameObjectResolveOwnerOrParentB(31) == &g_persons[7]);   // index 7
}

// ---------------------------------------------------------------------------
// MatchEntityFilterWithStatus
// ---------------------------------------------------------------------------
TEST(SimObject, MatchEntityFilterWithStatusRejectGate) {
    // aiPlayerTable[589*type] gives the faction byte; pick type 0 -> byte 3.
    u8 aiTable[589 * 4] = {};
    aiTable[589 * 0] = 3;   // faction bit 3, which IS in mask 0x0F82806F (bit3=8)
    u8 record[169] = {};
    record[0] = 0;          // type byte 0 -> aiTable index 0 -> faction 3
    const u16 noOwner = 0xFFFF;
    std::memcpy(record + 39, &noOwner, 2);

    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;
    ctx.queryFaction = 1;

    EntityFilter f{};
    f.factionMask = 0;
    f.ownerMode = 1;        // owner == -1 (true here)
    f.requireStatus = 0x40; // bit 0x40 set -> triggers the reject gate

    // bit3 is set in 0x0F82806F, and +11 has 0x40 -> rejected.
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, record), false);

    // Clear the 0x40 bit -> passes (owner==-1 matches mode 1).
    f.requireStatus = 0;
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, record), true);
}

TEST(SimObject, MatchEntityFilterWithStatusNullCases) {
    u8 record[169] = {};
    ObjectSearchContext ctx{};
    // null filter -> always match.
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, nullptr, record), true);
    // null record -> no match.
    EntityFilter f{}; f.ownerMode = 1;
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, nullptr), false);
}
