#pragma once
// character_render3 — a third cluster of Character render/anim-coupled leaves from
// gilde.exe: the animation-attach / preload family (body, movement, motion, morph,
// low-poly, and item-to-bone attaches), the footstep / noise-timer audio leaves, and
// the camera ray-projection helpers. These are 1:1 ports. The animation, path,
// renderer, sound and object calls they make live in OTHER (largely unreconstructed)
// modules, so they are routed through an installable CharRender3Hooks dispatch table
// with inert default implementations (mirrors the CharRenderHooks / CharRender2Hooks
// pattern in character_render.cpp / character_render2.cpp). The pure control flow,
// path-string formatting, table arithmetic, and ray math are golden-testable against a
// recording mock. Functions that are pure leaves (StrCmpNoCase, VectorNormalize,
// VectorWithinTolerance, MatrixCopy) delegate to their already-reconstructed homes in
// guild::util — never redefined here.
//
// Translated functions (this TU):
//   VIBE_Character_AttachItemToBone     0x4068c0  (attach an item node to a hand/head bone)
//   VIBE_Character_ResolveHeadBone      0x57c5d4  (resolve the head texture-bone index)
//   VIBE_Character_AttachAni            0x404038  (build+attach a body .baf animation)
//   VIBE_Character_AttachMotion         0x4032f8  (build+attach a motion .baf animation)
//   VIBE_Character_AttachMovementAni    0x4034c4  (attach the looping movement animation)
//   VIBE_Character_DetachMorphAni       0x4035d0  (tear down the morph animation channel)
//   VIBE_Character_PreloadAniSet        0x403c34  (varargs: preload a set of body anims)
//   VIBE_Character_PreloadAniSetByName  0x403f14  (preload a set by explicit base name)
//   VIBE_Character_PreloadLowPolyAniSet 0x403da0  (preload the low-poly anim set)
//   VIBE_Character_PlayFootstepSound    0x40905c  (pick + play the terrain footstep sample)
//   VIBE_Character_NoiseTimerUpdate     0x4049ec  (step the noise-target pivot toward a goal)
//   VIBE_Character_ProjectRayDirection  0x426764  (project a screen-space ray direction)
//   VIBE_Character_ScreenToWorldRay     0x426850  (unproject a screen point to a world ray)
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants.
//   dbl_6103AC == -15.0  : sitting Y-offset bias (shared with ComputeAttachOffset).
//   dbl_6103B4 ==   0.4  : NoiseTimerUpdate per-tick step length.
//   1078530011  -> 3.14159274f (pi) : AttachItemToBone / wimpel +pi yaw.
//   Hand/head bone names: "d3_LeftHand"/"d3_RightHand"/"d3_Head".
//   Footstep sample basenames: "normal_s"/"Normal_s" + terrain suffixes.
// ===========================================================================
constexpr float kNoiseStep   = 0.4f;     // dbl_6103B4
constexpr float kSitOffsetY  = -15.0f;   // dbl_6103AC

// AttachItemToBone slot -> hand/head bone selection (a2 in the original).
//   1 -> a1+26 slot, "d3_LeftHand"
//   2 -> a1+25 slot, "d3_RightHand"   (NB: slot 2 maps to the +25 word, per the switch)
//   3 -> a1+27 slot, "d3_Head"
enum class ItemBone { kNone = 0, kLeftHand = 1, kRightHand = 2, kHead = 3 };

// PlayFootstepSound terrain-type -> sample base (a1[5]+112 frame-event terrain code).
//   3 -> "Erde_s", 4 -> "Wiese_s", 8 -> "Stein_s", 10 -> "Pfuetze_s",
//   6/11 -> "Stein_s" (stone), others -> "Normal_s".
const char* FootstepSampleForTerrain(int terrain);

// ===========================================================================
// CharActor3 — the live actor record as this cluster touches it. Only the referenced
// offsets are modeled (full record lives elsewhere; mirrors RenderActor / CharActor2).
// Pointer fields are native here; the originals store 32-bit handles.
// ===========================================================================
struct CharActor3 {
    char  baseName[256];   // a1+304 : the actor's model base-name (path component)
    void* bodyMesh;        // a1+52  : the renderable body object (mesh+492 -> anim base)
    void* lowPolyObj;      // a1+492 : low-poly proxy object (PreloadLowPolyAniSet)
    void* universe;        // a1+136 : owning universe (IndexFromPointer arg)
    u8    sitting;         // a1+140 & 0x10 (unused here; documented for parity)
    bool  isLocalUniverse; // IndexFromPointer(universe) != 0
    // AttachMovementAni / AttachAni / AttachMotion bookkeeping:
    void* moveAnim;        // a1+128 : current movement-anim handle (0 == free)
    void* attachAnim;      // a1+112 : last AttachAni handle
    u8    attachFlags;     // a1+132/133 : direction/seq byte set by the attach
    // AttachItemToBone slots (a1+25/26/27 are dword item-node handles):
    void* itemLeft;        // a1+26
    void* itemRight;       // a1+25
    void* itemHeadNode;    // a1+27
};

// ===========================================================================
// Cross-module dispatch hooks (anim / path / renderer / sound / object). Each call the
// originals make is a slot here; the default table is inert so the control flow / path
// formatting / table arithmetic is testable. Tests install a recording mock.
// ===========================================================================
struct CharRender3Hooks {
    // VIBE_Path_ConvertBackslashToSlash(s) @0x44eb54 — normalize a path in place.
    void  (*convertBackslashToSlash)(char* s);
    // VIBE_Anim_FindFreeMeshSlot() @0x5cf114 — returns a cached anim handle or 0.
    void* (*findFreeMeshSlot)();
    // VIBE_Light_SetGrayColorThunk(a,b) @0x5c6af0 — renderer state push.
    void  (*setGrayColorThunk)(int a, int b);
    // VIBE_Anim_LoadStreamToStock(path, flag) @0x5d3858 — load a .baf, returns handle.
    void* (*loadStreamToStock)(const char* path, int flag);
    // VIBE_Anim_AttachToBone(animBase, mask) @0x5d0b64 — attach, returns the channel.
    void* (*attachToBone)(void* animBase, int mask);
    // VIBE_Anim_PruneExpiredAttachments(animBase) @0x5d0d38.
    void  (*pruneExpiredAttachments)(void* animBase);
    // VIBE_Anim_CreateMorphAnim(...) @0x5cf150 — build a morph clip; returns a handle.
    void* (*createMorphAnim)(void* meshGeom, const char* name);
    // VIBE_Anim_ComputeBoneDelta(...) @0x5cba40.
    void  (*computeBoneDelta)(void* bodyMesh, void* morph);
    // VIBE_Object_AttachToUniverseNode(parentMesh, mat, name, off) @0x5b3e30 -> node.
    void* (*attachToUniverseNode)(void* parentMesh, const char* name);
    // VIBE_Object_DetachAndRelease(node) @0x5b4258.
    void  (*detachAndRelease)(void* node);
    // VIBE_Object_AttachToBone(node, boneName) @0x5b2b70.
    void  (*objectAttachToBone)(void* node, const char* boneName);
    // VIBE_Light_BuildObjectCache(node) @0x5c8218.
    void  (*buildLightCache)(void* node);
    // VIBE_Universe_SwitchActiveSlot(slot,...) @0x5b4a24 — returns prior slot.
    int   (*switchUniverse)(int slot);
    // VIBE_Object_SetPivotVector(obj, vec[3]) @0x5af490.
    void  (*setPivotVector)(void* obj, const float vec[3]);
    // VIBE_Object_SetPosition(obj, vec[3]) @0x5af38c.
    void  (*setObjectPosition)(void* obj, const float vec[3]);
    // VIBE_Sound_PlaySample(actorMesh, name) @0x4461d0 — returns a non-zero handle on play.
    float (*soundPlaySample)(void* actorMesh, const char* name);
    // VIBE_Sound3d_PlayOneShot(handle, obj, vol, radius) @0x424cd0.
    void  (*sound3dPlayOneShot)(float handle, void* obj, int vol, float radius);
    // VIBE_Script_ReportError(msg) — script-side error path.
    void  (*reportError)(const char* msg);
};
void SetCharRender3Hooks(const CharRender3Hooks* hooks);
const CharRender3Hooks& GetCharRender3Hooks();

// ===========================================================================
// Path / name formatting — the .baf path the attach family builds. Reproduces the two
// VIBE_Crt_Sprintf_0 calls ("character/%s/%s_%s.baf" and "%s_%s") + the slash fixup.
// `out` must hold >= 256 bytes; `name` is the per-clip sub-name, `base` the model base.
// ===========================================================================
void BuildAniPath(char out[256], const char* base, const char* name);
// Low-poly variant: "lowpolycharacter/%s/%s_%s_LOW.baf".
void BuildLowPolyAniPath(char out[256], const char* base, const char* name);
// Returns true if `name` is one of the gait clips that load with the "loop" flag set
// ("bewegung/gehen" or "bewegung/karren_ziehen").
bool IsLoopingGait(const char* name);

// ===========================================================================
// Animation-attach family.
// ===========================================================================

// ===========================================================================
// AttachItemToBone / ResolveHeadBone.
// ===========================================================================

// gilde.exe 0x4068c0 — VIBE_Character_AttachItemToBone. Detaches any existing item node
// in the slot, then (when `name` is set) attaches a fresh universe node under the body
// mesh, fixes up its renderer flags, builds its light cache, and attaches it to the
// matching bone ("d3_LeftHand"/"d3_RightHand"/"d3_Head"). The whole sequence is bracketed
// by a universe-slot switch (restored on every exit). `slot == kNone` detaches only.
void AttachItemToBone(CharActor3* a, ItemBone slot, const char* name);

// gilde.exe 0x57c5d4 — VIBE_Character_ResolveHeadBone. The original branches on whether
// the actor's attached record (a1+388) is a real scene actor or an office-staff dummy.
//   * Scan branch: first present head bone whose name is "kopf"/"abt_kopf" -> index+1468.
//   * Staff branch: staffBones[recordId % count] + 1468 (count = valid bone entries).
// Both write the result to a1[99]; we return it (0 == not resolved).
int ResolveHeadBoneFromScan(const char* const* boneNames, int boneCount);
int ResolveHeadBoneFromStaff(const unsigned char staffBones[4], int recordId);

// gilde.exe 0x404038 — VIBE_Character_AttachAni. Formats the body-anim .baf path,
// finds/loads the clip (loop flag set for the gait clips), attaches it to the body
// mesh's anim base (bodyMesh+492+244) with mask 0x20000 | ((8*(seq&1))|0xD0)<<8, and
// records the result at a1+112 (and a1+133 = -1). The channel's +60 owner is set from
// dword_62D090 when the actor is local, else -1. Returns the channel (0 on failure).
void* AttachAni(CharActor3* a, const char* name, int seq);

// gilde.exe 0x4032f8 — VIBE_Character_AttachMotion. Like AttachAni but the clip name is
// the (19-byte-strided) motion-table entry at dword_66FCD0[19*idx+1]+1; mask 153600
// (0x25800); always loads with flag 1; sets the channel +60 owner. The motion clip name
// is supplied by the caller (resolved from the table). Returns the channel.
void* AttachMotion(CharActor3* a, const char* motionName);

// gilde.exe 0x4034c4 — VIBE_Character_AttachMovementAni. No-op (returns 0) when a
// movement anim is already bound (a1+128 != 0). Otherwise formats the path, finds/loads
// the clip (loop flag for gait clips), stores it at a1+128, sets a1+132 = dir, bumps the
// clip refcount (+332), and returns the handle (0 on failure).
void* AttachMovementAni(CharActor3* a, const char* name, u8 dir);

// gilde.exe 0x4035d0 — VIBE_Character_DetachMorphAni. When a morph source is bound
// (result[28] != 0) and no free slot is cached: builds the morph clip, attaches it to
// the body anim base with mask 0x20000 | (24<<8), records it at result[31] (+60 owner
// from dword_62D08C / -1), optionally computes the bone delta, prunes the anim base,
// and clears result[28]. We pass the resolved morph-source presence as a flag.
struct MorphCtx {
    bool  hasMorphSource;   // result[28] != 0
    void* bodyMesh;         // result[13]  (its +460 -> geom, +492 -> anim base)
    void* meshGeom;         // *(bodyMesh+460)+16  (CreateMorphAnim arg); 0 => skip create
    bool  computeDelta;     // (*(morph+104)+361)==0 && (morph+109)>>6 != 0
    bool  isLocalUniverse;  // IndexFromPointer(result+34) != 0
};
void DetachMorphAni(const MorphCtx& c);

// ===========================================================================
// Preload family — load (but do not necessarily keep attached) a set of anim clips.
// The originals take a vararg / array of (char*) clip names; here we accept an
// explicit (names,count) span. `base` is the model base name.
// ===========================================================================

// gilde.exe 0x403c34 — VIBE_Character_PreloadAniSet. For each non-empty clip name: build
// the path, and (when no free slot is cached) load it (loop flag for gait clips), push
// renderer gray color, attach with mask 153600, and prune. Returns nothing.
void PreloadAniSet(CharActor3* a, const char* const* names, int count);

// gilde.exe 0x403f14 — VIBE_Character_PreloadAniSetByName. As PreloadAniSet but always
// loads with flag 0 (never the loop flag) and uses an explicit base name + MEMORY[0x34]
// (the wild/global actor) anim base.
void PreloadAniSetByName(const char* base, const char* const* names, int count);

// gilde.exe 0x403da0 — VIBE_Character_PreloadLowPolyAniSet. As PreloadAniSet but the
// path is the low-poly form, the mask is 133120 (0x20800), the clip always loads with
// flag 1, and the anim base is the low-poly object's (a1+492 ->+492+244).
void PreloadLowPolyAniSet(CharActor3* a, const char* const* names, int count);

// ===========================================================================
// Audio leaves.
// ===========================================================================

// gilde.exe 0x40905c — VIBE_Character_PlayFootstepSound. On a frame footstep event
// (a1[5]+112 frame-anim present, global sound enabled, actor in the active universe,
// and the current event code is one of {4,12,18,24} and differs from the last played),
// picks the terrain-suffixed sample (when the actor is local, the terrain noise is
// enabled, and the active-sound count < 3), plays it, and bumps the active count. The
// terrain code + gating are supplied by the caller; returns the new last-event byte.
struct FootstepCtx {
    bool  hasFrameEvent;     // *(a1[5]+112) != 0
    bool  soundEnabled;      // dword_62D074
    bool  inActiveUniverse;  // off_649D64==*(a1[5]+136) && dword_62D080==*(a1[5]+44)
    int   eventCode;         // **(a1[5]+112)
    u8    lastEvent;         // a1[256] (the previously-played event byte)
    bool  isLocalUniverse;   // IndexFromPointer(*(a1[5]+136))
    bool  terrainNoiseOn;    // *(a1[5]+4) & 1
    int   activeSoundCount;  // dword_62D078 (< 3 to play)
    bool  hasTerrainKind;    // *(*(a1[5]+136)+172) != 0 (else plain "Normal_s")
    int   terrainKind;       // selects the suffixed sample
    void* actorMesh;         // a1[5]
    void* soundObj;          // *(a1[5]+52)
};
// Returns the (possibly updated) last-event byte and, via *played, whether a sample
// was actually started (so the caller can bump dword_62D078).
u8 PlayFootstepSound(const FootstepCtx& c, bool* played);

// gilde.exe 0x4049ec — VIBE_Character_NoiseTimerUpdate. When the actor's noise timer
// (+272) is active: steps the noise-source pivot 0.4 units toward the body's bone-19
// world position, sets the pivot + position, decrements the timer, and — if the new
// position is within 0.4 of the stored target (+276) — resets the pivot to {0,0,0},
// stores the goal, and re-places the object. The bone/world vectors are supplied by the
// caller (the engine reads them off the mesh bone array). Returns whether it reset.
struct NoiseTimerCtx {
    bool  active;            // *(a1+272) != 0
    float boneWorld[3];      // bone-19 world position (mesh[19]+mesh[30] etc.)
    float noisePos[3];       // a1[69..71] (the current noise-source position)
    float meshPivot[3];      // *(a1+52)+120..128 (pivot reference)
    float meshOrigin[3];     // *(a1+52)+76..84 (object origin)
    float target[3];         // a1+276 (the stored goal)
    u8    timer;             // a1+272
    void* obj;               // *(a1+52)
};
struct NoiseTimerResult {
    bool  stepped;           // the timer was active and a step was applied
    bool  reset;             // reached the target -> pivot/target reset
    float newPos[3];         // the world position pushed to the object
    u8    newTimer;          // a1+272 after the decrement
};
NoiseTimerResult NoiseTimerUpdate(const NoiseTimerCtx& c);

// ===========================================================================
// Camera ray helpers (pure math, no hooks). The camera matrix / position globals
// (dword_13FCD1C +396 view matrix, +76 eye position; the screen-plane constants) are
// runtime-resolved; the caller supplies them. These reproduce the matrix-multiply ray
// transforms exactly (column-major 4x4, the same convention as MatrixCopy).
// ===========================================================================

// gilde.exe 0x426764 — VIBE_Character_ProjectRayDirection. Builds a ray direction by
// scaling the fixed forward basis (dword_425DB0/B8 == {0,0,1}) by -dist, rotating it by
// the upper-left 3x3 of the camera matrix, and adding the eye position. `cameraMat` is
// the 16-float view matrix (its +396 source), `eye` the camera origin (+76..84).
void ProjectRayDirection(const float cameraMat[16], const float eye[3], float dist,
                         float outOrigin[3]);

// gilde.exe 0x426850 — VIBE_Character_ScreenToWorldRay. Unprojects a screen pixel
// (sx,sy) to a world-space ray direction: forms the normalized screen vector
// {sx-cx, -(sy-cy), focal}, rotates it through the camera matrix's 3x3, and writes the
// direction; also computes a depth-scaled position {dir * (zFar-zNear)/focal}. `cx/cy`
// are the screen center (flt_13FCD18 / flt_13FCD10), `focal` flt_13FCD0C.
void ScreenToWorldRay(const float cameraMat[16], float sx, float sy, float cx, float cy,
                      float focal, float zNear, float zFar,
                      float outDir[3], float outScaled[3]);

} // namespace guild::sim
