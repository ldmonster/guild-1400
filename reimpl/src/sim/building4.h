#pragma once
// Building default-object / worker-capacity / storage-room / occupant-sync slice
// for the Guild simulation (gilde.exe). MODULE: buildings, prefix VIBE_Building_*
// (namespace guild::sim). Fourth companion to building.{h,cpp} / building2.* /
// building3.* — translates a further batch of the still-deferred Building_*
// functions that:
//
//   * seed a building's default child objects from the recovered defaults table
//     (dword_6496A9), and clamp its worker capacities,
//   * locate a building's storable / upgrade-storage object through the scene
//     query subsystem,
//   * attach/remove storage rooms, push an occupant's profession state delta, and
//   * evaluate a "buy this building" handler hit.
//
// The query / scene / person / handler / command subsystems are NOT reconstructed
// here, so every cross-module leaf is routed through an installable Building4Hooks
// struct with INERT default implementations defined in building4.cpp (the
// established Building3Hooks / CutsceneMiscHooks pattern). Tests install their own.
//
// REAL reconstructed siblings wired in (NOT mocked):
//   VIBE_Building_MapTypeToCategory  (building_type.cpp, 0x5878b0) — used verbatim
//     by FindStorableObject / FindUpgradeStorage to classify the building's +0 byte.
//   VIBE_Building_IsProductionType   (building_type.cpp, 0x587f80) — the production
//     predicate FindStorableObject branches on.
//
// Building records ("char* a1" in the originals) are the live 169-byte object
// records; only the fields actually read are documented per-function. The big
// game-state arrays are exposed as raw base pointers (null base => the originals'
// null-table behaviour) where indexed directly.
//
// Translated functions (gilde.exe addr):
//   VIBE_Building_EnsureDefaultObjects   0x586df8
//   VIBE_Building_InitWorkerCapacities   0x586ed8
//   VIBE_Building_FindStorableObject     0x5877ac
//   VIBE_Building_FindUpgradeStorage     0x58a294
//   VIBE_Building_AttachStorageRooms     0x588554
//   VIBE_Building_RemoveStorageRoom      0x588ce4
//   VIBE_Building_SyncProfessionState    0x58a154
//   VIBE_Building_EvalBuyBuilding        0x46c97c
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from gilde.exe).
// ===========================================================================
// gilde.exe dword_6496A9 — the building default-objects table read by
// EnsureDefaultObjects. 23 records of 21 bytes. The original addresses it as a
// dword/word base: record N's +3 byte (= *(int*)(&dword_6496A9 + 21*N) >> 24) is
// the BUILDING-TYPE byte to match against the building's +0; the TEN WORDs at
// record offset +4 (word_6496AD, i.e. &dword_6496A9 + 21*N + 4) are the child
// object proto-ids to ensure (a 0 word means "no object in this slot"). The
// original's inner loop spans ecx in [21*N, 21*N+20), i.e. WORDs at record
// offsets +4,+6,...,+22 — the last two of which spill into the following
// record's first bytes (verbatim quirk). The first non-zero proto for most
// records sits at +8 (the +4/+6 slots are commonly 0). The last record (#22)
// carries a single proto (0x1CD at +8). The stored table is padded with two
// trailing bytes so the last record's +22/+23 read stays in bounds.
constexpr int kDefaultObjRecords = 23;
constexpr int kDefaultObjStride  = 21;   // bytes per record
constexpr int kDefaultObjTypeOff = 3;    // type byte within a record (+3)
constexpr int kDefaultObjProtoOff = 4;   // first proto WORD within a record (+4)
constexpr int kDefaultObjProtoCount = 10; // proto WORD slots (offsets +4..+22)
extern const std::uint8_t kDefaultObjectsTable[kDefaultObjRecords * kDefaultObjStride + 2];

// ===========================================================================
// Cross-module hooks (default inert). Every callee with no reconstructed target
// is routed here; tests install a mock.
// ===========================================================================
struct Building4Hooks {
    virtual ~Building4Hooks() = default;

    // gilde.exe 0x5857fc — VIBE_GameObject_QueryFind: find an object linked to
    // `containerHandle` matching (groupA, groupB, flag, protoId). Returns an
    // opaque object handle (0 = none). Inert: 0.
    virtual std::int32_t GameObjectQueryFind(std::int32_t containerHandle,
                                             int groupA, int groupB, int flag,
                                             std::int32_t protoId) {
        (void)containerHandle; (void)groupA; (void)groupB; (void)flag;
        (void)protoId; return 0;
    }
    // gilde.exe 0x58529c — VIBE_GameObject_IterNext: advance the most recent
    // QueryFind iteration; 0 = end.
    virtual std::int32_t GameObjectIterNext() { return 0; }

    // gilde.exe (VIBE_GameObject_AddObjekt) — create a child object of `proto`
    // under `parentHandle` with flags (`groupA`), variant `variant`. Returns the
    // new object handle (0 = failed). Inert: 0.
    virtual std::int32_t GameObjectAddObjekt(std::int32_t parentHandle,
                                             std::int32_t proto, int groupA,
                                             std::int32_t variant) {
        (void)parentHandle; (void)proto; (void)groupA; (void)variant; return 0;
    }

    // gilde.exe 0x585af0/0x588ce4-leaf — VIBE_GameObject_RemoveByProt(&rec[93],
    // proto, slot): remove a child object by proto-id; returns the engine status
    // (-2 == "not present"). Inert: 0 (== removed OK).
    virtual std::int32_t GameObjectRemoveByProt(std::int32_t childListField,
                                                std::int16_t proto,
                                                std::int32_t slot) {
        (void)childListField; (void)proto; (void)slot; return 0;
    }

    // gilde.exe 0x5880b4 — VIBE_Building_BuildFlagNodeList(objHandle, ownerWord):
    // build the scene flag-node list for an attached storage room. Returns the
    // engine status byte. Inert: 0.
    virtual std::int32_t BuildFlagNodeList(std::int32_t objHandle,
                                           std::uint16_t ownerWord) {
        (void)objHandle; (void)ownerWord; return 0;
    }

    // gilde.exe (VIBE_Person_QueryByGoodType): resolve the active person's good
    // store of category `goodType` within scene context `ctx`; returns the person
    // record pointer (null = none). Inert: null.
    virtual const std::uint8_t* PersonQueryByGoodType(int goodType,
                                                      std::int32_t ctx) {
        (void)goodType; (void)ctx; return nullptr;
    }

    // gilde.exe 0x494* — VIBE_Command_BeginDeltaPacket(rec, objId): open a delta
    // packet for the record's networked state.
    virtual void CommandBeginDeltaPacket(std::int32_t rec, std::int32_t objId) {
        (void)rec; (void)objId;
    }
    // VIBE_Command_AppendCopiedField(group, count, src, fieldOff): append a copied
    // field to the open delta packet.
    virtual void CommandAppendCopiedField(std::uint32_t group, std::uint32_t count,
                                          const void* src, int fieldOff) {
        (void)group; (void)count; (void)src; (void)fieldOff;
    }
    // VIBE_Command_QueueRequestState23(): flush/queue the open delta packet.
    virtual void CommandQueueRequestState23() {}

    // gilde.exe 0x4c63f8/0x4c6278 — VIBE_He_FindFirstHandlerByFilter /
    // _FindNextMatchingHandler: handler-entity enumerator used by EvalBuyBuilding.
    virtual const std::uint32_t* HeFindFirstHandlerByFilter(int a, int b, int c) {
        (void)a; (void)b; (void)c; return nullptr;
    }
    virtual const std::uint32_t* HeFindNextMatchingHandler() { return nullptr; }

    // gilde.exe 0x588798 — VIBE_Building_EnqueueBuyBuilding(buildingId, buyerId,
    // handler, handlerField44, sellerId, finalFlag): op-coded purchase. Itself a
    // command-heavy leaf, so it stays a hook. The matched handler is forwarded as
    // a pointer (the original passes it in ecx). Inert: no-op.
    virtual void EnqueueBuyBuilding(std::int32_t buildingId, std::int32_t buyerId,
                                    const std::uint32_t* handler,
                                    std::int32_t handlerField44,
                                    std::int32_t sellerId, int finalFlag) {
        (void)buildingId; (void)buyerId; (void)handler; (void)handlerField44;
        (void)sellerId; (void)finalFlag;
    }
};
void SetBuilding4Hooks(Building4Hooks* hooks);
Building4Hooks* Building4HooksGet();

// ===========================================================================
// Default-object seeding / worker-capacity clamp.
// ===========================================================================
// gilde.exe 0x586df8 — VIBE_Building_EnsureDefaultObjects(building)
//   For each defaults-table record whose +3 type byte equals the building's +0
//   type byte: ensure a proto-255 "room" object exists under the building's
//   container (rec+93) — creating one (GameObjectAddObjekt(rec+1,255,1,recIdx))
//   if absent — then ensure each non-zero proto WORD of the record exists as a
//   child of that room object (querying its sub-list, adding when missing). The
//   container handle is read from `building`+93, the building object id from +1.
//   Returns the last engine handle touched (the original's `result`).
std::int32_t Building4_EnsureDefaultObjects(const std::uint8_t* building);

// gilde.exe 0x586ed8 — VIBE_Building_InitWorkerCapacities(building)
//   Locate the building's two worker objects (proto 42 and proto 278) under its
//   container and write their +28/+29 capacity bytes from the building-type
//   record's worker fields. `recBase42`/`recBase278` are the resolved worker
//   object record pointers (null = absent), and `workerCfg` supplies the three
//   capacity bytes the original reads at type-record +573/+574/+575 (passed in to
//   keep the function pure). On a record found, writes its +28 (and +29 for the
//   proto-42 worker) capacity bytes per the original's min-of-2 clamp.
//   `cfg573/cfg574/cfg575` are those three type bytes.
struct WorkerCfg {
    std::uint8_t cfg573 = 0;   // primary worker count A
    std::uint8_t cfg574 = 0;   // primary worker count B
    std::uint8_t cfg575 = 0;   // fallback single-capacity
};
void Building4_InitWorkerCapacities(std::uint8_t* recBase42,
                                    std::uint8_t* recBase278,
                                    const WorkerCfg& cfg);

// ===========================================================================
// Storage-object finders (wired against the REAL MapTypeToCategory sibling).
// ===========================================================================
// gilde.exe 0x5877ac — VIBE_Building_FindStorableObject(building)
//   The building's +0 byte selects a UI category via the genuine
//   Building_MapKindToCategory; production buildings query proto 253 in their
//   container (rec+93), kind-4 buildings query proto 84, otherwise iterate the
//   container's group-4 objects skipping proto 253. Returns the found object
//   handle (0 = none). `kindByte` is the building's +0 byte; `container` is its
//   +93 handle. `isProduction` is computed from the SAME +0 byte through the real
//   Building_IsProductionKind sibling internally.
std::int32_t Building4_FindStorableObject(std::uint8_t kindByte,
                                          std::int32_t container);

// gilde.exe 0x58a294 — VIBE_Building_FindUpgradeStorage(building, ctx)
//   Classify the building's +0 byte via the genuine Building_MapKindToCategory:
//   category 3 -> the active person's good store (PersonQueryByGoodType(1)) then
//   its container's proto-277 object; category 5 -> map the active person's class
//   byte (byte_12CEA79[536*word_63CC5C]) {30->2,31->3,32->4,33->5} to a good type,
//   resolve that store and its container's proto-322 object; any other category
//   returns 0. `activePersonClass` is the resolved person class byte; `ctx` is the
//   scene context forwarded to PersonQueryByGoodType.
std::int32_t Building4_FindUpgradeStorage(std::uint8_t kindByte,
                                          std::uint8_t activePersonClass,
                                          std::int32_t ctx);

// ===========================================================================
// Storage-room attach / remove (routed through the hooks).
// ===========================================================================
// gilde.exe 0x588554 — VIBE_Building_AttachStorageRooms(building, owner)
//   Only for storage-kind buildings (type-record +0 == 2): collect every group-4
//   sub-object of the building's container (rec+93) then call BuildFlagNodeList on
//   each with the building's +39 owner word. Returns the last engine handle.
//   `isStorageKind` is the type-record +0 == 2 test resolved by the caller; the
//   building's +93 container and +39 owner word are read here.
std::int32_t Building4_AttachStorageRooms(const std::uint8_t* building,
                                          bool isStorageKind);

// gilde.exe 0x588ce4 — VIBE_Building_RemoveStorageRoom(building, proto, slot)
//   Remove a storage-room object by proto from the building's container
//   (&rec[93]); returns -3 when the engine reports "not present" (-2), else 0.
std::int32_t Building4_RemoveStorageRoom(std::int32_t childListField,
                                         std::int16_t proto, std::int32_t slot);

// ===========================================================================
// Occupant profession-state sync (routed through the Command hooks).
// ===========================================================================
// gilde.exe 0x58a154 — VIBE_Building_SyncProfessionState(rec, objId)
//   Derives a profession code from the occupant's linked work object (rec[91]):
//   object kind 32 -> 12, kind 31 -> 11, otherwise the occupant's +354 byte
//   (= *(int*)(rec+353) >> 24) minus 1. Pushes that as a copied field at offset
//   357 in one delta packet, then a zero at offset 372 in a second. The two field
//   pushes are observable through the Command hooks. `workKind` is *(u8*)rec[91]
//   (0xFFFF/absent -> 0xFF sentinel), `defaultProf` is the +354 byte.
void Building4_SyncProfessionState(std::int32_t rec, std::int32_t objId,
                                   int workKind, std::uint8_t defaultProf);

// ===========================================================================
// "Buy this building" handler evaluation (routed through the hooks).
// ===========================================================================
// gilde.exe 0x46c97c — VIBE_Building_EvalBuyBuilding(actor, target)
//   Only when the target object's +0 byte is 4 (a building plot): scan the
//   handler list (filter 1,0,27) for a handler whose +43 dword equals the target
//   object's +4 id; on a hit enqueue the buy command and return 16, else 0.
//   `targetKind` is the target's +0 byte, `targetId` its +4 dword, `actorId` the
//   actor's +4 dword. Returns 16 on a queued purchase, 0 otherwise.
int Building4_EvalBuyBuilding(std::uint8_t targetKind, std::int32_t targetId,
                              std::int32_t actorId);

}  // namespace guild::sim
