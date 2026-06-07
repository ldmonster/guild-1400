#pragma once
// Wave 29 PLAY — P3 / Milestone M3: save/load ROUND-TRIP fidelity (namespace
// guild::play).
//
// The plan's M3 invariant is "load -> save -> reload is equivalent". Everything
// before this wave was AUGSBURG-only; this module builds the round-trip over the
// FAITHFUL field-selective savegame format and proves it for EVERY shipped city
// (AUGSBURG / BERLIN / DRESDEN / HANNOVER / KOELN).
//
// There is no shipped SAVE oracle (Resources/gamedata/Saves is empty), so M3 here
// is a strict SELF-CONSISTENCY proof, not a diff against an original SAVE:
//
//   load .cty (the shipped city seed) into the live world
//     -> HashFullWorld()  ==> h1
//     -> SAVE the live world into a memory stream  (save stream S1)
//     -> zero the world + RELOAD from S1
//     -> HashFullWorld()  ==> h2
//     -> assert h1 == h2          (the save carried the whole world faithfully)
//     -> SAVE the reloaded world again  (save stream S2)
//     -> assert S1 == S2 byte-for-byte  (the save format is a fixed point)
//
// THE SAVE PATH. The original always WRITES at the current version
// (kSaveVersionCurrent == 0x10045) via the per-table write serializers; the read
// path version-gates each field. The shipped `.cty` seeds are version 0x1003B, so
// the round trip is genuinely cross-version: load@0x1003B -> save@0x10045 ->
// reload@0x10045. At 0x10045 EVERY field gate is satisfied, so the WRITE
// serializers (which emit every field) and the READ loaders (which read every
// field) are exact mirrors. The save stream this module emits is fed straight back
// through the SAME io::Load* sub-loaders the real LoadWorld driver calls — so the
// round trip exercises the genuine reconstructed serializers end to end.
//
// We reuse (extern, never redefine):
//   io::LoadWorld / WorldState / the table sub-loaders   (io/save_world_load.h)
//   io::SaveWriteScenarioBlock / SaveWriteScalarBlock    (io/save.h)
//   io::SaveWritePersonTable / SaveWritePersonSceneRecord(io/save_person.h)
//   io::SaveWriteBuildingSlotTables                      (io/save_serial3.h)
//   io::Vfs* memory streams                              (io/vfs.h)
//   play::HashFullWorld                                  (play/world_digest.h)
//   the determinism rig (ZeroWorldGlobals + Srand(seed) before each hash) — the
//   exact play-layer gotchas the playable-slice module documents.
//
// The scene-tile index table and the 164-stride global-counter table have no
// dedicated WRITE serializer sibling (only LOAD), so this module emits their bytes
// directly by inverting the documented LOAD field order (the on-disk layout the
// loaders consume), and reads them back through the REAL loaders.
//
// ADDITIVE (new files): it CALLS the reconstructed siblings; it defines no new
// world state and installs no global hooks. Headless. The per-city e2e is GUARDED
// on the shipped `.cty` assets being present (honors GUILD_GAME_DIR).
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"

namespace guild::play {

// ===========================================================================
// The result of one city round-trip.
// ===========================================================================
struct RoundTripResult {
    bool          loaded       = false;  // the city loaded into the live arrays
    std::uint32_t personCount  = 0;      // dword_647724 (city-record count)
    std::uint32_t objectCount  = 0;      // live object-array slots populated
    std::uint32_t sceneTiles   = 0;      // scene-tile index-table count

    bool          saved        = false;  // the live world serialized to a stream
    bool          reloaded     = false;  // the stream reloaded into a zeroed world

    std::uint64_t hashAfterLoad   = 0;   // h1: HashFullWorld() after the .cty load
    std::uint64_t hashAfterReload = 0;   // h2: HashFullWorld() after save->reload

    std::size_t   saveBytes1    = 0;     // size of the first save stream
    std::size_t   saveBytes2    = 0;     // size of the re-saved stream
    bool          saveBytesStable = false; // S1 == S2 byte-for-byte

    // The central M3 invariant: a save preserved the entire world.
    bool hashEquivalent() const { return loaded && reloaded && hashAfterLoad == hashAfterReload; }
    bool ok() const { return hashEquivalent() && saveBytesStable; }
};

// ===========================================================================
// RoundTripCity — load `ctyPath` into the live world, then prove the M3 invariant
// (load -> save -> reload -> hash equivalence) plus save-byte stability.
//
//   1. mount nothing of its own — the caller must already have bound the VFS to a
//      real game dir (app::MountRealGameAssets) so `ctyPath` resolves; OR pass a
//      path the bound VFS can open. The determinism rig zeroes the world first.
//   2. io::LoadWorld(ctyPath) into the live sim arrays,
//   3. h1 = HashFullWorld() (with the Srand(seed) re-anchor),
//   4. SAVE the live world into a memory stream (the faithful field-selective
//      format, version 0x10045) -> S1,
//   5. ZERO the world + RELOAD from S1,
//   6. h2 = HashFullWorld(),
//   7. SAVE the reloaded world again -> S2; assert S1 == S2.
//
// `seed` re-anchors the CRT RNG right before each HashFullWorld() compare (the
// base digest folds the live RNG state). Returns the per-city result.
RoundTripResult RoundTripCity(shim::IFileSystem* fs, const std::string& ctyPath,
                              std::uint32_t seed);

// Same, but the world is ALREADY loaded into the live arrays (caller did the
// io::LoadWorld). Skips step 2; runs the save->reload->resave invariant over the
// live world. `loadedCounts` (objects/persons/tiles) are reported as-is. Used by
// the unit test on a synthetic live world (no .cty, no VFS file).
RoundTripResult RoundTripLiveWorld(std::uint32_t objectCount, std::uint32_t personCount,
                                   std::uint32_t sceneTileCount, std::uint32_t seed);

// ===========================================================================
// The save / reload primitives (exposed so the three tiers can drive them
// directly). All operate on the LIVE sim arrays + a caller-supplied byte vector.
// ===========================================================================

// Serialize the live world (the portable city-seed subset: header + scalar block
// + scene-tile index table + object table + global counters + person/scene
// records + building-slot tables) into `out`, version 0x10045. `objectCount`,
// `personCount`, `sceneTileCount` bound the records emitted. Returns true on a
// fully-written stream; `out` is resized to the exact stream length.
bool SaveLiveWorld(std::vector<guild::u8>& out,
                   std::uint32_t objectCount, std::uint32_t personCount,
                   std::uint32_t sceneTileCount);

// Reload a world previously written by SaveLiveWorld from `stream` into the live
// sim arrays (zeroing them first via the determinism rig). Fills the recovered
// counts. Returns true on a fully-parsed stream.
bool ReloadSavedWorld(const std::vector<guild::u8>& stream,
                      std::uint32_t* objectCountOut, std::uint32_t* personCountOut,
                      std::uint32_t* sceneTileCountOut);

// Zero EVERY live world table HashFullWorld folds + reseed the economy baseline
// (the play-layer determinism rig). Exposed so a tier can blank the slate.
void RoundTripZeroWorld(std::uint32_t seed);

} // namespace guild::play
