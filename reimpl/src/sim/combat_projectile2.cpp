#include "sim/combat_projectile2.h"

#include <cmath>

namespace guild::sim {

// gilde.exe 0x487760 — the arrow-spawn cadence gate.
bool ArrowSpawnGateOpen(i32 activeLevel, i32 hostLevel, u32 gameTick) {
    return activeLevel == hostLevel && (gameTick % kArrowSpawnCadence) == 0;
}

// gilde.exe 0x487760 — the per-slot projectile lifetime tick.
//   v3 = count;
//   if (v3 <= 4 && v3 != 0) {       // active slot
//       count = v3 - 1;             // decrement
//       if (v3 == 2)  count = 100;  // re-armed (just launched), keep scanning
//       else          fired;        // value 1/3/4 -> exits to spawn arrows
//   }
ProjectileTick TickProjectileSlot(u8 countdown) {
    ProjectileTick t;
    if (countdown != 0 && countdown <= 4) {
        t.active = true;
        u8 next = static_cast<u8>(countdown - 1);
        if (countdown == 2) {
            t.count = 100;             // re-arm; this slot does NOT spawn this pass
            t.fired = false;
        } else {
            t.count = next;
            t.fired = true;            // breaks out to the arrow-spawn loop
        }
    } else {
        t.count = countdown;           // inactive (0) or out of band (>4): skipped
    }
    return t;
}

// gilde.exe 0x487760 — the throwing-knife (370) AIM/transparency rule.
KnifeAim ResolveKnifeAim(float nearestDist, bool highlighted, u8 highlightOpacity) {
    KnifeAim a;
    if (highlighted) {
        a.highlighted = true;
        a.inRange = true;
        a.transparency = highlightOpacity;   // v22 = 128 (own-side highlight)
        return a;
    }
    if (nearestDist < 0.0f) {
        a.inRange = false;                    // v38 == 0: no target -> hidden
        a.transparency = 0;
        return a;
    }
    a.inRange = true;
    // v19 = 255.0 - v36 * 0.01 * 255.0;  v22 = (u8)(int)v19
    float v19 = kFullOpacity - nearestDist * kKnifeTransparencyScale * kFullOpacity;
    int iv = static_cast<int>(v19);
    a.transparency = static_cast<u8>(iv);     // truncate to byte (matches original)
    return a;
}

// gilde.exe 0x487760 — the throwing-knife nearest-enemy scan (inner loop).
float NearestKnifeTargetDist(i32 selfFaction, const std::vector<KnifeTarget>& targets) {
    bool found = false;
    float best = 999999.0f;                   // v36 = 999999.0
    for (const KnifeTarget& t : targets) {
        if (t.faction == selfFaction)         // skip same faction
            continue;
        if (!t.alive)                          // skip dead/non-units
            continue;
        if (t.dist < kKnifeThrowRadius) {      // SLODWORD(dist) < 100.0f
            found = true;
            if (t.dist < best)
                best = t.dist;
        }
    }
    return found ? best : -1.0f;
}

} // namespace guild::sim
