#pragma once
// Full-world determinism digest (PLAYABLE_PLAN P4 — broadened oracle).
//
// determinism.h's HashWorldState/SnapshotWorld cover the ENTITY substrate only:
// g_persons / g_personIds / g_objects / g_sceneNodes(+count) / g_buildingPersons,
// the loaded-guard flags and the CRT RNG state. That is enough to prove the
// entity arrays evolve deterministically, but the simulated WORLD is larger: the
// building-type catalog and production tables, the city/economy/market state, the
// game clock, the treasury/player bookkeeping, and the law/event/office/crime/
// relation tables all live in their own globals and a turn mutates them too.
//
// This module folds those additional live tables ON TOP of the existing
// HashWorldState digest, so determinism/diff covers the whole reconstructed
// world. It does NOT redefine or re-fold the base regions — it calls
// play::SnapshotWorld() for those and then appends the extra regions in a FIXED
// order (documented in world_digest.cpp). The result is still an order-stable
// 64-bit FNV-1a digest plus a per-region list for localizing a divergence.
//
// It reads the REAL world globals via extern (declared in their home headers:
// sim/building.h, sim/building_production.h, sim/building_create.h, world/city.h,
// sim/command_apply5.h, sim/command_apply6.h, sim/actionqueue.h, world/law.h,
// world/event.h, world/office.h, world/crime.h, world/relation.h) — it never
// redefines them.
#include <cstdint>
#include <string>

#include "play/determinism.h"   // Fnv1a64, WorldSnapshot, CompareSnapshots

namespace guild::play {

// Hash the ENTIRE simulated world: the base entity digest (HashWorldState) plus
// every additional live world table, folded in a fixed order. Order-stable and
// reproducible; any single changed field in any folded table flips the digest.
std::uint64_t HashFullWorld();

// Same fold, but records each region's sub-hash for diffing. The region list
// begins with all of SnapshotWorld()'s base regions (persons..rng) and then the
// appended full-world regions, in the fixed fold order.
WorldSnapshot SnapshotFullWorld();

} // namespace guild::play
