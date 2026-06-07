#pragma once
// =============================================================================
// guild::play — REAL RENDER / CHARACTER-MESH BRIDGE (PLAYABLE_PLAN P6).
//
// The "424 inert hooks" gap: many reconstructed leaves are NOT connected at
// runtime — they sit behind installable hook tables whose DEFAULT slots are inert
// (identity / zero / return-0), i.e. the engine's "subsystem not present this
// frame" no-op. This module de-inerts ONE cohesive cluster that lives on the real
// per-FRAME character-bridge path: the Character attach-offset / head-variant
// resolve leaves (sim::CharRenderHooks). Those feed
//
//   VIBE_Character_ComputeAttachOffset  0x404860  (attach slot -> world offset)
//   VIBE_Character_ApplyAttachOffset    0x404964  (seat the attached actor)
//   VIBE_Character_SetupAttachCamera    0x404998  (3D sound listener from attach)
//   VIBE_Character_ApplyHeadVariant     0x57c548  (resolve + apply a head texture set)
//
// which the in-game frame walk runs to place every attached actor/prop and select
// its mesh texture set. By DEFAULT three of the CharRenderHooks slots are inert:
//
//   pointThroughPivot   -> identity copy   (the geometry is left untransformed)
//   meshRootTranslation -> zero            (the mesh root offset is dropped)
//   selectTextureSet    -> return 0        (no texture set is ever applied)
//
// IDA grounding (decompiled 0x404860 / 0x57c548) shows the original wires those
// three points to REAL reconstructed leaves that already exist in src/:
//
//   pointThroughPivot   -> util::PointThroughBoneChainPivot   (0x5c8d0c)
//   meshRootTranslation -> read mesh+132/136/140 (float idx 33/34/35)  (0x4048d1)
//   selectTextureSet    -> sim::ObjectSelectTextureSet        (0x5b3f54)
//
// InstallRealRenderBridge() swaps those inert slots for the real leaves through
// sim::SetCharRenderHooks (a PUBLIC setter) — purely additive, touching no
// owned file. After install, ComputeAttachOffset/ApplyAttachOffset transform the
// attach geometry through the real bone-chain pivot + add the real mesh root
// translation, and ApplyHeadVariant actually drives the real texture-set selector.
//
// The other CharRenderHooks slots (sound listener, anim loop flags, action-queue
// unlink, StandUp, AttachItemToBone, script error) target OS/Miles/script-VM or
// owned modules with no standalone reconstructed math leaf to point at, so they
// stay inert; this is documented in the module report.
// =============================================================================
#include "guild/common/types.h"

namespace guild::play {

// Install the REAL render/character-mesh bridge: point the inert CharRenderHooks
// pivot / mesh-root-translation / texture-set-select slots at their real
// reconstructed leaves. Idempotent: the installed table is process-static. After
// this, the Character attach/head-variant leaves run the real geometry + texture
// resolve instead of the inert no-op.
void InstallRealRenderBridge();

// Restore the inert default CharRenderHooks (clears the bridge). Mainly for tests
// that want to observe the inert vs real behaviour difference within one process.
void UninstallRealRenderBridge();

// True while the real bridge is installed (the CharRenderHooks are the bridge's).
bool RealRenderBridgeInstalled();

// ---------------------------------------------------------------------------
// Bridge leaf accessors (exposed so tests can drive the exact same real leaves
// the installed hooks route to, without reaching across module boundaries).
// ---------------------------------------------------------------------------

// The real pivot leaf the bridge installs: transforms model-space point `in`
// through the mesh frame `mesh` (util::PointThroughBoneChainPivot @0x5c8d0c).
// `mesh` is the actor+52 mesh-frame base (a float[] laid out as the engine frame:
// pivot at idx 27..29 / 19..21, 3x3 at 99..109, parent link embedded at byte 504).
void BridgePointThroughPivot(void* mesh, const float in[3], float out[3]);

// The real mesh-root translation the bridge installs: out = mesh[33..35]
// (bytes 132/136/140), exactly the add the original 0x404860 performs.
void BridgeMeshRootTranslation(void* mesh, float out[3]);

} // namespace guild::play
