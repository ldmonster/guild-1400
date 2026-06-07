#pragma once
// gilde.exe — Duel: the pistol-duel mini-game (the "card game" choice round +
// shot scoring). MODULE: combat / Duel core (prefix VIBE_Duel_*), namespace
// guild::sim.
//
// The duel is a small turn-based choice game between two combatants. Each round
// a player picks one of:
//   * TAUNT (Lästern, choice 2): a contested skill roll; the loser becomes
//     "rattled" (a penalty multiplier on their next shot).
//   * CONCENTRATE/AIM (choice 3): a skill check; on success the player gains the
//     "aim" flag (a tighter, lower-but-reliable damage band on their shot).
//   * SHOOT (choice 4): resolve a pistol shot — a hit-chance roll, then a damage
//     roll; the damage is added to the shooter's score and subtracted from the
//     target's HP. The duel ends when a hit drops the target below the fatal HP
//     ratio (0.2).
//
// Determinism. The shot's damage rolls use the SAME "cutscene" LCG as the rest
// of the combat presentation (CutsceneRng in combat.h). The original also has a
// shared replay table (dword_11AA540) consulted by VIBE_Duel_RandomMod for
// flavour-text selection and cross-peer message sync — modelled by DuelRng.
//
// SKILL stat. Each combatant's shooting/aim/taunt rating comes from the building
// production-rating accessor (VIBE_Building_EvalProductionRating, owned by the
// building module). It is passed in as a float so this module stays standalone;
// the host/test supplies it.
#include "guild/common/types.h"
#include "sim/combat.h"        // CombatUnit, CutsceneRng, DuelState

#include <vector>

namespace guild::sim {

// The shared deterministic duel RNG: a replay table indexed by a monotonically
// advancing cursor (gilde.exe dword_11AA540[cursor % len], cursor++). Used so
// every peer rolls the identical flavour/message sequence.
struct DuelRng {
    std::vector<u32> table;   // dword_11AA540
    u32 cursor = 0;           // dword_6315E8 (length == dword_6315E4)

    // gilde.exe 0x4a4b30 — VIBE_Duel_RandomMod(n).
    //   v = table[cursor % len] % n;  ++cursor;  return v;
    int Mod(int n);
};

// Result of resolving one duel shot.
struct DuelShotResult {
    bool hit = false;      // the shooter's roll landed
    int  damage = 0;       // hit-score added (0 on a miss)
    int  remainingHp = 0;  // defender HP after the shot
};

// gilde.exe 0x4a4b68 — VIBE_Duel_ResolveShot  (RULES/MATH core, scoring).
// One pistol shot from `shooter` (slot A or B) at `target`:
//   chance = shooterSkill * 0.8;                          (dbl_61CE24)
//   if shooter has the "good pistol":  chance *= 1.5;     (dbl_61CE2C)
//   if shooter is "rattled" (lost taunt): chance *= 0.66; (dbl_61CE34)
//   roll = cutscene RandFloat();
//   if roll >= chance  -> MISS (damage 0).
//   else HIT: damage = aim ? RandInt(15)+5 : RandInt(30)+15;  (5..19 vs 15..44)
//   score[shooter] += damage;                            (byte_6315DC/DD)
//   target HP -= (int)(target.worth * 0.01 * damage);    (dbl_61CE3C scale)
// `shooterIsA` selects whose flags/score apply.  Returns the shot result and
// mutates `state` (score) and `target.hp`.
DuelShotResult Duel_ResolveShot(DuelState& state, bool shooterIsA,
                                CombatUnit& target, CutsceneRng& rng);

// The pre-shot choice round (the "card game"). Each helper resolves one choice.
enum class DuelChoice { kTaunt = 2, kAim = 3, kShoot = 4, kOther = 0 };
enum class DuelChoiceOutcome { kTauntLanded, kTauntFailed, kAimGained, kAimMissed,
                               kShootDeferred, kNone };

// gilde.exe 0x4a4eb4 (TAUNT branch) — Duel_ProcessIntroChoice, choice 2.
//   v41 = RandFloat() + attackerSkill;
//   if (RandFloat() + defenderRating4 >= v41)  -> defender wins -> attacker rattled
//   else                                      -> attacker wins  (no flag)
DuelChoiceOutcome Duel_ResolveTaunt(DuelState& state, bool attackerIsA,
                                    float attackerSkill, float defenderRating4,
                                    CutsceneRng& rng);

// gilde.exe 0x4a4eb4 (AIM branch) — Duel_ProcessIntroChoice, choice 3.
//   if (RandFloat() >= ownRating2) -> miss (no flag)
//   else                           -> gain aim flag
DuelChoiceOutcome Duel_ResolveAim(DuelState& state, bool playerIsA,
                                  float ownRating2, CutsceneRng& rng);

// gilde.exe 0x4a4a60 — VIBE_Duel_CheckFatalHit  (RULES core extracted).
// After a hit, the duel ends when the target's HP ratio drops below dbl_61CD94
// == 0.2. Returns true (fatal) and sets state.over.
bool Duel_CheckFatalHit(DuelState& state, double currentHp, double maxHp);
constexpr double kDuelFatalRatio = 0.2;   // dbl_61CD94

} // namespace guild::sim
