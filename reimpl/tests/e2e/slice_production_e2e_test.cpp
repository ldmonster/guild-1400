// tests/e2e/slice_production_e2e_test.cpp — GUARDED real-asset BUILDINGS/PRODUCTION
// slice on AUGSBURG (Wave 27 P5). The full end-to-end production proof:
//
//   mount AUGSBURG + io::LoadWorld -> seed a workshop production row -> scripted
//   click on a production building -> opcode-28 production order (REAL
//   QueueRequestSlotReset28) -> apply (writes the ordered prot into the slot) ->
//   the REAL per-building production tick -> one game-day (real economy passes),
//   then assert the production yield + treasury evolved, the whole-world hash moved,
//   and the entire run is byte-identical on rerun (deterministic).
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/slice_production.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

const int kProdBuilding = 1;     // production-table building row driven
const i16 kProt         = 60;    // the product the order commits to produce

ProductionSliceResult RunOnce() {
    shim::DiskFileSystem fs(GameDir());
    return RunProductionSlice(&fs, GameDir(), "Augsburg",
                              kProdBuilding, kProt, /*econSeed=*/0xA065B);
}

} // namespace

// ---------------------------------------------------------------------------
// The full production slice on AUGSBURG: order applied, tick + day evolve state,
// deterministic on rerun.
// ---------------------------------------------------------------------------
TEST(SliceProductionE2E, AugsburgProductionSliceEvolvesDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SliceProductionE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[prod-e2e] asset dir: %s\n", GameDir().c_str());

    ProductionSliceResult r = RunOnce();

    std::printf("[prod-e2e] loaded=%d persons=%u objects=%u | cmd issued=%d enqueued=%d "
                "opcode=%d prot=%d slot=%d bld=%d | econPasses=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                (int)r.commandIssued, (int)r.commandEnqueued, r.commandOpcode,
                (int)r.commandProt, r.appliedSlot, r.prodBuilding, r.economyPasses);
    std::printf("[prod-e2e] yield %.2f -> %.2f | smoothIn %.2f -> %.2f | treasury %lld -> %lld\n",
                r.yieldBefore, r.yieldAfter, r.smoothInBefore, r.smoothInAfter,
                (long long)r.treasuryBefore, (long long)r.treasuryAfter);
    std::printf("[prod-e2e] hashAfterLoad=%llu hashAfterCommand=%llu hashAfterDay=%llu\n",
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);

    CHECK(r.loaded);
    if (!r.loaded) { io::VfsShutdown(); return; }

    // The shipped Augsburg seed (stable across the asset).
    CHECK_EQ(r.personCount, (u32)1);
    CHECK_EQ(r.objectCount, (u32)55);

    // The click issued + enqueued the REAL opcode-28 production order onto the slot.
    CHECK(r.commandIssued);
    CHECK_EQ(r.commandOpcode, 28);
    CHECK_EQ((int)r.commandProt, (int)kProt);
    CHECK(r.commandEnqueued);
    CHECK(r.appliedSlot >= 0);
    // The order alone changed the world (the slot record was written).
    CHECK(r.commandChangedWorld());

    // The REAL production tick + day evolved observable state.
    CHECK(r.productionEvolved());
    CHECK(r.yieldAfter != r.yieldBefore);
    CHECK(r.economyPasses > 0);
    CHECK(r.treasuryAfter != r.treasuryBefore);

    // THE CENTRAL PROOF: the whole world changed across the slice.
    CHECK(r.worldChanged());
    CHECK(r.hashAfterLoad != 0u);
    CHECK(r.ok());

    // Determinism: a full rerun reproduces identical hashes + observables.
    ProductionSliceResult r2 = RunOnce();
    std::printf("[prod-e2e] rerun hashAfterLoad=%llu hashAfterDay=%llu yieldAfter=%.2f\n",
                (unsigned long long)r2.hashAfterLoad,
                (unsigned long long)r2.hashAfterDay, r2.yieldAfter);
    CHECK_EQ(r.hashAfterLoad, r2.hashAfterLoad);
    CHECK_EQ(r.hashAfterCommand, r2.hashAfterCommand);
    CHECK_EQ(r.hashAfterDay, r2.hashAfterDay);
    CHECK_EQ(r.yieldAfter, r2.yieldAfter);
    CHECK_EQ((long long)r.treasuryAfter, (long long)r2.treasuryAfter);

    io::VfsShutdown();
}
