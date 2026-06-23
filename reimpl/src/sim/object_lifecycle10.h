#pragma once
// ===========================================================================
// object_lifecycle10.{h,cpp} — VIBE_Object_* remaining leaves (batch 10).
// namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family after batches
// 1..9 (mesh / transform / object lookup). After batch 9 the prefix is nearly
// exhausted; the genuinely-untranslated DETERMINISTIC remainder this batch
// translates 1:1 (confirmed by real DEFINITION lines / .cpp provenance, NOT by the
// deferred-comment blocks). The big remaining VIBE_Object_* functions are all
// deferred (see the DEFERRED list at the bottom): they are register-artifact /
// FPU-no-arg / opaque-global-state-machine ridden and cannot be faithfully 1:1'd.
//
//   * InitStruct 0x5b0e88            — zero/seed a freshly-spawned scene node:
//                                      clear the per-submesh field runs, set the
//                                      default flag bytes (+528/+529/+530/+531),
//                                      seed the +488 light block default float
//                                      (0.1f / int 15), then re-seat world-xlate
//                                      and position to zero.
//   * AllocDrawData 0x5b107c         — allocate the +492 0x910 draw block (when
//                                      absent), seed its 4 submesh entries via the
//                                      REAL InitSubMeshEntry sibling (0x5b0e18,
//                                      object_lifecycle4), clear the bone-palette
//                                      block, set the default scale 1.0f + flag.
//   * AttachToUniverseNode 0x5b3e30  — spawn a kind-4 node for a model, attach the
//                                      stock-object LODs, upload each submesh's
//                                      textures, then seat position / world-xlate
//                                      and link it into the scene (or under a
//                                      parent). Returns the node, or null after a
//                                      Dispose when the mesh failed to attach.
//   * FindByHandle 0x5b7be4          — find a scene node by handle: snapshot+detach
//                                      the root's +124 child link, walk the active
//                                      universe with the REAL MatchHandleCallback
//                                      (object_lifecycle3), then restore the link.
//                                      Returns the matched node.
//   * FindByName 0x5b7cb0            — the by-name variant (REAL MatchNameCallback,
//                                      case-insensitive). The Match*Callback
//                                      predicates (0x5b7b7c / 0x5b7c48) are ALREADY
//                                      reconstructed in object_lifecycle3 and are
//                                      REUSED here (not re-defined — ODR).
//   * DestroySpawnedEntities 0x4fff10 — tear down every spawned entity across the
//                                      universe slot tables (731 + 72 slots) and
//                                      free the heightmap pool (64 rows), resetting
//                                      each active slot.
//
// Reuses REAL reconstructed siblings (NOT mocks):
//   * ObjectMatchHandleCallback / ObjectMatchNameCallback + FindCtx + SceneNode3
//     (object_lifecycle3.cpp @0x5b7b7c / 0x5b7c48) — FindByHandle/FindByName pass
//     these genuine predicates to the scene walk; the by-name one is case-insensitive
//     (the live StrCmpNoCase path). The integration test drives a real walk over
//     several candidate nodes through the real MatchNameCallback.
//   * VIBE_Object_InitSubMeshEntry (object_lifecycle4.cpp @0x5b0e18) — AllocDrawData
//     seeds each entry through it; routed via a hook so this batch never re-defines
//     it (it is ALREADY reconstructed in object_lifecycle4 — ODR).
//
// Every UNRECONSTRUCTED cross-module leaf (Memory_AllocDebug, Object_Spawn/SetParent/
// SetPosition/SetWorldTranslation/LinkIntoScene/Dispose, Mesh_*, Texture_*,
// Universe_*, SceneGraph_WalkAndInvoke, Heightmap_*) is routed through ObjLife10Hooks
// with INERT default implementations defined in THIS library .cpp. Tests install
// their own captor hooks; nothing in a test defines a symbol that src/ references
// (the unified-build pitfall).
//
// LP64 caveat: the original is 32-bit; "pointers" stored in the node record are
// 4-byte slots. Pointer-typed slots this batch FOLLOWS are stored/read at NATIVE
// width via SceneNode10::p()/BlockPtr so a real 64-bit heap pointer never gets
// truncated; plain int/float/flag fields stay at their verbatim 4-byte/1-byte
// offsets. Public entry points that return the original eax "pointer" return void*.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x5b0e88  VIBE_Object_InitStruct
//   0x5b107c  VIBE_Object_AllocDrawData
//   0x5b3e30  VIBE_Object_AttachToUniverseNode
//   0x5b7be4  VIBE_Object_FindByHandle
//   0x5b7cb0  VIBE_Object_FindByName
//   0x4fff10  VIBE_Object_DestroySpawnedEntities
//
// (0x5b7b7c MatchHandleCallback / 0x5b7c48 MatchNameCallback are REUSED from
//  object_lifecycle3 — already reconstructed there; NOT re-translated here.)
#include <cstdint>
#include <cstring>

#include "guild/common/types.h"
#include "sim/object_lifecycle3.h"   // REAL siblings: ObjectMatch*Callback, FindCtx,
                                     // SceneNode3 (0x5b7b7c / 0x5b7c48)

namespace guild::sim {

// ===========================================================================
// SceneNode10 — flat byte view of the "d3:SpawnObject" scene node, 0x21C bytes,
// for the fields this batch touches. Same opaque block as SceneNode6/7/8/9; named
// only where this batch reads/writes (raw offsets only, LP64-safe). Offsets are
// from the original (a1[N] meaning the dword at 4*N):
//   +488   (488)   lightBlock   (a1[122]) per-object light block ptr (InitStruct;
//                              passed explicitly — see ObjectInitStruct LP64 note)
//   +492   (492)   drawData     (a1[123]) alloc'd draw block ptr
//   +496   (496)   childLink    (a1[124]) the child link FindByHandle snapshots and
//                              temporarily zeroes during the walk (then restores)
//   +528   (528)   flags528      (a1+528)  flag byte cleared/seeded on init
//   +529   (529)   flags529      (a1+529)  flag byte cleared/seeded on init
//   +530   (530)   flags530      (a1+530)
//   +531   (531)   flags531      (a1+531)
//   +532   (532)   flags532      (a1+532)
//   +533   (533)   attachKind    (a1+533)  init default = 1
//   +535   (535)   upgradeState  (a1+535)
// ===========================================================================
constexpr int kNodeSize10 = 0x21C;        // 540 bytes

namespace n10 {
constexpr int kLightBlock   = 488;
constexpr int kDrawData     = 492;
// (the +496 a1[124] child link is followed on SceneNode3 in FindByHandle/Name.)
constexpr int kFlags528     = 528;
constexpr int kFlags529     = 529;
constexpr int kFlags530     = 530;
constexpr int kFlags531     = 531;
constexpr int kFlags532     = 532;
constexpr int kAttachKind   = 533;
constexpr int kUpgradeState = 535;
}  // namespace n10

// LP64 modeling: the 32-bit original stores 4-byte slots; on a 64-bit host a real
// heap pointer can't survive a 4-byte slot. So pointer-typed slots (the ones this
// batch FOLLOWS — drawData/lightBlock/childLink/...) are stored and read at NATIVE
// width via `p(off)` (occupying sizeof(void*) bytes at `off`); plain integer / float
// / flag fields stay at their verbatim 4-byte/1-byte offsets via d()/f()/b(). The
// struct is padded past 0x21C so a native pointer at the highest pointer offset
// still fits.
struct SceneNode10 {
    u8 raw[kNodeSize10 + 16];

    float&  f(int off) { return *reinterpret_cast<float*>(raw + off); }
    i32&    d(int off) { return *reinterpret_cast<i32*>(raw + off); }
    u8&     b(int off) { return raw[off]; }
    void*&  p(int off) { return *reinterpret_cast<void**>(raw + off); }

    void reset() { for (auto& x : raw) x = 0; }
    SceneNode10() { reset(); }
};

// Native-width pointer load/store into an external opaque block (LP64-safe).
inline void* BlockPtr(const void* base, int off) {
    void* v;
    std::memcpy(&v, static_cast<const u8*>(base) + off, sizeof(void*));
    return v;
}
inline void SetBlockPtr(void* base, int off, void* v) {
    std::memcpy(static_cast<u8*>(base) + off, &v, sizeof(void*));
}

// ===========================================================================
// Recovered constants.
// ===========================================================================
// The +492 draw block is 0x910 (2320) bytes; it holds 4 submesh entries of 0x180
// (384) bytes starting at +244, a bone-palette region, then a small trailer.
constexpr int   kDrawBlockSize    = 0x910;   // VIBE_Memory_AllocDebug(0x910, ...)
constexpr int   kSubMeshStride    = 384;     // entry stride
constexpr int   kSubMeshFirst     = 244;     // first entry offset in draw block
constexpr int   kSubMeshCount     = 4;       // 1536 / 384
// AttachToUniverseNode (0x5b3e30) reads, per LOD record (stride 384 within the draw
// block), the LOD-mesh ptr at +260 ([ecx+104h]) and the texture-handle array ptr at
// the verbatim 32-bit +264 ([ecx+108h]). Those two pointer slots are 4 bytes apart;
// under the module's LP64 doctrine (followed pointer slots are native-width) the
// texarr ptr is read from the next native slot (+260+sizeof(void*)) so both survive
// disjoint on a 64-bit host (== +264 exactly on a 32-bit build). The per-LOD mesh
// record's texture/material count is the 32-bit field at lodMesh+480 ([eax+1E0h]).
constexpr int   kLodMeshPtrOff    = 260;     // [ecx+104h] LOD-mesh ptr (presence gate)
constexpr int   kLodTexCountOff   = 480;     // [eax+1E0h] tex count on the LOD mesh
// The +488 light block default per-stage: float 0.1f at slot, int 15 at +60.
constexpr i32   kInitFloatBits    = 1050253722;  // 0.1f
constexpr i32   kOneFloatBits     = 1065353216;  // 1.0f
// FindByHandle/FindByName tag the walk-flags' high byte with bit1 (HIBYTE|=2).
constexpr u16   kWalkFlagHiBit    = 0x0200;

// ===========================================================================
// Mockable hooks for the unreconstructed leaves. nullptr field => inert default.
// ===========================================================================
struct ObjLife10Hooks {
    // VIBE_Memory_AllocDebug(size, tag) -> block ptr (null on OOM). Returns a
    // POINTER (eax); modeled void* so a real 64-bit allocator survives LP64.
    void* (*memAllocDebug)(unsigned size, const char* tag) = nullptr;

    // --- AttachToUniverseNode leaves ---
    // VIBE_Object_Spawn(kind=4, name) -> fresh node handle.
    void* (*objSpawn)(int kind, const char* name) = nullptr;
    // VIBE_Object_SetParent(parent, node).
    void (*objSetParent)(void* parent, SceneNode10* node) = nullptr;
    // VIBE_Object_SetPosition(node, posPtr) / SetWorldTranslation(node, xlatePtr).
    void (*objSetPosition)(SceneNode10* node, const float* pos) = nullptr;
    void (*objSetWorldTranslation)(SceneNode10* node, const float* xlate) = nullptr;
    // VIBE_Object_LinkIntoScene(node). Default: noop.
    void (*objLinkIntoScene)(SceneNode10* node) = nullptr;
    // VIBE_Object_Dispose(node). Default: noop.
    void (*objDispose)(SceneNode10* node) = nullptr;
    // VIBE_Mesh_FindStockObject() -> non-zero when a stock object is loaded.
    int (*meshFindStockObject)() = nullptr;
    // VIBE_Mesh_LoadOrFindByName(name) -> mesh handle.
    void (*meshLoadOrFind)(const char* name) = nullptr;
    // VIBE_Mesh_AttachStockObjectLods(node, 0, name, end) — attach LODs.
    void (*meshAttachLods)(SceneNode10* node, const char* name) = nullptr;
    // VIBE_Texture_UploadToSurface(surfacePtr, 0, srcPtr, lod). Default: noop.
    void (*textureUploadToSurface)(void* surface, void* src, int lod) = nullptr;
    // VIBE_Object_InitSubMeshEntry(entryBytes, node) — seed one 0x180 draw entry.
    // ALREADY reconstructed in object_lifecycle4 (0x5b0e18); routed via this hook so
    // this batch never re-defines it. Default: a faithful raw-byte seeder (in .cpp).
    void (*initSubMeshEntry)(u8* entryBytes, void* node) = nullptr;

    // --- FindByHandle / FindByName leaves ---
    // VIBE_SceneGraph_WalkAndInvoke(root, startNode, cb, walkFlags, resultSlot).
    // `cb` is one of the REAL object_lifecycle3 Match*Callback predicates (passed as
    // a void*: bool(*)(SceneNode3*, FindCtx*)). The walk visits each node and invokes
    // cb(node, resultSlot); cb records the hit into resultSlot->found. Default: inert
    // (no nodes visited) so resultSlot->found stays null.
    void (*sceneWalkAndInvoke)(void* root, void* startNode, void* cb,
                               u16 walkFlags, void* resultSlot) = nullptr;

    // --- DestroySpawnedEntities leaves ---
    // VIBE_Universe_SwitchActiveSlot(hi, mode, slot, ctx). Default: noop.
    void (*universeSwitchActiveSlot)(int hi, int mode, int slot, void* ctx) = nullptr;
    // VIBE_Universe_ResetCurrentSlot(ctx) -> result. Default: 0.
    int (*universeResetCurrentSlot)(void* ctx) = nullptr;
    // VIBE_SceneGraph_FreeNodeRecursive(node). Default: noop.
    void (*sceneGraphFreeNodeRecursive)(void* node) = nullptr;
    // VIBE_Heightmap_Free(handle, ctx, 0) -> result. Default: 0.
    int (*heightmapFree)(void* handle, void* ctx) = nullptr;
};
void ObjLife10SetHooks(const ObjLife10Hooks& hooks);
void ObjLife10ResetHooks();

// ===========================================================================
// Public faithful entry points.
// ===========================================================================

// 0x5b0e88 — zero/seed a freshly-spawned scene node. Clears the 10 per-submesh
// field runs (a1[34..39],a1[33] over the +24-stride loop), the +500/+524 dwords,
// the named draw-state dwords, sets the default flag bytes, seeds the +488 light
// block's per-stage defaults (0.1f / int 15) when present, then SetWorldTranslation
// and SetPosition to zero. Returns void* (the original eax is the SetPosition rc).
//
// LP64: the original reads the light block from the node's +488 word slot
// (a1[122]) and dereferences it. A real 64-bit heap pointer cannot survive a 4-byte
// slot round-trip, so the light block is passed EXPLICITLY here (the live binary's
// a1[122]); pass nullptr when the node has no light block (matching v2[122]==0).
void* ObjectInitStruct(SceneNode10* node, void* lightBlock);

// 0x5b107c — allocate the +492 draw block (when absent): AllocDebug(0x910), seed
// the 4 submesh entries via the InitSubMeshEntry hook (REAL 0x5b0e18 sibling),
// clear the bone-palette region (512-byte stride x4 slots), set the trailer defaults
// (scale 1.0f at +2296, sentinel -1 at +176, flag bit6 at +2317). Returns the draw
// block (void*).
void* ObjectAllocDrawData(SceneNode10* node);

// 0x5b3e30 — spawn a kind-4 node for `model`, attach its stock-object LODs, upload
// every submesh's textures, then seat `pos` / `xlate` and link it into the scene
// (under `parent` when non-null, else LinkIntoScene). Returns the new node, or null
// (after Dispose) when the attached mesh has no resident submeshes. `parent` may be
// null (root-level attach). `pos`/`xlate` are the original a2 (edx) / a4 (ebx).
void* ObjectAttachToUniverseNode(void* parent, const float* pos, const char* model,
                                 const float* xlate, void* ctx);

// The Find* functions hand WalkAndInvoke the REAL FindCtx (object_lifecycle3):
// {queryStr (a3), queryNode (a4), found (result == the Find* return value)}.

// 0x5b7be4 — find a scene node by HANDLE. Returns null when both `name`/`handle` are
// null. Snapshots and zeroes `root`'s +124 child link, sets the walk-flags high byte
// bit1, walks the active universe invoking the REAL ObjectMatchHandleCallback (via
// the sceneWalkAndInvoke hook) against a FindCtx, then restores the child link.
// Returns the matched node (ctx.found). `root` may be null (whole-universe walk).
SceneNode3* ObjectFindByHandle(SceneNode3* root, u16 walkFlags, const char* name,
                               SceneNode3* handle, void* ctx);

// 0x5b7cb0 — the by-NAME variant of FindByHandle: identical control flow but the
// walk uses the REAL ObjectMatchNameCallback (case-insensitive). Returns the match.
SceneNode3* ObjectFindByName(SceneNode3* root, u16 walkFlags, const char* name,
                             SceneNode3* handle, void* ctx);

// 0x4fff10 — tear down every spawned entity across the two universe slot tables and
// the heightmap pool, resetting each active slot. `objRoot` (== dword_634488) is the
// top scene-graph node freed recursively when present; the slot tables and heightmap
// pool are supplied as flat buffers the caller owns (modeling dword_122DDAC / +0xB0
// signs, dword_122DD5D and dword_13ECF78). Returns the last ResetCurrentSlot rc.
//
// The two slot tables are 1-BYTE-STRIDE PACKED tables (verbatim from the original,
// whose exact field packing is opaque): for table A, slot `i`'s active flag is the
// signed byte at base + 4 + i (== byte_122DDB0[i]); the universe-hi passed to
// SwitchActiveSlot is the high byte of the dword read at base + 1 + i. We model both
// tables as raw byte buffers and index by BYTE exactly as the original, so the
// overlapping reads stay bit-identical (no layout guessing).
//   `slotsA`  : byte base of dword_122DDAC; `slotsACount` = 731 slots (i: 0..730).
//   `slotsB`  : byte base of dword_122DD5D; `slotsBCount` = 72 slots (j: 0..71).
//               Its flag byte is at base + 3 + j; the hi dword at base + j.
//   `heightmaps`: 64 handles at stride 246 dwords (the original 0..15744 step 246).
// Caller must size slotsA >= 4 + slotsACount + 4 bytes and slotsB >= 3 + slotsBCount
// + 4 bytes so the overlapping dword reads stay in bounds.
struct DestroyTables10 {
    u8* slotsA = nullptr;      int slotsACount = 731;   // dword_122DDAC byte base
    u8* slotsB = nullptr;      int slotsBCount = 72;    // dword_122DD5D byte base
    i32* heightmaps = nullptr; int heightmapStride = 246; int heightmapSpan = 15744;
};
// `firstSlot` is the original a1 (ecx) passed verbatim to the first SwitchActiveSlot.
// `objRoot` (== dword_634488) is the top scene-graph root freed when present;
// `objRootSlot` aliases that global so the function can null it after freeing.
int ObjectDestroySpawnedEntities(int firstSlot, void* objRoot, void** objRootSlot,
                                 const DestroyTables10& t, void* ctx);

}  // namespace guild::sim
