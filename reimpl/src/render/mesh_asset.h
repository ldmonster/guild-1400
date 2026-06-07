#pragma once
#include "guild/common/types.h"
#include "render/bgf_loader.h"
#include "render/mesh_load.h"
#include "render/model_io.h"
#include "render/anim_load.h"
#include "io/vfs.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// guild::render — VFS-backed by-name asset loaders + the model/anim asset cache.
//
// The already-reconstructed binary parsers (bgf_loader.cpp LoadFastChunk,
// mesh_load.cpp LoadBgfFile, anim_load.cpp LoadBinaryAnimation, model_io.cpp
// LoadSyntheticModel) all consume a FLAT byte buffer. In the original those bytes
// were streamed straight out of the VFS:
//
//   0x5D32D4  VIBE_Mesh_LoadAndRegister     — BuildTexturePath("*"+name+".bgf") ->
//                                             VIBE_Mesh_LoadBgfFile -> link into the
//                                             stock-object registry (dword_13FCAEC
//                                             head; +508 back-link / +512 next).
//   0x5D345C  VIBE_Mesh_LoadOrFindByName    — FindStockObject (cache hit by name,
//                                             VIBE_Util_StrCmpNoCaseN 63) else
//                                             LoadAndRegister.
//   0x5D3550  VIBE_Mesh_LoadOrAddRefByName  — same, returning an int handle/refcount.
//   0x5D10D0  VIBE_Mesh_FindStockObject     — the cache lookup itself (walk the
//                                             registry list comparing name, 63 chars,
//                                             case-insensitive).
//   0x5F87B8  VIBE_Model_LoadFastChunk      — the fast-chunk binary reader.
//   0x5E450C  VIBE_ModelIo_LoadBinaryAnimation — the .anim/.oam streamer.
//
// This module supplies the THIN VFS-open wrappers around those existing cores plus
// the by-name asset cache. It does NOT redefine the parsers (ODR): it opens the
// named file through guild::io::VfsOpenFile (which itself reads through the host
// shim::IFileSystem), slurps the whole stream into a buffer, and hands it to the
// existing LoadBgfFile / LoadFastChunk / LoadBinaryAnimation.
//
// The original resolved a leaf name into a real path via VIBE_Vfs_ResolveAndBuildPath
// (the search-list + "*"-prefix path-tree walker, deferred — see io/vfs.h). Here the
// wrappers open the name (or a caller-built path) directly through the VFS; the
// material/texture resolution that LoadBgfFile triggers per material runs unchanged
// (it calls render::TextureLoadByName, now the real VFS+decode in texture_asset).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Read an entire VFS stream into a byte buffer (seek-to-end / tell / rewind /
// read). Returns false if the path cannot be opened. Mirrors the open+read+close
// idiom the loaders use (VIBE_Vfs_OpenFile / Tell / Seek / ReadStream / Close).
// ---------------------------------------------------------------------------
bool VfsSlurp(const char* path, std::vector<u8>& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x5D2348 — Mesh_LoadByName.
// Open `path` through the VFS, slurp it, and run the full VIBE_Mesh_LoadBgfFile
// post-process pipeline (vertex dedup, morph-bake, material dedup + texture
// resolution, bounds/normals) into `out`. Returns false on open/parse failure.
//   name : the mesh name stored in the Mesh header (the upper-cased path in the
//          original). If empty, `path` is used.
// ---------------------------------------------------------------------------
bool Mesh_LoadByName(const char* path, const std::string& name, Mesh& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x5F87B8 — Model_LoadByName (fast-chunk reader, VFS form).
// Open `path`, slurp it, parse the fast-chunk record into `out`. Returns false on
// open/parse failure. This is the low-level fast-chunk reader without the outer
// post-process; use Mesh_LoadByName for the full orchestrator.
// ---------------------------------------------------------------------------
bool Model_LoadByName(const char* path, BgfModel& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x5E450C — Anim_LoadByName (binary animation, VFS form).
// Open `path`, slurp it, run VIBE_ModelIo_LoadBinaryAnimation into `out`.
// ---------------------------------------------------------------------------
bool Anim_LoadByName(const char* path, const std::string& name, u8 loadFlag,
                     Animation& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x5F9558 — Model_WriteFastChunk (the WRITE side of FastChunkIo).
// Serialize a parsed BgfModel back into the .BGF fast-chunk byte layout, exactly
// in the field order VIBE_Model_FastChunkIo emits via the VIBE_Bio_Write* block:
//   byte 'd' tag? -> here we emit the bare fast-chunk record the reader expects:
//     '-' (chunk tag) u32 chunkSize u32 magic(0xFAB7E6C5)
//     u32 materialCount, u32 vertexCount, u32 polyCount
//     (vertexCount+8) * { vec3 pos, vec3 normal }
//     u32 objectFlags
//     polyCount * { u32 vtx[3], vec3 uv0, vec3 uv1, vec3 uv2,
//                   matIndex (byte if materialCount<=254 else u32) }
//     materialCount * { string name0, name1, name2, 6 flag bytes }
//     u32 dummyCount, dummyCount * { string name, vec3 pos, vec3 rot }
// The bytes produced round-trip through LoadFastChunk byte-for-byte.
// ---------------------------------------------------------------------------
std::vector<u8> Model_WriteFastChunk(const BgfModel& m);

// ---------------------------------------------------------------------------
// Model / anim asset cache (models the stock-object registry dword_13FCAEC /
// VIBE_Mesh_FindStockObject). Keyed by the upper-cased name; a re-load returns
// the SAME handle (a stable pointer into the cache) rather than re-parsing.
// ---------------------------------------------------------------------------
class MeshAssetCache {
public:
    // Look up (case-insensitive, 63 chars, like VIBE_Util_StrCmpNoCaseN) an
    // already-loaded mesh; nullptr if not present.
    Mesh* Find(const std::string& name);

    // gilde.exe 0x5D345C — load-or-find. On a cache hit returns the existing
    // handle; otherwise opens `path` via the VFS, parses it, caches and returns
    // it. Returns nullptr on load failure.
    Mesh* LoadOrFind(const char* path, const std::string& name);

    std::size_t size() const { return order_.size(); }

private:
    static std::string Key(const std::string& name);
    std::unordered_map<std::string, std::unique_ptr<Mesh>> map_;
    std::vector<std::string> order_;  // mirrors the registry link order
};

} // namespace guild::render
