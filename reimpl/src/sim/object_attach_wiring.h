#pragma once
// =============================================================================
// object_attach_wiring — connect the character FACTORY's inert attach hook to the
// GENUINE reconstruction of VIBE_Object_AttachToUniverseNode @0x5b3e30.
//
// gilde.exe 0x5b3e30 — VIBE_Object_AttachToUniverseNode is ALREADY reconstructed
// 1:1 in sim/object_lifecycle10.{h,cpp} as guild::sim::ObjectAttachToUniverseNode
// (the universe scene-node factory: VIBE_Object_Spawn(4,name) -> SetParent ->
// Mesh_FindStockObject / Mesh_LoadOrFindByName -> Mesh_AttachStockObjectLods ->
// the nested LOD x texture upload loop -> SetPosition/SetWorldTranslation ->
// root-only LinkIntoScene; mesh-load failure -> Dispose -> null). It is NOT
// re-defined here (ODR) — this module is GLUE only.
//
// The factory body VIBE_Character_CreateMesh @0x4029c4 calls it as
//   *(rec+52) = VIBE_Object_AttachToUniverseNode(0, a3, model, &dword_401010, &model)
// i.e. parent=0, pos=a3 (the factory's `parentMat` edx arg), worldRot/xlate =
// dword_401010 (the {0,0,0,0} zero seed), lodArg=&model. CharacterFactoryHooks
// models this leaf as `void* attachToUniverseNode(void* parentMat, const char*
// model)` (parent is always 0; the factory passes its `parentMat` through edx as
// the `pos` vector). This installer binds that hook to the genuine function with
// the exact argument mapping above, so the live factory call tree
// (CreateFromModel -> CreateMesh -> attach -> ObjectAttachToUniverseNode) is wired
// end to end (rule 13). Tokens that need the universe leaves below the genuine
// function (Object_Spawn / Mesh_* / Texture_* / SetPosition / LinkIntoScene /
// Dispose) still bottom out in ObjLife10Hooks (inert by default); see the GAPS
// note in the .cpp.
// =============================================================================
namespace guild::sim {

// Install the real ObjectAttachToUniverseNode behind CharacterFactoryHooks
// .attachToUniverseNode (and the matching .destroy). Idempotent; composes with
// any previously installed factory-hook fields (only the attach/destroy slots are
// rebound, the rest keep their current values). Returns nothing; call once at the
// real-subsystem init point (alongside InstallRealSimHooks*).
void InstallRealObjectAttachWiring();

// Observers exposing the root-attach scene-list calls the live chain makes after
// InstallRealObjectAttachWiring() (the full intrusive-list splice is a live-universe
// gap; these count the genuine LinkIntoScene / SetParent / Dispose invocations so the
// chain's control flow is verifiable). Monotonically counted from install. For e2e
// assertions only.
int ObjectAttachWiringLinkIntoSceneCalls();
int ObjectAttachWiringSetParentCalls();
int ObjectAttachWiringDisposeCalls();

} // namespace guild::sim

// ---------------------------------------------------------------------------
// LIVE-SCENE splice access (rule 13). After InstallRealObjectAttachWiring() the
// objSetParent / objLinkIntoScene leaves splice spawned nodes into a REAL traversable
// render::SceneNode tree via the scene_link trio (render::LinkIntoScene / SetParent /
// LinkAsSibling). Each verbatim SceneNode10 the live chain threads gets a SHADOW
// render::SceneNode (carrying the +496/+500/+504/+508/+528/+533 link fields the walk
// reads — see the .cpp reconciliation note); the shadows form the tree.
//
// These accessors expose the built tree so a caller (the e2e, or the live universe
// render once it consumes shadow nodes) can walk it with render::WalkAndInvoke.
#include "render/scene_link.h"        // render::LiveScene / SceneNode
#include "sim/object_lifecycle10.h"   // guild::sim::SceneNode10

namespace guild::sim {

// The module's LiveScene (the engine's scene-list head / sentinel / currentCamera /
// activeUniverse globals). Its `sceneHead` is the head of the spliced root chain; feed
// it into a UniverseRoot.childHead and the env's listTerminator (== `sentinel`) to walk.
render::LiveScene& ObjectAttachWiringLiveScene();

// The shadow render::SceneNode for a spawned SceneNode10 (null if none was created —
// a node only gets a shadow when it is spliced via SetParent/LinkIntoScene). Created
// lazily by the splice leaves; `nodeType` mirrors the SceneNode10's +533.
render::SceneNode* ObjectAttachWiringShadowFor(SceneNode10* node);

// Reset the live-scene splice state (head/sentinel/current/shadow table). For tests.
void ObjectAttachWiringResetLiveScene();

} // namespace guild::sim
