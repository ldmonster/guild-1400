#include "sim/duel.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants (decoded from the data section).
//   dbl_61CE24 = 0.8   base shot hit-chance scale
//   dbl_61CE2C = 1.5   good-pistol multiplier  (object slot 367 active)
//   dbl_61CE34 = 0.66  rattled (lost-taunt) multiplier
//   dbl_61CE3C = 0.01  damage -> HP scale
//   dbl_61CD94 = 0.2   fatal-hit HP ratio (kDuelFatalRatio)
// ---------------------------------------------------------------------------
static constexpr double kShotChanceScale = 0.8;   // dbl_61CE24
static constexpr double kGoodPistolMul   = 1.5;   // dbl_61CE2C
static constexpr double kRattledMul      = 0.66;  // dbl_61CE34
static constexpr double kDmgToHpScale    = 0.01;  // dbl_61CE3C

// gilde.exe 0x4a4b30 — VIBE_Duel_RandomMod.
//   v = dword_11AA540[cursor % len] % n;  ++cursor;  return v;
int DuelRng::Mod(int n) {
    if (table.empty() || n == 0)
        return 0;
    int v = static_cast<int>(table[cursor % table.size()]) % n;
    ++cursor;
    return v;
}

// gilde.exe 0x4a4a60 — VIBE_Duel_CheckFatalHit (rules core).
//   if (Building_ComputeOutputRatio(unit) >= dbl_61CD94 /*0.2*/) return 0;
//   ... (present.) ... dword_6315C4 = 1; return 1;
bool Duel_CheckFatalHit(DuelState& state, double currentHp, double maxHp) {
    double ratio = (maxHp != 0.0) ? (currentHp / maxHp) : 0.0;
    if (ratio >= kDuelFatalRatio)
        return false;
    state.over = true;          // dword_6315C4 = 1
    return true;
}

// gilde.exe 0x4a4b68 — VIBE_Duel_ResolveShot (rules/scoring core).
//   ecx = shooter unit, esi = target unit (action args +0x180 / +0x17C).
//   var_24 = EvalProductionRating(shooter,3) * 0.8;     (dbl_61CE24)
//   if (Inventory_IsObjectSlotActive(shooter,367)) var_24 *= 1.5;  (dbl_61CE2C)
//   // RATTLED gate is CROSS-WIRED to the OTHER party's byte:
//   if (shooter==A && byte_6315DF/*rattledB*/) || (shooter==B && byte_6315DE/*rattledA*/)
//        var_24 *= 0.66;                                  (dbl_61CE34)
//   if (RandFloat() >= var_24) { miss; v6 = 0; }
//   else { v6 = 1;
//     // AIM gate is ALSO CROSS-WIRED to the OTHER party's byte:
//     v7 = ((shooter==A && byte_6315E1/*aimB*/)||(shooter==B && byte_6315E0/*aimA*/))
//            ? (RandInt(0xF)+5) : (RandInt(0x1E)+15);
//     // SCORE is keyed on the TARGET (esi), not the shooter:
//     if (target==A) byte_6315DC/*scoreA*/ += v7; else byte_6315DD/*scoreB*/ += v7;
//     v8 = worth(esi[+0x1C]) * 0.01 * v7; ConvertX; SpawnDamageNumber(v8);  (present.)
//     ... CheckFatalHit(target,v7); if !fatal:
//     v14 = v7*0.01; v15 = target.hp - target.worth*v14; ConvertX; target.hp=(int)v15 }
// NOTE (handoff): in the binary the HP subtraction happens ONLY on a non-fatal
// hit, AFTER CheckFatalHit reads the pre-subtraction ratio. This factored core
// subtracts unconditionally on a hit; the fatal-vs-nonfatal ordering lives in the
// caller (cutscene_duel.cpp, not in this chunk). The byte cross-wiring below is
// the load-bearing 1:1 fix.
//
// In this module's A/B model the duel target is always the OTHER party, so the
// target identity == !shooterIsA.
DuelShotResult Duel_ResolveShot(DuelState& state, bool shooterIsA,
                                CombatUnit& target, CutsceneRng& rng) {
    DuelShotResult out{};
    float skill      = shooterIsA ? state.skillA : state.skillB;
    bool  goodPistol = shooterIsA ? state.goodPistolA : state.goodPistolB;
    // CROSS-WIRED: shooter A reads rattledB (byte_6315DF), shooter B reads
    // rattledA (byte_6315DE) — see disasm 0x4a4bcd..0x4a4be6.
    bool  rattled    = shooterIsA ? state.rattledB : state.rattledA;
    // CROSS-WIRED: shooter A reads aimB (byte_6315E1), shooter B reads aimA
    // (byte_6315E0) — see disasm 0x4a4c10..0x4a4c25.
    bool  aim        = shooterIsA ? state.aimB : state.aimA;

    double chance = static_cast<double>(skill) * kShotChanceScale;  // *0.8
    if (goodPistol) chance *= kGoodPistolMul;                       // *1.5
    if (rattled)    chance *= kRattledMul;                          // *0.66

    double roll = rng.RandFloat();
    if (roll >= chance) {
        out.hit = false;
        out.damage = 0;
        out.remainingHp = target.hp;
        return out;
    }

    // HIT.
    out.hit = true;
    int dmg;
    if (aim)
        dmg = static_cast<int>(rng.RandInt(0x0F)) + 5;   // 5..19  (RandInt(15)+5)
    else
        dmg = static_cast<int>(rng.RandInt(0x1E)) + 15;  // 15..44 (RandInt(30)+15)
    out.damage = dmg;

    // Accumulate score on the TARGET's side (byte_6315DC / byte_6315DD keyed on
    // esi==target, disasm 0x4a4c36). target == !shooter here.
    if (!shooterIsA)
        state.scoreA = static_cast<u8>(state.scoreA + dmg);
    else
        state.scoreB = static_cast<u8>(state.scoreB + dmg);

    // Apply HP loss to the target: hp -= (int)(worth * (dmg*0.01)).  ConvertX
    // truncates toward zero; a plain (int) cast matches for the values here.
    double v16   = static_cast<double>(dmg) * kDmgToHpScale;
    double newHp = static_cast<double>(target.hp) -
                   static_cast<double>(target.worth) * v16;
    target.hp = static_cast<int>(newHp);
    out.remainingHp = target.hp;
    return out;
}

// gilde.exe 0x4a4eb4 (TAUNT branch) — Duel_ProcessIntroChoice, choice 2.
//   v33 = EvalProductionRating(attacker/esi, 4);   v35 = EvalProductionRating(defender/edi, 4);
//   v36 = RandFloat() + v33;                         (first draw -> attacker term)
//   if (RandFloat() + v35 >= v36) -> defender prevails: NO rattle flag.    (0x4a5002 jnb)
//   else (defender's roll < attacker's): attacker LANDS the taunt:
//        if (attacker==A/esi==E30) byte_6315DE/*rattledA*/ = 1;            (0x4a503f)
//        else                      byte_6315DF/*rattledB*/ = 1;            (0x4a5166)
// NOTE: the rattle BYTE is set under the attacker's identity, but ResolveShot
// reads it CROSS-WIRED (shooter A reads rattledB, shooter B reads rattledA), so
// the net effect penalizes the DEFENDER's next shot. We set the byte exactly as
// the binary does (attackerIsA -> rattledA) and the cross-read lives in
// Duel_ResolveShot above. The flag is set only on the attacker-win branch.
DuelChoiceOutcome Duel_ResolveTaunt(DuelState& state, bool attackerIsA,
                                    float attackerSkill, float defenderRating4,
                                    CutsceneRng& rng) {
    double v36 = rng.RandFloat() + static_cast<double>(attackerSkill);
    double def = rng.RandFloat() + static_cast<double>(defenderRating4);
    if (def >= v36) {
        // Defender prevails (jnb): no rattle byte set.
        return DuelChoiceOutcome::kTauntFailed;
    }
    // Attacker lands the taunt: set the rattle byte under the attacker's identity.
    if (attackerIsA) state.rattledA = true; else state.rattledB = true;
    return DuelChoiceOutcome::kTauntLanded;
}

// gilde.exe 0x4a4eb4 (AIM branch) — Duel_ProcessIntroChoice, choice 3.
//   v39 = EvalProductionRating(self, 2);
//   if (RandFloat() >= v39) -> miss (no flag)  else -> gain aim flag
DuelChoiceOutcome Duel_ResolveAim(DuelState& state, bool playerIsA,
                                  float ownRating2, CutsceneRng& rng) {
    double roll = rng.RandFloat();
    if (roll >= static_cast<double>(ownRating2))
        return DuelChoiceOutcome::kAimMissed;
    if (playerIsA) state.aimA = true; else state.aimB = true;
    return DuelChoiceOutcome::kAimGained;
}

} // namespace guild::sim
