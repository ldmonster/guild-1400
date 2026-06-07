#pragma once
// gilde.exe — session / save flow (guild::play). PLAYABLE_PLAN P3, M3.
//
// This module assembles the reconstructed save/load layer into a playable SESSION
// FLOW: it (1) seeds a small LIVE world into the engine's canonical entity arrays
// (sim::g_persons / sim::g_objects) for a new game, (2) runs a few deterministic
// turns over that live world, and (3) round-trips the whole session through the
// REAL io save/load spine + per-table serializers so the simulated state survives a
// save -> reload.
//
// What it WIRES (de-inert through the live path, not just a unit fixture):
//   * io::WriteGameState / io::LoadGameState @0x5a348c / 0x5a7604 — the top-level
//     savegame writer/loader (header + scalar block) — driven over a real in-memory
//     VFS so the bytes actually hit a stream and parse back.
//   * io::SaveWritePersonRecords / io::SaveLoadPersonRecords (object/building table,
//     169-stride) AND io::SaveWriteGameStateHeaderPreamble + SaveWritePersonScene-
//     Record / their loaders (person/scene table, 536-stride) — the per-table
//     serializers that persist the genuinely-simulated entity arrays. Without these
//     the .SAV would carry only the scalar header and the sim state would NOT
//     round-trip; wiring them makes the round-trip prove persistence.
//   * io::LoadWorld @0x5a7604 (the full .cty world load driver from the prior wave) —
//     a bonus guarded path that seeds the live world from the REAL AUGSBURG.cty when
//     the shipped asset is present.
//
// Everything is headless and deterministic: the new-game seed derives every field
// from crt::Srand(seed) + crt::RandNext(), so the same seed reproduces the same
// world (PLAYABLE_PLAN B1 self-consistency), and a save taken AFTER simulating turns
// differs byte-wise from the pre-sim save (proving sim state is persisted).
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "io/gamestate.h"   // io::GameState / WriteGameState / LoadGameState
#include "io/save.h"        // SaveScalarBlock / SaveHeader / version

namespace guild::play {

// ===========================================================================
// SessionConfig — the new-game seed parameters.
// ===========================================================================
struct SessionConfig {
    std::uint32_t seed     = 0;     // crt::Srand seed (determinism anchor)
    int           persons  = 0;     // live person/scene records to seed (<= cap)
    int           objects  = 0;     // live object/building records to seed (<= cap)
    std::string   cityName = "Augsburg";
};

// ===========================================================================
// SessionWorld — the live session snapshot. The PERSON/OBJECT bodies live in the
// engine-canonical sim::g_persons / sim::g_objects arrays (the new-game seed and the
// turn simulator write THOSE directly, so the live path is exercised); this struct
// carries the scalar GameState header + the table record COUNTS so save/load can
// serialize the live arrays and a reload can be compared for equivalence.
// ===========================================================================
struct SessionWorld {
    io::GameState  state;            // header + scalar block (the .SAV scalar phase)
    std::uint32_t  personCount = 0;  // live person/scene records (536-stride table)
    std::uint32_t  objectCount = 0;  // live object/building records (169-stride table)

    // A standalone copy of the two live tables, captured at save time so a reload can
    // round-trip them without aliasing the global arrays. Empty until SaveSession.
    std::vector<guild::u8> personBytes; // personCount * 536
    std::vector<guild::u8> objectBytes; // objectCount * 169
};

// ===========================================================================
// New game: seed a small live world into the engine entity arrays deterministically.
// Resets sim::g_persons / sim::g_objects, then fills `cfg.persons` person/scene
// records and `cfg.objects` object/building records with seed-derived fields, and
// builds the matching io::GameState header/scalar block. The same `cfg` (same seed)
// always yields the identical world (self-consistency invariant).
// ===========================================================================
void NewGame(const SessionConfig& cfg, SessionWorld& world);

// ===========================================================================
// Simulate `turns` game turns over the LIVE world (sim::g_persons / sim::g_objects).
// Each turn deterministically advances the scalar game clock, ages every live person
// (cash accrues, a per-turn bitfield rotates) and touches every live object. Returns
// the number of person-record mutations applied (a state-actually-changed witness).
// Must be called after NewGame for the same `world`.
// ===========================================================================
int RunTurns(SessionWorld& world, int turns);

// ===========================================================================
// Save the live session to `path` through the REAL io save spine: capture the live
// entity tables into `world` (personBytes/objectBytes), then write the scalar
// header + scalar block (io::WriteGameState) followed by the two per-table
// serializers, all to one in-memory VFS stream. Returns true on a full write.
// (A VFS filesystem must already be bound via io::VfsInit.)
// ===========================================================================
bool SaveSession(SessionWorld& world, const char* path);

// ===========================================================================
// Load a session previously written by SaveSession at `path` into `out`: read back
// the scalar header + block (io::LoadGameState) and the two entity tables (the
// per-table loaders), populating out.state + out.personBytes/out.objectBytes and the
// live sim::g_persons / sim::g_objects arrays. Returns true on a full, version-valid
// load. `personCount`/`objectCount` give the table sizes to read (carried out of band,
// as the original takes them from the just-loaded GameStateHeader preamble).
// ===========================================================================
bool LoadSession(const char* path, std::uint32_t personCount,
                 std::uint32_t objectCount, SessionWorld& out);

// ===========================================================================
// Structural / byte equivalence between two sessions (a round-trip oracle).
// ===========================================================================
struct EquivResult {
    bool headerEqual  = false;   // SaveHeader byte-identical
    bool scalarEqual  = false;   // SaveScalarBlock byte-identical
    bool personsEqual = false;   // person table byte-identical (count + bytes)
    bool objectsEqual = false;   // object table byte-identical (count + bytes)
    bool allEqual() const {
        return headerEqual && scalarEqual && personsEqual && objectsEqual;
    }
};
EquivResult CompareSessions(const SessionWorld& a, const SessionWorld& b);

// ===========================================================================
// Bonus guarded path: load the REAL shipped <root>/Resources/gamedata/Cities/
// AUGSBURG.cty (gzip-framed partial city seed) through io::LoadWorld into the live
// arrays. Returns true on a successful real-asset load. (A VFS filesystem rooted at
// `root` must already be bound; see the e2e test.) `outPersonCount`/`outObjectCount`
// receive the live record counts the driver scattered.
// ===========================================================================
bool LoadRealCity(const char* path, std::uint32_t* outPersonCount,
                  std::uint32_t* outObjectCount);

} // namespace guild::play
