// Verifies InstallRealCombatWiring binds CombatSlots5Hooks.spawnThrownBomb to the
// reconstructed VIBE_Object_SpawnThrownBomb (0x4869dc), so Combat_ThrowBombAction
// (0x48ce88) reaches the real slot-alloc + ballistic-descriptor control flow (rule 13).
#include "tests/framework/test.h"

#include "sim/wire_combat.h"
#include "sim/combat_slots5.h"
#include "sim/object_throwbomb.h"

using namespace guild;
using namespace guild::sim;

TEST(WireCombatBomb, ThrowBombActionReachesReconstructedSpawn) {
    ResetBombTable();
    SetThrowBombHooks(nullptr);            // inert engine leaves (headless)
    InstallRealCombatWiring();

    // The hook is now bound (was inert/null before the wiring pass).
    CHECK(GetCombatSlots5Hooks().spawnThrownBomb != nullptr);

    // Drive the real ThrowBombAction: not the local side (skip the request-22 path),
    // just the always-spawn tail -> the wired SpawnThrownBomb allocates slot 0.
    BombActionInput in{};
    in.isLocalSide = false;
    in.hasActiveTarget = false;
    in.unitNodePos = 0;
    in.throwArgA = 42;                     // target tile
    in.throwArgB = 7;                      // arg
    BombActionOutcome out = ThrowBombAction(in, /*activeTarget=*/nullptr);

    CHECK(out.spawnedThrownBomb);
    CHECK_EQ(out.thrownBombResult, 0);     // first bomb -> slot 0
    // The reconstructed spawn recorded the throw into the bomb table.
    CHECK_EQ(g_bombTable[0].tile, 42);
    CHECK_EQ(g_bombTable[0].arg, 7);

    SetCombatSlots5Hooks(nullptr);
    ResetBombTable();
}
