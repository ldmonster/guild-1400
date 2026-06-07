#pragma once
// ===========================================================================
// object_lifecycle9.{h,cpp} — VIBE_Object_* remaining leaves (batch 9).
// namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family after batches
// 1..8. This batch is a coherent grab-bag of the larger *deterministic* leaves
// that batches 1..8 explicitly DEFERRED (see object_lifecycle7.h's deferred list
// and object_lifecycle8.h's planned list):
//
//   * Clone 0x5b2478                — deep-copy a scene node: alloc a fresh 0x21C
//                                     block (+ a 0x1AC light block for kind>=5),
//                                     run InitStruct, copy the name + a fixed
//                                     run of float/int fields, the +488/+156/+528
//                                     fixed-size field runs, optionally clone the
//                                     draw block, then re-seat position / world
//                                     translation. Verbatim field offsets.
//   * MoveBetweenUniverses 0x5b51e0 — unlink a node from one universe's object
//                                     list and relink it at the head of another;
//                                     mirrors the active-universe scratch slots
//                                     (g_activeUniverse) exactly as the original.
//   * ParseNameAndBind 0x4ffb40     — parse a node's "prefix_type" name, look the
//                                     suffix up in the scene-type (65-byte rows)
//                                     or building-type (589-byte rows) table and
//                                     pack the +72 type word. Uses REAL util
//                                     siblings StrChrLast + StrCmpNoCase.
//   * RunScriptCallback 0x5b34e4    — a node-state-driven mesh-reload callback.
//   * UpdateBuildingVisualState 0x506b68 — building "under construction / built /
//                                     demolished" mip-filter + flag state machine.
//   * RebindParentMesh 0x5b4420     — dispose old child/draw data, reload + attach
//                                     a new mesh by name, rebuild the light cache.
//   * ToggleHiddenState 0x5b3698    — toggle a 'r'(=114) node between state 5/6.
//   * ToggleSuspendStateNamed 0x5b4274 — suspend/restore a node's render state,
//                                     re-copying its name when it is a '!'(=33).
//   * RebuildModelByOwner 0x5a8140  — walk the Gebaeude table (169-byte rows) and
//                                     rebuild the model for every record this node
//                                     owns.
//   * CollectMatchingProts 0x586508 — collect every prototype row (731 rows) whose
//                                     class byte matches this node's prototype.
//   * IsBuildingType 0x583a2c       — predicate: is this prototype a building kind?
//   * SpawnBomb 0x486648            — find a free bomb-pool slot and attach a bomb.
//
// Reuses real reconstructed siblings (NOT mocks):
//   * util::StrChrLast   (string_ops.cpp @0x5d3ef0 == VIBE_Util_StrChr) — ParseNameAndBind
//   * util::StrCmpNoCase (string_ops.cpp @0x5cb8f0)                     — ParseNameAndBind
//   * g_activeUniverse   (off_649D64, owned by character_query.cpp)     — MoveBetweenUniverses
//
// All unreconstructed cross-module leaves (Memory_Alloc/FreeDebug, InitStruct,
// AllocDrawData, SetPosition, SetWorldTranslation, Mesh_*, Light_*, Texture_*,
// Shadow_*, SceneGraph_*, Universe_*, Script_*, Render_*, the plain StrCmp, the
// BuildModelName table variant, AttachToUniverseNode) are routed through
// ObjLife9Hooks with INERT default implementations defined in THIS library .cpp.
// Tests install their own captor hooks; nothing in a test defines a symbol that
// src/ references.
//
// LP64 caveat: the original is 32-bit; "pointers" in the node record are 4-byte
// slots. We model node records as opaque byte blocks and access fields by raw
// offset, never as named C++ pointer members, so a real 64-bit heap pointer never
// gets truncated into a 4-byte slot. Public entry points that return the original
// eax "pointer" return void* so a real allocator pointer survives.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x583a2c  VIBE_Object_IsBuildingType
//   0x586508  VIBE_Object_CollectMatchingProts
//   0x5a8140  VIBE_Object_RebuildModelByOwner
//   0x5b3698  VIBE_Object_ToggleHiddenState
//   0x5b4274  VIBE_Object_ToggleSuspendStateNamed
//   0x5b4420  VIBE_Object_RebindParentMesh
//   0x486648  VIBE_Object_SpawnBomb
//   0x5b2478  VIBE_Object_Clone
//   0x5b51e0  VIBE_Object_MoveBetweenUniverses
//   0x4ffb40  VIBE_Object_ParseNameAndBind
//   0x506b68  VIBE_Object_UpdateBuildingVisualState
//   0x5b34e4  VIBE_Object_RunScriptCallback
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// SceneNode9 — flat byte view of the "d3:SpawnObject" scene node, ~0x21C bytes,
// for the fields this batch touches. Same opaque block as SceneNode6/7/8; named
// only where this batch reads/writes. Re-declared locally (no named pointer
// members — raw offsets only, LP64-safe). Offsets are from the original:
//   +0     (0)    nameByte[0]  first char of the name (also the prototype class
//                              when read as a byte: 114='r', 33='!', 7, 10, ...)
//   +64    (64)   color        (a1+64) packed RGBA, copied/written by several fns
//   +72    (72)   typeWord     (a1+72) packed type id (ParseNameAndBind target)
//   +76    (76)   pos[3]       world translation       (a1[19..21])
//   +97    (97)   smokeScript  (a1+97) chimney script back-ref
//   +104   (104)  scaleX       (a1+26) used by ComputeMarketValue/bounds
//   +128   (128)  worldScale   (a1[32] float)
//   +132   (132)  worldXlate[3]                          (a1[33..35])
//   +149   (149)  smokeHandle  (a1+149) running smoke-script handle (-1 = none)
//   +156   (156)  fieldRun240  240-byte field run cloned verbatim
//   +460   (460)  meshState    (a1+460) mesh-state block ptr
//   +488   (488)  fieldRun392  392-byte field run cloned verbatim (when set)
//   +492   (492)  drawData     (a1+492) alloc'd draw block ptr
//   +496   (496)  firstChild   (a1+124) child list head
//   +500   (500)  nextSibling  (a1+125)
//   +508   (508)  attachedSub  (a1+127) attached sub-object
//   +512   (512)  ownerTag     (a1+128)
//   +528   (528)  flags528      bit0/bit2; nibble cleared on copy
//   +529   (529)  flags529      bits 5/6 cleared on copy; bit2 set on bomb attach
//   +533   (533)  attachKind    (a1+533) 5/6 = hidden-toggle states; >=5 => light
//   +534   (534)  savedKind     (a1+534) suspend-state backup
// ===========================================================================
constexpr int kNodeSize = 0x21C;        // 540 bytes

// Field offsets (named once, used by both .cpp and tests).
namespace n9 {
constexpr int kColor       = 64;
constexpr int kTypeWord    = 72;
constexpr int kPos         = 76;
constexpr int kSmokeScript = 97;
constexpr int kWorldScale  = 128;
constexpr int kWorldXlate  = 132;
constexpr int kFieldRun156 = 156;   // 240-byte run
constexpr int kMeshState   = 460;
constexpr int kFieldRun488 = 488;   // 392-byte run
constexpr int kDrawData    = 492;
constexpr int kFirstChild  = 496;
constexpr int kNextSibling = 500;
constexpr int kAttachedSub = 508;
constexpr int kOwnerTag    = 512;
constexpr int kFlags528    = 528;
constexpr int kFlags529    = 529;
constexpr int kAttachKind  = 533;
constexpr int kSavedKind   = 534;
}  // namespace n9

struct SceneNode9 {
    u8 raw[kNodeSize];

    float&  f(int off) { return *reinterpret_cast<float*>(raw + off); }
    i32&    d(int off) { return *reinterpret_cast<i32*>(raw + off); }
    u8&     b(int off) { return raw[off]; }

    void reset() { for (auto& x : raw) x = 0; }
    SceneNode9() { reset(); }
};

// ===========================================================================
// Universe9 — flat view of the universe record (off_649D64 is the active one).
// MoveBetweenUniverses touches:
//   +128 (a1[32])  objectListHead  the node whose +496 chains the next sibling
//   +132 (a1[33])  objectListTail
//   +160 (a1[40])  / +164 (a1[41]) / +168 (a1[42]) scratch mirror slots
// The original mirrors four process globals (dword_13FCF10/13FD140/1408438/
// 140874C) into/out of the active universe's +128/+132/+164/+168 slots. We model
// those four globals as plain fields the test owns, so the mirror dance is exact
// without process globals.
// ===========================================================================
struct Universe9 {
    u8 raw[0x200];
    i32& d(int off) { return *reinterpret_cast<i32*>(raw + off); }
    void reset() { for (auto& x : raw) x = 0; }
    Universe9() { reset(); }
};

// ===========================================================================
// Recovered constants.
// ===========================================================================
// SpawnThrownBomb physics doubles (kept for reference): dbl_61B174=60.0,
// dbl_61B17C=0.5, dbl_61B184=50.0, dbl_61B18C=-60.0.

// ===========================================================================
// Mockable hooks for the unreconstructed leaves. nullptr field => inert default.
// ===========================================================================
struct ObjLife9Hooks {
    // VIBE_Memory_AllocDebug(size, tag) -> block ptr (null on OOM). Returns a
    // POINTER (eax); modeled void* so a real 64-bit allocator survives LP64.
    void* (*memAllocDebug)(unsigned size, const char* tag) = nullptr;
    // VIBE_Memory_FreeDebug(p, ...) — free a tracked block. Default: noop.
    void (*memFreeDebug)(void* p) = nullptr;

    // VIBE_Object_InitStruct(node, block) — zero/seed a fresh node block. The real
    // one zero-fills; default leaves the (already-zeroed) block alone.
    void (*initStruct)(SceneNode9* node) = nullptr;
    // VIBE_Object_AllocDrawData(node) — allocate the +492 draw block. Default: noop.
    void (*allocDrawData)(SceneNode9* node) = nullptr;
    // VIBE_Object_FreeDrawData(node) — free the +492 draw block. Default: noop.
    void (*freeDrawData)(SceneNode9* node) = nullptr;
    // VIBE_Object_SetPosition(node, posPtr) / SetWorldTranslation(node, xlatePtr).
    void (*setPosition)(SceneNode9* node, const float* pos) = nullptr;
    void (*setWorldTranslation)(SceneNode9* node, const float* xlate) = nullptr;
    // VIBE_Object_Dispose(handle) — recursive teardown of a sub-node. Default: noop.
    void (*dispose)(void* handle) = nullptr;
    // VIBE_Object_DisposeResources(node) — free a node's mesh/anim resources.
    void (*disposeResources)(SceneNode9* node) = nullptr;
    // VIBE_Object_InflateGeometry(node) — rebuild a node's geometry on arrival in
    // a new universe (deferred 0x5b30d4; routed as a hook). Default: noop.
    void (*inflateGeometry)(SceneNode9* node) = nullptr;
    // VIBE_Object_MarkDirtyFlag(node, clearHi) — the dirty-bit callback walked by
    // the scene graph. Default forwards into the real ObjLife7 MarkDirtyFlag via
    // the .cpp (so a node passed here gets its +528 bit2 set), see the .cpp.
    // (Modeled as a hook here only to keep the walk inert by default.)
    void (*markDirty)(SceneNode9* node, char clearHi) = nullptr;

    // VIBE_Mesh_LoadOrFindByName(name) -> non-zero when the mesh exists.
    int (*meshLoadOrFind)(const char* name) = nullptr;
    // VIBE_Mesh_AttachStockObjectLods(node, 0, name, end) — attach the mesh LODs.
    void (*meshAttachLods)(SceneNode9* node, const char* name) = nullptr;
    // VIBE_Texture_UploadAllRecords() — flush textures. Default: noop.
    void (*textureUploadAll)() = nullptr;
    // VIBE_Light_BuildObjectCache(node) — rebuild the per-object light cache.
    void (*lightBuildObjectCache)(SceneNode9* node) = nullptr;
    // VIBE_Light_RemoveCacheEntry(node) — drop the node's light-cache entries.
    void (*lightRemoveCacheEntry)(SceneNode9* node) = nullptr;
    // VIBE_Light_RefreshAllObjects() — recompute every object's lighting.
    void (*lightRefreshAll)() = nullptr;
    // VIBE_Shadow_ResetCasterTransforms(node) — clear cached shadow transforms.
    void (*shadowResetCasters)(SceneNode9* node) = nullptr;
    // VIBE_SceneGraph_GetFirstActiveChild(node) -> first non-hidden child handle.
    void* (*sceneGetFirstActiveChild)(SceneNode9* node) = nullptr;
    // VIBE_SceneGraph_WalkAndInvoke(root, node, cb, flags, ctx). Default: inert.
    void (*sceneWalkAndInvoke)(void* root, SceneNode9* node, int flags,
                               int ctx) = nullptr;
    // VIBE_Universe_InitCameraNode(universe) — seed a universe's camera node.
    void (*universeInitCameraNode)(Universe9* u) = nullptr;

    // VIBE_Object_AttachToUniverseNode(0, posVec, model, extra) -> node handle.
    void* (*attachToUniverseNode)(const float* pos, const char* model,
                                  int extra) = nullptr;
    // VIBE_Object_BuildModelName(node, recPtr, mode) — the *table-row* variant
    // (3-arg) used by RebuildModelByOwner; distinct from object_lifecycle7's
    // 7-arg public ObjectBuildModelName. Default: noop.
    void (*buildModelNameByRow)(SceneNode9* node, void* recRow, u8 mode) = nullptr;

    // VIBE_Util_StrCmp(a, b) -> 0 when equal (NOT case-insensitive). The plain
    // 0x5d3f10 comparator is not yet reconstructed. ParseNameAndBind's "strip
    // known suffix" branch needs it. Default: a faithful byte strcmp.
    int (*utilStrCmp)(const char* a, const char* b) = nullptr;
};
void ObjLife9SetHooks(const ObjLife9Hooks& hooks);
void ObjLife9ResetHooks();

// ===========================================================================
// Table descriptors. The originals index two global type tables by row stride;
// the test supplies the base + names so the lookups are pure (no globals).
//   sceneTypeTable  (dword_13CE27C): 65-byte rows, name at +1, class byte at +0
//   buildingTable   (dword_13CE294): 589-byte rows, class byte at +0
//   gebaeudeTable   (dword_13CE298): 169-byte rows, occupied byte at +0,
//                                    owner tag at +1
// ===========================================================================
constexpr int kSceneTypeStride  = 65;
constexpr int kBuildingStride   = 589;
constexpr int kGebaeudeStride   = 169;
constexpr int kSceneTypeRows    = 731;   // 47515/65
constexpr int kBuildingRows     = 72;    // 42408/589
constexpr int kGebaeudeRows     = 256;   // 43264/169
constexpr int kProtClassRows    = 731;   // byte_13CE862 length

// ===========================================================================
// Public faithful entry points.
// ===========================================================================

// 0x583a2c — predicate: is the prototype at scene-type-row `proto` a building
// kind? class byte (row+0) in {1,3,4,11,26,27,28}. `tableBase` == dword_13CE27C.
bool ObjectIsBuildingType(const u8* sceneTypeBase, i16 proto);

// 0x586508 — scan all 731 prototype-class rows (`protClass`, == byte_13CE862) and
// for each row != 72 whose building-table class byte matches this node's, append
// the row index to `out` (== word_13CE29C). Writes the count to *outCount (==
// word_13CE860). Returns 0 (1 when the building table is null). `nodeProto` is the
// node's first byte (*a1). `buildingBase` == dword_13CE294.
int ObjectCollectMatchingProts(const u8* protClass, const u8* buildingBase,
                               i16 nodeProto, u16* out, u16* outCount);

// 0x5a8140 — walk the Gebaeude table (256 rows of 169 bytes) and for every
// occupied row whose +1 owner tag equals this node's +512 owner tag, rebuild the
// model via the row-variant BuildModelName hook (mode 2). Returns 1.
char ObjectRebuildModelByOwner(SceneNode9* node, u8* gebaeudeBase);

// 0x5b3698 — toggle a node between attach-kind 5 (visible) and 6 (hidden). `hide`
// true + node is a 'r'(=114) in state 5 => go hidden (write color = `dirtyTick`,
// walk-and-invoke MarkDirty). `hide` false + state 6 => go visible. Returns 1.
char ObjectToggleHiddenState(SceneNode9* node, char hide, int dirtyTick);

// 0x5b4274 — suspend (hide=true) / restore (hide=false) a node's render state.
// Suspend: when the node is a '!'(=33) re-copy its (2-byte-stepped) name into
// itself; if savedKind was 5/6 refresh lighting; set attachKind from savedKind.
// Restore: stash attachKind in savedKind, set attachKind=1. Returns 1 (0 if null).
char ObjectToggleSuspendStateNamed(SceneNode9* node, char hide);

// 0x5b4420 — rebind a node to a new parent mesh: dispose the old attached sub
// (+508) and first active child (+496 when not flagged), free draw data, load and
// attach the mesh `name`, flush textures, rebuild the light cache. Returns 1 (0
// when null / no mesh-state).
char ObjectRebindParentMesh(SceneNode9* node, const char* name);

// 0x486648 — find a free slot in the bomb pool (handles[64], stride 2) and attach
// a bomb node there at position `pos`. Writes the dirty tick into the parallel
// `ticks` slot. Returns the index of the filled slot, or -1 when the pool is full.
// `dirtyTick` == dword_62EB38.
int ObjectSpawnBomb(const float* pos, void** handles, int* ticks, int slotCount,
                    int dirtyTick);

// 0x5b2478 — deep-clone a scene node. `src` is the source node; allocates a fresh
// 0x21C block (+ a 0x1AC light block for attachKind>=5 stored at +488 word slot),
// runs InitStruct, copies the name + the +76..+152 field run, +488 (392 bytes),
// +156 (240 bytes), +528 (12 bytes), clones draw data when present, clears the
// +529 hi nibble / +528 low bits then sets bit2, and re-seats position + world
// translation. Returns the new block (void*; the original eax is a pointer).
void* ObjectClone(SceneNode9* src);

// 0x5b51e0 — move node `self` from universe `from` to universe `to`. Unlinks
// `self` from `from`'s object list (head/tail at +128/+132, chain at node +496/
// +500) and relinks it at `to`'s list tail; mirrors the active-universe scratch
// slots. Calls InflateGeometry on arrival. Returns 1 on success, 0 otherwise.
// `active` is the g_activeUniverse pointer (may be null); `scratch` is the 4 game
// globals the active universe mirrors (the test owns them).
struct UniverseScratch9 {
    i32 g0 = 0;  // dword_13FCF10 <-> active+128
    i32 g1 = 0;  // dword_13FD140 <-> active+132
    i32 g2 = 0;  // dword_1408438 <-> active+164
    i32 g3 = 0;  // dword_140874C <-> active+168
};
char ObjectMoveBetweenUniverses(SceneNode9* self, Universe9* from, Universe9* to,
                                Universe9* active, UniverseScratch9* scratch);

// 0x4ffb40 — parse a node's name into a type binding. The name is "prefix_suffix";
// the suffix (after the LAST '_' run) is matched case-insensitively against either
// the scene-type table (when the prefix is the building marker `bldMarker`, ==
// byte_620BD4) or the building table (== byte_620BD8) and the resulting row index
// is packed into the node's +72 word. Returns 1 on a match, 0 otherwise. Uses REAL
// util::StrChrLast + util::StrCmpNoCase. `sceneNames`/`buildNames` are row-1 name
// bases; `markerBuild`/`markerScene` are the two prefix markers.
struct ParseTables9 {
    const u8* sceneTypeBase = nullptr;   // dword_13CE27C (65-byte rows)
    const u8* buildingBase  = nullptr;   // dword_13CE294 (589-byte rows)
    const char* markerScene = "";        // byte_620BD4 (=> scene table branch)
    const char* markerBuild = "";        // byte_620BD8 (=> building table branch)
};
int ObjectParseNameAndBind(const char* name, i32* outTypeWord,
                           const ParseTables9& t);

// 0x506b68 — building "visual state" mip-filter + flag state machine. `node` is
// the gameplay record; +7280 holds the 3-bit state nibble. Depending on the
// active-universe flag `universeActive`, the *node+0 word, and the global build-
// phase `buildPhase` (== byte_123351A), pick a new flag set (4/8/0x10), maybe set
// the mip filter and the low-nibble flag. Returns the last computed byte. `force`
// (=a2) forces the side-effect calls even when nothing changed. Writes the chosen
// global build phase into *outBuildPhaseGlobal (== byte_63448C) when set.
char ObjectUpdateBuildingVisualState(u8* node, char force, int universeActive,
                                     char buildPhase, int* outBuildPhaseGlobal,
                                     int curMipShift);

// 0x5b34e4 — a script "change model" callback: when the node has a script name
// (+123 block, +260) whose top owner byte matches `ctx`, and the trigger byte is
// clear, reload the named mesh: copy the (2-byte-stepped) name, strip a trailing
// known extension, dispose old resources, load+attach the new mesh, then walk the
// tree marking dirty. Returns 1. `scriptName` is *(node[123]+260); `triggerByte`
// is *(ctx+16); `ctxOwnerByte` is *(ctx+1) top byte; `nodeOwnerByte` is node[133]
// top byte. The known extensions to strip come from `ext0`/`ext1`/`ext2`.
struct ScriptCbInputs9 {
    const char* scriptName = nullptr;   // *(a1[123]+260)
    char  triggerByte = 0;              // *(a2+16)
    int   ctxOwnerTop = 0;              // *(a2+1) >> 24
    int   nodeOwnerTop = 0;             // a1[133] >> 24
    int   scriptBlockOk = 0;            // a1[123] && *(a1[123]+260) both set
};
char ObjectRunScriptCallback(SceneNode9* node, const ScriptCbInputs9& in);

}  // namespace guild::sim
