#pragma once
// ===========================================================================
// object_lifecycle7.{h,cpp} — VIBE_Object_* remaining leaves (batch 7).
// namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family after
// batches 1..6. The genuinely-untranslated remainder is a grab-bag of small,
// deterministic leaves: a dirty-flag bit setter, the clip-clamped dirty-rect
// queue insert, the animation-scale field write, a script "change state"
// command builder, the recursive name+count formatter, the block clone/free
// helper, the named-node list insert, the GUI button-callback table write, the
// scene-node "move into another tree" callback, three script Cmd* entry points
// (ShowObject / ShowObjectAtDummy / LoadScene) and the model-name builder.
//
// The big remaining VIBE_Object_* functions (UpdateGateContact 0x4b8ba8,
// ApplyTransformConstraints 0x5e89b4, AttachUpgradeEffect 0x505208,
// ResolveQuickJumpContact 0x4b950c, Clone 0x5b2478, ComputeBoneScreenExtents
// 0x5b6ebc, the CmdAttachAnimation* family, ParseNameAndBind 0x4ffb40,
// MoveBetweenUniverses 0x5b51e0, ComputeMarketValue 0x594df0) are deferred —
// see the .cpp report. ComputeMarketValue in particular has a Hex-Rays
// register-aliasing artifact (`*(int*)(v12 + 354)` with v12 never assigned in
// the listing) that cannot be faithfully resolved from the pseudocode alone.
//
// Reuses g_activeUniverse (owned by character_query.cpp, == off_649D64 @0x649D64)
// via extern as the scene-graph walk root, exactly as the originals do.
//
// All unreconstructed cross-module leaves (SceneGraph_WalkAndInvoke / TraverseTree,
// AttachToUniverseNode, Light_BuildObjectCache / RemoveCacheEntry,
// Transform_PointThroughBoneChain, Render_FreeObjectNode, Scene_LoadFromStream,
// Script_ReportError, Crt_Sprintf, AnimationFlags_Compute, ParseNameAndBind,
// Universe_RestoreObjectStates, Command_*, GameObject_QueryFind, Memory_*,
// ModelIo_LoadBinaryAnimation, Light_SetGrayColorThunk) are routed through
// ObjLife7Hooks with INERT default implementations defined in THIS library .cpp.
// Tests install their own captor hooks; nothing in a test defines a symbol that
// src/ references.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x5af298  VIBE_Object_MarkDirtyFlag
//   0x40e818  VIBE_Object_Reinitialize
//   0x41e388  VIBE_Object_ApplyAnimScale
//   0x594a68  VIBE_Object_RequestChangeZustand
//   0x510000  VIBE_Object_FormatNameWithCountRecursive
//   0x5f1d00  VIBE_Object_CloneOrFreeData
//   0x42e12c  VIBE_Object_InsertNamedNode
//   0x41e49c  VIBE_Object_SetButtonCallback
//   0x5b5184  VIBE_Object_MoveNodeCallback
//   0x43e624  VIBE_Object_CmdShowObject
//   0x43e66c  VIBE_Object_CmdShowObjectAtDummy
//   0x43ea80  VIBE_Object_CmdLoadScene
//   0x4ffe0c  VIBE_Object_BuildModelName
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// SceneNode7 — flat byte view of the "d3:SpawnObject" scene-graph node, for the
// fields this batch touches. Same opaque ~0x21C block as SceneNode6/SceneObject;
// re-declared here (named only where this batch touches) to avoid an ODR clash
// with the sibling translation units' views. Byte offsets are from the original:
//   +0x4C  (76)   pos[3]       world translation       (a1[19..21])
//   +0x84  (132)  worldXlate[3] / euler                (a1[33..35] / +132..+140)
//   +0x90  (144)  drawData     (a1[36])                 alloc'd draw block ptr
//   +0x18C (396)  matrix[16]   euler->matrix scratch    (+396)
//   +0x1EC (492)  subMesh      (a1[123]) sub-mesh block ptr
//   +0x1F8 (504)  parent       (a1[126]) parent node
//   +0x208 (520)  ownerTag     (a1[130])
//   +0x210 (528)  flags528      bit2 = dirty
//   +0x212 (530)  flags530      bit7 cleared on dirty(arg)
//   +0x213 (531)  flags531      bit0 cleared on dirty
//   +0x215 (533)  attachKind    (3 == world-anchored)
//   +0x217 (535)  modelKind     (3/4 set by BuildModelName)
// ===========================================================================
struct SceneNode7 {
    u8 raw[0x21C];

    float& f(int off) { return *reinterpret_cast<float*>(raw + off); }
    i32&   d(int off) { return *reinterpret_cast<i32*>(raw + off); }
    u8&    b(int off) { return raw[off]; }

    void reset() { for (auto& x : raw) x = 0; }
    SceneNode7() { reset(); }
};

// ===========================================================================
// Dirty-rect / clip queue used by VIBE_Object_Reinitialize (dword_62D2DC[0] is
// the queue base; each record is 20 bytes: x,y,w,h,objptr). The clip bounds are
// dword_64A1B4/B8 (min x/y) and dword_64A1BC/C0 (max x/y). Modeled as a POD so a
// test can supply the bounds + base and read back the inserted record.
// ===========================================================================
struct DirtyRectQueue {
    struct Rect {
        i32 x;       // +0   (a1)
        i32 y;       // +4   (v9 / a2)
        i32 w;       // +12  (a4)   NOTE: original writes +12 = w, +16 = h, +0 = x, +4 = y, +8 = obj
        i32 h;       // +16  (v6)
        i32 obj;     // +8   (a5)
    };
    i32  base = 0;          // dword_62D2DC[0]  (non-zero queue base flag in test)
    int  count = 0;         // number of slots scanned occupied (v8)
    Rect slots[512] = {};   // up to 512 records; original caps occupancy at 512
};

// Recovered market-value constants (kept for reference / future ComputeMarketValue).
// (dbl_626B34=0.01, dbl_626B3C=0.003968253968253968, dbl_626B44=0.25,
//  dbl_626B4C=-0.5, dbl_626B54=0.1, dbl_626B5C=0.3)

// ===========================================================================
// Mockable hooks for the unreconstructed leaves. nullptr field => inert default.
// ===========================================================================
struct ObjLife7Hooks {
    // VIBE_SceneGraph_WalkAndInvoke(root, node, callback, flags, ctx) -> char.
    // Used by MoveNodeCallback to flush light-cache entries. Default: inert, 0.
    char (*sceneGraphWalkAndInvoke)(int root, int node, int (*cb)(),
                                    int flags, int ctx) = nullptr;

    // VIBE_AnimationFlags_Compute(handle, out[]) -> non-zero when valid; writes
    // the scale numerator into out[0] (xScale, v3 in eax) and out[1]
    // (yScale, v6[0]) and the new flag word (v5). ApplyAnimScale reads:
    //   v3 = out scale, v5 = out[1] (esp-14h aka the returned numerators).
    // We model it as: returns scale `n`, and writes baseW->*ow, baseH->*oh.
    int (*animationFlagsCompute)(int handle, int* outScale, int* outW,
                                 int* outH) = nullptr;

    // VIBE_GameObject_QueryFind(a,b,c,d,id) -> node ptr (0 when not found).
    // RequestChangeZustand uses it to resolve a script object id. Default: 0.
    void* (*gameObjectQueryFind)(int a, int b, int c, int d, int id) = nullptr;

    // The Command_* delta-packet builder chain used by RequestChangeZustand.
    // beginDeltaPacket(node, ctx); appendRawField(a,b,valPtr,fieldId);
    // queueRequestState22() -> int. Default: noop / returns 0.
    void (*commandBeginDeltaPacket)(void* node, int ctx) = nullptr;
    void (*commandAppendRawField)(unsigned a, unsigned b, const void* val,
                                  int fieldId) = nullptr;
    int  (*commandQueueRequestState22)() = nullptr;

    // VIBE_Script_ReportError(ctx, code, msg) — the diagnostic path. We pass the
    // already-formatted message text. Default: noop.
    void (*reportError)(const char* msg) = nullptr;

    // VIBE_Crt_Sprintf_0(dst, fmt, ...) -> int. Only used to format strings; the
    // module formats with std::snprintf and feeds the result onward, so this is
    // not separately hooked.

    // VIBE_Light_SetGrayColorThunk(a, b) — FormatNameWithCountRecursive calls it
    // before each sprintf (a UI side effect). Default: noop.
    void (*lightSetGrayColorThunk)(int a, int b) = nullptr;

    // VIBE_Memory block helpers used by CloneOrFreeData:
    //   allocFromFreeList(size)         -> new block ptr (null on OOM)
    //   blockHeaderClear(p)             -> the block's stored byte size
    //   shrinkBlock(p)                  -> non-zero when the block already fits
    //   returnToFreeList(p)             -> frees the block
    // The original's AllocFromFreeList returns a POINTER in eax; we model it as
    // void* (not int) so a real allocator's 64-bit pointer survives on LP64 — the
    // public ObjectCloneOrFreeData still narrows it to the faithful 32-bit handle.
    // Defaults model a NO-pool: alloc returns null, header size 0, shrink 0.
    void* (*memAllocFromFreeList)(unsigned size) = nullptr;
    unsigned (*memBlockHeaderClear)(const void* p) = nullptr;
    // shrinkBlock returns the (possibly resized) block pointer when it fits in
    // place, else null -> the alloc+copy+free realloc path runs. (eax is a ptr.)
    void* (*memShrinkBlock)(const void* p) = nullptr;
    void (*memReturnToFreeList)(const void* p) = nullptr;

    // VIBE_ModelIo_LoadBinaryAnimation(name, arg, kind) -> node ptr (0 on miss).
    // InsertNamedNode loads then links the result. Default: 0.
    void* (*modelIoLoadBinaryAnimation)(const char* name, void* arg,
                                        char kind) = nullptr;

    // VIBE_Object_AttachToUniverseNode(zero, posVec, model, extra) -> node handle.
    // CmdShowObject/CmdShowObjectAtDummy attach a freshly-positioned node.
    // Default: 0 (attach fails -> handler returns 0 / skips the cache build).
    int (*attachToUniverseNode)(int zero, const float* posVec, void* model,
                                int extra) = nullptr;

    // VIBE_Light_BuildObjectCache(handle) — rebuild the per-object light cache.
    void (*lightBuildObjectCache)(int handle) = nullptr;

    // VIBE_Transform_PointThroughBoneChain(mtx, posIn, posOut[3]) — resolve a
    // dummy bone's world position. Default: copies posIn -> posOut.
    void (*transformPointThroughBoneChain)(const float* mtx, const float* posIn,
                                           float* posOut) = nullptr;

    // VIBE_Scene_LoadFromStream(name, a, b, c) -> non-zero on success.
    int (*sceneLoadFromStream)(const char* name, int a, i16 b, int c) = nullptr;

    // VIBE_Render_FreeObjectNode(node, handle) — MoveNodeCallback render cleanup.
    void (*renderFreeObjectNode)(void* node, int handle) = nullptr;

    // VIBE_Object_ParseNameAndBind(node, node) -> int. BuildModelName reparses the
    // formatted name. Default: returns 0.
    int (*parseNameAndBind)(SceneNode7* node) = nullptr;

    // VIBE_Universe_RestoreObjectStates(node, flag). BuildModelName side effect.
    void (*universeRestoreObjectStates)(SceneNode7* node, unsigned flag) = nullptr;
};
void ObjLife7SetHooks(const ObjLife7Hooks& hooks);
void ObjLife7ResetHooks();

// ===========================================================================
// GUI widget table view (dword_69FFB4: 740-byte slots; SetButtonCallback maps a
// widget index to a callback-table index via the slot's +116 field; the callback
// table is dword_695098, stride 87 ints, 4-byte entries). Modeled as a POD pair.
// ===========================================================================
constexpr int kWidgetSlotStride   = 740;   // bytes per widget slot
constexpr int kWidgetSlotIdxField = 116;   // +116: the callback-table base index
constexpr int kCallbackStride     = 87;    // 87 ints per callback record

struct WidgetTable { u8* base = nullptr; };       // dword_69FFB4
struct CallbackTable { i32* base = nullptr; };     // dword_695098

// ===========================================================================
// Public faithful entry points. See the .cpp for the 1:1 control flow.
// ===========================================================================

// 0x5af298 — set the dirty bit (+528 |= 4); when `clearHi` clear +530 bit7; always
// clear +531 bit0. Returns 1. This is the SceneGraph_WalkAndInvoke callback.
char ObjectMarkDirtyFlag(SceneNode7* node, char clearHi);

// 0x40e818 — clip-clamp a dirty rectangle (x,y,w,h, obj) against the screen bounds
// and append it to the dirty-rect queue (capped at 512 records). Returns the
// last `result` value the original would (the appended record's queue offset, or
// the pre-existing `result` when bounds reject / no obj). Mutates `q`.
int ObjectReinitialize(DirtyRectQueue* q, int x, int y, int w, int h, int obj,
                       int clipMinX, int clipMinY, int clipMaxX, int clipMaxY);

// 0x41e388 — recompute a node's on-screen anim scale (+20/+22 words) from the
// AnimationFlags_Compute result. `handle` != -1 required. Returns the AnimationFlags
// result on the no-flags path (faithful eax), else the node base address marker.
int ObjectApplyAnimScale(SceneNode7* node, int handle);

// 0x594a68 — script "change object state" command: resolve the object by id, clamp
// the requested delta against the current state, build a delta packet and queue a
// state-22 request. Returns the queue result (or the sprintf result on the
// not-found error path, matching the original). `curState` models *(node+18).
int ObjectRequestChangeZustand(int objId, char delta, int ctx, int curState);

// 0x510000 — recursively format "<name> (<count>)" into `buf` at depth `depth`,
// walking the child chain. `node` is the gameplay record (its +0 word indexes the
// building-type table at `typeTable`, stride 589; the name is at +1; the count is
// at +14). Children chain via +20 then +63. Returns the deepest buf written.
// The walk is bounded by the supplied node graph (no globals).
struct NameCountNode {
    u16 typeIndex;          // *node (word) -> typeTable row
    i32 count;              // +14 (the "(%li)" count)
    NameCountNode* firstChild = nullptr;  // *(node+20)
    NameCountNode* nextSibling = nullptr; // *(child+63)
};
char* ObjectFormatNameWithCountRecursive(NameCountNode* node, int depth,
                                         char* buf, const char* const* typeNames);

// 0x5f1d00 — clone-or-free a heap block: a1==0 => allocate `size`; size==0 =>
// free and return null; else shrink-in-place if possible, otherwise
// alloc+copy+free. Returns the resulting block pointer (the original's eax is a
// pointer; modeled as void* so a real 64-bit allocator pointer survives LP64).
void* ObjectCloneOrFreeData(const void* block, unsigned size);

// 0x42e12c — copy a (UTF-16-ish, 2-byte stepped) name into a stack buffer, load a
// binary animation node by that name, and push it onto the global node list
// (modeled here by an explicit `list` so the test owns the storage). `kind` is the
// load mode. Returns the loaded node (or nullptr).
struct NamedNodeList {
    void* head = nullptr;     // dword_13FC8E4
    void* sentinel = nullptr; // &unk_13FC780 (written into result+88)
};
void* ObjectInsertNamedNode(const char* name, void* arg, char kind,
                            NamedNodeList* list);

// 0x41e49c — install a GUI button callback: map widget index `w` through the slot
// table to a callback-table index, store `cb` there. Returns that index * 4
// (the original's eax). `wt`/`ct` supply the two table bases.
int ObjectSetButtonCallback(const WidgetTable& wt, const CallbackTable& ct,
                            int w, int cb);

// 0x5b5184 — "move node into another tree" callback: flush the node's light-cache
// entries, set the node's owner tag (+520) from arg[1], then walk a render-node
// list (arg) freeing any render node whose +184 owner matches `handle`. Returns 1.
// `renderHead`/`renderSentinel` model the *(arg+164) list + &unk_1408440.
struct RenderNode {
    int owner;                 // +184 (v4[184])
    RenderNode* next = nullptr; // +194 dword (v4[194])
};
char ObjectMoveNodeCallback(int handle, int newTreeRoot, int newOwnerTag,
                            RenderNode* renderHead, RenderNode* renderSentinel);

// 0x43e624 — script ShowObject: gather a 3-float position from three arg vectors,
// attach a node, and (when attached) build its light cache. Returns the handle.
int ObjectCmdShowObject(const float* posA, const float* posB, const float* posC,
                        void* model);

// 0x43e66c — script ShowObjectAtDummy: resolve a dummy bone's world position via
// the bone chain, attach a node there, build its cache. `dummy` is the dummy node
// (its +0 is the matrix base, +76 the local pos). Returns the handle (0 when the
// dummy is null).
int ObjectCmdShowObjectAtDummy(SceneNode7* dummy, void* model, int extra);

// 0x43ea80 — script LoadScene: load a scene from disk; on failure report the
// "Could not find scene on disk" diagnostic. Returns 1.
int ObjectCmdLoadScene(const char* name, i16 target);

// 0x4ffe0c — build a model name into `node` from a type-table row (mode 1 ->
// "ob_%s" from the scene-type table, mode 2 -> "gb_%s" from the building-type
// table), set the model-kind byte, reparse+bind the name and link the destroy
// handler. `mode`: 0/other => skip the format, just parse. `rec` is the gameplay
// record (its +0 word/byte indexes the table, +90 a flag byte, +97 a back-ptr,
// +533 the attach kind). Returns the parse-and-bind result.
int ObjectBuildModelName(SceneNode7* node, u8 typeIndex, u8 recFlag90,
                         u8 recAttachKind, u8 firstByteIsTen, u8 mode,
                         const char* typeName);

}  // namespace guild::sim
