#pragma once
// ===========================================================================
// cutscene_duel.{h,cpp} — the per-type DUEL cutscene MAIN (type 4) state
// machine (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// This is the deterministic core of the type→main-fn table entry for the duel
// cutscene (dword_11AE5C0[5*4] == VIBE_Cutscene_Duel @0x4a53a8) plus its step /
// helper fns:
//   VIBE_Cutscene_Duel               (0x4a53a8) — the heavyweight duel main.
//   VIBE_Cutscene_RollDuelOutcomeTier(0x4a6908) — the AI choice / outcome tier.
//   VIBE_Duel_ProcessIntroChoice     (0x4a4eb4) — one intro choice (taunt/aim/
//                                                 shoot) — RULES math lives in
//                                                 duel.{h,cpp}; this drives it.
//
// The duel is a pistol duel between two participants (slot fields +52 and +56 =
// the two combatant person ids; the original reads a1[13]/a1[14] = +52/+56).
// The MAIN, stripped of its render/voice/scene/.esc leaves, is:
//
//   1. validate both combatants resolve (Person_FindRecordById != 0); if either
//      is missing the duel aborts (returns, no outcome). If the duel is a remote
//      "report only" replay (dword_6315BC) it forwards to ReportToOffice — here
//      that is a hook.
//   2. seed the shared replay RNG table (Util_InitAndShuffleDwordArray(10)) and
//      reset the duel score (byte_6315DC/DD = 0), the cursor (dword_6315E8 = 0,
//      len dword_6315E4 = 10).
//   3. snapshot the two combatants into local CombatUnit copies (dword_11B4E30 /
//      11B4E34) and spawn their actors (leaf).
//   4. run up to 3 intro/shoot ROUNDS (the `do { } while (v52 < 3 && !over)`):
//        per round, clear the per-round flags (rattled/aim for both), then run
//        each combatant's chosen intro action (ProcessIntroChoice): choice 2 =
//        TAUNT (contested roll -> loser rattled), 3 = AIM (skill check -> aim
//        flag), 4 = SHOOT (resolve a pistol shot via Duel_ResolveShot). The
//        choice byte per participant is the participant-row +148 value
//        (dword_11AB094), set by RollDuelOutcomeTier / the GUI; injected here.
//        After both act, the fatal-hit gate (dword_6315C4) may end the duel.
//   5. outcome: if a fatal hit occurred (over), the winner is whoever has the
//      higher current HP ratio (Building_ComputeOutputRatio); otherwise it is a
//      draw. The winner's office/relation updates + the cmd27 turn-sync are the
//      host's job (hooks).
//
// All GUI/scene/voice/script leaves route through DuelCutsceneHooks. The RULES
// math (taunt/aim/shot) is the already-recovered duel.{h,cpp} core, reused as-is.
#include "guild/common/types.h"
#include "sim/combat_types.h"   // DuelState, CombatUnit
#include "sim/combat.h"         // CutsceneRng (the combat-side LCG used by duel.h)
#include "sim/duel.h"           // Duel_ResolveShot / Taunt / Aim / fatal-hit

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered duel constants.
//   dbl_61D0A4 = 0.5    duel-score -> money scale (a)
//   dbl_61D0AC = 10.0   duel-score -> money scale (b)  => money = score*0.5*10
//   The fatal-hit ratio (0.2) lives in duel.h as kDuelFatalRatio.
//   The shared replay table is seeded to length 10 (Util_InitAndShuffleDwordArray
//   (10, dword_11AA540); dword_6315E4 = 10; dword_6315E8 = 0).
// ---------------------------------------------------------------------------
constexpr double kDuelScoreMulA = 0.5;   // dbl_61D0A4
constexpr double kDuelScoreMulB = 10.0;  // dbl_61D0AC
constexpr int    kDuelReplayLen = 10;    // dword_6315E4
constexpr int    kDuelMaxRounds = 3;     // v52 < 3 loop bound

// Per-combatant intro choice (the participant-row +148 value, dword_11AB094).
enum class DuelIntroChoice : u8 {
    kNone  = 0,
    kTaunt = 2,   // contested taunt roll
    kAim   = 3,   // concentrate / aim skill check
    kShoot = 4,   // resolve a pistol shot
};

// ---------------------------------------------------------------------------
// Per-combatant duel inputs. The original pulls these from the two snapshotted
// CombatUnit copies + the per-frame production-rating accessor + the per-round
// participant choice. We bundle them so the FSM is a pure function.
// ---------------------------------------------------------------------------
struct DuelCombatant {
    i32   personId   = -1;     // slot +52 / +56 (a1[13] / a1[14])
    int   hp         = 100;    // CombatUnit +0x24 — current HP
    int   worth      = 100;    // CombatUnit +0x1C — value scale (HP loss = worth*...)
    float skill      = 0.0f;   // EvalProductionRating(unit, 3/4) — shoot/taunt skill
    float aimRating  = 0.0f;   // EvalProductionRating(unit, 2) — aim skill check
    bool  goodPistol = false;  // object slot 367 active -> hit chance * 1.5
    // The per-round chosen intro action (RollDuelOutcomeTier / GUI -> +148).
    DuelIntroChoice choice = DuelIntroChoice::kShoot;
};

// Result of running a duel to completion.
struct DuelOutcome {
    bool over     = false;   // dword_6315C4 — a fatal hit occurred
    int  winner   = -1;      // 0 = A, 1 = B, -1 = draw
    int  scoreA   = 0;       // byte_6315DC accumulated hit score
    int  scoreB   = 0;       // byte_6315DD
    int  hpA      = 0;       // A's HP after the duel
    int  hpB      = 0;       // B's HP after the duel
    int  rounds   = 0;       // rounds actually played
    bool aborted  = false;   // a combatant failed to resolve (early return)
};

// ---------------------------------------------------------------------------
// Leaf hooks — the duel main's scene/voice/script/command side effects. A test
// installs a recording mock; nullptr installs an inert default.
// ---------------------------------------------------------------------------
struct DuelCutsceneHooks {
    // VIBE_Cutscene_LoadScene("duell.ed3"…) — load the duel arena (kIntro).
    void (*loadScene)(const char* scene, void* ctx) = nullptr;
    // One round's presentation marker (round index, choice each combatant made).
    void (*onRound)(int round, DuelIntroChoice a, DuelIntroChoice b, void* ctx) = nullptr;
    // One shot resolved (shooter 0/1, hit, damage, target HP after).
    void (*onShot)(int shooter, const DuelShotResult& r, void* ctx) = nullptr;
    // dword_6315BC — remote "report only" replay (forward to ReportToOffice). When
    // this returns true the main forwards and does NOT run the local duel.
    bool (*reportOnly)(void* ctx) = nullptr;
    // The winner's office/relation update + the cmd27 turn-sync (kSentence).
    void (*onOutcome)(const DuelOutcome& out, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// ===========================================================================
// gilde.exe 0x4a6908 — VIBE_Cutscene_RollDuelOutcomeTier.
//   if (dword_6315A4) { r = RandInt(100); slot[+148] = r>50 ? 4 : (r<=15 ? 3 : 2); }
//   else              { r = RandInt(10);  slot[+148] = (r > 7); }
// `tieredMode` mirrors dword_6315A4. Returns the chosen choice byte (also the
// duel intro choice the participant takes). In tiered mode the value is a
// DuelIntroChoice (2/3/4); otherwise it is a 0/1 flag.
u8 DuelRollOutcomeTier(CutsceneRng& rng, bool tieredMode);

// ===========================================================================
// gilde.exe 0x4a4eb4 — VIBE_Duel_ProcessIntroChoice (deterministic spine).
//   Runs ONE combatant's chosen intro action against the other:
//     kTaunt: contested roll (Duel_ResolveTaunt) — loser becomes rattled.
//     kAim:   skill check (Duel_ResolveAim) — on success gain the aim flag.
//     kShoot: resolve a pistol shot (Duel_ResolveShot) — score + HP loss.
//   `actorIsA` selects whose flags/score apply. Mutates `state` and, on a shot,
//   the target combatant's HP + the shot result. Returns the shot result (a miss
//   with hit=false for non-shoot choices).
DuelShotResult DuelProcessIntroChoice(DuelState& state, bool actorIsA,
                                      DuelIntroChoice choice,
                                      const DuelCombatant& actor,
                                      const DuelCombatant& other,
                                      DuelCombatant& target, CutsceneRng& rng);

// ===========================================================================
// gilde.exe 0x4a53a8 — VIBE_Cutscene_Duel (deterministic core).
//   Validate both combatants, then run up to 3 rounds of intro choices + shots,
//   ending early on a fatal hit. Returns the outcome. `rng` is the cutscene LCG
//   (seeded by ExecMainFunc from the slot seed before this runs). The two
//   combatants carry their per-round `choice`.
//
// NOTE the original re-reads each combatant's choice per round from the GUI /
// RollDuelOutcomeTier; here the caller sets `a.choice` / `b.choice` before each
// round via the supplied `chooser` callback (nullptr -> keep the current choice).
// `rng` is the combat/cutscene LCG (combat.h CutsceneRng); the original seeds it
// from the slot's seed word in ExecMainFunc before invoking this main.
using DuelChooser = void (*)(int round, DuelCombatant& a, DuelCombatant& b,
                             CutsceneRng& rng, void* ctx);

DuelOutcome CutsceneDuel(DuelCombatant a, DuelCombatant b, CutsceneRng& rng,
                         const DuelCutsceneHooks& hooks,
                         DuelChooser chooser = nullptr, void* chooserCtx = nullptr);

} // namespace guild::sim
