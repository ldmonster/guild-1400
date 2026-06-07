#pragma once
// character_social — the idle/social branch of the per-frame Character driver that
// the first character agent deferred: the proximity scan that finds nearby actors
// and the idle behaviour that, when a neighbour is in range, spawns a "talk"
// (type 45) action; otherwise attaches a stand/sit idle animation.
//
//   VIBE_Character_FindNearbyInRadius   0x40507c  (proximity scan, up to 16 hits)
//   VIBE_Character_Update idle branch   0x405148  (the `else` of the +296 gate)
//
// The proximity math (VectorWithinTolerance) and the spawn logic are translated
// 1:1; the render/anim/heightmap leaves (ResolveMesh, ComputeTargetTile,
// WorldToTileWithHeight, AttachAni) are routed through the hook below, mirroring
// the existing charaction.h / charaction_misc.h pattern.
#include "guild/common/types.h"

namespace guild::sim {

struct Character; // character.h

// ===========================================================================
// Social avatar view — the avatar fields the proximity scan reads (the float*
// "v8" object in the decompilation, Character +20 -> avatar):
//   avatar[11] : entity/world id     (must match the scanning actor's)
//   avatar[13] : 3D object ptr; (+76) = world position X,Y,Z floats
//   avatar[34] : parent character / group id  (must match)
//   avatar[74] : current action node ptr; if its +9 type == 45 the actor is
//                already talking/walking and is skipped as a candidate.
// We model exactly these fields so the scan is testable without the renderer.
// ===========================================================================
struct SocialAvatar {
    i32   worldId;       // avatar[11]
    i32   groupId;       // avatar[34]
    float pos[3];        // avatar[13]+76 : world X,Y,Z
    u8    actionType;    // avatar[74]->+9 : current action type (45 == busy/talk)
    bool  hasAction;     // avatar[74] != null
    u8    meshGate;      // mesh+533 (1 == "open"/skip-gate)
    u8    meshGate2;     // +492 secondary mesh +533
    bool  hasMesh2;      // +492 != null
};

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance. True when every component
// of |a - b| is within `tol` (axis-aligned box test, the engine's neighbour gate).
bool VectorWithinTolerance(const float a[3], const float b[3], float tol);

// gilde.exe 0x40507c — VIBE_Character_FindNearbyInRadius. Scans the live-character
// array (dword_66F0D0[0..511]); for each other live actor in the same group
// (avatar[34]) and world (avatar[11]) that is not already running a type-45 action
// and whose mesh gate test passes, if its world position is within `radius` of the
// scanning actor it is appended to `out`. Stops at 512 actors or 16 hits (the
// original's v6<64 byte cursor). Returns the number of hits.
int FindNearbyInRadius(Character* self, Character** out, int maxOut, float radius);

// ===========================================================================
// Idle/social behaviour hook (heightmap + idle anim leaves).
// ===========================================================================
struct SocialHooks {
    // VIBE_Character_ResolveMesh(ch) + ComputeTargetTile + WorldToTileWithHeight:
    // resolve a meeting tile between `self` and `other`; fills col,row; returns
    // nonzero on success.
    int (*resolveMeetTile)(Character* self, Character* other, int* col, int* row);
    // Attach the stand ("stehen/stehen_newnoise") or sit ("sitzend/sit_newnoise")
    // idle animation when there is no neighbour and the idle-anim flag is pending.
    // `sit` selects the sit variant; returns the attached anim handle (or null).
    void* (*attachIdleAnim)(Character* ch, bool sit);
};
void SetSocialHooks(const SocialHooks* hooks);
const SocialHooks& GetSocialHooks();

// gilde.exe 0x405148 (idle branch) — VIBE_Character_UpdateIdleSocial. The `else`
// of the +296 (has-action) gate in Character_Update, factored out: when the actor
// is in the active scene, not dirty/hidden/sitting, and has no current target
// (+292 == 0): scan for a neighbour within 20.0; if one is found build a talk
// (type 45) action toward the resolved meeting tile; otherwise clear the dirty-
// mesh flag (+140 &= ~8). The stand/sit idle-anim attach (the +141 0x10 path) is
// applied via attachIdleAnim. Returns 1 if a talk action was spawned, else 0.
int UpdateIdleSocial(Character* ch);

} // namespace guild::sim
