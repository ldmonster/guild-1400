// character_social — the idle/social branch of Character_Update: the proximity
// scan and the "spawn a talk action when a neighbour is near" behaviour. Faithful
// 1:1 port of the gilde.exe control flow; render/heightmap leaves via SocialHooks.
#include "sim/character_social.h"

#include "sim/actionqueue.h"
#include "sim/charaction.h"
#include "sim/character.h"

#include <cmath>

namespace guild::sim {

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance (axis-aligned box test).
bool VectorWithinTolerance(const float a[3], const float b[3], float tol) {
    return std::fabs(b[0] - a[0]) <= tol
        && std::fabs(b[1] - a[1]) <= tol
        && std::fabs(b[2] - a[2]) <= tol;
}

// ---------------------------------------------------------------------------
// Idle/social hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const SocialHooks* g_social = nullptr;
int   DefResolveMeetTile(Character*, Character*, int* c, int* r) { if (c) *c = 0; if (r) *r = 0; return 1; }
void* DefAttachIdleAnim(Character*, bool) { return reinterpret_cast<void*>(1); }
const SocialHooks g_socialDefault = { DefResolveMeetTile, DefAttachIdleAnim };
} // namespace

void SetSocialHooks(const SocialHooks* h) { g_social = h; }
const SocialHooks& GetSocialHooks() { return g_social ? *g_social : g_socialDefault; }

// gilde.exe 0x40507c — VIBE_Character_FindNearbyInRadius.
// Walks dword_66F0D0[0..511]; appends each qualifying neighbour pointer to `out`.
//   v4 = 0; v5 = 0; v6 = 0;       (hit count / index / byte cursor)
//   do {
//     v7 = chars[v5];
//     if (v7 && self != v7) {
//       if (self.avatar[34] == cand.avatar[34] && self.avatar[11] == cand.avatar[11]) {
//         act = cand.avatar[74];
//         if (!act || *(act+9) != 45) {       // not already talking
//           if (*(cand.mesh+533) != 1 || (m2 = cand.mesh2) && *(m2+533) != 1) {
//             if (VectorWithinTolerance(self.avatar[13]+76, cand.mesh+76, radius)) {
//               ++v4; out[v6/4] = chars[v5]; v6 += 4;
//             } } } } }
//     ++v5;
//   } while (v5 < 512 && v6 < 64);
//   return v4;
int FindNearbyInRadius(Character* self, Character** out, int maxOut, float radius) {
    SocialAvatar* sa = self->social;
    int hits = 0;
    int cursor = 0;                     // v6: byte cursor (caps at 64 == 16 ptrs)
    for (int i = 0; i < kCharacterCapacity; ++i) {
        Character* cand = g_characters[i];
        if (!cand || cand == self)
            continue;
        SocialAvatar* ca = cand->social;
        if (!ca)
            continue;
        if (sa->groupId != ca->groupId || sa->worldId != ca->worldId) // [34]/[11]
            continue;
        if (ca->hasAction && ca->actionType == 45)   // already in a type-45 action
            continue;
        // mesh gate: *(mesh+533) != 1, OR a secondary mesh exists with +533 != 1.
        bool gateOk = (ca->meshGate != 1)
                   || (ca->hasMesh2 && ca->meshGate2 != 1);
        if (!gateOk)
            continue;
        if (VectorWithinTolerance(sa->pos, ca->pos, radius)) {
            if (cursor < maxOut * 4 && cursor < 64) {
                out[cursor / 4] = cand;
                cursor += 4;
                ++hits;
            }
        }
        if (cursor >= 64)
            break;
    }
    return hits;
}

// gilde.exe 0x405148 (idle branch) — VIBE_Character_UpdateIdleSocial.
// Reproduces the `else` of the +296 has-action gate plus the stand/sit idle path:
//   if (scene != ch+136 || (ch+140 & 2) || ch+141<0 (0x80) || (ch+140 & 0x20)
//       || ch+292) { ch+140 &= ~8; }                       // not eligible
//   else if (FindNearbyInRadius(ch, buf, 20.0)) {
//       mesh = ResolveMesh(ch); ComputeTargetTile(&tile, world);
//       if (WorldToTileWithHeight(mesh, world, &col, &row))
//          InsertActionVararg(ch | type45, col, row, scene);  // spawn talk
//   }
//   ... (idle stand/sit attach handled below when no action and idle-anim pending)
int UpdateIdleSocial(Character* ch) {
    const SocialHooks& hk = GetSocialHooks();

    // Eligibility gate: must be in the active scene, not dirty-anim (+140 & 2),
    // not hidden/busy (+141 & 0x80), not sitting (+140 & 0x20), no target (+292).
    bool eligible = (ch->scene != nullptr)                 // scene == off_649D64
                 && (ch->flagsA & 0x02) == 0
                 && (ch->flagsB & 0x80) == 0
                 && (ch->flagsA & 0x20) == 0
                 && ch->targetWorldX == 0.0f;
    if (!eligible) {
        ch->flagsA &= ~0x08u;                              // ch+140 &= ~8
        return 0;
    }

    // gilde.exe 0x405418: `else if ( FindNearbyInRadius(...) )`.  Note the binary
    // does NOT clear +140&~8 in the eligible-but-no-neighbour case — the flag clear
    // only happens in the NOT-eligible branch above (0x4054a9).  When a neighbour is
    // found but WorldToTileWithHeight misses, it likewise just falls through.
    Character* buf[16] = {};
    if (FindNearbyInRadius(ch, buf, 16, 20.0f)) {
        Character* other = buf[0];
        int col = 0, row = 0;
        if (hk.resolveMeetTile(ch, other, &col, &row)) {  // 0x405469 hit
            // VIBE_CharAction_InsertActionVararg(ch | type45, col, row, scene id).
            i32 args[3] = { col, row, ch->slotIndex };
            InsertActionVararg(ch, kActWalk /*45 talk/walk*/, args, 3);
            return 1;
        }
        return 0;                                          // tile miss -> no action
    }
    return 0;                                              // no neighbour -> no flag clear
}

} // namespace guild::sim
