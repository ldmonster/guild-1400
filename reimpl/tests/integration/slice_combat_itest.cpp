// tests/integration/slice_combat_itest.cpp — the COMBAT / INTRIGUE slice on a small
// real-FORMAT world (synthetic entity records + the live g_crimeTable/g_relationMatrix
// tables; no shipped assets). Drives the real loop:
//   seed -> REAL hostile command (attack opcode-80 OR crime ExAddStraftat) -> one
//   game-day, and asserts:
//   * a FOLDED combat/crime field changed (the target's order pad, or the crime
//     record + relation cell),
//   * HashFullWorld() differs pre/post the command AND across the whole slice,
//   * the run is BYTE-IDENTICAL on a rerun (ZeroWorldGlobals blanks the crime/relation
//     tables, Srand anchors the RNG, and the day dirties g_crime/g_relation so they
//     MUST be re-zeroed for the compare).
#include "test.h"

#include "play/slice_combat.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "world/city.h"
#include "world/office.h"
#include "world/law.h"
#include "world/crime.h"
#include "world/relation.h"
#include "world/event.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::world;
using namespace guild::sim;

namespace {

CombatInteraction AttackIt() {
    CombatInteraction it;
    it.action      = HostileAction::kAttack;
    it.attackerId  = 8001;
    it.targetId    = 705;       // matches a seeded object id below
    it.attackTileX = 17;
    it.attackParam = 1;
    return it;
}

CombatInteraction CrimeIt() {
    CombatInteraction it;
    it.action        = HostileAction::kCrime;
    it.perpetratorId = 4;
    it.victimId      = 9;
    it.crimeLocation = 12;
    it.crimeType     = CrimeType::kTheft;
    return it;
}

// Blank the folded combat/crime tables, seed a real-format substrate, install the
// attack resolver. Mirrors RunCombatSlice's preamble (no shipped assets). RunGameDay
// dirties g_crime/g_relation, so a faithful re-run MUST re-zero them here.
void SeedCombatWorld(std::uint32_t seed) {
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_persons, 0, sizeof(g_persons));
    ResetEntityArrays();

    std::memset(g_lawTable, 0, sizeof(g_lawTable));
    std::memset(g_officeHolders, 0, sizeof(g_officeHolders));
    std::memset(g_crimeTable, 0, sizeof(g_crimeTable));
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
    std::memset(g_eventTable, 0, sizeof(g_eventTable));
    g_eventTableCount = 0;
    g_missionLcgState = 0;

    crt::Srand(seed);
    CityInitParameterTable(100.0f);
    g_capDivisor = 0.0f;
    g_cityTotalMoney = 0.0f;
    g_cityTotalGoods = 0.0f;

    // A few alive object records (the attack target is id 705 in slot 0).
    g_objects[0].alive = 1; g_objects[0].id = 705;
    for (int i = 1; i < 6; ++i) { g_objects[i].alive = 1; g_objects[i].id = 700 + i * 3; }
}

} // namespace

// ---------------------------------------------------------------------------
// ATTACK: the folded order pad changes + the world hash moves across the slice.
// ---------------------------------------------------------------------------
TEST(SliceCombatItest, AttackFieldAndHashEvolveOverSlice) {
    const std::uint32_t seed = 0xC0FFEE;
    CombatInteraction it = AttackIt();

    SeedCombatWorld(seed);
    // The combat resolver pins target id 705 to slot 0 (RunCombatSlice's
    // InstallCombatResolver; here we use the inert default -> REAL BuildingFindById).
    SetCombatApplyHooks(nullptr);

    crt::Srand(seed);
    std::uint64_t hLoad = HashFullWorld();

    u8* base = reinterpret_cast<u8*>(&g_objects[0]);
    int padBefore = base[0x60];

    CombatPacket pkt = BuildCombatPacket(it);
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 80);
    CHECK_EQ((int)pkt.orderKind, 2);
    int applied = ApplyCombatPacket(pkt, it);
    CHECK_EQ(applied, 1);
    int padAfter = base[0x60];

    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();

    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    RunGameDay(seed, st);
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    std::printf("[combat-itest] attack pad %d->%d hLoad=%llu hCmd=%llu hDay=%llu\n",
                padBefore, padAfter, (unsigned long long)hLoad,
                (unsigned long long)hCmd, (unsigned long long)hDay);

    CHECK_EQ(padBefore, 0);
    CHECK_EQ(padAfter, 2);                           // attack order kind
    CHECK(hCmd != hLoad);                            // g_objects is folded
    CHECK(hDay != hLoad);
    CHECK(hDay != hCmd);

    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// CRIME: the folded crime record + relation change + the world hash moves.
// ---------------------------------------------------------------------------
TEST(SliceCombatItest, CrimeRecordAndHashEvolveOverSlice) {
    const std::uint32_t seed = 0xBEEF;
    CombatInteraction it = CrimeIt();

    SeedCombatWorld(seed);
    crt::Srand(seed);
    std::uint64_t hLoad = HashFullWorld();

    int relBefore = RelationGet(it.victimId, it.perpetratorId);
    int slot = ApplyCrimeCommand(it, /*crimeId=*/2025);
    CHECK(slot >= 0);
    int relAfter = RelationGet(it.victimId, it.perpetratorId);

    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();

    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    RunGameDay(seed, st);
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    std::printf("[combat-itest] crime slot=%d id=%d rel %d->%d hLoad=%llu hCmd=%llu hDay=%llu\n",
                slot, g_crimeTable[slot].id, relBefore, relAfter,
                (unsigned long long)hLoad, (unsigned long long)hCmd,
                (unsigned long long)hDay);

    CHECK_EQ(g_crimeTable[slot].id, 2025);
    CHECK_EQ(g_crimeTable[slot].perpetrator, 4);
    CHECK_EQ(g_crimeTable[slot].target, 9);
    CHECK_EQ(relBefore, 0);
    CHECK_EQ(relAfter, -10);
    CHECK(hCmd != hLoad);                            // g_crimeTable + g_relationMatrix folded
    CHECK(hDay != hLoad);
    CHECK(hDay != hCmd);

    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// Determinism: a full rerun reproduces byte-identical hashes (re-zeroing the
// crime/relation tables the prior day dirtied).
// ---------------------------------------------------------------------------
TEST(SliceCombatItest, FullSliceIsDeterministic) {
    const std::uint32_t seed = 0xBADA55;

    auto runCrime = [&](std::uint64_t& hLoad, std::uint64_t& hCmd, std::uint64_t& hDay) {
        SeedCombatWorld(seed);
        crt::Srand(seed);
        hLoad = HashFullWorld();
        ApplyCrimeCommand(CrimeIt(), 5555);
        crt::Srand(seed);
        hCmd = HashFullWorld();
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
        crt::Srand(seed);
        hDay = HashFullWorld();
    };

    std::uint64_t aLoad, aCmd, aDay, bLoad, bCmd, bDay;
    runCrime(aLoad, aCmd, aDay);
    runCrime(bLoad, bCmd, bDay);

    std::printf("[combat-itest] det A(%llu,%llu,%llu) B(%llu,%llu,%llu)\n",
                (unsigned long long)aLoad, (unsigned long long)aCmd, (unsigned long long)aDay,
                (unsigned long long)bLoad, (unsigned long long)bCmd, (unsigned long long)bDay);

    CHECK_EQ((long long)aLoad, (long long)bLoad);
    CHECK_EQ((long long)aCmd, (long long)bCmd);
    CHECK_EQ((long long)aDay, (long long)bDay);
    CHECK(aCmd != aLoad);
    CHECK(aDay != aCmd);

    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// RunCombatStepsSynthetic agrees on the combat delta for the same seeded world.
// ---------------------------------------------------------------------------
TEST(SliceCombatItest, SyntheticStepsAgreeOnCombatDelta) {
    CombatStepHash h[3];
    i32 before = -1, after = -1;
    int n = RunCombatStepsSynthetic(0x9753, AttackIt(), h, 3, &before, &after);
    CHECK_EQ(n, 3);
    CHECK_EQ((int)before, 0);
    CHECK_EQ((int)after, 2);
    CHECK(h[1].mutated);
    CHECK(h[2].mutated);
    ResetEntityArrays();
}
