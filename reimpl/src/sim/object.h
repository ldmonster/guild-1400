#pragma once
// ===========================================================================
// object.{h,cpp} — the scene-object (GameObject) RECORD LIFECYCLE
// ===========================================================================
// MODULE: the Object / GameObject lifecycle (namespace guild::sim).
//
// This is the item/object scene-record system of gilde.exe: the flat
// fixed-stride node array at *(0x13CE290) that the game models as a TREE of
// "Objekt" records (carried goods, stock stacks, transports, contained items).
// Every node is 67 / 0x43 bytes. The tree is threaded with two intrusive
// links inside each record:
//
//   * +0x14 (dword)  childHead   — head of this node's child list (== the
//                                  "entityPtr" field of the existing SceneNode;
//                                  DFS descends here, see IterNextTree @0x585488
//                                  which pushes *(node+20) onto the DFS stack).
//   * +0x3F (dword)  sibling     — next sibling in the parent's child list
//                                  (== the "childPtr" field of SceneNode; the
//                                  remove/walk loops chase node->+63).
//
// (The existing entity.cpp/SceneNode names the +20 field "entityPtr" and the
// +63 field "childPtr"; the live lifecycle code uses +20 as the child-list HEAD
// and +63 as the SIBLING link. This module documents and uses the real roles
// while leaving the SceneNode struct in types.h untouched, accessing the record
// by byte offset exactly as the binary does.)
//
// Reconstructed record layout (67 bytes), recovered byte-for-byte from
// AddObjekt(0x585af4), RemoveByProt(0x5859b4), RemoveById(0x585a30),
// FreeChildList(0x585aa4), RemoveObjektAmount(0x5863b4),
// DecrementObjektStock(0x586458) and the resolve helpers:
//
//   +0x00  u16  prototype / type word (a.k.a. "ObjProt"; 0 == FREE slot)
//   +0x02  i32  unique object id ("ObjID"; assigned from dword_649890)
//   +0x06  i32  location/parent-entity id (the `a1` arg to AddObjekt; the
//               container this object lives in — building/person/object id)
//   +0x0A  i32  owner id (the owning person/faction id)
//   +0x0E  i32  amount / stock count ("Menge"; the stacked quantity)
//   +0x12  u8   fill / quality = 100 at creation
//   +0x13  u8   = 0 at creation
//   +0x14  i32  childHead — head of child list (-1/0 == none)
//   +0x18  ..   transform/color block (Light_SetGrayColor writes a4+14 words ==
//               +0x1C; render leaf, not modeled here)
//   +0x1C  i32  type-specific dword A (set per prototype; +28)
//   +0x20  i32  type-specific dword B (+32)
//   +0x24  i32  type-specific dword C (+36)
//   +0x28  i32  type-specific dword D (+40)
//   +0x3B  i32  attached character/transport ptr (+59; render leaf)
//   +0x3F  i32  sibling — next sibling link (-1/0 == end of list)
//
// Storage model: the reimpl reuses the existing flat node array
// (sim::g_sceneNodes / g_sceneNodeCount from entity.h) and stores node INDICES
// (-1 == null) in the childHead/sibling link fields, mirroring entity.cpp's
// index-based scene model. The original used raw 32-bit pointers; the control
// flow is a 1:1 translation with pointer arithmetic replaced by index math.
//
// Mutations that the binary routes through the lockstep command channel
// (ExCreateGebaeude / ExSellObjekt / ExSetObjectField …) are surfaced through a
// mockable command hook so the CRUD rules are testable in isolation. The render
// / mesh / Character / Universe leaves invoked by AddObjekt's prototype-specific
// branches (cart & horse model creation, avatar attach, universe slot switch)
// are forward-declared as stubs — see object.cpp.
//
// Translated functions:
//   VIBE_GameObject_FindFreeSlot          0x585980
//   VIBE_GameObject_AddObjekt             0x585af4  (record core; render leaves stubbed)
//   VIBE_GameObject_AddObjektToParent     0x5862a4
//   VIBE_GameObject_RemoveByProt          0x5859b4
//   VIBE_GameObject_RemoveById            0x585a30
//   VIBE_GameObject_FreeChildList         0x585aa4
//   VIBE_GameObject_RemoveObjektAmount    0x5863b4
//   VIBE_GameObject_DecrementObjektStock  0x586458
//   VIBE_GameObject_ResolveOwnerOrParentA 0x5916d0
//   VIBE_GameObject_ResolveOwnerOrParentB 0x591730
//   VIBE_GameObject_ResolveTypeFieldA     0x591790
//   VIBE_GameObject_ResolveRootContainer  0x591818
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/entity.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Scene-object record byte-offset map (into the 67-byte SceneNode blob).
// ---------------------------------------------------------------------------
enum ObjectField : int {
    kObPrototype = 0x00,  // u16 type/prototype word (0 == free slot)
    kObId        = 0x02,  // i32 unique id (from dword_649890)
    kObLocation  = 0x06,  // i32 location/parent-entity id (AddObjekt `a1`)
    kObOwner     = 0x0A,  // i32 owner person/faction id
    kObAmount    = 0x0E,  // i32 amount / stock ("Menge")
    kObFill      = 0x12,  // u8  fill/quality (100 at creation)
    kObChildHead = 0x14,  // i32 child-list head (node index; -1 == none)
    kObTypeA     = 0x1C,  // i32 type-specific dword A (+28)
    kObTypeB     = 0x20,  // i32 type-specific dword B (+32)
    kObTypeC     = 0x24,  // i32 type-specific dword C (+36)
    kObTypeD     = 0x28,  // i32 type-specific dword D (+40)
    kObSibling   = 0x3F,  // i32 next-sibling link (node index; -1 == end)
};

// Scene-node array capacity in gilde.exe: FindFreeSlot's scan bound 548864 ==
// 67 * 8192. The reimpl reuses the existing flat node array g_sceneNodes whose
// storage bound is kSceneNodeCapacity (entity.h); scans are capped to that so
// the model never reads OOB. The original geometry is preserved in the comment.
constexpr int kSceneObjectStride       = 67;     // 0x43
constexpr int kSceneObjectOrigCapacity = 8192;   // 548864 / 67
constexpr int kSceneObjectCapacity     = kSceneNodeCapacity;

// Currency prototype id (good 9) — used by the money-summing queries.
constexpr i16 kObjProtCurrency = 9;

// ---------------------------------------------------------------------------
// Raw record accessors (the binary reads these fields unaligned, by byte
// offset). Index `node` is a SLOT INDEX into sim::g_sceneNodes; -1 == null.
// ---------------------------------------------------------------------------
i16  ObGetPrototype(int node);
void ObSetPrototype(int node, i16 v);
i32  ObGetId(int node);
void ObSetId(int node, i32 v);
i32  ObGetLocation(int node);
void ObSetLocation(int node, i32 v);
i32  ObGetOwner(int node);
void ObSetOwner(int node, i32 v);
i32  ObGetAmount(int node);
void ObSetAmount(int node, i32 v);
i32  ObGetChildHead(int node);
void ObSetChildHead(int node, i32 v);
i32  ObGetSibling(int node);
void ObSetSibling(int node, i32 v);

// ---------------------------------------------------------------------------
// Command hook: the binary routes record creation / stock changes through the
// lockstep command system. Tests install a mock to capture the emitted
// mutations; nullptr applies the change directly (single-player/host path,
// dword_764CE0 == -1). Called BEFORE the local apply.
// ---------------------------------------------------------------------------
enum class ObjectCmd { kAdd, kRemove, kAddAmount, kRemoveAmount, kSetAmount };
using ObjectCommandFn = void (*)(ObjectCmd cmd, int node, i16 prototype,
                                 i32 location, i32 amountDelta);
void ObjectSetCommandHook(ObjectCommandFn fn);

// Resets the module's mutable globals: the person/object side child-head table
// and the object-id counter (dword_649890). Not in the original (which keeps
// these inside the entity records / a single global); a test/setup helper.
void ObjectResetContainerHeads();

// ---------------------------------------------------------------------------
// gilde.exe 0x585980 — VIBE_GameObject_FindFreeSlot.
// Scans the node array (stride 67, bound 548864) for the first slot whose type
// word (+0) is 0 (free). Returns the slot index, or -1 if the array is full.
// (The original returns the slot ADDRESS, or 0; we return an index / -1.)
// ---------------------------------------------------------------------------
int GameObjectFindFreeSlot();

// gilde.exe 0x585aa4 — VIBE_GameObject_FreeChildList.
// `headField` is the index slot holding a child-list head. Walks the sibling
// chain from that head, recursively freeing each child's own child list
// (node+20), zeroing each node's type word (+0), and decrementing the live
// count (dword_6498C0). The head field is left pointing at the cleared chain's
// tail (faithful to the original's *v1 = ... loop). Returns 0 on success, -1
// if the head was already empty, and is a no-op for a null head field.
int GameObjectFreeChildList(int* headField);

// gilde.exe 0x585af4 — VIBE_GameObject_AddObjekt (record core).
// Creates a new object node of `prototype`, `amount` units, located in entity
// `location`, and links it as a child of that entity's container. `ownerHint`
// is the optional parent-list head index the original threads through `a4`
// (-1 to resolve from `location`). Returns the new node index, or -1 on
// failure (array full, location unresolved, or prototype/location == -1).
//
// The prototype-specific render/avatar/transport branches (carts 0x134-0x136,
// horse 310/0x136, universe slot switching) are DEFERRED to stubs; the record
// allocation, field init, id assignment, owner resolution, child-list linkage
// and the auto-spawn of the embedded "477" capacity child are faithful.
int GameObjectAddObjekt(i32 location, i16 prototype, i32 amount, int ownerHint);

// gilde.exe 0x5862a4 — VIBE_GameObject_AddObjektToParent.
// Adds `amount` of `prototype` under entity `location`: if a child stack of the
// same prototype already exists, its amount (+14) is increased; otherwise a new
// node is created via AddObjekt. Returns the affected node index, or -1.
int GameObjectAddObjektToParent(i32 location, i16 prototype, i32 amount);

// gilde.exe 0x5859b4 — VIBE_GameObject_RemoveByProt.
// `headField` points at a child-list head index. Walks the sibling chain
// matching prototype word (+0); on a hit, recursively frees that node's child
// list (if its childHead +20 is set), unlinks it from the sibling chain, zeroes
// its type word and decrements the live count. Returns 0 on success, -1 if the
// list is empty / headField null, -2 if no match.
int GameObjectRemoveByProt(int* headField, i16 prototype);

// gilde.exe 0x585a30 — VIBE_GameObject_RemoveById.
// As RemoveByProt but matches the id field (+2) instead of the prototype word.
int GameObjectRemoveById(int* headField, i32 id);

// gilde.exe 0x5863b4 — VIBE_GameObject_RemoveObjektAmount.
// Subtracts `amount` from the matching child stack's count (+14) under entity
// `location`. If the requested amount exceeds the stock, returns 0 (no change).
// If the stock reaches <= 0 the stack is removed via RemoveByProt. Returns 1 on
// success, 0 if over-draw / not found, and the resolve result on miss.
int GameObjectRemoveObjektAmount(i32 location, i16 prototype, i32 amount);

// gilde.exe 0x586458 — VIBE_GameObject_DecrementObjektStock.
// Like RemoveObjektAmount but creates the stack (amount 0) if absent first,
// then subtracts `amount` from the count; when the count hits exactly 0 the
// stack is removed. Returns 1 on success, else the resolve result.
int GameObjectDecrementObjektStock(i32 location, i16 prototype, i32 amount);

// ---------------------------------------------------------------------------
// Owner / parent / root resolution helpers (read node+6 / node+10).
// ---------------------------------------------------------------------------

// gilde.exe 0x5916d0 — VIBE_GameObject_ResolveOwnerOrParentA.
// Resolves the entity referenced by node's owner id (+10). If it is a Person,
// returns that Person. Else if it is an Object with faction (+37) != -1,
// returns the Person whose index == that faction id. Else nullptr.
Person* GameObjectResolveOwnerOrParentA(int node);

// gilde.exe 0x591730 — VIBE_GameObject_ResolveOwnerOrParentB.
// As A but uses the Object's owner word (+39) instead of the faction word.
Person* GameObjectResolveOwnerOrParentB(int node);

// gilde.exe 0x591790 — VIBE_GameObject_ResolveTypeFieldA.
// Resolves node+10's id: Person -> its marker word (+0); Object -> its faction
// word (+37); else -1. (Sibling of GameObjectResolveTypeFieldB which uses +39.)
i16 GameObjectResolveTypeFieldA(int node);

// gilde.exe 0x591818 — VIBE_GameObject_ResolveRootContainer.
// Walks up the container chain from node's location id (+6): repeatedly
// resolves the id; if it resolves to an Object/Building, returns that
// ObjectRec*; if to a scene node, follows that node's location (+6) up; stops
// (returns nullptr) on a Person, a -1 link, or an unresolved id.
ObjectRec* GameObjectResolveRootContainer(int node);

// ---------------------------------------------------------------------------
// Child-aggregation queries (walk a container's children via QueryFind /
// IterNext, summing the amount field). gilde.exe 0x58f1f4 / 0x58f240 /
// 0x5914fc / 0x591584 / 0x590200.
// ---------------------------------------------------------------------------

// gilde.exe 0x58f1f4 — VIBE_GameObject_CountAtLocation.
// Counts the currency (prototype 9) child stacks directly under `location`.
int GameObjectCountAtLocation(i32 location);

// gilde.exe 0x58f240 — VIBE_GameObject_SumValuesAtLocation.
// Sums the amount (+14) of every currency child stack under `location`.
int GameObjectSumValuesAtLocation(i32 location);

// gilde.exe 0x5914fc — VIBE_GameObject_SumChildMoney.
// Sums the amount of currency children whose child-head index is `headIndex`
// (a raw child-list head, not an entity id).
int GameObjectSumChildMoney(int headIndex);

// gilde.exe 0x591584 — VIBE_GameObject_SumMoneyObjects.
// Sums the amount of currency children under the person whose container head
// is at person+376 (here: the head index supplied directly).
int GameObjectSumMoneyObjects(int containerHeadIndex);

// gilde.exe 0x590200 — VIBE_GameObject_CollectStorageBuildings.
// Walks all storage buildings (QueryFind type==29) whose owner field (+14, the
// amount slot reused as an owner ref by storage nodes) matches `building`'s id;
// collects up to 3 node indices into `outNodes` (may be null to just count).
// Returns the number matched (capped at 3).
int GameObjectCollectStorageBuildings(ObjectRec* building, int* outNodes);

} // namespace guild::sim
