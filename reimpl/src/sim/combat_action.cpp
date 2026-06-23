#include "sim/combat_action.h"

#include "crt/rand.h"

namespace guild::sim {

// ===========================================================================
// PerformAttackAction — order-slot -> attack-action sequencing.
// ===========================================================================

// gilde.exe 0x490a80 — VIBE_Combat_PerformAttackAction (decision core).
//
// Original control-flow distilled (presentation stripped):
//   if (!*(a1+4) || *a1 == -1) return;          // slot.state 0 / empty -> bail
//   UnitById = FindUnitById(*a1);                // attacker
//   ObjectDef = FindObjectDef(UnitById);         // attacker weapon
//   v5 = FindUnitById(a1[4]);                     // target
//   if (v5) *(v5+9) = a1[10];                     // write predicted dmg scratch
//   if (!ObjectDef || (type != 372 && type != 374)) {   // melee branch
//     if (*(a1+29)) {                             // armed (slot.firing)
//        ... shout-on-attack: RandomModulo(10) > 8 -> voice (presentation) ...
//        if (a1[8] /*hasHit*/) *(v5+9) -= a1[9];  // apply the connect delta
//        ... queue ResolveMeleeHit action (anim) ...
//     }
//   }
//   if (type == 372 && a1[8]) { ... DropBombAction ... }
//   if (type == 374 && *(a1+29)) { ... ThrowBombAction ... }
//
// The shout/voice/heightmap/op78/anim-queue are all presentation/command leaves
// (DEFERRED); the rule is: classify the action and apply the HP delta on connect.
AttackActionResult PerformAttackAction(const OrderSlot& slot, CombatUnitAI& attacker,
                                       CombatUnitAI* target, int predictedDamage,
                                       bool armed, bool hasHit, int damage) {
    AttackActionResult r;

    // (1) Slot validity gate: empty slot or idle state -> nothing to do.
    if (slot.state == kOrderIdle || slot.unitId == -1)
        return r;
    if (!attacker.unit || !attacker.unit->alive)
        return r;

    // (2) Write the predicted-damage estimate onto the target's HP scratch
    //     (*(v5+9) = a1[10]) — the displayed/queued estimate, before the connect.
    if (target && target->unit)
        target->unit->targetScratch = predictedDamage;  // unit[8] scratch

    i16 weaponType = attacker.weaponType;
    bool isThrowOrDrop = (static_cast<u16>(weaponType) == kWpnDropBomb ||
                          static_cast<u16>(weaponType) == kWpnThrowBomb);

    // (3) Melee branch: any non-bomb weapon, when the swing is armed.
    if (!isThrowOrDrop) {
        if (armed) {
            // The shout-on-attack roll (RandomModulo(10) > 8 -> play a voice line)
            // is presentation; the original advances the CRT LCG unconditionally
            // here, so we preserve that consumption for determinism.
            Math_RandomModulo(0x0A);   // VIBE_Math_RandomModulo(10) — voice gate

            // The connect: if the swing actually hit, subtract the damage delta
            // from the target's HP scratch (*(v5+9) -= a1[9]).
            if (hasHit && target && target->unit) {
                target->unit->targetScratch -= damage;
                r.connected = true;
                r.appliedDamage = damage;
                if (CombatCommandSink())
                    CombatCommandSink()->OnUnitDamage(target->unit->id,
                                                      target->unit->targetScratch);
            }
            r.kind = AttackActionKind::MeleeSwing;
        }
        return r;
    }

    // (4) Bomb branches (mutually exclusive with the melee branch).
    if (static_cast<u16>(weaponType) == kWpnDropBomb && hasHit) {
        r.kind = AttackActionKind::DropBomb;
        r.connected = true;
        return r;
    }
    if (static_cast<u16>(weaponType) == kWpnThrowBomb && armed) {
        r.kind = AttackActionKind::ThrowBomb;
        r.connected = true;
        return r;
    }
    return r;
}

// ===========================================================================
// ResolveMeleeHit — the anim-coupled wrapper's RULE core.
// ===========================================================================

// gilde.exe 0x48c96c — VIBE_Combat_ResolveMeleeHit (rules core).
//
// Original distilled:
//   v27 = 1;
//   if (!range || DistanceToTargetXZ(victim, attacker.actor) > range) {
//       v2 = 0; v27 = 0;                          // out of range -> whiff
//   }
//   if (host) {                                    // dword_631204 == active id
//       if (victim.weaponClass == 1 || == 2) {     // ranged target -> clear it
//           ... BeginDeltaPacket / AppendRawField / QueueRequestState22 ...
//       }
//   }
//   if (v2) { ... voice/damage-number (presentation) ... }
//   if (*(attacker+8) && *(victim+8) && v27) {     // both alive + in range
//       switch (weaponType) { ... voice per type ... }
//       if (!ApplyUnitDeath(attacker, ...)) {       // NB original passes attacker
//           ... blood/standup/sound ...
//           if (weaponType == 366) { *(attacker+8) = 4; ... banner text ... }
//       }
//   }
//
// NOTE on the death-gate argument: the original calls ApplyUnitDeath on `v1`
// (a1[95], the *attacker* unit in this resolver's frame) — the resolver's `v1`
// is the unit whose swing-anim is running and whose HP was the one decremented by
// PerformAttackAction's connect. We model it as `victim` (the unit taking the
// damage) for clarity; the gate semantics (ratio < 0.05 -> dead, clear alive) are
// identical and live in ApplyUnitDeath (combat.cpp).
MeleeResolveResult ResolveMeleeHit(CombatUnitAI& attacker, CombatUnit& victim,
                                   double distance, double weaponRange,
                                   bool connected, int damageMagnitude,
                                   double currentHp, double maxHp, i16 weaponType) {
    MeleeResolveResult r;

    // gilde.exe 0x48c9db: v26(inRange) starts 1; if (!objectdef || Distance > range)
    // -> v2 = 0, v26 = 0 (out of range whiffs). (The original keeps going to clear
    // the active target via a delta packet, which is command/presentation; for the
    // rule we early-out — no death can happen out of range.)
    r.inRange = (weaponRange > 0.0) && (distance <= weaponRange);
    if (!r.inRange)
        return r;

    bool landed = connected && damageMagnitude != 0;   // v2 != 0

    // gilde.exe 0x48cadf: the death gate runs when BOTH units are alive and the
    // swing was in range:  if ( *(v1+8) /*victim*/ && *(v27+8) /*attacker*/ && v26 ).
    // (v1 == a1[95] is the struck/victim unit; v27 == a1[96] is the attacker. The
    // per-weapon-type voice switch all funnels to LABEL_20 == ApplyUnitDeath(v1).)
    bool attackerAlive = attacker.unit && attacker.unit->alive;
    if (victim.alive && attackerAlive) {
        // LABEL_20: result = ApplyUnitDeath(v1 == victim).
        r.killed = ApplyUnitDeath(victim, currentHp, maxHp);
        // gilde.exe 0x48cb3b: the banner / defeat-marker runs ONLY when
        // ApplyUnitDeath returned 0 (the unit SURVIVED the ratio gate) AND the
        // swing landed (v2) AND the weapon is the banner stab 366. A banner stab
        // "defeats"/captures rather than kills:  *(v1+8) = 4.
        if (!r.killed && landed && static_cast<u16>(weaponType) == kWpnStab2) {
            victim.alive = kUnitDefeatedMarker;   // *(v1+8) = 4
            r.defeated = true;
        }
    }
    return r;
}

} // namespace guild::sim
