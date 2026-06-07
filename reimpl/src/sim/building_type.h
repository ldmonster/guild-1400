#pragma once
// Building-type classification / code-mapping for the Guild simulation
// (gilde.exe). MODULE: buildings, prefix VIBE_BuildingType_* and the type
// predicate helpers from VIBE_Building_* (namespace guild::sim).
//
// These are pure, self-contained integer mappers over a building TYPE CODE
// (the "Gebaeude type" 1..75 used by the upgrade/profession system) and over
// the building-type-table KIND enum (the +0 byte of a BuildingTypeDef).
//
// Translated functions:
//   VIBE_BuildingType_GroupFromCode          0x58a4c8
//   VIBE_BuildingType_GroupFromPairCode      0x58a54c
//   VIBE_BuildingType_ComputeRankWithinGroup 0x58a560
//   VIBE_BuildingType_MapToActionCode        0x589a7c
//   VIBE_BuildingType_MapToCategoryCode      0x589af0
//   VIBE_BuildingType_MapToProfessionCode    0x589be4
//   VIBE_BuildingType_MapActionToCategory    0x58a25c
//   VIBE_BuildingType_ComputeVariantIndex    0x589cb0
//   VIBE_BuildingType_ClassifyByRange        0x589c20
//   VIBE_Building_ClassifyTypeFlag           0x589818
//   VIBE_Building_IsTypeInGroup              0x5898cc
//   VIBE_Building_MapTypeToCategory          0x5878b0   (table-driven)
//   VIBE_Building_IsStorageType              0x587f50   (table-driven)
//   VIBE_Building_IsProductionType           0x587f80   (table-driven)
#include "guild/common/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Group / rank arithmetic over the type CODE (1..75).
//
// The building type codes are laid out as twelve contiguous "groups" of six
// codes each (with a couple of three-code groups). GroupFromCode returns the
// 1-based group id (1..13), and the *MapTo* functions translate that group id
// into an action / category / profession code. The originals implement these as
// dense switch tables that tail-jump into a shared block of `mov al,N; ret`
// epilogues; we recover the return constants from those epilogues (see .cpp).
// ---------------------------------------------------------------------------

// gilde.exe 0x58a4c8 — VIBE_BuildingType_GroupFromCode  (__usercall, al=(code@al))
// code 1..75 -> group id; 0 for codes outside the table.
u8 BuildingType_GroupFromCode(u8 code);

// gilde.exe 0x58a54c — VIBE_BuildingType_GroupFromPairCode (al=(code@al))
// Same grouping but for a half-density "pair code" (1..26, two codes/group).
u8 BuildingType_GroupFromPairCode(u8 code);

// gilde.exe 0x58a560 — VIBE_BuildingType_ComputeRankWithinGroup (eax=(code@al))
// Rank within the code's six-code group, counting DOWN: the highest code in a
// group is rank 1, the lowest is rank 6. Returns 0 for ungrouped codes.
int BuildingType_ComputeRankWithinGroup(u8 code);

// gilde.exe 0x589a7c — VIBE_BuildingType_MapToActionCode (al ret; arg = type code)
// type code -> "action code" (via GroupFromCode then a per-group remap).
u8 BuildingType_MapToActionCode(u8 code);

// gilde.exe 0x589af0 — VIBE_BuildingType_MapToCategoryCode (al=(group@al))
// group id (1..12) -> category code.
u8 BuildingType_MapToCategoryCode(u8 group);

// gilde.exe 0x589be4 — VIBE_BuildingType_MapToProfessionCode (arg = type code)
// type code -> profession code (GroupFromCode then per-group remap).
u8 BuildingType_MapToProfessionCode(u8 code);

// gilde.exe 0x58a25c — VIBE_BuildingType_MapActionToCategory (al=(action@al))
// action code -> category code (inverse-ish of MapToActionCode).
u8 BuildingType_MapActionToCategory(u8 action);

// gilde.exe 0x589cb0 — VIBE_BuildingType_ComputeVariantIndex (al=(group@al), dl=(rank@dl))
// (group, rank) -> the concrete type code / variant index for that slot.
u8 BuildingType_ComputeVariantIndex(u8 group, u8 rank);

// gilde.exe 0x589c20 — VIBE_BuildingType_ClassifyByRange (al=(code@al))
// Coarse range classifier returning a small tag (7/2/3/4/9/0).
u8 BuildingType_ClassifyByRange(u8 code);

// gilde.exe 0x589818 — VIBE_Building_ClassifyTypeFlag (al=(code@al))
// Returns 2 for "buildable/owned plot" code ranges, else 0.
u8 Building_ClassifyTypeFlag(u8 code);

// gilde.exe 0x5898cc — VIBE_Building_IsTypeInGroup (al=(code@al))
// Returns 0 for the listed shop/production codes, else delegates to the coarse
// range classifier's "==2" tag (the JUMPOUT 0x589850 = `mov al,2; ret`).
u8 Building_IsTypeInGroup(u8 code);

// ---------------------------------------------------------------------------
// Table-driven predicates over the building-type KIND enum.
//
// The originals index the 589-byte type table by the building's +0 type word
// and read the kind byte. Here we take the BuildingTypeDef directly (the caller
// resolves it from the building's typeIndex via the table accessor in
// building.h) so the predicate stays pure and table-faithful.
// ---------------------------------------------------------------------------

// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory (al=(typeIndex@al))
// kind enum (typeDef.kind) -> display/UI category (0..8). Verbatim switch.
u8 Building_MapKindToCategory(u8 kind);

// gilde.exe 0x587f50 — VIBE_Building_IsStorageType.  kind == 10.
bool Building_IsStorageKind(u8 kind);

// gilde.exe 0x587f80 — VIBE_Building_IsProductionType.  kind in {11,12,13,16,28}.
bool Building_IsProductionKind(u8 kind);

}  // namespace guild::sim
