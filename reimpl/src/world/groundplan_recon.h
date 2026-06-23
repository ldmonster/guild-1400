#pragma once
// ============================================================================
// Die Gilde 1:1 — city ground-plan (Bauplatz / Grundriss) layout + city-map
// pure logic, reconstructed from gilde.exe.
//
// This module isolates the *pure* layout / eligibility / classification math
// of the ground-plan ("Riss") info panel and the city-map cluster. The original
// functions are heavily coupled to the widget / surface / form / frame-loop
// subsystems (window creation, blueprint BMP blits, frame pumps). Those coupled
// drivers are OMITTED here (see groundplan_recon.cpp manifest) because a
// faithful translation would require the entire UI/render stack; reproducing
// them with stand-ins would violate rule 8 (no cheap analogues).
//
// What IS reconstructed 1:1 below is the table/branch math that determines:
//   * the building "state" classification used to drive the panel
//     (VIBE_Groundplan_GetBuildingState, 0x4af464)
//   * the city coat-of-arms ("Wappen") label string id chosen from the
//     selected building's group code (VIBE_Groundplan_GetWappenLabelId,
//     0x4ae59c) — the plot-selection grid walk + group->label table.
//
// The two building-type accessors these depend on
// (VIBE_Building_MapTypeToCategory @0x5878b0, VIBE_Building_MapTypeToState
// @0x592a5c, VIBE_BuildingType_GroupFromCode @0x58a4c8) already exist in
// src/sim/* and are reused verbatim through the GroundplanHooks indirection so
// this file stays self-contained / linkable in the headless build.
//
// Types from guild/common/types.h. Namespace guild::world.
// ============================================================================
#include "guild/common/types.h"

namespace guild {
namespace world {

// ---------------------------------------------------------------------------
// Inert-default hooks for the coupled-leaf inputs (building-type table
// accessors + the one mutable global the original writes). The real backends
// (src/sim/building*.cpp) are wired in at the call site; the headless / unit
// build supplies the defaults below so the pure math is exercisable in
// isolation. Setting a member to nullptr means "subsystem absent" (inert).
// ---------------------------------------------------------------------------
struct GroundplanHooks {
    // gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory(typeIndexByte) -> category.
    u8 (*MapTypeToCategory)(u8 typeIndexByte) = nullptr;

    // gilde.exe 0x592a5c — VIBE_Building_MapTypeToState(typeCode, &outState) -> nonzero on hit.
    //   On a nonzero return, *outState carries the resolved state byte.
    int (*MapTypeToState)(u8 typeCode, u8* outState) = nullptr;

    // gilde.exe 0x58a4c8 — VIBE_BuildingType_GroupFromCode(code) -> building group (0..12).
    u8 (*GroupFromCode)(u8 code) = nullptr;

    // byte_6317B5 — the panel's "active building state" global the original
    //   mutates as a side effect of GetBuildingState. Reconstructed as an
    //   explicit out-cell so the function stays pure & testable.
    u8* stateGlobal = nullptr;  // byte_6317B5
};

// gilde.exe 0x4ae824 — VIBE_Groundplan_RetZero. Returns 0.
int Groundplan_RetZero();

// ---------------------------------------------------------------------------
// gilde.exe 0x4af464 — VIBE_Groundplan_GetBuildingState
//   (__usercall, al = (a1@eax)); a1 points at the building's type-code byte.
//
//   cat = MapTypeToCategory(*a1)
//   if cat == 3:  byte_6317B5 = 1;                       return cat
//   if cat == 5:  if MapTypeToState(*a1,&st): byte_6317B5 = st; return st
//   else:                                                return cat
//
// Faithful translation. The two table accessors + the state global come from
// `h`; when a hook is null the corresponding branch is inert (returns the
// category unchanged, matching "subsystem absent").
// ---------------------------------------------------------------------------
u8 Groundplan_GetBuildingState(const u8* typeCodePtr, const GroundplanHooks& h);

// ---------------------------------------------------------------------------
// Pure group-code -> Wappen label-string-id mapping extracted 1:1 from the
// inner switch of VIBE_Groundplan_GetWappenLabelId (0x4ae59c).
//
//   group in {1,2,10}      -> 1241
//   group in {3,4}         -> 1245
//   group in {11,12,6}     -> 1249
//   group in {5,7,8,9}     -> 1253
//   otherwise              -> 1241  (the v0 default)
//
// Exposed standalone because it is the load-bearing eligibility table and is
// trivially golden-vector testable.
// ---------------------------------------------------------------------------
int Groundplan_WappenLabelForGroup(u8 group);

// ---------------------------------------------------------------------------
// gilde.exe 0x4ae59c — VIBE_Groundplan_GetWappenLabelId
//   () -> int label string id.
//
// Original control flow:
//   switch (byte_12335B8 /* forced-wappen override */):
//     case 1: return 1241   (v0 default)
//     case 2: return 1245
//     case 3: return 1249
//     case 4: return 1253
//     case 0: walk the plot/building array (stride 536) until a record's
//             marker byte (+0) is 6 (selected) or 7 (terminator), counting
//             the index `v1`; then read HIBYTE of the type word of record v1
//             in the *parallel* detail array (stride 536 dwords == 134 dwords),
//             feed it to GroupFromCode, and map via the table above.
//     default (>=5): return 1241 (v0 default)
//
// The two arrays (byte_12CE912 markers, unk_12CEA71 type-word details) are the
// live building/plot grid; here they are passed in as spans so the grid walk +
// group->label math is reconstructed exactly while staying free of the global
// game state. `forcedWappen` is byte_12335B8.
//
//   markers      : base of byte_12CE912; element stride = 536 bytes.
//   markerCount  : number of plot slots (original bound: index < 411648/536 = 768).
//   typeWords    : base of unk_12CEA71 reinterpreted as u32[]; stride = 134 dwords.
//                  The selected building's type word is typeWords[134*index];
//                  HIBYTE(word) is the code fed to GroupFromCode.
// ---------------------------------------------------------------------------
int Groundplan_GetWappenLabelId(u8 forcedWappen,
                                const u8* markers,
                                int markerCount,
                                const u32* typeWords,
                                const GroundplanHooks& h);

}  // namespace world
}  // namespace guild
