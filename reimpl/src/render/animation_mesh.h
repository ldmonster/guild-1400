#pragma once
#include "guild/common/types.h"

#include <cstddef>

// =============================================================================
// guild::render — VIBE_Animation_* / VIBE_Mesh_* loader + anim/AABB LEAVES.
//
// Faithful 1:1 reconstruction of a slice of the deterministic loader/animation/
// bounding-volume leaves in gilde.exe (imagebase 0x400000). These are the
// untranslated leaves NOT already covered by the sibling render files
// (animation_*.cpp, mesh_*.cpp, bgf_loader.cpp, skeleton*.cpp):
//
//   0x5D9774  VIBE_Animation_GetPtr              (anim-name -> registry entry)
//   0x5D89BC  VIBE_Animation_Advanced            (advanced-frame flag toggle)
//   0x5D1020  VIBE_Mesh_SetActiveTexturePath     (set CurTexSet dir context)
//   0x5D1034  VIBE_Mesh_BuildTexturePath         ("*"+CurTexSet+name -> vfs path)
//   0x5D15FC  VIBE_Mesh_BuildLodFileName         (LOD/base filename builder)
//   0x5B4944  VIBE_Mesh_MarkAllFramesDirty       (re-precache every frame texture)
//   0x429070  VIBE_Mesh_ApplyTransformRecursive  (recursive geometry inflate)
//   0x5F5628  VIBE_Mesh_AccumulateMemoryCallback (scene-walk memory accumulator)
//   0x4283AC  VIBE_Mesh_AccumulateAabbRecursive  (min/max AABB over 8 corners)
//   0x427820  VIBE_Mesh_TestAabbOverlapRecursive (AABB overlap + child recursion)
//   0x5D20DC  VIBE_Mesh_SaveTextureSet           (.TXS texture-set writer)
//   0x5D2240  VIBE_Mesh_LoadTextureSet           (.TXS texture-set reader)
//
// Every callee into an unreconstructed module is routed through the installable
// AnimMeshHooks struct (inert defaults defined in animation_mesh.cpp), so each
// record/math path is exact and independently testable. Tests install captors.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// .TXS texture-set file magic (gilde.exe immediate 603064750 == 0x23F209AE,
// written/read as the first dword-pair).
constexpr u32 kTxsMagic = 0x23F209AEu;   // 603064750

// AABB seed sentinels — VIBE_Mesh_ComputeObjectAabb seeds min=+1e10, max=-1e10
// (gilde.exe immediates 1343554297 / -803929351 reinterpreted as float).
constexpr float kAabbSeedMin =  1.0e10f;  // 0x501502F9
constexpr float kAabbSeedMax = -1.0e10f;  // 0xD01502F9

// VIBE_Mesh_BuildTexturePath path prefix (unk_628F14 = "*").
constexpr char  kTexPathPrefix = '*';
// LOD/base filename suffix (aS_8 = "_s") and the sprintf format (aSI_1).
inline constexpr const char* kLodBaseSuffix = "_s";
inline constexpr const char* kLodFmt        = "%s_%i";

// ---------------------------------------------------------------------------
// AnimRegistryEntry — one 128-byte slot of the global animation registry
// (dword_1406A80 = count, dword_1406A84 = base). The original compares the
// caller's name against entry+0 case-insensitively and skips entries whose
// frame-count field (+64) is <= 0.
// ---------------------------------------------------------------------------
struct AnimRegistryEntry {
    char name[64];   // +0   animation name (compared case-insensitively)
    i32  frameCount; // +64  number of frames (entry valid only if > 0)
    u8   rest[60];   // +68  remainder (128-byte stride)
};

// ---------------------------------------------------------------------------
// AabbNode — the subset of the scene-graph object node the AABB-recursion
// leaves read. Faithful to the recovered byte offsets; only touched fields are
// named (the rest is implied by the recursion over firstChild / nextSibling).
//   +460 (drawData)   model block; **(int*)+460 = corner-vertex array base,
//                      +8 = base-vertex index, +12 = sub-object count, +4 = subs
//   +496 (nextSibling) intrusive sibling link
//   +508 (firstChild)  first child link
//   +533 (nodeType)    == 4 means "mesh" (has geometry to accumulate)
// The 8 AABB corner vertices live at meshBase + 80*baseIndex, stride 80 floats
// (the engine's 320-byte transformed-vertex record), x/y/z at offsets 0/1/2.
// ---------------------------------------------------------------------------
struct AabbCorner { float x, y, z; };

struct AabbNode {
    bool        hasMesh   = false;  // nodeType(+533) == 4
    AabbCorner  corners[8];         // the 8 transformed bounding-box corners
    AabbNode*   firstChild = nullptr; // +508
    AabbNode*   nextSibling = nullptr; // +496
};

// Axis-aligned box accumulator (min/max each axis).
struct Aabb {
    float mn[3];
    float mx[3];
};

// ---------------------------------------------------------------------------
// Mockable hooks for the unreconstructed leaves the originals call. The default
// implementations are inert (the record/math logic still runs faithfully).
// ---------------------------------------------------------------------------
struct AnimMeshHooks {
    // VIBE_Vfs_NormalizeDirPath(dir, ctx) — returns a context handle stored in
    // dword_1406110 (CurTexSet). Default returns 0.
    int  (*normalizeDirPath)(const char* dir) = nullptr;
    // VIBE_Vfs_ResolveAndBuildPath(name, ctx) — resolves "*..." into a real
    // path; returns nullptr if the asset does not exist. Default returns name.
    const char* (*resolvePath)(const char* name) = nullptr;
    // VIBE_Object_InflateGeometry(node) — per-node geometry inflate. Default
    // increments a call counter (observable via AnimMeshInflateCalls()).
    char (*inflateGeometry)(AabbNode* node) = nullptr;
    // VIBE_Render_PrecacheTexture(tex, frameSlot, frameIndex) — default no-op,
    // counts calls (AnimMeshPrecacheCalls()).
    void (*precacheTexture)(int frameIndex) = nullptr;
    // VIBE_FrameData_Process(a1, a2, framePtr, a4) — the per-frame processor
    // VIBE_Animation_Advanced delegates to with the temporary mode byte forced
    // to 3. Default no-op, counts calls (AnimMeshFrameProcessCalls()).
    void (*frameProcess)(void* frame) = nullptr;
};
void AnimMeshSetHooks(const AnimMeshHooks& hooks);
void AnimMeshResetHooks();
int  AnimMeshInflateCalls();
int  AnimMeshPrecacheCalls();
int  AnimMeshFrameProcessCalls();

// ===========================================================================
// 0x5D89BC — VIBE_Animation_Advanced.
//   Bounds-check frameIndex against the clip's frame count (+42). Locate the
//   frame sub-block (base + offset stored at base + 4*frameIndex + 69), save
//   its mode byte (+13), force it to 3, run FrameData_Process over the frame,
//   then restore the saved byte. Returns 0 if out of range, else 1.
// Reconstructed against an AdvancedClip view of the same byte layout.
// ===========================================================================
struct AdvancedFrame {
    u8   pad[13];
    u8   mode;     // +13  processing mode (temporarily forced to 3)
    u8   rest[2];
};
struct AdvancedClip {
    u16            frameCount;       // +42  number of frames
    AdvancedFrame* frames[256];      // resolved frame sub-blocks
    int            frameCountField;  // mirrors frameCount for the guard
};
// Returns the mode byte observed by frameProcess during the call (or -1 if the
// index was out of range), so tests can prove the 3-forcing happened.
int  AnimationAdvanced(AdvancedClip& clip, unsigned frameIndex);

// ===========================================================================
// 0x5D9774 — VIBE_Animation_GetPtr.
//   Copy the caller name (inlined 2-byte strcpy), then linear-scan the global
//   animation registry of `count` 128-byte entries; return the first whose
//   frameCount(+64) > 0 AND whose name matches case-insensitively. 0 if none.
// ===========================================================================
AnimRegistryEntry* AnimationGetPtr(const char* name, AnimRegistryEntry* table,
                                   u32 count);

// ===========================================================================
// 0x5D1020 — VIBE_Mesh_SetActiveTexturePath.
//   dword_1406110 = VIBE_Vfs_NormalizeDirPath(dir, CurTexSetCtx). Returns it.
// ===========================================================================
int  MeshSetActiveTexturePath(const char* dir);

// ===========================================================================
// 0x5D1034 — VIBE_Mesh_BuildTexturePath.
//   tmp = "*" (prefix) + a1 + a2; return VIBE_Vfs_ResolveAndBuildPath(tmp).
//   `out` receives the assembled "*..."+a1+a2 string (>= 272 bytes).
// ===========================================================================
const char* MeshBuildTexturePath(char* out, const char* a1, const char* a2);

// ===========================================================================
// 0x5D15FC — VIBE_Mesh_BuildLodFileName.
//   lodMode = g_lodMode (signed byte). v6 = lodMode & 0x7F.
//   a4 <  0 : "base" request. If lodMode >= 0 -> 0. Else copy base->out and
//             append "_s" (and base2->out2 + "_s" when present); return 1.
//   a4 == 0 : v6==2 -> probe "%s_%i" indices 1..0 via MeshBuildTexturePath(".bgf")
//             until one resolves; on miss fall back to plain copy. else plain
//             copy base->out (+ base2->out2); return 1.
//   a4 >  0 : if v6==2, a4 = 2-a4; idx = a4-1; sprintf "%s_%i"; return 1.
//   `out`/`out2` must be sized for the longest assembled name.
// ===========================================================================
char MeshBuildLodFileName(const char* base, const char* base2, char* out,
                          int lod, char* out2, signed char lodMode);

// ===========================================================================
// 0x5B4944 — VIBE_Mesh_MarkAllFramesDirty.
//   Walk the frame array (count = model[+480]); for every live frame that has a
//   texture(+100) and is not already dirty(+104 bit7), precache it and set the
//   dirty bit. Reconstructed against a flat FrameEntry array for testability.
// ===========================================================================
struct FrameEntry {
    int  texture;  // +100  texture handle (0 = no texture, skipped)
    u8   flags;    // +104  bit7 = "dirty / precached"
};
void MeshMarkAllFramesDirty(FrameEntry* frames, int count);

// ===========================================================================
// 0x429070 — VIBE_Mesh_ApplyTransformRecursive.
//   result = InflateGeometry(node); then for each child via firstChild(+508),
//   stepping nextSibling(+496), recurse. Returns last InflateGeometry result.
// ===========================================================================
char MeshApplyTransformRecursive(AabbNode* node);

// ===========================================================================
// 0x4283AC — VIBE_Mesh_AccumulateAabbRecursive.
//   If node has mesh: for each of the 8 corner verts, expand mn (min) / mx
//   (max) per axis. Then recurse into children. mn/mx are the running box.
// ===========================================================================
void MeshAccumulateAabbRecursive(Aabb& box, AabbNode* node);

// ===========================================================================
// 0x5F5628 — VIBE_Mesh_AccumulateMemoryCallback.
//   if ComputeSceneMemorySize(node, kind, &tmp) -> *accumulator += tmp.
//   The size computation is a separate reconstructed leaf; this models the
//   accumulate step exactly. Returns 1.
// ===========================================================================
char MeshAccumulateMemoryCallback(int* accumulator, int nodeSize);

// ===========================================================================
// 0x427820 — VIBE_Mesh_TestAabbOverlapRecursive (geometry-overlap subset).
//   For a mesh node, compute its own world AABB (min/max over the 8 corners)
//   and test it against the query box `q`. Returns 0 if this node is a mesh
//   (it consumed the query), 1 otherwise; ANDs in the result of every child.
//   On overlap, `outBox` (if non-null) is GROWN per-axis by the node's box.
// ===========================================================================
int MeshTestAabbOverlapRecursive(const Aabb& q, AabbNode* node, Aabb* outBox);

// ===========================================================================
// .TXS texture-set serialization (0x5D20DC writer / 0x5D2240 reader).
// A texture set is a rows x cols grid of 64-byte name strings. The on-disk
// format is, all dword-pairs / strings via the Bio layer:
//   [magic=0x23F209AE][rows][cols] then rows*cols * 64-byte strings.
// Reconstructed against an in-memory MeshTextureSet for golden round-tripping.
// ===========================================================================
struct MeshTextureSet {
    int rows = 0;
    int cols = 0;
    char names[256][64];  // [row*cols + col][64]; capacity-bounded
    char* at(int r, int c) { return names[r * cols + c]; }
    const char* at(int r, int c) const { return names[r * cols + c]; }
};

// Serialize `ts` into `buf` (caller-sized). Returns the number of bytes written.
// Layout matches VIBE_Mesh_SaveTextureSet's Bio writes exactly.
std::size_t SaveTextureSet(const MeshTextureSet& ts, u8* buf, std::size_t cap);

// Parse a .TXS blob written by SaveTextureSet into `ts`. Returns true on a
// valid magic with rows>0 && cols>0 (matches VIBE_Mesh_LoadTextureSet's guard).
bool LoadTextureSet(const u8* buf, std::size_t len, MeshTextureSet& ts);

} // namespace guild::render
