#pragma once
// world_setup — the world-state bootstrap of the Guild game start (gilde.exe).
//
// This module covers the pieces of the world bring-up that are pure data/iteration
// logic (the heavy .dat-parsing + 589-stride AiPlayer table builders are render/io
// data-bound and listed as deferred in the report). It provides:
//   * VIBE_World_CountActiveObjects 0x5839f0 — the canonical name -> scene-type-id
//     resolver over the 65-byte type-def table (used by the city loader to resolve
//     Waehrung / KONTOR object ids, and pervasively elsewhere).
//   * WorldSetupInit — the world-state init that the session start runs: it resets
//     the active universe slot, seeds the city economy parameter table, and clears
//     the world-bootstrap globals (record arrays + the loaded flags).
//
// Namespace: guild::world (uses guild::sim for the universe reset).
#include "guild/common/types.h"

namespace guild::world {

// gilde.exe 0x5839f0 — VIBE_World_CountActiveObjects (__usercall, eax=name).
// Despite the auto-name, this is a name -> type-index lookup: it case-insensitively
// compares `name` against the descriptor name at (typeBase + 65*i + 1) for each of
// up to 731 descriptors (loop bound 47515 = 65*731), returning the matching index
// or 0 if none matched. `typeBase` is the 65-byte scene type-def table base
// (gilde.exe dword_13CE27C; pass guild::sim::g_sceneTypes). Matches the engine's
// VIBE_Util_StrCmpNoCase (returns 0 on equal) loop exactly.
int WorldCountActiveObjects(const char* name, const void* typeBase, int typeStride,
                            int typeCount);

// Convenience binding over the world's own type-def base global (g_worldTypeBase
// == dword_13CE27C), exactly as the engine reads it. Returns 0 if unloaded.
int WorldCountActiveObjects(const char* name);

// ---------------------------------------------------------------------------
// World-state bootstrap.
// ---------------------------------------------------------------------------
// The world-bootstrap record-array base globals (dword_13CE294 building/AiPlayer
// base, dword_13CE298 object/building base, dword_13CE290 scene-node base,
// dword_13CE27C type-def base). In the original these are heap pointers allocated
// by VIBE_World_LoadBuildingAndObjectData; the bootstrap clears them to "unloaded".
extern void* g_worldBuildingBase;  // dword_13CE294
extern void* g_worldObjectBase;    // dword_13CE298
extern void* g_worldSceneBase;     // dword_13CE290
extern void* g_worldTypeBase;      // dword_13CE27C
extern bool  g_worldLoaded;        // mirrors "data is loaded" guard

// WorldSetupInit — the world-state init the session start performs before any
// scene is populated:
//   * resets the active universe slot (guild::sim::UniverseResetCurrentSlot),
//   * seeds the city economy parameter table (CityInitParameterTable),
//   * clears the world-bootstrap record-array globals to the unloaded state.
// `capDivisor` is the economy cap/equilibrium divisor seeded into flt_641DA8.
void WorldSetupInit(float capDivisor);

} // namespace guild::world
