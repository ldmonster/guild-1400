#pragma once
// gilde.exe 0x5929f0 — VIBE_Person_QueryByGoodType.
//
// A thin dispatcher: given a "good type" (1..5) and an iterator seed, it begins a
// person/object query filtered on the AiPlayer-table byte (filter op 5) for a
// good-type-specific key, returning the first matching record (or null).
//
//     good 1 -> Person_QueryBegin(seed, 1, 5, 15)
//     good 2 -> Person_QueryBegin(seed, 1, 5, 23)
//     good 3 -> Person_QueryBegin(seed, 1, 5, 24)
//     good 4 -> Person_QueryBegin(seed, 1, 5, 25)
//     good 5 -> Person_QueryBegin(seed, 1, 5, 26)
//     other  -> null
//
// (`1` is the filter count; `5` is filter op 5 = "AiPlayer-table byte == key".)
// The dispatch is implemented on top of the existing
// guild::sim::PersonQueryBegin (entity.h, 0x586c20) — REUSED, not redefined.
//
// This is the function the office-wage path (0x57b5b2) and the stat-resolver
// (gamelogic_recon5_resolve_stat) call to resolve "the active person's good
// store"; many other sites reference it under the `personQueryByGoodType` hook
// name. This module provides the concrete 1:1 dispatcher behind those hooks.
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// The good-type -> filter-key table (the original's switch on (goodType-1)).
// kPersonGoodTypeKey[goodType-1] is the op-5 filter key for goodType in 1..5.
//   good 1->15, 2->23, 3->24, 4->25, 5->26.
extern const int kPersonGoodTypeKey[5];

// gilde.exe 0x5929f0 — VIBE_Person_QueryByGoodType (__usercall: al=goodType,
// esi=seed). Returns the first record matching the good-type filter, or nullptr
// for goodType outside 1..5 (the switch default). `seed` is the iterator start
// node (the original's a2/esi); it forwards to PersonQueryBegin's start arg.
ObjectRec* PersonQueryByGoodType(u8 goodType, int seed);

} // namespace guild::sim
