#pragma once
// Building occupancy / scene-query / lifecycle-unlink slice for the Guild
// simulation (gilde.exe). MODULE: buildings, prefix VIBE_Building_*
// (namespace guild::sim). Third companion to building.{h,cpp} / building2.* —
// translates a batch of the still-deferred Building_* functions that:
//
//   * read the open-hours / type-name / type-string-id constant tables, and
//   * walk the live scene through the object/person/handler query subsystems
//     (VIBE_GameObject_QueryFind, VIBE_Person_QueryBegin/IterNext,
//     VIBE_He_FindFirstHandlerByFilter/FindNextMatchingHandler), and
//   * unlink / release / destroy building occupants.
//
// The query/scene/memory subsystems are NOT reconstructed in this module, so
// every cross-module leaf is routed through an installable Building3Hooks struct
// with INERT default implementations defined in building3.cpp (the established
// CutsceneMiscHooks / IBuilding2QueryHooks pattern). Tests install their own.
// The one reconstructed sibling actually wired in is VIBE_BuildingType_
// GroupFromCode (building_type.cpp, gilde.exe 0x58a4c8), used verbatim by
// Building3_UpdateOccupantCategory.
//
// The big game-state arrays this module indexes are exposed as raw base
// pointers via SetBuilding3Arrays() (null base => the originals' null-table
// behaviour). Building records ("char* a1" in the originals) are passed by
// pointer to the live 589-stride record; only the fields actually read are
// documented per-function below.
//
// Translated functions (gilde.exe addr):
//   VIBE_Building_CheckTimeWindowOpen        0x51dc04
//   VIBE_Building_LookupTypeName             0x50c738
//   VIBE_Building_LookupTypeStringId         0x587fcc
//   VIBE_Building_FindWorkProductObject      0x587674
//   VIBE_Building_FindActiveWorkSlot         0x586904
//   VIBE_Building_FindNearestSameType        0x587908
//   VIBE_Building_FindNearestVacantSameType  0x587a28
//   VIBE_Building_UpdateOccupantCategory     0x58a044
//   VIBE_Building_FreeAndUnlink              0x586d6c
//   VIBE_Building_ReleaseOccupantHoldings    0x589468
//   VIBE_Building_DetachAndDestroyOccupant   0x588d00
//   VIBE_Building_CollectByCityHandle        0x591870
//   VIBE_Building_CollectOwnedByPerson       0x5918e0
//   VIBE_Building_ResetAllBuildings          0x5896fc
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from gilde.exe).
// ===========================================================================
// gilde.exe dword_63C708 (records start at 0x63C70D) — building open-hours
// table read by CheckTimeWindowOpen. A flat array of 3-byte records
// {typeCode, openHour, closeHour}; iteration stops at the first record whose
// FOLLOWING record's typeCode byte is 0 (the original's `byte_63C710[eax]`
// continue-sentinel == next record's type byte). byte_63C70D (the +5 byte =
// the first record's type byte) doubles as the "table enabled" flag.
struct OpenHoursRecord {
    std::uint8_t typeCode;
    std::uint8_t openHour;
    std::uint8_t closeHour;
};
constexpr int kOpenHoursCount = 16;
extern const OpenHoursRecord kOpenHoursTable[kOpenHoursCount];

// gilde.exe 0x63C4F8 (aBkBrunnen) — 12 entries of 44 bytes. Each entry is a
// 32-byte NUL-padded "bk_*" name followed by 12 type-code bytes (offset 32..43).
// LookupTypeName scans all 12*12 code slots for `code` and returns the row name.
constexpr int kTypeNameRows      = 12;
constexpr int kTypeNameRowStride = 44;
constexpr int kTypeNameNameLen   = 32;   // name field length within a row
constexpr int kTypeNameCodeCount = 12;   // code bytes per row (offset 32..43)
extern const std::uint8_t kTypeNameTable[kTypeNameRows * kTypeNameRowStride];

// gilde.exe dword_582F68 — the type/string-id pair table copied onto the stack
// by LookupTypeStringId (0x158 = 344 bytes = 86 dwords = 43 pairs). Each pair is
// {typeCode:dword, stringIdx:dword}; the scan walks pairs (word stride 2 in the
// original's dword view) testing the HIGH word of pair.typeCode against the
// object's type index, stops on a 0 typeCode word, terminator pair {0,-1}.
constexpr int kTypeStringIdPairs = 43;
struct TypeStringIdPair {
    std::uint32_t typeCode;
    std::uint32_t stringIdx;
};
extern const TypeStringIdPair kTypeStringIdTable[kTypeStringIdPairs];

// ===========================================================================
// Game-state array bases. Null base => the originals' null-table behaviour.
// ===========================================================================
//   buildingTypeBase : dword_13CE294 (stride 589) — BuildingTypeDef catalog.
//   objectTypeBase   : dword_13CE27C (stride 65)  — object category table.
struct Building3Arrays {
    const std::uint8_t* buildingTypeBase = nullptr;  // dword_13CE294
    const std::uint8_t* objectTypeBase   = nullptr;  // dword_13CE27C
};
void SetBuilding3Arrays(const Building3Arrays& a);
const Building3Arrays& Building3ArraysGet();

// The current game clock hour (WORD2(qword_13CE852) in the originals — the high
// word of the packed game-time qword, i.e. the hour-of-day). Set by the caller.
void SetBuilding3GameHour(int hour);
int  Building3GameHour();

// ===========================================================================
// Cross-module hooks (default inert). Every callee with no reconstructed target
// is routed here; tests install a mock.
// ===========================================================================
struct Building3Hooks {
    virtual ~Building3Hooks() = default;

    // gilde.exe 0x5857fc — VIBE_GameObject_QueryFind: find an object linked to
    // `containerHandle` matching (groupA, groupB, flag, protoId). Returns an
    // opaque object handle (0 = none). Inert: 0.
    virtual std::int32_t GameObjectQueryFind(std::int32_t containerHandle,
                                             int groupA, int groupB, int flag,
                                             std::int32_t protoId) {
        (void)containerHandle; (void)groupA; (void)groupB; (void)flag;
        (void)protoId; return 0;
    }

    // gilde.exe 0x586c20 / 0x586a6c — Person scene iterator. QueryBegin opens an
    // iteration scoped to (ctx, groupA, groupB[, protoId]); returns the first
    // record pointer (null = empty). IterNext advances; null = end.
    virtual const std::uint8_t* PersonQueryBegin(std::int32_t ctx, int groupA,
                                                 int groupB, std::int32_t proto) {
        (void)ctx; (void)groupA; (void)groupB; (void)proto; return nullptr;
    }
    virtual const std::uint8_t* PersonIterNext() { return nullptr; }

    // gilde.exe 0x5c8b38 — VIBE_Transform_PointThroughBoneChain: transform the
    // local point `in[3]` by the object's bone chain `obj`, writing world xyz to
    // out[3]. Inert: copy in -> out (identity).
    virtual void TransformPoint(const float* obj, const float* in, float* out) {
        (void)obj;
        if (in && out) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; }
    }

    // gilde.exe 0x43923c — VIBE_Memory_FreeDebug(ptr, size, file, line).
    virtual void MemoryFreeDebug(std::int32_t ptr, std::int32_t size) {
        (void)ptr; (void)size;
    }
    // gilde.exe 0x585aa4 — VIBE_GameObject_FreeChildList(&rec[93]).
    virtual void GameObjectFreeChildList(std::int32_t* childListField) {
        (void)childListField;
    }

    // gilde.exe 0x4b09c8 / 0x4dc074 / 0x4f24e0 — occupant-release leaves.
    virtual void CharacterChangePlayerAction(int a, int b, int c, std::uint16_t actor) {
        (void)a; (void)b; (void)c; (void)actor;
    }
    virtual void CharActionCancelEntityActions(const std::uint8_t* rec) { (void)rec; }
    virtual void EventCancelMatchingActors() {}
    // gilde.exe 0x47ed68 — VIBE_Office_ReleaseCharacterHoldings(rec); returns rec.
    virtual std::int32_t OfficeReleaseCharacterHoldings(std::int32_t rec) { return rec; }

    // gilde.exe 0x4949c4 — VIBE_Command_QueueRequestEntity29(-1, rec).
    virtual std::int32_t CommandQueueRequestEntity29(std::int32_t a, std::int32_t rec) {
        (void)a; (void)rec; return 0;
    }
    // gilde.exe 0x426724 — VIBE_Character_IndexFromPointer(charPtr).
    virtual std::uint32_t CharacterIndexFromPointer(std::int32_t charPtr) {
        (void)charPtr; return 0;
    }
    // gilde.exe 0x5b4a24 — VIBE_Universe_SwitchActiveSlot(slot).
    virtual void UniverseSwitchActiveSlot(std::uint32_t slot) { (void)slot; }
    // gilde.exe 0x402120 — VIBE_Character_Destroy(charPtr).
    virtual void CharacterDestroy(std::int32_t charPtr) { (void)charPtr; }
    // gilde.exe 0x591818 — VIBE_GameObject_ResolveRootContainer(rec); 0 = none.
    virtual std::int32_t GameObjectResolveRootContainer(std::int32_t rec) {
        (void)rec; return 0;
    }
    // gilde.exe 0x585a30 — VIBE_GameObject_RemoveById(&root[93], objId).
    virtual std::int32_t GameObjectRemoveById(std::int32_t* childListField,
                                              std::int32_t objId) {
        (void)childListField; (void)objId; return 0;
    }

    // gilde.exe 0x4c63f8 / 0x4c6278 — handler-entity enumerator.
    virtual const std::uint32_t* HeFindFirstHandlerByFilter(int a, int b, int c) {
        (void)a; (void)b; (void)c; return nullptr;
    }
    virtual const std::uint32_t* HeFindNextMatchingHandler() { return nullptr; }

    // gilde.exe 0x5894b0 — VIBE_Building_RemoveAndCleanup(index, mode).
    virtual void BuildingRemoveAndCleanup(int index, int mode) {
        (void)index; (void)mode;
    }
    // gilde.exe 0x5c6af0 — VIBE_Light_SetGrayColorThunk(r, g, lightRec).
    virtual void LightSetGrayColorThunk(int r, int g, std::int16_t* lightRec) {
        (void)r; (void)g; (void)lightRec;
    }
};
void SetBuilding3Hooks(Building3Hooks* hooks);
Building3Hooks* Building3HooksGet();

// ===========================================================================
// Time-window / name / string-id table lookups
// ===========================================================================
// gilde.exe 0x51dc04 — VIBE_Building_CheckTimeWindowOpen(building, &open, &close)
//   For building type code `typeCode`, find its open-hours record. If the table
//   is disabled (byte_63C70D == 0) the building is always open (returns true).
//   Records not present in the table are open (returns true). When found, writes
//   open/close hours to the out params (if non-null) and returns whether the
//   current game hour is within [open, close). Verbatim 3-byte-record scan.
bool Building3_CheckTimeWindowOpen(std::uint8_t typeCode, int* outOpen, int* outClose);

// gilde.exe 0x50c738 — VIBE_Building_LookupTypeName(code, out)
//   Scan the 12x12 code grid for `code`; on a hit copy the row's name into `out`
//   and return 1, else return 0. (The original copies the name word-by-word.)
int Building3_LookupTypeName(std::uint8_t code, char* out);

// gilde.exe 0x587fcc — VIBE_Building_LookupTypeStringId(typeIndex)
//   Only object-type category 2 (container) records have a string id. Scans the
//   pair table for a pair whose HIGH word equals `typeIndex`; returns
//   stringIdx + 1433 on a hit, else -1. `objectCategory` is the object-type
//   table byte (objectTypeBase[65*typeIndex] +0) resolved by the caller.
int Building3_LookupTypeStringId(std::uint16_t typeIndex, int objectCategory);

// ===========================================================================
// Scene-query occupant finders (routed through the hooks).
// ===========================================================================
// gilde.exe 0x587674 — VIBE_Building_FindWorkProductObject(building)
//   The building's type byte + UI category select which proto-id of work product
//   to query for in its container (rec+93). Returns the found object handle (0).
std::int32_t Building3_FindWorkProductObject(const std::uint8_t* building);

// gilde.exe 0x586904 — VIBE_Building_FindActiveWorkSlot(actorId)
//   For the globally-selected building (dword_6477A4), query its six candidate
//   work-station objects (protos 146..151); return the first that has an active
//   sub-object matching `actorId` resolved to a proto-278 handle. 0 = none.
std::int32_t Building3_FindActiveWorkSlot(std::int32_t selectedBuilding,
                                          std::int16_t actorId);

// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType(building, typeByte)
//   Iterate production buildings of the scene; among those with the same +0 type
//   byte as `typeByte` (and not type 10), pick the one nearest to `building`
//   (Euclidean distance over the transformed bone-chain origin). Returns the
//   nearest record handle (the original's `v3`), 0 if none. `selfRec` carries the
//   self origin; record layout fields (+0 type, +97 mesh ptr) are read here.
std::int32_t Building3_FindNearestSameType(const std::uint8_t* selfRec,
                                           std::uint8_t typeByte);

// gilde.exe 0x587a28 — VIBE_Building_FindNearestVacantSameType(typeByte, ctx)
//   Like FindNearestSameType but additionally requires the candidate's owner word
//   (+39) be 0xFFFF (vacant). Returns the nearest vacant record pointer, null if
//   none. `selfCtx` is the scene context used to seed the person iteration.
const std::uint8_t* Building3_FindNearestVacantSameType(std::uint8_t typeByte,
                                                        std::int32_t selfCtx,
                                                        const std::uint8_t* selfRec);

// ===========================================================================
// Occupant category sync — wired against the REAL BuildingType_GroupFromCode.
// ===========================================================================
// gilde.exe 0x58a044 — VIBE_Building_UpdateOccupantCategory
//   Resolves the family record for occupant `personIdx`; depending on the
//   occupant class byte (5/6/7) it copies a category byte through the
//   BuildingType_GroupFromCode-derived slot of the family record (clamping to the
//   stronger of the existing entries) and sets the dirty flag. Returns the
//   67*personIdx token the original returns. The arrays the original owns are
//   passed in: `recCode` (occupant's +356 type byte, high byte of the +353 dword),
//   `classByte` (byte_12CE912[536*idx]), `directCat` (byte_12CEA74[536*idx]), and
//   the resolved family-record base (null = no family). `outFamilyCat` (optional)
//   receives the byte written into the family record's group slot. `outDirty`
//   receives 1 when dword_631754 would be set. Returns 67*personIdx.
int Building3_UpdateOccupantCategory(int personIdx, std::int8_t recCode,
                                     std::uint8_t classByte, std::uint8_t directCat,
                                     std::uint8_t* familyRecord, int* outFamilyCat,
                                     int* outDirty, std::uint8_t* outDirectCat);

// ===========================================================================
// Lifecycle unlink / release / destroy (routed through the hooks).
// ===========================================================================
// gilde.exe 0x586d6c — VIBE_Building_FreeAndUnlink(rec)
//   Validate the record (-1 if null, -2 if +0 type byte is 0). For type-30
//   records free the +113 buffer. Then unlink the record pointer from two parallel
//   index arrays (dword_12CEA7C / dword_12CEA80, 768 slots of 134-dword stride),
//   free its child list, clear the +0 type byte. Returns 0 on success. The two
//   index arrays are passed in (768 entries each) so the unlink is observable.
int Building3_FreeAndUnlink(std::uint8_t* rec, std::int32_t recId,
                            std::int32_t* indexArrayA, std::int32_t* indexArrayB,
                            int indexCount);

// gilde.exe 0x589468 — VIBE_Building_ReleaseOccupantHoldings(rec)
//   If `rec` has a valid actor word (+0 != 0xFFFF) cancel its player action,
//   entity actions and matching event actors, then release its office holdings.
//   Returns `rec`. `actorWord` is *(u16*)rec.
const std::uint8_t* Building3_ReleaseOccupantHoldings(const std::uint8_t* rec,
                                                      std::uint16_t actorWord);

// gilde.exe 0x588d00 — VIBE_Building_DetachAndDestroyOccupant(rec)
//   If the +36 linked record has flag bit 2, enqueue a request-entity-29 command.
//   If the +59 linked character exists, switch universe to its slot, destroy it,
//   switch back. Finally resolve the root container and remove the object by id.
//   Returns the remove result (0 if no root). Linked fields (+36 flags, +59 char,
//   +2 objId) are passed in.
std::int32_t Building3_DetachAndDestroyOccupant(std::int32_t rec,
                                                std::int32_t linkedRec36,
                                                std::uint8_t flags36,
                                                std::int32_t char59,
                                                std::int32_t char59Ptr136,
                                                std::int32_t activeSlot,
                                                std::int32_t objId);

// gilde.exe 0x591870 — VIBE_Building_CollectByCityHandle(cityIdx, filter, out)
//   Enumerate handler entities of filter (1,0,35); collect up to 9 whose +44
//   dword equals dword_12CE914[134*cityIdx] and (when `filter` non-null) whose
//   +43 dword equals filter[+1]. Returns the count. `cityField` is the
//   dword_12CE914[134*cityIdx] value resolved by the caller.
int Building3_CollectByCityHandle(std::int32_t cityField, const std::int32_t* filter,
                                  std::uint32_t** out);

// gilde.exe 0x5918e0 — VIBE_Building_CollectOwnedByPerson(ownerId, person, out, cityIdx)
//   Enumerate handler entities of filter (1,0,35); for each, open a person query
//   for its +43 dword and collect the handler when the person's +39 word equals
//   `ownerId`, the person matches `person` (if non-null) and (cityIdx 0xFFFF or
//   the handler's +44 dword equals dword_12CE914[536*cityIdx]). Up to 9. Returns
//   the count. `cityField` is the resolved dword_12CE914[536*cityIdx].
int Building3_CollectOwnedByPerson(std::uint16_t ownerId, const std::uint8_t* person,
                                   std::int32_t cityField, bool cityWild,
                                   std::uint32_t** out);

// gilde.exe 0x5896fc — VIBE_Building_ResetAllBuildings()
//   RemoveAndCleanup all 768 building slots (mode 2), reset the 16 light records
//   (word_13C3110, stride 82: SetGrayColorThunk(0,164,rec); rec[0]=-1) and clear
//   the three selection globals. `lightRecords` is the word_13C3110 base (16*82).
void Building3_ResetAllBuildings(std::int16_t* lightRecords, int* outSel0,
                                 int* outSel1, int* outSel2);

}  // namespace guild::sim
