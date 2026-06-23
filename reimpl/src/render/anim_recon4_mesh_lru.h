#pragma once
// anim_recon4_mesh_lru.h — anim/mesh/shape leaves (recon4 cluster)
//
// Faithful 1:1 reconstructions of a cluster of small mesh/animation/shape leaves
// from gilde.exe. The *pure logic* (LRU mesh-budget eviction, bone-name matching,
// mesh memory-size arithmetic, animation flag/scale bit math, OAM keyframe
// post-load transform) is reconstructed exactly here. The genuinely coupled
// leaves — heap free + bookkeeping, scene-graph walk, and VFS file reads — are
// routed through an inert-default hooks struct so the in-scope logic stays
// verifiable and headless.
//
//   gilde.exe 0x5cfd24 — VIBE_Anim_ComputeMeshMemorySize   (pure arithmetic)
//   gilde.exe 0x5cfdb8 — VIBE_Anim_EvictMeshesForBudget     (LRU eviction)
//   gilde.exe 0x5cfe30 — VIBE_Anim_ReleaseMeshData          (refcount/unlink + free)
//   gilde.exe 0x5cbfc0 — VIBE_Anim_AssignSubMeshBones       (bone-name matching)
//   gilde.exe 0x41dc74 — VIBE_AnimationFlags_Compute        (flag/scale bit math)
//   gilde.exe 0x5b2ef8 — VIBE_Mesh_FreeAttachedBuffers      (buffer free loop)
//   gilde.exe 0x43fc38 — VIBE_Anim_FreeObjAnimDataAndReset  (free + global reset)
//   gilde.exe 0x5d3f10 — VIBE_Util_StrCmp                   (strcmp helper)
//   gilde.exe 0x5d367c — VIBE_Mesh_LoadObjectAnimation      (OAM transform; IO omitted)
//
// All struct accesses are modelled byte-exactly against the original flat record
// layouts. We expose the records as raw byte buffers addressed by the same
// offsets the decompile uses, so the reconstruction is observably 1:1 over
// synthetic data without committing to a guessed C struct.

#include "guild/common/types.h"
#include <cstring>

namespace guild::render::anim_recon4 {

using guild::u8;
using guild::u16;
using guild::i32;
using guild::u32;
using f32 = float;

// ---------------------------------------------------------------------------
// Raw record accessors. The original code treats these records as flat byte
// blobs indexed by literal offsets (e.g. `*(_DWORD*)(p + 344)`). We mirror that
// exactly via helpers over a u8* base, so the offsets in the reconstruction are
// the offsets in the binary.
// ---------------------------------------------------------------------------
inline u32  rd_u32(const void* base, i32 off) {
    u32 v; std::memcpy(&v, static_cast<const u8*>(base) + off, 4); return v;
}
inline void wr_u32(void* base, i32 off, u32 v) {
    std::memcpy(static_cast<u8*>(base) + off, &v, 4);
}
inline i32  rd_i32(const void* base, i32 off) {
    i32 v; std::memcpy(&v, static_cast<const u8*>(base) + off, 4); return v;
}
inline void wr_i32(void* base, i32 off, i32 v) {
    std::memcpy(static_cast<u8*>(base) + off, &v, 4);
}
inline u16  rd_u16(const void* base, i32 off) {
    u16 v; std::memcpy(&v, static_cast<const u8*>(base) + off, 2); return v;
}
inline void wr_u16(void* base, i32 off, u16 v) {
    std::memcpy(static_cast<u8*>(base) + off, &v, 2);
}
inline u8   rd_u8(const void* base, i32 off) {
    return static_cast<const u8*>(base)[off];
}
inline void wr_u8(void* base, i32 off, u8 v) {
    static_cast<u8*>(base)[off] = v;
}
inline f32  rd_f32(const void* base, i32 off) {
    f32 v; std::memcpy(&v, static_cast<const u8*>(base) + off, 4); return v;
}
inline void wr_f32(void* base, i32 off, f32 v) {
    std::memcpy(static_cast<u8*>(base) + off, &v, 4);
}
// ---------------------------------------------------------------------------
// 32-bit-pointer arena. The original stores absolute 32-bit pointers in the
// intrusive-list link fields (+352/+356, only 4 bytes apart). A real 64-bit
// host pointer does not fit and the two adjacent fields would overlap, so we
// model the live process address space as a single contiguous `arena` and store
// 32-bit OFFSETS-from-arena-base in those link fields — byte-exactly 4 bytes,
// exactly the arithmetic the binary performs on 32-bit pointers. Record handles
// (sub-array base, etc.) are likewise arena offsets. A null link is offset 0,
// matching the original's NULL.
//
// All link traversal goes through `Arena`, so the LRU/budget LOGIC and the
// 4-byte field layout are both reproduced 1:1.
// ---------------------------------------------------------------------------
struct Arena {
    u8*   base = nullptr;
    void* at(u32 off)  const { return off ? base + off : nullptr; }
    u32   off(const void* p) const {
        return p ? static_cast<u32>(static_cast<const u8*>(p) - base) : 0u;
    }
};

// ---------------------------------------------------------------------------
// Mesh record field offsets (from gilde.exe Hex-Rays; indices are DWORD indices
// in the decompile, e.g. v4[83] == byte offset 332).
//   +320 (idx 80) : vertex count             (a1[80])
//   +328 (idx 82) : sub-record count          (a1[82])
//   +332 (idx 83) : refcount / "busy" flag    (a1[83]) — also used as Evict skip
//   +344          : LRU key (last-use stamp)
//   +348 (idx 87) : sub-array base ptr (idx)  (a1[87])
//   +352 (idx 88) : intrusive list next       (a1[88])
//   +356 (idx 89) : intrusive list prev       (a1[89])
// Sub-record stride 192; within a sub-record:
//   +180 : buffer A ptr
//   +184 : buffer B ptr
//   +188 : buffer C ptr
// ---------------------------------------------------------------------------
namespace mesh_off {
    constexpr i32 VERT_COUNT   = 320;   // a1[80]
    constexpr i32 SUB_COUNT    = 328;   // a1[82]
    constexpr i32 REFCOUNT     = 332;   // a1[83] (Evict: skip if non-zero)
    constexpr i32 LRU_KEY      = 344;
    constexpr i32 SUBARRAY     = 348;   // a1[87]
    constexpr i32 LIST_NEXT    = 352;   // a1[88]
    constexpr i32 LIST_PREV    = 356;   // a1[89]
    constexpr i32 SUB_STRIDE   = 192;
    constexpr i32 SUB_BUF_A    = 180;
    constexpr i32 SUB_BUF_B    = 184;
    constexpr i32 SUB_BUF_C    = 188;
}

// ===========================================================================
// Hooks for the genuinely coupled leaves. nullptr field => inert default.
// ===========================================================================
struct MeshAnimHooks {
    // VIBE_Memory_FreeDebug(ptr, a, b, c). Heap free; default: noop. We pass the
    // logical pointer value (as uintptr) so tests can observe free order.
    void (*freeDebug)(u32 ptr, void* ctx) = nullptr;

    // VIBE_SceneGraph_WalkAndInvoke(root, node, cb, flags, ctx). Used by
    // FreeAttachedBuffers to flush the light cache. Default: inert, returns 0.
    char (*sceneGraphWalkAndInvoke)(u32 root, u32 node, int flags, u32 ctx,
                                    void* userctx) = nullptr;

    // VIBE_Object_ChangeTransparency(mesh, subRecPtr, value, ctx). Default: noop.
    void (*changeTransparency)(void* mesh, u32 subRecPtr, int value,
                               void* ctx) = nullptr;

    // VIBE_State_Update(handle) for AnimationFlags_Compute. Returns a record
    // pointer (modelled as a byte buffer base) or nullptr. Default: nullptr.
    void* (*stateUpdate)(u32 handle, void* ctx) = nullptr;

    void* ctx = nullptr;
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5cfd24 — VIBE_Anim_ComputeMeshMemorySize
// Pure arithmetic over the mesh record. Returns -1 when the mesh or its
// sub-array base is null. Otherwise:
//   v2  = (sub[+188]?12:0 + sub[+180]?3:0 + sub[+184]?3:0) * vertCount
//   ret = subCount * (v2 + 192) + 364
// NOTE: the original probes only the *first* sub-record's buffer presence
// (a1[87]+180/184/188) to choose the per-vertex stride, then multiplies by the
// sub-record count — translated 1:1.
// ---------------------------------------------------------------------------
i32 ComputeMeshMemorySize(const void* mesh, const Arena& arena);

// ---------------------------------------------------------------------------
// gilde.exe 0x5cfdb8 — VIBE_Anim_EvictMeshesForBudget
// LRU eviction over the intrusive mesh list [listHead .. sentinel).
//   a1 = target budget (must be >= 0), a2 = current usage.
// While usage > budget: scan the list for the entry with the smallest LRU key
// among entries whose REFCOUNT (busy flag, +332) is zero; that victim is
// released. usage -= ComputeMeshMemorySize(victim). If no eligible victim found,
// return 0 (failure). Returns 1 once usage <= budget.
//   `lruSeed` models dword_649D58 (the running-min seed, 0 in the binary; the
//   compare is `seed >= key`, so 0 only matches a key of 0 — matching the
//   original's first-iteration behavior).
// listHead / sentinel are the intrusive list endpoints (dword_13FC760 /
// &unk_13FC780). Each node's next ptr is at +352.
// ---------------------------------------------------------------------------
char EvictMeshesForBudget(i32 budget, i32 usage,
                          u32 listHead, u32 sentinel,
                          u32 lruSeed,
                          const Arena& arena,
                          const MeshAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5cfe30 — VIBE_Anim_ReleaseMeshData
// Decrement refcount (+332). If still > 0, return. Otherwise, when `unlink`
// (a2) is set: unlink from the intrusive list (prev.next = node.next;
// next.prev = node.prev), free every sub-record's three buffers (+180/+184/+188)
// across subCount sub-records, free the sub-array and the node itself (via the
// freeDebug hook). When `unlink` is clear: recompute global usage by walking the
// list, and if over budget invoke EvictMeshesForBudget; on eviction failure set
// unlink=1 and retry (the original's tail-call loop). Returns the last status.
//   budget models dword_64A094 (0x400000 in the binary).
// ---------------------------------------------------------------------------
char ReleaseMeshData(u32 mesh, bool unlink,
                     u32 listHead, u32 sentinel,
                     i32 budget,
                     const Arena& arena,
                     const MeshAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5cbfc0 — VIBE_Anim_AssignSubMeshBones
// For each of up to 3 sub-mesh slots (stride 116 starting at mesh+492+272), if
// the slot's flag at (+132 of the source record at mesh+492+244+116*i) is set:
//   - reset its 4 bone-index bytes (+112..+115) to 0xFF
//   - walk the bone-name array of the slot's anim source (count at src+324,
//     names stride 64 starting at src+64); for each bone name, walk the mesh's
//     child list (head at mesh+508, next at child+496) and for each child whose
//     name (child[+492]+180) strcmp-equals the bone name, store the bone index
//     into the next free slot (+112+n), n<4.
// Returns 0 always.
// ---------------------------------------------------------------------------
char AssignSubMeshBones(void* mesh, const MeshAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5b2ef8 — VIBE_Mesh_FreeAttachedBuffers
// Iterate 4 sub-records (stride 384) of the record at mesh+492. For each whose
// count (+252) > 0: optionally call ChangeTransparency (when its +620 byte != 0xFF
// or its +622 byte has bit0 set), zero +256 and +252, free +244 and +248 buffers.
// Then zero mesh+460 and, if any sub-record was processed, invoke the
// scene-graph walk to flush the light cache.
// ---------------------------------------------------------------------------
char FreeAttachedBuffers(void* mesh, const MeshAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x43fc38 — VIBE_Anim_FreeObjAnimDataAndReset
// Thin wrapper: calls VIBE_Anim_FreeObjAnimData(<global>, a1, a2) — modelled via
// the freeObjAnimData hook — then resets two globals (dword_62D4E8 / dword_62D4E4)
// to 0 and returns 0. (In the binary `v2` is uninitialised edx; the only
// observable, well-defined writes are the two zeroings, so we set both to 0.)
// ---------------------------------------------------------------------------
struct FreeAnimResetState {
    u32 g_62D4E8 = 0xDEADBEEF;
    u32 g_62D4E4 = 0xDEADBEEF;
};
struct FreeAnimResetHooks {
    void (*freeObjAnimData)(u32 g_13FCD1C, i32 a1, i32 a2, void* ctx) = nullptr;
    void* ctx = nullptr;
    u32 g_13FCD1C = 0;
};
i32 FreeObjAnimDataAndReset(i32 a1, i32 a2,
                            FreeAnimResetState& st,
                            const FreeAnimResetHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp
// Standard C strcmp: 0 if equal, -1 / +1 on first differing byte (sign per the
// original's `result = -v6; LOBYTE(result) |= 1`). Operates byte-wise; the
// original's word-at-a-time fast path is behaviour-identical to this.
// ---------------------------------------------------------------------------
i32 UtilStrCmp(const char* a, const char* b);

// ---------------------------------------------------------------------------
// gilde.exe 0x41dc74 — VIBE_AnimationFlags_Compute
// Flag/scale bit math. Record table at dword_69FFB4, stride 740.
//   if handle == -1 -> 0.
//   rec = table + 740*handle.  type = rec[+24].
//   if type not in {1,8,5} -> 0.
//   st = stateUpdate(rec[+8])  (a record pointer; nullptr => return 0).
//   if type in {8,5}:
//       kf = st + *(st + 4*rec[+116] + 69)   (a keyframe pointer)
//       if kf: *outW = u16(kf+6); *outH = u16(kf+10)
//       return 1
//   else (type==1):
//       *outW = u16(st+44); *outH = u16(st+46); return 1
// We expose `recTable` (the 740-stride record blob) and the stateUpdate hook.
// NOTE: in the decompile `v5` (the rec ptr reused after the State_Update call)
// is the same `rec`; the type re-check at +24 reads the same record. Translated
// faithfully: we re-read type from `rec`.
// ---------------------------------------------------------------------------
struct AnimFlagsResult {
    int ret = 0;
    u16 outW = 0;
    u16 outH = 0;
};
AnimFlagsResult AnimationFlagsCompute(i32 handle, const void* recTable,
                                      const MeshAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5d367c — VIBE_Mesh_LoadObjectAnimation (POST-LOAD TRANSFORM ONLY)
// The full routine is VFS-file-read driven (open .oam, read magic + arrays).
// The file IO itself is OMITTED (see notes / report). What is reconstructed 1:1
// is the in-memory transform the original applies to the loaded ObjAnim record
// after the arrays are read — this is pure logic:
//   - frameCount = *(u32*)(rec+0).  array base = *(u32*)(rec+56) (idx 14).
//   - fix-up last keyframe: rec.arr[frameCount-1] = rec.arr[frameCount-2]
//     (copies the leading dword; in the original it's the per-entry count field).
//   - set flag bits at rec+45/46 from `loopFlag` (a3&1): clear bit5 of +45,
//     clear bit1 of +46, then OR in (2 * (loopFlag&1)).
//   - copy 6 transform dwords from the owning object (off 76/80/84/132/136/140)
//     into rec+20/24/28/32/36/40 (idx 5..10).
//   - multiply the per-entry count field of every keyframe by 3 (stride 88).
//   - if (rec+45 & 2): set rec[1]=frameCount-1, rec[3]=arr[last].count-1,
//     rec[2]=AdvanceFrameIndex(...); else rec[3]=0, rec[2]=1, rec[1]=0.
// We model the ObjAnim record (0x3C bytes header + keyframe array) and the
// owning-object transform block as byte buffers. AdvanceFrameIndex is routed
// through a hook (default: returns the seed value).
// ---------------------------------------------------------------------------
namespace oam_off {
    constexpr i32 FRAME_COUNT   = 0;    // rec[0]
    constexpr i32 IDX1          = 4;    // rec[1]
    constexpr i32 IDX2          = 8;    // rec[2]
    constexpr i32 IDX3          = 12;   // rec[3]
    constexpr i32 XFORM0        = 20;   // rec[5]  (float)
    constexpr i32 XFORM1        = 24;   // rec[6]
    constexpr i32 XFORM2        = 28;   // rec[7]
    constexpr i32 XFORM3        = 32;   // rec[8]
    constexpr i32 XFORM4        = 36;   // rec[9]
    constexpr i32 XFORM5        = 40;   // rec[10]
    constexpr i32 FLAG45        = 45;
    constexpr i32 FLAG46        = 46;
    constexpr i32 ARR_PTR       = 56;   // rec[14]
    constexpr i32 KF_STRIDE     = 88;   // keyframe stride; +0 = per-entry count
    // owning-object transform source offsets:
    constexpr i32 OBJ_X0 = 76, OBJ_X1 = 80, OBJ_X2 = 84;
    constexpr i32 OBJ_X3 = 132, OBJ_X4 = 136, OBJ_X5 = 140;
}
struct ObjAnimHooks {
    // VIBE_Anim_AdvanceFrameIndex(b1, idx1, lastFrame, zero, frameCount).
    // Default: returns `idx1` (a benign passthrough used only when bit1 set).
    i32 (*advanceFrameIndex)(u8 b1, i32 idx1, i32 lastFrame, i32 zero,
                             i32 frameCount, void* ctx) = nullptr;
    void* ctx = nullptr;
};
// Applies the post-load transform in place. `objBlock` is the owning object's
// byte buffer (transform source). `loopFlag` is a3 from the original.
void LoadObjectAnimation_ApplyTransform(void* oamRec, const void* objBlock,
                                        u8 loopFlag,
                                        const ObjAnimHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x41f5e0 — VIBE_Shape_LoadAndRegister (REGISTRATION LOGIC ONLY)
// The full routine is a VFS-file-read driver: it opens a shape file, seeks to
// end to size it, allocates a free-list block, reads the whole file in, then
// registers the shape into a global 84-stride table (dword_62D204 base,
// dword_62D208 count) and frees the block. The file IO + free-list alloc + VFS
// close are OMITTED (see report) — those are pure VFS/heap leaves outside this
// cluster. What is reconstructed 1:1 here is the table-registration logic, which
// is pure table manipulation over an already-loaded shape blob:
//   - classify the blob (via hook: VIBE_Shape_ClassifyType) and store the type
//     at table[idx]+60.
//   - if type in {1,4,5,8}: bounds = CoordTransform(blob,0) (hook); copy
//     u16@bounds+6 -> +80, u16@bounds+10 -> +82, store idx at +76.
//   - if type == 17: copy u16@blob+12 -> +80, u16@blob+14 -> +82.
//   - store blob size at +56; copy the NUL-terminated name (byte-pair loop)
//     into table[idx]+0.
//   - flags: t = table[idx]+68; t = (t|1) & 0xFD; store at +68.
//   - if type in {5,8}: register (blob[+42]-1) extra sub-frames at successive
//     table slots, each named "..." (asc_611284), with bounds from
//     CoordTransform(blob, k+1), inheriting the type from the first slot and
//     storing the first slot's index at +76.
// Returns the new table count via a State helper (hook). `nameSrc` is the a2
// name argument (stride-2 byte stream, as the original copies a2[0],a2[1],... ).
// ---------------------------------------------------------------------------
namespace shape_off {
    constexpr i32 STRIDE   = 84;
    constexpr i32 NAME     = 0;
    constexpr i32 SIZE     = 56;
    constexpr i32 TYPE     = 60;
    constexpr i32 FLAGS    = 68;
    constexpr i32 PARENT   = 76;   // owning/first-slot index
    constexpr i32 BOUND_W  = 80;
    constexpr i32 BOUND_H  = 82;
}
struct ShapeRegHooks {
    // VIBE_Shape_ClassifyType(blob) -> type byte. Default: 1.
    u8  (*classifyType)(const void* blob, void* ctx) = nullptr;
    // VIBE_Coord_Transform(blob, idx) -> bounds record pointer. The original:
    //   result += *(u32*)(result + 4*idx + 69). Default models this directly.
    const void* (*coordTransform)(const void* blob, u16 idx, void* ctx) = nullptr;
    void* ctx = nullptr;
};
// Registers `blob` (size `blobSize`) into `table` starting at slot `startIndex`.
// Returns the new slot count (startIndex + number of slots written). `nameSrc`
// is the stride-2 name byte stream; if null, the name copy is skipped.
i32 Shape_RegisterLoaded(void* table, i32 startIndex,
                         const void* blob, u32 blobSize,
                         const char* nameSrc,
                         const ShapeRegHooks& hooks);

} // namespace guild::render::anim_recon4
