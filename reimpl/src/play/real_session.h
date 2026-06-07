#pragma once
// gilde.exe — REAL-asset save round-trip (guild::play). PLAYABLE_PLAN P3 / M3.
//
// session_flow.{h,cpp} already round-trips a SYNTHETIC seeded world through the real
// io save spine. This module closes the last gap: it round-trips a REAL shipped city
// (AUGSBURG.cty and friends) — load -> simulate K game-days -> save -> reload ->
// prove structural + hash equivalence — without editing session_flow.cpp.
//
// What it WIRES (all real reconstructed code over real bytes):
//   * app::MountRealGameAssets (src/app/real_boot.h) — binds the VFS to the real
//     install and mounts the Resources/*.BIN archives (the front half of a real boot).
//   * io::LoadWorld @0x5a7604 (io/save_world_load.h) — the full .cty world-load
//     driver; for the shipped city seeds (header.flag & 2, version 0x1003B) it takes
//     the PARTIAL path and scatters the scene/object/person tables into the live
//     sim::g_persons / sim::g_objects arrays.
//   * play::RunEconomyTurn (play/turn_economy.h) — the per-DAY economy cascade (the
//     real Amt/Economy passes), so the simulated state genuinely advances before save.
//   * io save spine via play::SaveSession / LoadSession (the real per-table
//     serializers) over a WRITABLE in-memory VFS, to a `.SAV` name (NOT a `.bin`
//     name — a `*.bin` open is read as a PKZIP archive backing).
//   * play::CompareSessions + play::HashFullWorld — the round-trip oracle (byte +
//     full-world-digest equivalence).
//
// Everything is headless and GUARDED: callers pass their own shim::IFileSystem and
// gate on real-asset presence (the test skips cleanly when the install is absent).
#include <cstdint>
#include <string>

#include "play/session_flow.h"        // SessionWorld / SaveSession / LoadSession / CompareSessions
#include "shim/IFileSystem.h"

namespace guild::play {

// ===========================================================================
// LoadRealCity — mount the real game assets and load a city seed into the live
// world. Binds the VFS to `fs` (rooted at the real "Die Gilde" install at
// `gameDir`), mounts the Resources/*.BIN archives, then loads
// "Resources/gamedata/Cities/<UPPER(cityName)>.cty" through io::LoadWorld into the
// live sim::g_persons / sim::g_objects arrays.
//
//   * `fs` must be rooted at the real game directory (so "Gilde.INI",
//     "Resources/gamedata/Cities/AUGSBURG.cty" resolve). The caller owns it.
//   * The VFS stays bound on return (the caller drives further loads/saves through
//     it); the caller is responsible for io::VfsShutdown() when done.
//   * `outPersonCount` / `outObjectCount` receive the live record counts the driver
//     scattered (the .cty's person/scene + object/building tables).
//
// Returns true on a successful real-asset load.
bool LoadRealCity(shim::IFileSystem* fs, const std::string& gameDir,
                  const std::string& cityName,
                  std::uint32_t* outPersonCount,
                  std::uint32_t* outObjectCount);

// ===========================================================================
// CaptureLiveWorld — snapshot the live sim arrays + a synthetic GameState header
// into a SessionWorld so the real save spine (SaveSession) can serialize them.
//
// io::LoadWorld populates the live arrays but does NOT fill a SessionWorld; this
// captures `personCount` person/scene records and `objectCount` object/building
// records from the live arrays, plus a minimal header/scalar block carrying those
// counts, so the loaded real city can be saved and round-tripped.
void CaptureLiveWorld(std::uint32_t personCount, std::uint32_t objectCount,
                      const std::string& cityName, SessionWorld& out);

// ===========================================================================
// The result of a real-asset round-trip (load -> turns -> save -> reload).
// ===========================================================================
struct RealRoundTripResult {
    bool loaded        = false;   // LoadRealCity succeeded
    bool saved         = false;   // SaveSession succeeded
    bool reloaded      = false;   // LoadSession succeeded
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;
    int  turns         = 0;       // economy turns simulated
    int  personMutations = 0;     // person-record mutations applied across the turns

    EquivResult equiv;            // CompareSessions(saved-snapshot, reloaded)
    std::uint64_t hashSaved    = 0;  // HashFullWorld() of the live world at save time
    std::uint64_t hashReloaded = 0;  // HashFullWorld() of the live world after reload

    bool structuralEqual() const { return equiv.allEqual(); }
    bool hashEqual() const { return hashSaved == hashReloaded; }
    bool ok() const {
        return loaded && saved && reloaded && structuralEqual() && hashEqual();
    }
};

// ===========================================================================
// RealRoundTrip — the full M3 path over a real city:
//   1. LoadRealCity(<gameDir>/<cityName>.cty) into the live arrays.
//   2. capture the live world into a SessionWorld.
//   3. run `turns` per-day economy turns (play::RunEconomyTurn) over the live
//      world (advances prices/treasury/production AND consumes the seeded RNG).
//   4. re-capture the (post-turn) live world, take HashFullWorld(), and SaveSession
//      to `savePath` (a `.SAV`/`.dat` name) through the writable VFS.
//   5. LoadSession the saved file back into the live arrays + a SessionWorld.
//   6. take HashFullWorld() of the reloaded world and CompareSessions the saved vs
//      reloaded snapshots.
//
// `econSeed` seeds the economy turn RNG (crt::Srand) for reproducibility. `fs` must
// be a WRITABLE shim::IFileSystem rooted at the real install (it must serve the real
// .cty on read AND accept the .SAV on write). The VFS is bound for the duration and
// shut down on return.
//
// Returns the round-trip result (counts, mutations, equivalence, hashes).
RealRoundTripResult RealRoundTrip(shim::IFileSystem* fs, const std::string& gameDir,
                                  const std::string& cityName, int turns,
                                  std::uint32_t econSeed, const char* savePath);

} // namespace guild::play
