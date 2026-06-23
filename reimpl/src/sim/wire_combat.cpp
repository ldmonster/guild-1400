// See wire_combat.h. Binds the reconstructed combat-cluster leaves to the live
// combat hook bridges. The single directly-bindable field across the five bridges
// is CombatDriversHooks.randomModulo -> Math_RandomModulo; every other field stays
// null (= inert default).
#include "sim/wire_combat.h"

#include "sim/combat_drivers.h"   // CombatDriversHooks / SetCombatDriversHooks
#include "sim/combat.h"           // Math_RandomModulo (gilde.exe 0x58b89c)

// The remaining four bridges are included only so the inventory of inert fields is
// compile-checked against the real struct definitions (and so a future bind has the
// types in scope). No field on them is bound here — they keep their null defaults.
#include "sim/charaction_brawl.h"  // BrawlHooks
#include "sim/combat_slots3.h"     // CombatSlots3Hooks
#include "sim/combat_slots4.h"     // CombatSlots4Hooks
#include "sim/combat_slots5.h"     // CombatSlots5Hooks
#include "sim/object_throwbomb.h"  // SpawnThrownBomb (gilde.exe 0x4869dc)

namespace guild::sim {

namespace {
// Adapter: CombatSlots5Hooks.spawnThrownBomb is `int(int nodePos,int a,int b)`; the
// reconstructed VIBE_Object_SpawnThrownBomb takes the spawn coords + target tile +
// arg. The combat module abstracts the unit's coords pointer as the int `nodePos`,
// so headless the spawn position resolves to the zeroed origin (consistent with the
// module's int contract) — the reconstructed slot-alloc + ballistic-descriptor logic
// runs, with the engine attach/heightmap/anim leaves on their inert ThrowBombHooks.
int Recon_SpawnThrownBomb(int /*nodePos*/, int targetTile, int arg) {
    BombSpawn spawn{};                                   // abstracted origin
    return SpawnThrownBomb(spawn, targetTile, arg, /*outDescriptor=*/nullptr);
}
} // namespace

void InstallRealCombatWiring() {
    // ---- CombatDriversHooks: bind the one signature-compatible leaf ----------
    // `int (*randomModulo)(u16)` == `int Math_RandomModulo(u16)` (0x58b89c). This
    // is the RNG the deployment-AI / auto-resolve / pursuit drivers roll through
    // (DriverRoll -> hook). soundRangeScale (0x485dc0) and buildOrderForUnit
    // (0x48c24c) stay null: their reconstructions take the abstracted CombatUnitAI
    // record, not the `const void*` native record the hook field hands them, so they
    // are not directly assignable here (see wire_combat.h).
    static CombatDriversHooks driversHooks{};
    driversHooks.randomModulo = &Math_RandomModulo;   // 0x58b89c
    SetCombatDriversHooks(&driversHooks);

    // ---- BrawlHooks (0x4d201c) ------------------------------------------------
    // All fields stay INERT. The leaves are cross-module engine calls over native
    // person records; their reconstructed siblings have native-record signatures
    // routed through OTHER bridges (the integration test wires them):
    //   packetStatus           VIBE_Command_GetPacketStatusById  (CommandQueue::GetPacketStatusById)
    //   freeHandlerEntry       VIBE_He_FreeHandlerEntry 0x4c6144  (HandlerTable::FreeHandlerEntry, member)
    //   findPersonById         VIBE_Person_FindRecordById
    //   aggressorRecord        word_12CE910[268*he+8] table addressing
    //   adjustRelationByMood   VIBE_Npc_AdjustRelationByMood 0x56840c (NpcAdjustRelationByMood(HeRecord*,i8))
    //   registerApEvent        VIBE_MeisterAi_RegisterApEvent
    //   sendDefeatMessage      VIBE_He_SendEntityMessage + family defeat bump
    //   bumpVictimFamilyDefeats VIBE_Person_GetFamilyRecord
    //   restorePoseAndRequeue  saved-pose copy + VIBE_Command_QueueRequestEntity29
    // (Mirrors the sibling GroupInteractHooks bridge, which is also left inert.)

    // ---- CombatSlots3Hooks (0x485a54 .. 0x492e6c) ----------------------------
    // All fields stay INERT — GameObject query iterators (gameObjectQueryFind/
    // IterNext/Id/Hp), Command_QueueClearRequest, Window/Widget/Mesh/Form/DragSlot
    // teardown, and the GameLogic frame-loop driver: cross-module render/command
    // leaves with no directly-bindable headless reconstruction.

    // ---- CombatSlots4Hooks (0x4864a0 .. 0x4a4944) ----------------------------
    // All fields stay INERT — text-label/widget projection, escape-delta command,
    // attack/move order issue, blood-pool mesh spawn: render/command/audio leaves.

    // ---- CombatSlots5Hooks (0x486430 .. 0x4897bc) ----------------------------
    // Bind the bomb-spawn leaf to the reconstructed VIBE_Object_SpawnThrownBomb
    // (0x4869dc) so Combat_ThrowBombAction reaches the real slot-alloc + ballistic-
    // descriptor control flow (rule 13). Seed from the inert defaults and override
    // only that field; the rest (person roster iterator, bone-chain world pos, the
    // int(*)(int) randomModulo not assignable from Math_RandomModulo, health-bar
    // window, cutscene form/script/music, voice, scene-walk register) stay inert.
    static CombatSlots5Hooks slots5Hooks = GetCombatSlots5Hooks();
    slots5Hooks.spawnThrownBomb = &Recon_SpawnThrownBomb;   // 0x4869dc
    SetCombatSlots5Hooks(&slots5Hooks);
}

} // namespace guild::sim
