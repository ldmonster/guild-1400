#pragma once
// gilde.exe — guild::io  (MODULE: savegames — GameState save/load orchestration)
//
// The top-level writer/loader (VIBE_Save_WriteGameFile @0x5a348c /
// VIBE_Save_LoadGameFile @0x5a7604) drive a fixed sequence:
//
//   write: OpenFile("wb")
//          -> RelinkPersonObjects        (pre-save pointer->id, slot fixup)
//          -> WriteScenarioBlock         (header + thumbnail)
//          -> <scalar block, field-by-field>
//          -> <table writers...>         (Person/Building/GameState/Object/Amt/...)
//          -> CloseStream
//          -> RelinkPersonRecords        (restore id->pointer)
//
//   load:  OpenFile("rb")
//          -> reset world (Building_ResetAllBuildings / CharAction_QueueFreeAll /
//                          Object_DestroySpawnedEntities)
//          -> LoadHeaderAndThumbnail     (version + name + timestamp + thumbnail)
//          -> validate 0x10026 <= version <= 0x10045
//          -> <scalar block, version-gated>
//          -> <table loaders...>
//          -> RelinkLoadedPointers       (id->pointer, type-tagged)
//          -> CloseStream
//
// This module models that orchestration around the self-contained, reconstructed
// pieces (header, scalar block, relink table). The per-table serializers belong to
// other modules (sim/world) and are out of scope for this slice; here a `GameState`
// carries the recovered, byte-exact portions so the determinism / persistence
// invariant (save -> load -> byte-identical reconstruction) is testable end-to-end.
#include "guild/common/types.h"
#include "io/save.h"
#include "io/vfs.h"
#include <cstddef>
#include <vector>

namespace guild::io {

// A self-contained snapshot of the byte-exact, reconstructed game state: the save
// header, the fixed scalar block, and the pointer<->id relink table. (The full
// engine state additionally holds the Person/Building/Object/Amt tables, owned by
// other modules and excluded from this slice.)
struct GameState {
    SaveHeader      header{};
    SaveScalarBlock scalar{};
    // The 32768 x 10-byte relink table (dword_B5FB66 / byte_B5FB61). Stored as the
    // raw blob so round-trip byte-equality is testable; resolvers convert the
    // pointer/id slots on save/load.
    std::vector<guild::u8> relink;  // kRelinkBytes when populated

    // The raw thumbnail bytes (0xE100), optional.
    std::vector<guild::u8> thumbnail; // empty or kThumbnailBytes
};

// VIBE_Save_WriteGameFile @0x5a348c (header + scalar phase) — open `path` for write
// through the VFS, write the scenario header (+ thumbnail) and the scalar block in
// the exact field order, then close. Pre-save relink (pointer->id) is applied to
// `state.relink` via `save` resolvers if provided. Returns true on success.
//
// `partial` mirrors the bl&2 mode bit: when set, the original skips the
// law/map/object/etc. table phase. Here it only documents intent (tables are out of
// scope); the header+scalar+relink sequence is identical either way.
bool WriteGameState(const char* path, GameState& state,
                    const RelinkResolvers* save /*pointer->id, may be null*/,
                    bool partial);

// VIBE_Save_LoadGameFile @0x5a7604 (header + scalar phase) — open `path` for read,
// load + version-gate the header (+ thumbnail), validate the version range
// (0x10026..0x10045), read the scalar block, then close. Post-load relink
// (id->pointer) is applied to `state.relink` via `load` resolvers if provided.
// Returns true on a fully-parsed, version-valid stream.
bool LoadGameState(const char* path, GameState& state,
                   const RelinkResolvers* load /*id->pointer, may be null*/);

} // namespace guild::io
