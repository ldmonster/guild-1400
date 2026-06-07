#pragma once
// real_data_driver — real-asset DATA-TABLES driver for the Guild bring-up
// (gilde.exe). INTEGRATION GLUE, not a translation (no VIBE_* provenance).
//
// Given a shim::IFileSystem rooted at a real "Die Gilde — Europe 1400" install
// (or any fixture that provides the same files), this drives the reconstructed
// data-table loaders end to end over the bound files and CROSS-VALIDATES the
// loaded tables, returning a small result struct:
//
//   1. data/A_Geb.dat  -> the 72 building-type records   (world::WorldLoad...).
//   2. data/A_Obj.dat  -> the 731 scene/object-type records.
//   3. the derived per-object-type remap table (byte_13CE862, 731 entries) the
//      loader builds via world::WorldInitBuildingTypeTable.
//   4. the in-memory 28-entry economy good/profession ("goods/prices") table the
//      engine seeds via world::CityInitParameterTable (the prices/drift/cap
//      constants live in code, not on disk in this title).
//
// Then it cross-validates the loaded data:
//   * counts match the engine's fread counts (72 / 731 / 28),
//   * every building-type kind byte is in the catalog enum range (1..0x1A or 0),
//   * every building's room-list references (the object-type ids in roomList[],
//     present-bit stripped) RESOLVE into the 731 scene-type table — referential
//     integrity between A_Geb (rooms) and A_Obj (object types),
//   * every loaded record name is a printable, in-stride C string ("names"),
//   * the derived remap table is populated and mirrors into the shared consumer
//     table, and the goods table is seeded with the expected price constants.
//
// "Recipes" in this title are the per-building-type input/output profession +
// factor fields (BuildingTypeField::kInputProf/kOutputProf/kInputFactor/
// kOutputFactor) carried INSIDE each A_Geb record; the driver counts the records
// that carry a non-trivial recipe so callers can report it.
//
// The data loaders read through `fs` directly (guild::io::FileOpen); no global
// VFS bind is required for the .dat path. The driver exposes an optional
// hooks struct (inert by default) so an unreconstructed post-load callee can be
// observed/overridden by tests without src/ ever referencing a test symbol.
#include "shim/IFileSystem.h"

#include <string>

namespace guild::app {

// Per-table + cross-validation results of a real-data-tables load.
struct RealDataResult {
    // ---- load status ----
    bool loaded = false;           // WorldLoadBuildingAndObjectData returned 0
    int  loadRc = -100;            // raw loader return code (0 ok; -3 Geb, -6 Obj)

    // ---- counts observed ----
    int buildingTypeCount = 0;     // building-type records loaded (expect 72)
    int sceneTypeCount    = 0;     // scene/object-type records loaded (expect 731)
    int goodCount         = 0;     // economy good/price slots seeded (expect 28)

    // ---- cross-validation ----
    bool countsMatch       = false; // 72 / 731 / 28 as expected
    bool kindsInRange      = false; // every building kind byte in [0..0x1A]
    bool namesPrintable    = false; // every loaded record name is in-stride printable
    bool refsResolve       = false; // every room-list object-type ref < sceneTypeCount
    bool remapPopulated    = false; // derived remap table has non-zero entries
    bool remapMirrored     = false; // shared 256-wide consumer table mirrors the low part
    bool goodsSeeded       = false; // goods table seeded with the expected constants

    // ---- detail counters (for reporting) ----
    int  roomRefsChecked   = 0;     // total room-list object refs examined
    int  roomRefsResolved  = 0;     // of those, refs that resolved into the scene table
    int  recipeRecords     = 0;     // building types carrying a non-trivial recipe
    int  namedBuildings    = 0;     // building records with a non-empty name
    int  namedScenes       = 0;     // scene records with a non-empty name

    // True iff load succeeded AND every cross-validation flag passed.
    bool ok() const {
        return loaded && countsMatch && kindsInRange && namesPrintable &&
               refsResolve && remapPopulated && remapMirrored && goodsSeeded;
    }
};

// Installable post-load hook with an inert default. The original boot reseeds an
// RNG and pokes a few render/scene leaves after the data load; those are out of
// scope for a data driver, so the single reachable unreconstructed callee is
// routed through this hook (default = no-op). Tests may install a counter to
// observe that the driver invoked the post-load step exactly once per load.
struct RealDataHooks {
    void (*onPostLoad)(const RealDataResult& r) = nullptr;  // null => inert no-op
};

// Install (or clear, with nullptr) the process-wide data-driver hooks. Returns
// the previous hooks so a test can restore them.
RealDataHooks SetRealDataHooks(const RealDataHooks* hooks);

// Drive the real data tables: load A_Geb.dat / A_Obj.dat from "<dir>" through
// the reconstructed loaders over `fs`, seed the goods/price table, then
// cross-validate. `dir` is prefixed verbatim to the file names (e.g. "data/").
// `capDivisor` is forwarded to CityInitParameterTable (the FP value the original
// leaves in flt_641DA8); a sane default of 1.0 is used by the no-arg overload.
RealDataResult LoadRealDataTables(shim::IFileSystem* fs, const char* dir,
                                  float capDivisor);
inline RealDataResult LoadRealDataTables(shim::IFileSystem* fs, const char* dir) {
    return LoadRealDataTables(fs, dir, 1.0f);
}

} // namespace guild::app
