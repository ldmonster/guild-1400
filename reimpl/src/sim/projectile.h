#pragma once
// gilde.exe — Combat projectile / bomb physics (namespace guild::sim).
// MODULE: combat battle (prefix VIBE_Combat_*). Deferred by the earlier combat
// agent (combat.cpp DEFERRED list: 0x487760 / 0x4866b8 / 0x486ce4); translated
// here as the deterministic TRAJECTORY + AREA-OF-EFFECT DAMAGE math.
//
// SCOPE. This file translates the gameplay rules of the two bomb explosion
// passes and the arrow/projectile spawn roll:
//   * VIBE_Combat_UpdateBombExplosions @0x4866b8 — dropped-bomb (weapon type 372)
//     timed detonation; blast radius 170.0, linear falloff 1/170.
//   * VIBE_Combat_UpdateThrownBombs   @0x486ce4 — thrown-bomb (weapon type 374)
//     on-impact detonation; blast radius 100.0, linear falloff 1/100.
//   * VIBE_Combat_UpdateProjectiles   @0x487760 — the arrow-spawn damage roll
//     (the only rule there; the rest is mesh transparency / aim-turn presentation
//     and is DEFERRED).
// The particle/smoke/debris/voice/heightmap-terrain-scorch presentation is
// forward-declared away; the HP mutation is funnelled through ApplyUnitDeath
// (combat.cpp) plus the ICombatCommandSink hook so the rules stay testable.
//
// RNG NOTE. Bomb base-damage uses the cutscene LCG (CutsceneRng::RandInt), NOT
// the CRT generator — exactly as the originals (VIBE_Cutscene_RandInt @0x4ac9e8).
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_types.h"

#include <vector>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered float constants (decoded from the .rdata doubles/floats).
// ---------------------------------------------------------------------------
//   dropped bomb: dbl_61B164 = 0.01 (worth scale), flt_61B16C = 1/170 (falloff),
//                 radius 170.0 (1126825984), fuse +350 ticks.
//   thrown bomb:  dbl_61B1AC = 0.01 (worth scale), flt_61B1B4 = 1/100 (falloff),
//                 radius 100.0 (1120403456).
constexpr double kBombWorthScale     = 0.01;    // dbl_61B164 / dbl_61B1AC
constexpr float  kDroppedBombRadius  = 170.0f;  // float 170.0
constexpr float  kDroppedBombFalloff = 1.0f / 170.0f; // flt_61B16C
constexpr float  kThrownBombRadius   = 100.0f;  // float 100.0
constexpr float  kThrownBombFalloff  = 1.0f / 100.0f; // flt_61B1B4
constexpr u32    kDroppedBombFuse    = 350;     // detonates spawnTick + 350

// A live combat unit's world position + the HP store the blast subtracts from.
// (Mirrors the per-unit access in the bomb loops: position via the actor mesh
// at unit.actorPtr, HP at unit+36 == CombatUnit::hp.)
struct BlastTarget {
    CombatUnit* unit = nullptr;
    float x = 0.0f, y = 0.0f, z = 0.0f;  // world position of the unit
};

// Result of resolving a single blast against a single unit.
struct BombHit {
    i32   unitId   = -1;
    int   damage   = 0;     // HP actually subtracted (>= 0)
    bool  killed   = false; // ApplyUnitDeath fired
    int   teamColor = 0;    // damage-number colour: 1 == friendly side, else 2
};

// gilde.exe 0x486ce4 / 0x4866b8 — the shared blast-damage rule.
//   base = (bomb.minDamage + RandInt(bomb.maxDamage - bomb.minDamage)) *
//          unit.worth * 0.01;
//   dmg  = base * (1.0 - dist * falloff);    // dist = 3D distance to unit
//   unit.hp -= (int)dmg;  then ApplyUnitDeath(unit, hp, preHp).
// Applies to one target if it is alive and within `radius`. `rng` is the shared
// cutscene generator (advanced once per in-range target, as in the originals).
// `friendlySideId` is dword_6311E8 (side A owner) for the damage-number colour.
// Returns the hit (damage 0 / killed false if out of range / dead).
BombHit ResolveBlastOnTarget(Bomb& bomb, BlastTarget& target, float radius,
                             float falloff, i32 friendlySideId, CutsceneRng& rng);

// gilde.exe 0x4866b8 — VIBE_Combat_UpdateBombExplosions (rules core).
// Detonates every active DROPPED bomb whose fuse expired (spawnTick+350 < now),
// resolving its blast (radius 170) against all targets, deactivating it after.
// Returns the count of bombs that were still active this tick (the original's
// return value). Appends each resolved hit to `hitsOut`.
int UpdateBombExplosions(std::vector<Bomb>& bombs, std::vector<BlastTarget>& targets,
                         u32 now, i32 friendlySideId, CutsceneRng& rng,
                         std::vector<BombHit>* hitsOut = nullptr);

// gilde.exe 0x486ce4 — VIBE_Combat_UpdateThrownBombs (rules core).
// Detonates every active THROWN bomb (radius 100), resolving its blast against
// all targets, deactivating it after. Returns the active-bomb count this tick.
int UpdateThrownBombs(std::vector<Bomb>& bombs, std::vector<BlastTarget>& targets,
                      i32 friendlySideId, CutsceneRng& rng,
                      std::vector<BombHit>* hitsOut = nullptr);

// gilde.exe 0x487760 (excerpt) — the arrow-spawn damage roll.
//   range = objDef.maxDamage - objDef.minDamage;
//   dmg   = (minDamage + RandInt(range)) * unit.worth * 0.01;
// (The rest of UpdateProjectiles is mesh transparency + aim-turn presentation,
//  DEFERRED.) Returns the projectile damage value the original passes to
//  RequestBuildOp82.
int RollProjectileDamage(u8 minDamage, u8 maxDamage, float worth, CutsceneRng& rng);

} // namespace guild::sim
