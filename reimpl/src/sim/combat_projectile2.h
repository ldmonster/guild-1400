#pragma once
// gilde.exe — Combat PROJECTILE remainder: the throwing-knife (weapon 370)
// transparency/aim rule and the arrow-spawn loop's per-frame gate, extracted from
// VIBE_Combat_UpdateProjectiles @0x487760 (namespace guild::sim). MODULE: combat.
// The earlier projectile agent translated the arrow-spawn DAMAGE ROLL
// (RollProjectileDamage in projectile.cpp). This file translates the rest of the
// projectile pass that is RULE (not pure presentation):
//
//   * the per-frame ARROW-SPAWN GATE: the spawn loop only runs on the host's turn
//     (dword_631204 == active level) AND every 350 (0x15E) game-ticks; each active
//     projectile slot's countdown byte (+8) is decremented, with the value-2 slot
//     re-armed to 100 — the projectile lifetime/cadence rule.
//   * the THROWING-KNIFE (370) AIM rule: find the nearest live enemy unit within
//     the throw radius (100.0); the knife's mesh transparency is
//        255 - nearestDist * 0.01 * 255      (clamped to a byte),
//     i.e. a linear fade with proximity (the closer the target, the more opaque).
//     When a friendly/own marker (0x800-style highlight) is set the knife is fully
//     opaque (transparency 128 in that branch). The transparency write itself is
//     presentation; the in-range detection + falloff value is the rule.
//
// Determinism: the arrow damage roll (already in projectile.cpp) uses the cutscene
// LCG. The cadence gate is pure integer arithmetic on the game-tick clock.
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_types.h"

#include <vector>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants:
//   dbl_61B1FC = 0.01    (arrow-spawn damage * worth scale — see projectile.cpp)
//   flt_61B204 = 0.01    (throwing-knife transparency distance scale)
//   flt_61B208 = 255.0   (full opacity / byte max)
//   knife throw radius   = 100.0  (SLODWORD < 1120403456)
//   spawn cadence        = 350 ticks (0x15E)
// ---------------------------------------------------------------------------
constexpr float kKnifeTransparencyScale = 0.01f;   // flt_61B204
constexpr float kFullOpacity            = 255.0f;  // flt_61B208
constexpr float kKnifeThrowRadius       = 100.0f;  // 1120403456 == 100.0f
constexpr u32   kArrowSpawnCadence      = 350;     // 0x15E game-ticks

// gilde.exe 0x487760 — the arrow-spawn cadence gate.
// The spawn loop runs only on the host's active level every 350 ticks:
//   return (activeLevel == hostLevel) && (gameTick % 350 == 0);
bool ArrowSpawnGateOpen(i32 activeLevel, i32 hostLevel, u32 gameTick);

// gilde.exe 0x487760 — the per-slot projectile lifetime tick.
// Each active projectile slot holds a countdown byte (+8, 1..4). Per pass it is
// decremented; a slot reaching the "fire" value 2 is RE-ARMED to 100 (it just
// launched and now waits out its travel). Returns the new countdown value and a
// `fired` flag (true when the slot was at 2 -> launched this pass).
//   if (count <= 4 && count != 0) { --count; if (count was 2) { count = 100; fired } }
struct ProjectileTick { u8 count = 0; bool fired = false; bool active = false; };
ProjectileTick TickProjectileSlot(u8 countdown);

// gilde.exe 0x487760 — the throwing-knife (370) AIM/transparency rule.
// Scans live enemy units (different faction, alive) for the nearest within the
// throw radius (100.0, planar XZ). If a friendly/own highlight is set the knife is
// rendered at the fixed `highlightOpacity` (128). Otherwise, when a target is in
// range, the transparency is a linear fade:
//   transparency = (u8)(255.0 - nearestDist * 0.01 * 255.0).
// When NO target is in range the knife is hidden (inRange == false). Returns the
// computed byte transparency + whether a target was found. `nearestDist` is the
// planar distance to the closest in-range enemy (caller pre-resolves the scan
// distance; pass < 0 when none in range). `highlighted` mirrors the 0x800/global
// highlight branch (v16 & 8).
struct KnifeAim {
    bool inRange     = false;  // v38 — a live enemy was within radius
    bool highlighted = false;  // the own-side highlight branch was taken
    u8   transparency = 0;     // the byte written to Object_ChangeTransparency
};
KnifeAim ResolveKnifeAim(float nearestDist, bool highlighted, u8 highlightOpacity = 128);

// Convenience: scan a target set for the nearest in-range enemy of `selfFaction`,
// returning its planar distance (or -1.0f if none). Mirrors the inner loop of the
// throwing-knife branch (v36 = min dist over live enemies within 100.0).
struct KnifeTarget {
    i32   faction = 0;
    bool  alive   = false;
    float dist    = 1e30f;   // pre-computed planar XZ distance to the thrower
};
float NearestKnifeTargetDist(i32 selfFaction, const std::vector<KnifeTarget>& targets);

} // namespace guild::sim
