#pragma once
// Building queries / type-record lookups / room-slot collectors for the Guild
// simulation (gilde.exe). MODULE: buildings, prefix VIBE_Building_*
// (namespace guild::sim). Companion to building.{h,cpp} and friends; this file
// translates a slice of the still-deferred Building_* query/lookup functions
// that walk the three big game-state arrays:
//
//   * the building-TYPE definition table  (gilde.exe dword_13CE294, stride 589)
//       — the same catalog modeled by building_types.h BuildingTypeDef; reached
//         here by a raw byte base so the +0 state byte, +34 slot count and the
//         +35 word[64] room list are addressable verbatim.
//   * the object-TYPE table                (gilde.exe dword_13CE27C, stride 65)
//       — byte +0 = a category code (2 = container, 6 = workstation, 33 = wall).
//   * the char/object record array         (gilde.exe dword_13CE298, stride 169)
//       — the live placed-object/char slots (alive byte @+0, owner dword @+101).
//
// These large arrays are NOT owned by this module, so they are exposed as
// settable raw base pointers (mirroring the originals' null-base bail) plus a
// settable record count. Cross-module callees that have no reconstructed target
// yet (the scene/handler/person query subsystems) are routed through an
// installable hooks struct with INERT defaults defined in building2.cpp; tests
// install their own. Nothing here mutates owner state.
//
// Translated functions:
//   VIBE_Building_LookupTypeRecordA      0x589778
//   VIBE_Building_LookupTypeRecordB      0x5897c8
//   VIBE_Building_MatchTypeCode          0x589960
//   VIBE_Building_MatchProfessionCode    0x5898e8
//   VIBE_Building_MapTypeToState         0x592a5c
//   VIBE_Building_CollectObjectSlots     0x58faa8
//   VIBE_Building_CollectStorableSlots   0x58fb30
//   VIBE_Building_CollectSlotsAfterObject 0x58fbb4
//   VIBE_Building_CollectFlagNodeCallback 0x588044
//   VIBE_Building_FindOwnedDungeonSlot   0x594cfc
//   VIBE_Building_GetCategoryForObject   0x589d24
//   VIBE_Building_HasActiveOffice        0x58a280
//   VIBE_Building_FindOfficeStorage      0x58a354
#include <cstdint>

#include "guild/common/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from gilde.exe).
// ===========================================================================
// gilde.exe byte_649910 — 76 six-byte records read by LookupTypeRecordA; the
// fallback (index >= 76) returns record 0 (all zero).
constexpr int kTypeRecordA_Count  = 76;
// gilde.exe byte_649AD8 — 27 six-byte records read by LookupTypeRecordB.
constexpr int kTypeRecordB_Count  = 27;
constexpr int kTypeRecordStride   = 6;

extern const std::uint8_t kTypeRecordTableA[kTypeRecordA_Count * kTypeRecordStride];
extern const std::uint8_t kTypeRecordTableB[kTypeRecordB_Count * kTypeRecordStride];

// Output of a type-record lookup: the leading dword (+0) and the word at +4.
struct TypeRecord {
    std::uint32_t dword0;   // bytes [0..3] of the 6-byte record
    std::uint16_t word4;    // bytes [4..5] of the 6-byte record
};

// gilde.exe 0x589778 — VIBE_Building_LookupTypeRecordA (__usercall, eax=(code@al), esi=out)
// Copies record `code` from byte_649910 (76 entries) into *out; codes >= 76
// fall back to record 0. Returns the out pointer (eax in the original).
TypeRecord* Building_LookupTypeRecordA(std::uint8_t code, TypeRecord* out);

// gilde.exe 0x5897c8 — VIBE_Building_LookupTypeRecordB (code 0..26, else record 0).
TypeRecord* Building_LookupTypeRecordB(std::uint8_t code, TypeRecord* out);

// ===========================================================================
// Game-state array bases (set by the loader / by tests). Null base => the
// originals' null-table behavior.
// ===========================================================================
//   buildingTypeBase : dword_13CE294 (stride 589) — BuildingTypeDef catalog.
//   objectTypeBase   : dword_13CE27C (stride 65)  — object category table.
//   charArrayBase    : dword_13CE298 (stride 169) — placed char/object slots.
struct BuildingArrayBindings {
    const std::uint8_t* buildingTypeBase = nullptr;  // dword_13CE294
    const std::uint8_t* objectTypeBase   = nullptr;  // dword_13CE27C
    const std::uint8_t* charArrayBase    = nullptr;  // dword_13CE298
};
void SetBuildingArrayBindings(const BuildingArrayBindings& b);
const BuildingArrayBindings& BuildingArrays();

// ===========================================================================
// Cross-module query hooks (default inert). The query subsystems
// (VIBE_GameObject_QueryFind / VIBE_Person_QueryByGoodType / VIBE_He_*) are not
// reconstructed in this module; tests install a mock.
// ===========================================================================
struct IBuilding2QueryHooks {
    virtual ~IBuilding2QueryHooks() = default;

    // gilde.exe 0x5857fc — VIBE_GameObject_QueryFind: find an object linked to
    // container `containerHandle` matching (groupA, groupB, flag, protoId).
    // Returns an opaque object handle (>=0) or 0 / -1 for "none". Inert: 0.
    virtual std::int16_t GameObjectQueryFind(std::int32_t containerHandle,
                                             int groupA, int groupB, int flag,
                                             std::int32_t protoId) {
        (void)containerHandle; (void)groupA; (void)groupB; (void)flag;
        (void)protoId;
        return 0;
    }

    // gilde.exe 0x5929f0 — VIBE_Person_QueryByGoodType: resolve the office /
    // person record (a char* in the original) for (goodType, ctx); returns the
    // char-array slot index whose +93 container handle FindOfficeStorage uses,
    // and writes the "kind" result byte (the original's `v4`) to *kindOut.
    // Inert: returns -1 (no office), kind 0.
    virtual std::int32_t PersonQueryByGoodType(std::uint8_t goodType, int ctx,
                                               std::uint8_t* kindOut) {
        (void)goodType; (void)ctx;
        if (kindOut) *kindOut = 0;
        return -1;
    }
};
void SetBuilding2QueryHooks(IBuilding2QueryHooks* hooks);
IBuilding2QueryHooks* Building2QueryHooks();

// ===========================================================================
// Type-record matchers (read the building-TYPE table at dword_13CE294).
// ===========================================================================
// gilde.exe 0x589960 — VIBE_Building_MatchTypeCode (__cdecl(u16 typeIndex, int n, ...))
//   Reads the building-type record `typeIndex` (stride 589 from
//   buildingTypeBase) and tests whether its byte at +356 (the high byte of the
//   dword at +353) equals any of the `count` codes in `codes[]`. Returns 1 on
//   the first match, else 0. (The original packs the codes as a 4-byte-stride
//   varargs list; we take them as a plain byte array — codes[i] is the i-th.)
int Building_MatchTypeCode(std::uint16_t typeIndex, int count,
                           const std::uint8_t* codes);

// gilde.exe 0x5898e8 — VIBE_Building_MatchProfessionCode (__cdecl(u16, int, ...))
//   Same scan, but matches a code against EITHER profession byte +358 or +361
//   of the type record. Returns 1 on first match else 0.
int Building_MatchProfessionCode(std::uint16_t typeIndex, int count,
                                 const std::uint8_t* codes);

// gilde.exe 0x592a5c — VIBE_Building_MapTypeToState (__usercall, eax=(code@al), edx=out)
//   Reads the type record's state byte (+0) for `typeCode` and maps it to a UI
//   state in *outState. Returns 1 for the "active/special" states, 0 otherwise
//   (the 0-returns still write a state byte for some inputs). Verbatim switch.
int Building_MapTypeToState(std::uint8_t typeCode, std::uint8_t* outState);

// gilde.exe 0x589d24 — VIBE_Building_GetCategoryForObject
//   (__usercall, al=(personIdx@eax), dl=(matchCode@dl))
//   For person/scene record `personIdx` (the word_12CE910 array, stride 536),
//   the original reads the code byte at record +356 (the high byte of the dword
//   at +353), groups it via BuildingType_GroupFromCode, and:
//     if group(recCode) == matchCode  -> return byte_12CEA74[536*personIdx]
//     else family = Person_GetFamilyRecord(&record); if !family return 0
//     else return family[matchCode + 0x70]
//   We recover the verbatim branch; the person-array byte (`recCode`), the
//   direct category slot (`directCat` = byte_12CEA74[536*personIdx]) and the
//   family fallback (`familyCat`, < 0 == no family record) are resolved by the
//   caller from the arrays it owns. Returns the resolved category byte.
std::uint8_t Building_GetCategoryForObject(std::int8_t recCode, std::int8_t matchCode,
                                           std::uint8_t directCat, int familyCat);

// ===========================================================================
// Room / object slot collectors (walk the type record's +35 word[64] room list).
// ===========================================================================
// gilde.exe 0x58faa8 — VIBE_Building_CollectObjectSlots (__usercall, eax=bldg, edx=out)
//   Walk the building's room list (type record +35, up to 64 entries, stop on a
//   0 entry); for each, mask off the high bit and ask the scene for a matching
//   object (GameObjectQueryFind(handle, 2, 6, 0, slotId)). Append every hit
//   handle to out[] and return the count. `containerHandle` is *(int*)(bldg+93);
//   `typeCode` is *bldg (the +0 type byte).
int Building_CollectObjectSlots(std::uint8_t typeCode, std::int32_t containerHandle,
                                std::int16_t* out);

// gilde.exe 0x58fb30 — VIBE_Building_CollectStorableSlots (eax=bldg, edx=out)
//   Walk the same room list; for each slot id (low 15 bits) whose object-type
//   category (objectTypeBase[65*id] +0) is NOT 33 (wall), count it. When `out`
//   is non-null, every slot id (matching or not) is written to out[] but the
//   write cursor only advances on a non-33 entry (faithful to the original's
//   pre/post-increment quirk). Returns the non-33 count.
int Building_CollectStorableSlots(std::uint8_t typeCode, std::int16_t* out);

// gilde.exe 0x58fbb4 — VIBE_Building_CollectSlotsAfterObject (eax=bldg, edx=after, ebx=out)
//   Find the slot equal to *after (high bit masked) within the first +34 (count)
//   room entries; then collect the slots AFTER it until a 0 entry or a slot
//   whose object category is 2 or 6. Slots with category 33 are skipped from the
//   output but iteration continues. Returns the collected count; 0 if *after is
//   not found. `slotCount` is the +34 byte.
int Building_CollectSlotsAfterObject(std::uint8_t typeCode, std::uint8_t slotCount,
                                     std::int16_t afterSlot, std::int16_t* out);

// gilde.exe 0x588044 — VIBE_Building_CollectFlagNodeCallback (al ret; eax=node, edx=acc)
//   Enumeration callback over flag nodes. `node` points at an object record
//   whose +72 dword packs a kind byte (>>24) and a slot word (low 16 bits). When
//   the kind is 1 AND the slot's object category (objectTypeBase[65*slot]) is 33
//   (wall/flag), append the node to the accumulator: store the slot word at
//   acc+256+2*count, the node handle at acc+4*count, bump the count at acc+384.
//   Returns true while count < 64 (i.e. "keep enumerating").
//   `acc` is the accumulator block laid out as FlagNodeAccumulator below.
struct FlagNodeAccumulator {
    std::int32_t  handles[64];   // +0   collected node handles
    std::int16_t  slots[64];     // +256 collected slot words
    std::int32_t  count;         // +384 number collected
};
bool Building_CollectFlagNodeCallback(std::int32_t nodeHandle,
                                      std::int32_t nodePackedKindSlot,
                                      FlagNodeAccumulator* acc);

// gilde.exe 0x594cfc — VIBE_Building_FindOwnedDungeonSlot (eax=(person))
//   When person's +433 flag is set, scan the char/object array (charArrayBase,
//   stride 169, 256 slots): for each alive slot (byte +0 != 0) whose building
//   type (buildingTypeBase[589*typeByte] +0) == 4 (dungeon) and whose owner
//   dword (+101) equals the person's +4 dword, "found". On a hit the original
//   returns owner^owner == 0; if the scan exhausts it returns 1; if the +433
//   flag is clear it returns 0. `dungeonOwnerId` is the person's +4 dword.
int Building_FindOwnedDungeonSlot(bool hasDungeonFlag, std::int32_t dungeonOwnerId);

// gilde.exe 0x58a280 — VIBE_Building_HasActiveOffice (__fastcall(int goodType, int ctx))
//   true iff Person_QueryByGoodType(goodType, ctx) found an office (returns a
//   non-null/owned record). Routed through the query hook.
bool Building_HasActiveOffice(std::uint8_t goodType, int ctx);

// gilde.exe 0x58a354 — VIBE_Building_FindOfficeStorage (__usercall, al=(goodType), esi=ctx)
//   Resolve the office record for (goodType, ctx); if its kind byte == 1, find
//   the proto-277 storage object in its container, else the proto-322 one.
//   Returns the found object handle (0 = none). Routed through the query hook.
std::int16_t Building_FindOfficeStorage(std::uint8_t goodType, int ctx);

}  // namespace guild::sim
