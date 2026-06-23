#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — texraster_recon2: scene LIGHT orchestration cluster. These are
// the high-level light/daylight/torch refresh entry points; their heavy lifting
// is done by scene-graph traversal callbacks and object attach/detach helpers
// that live in OTHER modules. Reconstructed 1:1 from the Hex-Rays decompile:
//
//   0x42dc7c  VIBE_Light_CreateSunRays          (build the "sonnenstrahlen" obj)
//   0x5047c0  VIBE_Light_ApplyTorchEffects       (collect torch objs, reparent)
//   0x504860  VIBE_Light_RefreshTorchLighting    (traverse + rebuild octree)
//   0x504a00  VIBE_Light_EnableDaylight          (flag redraw + rebuild octree)
//   0x5c7d70  VIBE_Light_RegisterUpdateCallbacks (two TraverseTree passes)
//
// CROSS-MODULE / BOUNDARY: every scene-graph and object helper these call
// (VIBE_SceneGraph_TraverseTree, WalkAndInvoke, FreeNodeRecursive,
// BuildOctreeForRegion; VIBE_Object_FindByHandle, AttachToUniverseNode,
// ReparentWithTransform, DetachAndRelease; VIBE_Light_RefreshAllObjects,
// UpdateDayCycle, RefreshChildBrightness, UpdateFlickerIntensity; and the
// per-node callbacks VIBE_Character_FlagRedrawByMode, VIBE_Scene_CollectTorchObject)
// is a SEPARATE function in another cluster. They are routed through hooks so the
// orchestration control flow (traversal order, the octree free/rebuild guard,
// the exact flag-byte edits on the sun-rays object) is reconstructed exactly
// without redefining those callees (ODR-safe). The bit twiddling on the object
// record IS reconstructed verbatim.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// LightSceneGlobals — the orchestration globals (each carries its address).
// ---------------------------------------------------------------------------
struct LightSceneGlobals {
    int  octreeRoot   = 0;   // dword_634488  current octree root (0 == none)
    int  sunRaysObj   = 0;   // dword_62D564  the sun-rays object handle
    int  sunRaysFlag1 = 0;   // dword_62D568
    int  sunRaysStamp = 0;   // dword_62D56C  (= dword_62EB38 frame stamp)
    int  sunRaysFlag2 = 0;   // dword_62D570
    int  frameStamp   = 0;   // dword_62EB38
    int  universeRoot = 0;   // off_649D64    scene-graph root passed to traversals
    int  plichtObj76  = 0;   // *(FindByHandle("pLicht")+136) light parent handle
    int  sunPosX = 0, sunPosY = 0, sunPosZ = 0; // dword_13FCD1C +76/+80/+84
};

// ---------------------------------------------------------------------------
// Hooks for the cross-module callees (defaults are inert).
// ---------------------------------------------------------------------------
struct LightSceneHooks {
    // VIBE_SceneGraph_TraverseTree(root, 0, cb, mask) (0x5ac86c).
    void (*traverseTree)(int root, int a2, void (*cb)(), int mask) = nullptr;
    // VIBE_SceneGraph_WalkAndInvoke(root, a1, cb, a4, ctx) (0x5ac738). Returns char.
    char (*walkAndInvoke)(int root, void* a1, int (*cb)(), int a4, void* ctx) = nullptr;
    // VIBE_SceneGraph_FreeNodeRecursive(node) (0x5efe50).
    void (*freeNodeRecursive)(int node) = nullptr;
    // VIBE_SceneGraph_BuildOctreeForRegion(0,64,a3,8) (0x5f05b0). Returns root.
    int  (*buildOctreeForRegion)(int a1, int a2, unsigned a3, unsigned a4) = nullptr;
    // VIBE_Object_FindByHandle(0,8,"pLicht",0,a2) (0x5b7be4). Returns obj base ptr.
    int  (*objectFindByHandle)(int a1, int a2, const char* name, int a4, int a5) = nullptr;
    // VIBE_Object_AttachToUniverseNode(0, xyz, "sonnenstrahlen", desc) (0x5b3e30).
    int  (*objectAttachToUniverseNode)(int a1, const int* xyz, const char* name, const void* desc) = nullptr;
    // Resolve an object handle to its mutable record byte base (so the exact
    // flag-byte edits at +529..+536 can be reconstructed). Default: null.
    u8*  (*objectByteBase)(int handle) = nullptr;
    // Resolve the child-list head/next/type fields for ApplyTorchEffects. The
    // original reads them as *(node+508)/*(node+496)/*(node+533). Provided as a
    // resolver so the walk is reconstructed without owning the object layout.
    int  (*objChildHead)(int obj) = nullptr;   // *(obj+508)
    int  (*objChildNext)(int child) = nullptr; // *(child+496)
    i8   (*objChildType)(int child) = nullptr; // *(char*)(child+533)
    // VIBE_Object_ReparentWithTransform(child@eax, parent@edx) (0x5b7e54).
    void (*objectReparentWithTransform)(int child, int parent) = nullptr;
    // VIBE_Object_DetachAndRelease(obj) (0x5b4258). Returns char.
    char (*objectDetachAndRelease)(int obj) = nullptr;
    // VIBE_Light_UpdateDayCycle(obj) (0x42ddf8).
    void (*lightUpdateDayCycle)(int obj) = nullptr;
    // VIBE_Light_RefreshAllObjects(1) (0x5c886c). Returns u8.
    u8   (*lightRefreshAllObjects)(unsigned a1) = nullptr;
    // loc_5CB930 predicate: gate(child@eax, name@edx="rLicht"). Returns int.
    int  (*gatePredicate)(int child, const char* name) = nullptr;
    // Per-node callbacks (addresses noted; passed by pointer to traversal hook):
    void (*characterFlagRedrawByMode)() = nullptr;   // 0x5049d8
    void (*lightApplyTorchEffectsCb)() = nullptr;    // 0x5047c0 (used as a cb)
    void (*lightRefreshChildBrightness)() = nullptr; // 0x5c6b30
    void (*lightUpdateFlickerIntensity)() = nullptr; // 0x5c6be0
    int  (*sceneCollectTorchObject)() = nullptr;     // 0x504774
};

void SetLightSceneHooks(const LightSceneHooks& h);
const LightSceneHooks& GetLightSceneHooks();
LightSceneGlobals& LightScene();

// 0x42dc7c — VIBE_Light_CreateSunRays(a1@ecx, a2@edi).
// Looks up the "pLicht" parent (+136), attaches a "sonnenstrahlen" object at the
// sun XYZ (dword_13FCD1C +76/+80/+84), then sets the object's flag bytes:
//   +535 = 5; +536 = 1; +531 &= 0xFB; +530 |= 0x0C; +529 &= 0xFD;
// stores it in sunRaysObj, seeds the stamp from frameStamp, and runs the day
// cycle. Returns sunRaysObj.
int Light_CreateSunRays(int a1, int a2);

// 0x5047c0 — VIBE_Light_ApplyTorchEffects(a1@eax).
// Collects torch-bearing objects into a stack buffer (WalkAndInvoke +
// CollectTorchObject), then for each collected object walks its child list
// (+508 head, +496 next) and, for children with type byte (+533) >= 5, reparents
// them under a1 (slot index*4) when gatePredicate() passes. Detaches/releases
// each collected object afterward. Returns the last DetachAndRelease result.
char Light_ApplyTorchEffects(int a1);

// 0x504860 — VIBE_Light_RefreshTorchLighting.
// TraverseTree(root, ApplyTorchEffects, mask=192); if an octree root exists,
// free it and rebuild it; then RefreshAllObjects(1). Returns its u8 result.
u8 Light_RefreshTorchLighting();

// 0x504a00 — VIBE_Light_EnableDaylight.
// If an octree root exists, free it (and clear the slot). TraverseTree(root,
// CharacterFlagRedrawByMode, mask=64); rebuild the octree and store it. Returns
// the new octree root.
int Light_EnableDaylight();

// 0x5c7d70 — VIBE_Light_RegisterUpdateCallbacks.
// Two TraverseTree passes over the root: RefreshChildBrightness then
// UpdateFlickerIntensity (both mask=4). Returns the 2nd pass's result (char).
char Light_RegisterUpdateCallbacks();

} // namespace guild::render
