#pragma once
#include "guild/common/types.h"

#include <string>

// =============================================================================
// guild::render — the stock-object LOD filename strategy + the load-or-find LOD
// orchestrator + the object-node LOD attach, reconstructed 1:1 from gilde.exe.
//
//   0x5d1034  VIBE_Mesh_BuildTexturePath       — build "*"+name+suffix, resolve it
//                                                 through the VFS, return truthy
//                                                 when the resolved file exists.
//   0x5d15fc  VIBE_Mesh_BuildLodFileName       — PURE string strategy: pick the
//                                                 base / "_s" / "_<lod>" leaf name
//                                                 per the LOD-mode byte byte_64A098.
//   0x5d345c  VIBE_Mesh_LoadOrFindByName       — find-or-load the base, then the
//                                                 "_s" variant, then LOD 1/2 per
//                                                 the mode (orchestrates the cache).
//   0x5d1824  VIBE_Mesh_AttachStockObjectLods  — attach the cached stock objects'
//                                                 LOD meshes onto an object node's
//                                                 draw-data block.
//
// LOD-mode byte (byte_64A098): low 7 bits select the strategy
//   0 / 1 : normal / multi-LOD  (mode 1 additionally loads LOD frames 1 and 2)
//   2     : switch-LOD          (probe "%s_<n>" downward for an existing .bgf;
//                                LOD index is FLIPPED: a4 -> 2-a4)
// high bit (sign): LOD ENABLED. When clear (byte >= 0 as i8) the "_s" variant is
//   disabled (BuildLodFileName(a4<0) returns 0).
//
// The pure string formatting + branch logic is reconstructed here verbatim. The
// two engine-coupled leaves — the actual VFS file-existence probe (the original's
// VIBE_Vfs_ResolveAndBuildPath) and the scene-graph draw-block writes that
// AttachStockObjectLods performs — are routed through MeshLodHooks, an installable
// hook block with INERT DEFAULTS (mesh_lod_name.cpp) so the strategy and the
// attach structure are golden-testable in a headless build with no assets.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// LOD-mode state. Mirrors byte_64A098 (a single process-global byte in the
// original). LodModeMut() exposes it for tests / the LOD config path.
// ---------------------------------------------------------------------------
u8&  LodModeByteMut();
u8   LodModeByte();

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in mesh_lod_name.cpp).
// ---------------------------------------------------------------------------
struct MeshLodHooks {
    // VIBE_Mesh_BuildTexturePath leaf: resolve "*"+name+suffix through the VFS and
    // return true iff the resolved file exists. Default: false (no asset present).
    //   name   : the leaf mesh name passed to BuildTexturePath (a1).
    //   suffix : the appended suffix (a2), e.g. ".bgf".
    bool (*textureExists)(const char* name, const char* suffix) = nullptr;

    // VIBE_Mesh_AttachStockTextures leaf (0x5d1114): attach the named stock object's
    // mesh + textures into the object's draw block at `drawBlockOffset` (the byte
    // offset from the object pointer the original writes: +244, +1396, or
    // +244+384*lod). `lodArg` is the per-LOD argument the original threads through
    // (a4 for the base, the running 384*lod offset for LOD frames). Returns whether
    // the stock object was found+attached. Default: a no-op returning false.
    bool (*attachStockTextures)(void* object, int drawBlockOffset, int lodArg,
                                const char* stockName) = nullptr;

    // VIBE_Object_AllocDrawData (0x5b107c): allocate the object's draw-data block
    // when *(object+492) is null. Default: no-op.
    void (*allocDrawData)(void* object) = nullptr;
};

MeshLodHooks&       MeshLodHooksMut();
const MeshLodHooks& LodHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1034 — VIBE_Mesh_BuildTexturePath.
// Builds "*" + name + suffix and routes it through the VFS-existence hook. The
// path FORMATTING is pure (composed into `outPath` when non-null); the existence
// decision is the hook (false by default). Returns true iff the file exists.
//   name   : leaf mesh name (a1).
//   suffix : appended suffix (a2), e.g. ".bgf".
//   outPath: optional out buffer receiving the composed "*"+name+suffix string.
// ---------------------------------------------------------------------------
bool BuildTexturePath(const char* name, const char* suffix, std::string* outPath);

// ---------------------------------------------------------------------------
// gilde.exe 0x5d15fc — VIBE_Mesh_BuildLodFileName (PURE string strategy).
//   a1 (name)       : the base mesh name.
//   a2 (secondName) : optional parallel name (the path/dir-key the original carries
//                     alongside, written into a5); may be null.
//   a3 (out)        : primary out buffer (caller-owned, >= 256 bytes like the
//                     original's 256-byte stack buffers).
//   a4 (lodIndex)   : < 0  -> the "_s" suffix variant; 0 -> base; > 0 -> LOD i.
//   a5 (outSecond)  : optional parallel out buffer for a2; may be null.
// Returns 1 when a name was produced, 0 when none (LOD disabled "_s" / mode-2 probe
// exhausted). The mode-2 base case probes "%s_<n>" downward via BuildTexturePath.
// ---------------------------------------------------------------------------
u8 BuildLodFileName(const char* name, const char* secondName, char* out,
                    int lodIndex, char* outSecond);

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1824 — VIBE_Mesh_AttachStockObjectLods.
// Attach the cached stock object(s) for `name` onto `object`'s draw-data block.
//
// Faithful structure (object byte offsets cited from 0x5d1824):
//   * `attachExisting` mirrors the original's `if (a2)` fast path: when set, the
//     stock object for `name` is looked up directly and attached at drawData+244.
//   * Otherwise BuildLodFileName(name, 0, buf, 0, 0) yields the base name, which is
//     attached at drawData+244 (object+492 is the draw-data ptr; AllocDrawData runs
//     first when it is null).
//   * The "_s" variant (BuildLodFileName ..., -1, ...) is attached at drawData+1396.
//   * In multi-LOD mode (byte_64A098 & 0x7F == 1) LOD frames 1..2 are attached at
//     drawData + 244 + 384*lod (the 384-byte LOD frame stride), advancing the
//     running offset by 384 per successfully-attached frame, while the offset stays
//     below 1152 (== 244+384*... cap, i.e. 3 frames).
// All draw-block writes go through MeshLodHooks (allocDrawData / attachStockTextures)
// with inert defaults; `object` is an opaque pointer the hook interprets.
// Returns the last attach result byte (the original's al return).
// ---------------------------------------------------------------------------
u8 AttachStockObjectLods(void* object, bool attachExisting, const char* name,
                         int lodArg);

} // namespace guild::render
