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
//   v26 = shooterSkill * 0.8;
//   if (Inventory_IsObjectSlotActive(367))  v26 *= 1.5;   (good pistol)
//   if (rattled)                            v26 *= 0.66;
//   if (RandFloat() >= v26) { miss; v6 = 0; }
//   else { v6 = 1;
//     v7 = aim ? (RandInt(0xF)+5) : (RandInt(0x1E)+15);
//     score[shooter] += v7;
//     v8 = worth * 0.01 * v7;  SpawnDamageNumber(v8);   (presentation)
//     ...
//     v16 = v7 * 0.01;  v17 = hp - worth*v16;  hp = (int)v17;  }
DuelShotResult Duel_ResolveShot(DuelState& state, bool shooterIsA,
                                CombatUnit& target, CutsceneRng& rng) {
    DuelShotResult out{};
    float skill      = shooterIsA ? state.skillA : state.skillB;
    bool  goodPistol = shooterIsA ? state.goodPistolA : state.goodPistolB;
    bool  rattled    = shooterIsA ? state.rattledA : state.rattledB;
    bool  aim        = shooterIsA ? state.aimA : state.aimB;

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

    // Accumulate the shooter's hit score (byte_6315DC / byte_6315DD).
    if (shooterIsA)
        state.scoreA = static_cast<u8>(state.scoreA + dmg);
    else
        state.scoreB = static_cast<u8>(state.scoreB + dmg);

    // Apply HP loss to the target: hp -= (int)(worth * (dmg*0.01)).
    double v16   = static_cast<double>(dmg) * kDmgToHpScale;
    double newHp = static_cast<double>(target.hp) -
                   static_cast<double>(target.worth) * v16;
    target.hp = static_cast<int>(newHp);
    out.remainingHp = target.hp;
    return out;
}

// gilde.exe 0x4a4eb4 (TAUNT branch) — Duel_ProcessIntroChoice, choice 2.
//   v41 = RandFloat() + attackerSkill;
//   if (RandFloat() + defenderRating4 >= v41) -> defender wins -> attacker rattled
DuelChoiceOutcome Duel_ResolveTaunt(DuelState& state, bool attackerIsA,
                                    float attackerSkill, float defenderRating4,
                                    CutsceneRng& rng) {
    double v41 = rng.RandFloat() + static_cast<double>(attackerSkill);
    double def = rng.RandFloat() + static_cast<double>(defenderRating4);
    if (def >= v41) {
        // Defender wins: the attacker (taunter) becomes rattled.
        if (attackerIsA) state.rattledA = true; else state.rattledB = true;
        return DuelChoiceOutcome::kTauntFailed;
    }
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
