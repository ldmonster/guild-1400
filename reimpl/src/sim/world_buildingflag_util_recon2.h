#pragma once
// world_buildingflag_util_recon2 — small VIBE_World / VIBE_Universe leaf helpers
// reconstructed 1:1 from gilde.exe (Die Gilde / Europa 1400).
//
// Functions translated here:
//   gilde.exe 0x583990  VIBE_World_LookupBuildingTypeFlag
//   gilde.exe 0x5b371c  VIBE_Universe_ApplyHiddenToggle
//
// Both were verified absent from the rest of the reconstruction (no provenance
// xref to 0x583990 / 0x5b371c anywhere in src/) before being added here. The
// file is self-contained: it depends only on guild/common/types.h and on caller
// supplied data/hook pointers, so it compiles into the single src/** library with
// no ODR clash against the existing building / scene-graph code.

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x583990 — VIBE_World_LookupBuildingTypeFlag
//   char __usercall f(__int16 *type@<eax>, int /*unused*/@<ecx>)
//
// Classifies a scene object's type id against the static "building" kind-code
// set held at gilde.exe dword_582908+0x618 (= &dword_582908[390]).
//
// Original behaviour:
//   type    = *(i16*)type_ptr                       (sign-extended)
//   kindRec = dword_13CE27C + 65*type               (SceneTypeDef record base)
//   kind    = *(signed char*)kindRec                (the +0 kind byte)
//   The 16-byte set at dword_582908+0x618 is scanned: while set[0] != 0, find
//   the first set[i] whose value equals `kind` and return set[i]; the scan stops
//   at the implicit NUL terminator (set[i+1]==0 ends the walk). Returns 0 when
//   set[0]==0 or no element matches.
//
// kBuildingKindSet below is the exact 16 bytes recovered with get_bytes at
// gilde.exe 0x582F20 (dword_582908+0x618):
//   01 03 04 0B 0D 0E 12 13 15 16 1A 1B 1C 1F 21 00
// i.e. 15 building kind-codes followed by the 0x00 terminator.
//
// The "ecx" argument the original receives is dead (overwritten before any read
// via the stack movsd copy of the set); it is omitted from the C++ signature.

extern const guild::u8 kBuildingKindSet[16];

// Exact translation core. `kindRecordBase` is the gilde.exe dword_13CE27C table
// (stride 65 bytes); `flagSet` is the NUL-terminated kind-code set. Returns the
// matched kind code (>0) or 0.  Matching uses signed-char comparison exactly as
// the original (movsx of both the record byte and the set byte).
guild::u8 World_LookupBuildingTypeFlag(const guild::i16* typePtr,
                                       const guild::u8* kindRecordBase,
                                       const guild::u8* flagSet);

// Convenience overload using the recovered static set kBuildingKindSet.
guild::u8 World_LookupBuildingTypeFlag(const guild::i16* typePtr,
                                       const guild::u8* kindRecordBase);

// ---------------------------------------------------------------------------
// gilde.exe 0x5b371c — VIBE_Universe_ApplyHiddenToggle
//   unsigned __int8 __usercall f(unsigned __int8 newState@<al>)
//
// Forwards a "hidden" toggle across the whole scene graph then refreshes
// lighting. The original is exactly two calls:
//
//   VIBE_SceneGraph_WalkAndInvoke(off_649D64, 0, VIBE_Object_ToggleHiddenState,
//                                 6, newState);   // 0x5b3736
//   return VIBE_Light_RefreshAllObjects(1);       // 0x5b3747
//
// Those two callees (0x5ac738, 0x5c886c) live elsewhere in the reconstruction
// under their own module signatures, so to stay self-contained this leaf routes
// them through hook pointers. Default hooks are inert (return 0) which preserves
// the headless build; wire the real callees with SetUniverseHiddenToggleHooks.
// The control flow (walk, then return the lighting refresh result) is identical.

// walk: (rootNode, 0, perNodeCallback, 6, newState)
using SceneWalkInvokeFn = void (*)(void* rootNode, int zero,
                                   guild::u8 (*perNode)(guild::u8), int mode,
                                   guild::u8 newState);
using ObjectToggleHiddenFn = guild::u8 (*)(guild::u8 newState);
using LightRefreshAllFn    = guild::u8 (*)(unsigned int flag);

void SetUniverseHiddenToggleHooks(SceneWalkInvokeFn walk,
                                  void* rootNode,
                                  ObjectToggleHiddenFn toggle,
                                  LightRefreshAllFn refresh);

guild::u8 Universe_ApplyHiddenToggle(guild::u8 newState);

} // namespace guild::sim
