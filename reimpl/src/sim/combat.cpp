#include "sim/combat.h"

#include "crt/rand.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constant: the cutscene-RNG RandFloat fixed-point scale.
//   flt_61D934 bytes = 00 01 00 38 == float 0x38000100 == 3.0518509447574615e-05
//   (this is float(1/32767), NOT 2^-15 == 0x38000000). Confirmed via get_bytes.
//   The original does `fild int; fmul flt_61D934` -> (double)int * (double)float.
// ---------------------------------------------------------------------------
static constexpr float kRandFloatScale = 3.0518509447574615e-05f; // flt_61D934 (0x38000100)

// ===========================================================================
// RNG helpers
// ===========================================================================

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo  (__usercall, eax = (n@ax))
//   if (n) return (int)VIBE_Util_RandNext() % n;  else return 0;
int Math_RandomModulo(u16 n) {
    if (n)
        return crt::RandNext() % static_cast<int>(n);
    return 0;
}

// gilde.exe 0x4ac9e8 — VIBE_Cutscene_RandInt(n)
//   if (!n) return n;  (original early-out)
//   state = 1103515245*state + 12345;
//   return ((state>>16) % 0x7FFF) % n;
u32 CutsceneRng::RandInt(u32 n) {
    if (!n)
        return n;
    state = 1103515245u * state + 12345u;          // 32-bit wraparound intended
    u32 r = (state >> 16) % 0x7FFFu;                // HIWORD(state) % 0x7FFF
    return r % n;
}

// gilde.exe 0x4aca48 — VIBE_Cutscene_RandFloat
//   state = 1103515245*state + 12345;
//   return (double)((state>>16) % 0x7FFF) * 2^-15;
double CutsceneRng::RandFloat() {
    state = 1103515245u * state + 12345u;
    u32 r = (state >> 16) % 0x7FFFu;
    return static_cast<double>(r) * static_cast<double>(kRandFloatScale);
}

// ===========================================================================
// Combat-unit array
// ===========================================================================

// gilde.exe 0x486430 — VIBE_Combat_FindUnitById
//   v2 = 0;
//   while (word_B5A350[v2/2] == -1 || id != dword_B5A354[v2/4]) {
//       v2 += 536;  if (v2 >= 17152) return 0;
//   }
//   return &word_B5A350[v2/2];
CombatUnit* CombatField::FindUnitById(i32 id) {
    for (int i = 0; i < kUnitCapacity; ++i) {
        if (units_[i].marker != -1 && ids_[i] == id)
            return &units_[i];
    }
    return nullptr;
}

CombatUnit* CombatField::Spawn(i32 id, i32 hp, i32 teamId) {
    for (int i = 0; i < kUnitCapacity; ++i) {
        if (units_[i].marker == -1) {
            CombatUnit& u = units_[i];
            u = CombatUnit{};
            u.marker = static_cast<i16>(id);   // low word holds the unit id (*v1)
            u.id     = id;
            u.alive  = 1;
            u.hp     = hp;
            u.teamId = teamId;
            ids_[i]  = id;
            return &u;
        }
    }
    return nullptr;
}

// ===========================================================================
// Command hook
// ===========================================================================
namespace {
ICombatCommandSink* g_sink = nullptr;
} // namespace

void SetCombatCommandSink(ICombatCommandSink* sink) { g_sink = sink; }
ICombatCommandSink* CombatCommandSink() { return g_sink; }

// ===========================================================================
// Melee resolution
// ===========================================================================

// gilde.exe 0x4bfab4 (excerpt): *(a1+36) -= (u16)RandomModulo(0x3C) + 40;
int RollMeleeDamage() {
    return static_cast<u16>(Math_RandomModulo(0x3C)) + 40;   // RandomModulo(60)+40
}

// gilde.exe 0x48c790 — VIBE_Combat_ApplyUnitDeath (rules core).
//   if (Building_ComputeOutputRatio(unit) >= dbl_61B964 /*0.05*/) return 0;
//   ... (present.) ... *(a1+8) = 0; return 1;
bool ApplyUnitDeath(CombatUnit& unit, double currentHp, double maxHp) {
    double ratio = (maxHp != 0.0) ? (currentHp / maxHp) : 0.0;
    if (ratio >= kDeathRatio)
        return false;          // survives
    unit.alive = 0;            // *(a1+8) = 0
    if (g_sink)
        g_sink->OnUnitDeath(unit.id);
    return true;
}

// gilde.exe 0x4bfab4 — VIBE_Combat_ApplyMeleeHit.
bool ApplyMeleeHit(CombatUnit& unit) {
    int dmg = RollMeleeDamage();        // RandomModulo(60)+40
    double maxHp = static_cast<double>(unit.hp); // pre-hit HP is the "max" baseline
    unit.hp -= dmg;                     // *(a1+36) -= dmg
    if (g_sink)
        g_sink->OnUnitDamage(unit.id, unit.hp);
    return ApplyUnitDeath(unit, static_cast<double>(unit.hp), maxHp);
}

// ===========================================================================
// Target selection / distance
// ===========================================================================

// gilde.exe 0x4864a0 — VIBE_Combat_DistanceToTargetXZ.
//   v3 = bx - ax;  v4 = bz - az;  return sqrt(v3*v3 + 0*0 + v4*v4);
double DistanceXZ(float ax, float az, float bx, float bz) {
    float dx = bx - ax;
    float dz = bz - az;
    return std::sqrt(static_cast<double>(dx) * dx +
                     static_cast<double>(dz) * dz);
}

// gilde.exe 0x48ad54 — VIBE_Combat_FindNearestEnemyUnit.
//   best = 1e6;  for each live enemy unit:
//     d = DistanceToTargetXZ(self, cand);
//     if (Building_ComputeOutputRatio(cand) * d < best) { pick = cand; best = d; }
// (Note the original compares the WEIGHTED value against `best` but then stores
//  the UNWEIGHTED distance into `best` — faithfully reproduced below.)
const CombatUnit* FindNearestEnemyUnit(float selfX, float selfZ, i32 selfTeam,
                                       const std::vector<UnitPose>& candidates) {
    const CombatUnit* pick = nullptr;
    double best = 1000000.0;
    for (const UnitPose& c : candidates) {
        if (!c.unit) continue;
        if (c.unit->teamId == selfTeam) continue;   // *(a1+364) != *(cand+364)
        if (!c.unit->alive) continue;               // *(cand+8)
        double d = DistanceXZ(selfX, selfZ, c.x, c.z);
        if (c.hpRatio * d < best) {
            pick = c.unit;
            best = d;        // stores the UNWEIGHTED distance (as in the original)
        }
    }
    return pick;
}

} // namespace guild::sim

// ===========================================================================
// DEFERRED — render / animation / HUD / scenario-driver coupled (presentation,
// not rules). Listed here with address + reason; not translated.
// ---------------------------------------------------------------------------
//   0x489ba8 Combat_LoadScenarioAssets  — asset/mesh/scenario loader (58 strings)
//   0x48a7b4 Combat_UnloadScenario      — scene teardown
//   0x48dab0 Combat_BuildDeploymentScreen (0xa31) — deployment UI
//   0x48e4e4 Combat_RunResultScreen     (0x180b) — result/scoreboard UI
//   0x490014 Combat_RunBattleSetup      — battle bootstrap (UI + spawn glue)
//   0x491688 Combat_UpdateUnitOrders    (0x159d) — order-tick state machine,
//                                         interleaved with anim/move/render
//   0x492c28 Combat_RunBattleLoop / 0x48c5e8 RunBattleFrameLoop / 0x48c648
//            RunOrderWaitLoop / 0x4905bc TickBattleState — frame/loop drivers
//   0x487760 Combat_UpdateProjectiles / 0x4866b8 UpdateBombExplosions /
//            0x486ce4 UpdateThrownBombs — projectile/bomb physics + particles
//   0x48736c Combat_UpdateDamageNumbers / 0x487300 SpawnDamageNumber /
//            0x487130 CreateHealthBarWindow / 0x4872a0 RefreshHealthBars /
//            0x48759c ShowCommandFeedback — floating UI
//   0x485e88 AttachObjectMesh / 0x485b94 ResetObjectHighlights /
//            0x48c708 SpawnDeathBloodPool / 0x4a4944 SpawnBloodPool /
//            0x4876d8 StartCutscene / 0x48ac0c PlayIntroCutscene /
//            0x48ace8 PlayOutroCutscene — mesh/particle/cutscene
//   0x48c96c Combat_ResolveMeleeHit (0x45c) — melee resolver, but body is ~95%
//            voice/particle/mesh/command-delta presentation wrapped around the
//            single rule (ApplyUnitDeath gate). The RULE is translated as
//            ApplyMeleeHit/ApplyUnitDeath; the anim/voice wrapper is deferred.
//   0x490a80 Combat_PerformAttackAction (0x498) — dispatches melee/bomb actions
//            into the character action queue (anim-coupled); rule = ApplyMeleeHit.
//   0x48751c/0x487548 damage-number table mgmt; 0x4870f0/0x489430/0x489578/
//            0x4895cc/0x4906a8 spawn/HUD/flag setup; 0x48d518/0x48d7f8/0x48fcf0
//            objective/roster/info-text panels; 0x48b744 InitDefaultParameters
//            (UI/balance defaults, no combat math); 0x48bba8/0x48be60/0x48bfb0/
//            0x48c15c/0x48c24c/0x48c400 AI role-assignment & order-building
//            (order-issuing glue feeding the anim queue) — deferred.
//   0x4a4eb4 Duel_ProcessIntroChoice (full) — UI/voice/script wrapper; the three
//            RULE branches are translated (Duel_ResolveTaunt/Aim + ResolveShot).
//   0x4a4b68 Duel_ResolveShot (full) — translated rules core; voice/particle/
//            text/command portions deferred.
//   0x4a4eb4..0x4a69b4 Duel_ReportToOffice / CheckParticipants / BuildMessages /
//            ProcessIntroChoice — duel framing, office-report & message UI.
// ===========================================================================
