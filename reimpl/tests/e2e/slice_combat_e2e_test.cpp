// tests/e2e/slice_combat_e2e_test.cpp — GUARDED real-asset COMBAT / INTRIGUE slice on
// AUGSBURG. The full end-to-end proof:
//
//   load AUGSBURG -> click a hostile action on a rival ->
//     (A) ATTACK an enemy unit -> REAL opcode-80 command (BuildAttackPacket 0x488a4c
//         -> RequestBuildOp80 0x495874) -> REAL apply writing the order kind=2 into
//         the target's folded record pad
//     (B) CRIME -> REAL ExAddStraftat (0x498f44) record-write core: a g_crimeTable
//         slot gets the crime record + the folded g_relationMatrix cell sours
//   -> advance one real game-day -> assert the folded field moved (real before->after)
//      AND the whole run is byte-identical on rerun.
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/slice_combat.h"
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

CombatInteraction AugsburgAttack() {
    CombatInteraction it;
    it.action      = HostileAction::kAttack;
    it.attackerId  = 31337;
    it.targetId    = 90001;   // synthetic target seeded into slot 0 by RunCombatSlice
    it.attackTileX = 23;
    it.attackParam = 2;
    return it;
}

CombatInteraction AugsburgCrime() {
    CombatInteraction it;
    it.action        = HostileAction::kCrime;
    it.perpetratorId = 5;
    it.victimId      = 14;
    it.crimeLocation = 9;
    it.crimeType     = CrimeType::kArson;
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// ATTACK slice on AUGSBURG: real before->after + deterministic rerun.
// ---------------------------------------------------------------------------
TEST(SliceCombatE2E, AugsburgAttackEvolvesWorldDeterministically) {
    if (!RealAssetsPresent()) {
        std::printf("[combat-e2e] AUGSBURG assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    const std::uint32_t seed = 0x5EA7;
    CombatInteraction it = AugsburgAttack();

    shim::DiskFileSystem fs(GameDir());
    CombatSliceResult r = RunCombatSlice(&fs, GameDir(), "Augsburg", it, seed);

    std::printf("[combat-e2e] attack loaded=%d persons=%u objects=%u opcode=%d kind=%d ring=%d applied=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                r.attackOpcode, (int)r.attackOrderKind, r.attackRingSlot,
                (int)r.attackApplied);
    std::printf("[combat-e2e] attack pad %d->%d daySteps=%d hLoad=%llu hCmd=%llu hDay=%llu\n",
                r.padKindBefore, r.padKindAfter, r.daySteps,
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);

    CHECK(r.loaded);
    CHECK(r.attackBuilt);
    CHECK_EQ(r.attackOpcode, 80);
    CHECK_EQ((int)r.attackOrderKind, 2);
    CHECK(r.attackApplied);
    CHECK_EQ(r.padKindBefore, 0);
    CHECK_EQ(r.padKindAfter, 2);
    CHECK(r.combatChanged());
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.daySteps > 0);
    CHECK(r.ok());

    // Determinism: a full rerun reproduces byte-identical hashes.
    CombatSliceResult r2 = RunCombatSlice(&fs, GameDir(), "Augsburg", it, seed);
    CHECK_EQ((long long)r2.hashAfterLoad, (long long)r.hashAfterLoad);
    CHECK_EQ((long long)r2.hashAfterCommand, (long long)r.hashAfterCommand);
    CHECK_EQ((long long)r2.hashAfterDay, (long long)r.hashAfterDay);

    std::printf("[combat-e2e] attack determinism OK; rerun hDay=%llu\n",
                (unsigned long long)r2.hashAfterDay);
}

// ---------------------------------------------------------------------------
// CRIME slice on AUGSBURG: real before->after + deterministic rerun.
// ---------------------------------------------------------------------------
TEST(SliceCombatE2E, AugsburgCrimeEvolvesWorldDeterministically) {
    if (!RealAssetsPresent()) {
        std::printf("[combat-e2e] AUGSBURG assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    const std::uint32_t seed = 0xC0DE;
    CombatInteraction it = AugsburgCrime();

    shim::DiskFileSystem fs(GameDir());
    CombatSliceResult r = RunCombatSlice(&fs, GameDir(), "Augsburg", it, seed);

    std::printf("[combat-e2e] crime loaded=%d persons=%u objects=%u slot=%d id=%d perp=%d victim=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                r.crimeSlot, r.crimeId, r.crimePerp, r.crimeVictim);
    std::printf("[combat-e2e] crime rel %d->%d daySteps=%d hLoad=%llu hCmd=%llu hDay=%llu\n",
                r.relationBefore, r.relationAfter, r.daySteps,
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);

    CHECK(r.loaded);
    CHECK(r.crimeSlot >= 0);
    CHECK_EQ(r.crimePerp, 5);
    CHECK_EQ(r.crimeVictim, 14);
    CHECK_EQ(r.relationBefore, 0);
    CHECK_EQ(r.relationAfter, -10);
    CHECK(r.combatChanged());
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.daySteps > 0);
    CHECK(r.ok());

    CombatSliceResult r2 = RunCombatSlice(&fs, GameDir(), "Augsburg", it, seed);
    CHECK_EQ((long long)r2.hashAfterLoad, (long long)r.hashAfterLoad);
    CHECK_EQ((long long)r2.hashAfterCommand, (long long)r.hashAfterCommand);
    CHECK_EQ((long long)r2.hashAfterDay, (long long)r.hashAfterDay);

    std::printf("[combat-e2e] crime determinism OK; rerun hDay=%llu\n",
                (unsigned long long)r2.hashAfterDay);
}
