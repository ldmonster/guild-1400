#pragma once
// gilde.exe — Combat resolution: the RULES/MATH core of the brawl/battle mode
// (namespace guild::sim). MODULE: combat (prefix VIBE_Combat_*).
//
// SCOPE. This file translates the deterministic combat-resolution math:
//   * the RNG helpers the rolls use (Math_RandomModulo, Cutscene_RandInt/Float),
//   * unit lookup (FindUnitById) over the 32-slot combat-unit array,
//   * melee damage + the HP-ratio death gate,
//   * distance / target-selection helpers,
//   * the "beating" (street-brawl) target picker.
// The render/animation/HUD/scenario-driver functions (deployment & result
// screens, cutscenes, particle/voice/mesh attach, the 0x159d order-tick state
// machine, projectile/bomb physics) are LISTED as DEFERRED in combat.cpp — they
// are presentation, not rules. State mutations that the originals route through
// the lockstep command channel are funnelled through a forward-declared command
// hook (ICombatCommandSink) so the rules stay testable in isolation.
//
// RNG NOTE. Two distinct generators drive the rolls and MUST be reproduced
// exactly for determinism:
//   1. crt::RandNext  — the CRT LCG (state seeded by Srand). Math_RandomModulo
//      uses it:  RandNext() % n.
//   2. The "cutscene" LCG — a SEPARATE state (dword_11B4E38) advanced as
//      state = 1103515245*state + 12345, returning (state>>16)%0x7FFF, then % n.
//   3. The duel shot uses a shared replay table (dword_11AA540) for cross-peer
//      determinism — modelled by DuelRng.
#include "guild/common/types.h"
#include "sim/combat_types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// RNG helpers
// ===========================================================================

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo  (__usercall, eax = (n@ax))
// Returns RandNext() % n, or 0 when n == 0. Uses the CRT LCG (crt::RandNext).
int Math_RandomModulo(u16 n);

// The "cutscene" pseudo-random generator — a SEPARATE 32-bit LCG state
// (gilde.exe dword_11B4E38 @0x11B4E38). Combat & duel rolls that must stay in
// sync with cutscene scripting use this, NOT the CRT generator.
struct CutsceneRng {
    u32 state = 0;   // dword_11B4E38; game seeds it explicitly at battle start

    // gilde.exe 0x4ac9e8 — VIBE_Cutscene_RandInt(n) -> ((state>>16)%0x7FFF) % n.
    // Advances state = 1103515245*state + 12345 first. Returns n unchanged when
    // n == 0 (the original early-outs returning its eax input).
    u32 RandInt(u32 n);

    // gilde.exe 0x4aca48 — VIBE_Cutscene_RandFloat -> ((state>>16)%0x7FFF) * 2^-15.
    // Range [0, ~1.0). Used as the duel hit-chance roll.
    double RandFloat();
};

// ===========================================================================
// Combat-unit array (gilde.exe word_B5A350 @0xB5A350, stride 536, 32 slots)
// ===========================================================================

// A simple owning array that mirrors the original's flat fixed-stride layout and
// the linear id->ptr scan. (The originals address a static global; the model is
// observationally identical.)
class CombatField {
public:
    CombatField() : units_(kUnitCapacity), ids_(kUnitCapacity, 0) {
        for (auto& u : units_) u.marker = -1;
    }

    CombatUnit* units() { return units_.data(); }
    int capacity() const { return kUnitCapacity; }

    // gilde.exe 0x486430 — VIBE_Combat_FindUnitById  (linear scan, bound 17152).
    // Returns the unit whose slot is occupied (marker != -1) and whose id-column
    // entry equals `id`, else nullptr.
    CombatUnit* FindUnitById(i32 id);

    // Test/setup helper: claim the first free slot, set marker/id/hp/alive.
    CombatUnit* Spawn(i32 id, i32 hp, i32 teamId);

private:
    std::vector<CombatUnit> units_;
    std::vector<i32>        ids_;   // dword_B5A354 id column
};

// ===========================================================================
// Melee resolution
// ===========================================================================

// gilde.exe 0x4bfab4 — VIBE_Combat_ApplyMeleeHit  (RULES core).
//   unit->hp -= (RandomModulo(60) + 40);    // a damage roll in [40, 99]
//   then runs the death gate (ApplyUnitDeath). Returns true if the unit died.
// (The original additionally spawns a damage number, blood particle and a
//  stagger sound — presentation, omitted; see DEFERRED list.)
bool ApplyMeleeHit(CombatUnit& unit);

// The damage roll alone (RandomModulo(60) + 40), exposed for golden testing.
int RollMeleeDamage();

// gilde.exe 0x48c790 — VIBE_Combat_ApplyUnitDeath  (RULES core extracted).
// The death gate: a unit is "down" once its remaining HP ratio drops below the
// threshold. The original computes the ratio via the building-output model and
// compares to dbl_61B964 == 0.05; if >= 0.05 it survives (returns 0). We
// reproduce the comparison against the provided current/max HP ratio.
//   returns true (dead) iff  currentHp / maxHp < kDeathRatio (0.05)
// On death it clears the alive byte (unit->alive = 0).
bool ApplyUnitDeath(CombatUnit& unit, double currentHp, double maxHp);
constexpr double kDeathRatio = 0.05;   // dbl_61B964

// ===========================================================================
// Target selection / distance
// ===========================================================================

// gilde.exe 0x4864a0 — VIBE_Combat_DistanceToTargetXZ.
// Planar (X,Z) Euclidean distance between two world positions (the Y term is
// multiplied by 0.0 in the original, i.e. ignored). Each position is (x, z).
double DistanceXZ(float ax, float az, float bx, float bz);

// A live unit's world position for target search (x, z) plus its current HP
// ratio (the originals weight distance by Building_ComputeOutputRatio).
struct UnitPose {
    const CombatUnit* unit;
    float x;
    float z;
    double hpRatio;   // VIBE_Building_ComputeOutputRatio(unit), in [0,1]
};

// gilde.exe 0x48ad54 — VIBE_Combat_FindNearestEnemyUnit  (RULES core).
// Scans the 32-slot live-unit set; among units of a different team that are
// alive, returns the one minimising  hpRatio * distance(self, candidate).
// `self` provides the searcher's position+team; returns nullptr if none.
const CombatUnit* FindNearestEnemyUnit(float selfX, float selfZ, i32 selfTeam,
                                       const std::vector<UnitPose>& candidates);

// (Duel pistol-duel mini-game lives in duel.{h,cpp}.)

// ===========================================================================
// Command hook (lockstep) — forward-declared / mockable.
// ===========================================================================
// In the originals, combat damage and target assignment are broadcast as
// network commands (Command_BeginDeltaPacket / QueueRequest17 / EnqueueCmd15 /
// RequestBuildOp78). The rules above mutate the local model directly for
// testability; a host wires this sink to also emit the lockstep packet.
class ICombatCommandSink {
public:
    virtual ~ICombatCommandSink() = default;
    // Mirror of the delta the originals push when a unit takes damage.
    virtual void OnUnitDamage(i32 unitId, i32 newHp) = 0;
    virtual void OnUnitDeath(i32 unitId) = 0;
};

// Optional global sink; nullptr by default (rules run standalone).
void SetCombatCommandSink(ICombatCommandSink* sink);
ICombatCommandSink* CombatCommandSink();

} // namespace guild::sim
