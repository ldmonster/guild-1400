#pragma once
// character_render2 — a second cluster of Character render/anim-coupled leaves from
// gilde.exe: the flag (Wimpel) attach/refresh/remove sub-system, the per-actor
// visibility-state apply path, scene-attach reconciliation, and the mesh/anim
// sub-mesh maintenance bridges. These are 1:1 ports. The renderer / scene-graph /
// universe-slot / object calls they make live in OTHER (largely unreconstructed)
// modules, so they are routed through an installable CharRender2Hooks dispatch table
// with inert default implementations (mirrors the CharRenderHooks pattern in
// character_render.cpp). The pure control flow / table arithmetic is golden-testable
// against a recording mock. Functions that are pure leaves (StrCmpNoCase,
// VectorWithinTolerance, VectorAngleBetween, MatrixFromEuler) delegate to their
// already-reconstructed homes (guild::util) — never redefined here.
//
// Translated functions (this TU):
//   VIBE_Character_ResolveMeshSelf        0x4014f8  (lazily build+cache the heightmap mesh)
//   VIBE_Character_TouchMeshFrames        0x40194c  (inflate the actor's 4 mesh handles)
//   VIBE_Character_ApplyVisibilityState   0x4019cc  (set position + light/transport refresh)
//   VIBE_Character_ShowWithScale          0x401a24  (bone-chain world place + visibility)
//   VIBE_Character_RunMeshCallback        0x42644c  (scene-graph walk -> reset-mesh thunk)
//   VIBE_Character_UpdateSubMeshes        0x42664c  (prune+dirty submeshes matching a name)
//   VIBE_Character_DrawSubMeshes          0x4266b0  (prune all present submeshes)
//   VIBE_Character_AttachFlag             0x4b5d98  (attach the sp_WIMPEL flag node)
//   VIBE_Character_ShowFlag               0x4b5e9c  (apply the flag texture-set on a node)
//   VIBE_Character_RefreshFlagAnimation   0x4b5ef8  (walk the actor's scene tree -> AttachFlag)
//   VIBE_Character_CollectFlagNodes       0x4b62c0  (scene-walk callback: gather sp_WIMPEL)
//   VIBE_Character_RemoveFlagNodes        0x4b62fc  (collect then detach/release flag nodes)
//   VIBE_Character_UpdateAllFlags         0x4b63c0  (per-person: remove + refresh flags)
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// CharActor2 — the live actor record as this render-leaf cluster touches it. Only
// the referenced byte offsets are modeled; the full 516+ byte record lives elsewhere
// (mirrors character_render.h's RenderActor). Pointer fields are native here; the
// originals store 32-bit handles.
//   +36   "last scene-attach tag" dword (AttachToScene writes it).
//   +39   city/scene id word (0xFFFF == none) — indexes the 536-byte person table.
//   +90   per-person flag byte (UpdateAllFlags: bit 0 == "flag already handled").
//   +97   scene-tree root pointer (the flag/sub-mesh walks start here).
//   +388  attached-actor record pointer (AttachToScene / head root).
// ===========================================================================
struct CharActor2 {
    int   sceneTag36;   // +36
    u16   cityId39;     // +39   (0xFFFF == none)
    u8    flagByte90;   // +90   bit0 == flag handled this pass
    void* sceneRoot97;  // +97   scene-tree root
    void* attached388;  // +388  attached actor record
};

// SceneNode — a node in the actor's scene tree as the flag/sub-mesh code sees it.
//   +52   mesh/object handle (the renderable). For the flag walk this is the node id.
//   +96   mesh sub-array base (mesh+492); 3 entries of 116 bytes; each entry +376 is
//         a name pointer used by UpdateSubMeshes.
//   +460  object "geometry inflated" flag dword (0 => needs InflateGeometry).
//   +496  scene "visited" guard dword (saved/cleared around the walk).
//   +520  light owner pointer.
//   +528  bit 0 == "walk-from-self" flag (else save/restore +496 guard).
//   +529  flag byte (cleared bit 1 by AttachFlag).
//   +530  flag byte (AttachFlag sets 0x44 from a 0xB3 mask).
//   +531  redraw byte.
//   +535  mode byte (AttachFlag/ShowFlag set 4/5).
// (We expose a minimal handle struct; the renderer details are hook-side.)
struct FlagNode {
    void* handle;       // generic node/mesh handle (passed to hooks)
};

// 536-byte person/scene record (word_12CE910) fields the flag code reads:
//   +2    type byte (5/6/7 == flag-bearing city kinds).
//   +84   flag texture-set index byte (biased by +1342 on load; AttachFlag subtracts
//         62, RefreshAllFlags subtracts 61 — we keep the raw byte and the bias here).
//   +1340 (dword at word index 21 *2 == byte 84? no) — see notes in the .cpp.
struct PersonRecord2 {
    u8  typeByte;       // +2
    u8  texIndex;       // +84
};

// ===========================================================================
// Cross-module dispatch hooks (renderer / scene-graph / object / universe). Each
// call the originals make is a slot here; the default table is inert so the control
// flow / table arithmetic is testable. Tests install a recording mock.
// ===========================================================================
struct CharRender2Hooks {
    // VIBE_Object_InflateGeometry(obj): make sure an object's geometry is resident.
    void (*inflateGeometry)(void* obj);
    // --- ResolveMeshSelf universe/heightmap leaves ------------------------------
    // VIBE_Heightmap_Create(terrainCtx, arg, 2) -> the terrain mesh handle.
    void* (*heightmapCreate)(void* arg);
    // VIBE_Map_BuildCollisionGrid(mesh).
    void (*buildCollisionGrid)(void* mesh);
    // VIBE_Universe_SwitchActiveSlot(slot): make a universe slot active. Returns the
    // previously-active slot id (so the caller can restore it).
    void (*switchUniverse)(int slot);
    // VIBE_Universe_InitLogAndInflate(flag) / DisplayLogAndCleanup(flag): the
    // save/restore log bracket around a universe switch.
    void (*initLogAndInflate)(u8 flag);
    void (*displayLogAndCleanup)(u8 flag);
    // VIBE_Object_SetPosition(obj, pos[3]).
    void (*setObjectPosition)(void* obj, const float pos[3]);
    // VIBE_Object_SetWorldTranslation(obj, xlat[3]).
    void (*setWorldTranslation)(void* obj, const float xlat[3]);
    // VIBE_Light_BuildObjectCache(obj).
    void (*buildLightCache)(void* obj);
    // VIBE_Transform_PointThroughBoneChain(mesh, in[3], out[3]).  (NOT reconstructed.)
    void (*pointThroughBoneChain)(const void* mesh, const float in[3], float out[3]);
    // VIBE_Transform_RotateVectorByHierarchy(mesh, in[3], out[3]). (NOT reconstructed.)
    void (*rotateVectorByHierarchy)(const void* mesh, const float in[3], float out[3]);
    // VIBE_Character_SetVisible(actor, visible).
    void (*setVisible)(void* actor, int visible);
    // VIBE_Object_SelectTextureSet(node, mat, mode, texIndex): apply a texture set.
    int  (*selectTextureSet)(void* node, int texIndex);
    // VIBE_Object_AttachToUniverseNode(parentMesh, mat[4], name) -> new node handle.
    void* (*attachToUniverseNode)(void* parentMesh, const char* name);
    // VIBE_Object_ApplyParentTransform(node, mat).
    void (*applyParentTransform)(void* node);
    // VIBE_Character_LoadObjectAnimation(node, name, mode).
    void (*loadObjectAnimation)(void* node, const char* name, int mode);
    // VIBE_SceneGraph_WalkAndInvoke(rootList, node, callback, limit, ctx): walk the
    // scene tree rooted at `root` and invoke `cb(nodeHandle, nodeName, ctx)` for each
    // node up to `limit`. The hook owns the tree + the per-node name resolution; the
    // callback decides per node (continue while it returns true). The originals pass
    // VIBE_Character_AttachFlag / ShowFlag / CollectFlagNodes as the callback; we keep
    // those as first-class functions and let the walk hook feed them names.
    void (*walkScene)(void* root, bool (*cb)(void* nodeHandle, const char* nodeName,
                                             void* ctx),
                      int limit, void* ctx);
    // VIBE_SceneGraph_RemoveMeshFromTree(node, tree).
    void (*removeMeshFromTree)(void* node);
    // VIBE_Object_DetachAndRelease(node).
    void (*detachAndRelease)(void* node);
    // VIBE_Anim_PruneExpiredAttachments(animBase).
    void (*pruneExpiredAttachments)(void* animBase);
    // VIBE_Object_PropagateDirtyFlag(obj, flag).
    void (*propagateDirty)(void* obj, int flag);
    // Person table lookup: returns the 536-byte record for a city/scene id, or null.
    const PersonRecord2* (*lookupPerson)(u16 cityId);
    // VIBE_Character_RunMeshCallback's scene walk: resets dword_62D4EC, walks the tree
    // running the reset-mesh thunk with `limit`, and leaves the resolved handle in the
    // accumulator. Returns that accumulator. (Stands in for the WalkAndInvoke +
    // ResetMeshThunk + dword_62D4EC trio.)
    int  (*runMeshWalk)(void* ctxActor, int limit);
    // Sub-mesh slot name: returns the name pointer at mesh+492 + slot*116 + 376, or
    // null when the slot is empty / the +492 base is absent. (UpdateSubMeshes /
    // DrawSubMeshes iterate the 3 slots; the engine reads the names out of the mesh
    // record — modeled here so the loop + StrCmpNoCase match are faithful.)
    const char* (*submeshName)(void* mesh, int slot);
};
void SetCharRender2Hooks(const CharRender2Hooks* hooks);
const CharRender2Hooks& GetCharRender2Hooks();

// ===========================================================================
// Recovered constants / table biases.
// ===========================================================================
constexpr u16 kNoCity        = 0xFFFF;   // +39 == 0xFFFF: no flag.
constexpr int kFlagTexBiasA  = 62;       // AttachFlag/ShowFlag: texByte - 62.
constexpr int kPersonStride  = 536;      // word_12CE910 stride.
// Flag-bearing city/person kinds (type byte == 5/6/7).
inline bool IsFlagKind(u8 t) { return t == 5 || t == 6 || t == 7; }

// ===========================================================================
// Mesh maintenance bridges.
// ===========================================================================

// gilde.exe 0x4014f8 — VIBE_Character_ResolveMeshSelf. Lazily builds and caches the
// actor's terrain/heightmap mesh:
//   if (actor+176 already set) return it;                       // cache hit
//   loadFlag = (actor == wildActor) ? byte_649DD0 : actor+982;
//   if (actor+981 == 0 && loadFlag) {                           // needs a universe switch
//       switchUniverse(IndexFromPointer(actor)); saved = loadFlag & 0xFD;
//       initLogAndInflate(saved); }
//   mesh = HeightmapCreate(g_terrainCtx, actor+180, 2);
//   actor+176 = mesh;
//   if (actor != emptyActor && actor == wildActor && mesh) buildCollisionGrid(mesh);
//   if (saved) { displayLogAndCleanup(saved); restoreUniverse(savedSlot); }
//   return mesh;
// The universe-slot juggling + log/inflate are routed through hooks; the cache,
// the load-flag selection (& 0xFD), and the build-collision-grid gate are the
// behaviour reproduced + tested.
struct ResolveMeshCtx {
    void* cachedMesh;     // actor+176 (0 == miss)
    bool  isWildActor;    // actor == off_649D64 (selects byte_649DD0 for loadFlag)
    bool  isEmptyActor;   // actor == byte_13ECEC8 (collision-grid gate excludes it)
    u8    wildLoadFlag;   // byte_649DD0 (used when isWildActor)
    u8    actorLoadFlag;  // actor+982   (used otherwise)
    u8    actorReady981;  // actor+981   (non-zero => no universe switch)
    void* heightmapArg;   // actor+180   (passed to HeightmapCreate)
};
struct ResolveMeshResult {
    void* mesh;            // the created/cached mesh (actor+176 after the call)
    bool  switchedUniverse;
    bool  builtCollisionGrid;
};
ResolveMeshResult ResolveMeshSelf(const ResolveMeshCtx& c);

// gilde.exe 0x40194c — VIBE_Character_TouchMeshFrames. Ensures the four mesh handles
// the actor carries (a1[13] body, a1[25] head/lowpoly, a1[73][0] attach, a1[123]
// extra) have their geometry inflated (InflateGeometry when +460 == 0). Null inner
// handles are skipped. We pass the four resolved handles + their "inflated" flags.
struct MeshHandles {
    void* body;       bool bodyInflated;       // a1[13],  +460
    void* head;       bool headInflated;       // a1[25],  +460
    void* attach;     bool attachInflated;     // a1[73][0], +460
    void* extra;      bool extraInflated;      // a1[123], +460
    bool  hasHead;    // a1[25] != 0
    bool  hasAttachPtr;   // a1[73] != 0
    bool  hasAttach;      // *a1[73] != 0
    bool  hasExtra;   // a1[123] != 0
};
void TouchMeshFrames(const MeshHandles& m);

// gilde.exe 0x42644c — VIBE_Character_RunMeshCallback. Clears the pending-mesh accum
// (dword_62D4EC = 0), walks the scene with limit 480 (when a2 & 2) else 96, invoking
// the reset-mesh thunk per node, and returns the accumulated handle. Modeled as: the
// walk hook drives the thunk; we return the accumulator the hook leaves.
int RunMeshCallback(void* ctxActor, u8 modeFlags);

// gilde.exe 0x42664c — VIBE_Character_UpdateSubMeshes. For each of the 3 sub-mesh
// slots (stride 116 starting at mesh+492+376), if present AND its name matches `name`
// (case-insensitive, StrCmpNoCase == 0), prune the anim attachments (mesh+492+244)
// and propagate the dirty flag. Returns 1.
int UpdateSubMeshes(void* mesh, const char* name);

// gilde.exe 0x4266b0 — VIBE_Character_DrawSubMeshes. For each of the 3 sub-mesh slots
// that is present (name pointer != 0), prune the anim attachments. Returns 1.
int DrawSubMeshes(void* mesh);

// ===========================================================================
// Visibility / scene placement.
// ===========================================================================

// gilde.exe 0x4019cc — VIBE_Character_ApplyVisibilityState. Sets the body mesh
// position, optionally rebuilds the light cache (when `refreshLight`), refreshes the
// terrain type, then (if a transport is attached) updates the transport attach and
// its light cache, and (if a low-poly mesh exists) updates it. The terrain/transport/
// low-poly calls are themselves Character functions; here we model the renderer-side
// effects through hooks + caller-supplied predicates.
struct VisibilityCtx {
    void* bodyMesh;      // *(a1+52)
    bool  hasTransport;  // *(a1+292) != 0
    void* transportMesh; // **(a1+292)  (light cache target)
    bool  hasLowPoly;    // *(a1+492) != 0
};
void ApplyVisibilityState(const VisibilityCtx& v, const float pos[3], bool refreshLight);

// gilde.exe 0x401a24 — VIBE_Character_ShowWithScale. When `mesh` is present:
// transforms the local placement vector through the bone chain (-> world pos),
// rotates a reference axis through the hierarchy and measures the angle to it
// (-> world rotation Y), sets the body mesh world translation from the rotation,
// then applies the full visibility state at the new position (refreshLight = true).
// Returns whether the mesh was present.
struct ShowScaleCtx {
    void*  mesh;          // a1 (== result); 0 => no-op
    void*  bodyMesh;      // *(actor+52)
    VisibilityCtx vis;    // forwarded to ApplyVisibilityState
};
// `place` is the local placement vector (a2); `placeRot` the rotation-source vector
// (a2+19, i.e. the 20th float onward — the actor's facing basis).
bool ShowWithScale(const ShowScaleCtx& s, const float place[3], const float placeRot[3]);

// NB: VIBE_Character_AttachToScene (0x49cd10) is already translated in
// command_unit_orders.cpp as CharacterAttachToScene — not duplicated here (ODR).

// ===========================================================================
// Flag (sp_WIMPEL) attach / refresh / remove sub-system.
// ===========================================================================

// gilde.exe 0x4b5d98 — VIBE_Character_AttachFlag. Per-scene-node callback. If the
// node name != "dummy_FAHNE" it is a no-op (returns 1). Otherwise: transform the
// node's local mount (node+76) through the bone chain, attach an sp_WIMPEL child to
// the universe node, build a +pi-yaw rotation matrix and apply it, then (if the
// owner's city id is valid) select the flag texture-set (table[+84]-62) and set the
// new node's mode (+535=4), load the wimpel anim, and fix up its flag bytes.
// `nodeName` is the candidate node's name; `cityId` the owning actor's +39.
int AttachFlag(const char* nodeName, u16 cityId);

// gilde.exe 0x4b5e9c — VIBE_Character_ShowFlag. Per-scene-node callback. If the node
// name != "sp_WIMPEL" OR the owner has no city id (+39 == 0xFFFF) it is a no-op
// (returns 1). Otherwise applies the flag texture-set (table[+84]-62) to the node.
int ShowFlag(void* node, const char* nodeName, u16 cityId);

// gilde.exe 0x4b5ef8 — VIBE_Character_RefreshFlagAnimation. If the actor has a scene
// root (+97) and a valid city id (+39) whose person record type is a flag kind
// (5/6/7): walk the scene tree invoking AttachFlag. When the root's +528 bit 0 is
// clear, the +496 visited-guard is saved/zeroed around the walk and restored after.
void RefreshFlagAnimation(CharActor2* actor);

// gilde.exe 0x4b62c0 — VIBE_Character_CollectFlagNodes. Per-scene-node callback used
// by RemoveFlagNodes. If the node name == "sp_WIMPEL", append the node handle to the
// out list (out[0] is the count). Returns whether the count is still < 32 (continue).
struct FlagNodeList {
    int   count;          // out[0]
    void* nodes[32];      // out[1..]
};
bool CollectFlagNodes(void* nodeHandle, const char* nodeName, FlagNodeList* out);

// gilde.exe 0x4b62fc — VIBE_Character_RemoveFlagNodes. If the actor has a scene root
// (+97): walk the tree collecting sp_WIMPEL nodes (with the same +496 guard save/
// restore as RefreshFlagAnimation), then for each collected node remove it from the
// active tree (when dword_634488 is set) and detach/release it.
void RemoveFlagNodes(CharActor2* actor);

// gilde.exe 0x4b63c0 — VIBE_Character_UpdateAllFlags. Iterates the live persons; for
// each with a valid city id whose record is a flag kind (5/6/7) and whose +90 bit 0
// is clear, calls RemoveFlagNodes then RefreshFlagAnimation. The person iteration is
// supplied by the caller as a span (the engine's VIBE_Person_QueryBegin loop).
void UpdateAllFlags(CharActor2* const* persons, int count);

} // namespace guild::sim
