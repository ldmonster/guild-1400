#pragma once
// Wires the building-LIFECYCLE bridge (ILifecycleHooks, building_lifecycle.h)
// to its real reconstructed cross-cluster leaves (rule 13 — connect finished
// leaves to the live call tree).
//
// SCOPE NOTE (why only one of this agent's five bridges produces wiring):
// This wiring agent owns five inert hook bridges — SpawnHooks/KillHooks
// (render/particle_spawn.h), SpawnSceneHooks (personnel_recruit2.h), MotionHooks
// (charaction_motion.h), WalkHooks (charaction_walk.h) and ILifecycleHooks
// (building_lifecycle.h). The first four are PURE platform-boundary bridges:
// every field is a GPU/heap/texture/scene-actor/anim/sound/native-engine leaf
// (rules 3-5 platform boundary — texture upload, heap alloc, scene-graph object
// create, attach-anim, draw-submeshes, footstep sound, terrain bone-chain query).
// None has a pure-logic reconstructed target, so nothing is created for them
// (per the "if a bridge has ZERO bindable fields, create nothing" rule).
//
// ILifecycleHooks has exactly TWO fields backed by faithful 1:1 reconstructions
// living in other clusters, which this installer binds:
//
//   ReleaseOccupantHoldings(slot)  -> Building3_ReleaseOccupantHoldings(rec,word)
//                                     (building3.cpp, gilde.exe 0x589468)
//   FreeChildList(slot)            -> GameObjectFreeChildList(&rec[+376])
//                                     (object.cpp,    gilde.exe 0x585aa4)
//
// The remaining ILifecycleHooks fields stay INERT (seed-from-defaults: the
// installed table inherits the module's inert ILifecycleHooks base and overrides
// ONLY the two bindable virtuals). Inert + reason (verified via IDA decompile):
//   ClearTradeRoutes        — inlined in RemoveAndCleanup (no standalone fn).
//   NotifyRivalEvent        — 0x536070 is a Text_RenderFormattedMessage +
//                             He_SendEntityMessage render/messaging leaf; the
//                             world:: recon is only the pure text-id core (4954),
//                             not the side-effecting send -> binding it would drop
//                             observable side effects (rule 8). Inert.
//   DestroyCharacter        — 0x402120 needs the global character table
//                             (dword_66F0D0) + scene/universe/morph teardown; only
//                             an inert stand-in exists. Inert.
//   DecrementTypeActiveCount— inlined in RemoveAndCleanup. Inert.
//   BuildingDistanceSq      — derives from Transform_PointThroughBoneChain
//                             (render/scene bone transform); no pure-logic recon.
//                             Inert (default 0 == co-located).
namespace guild::sim {

// Binds ILifecycleHooks' two reconstructed-backed leaves; leaves the rest inert.
// Idempotent; safe to call once at boot alongside the other InstallReal*Wiring().
void InstallRealSpawnMotionWiring();

}  // namespace guild::sim
