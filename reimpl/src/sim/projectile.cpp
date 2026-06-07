#include "sim/projectile.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Shared blast-damage rule (the inner body of both bomb loops).
//
// gilde.exe 0x486ce4 (thrown) inner loop:
//   dx = bomb.x - unit.x; dy = bomb.y - unit.y; dz = bomb.z - unit.z;
//   dist = sqrt(dx*dx + dy*dy + dz*dz);
//   if (dist < radius) {
//       range  = maxDamage - minDamage;
//       baseI  = (int)((minDamage + RandInt(range)) * unit.worth * 0.01);
//       dmg    = (int)((1.0 - dist * falloff) * (double)baseI);
//       unit.hp -= dmg;
//       SpawnDamageNumber(unit, baseI, side==friendly ? 1 : 2);   // colour
//       if (!ApplyUnitDeath(unit)) PlayHitVoice();                // presentation
//   }
// (0x4866b8 dropped is byte-identical bar radius/falloff/constant — all equal in
//  value: worth-scale 0.01, falloff == 1/radius.)
//
// NOTE on the damage-number value: the original spawns the UN-attenuated base
// (`baseI`, the v30/v32 store) as the floating number, but subtracts the
// distance-attenuated `dmg` from HP. Both reproduced faithfully.
// ---------------------------------------------------------------------------
BombHit ResolveBlastOnTarget(Bomb& bomb, BlastTarget& target, float radius,
                             float falloff, i32 friendlySideId, CutsceneRng& rng) {
    BombHit hit;
    CombatUnit* unit = target.unit;
    if (!unit || !unit->alive)
        return hit;                 // *(unit+8) gate

    float dx = bomb.x - target.x;   // v9[19] - actor.x   (and y, z)
    float dy = bomb.y - target.y;
    float dz = bomb.z - target.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(dist < radius))           // v31/v36 < radius (int-compare of float bits)
        return hit;

    int range  = static_cast<int>(bomb.maxDamage) - static_cast<int>(bomb.minDamage);
    u32 roll   = rng.RandInt(static_cast<u32>(range));   // RandInt(max-min)
    int rollSum = static_cast<int>(bomb.minDamage) + static_cast<int>(roll);
    int baseI  = static_cast<int>(static_cast<double>(rollSum) *
                                  (static_cast<double>(unit->worth) * kBombWorthScale));
    int dmg = static_cast<int>((1.0 - static_cast<double>(dist) *
                                static_cast<double>(falloff)) *
                               static_cast<double>(baseI));

    int preHp = unit->hp;
    unit->hp -= dmg;                // *(unit+36) -= dmg
    if (CombatCommandSink())
        CombatCommandSink()->OnUnitDamage(unit->id, unit->hp);

    hit.unitId    = unit->id;
    hit.damage    = dmg;
    hit.teamColor = (friendlySideId == unit->teamId) ? 1 : 2;  // dword_6311E8 cmp
    hit.killed    = ApplyUnitDeath(*unit, static_cast<double>(unit->hp),
                                   static_cast<double>(preHp));
    return hit;
}

// gilde.exe 0x4866b8 — VIBE_Combat_UpdateBombExplosions.
//   for each dropped-bomb slot:
//     if (slot.handle && slot.spawnTick + 350 < now) { ...detonate, free... }
//     count active slots; return the count.
int UpdateBombExplosions(std::vector<Bomb>& bombs, std::vector<BlastTarget>& targets,
                         u32 now, i32 friendlySideId, CutsceneRng& rng,
                         std::vector<BombHit>* hitsOut) {
    int active = 0;
    for (Bomb& b : bombs) {
        if (!b.active)
            continue;
        ++active;                                       // ++v28 (active count)
        if (b.spawnTick + kDroppedBombFuse < now) {     // v2 < dword_62EB38
            for (BlastTarget& t : targets) {
                BombHit h = ResolveBlastOnTarget(b, t, kDroppedBombRadius,
                                                 kDroppedBombFalloff, friendlySideId, rng);
                if (hitsOut && h.unitId != -1)
                    hitsOut->push_back(h);
            }
            b.active = false;                           // dword_B5F810[i] = 0
        }
    }
    return active;
}

// gilde.exe 0x486ce4 — VIBE_Combat_UpdateThrownBombs.
//   for each thrown-bomb slot with the 0x20 "landed" flag set: detonate (radius
//   100), then free; return the active count.
int UpdateThrownBombs(std::vector<Bomb>& bombs, std::vector<BlastTarget>& targets,
                      i32 friendlySideId, CutsceneRng& rng,
                      std::vector<BombHit>* hitsOut) {
    int active = 0;
    for (Bomb& b : bombs) {
        if (!b.active)
            continue;
        ++active;                                       // ++v31 (active count)
        for (BlastTarget& t : targets) {
            BombHit h = ResolveBlastOnTarget(b, t, kThrownBombRadius,
                                             kThrownBombFalloff, friendlySideId, rng);
            if (hitsOut && h.unitId != -1)
                hitsOut->push_back(h);
        }
        b.active = false;                               // dword_B5F910[i] = 0
    }
    return active;
}

// gilde.exe 0x487760 (excerpt) — arrow/projectile spawn damage roll.
//   v6 = minDamage; v7 = RandInt(maxDamage - minDamage);
//   dmg = (double)(v6 + v7) * unit.worth * 0.01;
int RollProjectileDamage(u8 minDamage, u8 maxDamage, float worth, CutsceneRng& rng) {
    int range = static_cast<int>(maxDamage) - static_cast<int>(minDamage);
    u32 roll  = rng.RandInt(static_cast<u32>(range));
    int rollSum = static_cast<int>(minDamage) + static_cast<int>(roll);
    return static_cast<int>(static_cast<double>(rollSum) *
                            static_cast<double>(worth) * kBombWorthScale);
}

} // namespace guild::sim
