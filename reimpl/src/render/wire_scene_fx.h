#pragma once
// =============================================================================
// guild::render — DE-INERT the PURE-LOGIC scene/fx leaf hooks (rule 13 wiring).
//
// Several reconstructed scene/fx clusters surface their coupled leaves as
// globally-installable hook seams whose inert defaults (null / identity) leave
// the genuine reconstructed callee disconnected at runtime. Nothing in src/
// installs the *pure-logic* (non-GPU) ones — they stay inert. This module binds
// exactly those: the reconstructed math / transform / binary-IO leaves that are
// already faithful 1:1 reconstructions elsewhere in the tree.
//
// WHAT IS BOUND (pure-logic, reconstructed)
// ---------------------------------------------------------------------------
//   fxrecon (render/fxrecon_particle_mirror_shadow.h):
//     SetBuildBasisHook          -> guild::util::BuildBasisFromAngle   0x5ca544
//     SetMatrixToEulerHook       -> guild::util::MatrixToEuler         0x5cb2cc
//     SetSetWorldTranslationHook -> guild::sim::ObjectSetWorldTranslation 0x5af50c
//   modelio (render/modelio_recon.h):
//     SetModelIoBioHooks.readByte  -> guild::io::BioReadByte           0x5dc850
//     SetModelIoBioHooks.readDword -> guild::io::BioReadDword          0x5dc894
//   objlist (render/render_recon_objlist.h):
//     ObjListHooks().floorReloadTextures -> guild::render::ReloadTextures 0x5bd2d8
//        — the reconstructed SetGammaTable (0x5b9ef4) -> Floor_ReloadTextures
//          (0x5bd2d8) call edge. Pointer-as-handle adapter; inert FloorTileAccess
//          under headless (no live floor records), faithful to a no-floor gamma
//          change. A real floor + BMP backend supplies the live accessors.
//
// WHAT STAYS INERT (rule 3 GPU leaves / rule 4-6 platform leaves / unverified)
// ---------------------------------------------------------------------------
//   fxrecon:   SetSpawnEffectHook 0x42bd6c (scene-node alloc + GPU draw queue;
//                                  prototype arg-shape differs, unverified adapter),
//              SetCastFromLightHook 0x5f3f98 (mesh / GPU upload / cache slots),
//              SetAllocHook/SetFreeHook (memory-debug allocator, rule 6).
//   modelio:   SetResourceFreeHook (allocator free, rule 6),
//              SetResourceFreeEntryHooks (file/stream platform I/O, rule 4).
//   fx_recon3 RenderHooks (freeObjectNode/computeBoneWorld/coordConvertX/
//              findGroupMember): all GPU / scene-graph leaves (rule 3) — and the
//              struct is per-call parameter, no global setter — stays inert.
//   scene_recon_octree OctreeBuildHooks (computeNodeBounds = VIBE_Mesh_
//              ComputeBoundingBox 0x5c9e64 GPU/asset; nodeMeshAABB drawData
//              accessor) and scene_recon5 OctreeRebuildHooks: parameter structs,
//              no global install seam — left to their per-call inert defaults.
//
// This module only WRITES through the public Set* setters. It does not edit any
// existing .cpp, the hook-table modules, or app/wiring.cpp.
// =============================================================================
namespace guild::render {

// Install the pure-logic scene/fx leaf bindings. Idempotent.
void InstallRealSceneFxWiring();

} // namespace guild::render
