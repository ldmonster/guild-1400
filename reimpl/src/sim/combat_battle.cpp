#include "sim/combat_battle.h"

#include "crt/rand.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constant: VIBE_Math_RandomFloatScaled @0x58b910.
//   fild int; fmul flt_62675C  ->  (double)(int)RandNext() * (double)flt_62675C.
// flt_62675C bytes = 00 01 00 38 == float 0x38000100 == 3.0518509447574615e-05
//   (float-rounded 1/32767, NOT exact double 1/32767). Confirmed via get_bytes;
//   the multiply uses the FLOAT value promoted to double, so we must keep float.
// RandNext() is in [0, 0x7FFF] (== 0..32767), so the result is in [0, 1].
// ---------------------------------------------------------------------------
static constexpr float kRandFloatScaledScale = 3.0518509447574615e-05f; // flt_62675C (0x38000100)

// gilde.exe 0x58b910 — VIBE_Math_RandomFloatScaled.
static double RandomFloatScaled() {
    return static_cast<double>(crt::RandNext()) *
           static_cast<double>(kRandFloatScaledScale);
}

// ===========================================================================
// Unit combat strength
// ===========================================================================

// gilde.exe 0x485dc0 — the weapon-type weight switch (the v8 value).
// IMPORTANT: in the original `v8` is a FLOAT; the constants are float-precision
// (0.80000001 == float(0.8), 0.69999999 == float(0.7), 0.89999998 == float(0.9),
// 0.30000001 == float(0.3)). Return float-rounded values so the strength math is
// bit-identical to the binary (double 0.8 differs from float(0.8) by ~1.2e-8).
double WeaponWeight(i16 weaponType) {
    switch (static_cast<u16>(weaponType)) {
        case 340:           return static_cast<float>(0.5);   // stab (0.5 exact)
        case 342:           return static_cast<float>(0.8);   // 0x156 sword (0.80000001)
        case 344:           return static_cast<float>(1.0);   // 0x158 sword2 (1.0 exact)
        case 350:           return static_cast<float>(0.7);   // 0x15E (0.69999999)
        case 352:           return static_cast<float>(0.7);   // 0x160 dodge (0.69999999)
        case 366:           return static_cast<float>(0.8);   // stab2 banner (0.80000001)
        case 374:           return static_cast<float>(0.9);   // thrown bomb (0.89999998)
        default:            return static_cast<float>(0.3);   // LABEL_22 (0.30000001)
    }
}

// gilde.exe 0x485dc0 — VIBE_Combat_GetSoundRangeScale.
//   v7 = (float)EvalProductionRating(u, 3);  v8 = (float)weight;
//   v6 = (float)(v7 * v8);                    // FLOAT multiply, float result
//   return (double)ComputeOutputRatio(u) * (double)v6;
double GetSoundRangeScale(const CombatUnitAI& u) {
    float v7 = u.productionRating;                            // EvalProductionRating(u,3)
    float v8 = static_cast<float>(WeaponWeight(u.weaponType));
    float v6 = v7 * v8;                                       // float intermediate
    return static_cast<double>(u.outputRatio) *
           static_cast<double>(v6);                           // * ComputeOutputRatio
}

// ===========================================================================
// Role scoring
// ===========================================================================

// gilde.exe 0x48be60 — VIBE_Combat_ScoreUnitForRole.
double ScoreUnitForRole(const CombatUnitAI& u, u8 role) {
    bool deadRangedTarget =
        (u.weaponClass == 1 || u.weaponClass == 2) && u.activeTargetHp <= 0;
    // NOTE: cases 0,1,4,5 store the result through a FLOAT stack slot in the
    // original (v9/v11 result, v13), so the return is float-rounded. Cases 2/3
    // return exact double literals. Reproduce the float rounding.
    switch (role) {
        case 0: {                                  // attack
            float mult = u.role ? 0.5f : 1.0f;     // *(a1+452) nonzero -> 0.5
            if (deadRangedTarget)
                mult = 0.2f;
            // v9 = (float)(GetSoundRangeScale(u) * v10)
            return static_cast<float>(GetSoundRangeScale(u) * static_cast<double>(mult));
        }
        case 1: {                                  // conquer ware
            float mult;
            if (u.role == 1)        mult = 1.0f;
            else if (u.role)        mult = 0.5f;
            else                    mult = static_cast<float>(0.40000001); // float 0.4
            if (deadRangedTarget)
                mult = 1.0f;
            // return (float)(v11 / GetSoundRangeScale(u))
            return static_cast<float>(static_cast<double>(mult) / GetSoundRangeScale(u));
        }
        case 2:                                    // move to conquer
            return (u.role == 2) ? 1.0 : 0.5;
        case 3:                                    // hold
            return 1.0;
        case 4:                                    // tile
            if (u.role == 2)
                return 0.2;                        // result = 0.2 (double slot)
            // v13 = (float)(1.0 / ComputeOutputRatio(a1)); result = v13
            return static_cast<float>(1.0 / static_cast<double>(u.outputRatio));
        case 5:                                    // escape
            if (u.role == 2)
                return 0.2;
            return static_cast<float>(1.0 / static_cast<double>(u.outputRatio));
        default:
            return 0.0;                            // v9 == 0.0 default
    }
}

// ===========================================================================
// Balance index
// ===========================================================================

// gilde.exe 0x48bba8 — VIBE_Combat_ComputeUnitBalanceWeights (index derivation).
BalanceIndex ComputeBalanceIndex(const BattleDescriptor& battle, bool isAttacker,
                                 double sideStrength, double otherStrength,
                                 int defenderEnemyCount) {
    // Mode index (v21[4]): derived from flags + side. (0..5; default 6.)
    int mode = 6;
    u8 f = battle.modeFlags;
    if ((f & kBattleRaid) && isAttacker)         mode = 0;
    else if ((f & kBattleRaid) && !isAttacker)   mode = 1;
    else if ((f & kBattleAttack) && isAttacker)  mode = 2;
    else if ((f & kBattleAttack) && !isAttacker) mode = 3;
    else if ((f & kBattleDefend) && isAttacker)  mode = 4;
    else if ((f & kBattleDefend) && !isAttacker) mode = 5;

    // Strength ratio: attacker -> own(defenderStrength is *(v21))/ ... the
    // original computes attacker: *(v21)/v20  (= side A stored vs side B sum) and
    // defender: v20 / *(v21). We pass them pre-summed as side/other.
    double ratio = (otherStrength != 0.0) ? (sideStrength / otherStrength) : 0.0;

    int bucket;
    if (ratio < static_cast<double>(kBalanceHi)) {
        if (ratio < static_cast<double>(kBalanceMid)) {
            if (ratio < static_cast<double>(kBalanceLow))
                bucket = (ratio >= static_cast<double>(kBalanceVLow)) ? 1 : 0;
            else
                bucket = 2;
        } else {
            bucket = 3;
        }
    } else {
        bucket = 4;
    }

    // Defender special-case on the number of attacker-owned flag objects.
    if (!isAttacker) {
        if (defenderEnemyCount == 1)
            bucket = 1;
        else if (defenderEnemyCount == 2 || defenderEnemyCount == 3)
            bucket = 0;
    }
    return BalanceIndex{mode, bucket};
}

// ===========================================================================
// Role assignment planner
// ===========================================================================

// gilde.exe 0x48bfb0 — VIBE_Combat_AssignUnitsToRoles.
void AssignUnitsToRoles(std::vector<CombatUnitAI*>& units, const RoleWeights& weights,
                        CutsceneRng& rng) {
    int count = static_cast<int>(units.size());        // v3 (live unit count)
    // The original walks a fixed pool of 16 slots, nulling chosen entries; we
    // mirror that with a working copy that we null out on assignment.
    std::vector<CombatUnitAI*> pool = units;

    // The chosen role index (v29) is STICKY across outer iterations in the binary:
    // when no cumulative threshold covers the roll (v13 reaches 6 -> break) v29 is
    // left at its previous value (the binary leaves it uninitialized on the very
    // first iteration; that pure edge case is UB in the original). We seed it to 5
    // (the documented last-index fallthrough) and only update it when a threshold
    // matches, reproducing the sticky carry-over for the well-defined cases.
    u8 chosenRole = 5;
    for (int k = 0; k < count; ++k) {
        double roll = RandomFloatScaled();             // v26 (CRT LCG)
        // Pick the role whose cumulative threshold first covers the roll.
        if (roll <= static_cast<double>(weights[0])) {
            chosenRole = 0;                             // LABEL_31 with v13 == 0
        } else {
            for (int r = 1; r < 6; ++r) {              // v13 = 1..5
                if (roll <= static_cast<double>(weights[r])) {
                    chosenRole = static_cast<u8>(r);    // LABEL_31: v29 = v13
                    break;
                }
                // v13 >= 6 -> break leaving v29 (chosenRole) unchanged (sticky).
            }
        }

        // Among the remaining pool, pick the best-scoring unit for the role.
        int best = -1;
        double bestScore = -1.0;                        // v27 = -1.0
        for (int m = 0; m < static_cast<int>(pool.size()); ++m) {
            if (!pool[m])
                continue;
            double s = ScoreUnitForRole(*pool[m], chosenRole);
            if (s > bestScore) {
                best = m;
                bestScore = s;
            }
        }
        if (best < 0)
            break;

        // Assign if the unit is currently unassigned, or the reassignment roll
        // passes (cutscene RandFloat > 0.25). Then drop it from the pool.
        CombatUnitAI* picked = pool[best];
        if (picked->role == 0 || rng.RandFloat() > kRoleReassignChance) {
            picked->role = chosenRole;                  // *(unit+452) = role
            pool[best] = nullptr;                        // v22[a2] = 0
        }
    }
}

// ===========================================================================
// Order building
// ===========================================================================

// gilde.exe 0x48c24c — VIBE_Combat_BuildOrderForUnit (decision core).
BattleAction BuildOrderForUnit(const CombatUnitAI& u, bool alive, bool captured) {
    if (!u.unit)
        return BattleAction::None;
    // gilde.exe 0x48c27f: the switch is entered ONLY when the unit is alive AND
    // not captured ( *(a2+8) && actor[533] != 1 ). A dead unit (and a captured
    // one) builds no order.
    if (!alive || captured)                            // *(a2+8)==0 or actor[533]==1
        return BattleAction::None;
    switch (u.role) {                                  // *(a2+452)
        case kRoleAttack: {
            bool ranged = (u.weaponClass == 1 || u.weaponClass == 2);
            if (ranged && u.activeTargetHp <= 0)
                return BattleAction::None;              // ranged target dead -> wait
            if (u.weaponClass == 2)
                return BattleAction::AttackThreatTile;  // ranged -> threatened tile
            return BattleAction::AttackNearest;         // melee -> nearest enemy
        }
        case kRoleConquerWare: return BattleAction::ConquerWare;
        case kRoleMoveConquer: return BattleAction::MoveToConquer;
        case kRoleHold:        return BattleAction::Hold;
        case kRoleTile:        return BattleAction::TileOrder;
        case kRoleEscape:      return BattleAction::Escape;
        default:               return BattleAction::None;
    }
}

// ===========================================================================
// Order-tick state machine — attack rule core
// ===========================================================================

// gilde.exe 0x491688 (state 2 LABEL_46) — attack range gate + hit-chance + dmg.
AttackEval EvaluateAttack(double distance, double weaponRange, u8 accuracyByte,
                          u8 selfSkill, double skillMod, bool hasUnitTarget,
                          float targetWorth, u8 weaponMinDmg, u8 weaponMaxDmg,
                          CutsceneRng& rng, bool alwaysFires) {
    AttackEval e;
    // In range if within the weapon's reach (v16 > range -> v84 = 0).
    e.inRange = (distance <= weaponRange);
    if (!e.inRange)
        return e;

    // gilde.exe 0x491688 LABEL_46:  if ( v87[88] == 2 || ( ...roll... ) ).
    // weaponClass==2 short-circuits the `||`, so the hit-chance roll (and the
    // Math_RandomModulo(255) CRT-LCG draw) is SKIPPED and the shot always fires.
    if (alwaysFires) {
        e.fires = true;
    } else {
        // Hit-chance: roll a CRT d255, then compute the accuracy threshold.
        // chance = (i16)accuracyByte * 0.01 * (i16)selfSkill [* skillMod];
        int roll = Math_RandomModulo(0xFF);              // RandomModulo(255)
        double acc = static_cast<double>(static_cast<i16>(accuracyByte)) * kAccuracyScale;
        double chanceF = acc * static_cast<double>(static_cast<i16>(selfSkill));
        if (skillMod != 1.0)
            chanceF *= skillMod;                         // dword_631200 modifier
        // The chance is truncated by VIBE_Coord_ConvertX (TRUNCATE toward zero);
        // for the always-positive chanceF this matches the (int) cast.
        int chance = static_cast<int>(chanceF);
        e.fires = roll > (255 - chance);                 // v39 > 255 - (int)v38
    }

    if (e.fires) {
        int range = static_cast<int>(weaponMaxDmg) - static_cast<int>(weaponMinDmg);
        u32 r = rng.RandInt(static_cast<u32>(range));    // cutscene RandInt
        int rollSum = static_cast<int>(weaponMinDmg) + static_cast<int>(r);
        double worth = hasUnitTarget ? static_cast<double>(targetWorth) : kNoTargetWorth;
        double dmg = static_cast<double>(rollSum) * worth;  // (min+rand)*worth
        e.predictedDamage = static_cast<int>(dmg * kAccuracyScale); // * 0.01
    } else {
        e.predictedDamage = 0;
    }
    return e;
}

// gilde.exe 0x491688 — one slot's order tick (the switch(state) skeleton).
bool TickOrderSlot(OrderSlot& slot, CombatUnitAI& self, CombatUnitAI* target,
                   const OrderTickContext& ctx) {
    // Slot guard: empty / dead-and-no-weapon clears the slot (the v4 head check).
    if (slot.unitId == -1 || slot.state == kOrderIdle)
        return false;
    if (!self.unit || !self.unit->alive) {
        slot.unitId = -1;                                // *(v4) = -1
        slot.state  = kOrderIdle;                        // v4[4] = 0
        return false;
    }

    switch (slot.state) {
        case kOrderMove:
        case kOrderMarch:
        case kOrderCapture:
        case kOrderStand:
        case kOrderWareCollect:
        case kOrderEscape:
        case kOrderStandUp:
            // These states emit move/path/capture/stand commands through the
            // network channel (Command_RequestBuildOp78/80/81/85). They advance
            // a phase byte and set the in-flight packet id; the geometry/path
            // resolution is heightmap/path-coupled (DEFERRED — routed through the
            // command sink by the host). We faithfully model the phase advance.
            if (slot.packetId == 1 || slot.packetId == -1) {
                slot.packetId = -1;
                if (slot.phase == 0)
                    slot.phase = 1;                      // v4[12] = 1 (issued)
            }
            return true;

        case kOrderAttack: {
            // The attack rule: range gate + hit-chance + damage prediction.
            if (!ctx.rng)
                return false;
            bool hasUnitTarget = (target != nullptr && target->unit != nullptr);
            float targetWorth = hasUnitTarget ? target->unit->worth : 0.0f;
            AttackEval e = EvaluateAttack(
                ctx.distanceToTarget, ctx.weaponRange, ctx.accuracyByte,
                ctx.selfSkill, ctx.skillMod, hasUnitTarget, targetWorth,
                self.weaponMinDmg, self.weaponMaxDmg, *ctx.rng,
                /*alwaysFires=*/self.weaponClass == 2);
            slot.firing = e.inRange ? 1 : 0;             // v4[29] = v84
            slot.predictedDamage = e.predictedDamage;    // *(v4+9)
            slot.hitFlag = e.fires ? 1 : 0;              // *(v4+8)
            return true;
        }
        default:
            return false;
    }
}

// ===========================================================================
// Battle outcome
// ===========================================================================

namespace {
// Count units that are still combat-effective: marker (alive byte) nonzero and
// != 4 (fled), and not captured. We model captured via the actor flag separately;
// here `alive` is the unit.alive byte. (The original also checks actor[533]==1 ==
// captured; CombatUnitAI does not store it, so a captured unit must have alive
// cleared or be excluded by the caller — matches the EvaluateBattleOutcome usage
// where captured units are pre-filtered.)
int CountEffective(const std::vector<CombatUnitAI*>& side) {
    int n = 0;
    for (const CombatUnitAI* u : side) {
        if (!u || !u->unit)
            continue;
        u8 a = u->unit->alive;
        if (a != 0 && a != 4)
            ++n;
    }
    return n;
}
} // namespace

// gilde.exe 0x48cf6c — VIBE_Combat_EvaluateBattleOutcome.
BattleWinner EvaluateBattleOutcome(const std::vector<CombatUnitAI*>& attackers,
                                   const std::vector<CombatUnitAI*>& defenders,
                                   bool allFlagsAttacker) {
    int a = CountEffective(attackers);                   // v6
    int d = CountEffective(defenders);                   // v9
    BattleWinner winner = BattleWinner::Undecided;
    if (d <= 0)
        winner = BattleWinner::Attacker;                 // v29 = dword_6311E8
    else if (a <= 0)
        winner = BattleWinner::Defender;                 // v29 = dword_6311E4
    // Flag-capture special-case: if all flag objects are attacker-owned and there
    // were flag objects (v4 && v3 == v4) -> defender wins.
    if (allFlagsAttacker)
        winner = BattleWinner::Defender;                 // v29 = dword_6311E4
    return winner;
}

// ===========================================================================
// Abstract auto-resolve
// ===========================================================================

namespace {
// gilde.exe 0x490146/0x4901c8: the strength is accumulated as an INT with a
// per-iteration VIBE_Coord_ConvertX TRUNCATION:  v59 = (int)(GetSoundRangeScale +
// (double)v59).  This is NOT the same as summing in double and truncating once at
// the end (e.g. 1.6,1.6,1.6 -> 1,2,3 step-wise == 3, vs (int)4.8 == 4). Reproduce
// the per-step truncation exactly.
int SumStrength(const std::vector<CombatUnitAI*>& side) {
    int acc = 0;
    for (const CombatUnitAI* u : side) {
        if (!u || !u->unit)
            continue;
        u8 a = u->unit->alive;
        if (a != 0 && a != 4) {                          // alive and not fled
            double sum = GetSoundRangeScale(*u) + static_cast<double>(acc);
            acc = static_cast<int>(sum);                 // ConvertX truncate
        }
    }
    return acc;
}
} // namespace

// gilde.exe 0x490014 — the auto-resolve branch (dword_6315BC fast path).
AutoResolveResult AutoResolveBattle(const std::vector<CombatUnitAI*>& attackers,
                                    const std::vector<CombatUnitAI*>& defenders) {
    AutoResolveResult r;
    // gilde.exe: the attacker units live at +128 (v59), the defenders at +192
    // (v58). Each strength is an int-with-per-step-ConvertX-truncation sum.
    int atkSum = SumStrength(attackers);                 // v59 (offset +128)
    int defSum = SumStrength(defenders);                 // v58 (offset +192)

    // gilde.exe 0x490232/0x49024a: defender score (v31) is rolled FIRST, attacker
    // score (Begin) SECOND. The (u16) applies only to the RandomModulo(30) result
    // (always < 30, so a no-op) — there is NO outer u16 truncation of the sum.
    //   v31   = (u16)RandomModulo(30) + v58(defenderSum)
    //   Begin = v59(attackerSum) + (u16)RandomModulo(30)
    int defRoll = static_cast<u16>(Math_RandomModulo(0x1E));
    int defScore = defRoll + defSum;                     // v31
    int atkRoll = static_cast<u16>(Math_RandomModulo(0x1E));
    int atkScore = atkSum + atkRoll;                     // Begin

    r.attackerScore = atkScore;
    r.defenderScore = defScore;
    // gilde.exe: if (v31 /*defScore*/ <= (int)Begin /*atkScore*/) v32 = dword_6311E4
    // (DEFENDER) else v32 = dword_6311E8 (ATTACKER). Faithfully reproduced — note
    // the (counter-intuitive) direction is exactly the original's.
    r.winner = (defScore <= atkScore) ? BattleWinner::Defender : BattleWinner::Attacker;
    return r;
}

} // namespace guild::sim
