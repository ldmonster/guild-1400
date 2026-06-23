#include "sim/object.h"

#include <cstring>

// Faithful 1:1 port of the scene-object (GameObject) record lifecycle from
// gilde.exe. See object.h for the recovered 67-byte record layout and the
// child-head(+20) / sibling(+63) link convention.
//
// Storage reuses the existing flat node array (sim::g_sceneNodes /
// g_sceneNodeCount, from entity.cpp). The original threaded the tree through
// raw 32-bit pointers held in each record's +20 / +63 fields; the reimpl stores
// node INDICES there (-1 == null), mirroring entity.cpp's index-based scene
// model. The control flow below is a 1:1 translation with the pointer
// arithmetic replaced by index math.
//
// dword_6498C0 (the live node count) is sim::g_sceneNodeCount; AddObjekt
// increments it and the remove/free paths decrement it, exactly as the binary.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Raw record accessors. Each reads/writes a byte offset into the 67-byte slot
// via memcpy (the original does unaligned x86 loads/stores). `node` is a slot
// index into g_sceneNodes; callers guarantee 0 <= node < kSceneNodeCapacity.
// ---------------------------------------------------------------------------
namespace {
inline u8* SlotBytes(int node) {
    return reinterpret_cast<u8*>(&g_sceneNodes[node]);
}
inline i16 RdW(int node, int off) {
    i16 v; std::memcpy(&v, SlotBytes(node) + off, sizeof(v)); return v;
}
inline void WrW(int node, int off, i16 v) {
    std::memcpy(SlotBytes(node) + off, &v, sizeof(v));
}
inline i32 RdD(int node, int off) {
    i32 v; std::memcpy(&v, SlotBytes(node) + off, sizeof(v)); return v;
}
inline void WrD(int node, int off, i32 v) {
    std::memcpy(SlotBytes(node) + off, &v, sizeof(v));
}
// Unaligned word read of an ObjectRec field by byte offset (faction +37 etc.).
inline u16 ObjWord(const ObjectRec* o, int off) {
    u16 v; std::memcpy(&v, reinterpret_cast<const u8*>(o) + off, sizeof(v)); return v;
}
} // namespace

i16  ObGetPrototype(int node)        { return RdW(node, kObPrototype); }
void ObSetPrototype(int node, i16 v) { WrW(node, kObPrototype, v); }
i32  ObGetId(int node)               { return RdD(node, kObId); }
void ObSetId(int node, i32 v)        { WrD(node, kObId, v); }
i32  ObGetLocation(int node)         { return RdD(node, kObLocation); }
void ObSetLocation(int node, i32 v)  { WrD(node, kObLocation, v); }
i32  ObGetOwner(int node)            { return RdD(node, kObOwner); }
void ObSetOwner(int node, i32 v)     { WrD(node, kObOwner, v); }
i32  ObGetAmount(int node)           { return RdD(node, kObAmount); }
void ObSetAmount(int node, i32 v)    { WrD(node, kObAmount, v); }
i32  ObGetChildHead(int node)        { return RdD(node, kObChildHead); }
void ObSetChildHead(int node, i32 v) { WrD(node, kObChildHead, v); }
i32  ObGetSibling(int node)          { return RdD(node, kObSibling); }
void ObSetSibling(int node, i32 v)   { WrD(node, kObSibling, v); }

// ---------------------------------------------------------------------------
// Command hook.
// ---------------------------------------------------------------------------
static ObjectCommandFn g_objCmdHook = nullptr;
void ObjectSetCommandHook(ObjectCommandFn fn) { g_objCmdHook = fn; }
static void EmitCmd(ObjectCmd cmd, int node, i16 prototype, i32 location,
                    i32 amountDelta) {
    if (g_objCmdHook)
        g_objCmdHook(cmd, node, prototype, location, amountDelta);
}

// Global object-id counter (gilde.exe dword_649890). Each created node takes the
// current value, then it is incremented.
static i32 g_nextObjectId = 0;  // dword_649890

// ---------------------------------------------------------------------------
// Render / Character / Universe leaves invoked by AddObjekt's prototype-specific
// branches (cart & horse model creation, avatar attach, universe slot switch).
// These are forward-declared as no-op stubs here; a host links real backends.
// They are DEFERRED (see module report) — they touch the render/scene clusters.
// ---------------------------------------------------------------------------
namespace {
void StubSpawnTransportModel(int /*node*/, i16 /*prototype*/) {}
void StubAttachAvatar(int /*node*/) {}
} // namespace

// ===========================================================================
// gilde.exe 0x585980 — VIBE_GameObject_FindFreeSlot.
//   v1 = 0; while (!found && v1 < 548864) { if (*(WORD*)(base+v1)) v1+=67;
//                                           else found = base+v1; }
//   548864 == 67 * 8192.
// ===========================================================================
int GameObjectFindFreeSlot() {
    for (int i = 0; i < kSceneObjectCapacity; ++i) {
        if (ObGetPrototype(i) == 0)
            return i;
    }
    return -1;  // array full (original returns 0 == null)
}

// Helper: resolve a `location` entity id to the index slot that holds its
// child-list head, mirroring the per-kind dispatch in AddObjekt /
// AddObjektToParent / Remove*. ResolveEntityById returns 3=person, 1=object,
// 2=scene. The original reads the head pointer from a kind-specific offset; in
// the reimpl all containers expose a child-list-head SLOT. Persons/objects do
// not live in g_sceneNodes, so their "child head" is a per-entity head cell we
// keep alongside; scene nodes use their own +20 field.
//
// For the faithful tree model we support the common case the tests exercise:
// a scene-object container (the building/object node itself is a scene node,
// resolved as kind 2) whose child head is its +20 field. When `location`
// resolves to a scene node we return &that node's +20 via the index protocol:
// we return the node index and set *isSceneHead=true; the caller then uses
// ObGet/SetChildHead(node). For person/object containers (kinds 3/1) we fall
// back to a side head table keyed by entity id (the original stored the head in
// the person/object record itself at +93 / +20).
struct ContainerHead {
    int  sceneNode;   // index of the scene node owning the head (-1 if side)
    int  sideKey;     // entity id for the side table (-1 if scene)
};

// Side child-head table for person/object containers (their head lived inside
// the 536/169-byte record in the original; we keep it here keyed by id so the
// scene-object module stays self-contained).
namespace {
constexpr int kMaxContainers = 1024;
struct SideHead { i32 id; int head; bool used; };
SideHead g_sideHeads[kMaxContainers];

int* SideHeadCell(i32 id) {
    int free = -1;
    for (int i = 0; i < kMaxContainers; ++i) {
        if (g_sideHeads[i].used && g_sideHeads[i].id == id)
            return &g_sideHeads[i].head;
        if (free < 0 && !g_sideHeads[i].used)
            free = i;
    }
    if (free < 0)
        return nullptr;
    g_sideHeads[free].used = true;
    g_sideHeads[free].id = id;
    g_sideHeads[free].head = -1;
    return &g_sideHeads[free].head;
}
} // namespace

void ObjectResetContainerHeads() {
    for (int i = 0; i < kMaxContainers; ++i) {
        g_sideHeads[i].used = false;
        g_sideHeads[i].id = 0;
        g_sideHeads[i].head = -1;
    }
    g_nextObjectId = 0;
}

// Resolve `location` to a pointer to its child-list-head cell (an int holding a
// node index, -1 == empty). Returns nullptr if the location cannot be resolved.
static int* ResolveContainerHead(i32 location) {
    if (location == -1)
        return nullptr;
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person*    person = nullptr;
    int kind = GameObjectResolveEntityById(&obj, &scene, location, &person);
    if (kind == 0)
        return nullptr;
    if (kind == 2 && scene) {
        // Scene-node container: head is its own +20 field.
        int idx = static_cast<int>(scene - g_sceneNodes);
        return reinterpret_cast<int*>(SlotBytes(idx) + kObChildHead);
    }
    // Person (kind 3) / Object (kind 1): head lives in a side cell keyed by id.
    return SideHeadCell(location);
}

// ===========================================================================
// gilde.exe 0x585aa4 — VIBE_GameObject_FreeChildList.
//   v1 = headField; v2 = *headField;
//   if (!headField) return headField; if (!v2) return -1;
//   do { *v1 = *(v2+63);                       // splice head to next sibling
//        if (*(v2+20)) FreeChildList(v2+20);   // recurse into child's children
//        *(WORD*)v2 = 0; --count; v2 = *v1; } while (v2);
//   return 0;
// ===========================================================================
int GameObjectFreeChildList(int* headField) {
    if (!headField)
        return 0;                      // result (null head field)
    int v2 = *headField;
    if (v2 < 0)                        // *(_DWORD*)result == 0 (empty)
        return -1;
    do {
        *headField = ObGetSibling(v2);        // *v1 = *(v2+63)
        int childHead = ObGetChildHead(v2);   // *(v2+20)
        if (childHead >= 0) {
            int cell = childHead;
            GameObjectFreeChildList(&cell);
            ObSetChildHead(v2, cell);
        }
        ObSetPrototype(v2, 0);                // *(WORD*)v2 = 0
        --g_sceneNodeCount;                   // dword_6498C0--
        v2 = *headField;                      // v2 = *v1
    } while (v2 >= 0);
    return 0;
}

// ===========================================================================
// gilde.exe 0x585af4 — VIBE_GameObject_AddObjekt (record core).
//
// Faithful core (LABEL_9 -> LABEL_18 -> LABEL_22 path):
//   1. bail if !sceneArrayLoaded or location == -1 or prototype invalid.
//   2. resolve `location`'s child-list head (v56).
//   3. walk the existing sibling chain (v59) to its tail (chasing +63).
//   4. FindFreeSlot -> a4; link a4 onto the tail (or set head if empty).
//   5. init the record: prototype(+0), id(+2 from dword_649890++),
//      location(+6 = a1), amount(+14 = a3), owner(+10 resolved), fill(+18=100),
//      childHead(+20=0), sibling(+63=0), type-specific dwords by prototype.
//   6. prototype-specific render/avatar branches are DEFERRED to stubs.
// ===========================================================================
int GameObjectAddObjekt(i32 location, i16 prototype, i32 amount, int ownerHint) {
    // if (!dword_13CE27C || a1 == -1) return 0;
    if (!g_sceneArrayLoaded || location == -1)
        return -1;

    // Resolve the container and its child-head cell.
    int* head = nullptr;
    if (ownerHint >= 0) {
        // The original threads an explicit head pointer through a4 in its
        // recursive self-call (the embedded 477 child); honor that.
        // ownerHint is the parent NODE index whose +20 is the head cell.
        head = reinterpret_cast<int*>(SlotBytes(ownerHint) + kObChildHead);
    } else {
        head = ResolveContainerHead(location);
        if (!head)
            return -1;             // ResolveEntityById miss -> return 0
    }

    // Resolve owner id for the +10 field (person/object owner of `location`).
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person*    person = nullptr;
    int kind = GameObjectResolveEntityById(&obj, &scene, location, &person);
    i32 ownerId = -1;
    if (kind == 1 && obj)
        ownerId = obj->id;               // object branch: *(object+1) (id @+1)
    else if (kind == 2 && scene)
        ownerId = RdD(static_cast<int>(scene - g_sceneNodes), kObOwner); // scene+10
    else if (kind == 3 && person)
        ownerId = person->id;            // person branch: *(person+4) (id)

    // Walk the sibling chain to its tail and obtain a free slot.
    int newNode = GameObjectFindFreeSlot();
    if (newNode < 0)
        return -1;
    if (*head < 0) {
        *head = newNode;                 // empty list: head = new node
    } else {
        int tail = *head;
        while (ObGetSibling(tail) >= 0)  // chase +63 to the tail
            tail = ObGetSibling(tail);
        ObSetSibling(tail, newNode);     // *(tail+63) = new node
    }

    // Initialize the record (LABEL_18 / LABEL_22 field stores).
    ObSetPrototype(newNode, prototype);              // *a4 = prot
    ObSetId(newNode, g_nextObjectId);                // *(a4+2) = dword_649890
    ++g_nextObjectId;                                // dword_649890++
    ObSetLocation(newNode, location);                // *(a4+6) = a1
    ObSetOwner(newNode, ownerId);                    // *(a4+10) = ownerId
    ObSetAmount(newNode, amount);                    // *(a4+14) = a3
    SlotBytes(newNode)[kObFill]     = 100;           // *(a4+18) = 100
    SlotBytes(newNode)[kObFill + 1] = 0;             // *(a4+19) = 0
    ObSetChildHead(newNode, -1);                     // *(a4+20) = 0
    ObSetSibling(newNode, -1);                       // *(a4+63) = 0
    ++g_sceneNodeCount;                              // dword_6498C0++

    // Prototype-specific type-field block (gilde.exe 0x585c79..0x586297, the
    // big jump table on the prototype dispatched right after SetGrayColorThunk).
    //
    // BOUNDARY (DEFERRED): every arm of this dispatch is dominated by render /
    // Character / Universe leaves and/or by the loaded prototype-definition
    // table (base dword_13CE27C, stride 65) that is NOT present in the static
    // call tree:
    //
    //   * +0x1C/+0x20/+0x24/+0x28 type-field stores depend on the prototype
    //     class (184 -> +0x1C=6; 0x12C -> -1,-1,-1,(qword_13CE852-1); 0x12D ->
    //     -1*4; 0x15E/0x174/0x176 -> +0x20=15 or 6) — pure record writes, but
    //     keyed on prototype classes that no test exercises and that read
    //     loaded data (qword_13CE852).
    //   * 308 / 0x134-0x136 / 310 (carts & horse) run the LABEL_35 path:
    //     ResolveOwnerOrParentB, an embedded AddObjekt(.,477,1,.) child whose
    //     amount(+14)=*a4-307 and +28=*a4-51, Person_QueryBegin, Character_*,
    //     Universe_SwitchActiveSlot, Object_AttachToUniverseNode, RandNext-based
    //     model-name selection — all render/scene leaves (rules 3-5 territory).
    //   * The universal LABEL_22 tail (0x585c95): v7 = *(dword_13CE27C +
    //     65 * prototype) (typedef category byte); if it is 23/32/37 the +0x1C
    //     field is zeroed. Reads the prototype-definition table (not in tree).
    //
    // The deterministic record core above (prototype/id/location/owner/amount/
    // fill/childHead/sibling + the live-count bump) is faithful; the dispatch
    // body is intentionally NOT reconstructed here to avoid a cheap analogue
    // (rule 8). The previous revision fabricated a 308 amount-set and a partial
    // 477-child spawn that do not match the binary; both are removed.
    (void)&StubSpawnTransportModel;
    (void)&StubAttachAvatar;

    EmitCmd(ObjectCmd::kAdd, newNode, prototype, location, amount);
    return newNode;
}

// ===========================================================================
// gilde.exe 0x5862a4 — VIBE_GameObject_AddObjektToParent.
//   if (!prot) return 0;
//   if (!ResolveEntityById(...)) { log("Parent not found"); return 0; }
//   head = <kind-specific child head>;
//   existing = QueryFind(*head, id-filter, owner=0, prot);  // sibling search
//   if (existing) { *(existing+14) += amount; return existing; }
//   else          return AddObjekt(location, prot, amount, head-node);
// ===========================================================================
int GameObjectAddObjektToParent(i32 location, i16 prototype, i32 amount) {
    if (prototype == 0)
        return -1;
    int* head = ResolveContainerHead(location);
    if (!head)
        return -1;                      // "Parent not found"
    // Search the existing child list for a same-prototype stack.
    int n = (head ? *head : -1);
    while (n >= 0) {
        if (ObGetPrototype(n) == prototype) {
            ObSetAmount(n, ObGetAmount(n) + amount);    // *(node+14) += amount
            EmitCmd(ObjectCmd::kAddAmount, n, prototype, location, amount);
            return n;
        }
        n = ObGetSibling(n);
    }
    return GameObjectAddObjekt(location, prototype, amount, -1);
}

// ===========================================================================
// gilde.exe 0x5859b4 — VIBE_GameObject_RemoveByProt.
//   if (!a1) return -1; v5 = *a1; if (!v5) return -1;
//   v6 = 0; while (*v5 != prot) { a3 = v5; v5 = *(v5+63); ++v6; if (!v5) break; }
//   if (!v5) return -2;
//   if (*(v5+20)) FreeChildList(v5+10? .. real +20 child head);
//   if (v6) *(a3+63) = *(v5+63); else *a1 = *(v5+63);
//   *(WORD*)v5 = 0; --count; return 0;
// (RemoveByProt frees the child list at v5+10 in the decompile; that maps to
//  the +20 child-head index in the reimpl's index model — see header note.)
// ===========================================================================
int GameObjectRemoveByProt(int* headField, i16 prototype) {
    if (!headField)
        return -1;
    int v5 = *headField;
    if (v5 < 0)
        return -1;
    int prev = -1;
    int count = 0;
    while (ObGetPrototype(v5) != prototype) {
        prev = v5;
        v5 = ObGetSibling(v5);     // *(v5+63)
        ++count;
        if (v5 < 0)
            break;
    }
    if (v5 < 0)
        return -2;
    if (ObGetChildHead(v5) >= 0) {          // *(v5+20) != 0
        int cell = ObGetChildHead(v5);
        GameObjectFreeChildList(&cell);
        ObSetChildHead(v5, cell);
    }
    if (count) {
        ObSetSibling(prev, ObGetSibling(v5));   // *(a3+63) = *(v5+63)
    } else {
        *headField = ObGetSibling(v5);          // *a1 = *(v5+63)
    }
    EmitCmd(ObjectCmd::kRemove, v5, prototype, ObGetLocation(v5), 0);
    ObSetPrototype(v5, 0);                       // *(WORD*)v5 = 0
    --g_sceneNodeCount;
    return 0;
}

// ===========================================================================
// gilde.exe 0x585a30 — VIBE_GameObject_RemoveById (matches id @+2).
// ===========================================================================
int GameObjectRemoveById(int* headField, i32 id) {
    if (!headField)
        return -1;
    int v5 = *headField;
    if (v5 < 0)
        return -1;
    int prev = -1;
    int count = 0;
    while (id != ObGetId(v5)) {        // *(v5+2)
        prev = v5;
        v5 = ObGetSibling(v5);
        ++count;
        if (v5 < 0)
            break;
    }
    if (v5 < 0)
        return -2;
    if (ObGetChildHead(v5) >= 0) {     // *(v5+20)
        int cell = ObGetChildHead(v5);
        GameObjectFreeChildList(&cell);
        ObSetChildHead(v5, cell);
    }
    if (count) {
        ObSetSibling(prev, ObGetSibling(v5));
    } else {
        *headField = ObGetSibling(v5);
    }
    EmitCmd(ObjectCmd::kRemove, v5, ObGetPrototype(v5), ObGetLocation(v5), 0);
    ObSetPrototype(v5, 0);
    --g_sceneNodeCount;
    return 0;
}

// ===========================================================================
// gilde.exe 0x5863b4 — VIBE_GameObject_RemoveObjektAmount.
//   if (!ResolveEntityById(...)) return result;
//   head = <kind-specific child head>;
//   node = QueryFind(*head, ..., prot);  // sibling search
//   if (node) { v6 = *(node+14);
//               if (amount > v6) return 0;          // over-draw
//               *(node+14) = v6 - amount;
//               if (*(node+14) <= 0) { RemoveByProt(head, prot); return 1; }
//               return 1; }
// ===========================================================================
int GameObjectRemoveObjektAmount(i32 location, i16 prototype, i32 amount) {
    ObjectRec* obj = nullptr; SceneNode* scene = nullptr; Person* person = nullptr;
    int kind = GameObjectResolveEntityById(&obj, &scene, location, &person);
    if (kind == 0)
        return 0;
    int* head = ResolveContainerHead(location);
    if (!head)
        return 0;
    int node = -1;
    for (int n = *head; n >= 0; n = ObGetSibling(n)) {
        if (ObGetPrototype(n) == prototype) { node = n; break; }
    }
    if (node < 0)
        return kind;                  // result (resolve code) on miss
    i32 v6 = ObGetAmount(node);
    if (amount > v6)
        return 0;                     // requested more than stocked
    ObSetAmount(node, v6 - amount);
    EmitCmd(ObjectCmd::kRemoveAmount, node, prototype, location, amount);
    if (ObGetAmount(node) <= 0) {
        GameObjectRemoveByProt(head, prototype);
        return 1;
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x586458 — VIBE_GameObject_DecrementObjektStock.
//   node = QueryFind(*head, ..., prot);
//   if (!node) { node = AddObjekt(location, prot, 1, ...); *(node+14) = 0; }
//   *(node+14) -= amount;
//   if (!*(node+14)) RemoveByProt(head, prot);
//   return 1;
// ===========================================================================
int GameObjectDecrementObjektStock(i32 location, i16 prototype, i32 amount) {
    ObjectRec* obj = nullptr; SceneNode* scene = nullptr; Person* person = nullptr;
    int kind = GameObjectResolveEntityById(&obj, &scene, location, &person);
    if (kind == 0)
        return 0;
    int* head = ResolveContainerHead(location);
    if (!head)
        return 0;
    int node = -1;
    for (int n = *head; n >= 0; n = ObGetSibling(n)) {
        if (ObGetPrototype(n) == prototype) { node = n; break; }
    }
    if (node < 0) {
        node = GameObjectAddObjekt(location, prototype, 1, -1);
        if (node < 0)
            return 1;             // creation failed; matches "return 1" tail
        ObSetAmount(node, 0);     // *(node+14) = 0
        // re-resolve head: AddObjekt may have set it on an empty list.
        head = ResolveContainerHead(location);
        if (!head)
            return 1;
    }
    ObSetAmount(node, ObGetAmount(node) - amount);     // *(node+14) -= amount
    EmitCmd(ObjectCmd::kRemoveAmount, node, prototype, location, amount);
    if (ObGetAmount(node) == 0)
        GameObjectRemoveByProt(head, prototype);
    return 1;
}

// ===========================================================================
// Owner / parent / root resolution helpers.
// ===========================================================================

// gilde.exe 0x5916d0 — VIBE_GameObject_ResolveOwnerOrParentA.
//   ResolveEntityById(&obj, 0, *(node+10), &person);
//   if (person) return person;
//   if (!obj || *(obj+37)==0xFFFF) return 0;
//   return &word_12CE910[268 * *(obj+37)];   // person at index = faction id
Person* GameObjectResolveOwnerOrParentA(int node) {
    i32 ownerId = ObGetOwner(node);                  // *(node+10)
    ObjectRec* obj = nullptr; Person* person = nullptr;
    GameObjectResolveEntityById(&obj, nullptr, ownerId, &person);
    if (person)
        return person;
    if (!obj)
        return nullptr;
    u16 faction = ObjWord(obj, 37);
    if (faction == 0xFFFF)
        return nullptr;
    if (faction >= kPersonCapacity)
        return nullptr;
    return &g_persons[faction];
}

// gilde.exe 0x591730 — VIBE_GameObject_ResolveOwnerOrParentB (uses +39).
Person* GameObjectResolveOwnerOrParentB(int node) {
    i32 ownerId = ObGetOwner(node);
    ObjectRec* obj = nullptr; Person* person = nullptr;
    GameObjectResolveEntityById(&obj, nullptr, ownerId, &person);
    if (person)
        return person;
    if (!obj)
        return nullptr;
    u16 owner = ObjWord(obj, 39);
    if (owner == 0xFFFF)
        return nullptr;
    if (owner >= kPersonCapacity)
        return nullptr;
    return &g_persons[owner];
}

// gilde.exe 0x591790 — VIBE_GameObject_ResolveTypeFieldA.
//   ResolveEntityById(&obj, 0, *(node+10), &person);
//   if (person) return *person (marker word +0);
//   if (obj) return *(obj+37);  else return -1;
i16 GameObjectResolveTypeFieldA(int node) {
    i32 ownerId = ObGetOwner(node);
    ObjectRec* obj = nullptr; Person* person = nullptr;
    GameObjectResolveEntityById(&obj, nullptr, ownerId, &person);
    if (person)
        return person->marker;
    if (obj)
        return static_cast<i16>(ObjWord(obj, 37));
    return -1;
}

// gilde.exe 0x591818 — VIBE_GameObject_ResolveRootContainer.
//   v2 = *(node+6); if (v2 == -1) return 0;
//   while (ResolveEntityById(&obj, &scene, v2, &person)) {
//       if (obj) return obj;
//       if (person) return 0;
//       if (scene) v2 = *(scene+6);
//       if (v2 == -1) return 0; }
//   return 0;
ObjectRec* GameObjectResolveRootContainer(int node) {
    i32 v2 = ObGetLocation(node);          // *(node+6)
    if (v2 == -1)
        return nullptr;
    for (;;) {
        ObjectRec* obj = nullptr; SceneNode* scene = nullptr; Person* person = nullptr;
        int kind = GameObjectResolveEntityById(&obj, &scene, v2, &person);
        if (kind == 0)
            return nullptr;
        if (obj)
            return obj;
        if (person)
            return nullptr;
        if (scene) {
            int idx = static_cast<int>(scene - g_sceneNodes);
            v2 = RdD(idx, kObLocation);    // *(scene+6)
        }
        if (v2 == -1)
            return nullptr;
    }
}

// ===========================================================================
// Child-aggregation queries (sum the amount field over a container's children).
// The originals call QueryFind(container, id-filter, owner-match, prot=9) then
// IterNext; with our sibling-chain model that is a walk of the child list
// matching prototype 9 (currency), summing the amount field (+14). We translate
// the loop directly so the module is self-contained and deterministic.
// ===========================================================================

// Walk the sibling chain at `head`, summing amounts of prototype==9 nodes;
// optionally counts them. Mirrors the QueryFind(.., 1, 4, 9)/IterNext loops.
static int SumCurrencyChildren(int head, int* outCount) {
    int sum = 0, count = 0;
    for (int n = head; n >= 0; n = ObGetSibling(n)) {
        if (ObGetPrototype(n) == kObjProtCurrency) {
            sum += ObGetAmount(n);
            ++count;
        }
    }
    if (outCount)
        *outCount = count;
    return sum;
}

// gilde.exe 0x58f1f4 — VIBE_GameObject_CountAtLocation.
int GameObjectCountAtLocation(i32 location) {
    int* head = ResolveContainerHead(location);
    if (!head)
        return 0;
    int count = 0;
    SumCurrencyChildren(*head, &count);
    return count;
}

// gilde.exe 0x58f240 — VIBE_GameObject_SumValuesAtLocation.
int GameObjectSumValuesAtLocation(i32 location) {
    int* head = ResolveContainerHead(location);
    if (!head)
        return 0;
    return SumCurrencyChildren(*head, nullptr);
}

// gilde.exe 0x5914fc — VIBE_GameObject_SumChildMoney (head index supplied).
int GameObjectSumChildMoney(int headIndex) {
    return SumCurrencyChildren(headIndex, nullptr);
}

// gilde.exe 0x591584 — VIBE_GameObject_SumMoneyObjects (person container head).
int GameObjectSumMoneyObjects(int containerHeadIndex) {
    return SumCurrencyChildren(containerHeadIndex, nullptr);
}

// gilde.exe 0x590200 — VIBE_GameObject_CollectStorageBuildings.
//   v4 = QueryFind(0, type==29 flat scan); while (v4 && count<3) {
//       if (*(v4+14) == *(building+1)) { if (out) out[count]=v4; ++count; }
//       v4 = IterNext(); }
// We translate the flat scan over g_sceneNodes for type 29 nodes whose owner
// slot (+14, reused as an owner ref by storage nodes) matches the building id.
int GameObjectCollectStorageBuildings(ObjectRec* building, int* outNodes) {
    if (!building)
        return 0;
    i32 buildingId = building->id;            // *(building+1)
    int count = 0;
    for (int i = 0; i < kSceneObjectCapacity && count < 3; ++i) {
        if (ObGetPrototype(i) != 29)
            continue;
        if (ObGetAmount(i) == buildingId) {   // *(node+14) == buildingId
            if (outNodes)
                outNodes[count] = i;
            ++count;
        }
    }
    return count;
}

} // namespace guild::sim
