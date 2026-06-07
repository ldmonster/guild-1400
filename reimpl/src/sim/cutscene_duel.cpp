#include "sim/cutscene_duel.h"

// Faithful 1:1 port of the deterministic core of VIBE_Cutscene_Duel
// (gilde.exe 0x4a53a8) + RollDuelOutcomeTier (0x4a6908) + the choice driver of
// VIBE_Duel_ProcessIntroChoice (0x4a4eb4). The pistol-shot / taunt / aim RULES
// math is delegated to the already-recovered duel.{h,cpp} core; here we
// reconstruct the round state machine and the win/draw outcome. All scene /
// voice / .esc / command leaves are routed through DuelCutsceneHooks.

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x4a6908 — VIBE_Cutscene_RollDuelOutcomeTier.
// ---------------------------------------------------------------------------
u8 DuelRollOutcomeTier(CutsceneRng& rng, bool tieredMode) {
    if (tieredMode) {                          // if (dword_6315A4)
        u32 r = rng.RandInt(0x64u);            // RandInt(100)
        if (static_cast<int>(r) > 50)  return 4;   // *(slot+148) = 4
        if (static_cast<int>(r) <= 15) return 3;   // *(slot+148) = 3
        return 2;                                  // *(slot+148) = 2
    }
    u32 r = rng.RandInt(0x0Au);                // RandInt(10)
    return static_cast<u8>(r > 7);             // *(slot+148) = (r > 7)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4a4eb4 — VIBE_Duel_ProcessIntroChoice (deterministic spine).
//   v9 = participant[+148];                       (the chosen action byte)
//   if (v9 == 2)  -> TAUNT  (Duel_ResolveTaunt)
//   else if (v9 == 3) -> AIM  (Duel_ResolveAim)
//   else if (v9 == 4) -> SHOOT (Duel_ResolveShot)
//   else -> no-op (just plays the idle line).
// The original gates the whole thing on !dword_6315C4 (duel not already over);
// the caller (CutsceneDuel) enforces that.
// ---------------------------------------------------------------------------
DuelShotResult DuelProcessIntroChoice(DuelState& state, bool actorIsA,
                                      DuelIntroChoice choice,
                                      const DuelCombatant& actor,
                                      const DuelCombatant& /*other*/,
                                      DuelCombatant& target, CutsceneRng& rng) {
    DuelShotResult out{};
    switch (choice) {
    case DuelIntroChoice::kTaunt:
        // v38 = EvalProductionRating(self, 3);  v40 = EvalProductionRating(other, 4);
        // v41 = RandFloat() + v38;  if (RandFloat() + v40 >= v41) self rattled.
        Duel_ResolveTaunt(state, actorIsA, actor.skill, target.skill, rng);
        break;
    case DuelIntroChoice::kAim:
        // v39 = EvalProductionRating(self, 2); if (RandFloat() >= v39) miss else aim.
        Duel_ResolveAim(state, actorIsA, actor.aimRating, rng);
        break;
    case DuelIntroChoice::kShoot: {
        // The shot uses a transient CombatUnit view of the target for hp/worth.
        CombatUnit tgt{};
        tgt.hp    = target.hp;
        tgt.worth = static_cast<float>(target.worth);
        out = Duel_ResolveShot(state, actorIsA, tgt, rng);
        target.hp = tgt.hp;                    // write the HP loss back
        break;
    }
    case DuelIntroChoice::kNone:
    default:
        break;                                 // idle line only
    }
    return out;
}

// ---------------------------------------------------------------------------
// Win/draw outcome (the tail of VIBE_Cutscene_Duel @0x4a53a8):
//   if (dword_6315C4) {                              // a fatal hit occurred
//     v109 = Building_ComputeOutputRatio(A);
//     v65  = Building_ComputeOutputRatio(B);
//     v116 = (v65 >= v109) ? A : B;                  // winner = higher HP ratio
//   } else v116 = 0;                                 // draw
// We compare the HP ratios directly (hp/worth); B wins ties (v65 >= v109).
// ---------------------------------------------------------------------------
namespace {
int DuelWinnerByRatio(const DuelCombatant& a, const DuelCombatant& b) {
    double ra = (a.worth != 0) ? static_cast<double>(a.hp) / a.worth : 0.0;
    double rb = (b.worth != 0) ? static_cast<double>(b.hp) / b.worth : 0.0;
    // v116 = (rb >= ra) ? A(v118) : B(v104). i.e. A wins when its ratio is
    // strictly greater; B (the second combatant) wins ties.
    return (rb >= ra) ? 0 : 1;
}
} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x4a53a8 — VIBE_Cutscene_Duel (deterministic core).
// ---------------------------------------------------------------------------
DuelOutcome CutsceneDuel(DuelCombatant a, DuelCombatant b, CutsceneRng& rng,
                         const DuelCutsceneHooks& hooks,
                         DuelChooser chooser, void* chooserCtx) {
    DuelOutcome out{};

    // ----- 1. validate both combatants resolve. -----
    // VIBE_Person_FindRecordById(a1[13]); VIBE_Person_FindRecordById(a1[14]);
    // if (!v3 || !RecordById) -> the whole body is skipped (no outcome).
    if (a.personId < 0 || b.personId < 0) {
        out.aborted = true;
        return out;
    }

    // ----- remote report-only replay: forward to ReportToOffice, no local run. -
    if (hooks.reportOnly && hooks.reportOnly(hooks.ctx)) {
        out.aborted = true;
        return out;
    }

    // ----- 2. scene load + reset score/cursor. -----
    if (hooks.loadScene) hooks.loadScene("duell.ed3", hooks.ctx);

    // The DuelState mirrors byte_6315DC/DD (=0), byte_6315DE..E1 (rattled/aim).
    DuelState state{};
    state.skillA = a.skill;  state.skillB = b.skill;
    state.goodPistolA = a.goodPistol;  state.goodPistolB = b.goodPistol;
    state.scoreA = 0;  state.scoreB = 0;
    state.over = false;                        // dword_6315C4 = 0

    // ----- 4. the round loop:  do { } while (v52 < 3 && !dword_6315C4). -----
    int round = 0;
    do {
        // Per-round flag reset (byte_6315DE/DF/E0/E1 = 0 before each round).
        state.rattledA = state.rattledB = false;
        state.aimA = state.aimB = false;

        // Re-pick each combatant's choice for this round (GUI / RollDuelOutcomeTier).
        if (chooser) chooser(round, a, b, rng, chooserCtx);
        if (hooks.onRound) hooks.onRound(round, a.choice, b.choice, hooks.ctx);

        // The original runs A then B (ProcessIntroChoice twice). The first call's
        // operand order depends on the choice byte (v56 == 2/3 swaps), but the
        // net effect is: each combatant performs its own chosen action. We run A
        // against B, then B against A, honouring the not-over gate between them.
        if (!state.over) {
            DuelShotResult ra = DuelProcessIntroChoice(state, /*isA*/true,
                                                       a.choice, a, b, b, rng);
            if (ra.hit) {
                if (hooks.onShot) hooks.onShot(0, ra, hooks.ctx);
                // fatal-hit gate: Duel_CheckFatalHit(B) sets state.over.
                if (Duel_CheckFatalHit(state, b.hp, b.worth)) {
                    out.over = true;
                }
            }
        }
        if (!state.over) {
            DuelShotResult rb = DuelProcessIntroChoice(state, /*isA*/false,
                                                       b.choice, b, a, a, rng);
            if (rb.hit) {
                if (hooks.onShot) hooks.onShot(1, rb, hooks.ctx);
                if (Duel_CheckFatalHit(state, a.hp, a.worth)) {
                    out.over = true;
                }
            }
        }

        ++round;
    } while (round < kDuelMaxRounds && !state.over);

    // ----- 5. outcome. -----
    out.rounds = round;
    out.scoreA = state.scoreA;
    out.scoreB = state.scoreB;
    out.hpA = a.hp;
    out.hpB = b.hp;
    if (state.over) {
        out.over = true;
        out.winner = DuelWinnerByRatio(a, b);  // higher HP ratio wins
    } else {
        out.winner = -1;                       // draw
    }

    if (hooks.onOutcome) hooks.onOutcome(out, hooks.ctx);
    return out;
}

} // namespace guild::sim
