#pragma once
// ===========================================================================
// npcevent_reaper_full — the FULL (not math-only) reconstructions of the four
// Reaper ("Sensenmann" plague-NPC) event functions, over the native HeRecord
// model, so they can be WIRED to the live sim/npcevent_steps.h hooks
// (reaperApproach / reaperMove / reaperCachePose / reaperUpdateSound, each
// `int(HeRecord*)`).
//
// Provenance:
//   gilde.exe 0x4d8c34 — VIBE_NpcEvent_ReaperApproachTarget
//   gilde.exe 0x4d8f74 — VIBE_NpcEvent_ReaperMoveTowardTarget
//   gilde.exe 0x4d92a4 — VIBE_NpcEvent_ReaperCacheTargetPose
//   gilde.exe 0x4d9440 — VIBE_NpcEvent_ReaperUpdateSoundPos
//
// These take a `He` record in eax and the active-slot index in ecx (__usercall).
// Every original reads/writes He fields by raw byte offset and then dives into
// the engine scene-graph / transform / heightmap / sound clusters through the
// resolved entity records. This module translates the FULL bodies 1:1:
//
//   * The He-field reads/writes are byte-faithful off HeBytes(h):
//       +176 source-object id      (He_TargetObjId)
//       +180 target-object id      (He_ScanStep slot; the reaper's "victim")
//       +196 spawned flag (dword)  — 0 first spawn, 1 already attached
//       +200 reaper avatar node    — native pointer (LP64 side slot; see below)
//       +208/+212/+216 current world pos (x,y,z float)
//       +224/+228/+232 previous world pos (x,y,z float)
//       +236 last game tick (dword_62EB38 latch)
//   * The float math is delegated to the already-reconstructed recon4 kernels
//     in npcevent_recon4_reaper_move.{h,cpp} — REUSED, not duplicated.
//   * VIBE_Object_SetPosition @0x5af38c is REUSED from object_lifecycle3
//     (guild::sim::ObjectSetPosition) — not redefined.
//
// COUPLED LEAVES routed through ReaperFullHooks (inert defaults; the headless
// build links with no engine/Vulkan/SDL dependency). None of these are
// reconstructed elsewhere as plain callable functions — they live behind the
// existing per-module hook bridges — so the FULL bodies reach them here through
// one dedicated hook surface:
//   - VIBE_Universe_SwitchActiveSlot      0x5b4a24  (render-slot context switch)
//   - VIBE_GameObject_ResolveEntityById   0x583b44  (entity handle resolve)
//   - VIBE_Transform_PointThroughBoneChain 0x5c8b38 (bone-chain -> world point)
//   - VIBE_Heightmap_WorldToTileWithHeight 0x5c6644 (terrain height query)
//   - VIBE_Object_AttachToUniverseNode    0x5b3e30  (spawn reaper avatar node)
//   - VIBE_Character_LoadObjectAnimation  0x426488  (*sensenmann.baf)
//   - VIBE_Math_VectorAngleWrapped        0x5ca504  (orientation angle)
//   - VIBE_Transform_RotateVectorByHierarchy 0x5c8990
//   - VIBE_Sound3d_SetListenerFromVectors 0x4262a0  (rule 5 — SDL audio backend)
//   - VIBE_Command_GetGameSpeed           0x493e80  (via the recon4 hook)
//   - VIBE_Object_SetPosition             0x5af38c  (REUSED — object_lifecycle3)
//
// The hook surface is intentionally "position-shaped": the engine object reads
// the originals perform (resolve entity, gate on +97 sub-record / +460 mesh /
// +16 instance, then point a bone-chain) collapse into a single query that
// yields (a) whether the gate passed and (b) the resulting world point. This
// keeps the He-field state machine + the recon4 float math exactly 1:1 while the
// genuinely unreconstructed engine leaves stay behind a documented seam.
// ===========================================================================

#include "guild/common/types.h"
#include "sim/he.h"
#include "sim/npcevent_recon4_reaper_move.h"  // Vec3 + math kernels (REUSED)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Native-pointer side slot for He+200 (the reaper avatar node). The 32-bit
// original stores a 4-byte node pointer in the record; on LP64 a real engine
// node pointer is 8 bytes and would not fit, so — per the module's established
// LP64 reconciliation — the node handle lives in a dedicated native slot keyed
// by the record, while the He+196 spawned flag stays in the record itself.
// ---------------------------------------------------------------------------
void  ReaperFull_SetNode(HeRecord* h, void* node);
void* ReaperFull_GetNode(HeRecord* h);

// ---------------------------------------------------------------------------
// Coupled-leaf hooks. nullptr members install an inert default (gate fails /
// no-op / identity), so the He state machine + recon4 math are exercisable
// headless. Tests install captors to drive the gate and positions.
// ---------------------------------------------------------------------------
struct ReaperFullHooks {
    // dword_649D60 — the active scene slot the originals save into v3 at entry
    // and restore on every exit. Default 0.
    int (*getActiveSceneSlot)() = nullptr;

    // VIBE_Universe_SwitchActiveSlot(target, 1, anchor, restore). The reaper
    // funcs save dword_649D60 into v3, switch to slot 0 on entry, and restore v3
    // (target == saved slot) on every exit. Modeled so a test can assert the
    // enter (target 0) / leave (target == saved slot) bracket.
    void (*switchActiveSlot)(int target, int anchor, int restore) = nullptr;

    // Resolve the SOURCE object (He+176) and produce its bone-chain world point,
    // already height-biased by +flt_61EFE0 (80.0) on y. Returns false if the
    // original's gate chain (+97 sub-record, +460 mesh, +16 instance) fails.
    // `outPoint` is left untouched on failure.
    bool (*resolveSourcePoint)(HeRecord* h, i32 objectId, Vec3* outPoint) = nullptr;

    // Resolve the TARGET object (He+180) -> bone-chain world point. Two height
    // biases are used by the originals: +flt_61EFE0/EFE4 (move/approach) and
    // +flt_61F000/F004 (cache/sound) — all 80.0, so a single biased point is
    // faithful. Returns false if the gate chain fails.
    bool (*resolveTargetPoint)(HeRecord* h, i32 objectId, Vec3* outPoint) = nullptr;

    // The reaper avatar node's current bone-chain world point (a1+200 -> +76).
    // Returns false if there is no node (a1+200 == 0).
    bool (*resolveNodePoint)(HeRecord* h, Vec3* outPoint) = nullptr;

    // Terrain height at a world position (VIBE_Heightmap_WorldToTileWithHeight).
    // Returns the raw terrain height (the caller adds +flt_61EFE4 = 80.0).
    f32 (*terrainHeight)(const Vec3& worldPos) = nullptr;

    // The reaper node's "still arriving / blocked" flag at a1[200]+464. Nonzero
    // => the move step early-returns 1 (latch the tick, no movement this frame).
    int (*nodeMoveBlocked)(HeRecord* h) = nullptr;

    // VIBE_Object_AttachToUniverseNode(0, &spawnPos, "Sensenmann", &prevPose, slot)
    // -> the new node handle (0 == fail). On success the original copies the
    // node's +132..+140 (euler) into He+224..+232 and +76..+84 (pos) into
    // He+208..+216; we surface those two triples so the FULL body writes them.
    void* (*attachReaperNode)(HeRecord* h, const Vec3& spawnPos,
                              Vec3* outNodeEuler, Vec3* outNodePos) = nullptr;

    // VIBE_Character_LoadObjectAnimation(node, "*sensenmann.baf", 1).
    void (*loadReaperAnim)(void* node) = nullptr;

    // Read the reaper node's +132/+136/+140 (euler) and +76/+80/+84 (pos), used
    // by move/cache to refresh He+224..+232 (prev pose) and He+208..+216 (cur
    // pose) from the live node before writing the new position.
    void (*readNodePose)(HeRecord* h, Vec3* outEuler, Vec3* outPos) = nullptr;

    // VIBE_Object_SetPosition(node, &pos) — REUSED guild::sim::ObjectSetPosition.
    // Routed through the hook because the node here is an opaque engine handle.
    void (*setNodePosition)(HeRecord* h, const Vec3& pos) = nullptr;

    // VIBE_Sound3d_SetListenerFromVectors(node, listenerPos, 120, facing, 8).
    void (*sound3dSetListener)(HeRecord* h, const Vec3& listenerPos,
                               const Vec3& facing) = nullptr;
};

void SetReaperFullHooks(const ReaperFullHooks* hooks);
const ReaperFullHooks& GetReaperFullHooks();

// ===========================================================================
// The four FULL reaper event functions. Signature matches the
// sim/npcevent_steps.h hook type `int (*)(HeRecord*)`, so the integrator binds:
//     hooks.reaperApproach    = &guild::sim::ReaperApproachTarget;
//     hooks.reaperMove        = &guild::sim::ReaperMoveTowardTarget;
//     hooks.reaperCachePose   = &guild::sim::ReaperCacheTargetPose;
//     hooks.reaperUpdateSound = &guild::sim::ReaperUpdateSoundPos;
// ===========================================================================

// gilde.exe 0x4d8c34 — VIBE_NpcEvent_ReaperApproachTarget. Attach the reaper
// avatar at the source/target. Returns 1 on attach success, 0 on any gate fail.
int ReaperApproachTarget(HeRecord* h);

// gilde.exe 0x4d8f74 — VIBE_NpcEvent_ReaperMoveTowardTarget. Step the avatar
// toward the target. Returns 2 (arrived, within 45.0), 1 (moving / blocked), or
// 0 (gate fail / lost).
int ReaperMoveTowardTarget(HeRecord* h);

// gilde.exe 0x4d92a4 — VIBE_NpcEvent_ReaperCacheTargetPose. Cache the death
// pose / orientation. Returns 1 on success, 0 on gate fail.
int ReaperCacheTargetPose(HeRecord* h);

// gilde.exe 0x4d9440 — VIBE_NpcEvent_ReaperUpdateSoundPos. Update the 3D-sound
// listener for the plague effect. Returns 1 on success, 0 on gate fail.
int ReaperUpdateSoundPos(HeRecord* h);

} // namespace guild::sim
