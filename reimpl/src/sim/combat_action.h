#pragma once
// gilde.exe — Combat ATTACK-ACTION sequencing + anim-coupled melee resolver
// (namespace guild::sim). MODULE: combat (prefix VIBE_Combat_*). Deferred by the
// earlier combat agent (combat.cpp DEFERRED: 0x490a80 / 0x48c96c); this file
// translates the RULES/ORCHESTRATION those two functions wrap around the
// already-done damage core (ApplyMeleeHit / ApplyUnitDeath in combat.cpp):
//
//   * VIBE_Combat_PerformAttackAction @0x490a80 — drains one pending order slot
//     into an attack action: it validates the slot + acting/target unit, copies
//     the predicted damage onto the target's HP scratch, classifies the action
//     (melee hit / drop-bomb / throw-bomb) by the attacker's weapon-type, and
//     emits the corresponding character-action / command. The decision tree (the
//     RULE) is translated; the anim-queue insert, voice samples, heightmap tile
//     probe and the network op78 emission are routed through forward-declared
//     leaves / the command sink.
//
//   * VIBE_Combat_ResolveMeleeHit @0x48c96c — the anim-coupled wrapper invoked
//     from the action queue when the melee swing animation lands. It re-checks
//     range, (for ranged class 1/2 weapons) clears the active target via a delta
//     packet, then runs the single RULE — ApplyUnitDeath — and on a "banner" stab
//     (weapon 366) marks the victim defeated (+8 = 4). Voice/particle/mesh/text
//     are deferred; the range gate + death gate + defeat-marker are translated.
//
// Determinism: the damage roll / "taunt" roll uses the cutscene LCG
// (CutsceneRng); the "shout on attack" gate uses the CRT LCG (Math_RandomModulo).
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_battle.h"
#include "sim/combat_types.h"

namespace guild::sim {

// ===========================================================================
// Forward-declared presentation / command leaves (combat_action.cpp).
// These mirror the originals' anim/voice/command emission. They are routed
// through a mockable sink so the attack-sequencing rules stay testable.
// ===========================================================================

// The attack action a PerformAttackAction order slot resolves to. In the
// original this selects which character-action function gets queued
// (ResolveMeleeHit / DropBombAction / ThrowBombAction) — the RULE is which one.
enum class AttackActionKind {
    None,        // slot empty / acting unit gone / not yet armed (+29 == 0)
    MeleeSwing,  // weapon != 372/374: queue the melee swing -> ResolveMeleeHit
    DropBomb,    // weapon == 372 and slot.hitFlag set -> drop-bomb action
    ThrowBomb,   // weapon == 374 and slot armed (+29) -> throw-bomb action
};

// gilde.exe 0x490a80 — VIBE_Combat_PerformAttackAction (decision/sequencing core).
// Inputs mirror the order-slot + the two resolved units' weapon defs:
//   * `slot`        — the order slot being drained (a copy of the 44-byte record;
//                     the original qmemcpy's it to v40 and reads v40 / a1 fields).
//   * `attacker`    — the acting unit (FindUnitById(slot.unitId)); supplies the
//                     weapon class/type (its ObjectDef) and the actor ptr.
//   * `target`      — the struck unit (FindUnitById(slot.tileX == a1[4])); may be
//                     null. The original writes a1[10] (predicted damage) into the
//                     target's HP scratch (*(target+9) = a1[10]) up-front.
//   * `predictedDamage` — a1[10], the queued damage value written to the target.
//   * `armed`       — *(a1+29) (slot.firing): the swing is ready to land.
//   * `hasHit`      — a1[8] (slot.hitFlag dword nonzero): the swing connected.
//   * `damage`      — a1[9] (slot.predictedDamage): the HP delta applied on a hit.
// Behaviour (the RULE, faithfully):
//   1. if slot.state==0 (==!*(a1+4)) or slot.unitId==-1 -> None (nothing queued).
//   2. write target->hp scratch = predictedDamage (the displayed estimate).
//   3. if weapon is NOT 372/374 and armed:
//        - if hasHit: target->hp -= damage   (the connect applies the delta);
//        - kind = MeleeSwing.
//   4. weapon 372 + hasHit -> DropBomb;  weapon 374 + armed -> ThrowBomb.
// Returns the resolved action kind. Mutations (target HP) go through the model +
// the command sink. The anim-queue insert / voice / heightmap are deferred.
struct AttackActionResult {
    AttackActionKind kind = AttackActionKind::None;
    bool             connected = false;  // a melee/throw hit landed (hasHit/armed)
    int              appliedDamage = 0;  // HP actually subtracted from the target
};

AttackActionResult PerformAttackAction(const OrderSlot& slot, CombatUnitAI& attacker,
                                       CombatUnitAI* target, int predictedDamage,
                                       bool armed, bool hasHit, int damage);

// ===========================================================================
// Anim-coupled melee resolver (the RULE the swing-anim callback runs).
// ===========================================================================

// gilde.exe 0x48c96c — VIBE_Combat_ResolveMeleeHit (rules core extracted).
// Invoked when the melee swing lands. Re-checks range, then runs the death gate.
//   * `attacker` / `victim` — the two units (a1[95] / a1[96] in the original).
//   * `distance` / `weaponRange` — DistanceToTargetXZ(victim, attacker.actor) vs
//     the attacker weapon-def range (v5[3]); out of range -> the swing whiffs
//     (v2 = 0, no death gate, returns without applying).
//   * `connected` — a1[98]-derived: whether this resolve carries a real hit
//     (mirrors v2 != 0 after the range check). The original's `v2` is the queued
//     damage magnitude; here a nonzero `damageMagnitude` means "landed".
//   * `damageMagnitude` — the hit's damage band sample (v2); used for the
//     death-gate's currentHp and to gate the defeat banner.
//   * `currentHp` / `maxHp` — the victim's HP ratio inputs for ApplyUnitDeath.
//   * `weaponType` — the attacker weapon-def type word (*(v3)). Banner weapon 366
//     marks the victim defeated (sets victim.alive = 4) on a fatal hit.
// Returns true iff the victim died this resolve.
struct MeleeResolveResult {
    bool inRange = false;   // v27 — the swing was in range
    bool killed  = false;   // ApplyUnitDeath fired
    bool defeated = false;  // weapon 366 banner: victim marked +8 = 4
};

MeleeResolveResult ResolveMeleeHit(CombatUnitAI& attacker, CombatUnit& victim,
                                   double distance, double weaponRange,
                                   bool connected, int damageMagnitude,
                                   double currentHp, double maxHp, i16 weaponType);

// The victim's "defeated" marker value written by the banner stab (weapon 366):
// CombatUnit::alive becomes 4 (the EvaluateBattleOutcome "fled/defeated" value).
constexpr u8 kUnitDefeatedMarker = 4;

} // namespace guild::sim
