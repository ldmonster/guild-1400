#pragma once
// ===========================================================================
// person_record.h — reconciliation of the "person" record geometries
// ===========================================================================
// MODULE: sim entity substrate (namespace guild::sim).
//
// PURPOSE
// -------
// Two byte strides ("536" and "589") were both attributed to a "person" record
// in the recon docs (recon/04_sim.md vs recon/05_world.md). This header settles
// the ambiguity with decompiled evidence and gives every consumer one canonical
// definition of each array, plus a typed adapter the economy/world code uses so
// its field reads land on the correct base.
//
// DEFINITIVE FINDING: these are THREE DISTINCT arrays, NOT one array with a
// wrong stride. They do not alias. The recon/05 claim that the Person record is
// "589 bytes, base dword_13CE294, index = person id" is INCORRECT — 589 is the
// stride of a *type-descriptor* table indexed by a small type byte, never by a
// person id.
//
// ---------------------------------------------------------------------------
// EVIDENCE (Hex-Rays pseudocode / disassembly, gilde.exe, imagebase 0x400000)
// ---------------------------------------------------------------------------
//
// (1) Person / NPC array — word_12CE910 @0x12CE910, stride 536, 768 slots.
//     Indexed by PERSON ID via a linear scan.
//
//     VIBE_Person_FindRecordById (0x58bc6c):
//         v2 = 0;
//         while (word_12CE910[v2/2] == -1 || id != dword_12CE914[v2/4]) {
//             v2 += 536;                    // <-- stride 536
//             if (v2 >= 411648) return 0;   // 411648 == 536 * 768
//         }
//         return &word_12CE910[v2/2];
//
//     VIBE_GameObject_ResolveEntityById (0x583b44), person branch:
//         v8 = word_12CE910; v9 = 0;
//         while (*v8 == -1 || id != *((DWORD*)v8 + 1)) {  // marker @+0, id @+4
//             ++v9; v8 += 268;              // 268 words == 536 bytes
//             if (v9 >= 768) ...            // <-- 768 slots
//         }
//     => stride 536 (0x218), capacity 768. CONFIRMED. (kPersonStride.)
//
// (2) Object & Building array — base dword_13CE298, stride 169, 256 slots.
//     This is what VIBE_Person_QueryBegin/IterNext ACTUALLY iterate.
//
//     VIBE_Person_QueryBegin (0x586c20):  dword_6498DC = dword_13CE298;
//     VIBE_Person_IterNext   (0x586a6c):  v2 = dword_13CE298 + 43264;  // 169*256
//         ... v0 += 169 ...                                            // <-- stride 169
//         filter reads: *v0 (alive/type @+0), *(DWORD*)(v0+1) (id @+1),
//                       *(WORD*)(v0+37) (factionA), *(WORD*)(v0+39) (owner).
//     => stride 169 (0xA9), capacity 256. CONFIRMED. (kObjectStride.)
//
// (3) AiPlayer / building-TYPE descriptor table — base dword_13CE294, stride 589.
//     Indexed by *(record's TYPE byte)*, NEVER by a person id.
//
//     VIBE_Person_IterNext (0x586a6c), filter op 5:
//         if (*(BYTE*)(589 * *v0 + dword_13CE294) == byte_6498B8) ...
//                       ^^^ 589 * (object's +0 type byte)
//
//     VIBE_Economy_ComputeGoodsDemand (0x578438):
//         v6 = 589 * *j + dword_13CE294;            // *j == object +0 type byte
//         need = *(BYTE*)(v6 + 583);                // need/security byte @ type+583
//         ...  (employment test reads *(WORD*)(j+39) on the OBJECT record)
//     VIBE_Economy_ComputeGoodsSupply (0x578634): identical (589 * *i + base, +583).
//
//     VIBE_Ai_EvaluateMeister (0x453517 / 0x45357b) disassembly:
//         movsx ecx, byte ptr [eax]      ; type byte
//         lea   eax, [ecx*4]; add eax,ecx ; eax = 5*ecx
//         shl   eax, 2;       sub eax,ecx ; eax = 20*ecx - ecx = 19*ecx
//         mov   ecx, eax; shl eax, 5; sub eax,ecx ; eax = 32*(19c) - 19c = 589*ecx
//         add   eax, ds:dword_13CE294    ; base
//         mov   bl, [eax]                ; read kind byte @ type+0
//     => stride 589, index is a SMALL TYPE BYTE. CONFIRMED. (kBuildingTypeStride.)
//
// ---------------------------------------------------------------------------
// CONCLUSION
// ---------------------------------------------------------------------------
//   * 0x12CE910 (536B, 768) = g_persons        — the NPC/actor "Person" array.
//   * 0x13CE298 (169B, 256) = g_objects        — the Object/Building array that
//                                                 the "Person_*" query iterators
//                                                 actually walk.
//   * 0x13CE294 (589B)      = g_buildingTypes  — the AiPlayer/building-type
//                                                 descriptor table (BuildingTypeDef),
//                                                 indexed by the OBJECT's type byte.
//
//   The economy demand/supply pass reads TWO of these for each iterated record:
//     - the OBJECT record (g_objects) for employment (object+39), and
//     - the TYPE descriptor (g_buildingTypes[object.type]) for the need byte
//       (typeDef+583).
//   It never touches the 536-byte g_persons array. So the 536/589 "conflict" is
//   a category error in recon/05: the economy view is type-driven, not
//   person-id-driven.
//
// This header adds the canonical reconciliation constants + a typed adapter so
// economy.cpp can build its PersonEcoView from the real (object, type-table)
// pair without re-deriving the arithmetic, and an OFFSET/stride contract that
// the reconcile tests assert against the decompiled multipliers.
#include "guild/common/types.h"
#include "sim/types.h"             // Person (536), ObjectRec (169)
#include "sim/building_types.h"    // BuildingTypeDef (589)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Canonical strides & the index multipliers proven above. The reconcile tests
// assert these match the decompiled arithmetic exactly.
// ---------------------------------------------------------------------------
namespace person_reconcile {

// Stride of the real Person/NPC array (word_12CE910). FindRecordById steps by
// this; the scan bound is kPersonStride * kPersonCapacity.
constexpr int kPersonRecordStride   = kPersonStride;      // 536
constexpr int kPersonRecordCapacity = kPersonCapacity;    // 768
constexpr int kPersonScanBound      = 411648;             // 536 * 768
static_assert(kPersonRecordStride * kPersonRecordCapacity == kPersonScanBound,
              "Person scan bound must equal 536*768 (FindRecordById @0x58bc6c)");

// Stride of the Object/Building array (dword_13CE298). The "Person_*" iterators
// walk THIS; the scan bound is kObjectStride * kObjectCapacity.
constexpr int kObjectRecordStride   = kObjectStride;      // 169
constexpr int kObjectRecordCapacity = kObjectCapacity;    // 256
constexpr int kObjectScanBound      = 43264;              // 169 * 256
static_assert(kObjectRecordStride * kObjectRecordCapacity == kObjectScanBound,
              "Object scan bound must equal 169*256 (Person_IterNext @0x586a6c)");

// Stride of the AiPlayer/building-type descriptor table (dword_13CE294). The
// economy reads typeDef = base + kTypeDescriptorStride * objectTypeByte.
constexpr int kTypeDescriptorStride = kBuildingTypeStride; // 589

// Byte offsets the economy demand/supply pass reads, by source array:
//   object record (g_objects): employment word @+39 (0xFFFF == unemployed)
//   type descriptor (g_buildingTypes): need byte @+583
constexpr int kObjectEmploymentOff  = 39;   // *(WORD*)(j+39) in 0x578438/0x578634
constexpr int kTypeNeedByteOff      = 583;  // *(BYTE*)(589*type + base + 583)
constexpr u16 kEmploymentNone       = 0xFFFF;
static_assert(kTypeNeedByteOff == BuildingTypeField::kSecurity,
              "type need/security byte must be the +583 field of BuildingTypeDef");

}  // namespace person_reconcile

// ---------------------------------------------------------------------------
// Additional Person-record fields recovered from the Character data/rule pass
// (CollectByOwner 0x4b99ac, UpdateWorkScripts 0x4b9a5c, CountByType 0x4b092c,
// IsOwnerForTurn 0x453260). These are byte offsets into the 536-byte Person
// record (word_12CE910), additive to the field map in types.h:
//   +0x184 (+388, dword [97]) live-actor back-pointer (the ch_t in dword_66F0D0;
//          CollectByOwner reads *((DWORD*)person + 97) and follows it to read the
//          actor's universe ptr at actor+136 and home-universe id at actor+44).
//          NB: this overlaps the +0x184 jail/incapacitated dword in the types.h
//          map — the live game reuses the slot; the work-script/collect pass
//          treats it strictly as the live-actor pointer column.
//   On the live actor: +44 home-universe id (-1 == "wild"/no owner). CollectByOwner
//   matches actor[+44] against the requested owner-universe id at (person record
//   used as the owner key) +1, i.e. the dword at person+1 (== id low bytes).
// These are documented here so the query pass does not re-derive the multiplier.
namespace person_reconcile {
constexpr int kPersonLiveActorOff = 388;  // +0x184 dword [97] (live ch_t pointer)
}  // namespace person_reconcile

// ---------------------------------------------------------------------------
// Economy adapter: derive the per-person economy inputs from the canonical
// (object record, building-type table) pair, EXACTLY as the binary does.
//
//   need       = g_buildingTypes[object.type] .security        (typeDef + 583)
//   unemployed = (object[+39] == 0xFFFF)                        (object record)
//
// This is the one place the two distinct arrays are joined; the economy module
// consumes the (need, unemployed) result and never re-derives the offsets.
//
// `objectRecord` is a pointer into the 169-byte object array (what
// PersonQueryBegin/IterNext return). `typeTable` is the 589-stride descriptor
// array base (sim::g_buildingTypes). `typeTableLoaded` mirrors the original's
// null-base guard (dword_13CE294 != 0): when false the need byte reads 0.
// ---------------------------------------------------------------------------
struct PersonEcoInputs {
    u8   need;        // typeDef[object.type] + 583
    bool unemployed;  // object + 39 == 0xFFFF
};

// Reads the object's type byte (+0), indexes the 589-stride type table, and
// returns the need byte (+583) + the employment flag. memcpy keeps the
// unaligned +39 word read faithful and UB-free.
PersonEcoInputs PersonReadEcoInputs(const ObjectRec* objectRecord,
                                    const BuildingTypeDef* typeTable,
                                    bool typeTableLoaded);

}  // namespace guild::sim
