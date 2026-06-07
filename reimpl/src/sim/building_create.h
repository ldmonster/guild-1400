#pragma once
#include "guild/common/types.h"
#include "sim/types.h"

// gilde.exe — Building create leaf (namespace guild::sim).
//
// VIBE_Building_CreateGebaeude @0x586fb8 allocates a fresh building record in the
// shared object/building array (dword_13CE298, stride 169) for (typeByte, ownerWord),
// initialises ~40 fields (default worker capacities, stock/price defaults, scene
// links, name selection by RNG), spawns default sub-objects, and returns the new
// record. The full original is ~0x600 bytes and is deeply coupled to the render /
// scene leaves (VIBE_Scene_LoadObjectGroup, VIBE_Light_*, Avatar, RNG name tables,
// VIBE_Building_EnsureDefaultObjects, …) and to dozens of float/string tables that
// are not present in the cold IDB.
//
// The command-apply handlers (opcodes 0x0A / 0x3A / 0x4C) call CreateGebaeude as a
// LEAF: they only observe the returned record's id (read at obj+1) and a small set
// of fields they then overwrite. So we model the leaf as a mockable hook with a
// faithful default backend that allocates from the real g_objects array
// (entity.cpp) and stamps the record's alive byte (+0 = typeByte), id (+1), owner
// word (+37/+39), and the default scalar fields the originals initialise — enough
// for the apply handlers + tests to round-trip. Tests can install a spy.
//
// The full geometry/scene-graph leaf is listed as DEFERRED in the module report.

namespace guild::sim {

// VIBE_Building_CreateGebaeude @0x586fb8 — create a building record.
//   typeByte : building type/prototype (obj +0 alive/type byte).
//   ownerWord: owner person index word (obj +37 / +39).
// Returns the new record's flat base (>= 169 bytes), or nullptr if the array is
// full / not loaded. The id is at obj+1 (unaligned dword); the apply handlers read
// it back into the last-created-scene token (dword_63128C).
using CreateGebaeudeFn = u8* (*)(u8 typeByte, u16 ownerWord);
void SetCreateGebaeudeHook(CreateGebaeudeFn fn);

// Direct entry (calls the installed hook). Apply handlers use this.
u8* Building_CreateGebaeude(u8 typeByte, u16 ownerWord);

// The default backend's next-id allocator base (dword_649890 in the binary; the
// originals seed the new record id from it and post-increment). Reset by tests.
extern i32 g_buildingNextId; // dword_649890

// Reset the default backend's allocation state (test helper, not in orig).
void ResetBuildingCreate();

} // namespace guild::sim
