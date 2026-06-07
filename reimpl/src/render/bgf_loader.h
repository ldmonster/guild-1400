#pragma once
#include "render/geometry_types.h"
#include <cstddef>
#include <string>
#include <vector>

// =============================================================================
// guild::render — .BGF "fast chunk" binary mesh loader.
//
// Faithful 1:1 reconstruction of the binary parser at the heart of the .BGF
// model pipeline in gilde.exe (d3_io.c / d3_io_fl.c):
//
//   0x5F87B8  VIBE_Model_LoadFastChunk   (the binary fast-chunk reader)
//   0x5F86FC  VIBE_Vfs_FindChunkStart    (scan the script token stream to the
//                                         fast-chunk record, verify the magic)
//   0x5E44B4  VIBE_ModelIo_ReadChunkTag  (read the 4-byte version tag + dispatch)
//   0x5DC850.. VIBE_Bio_Read{Byte,Dword,Vec3,String}  (raw little-endian readers)
//   0x5F9558  VIBE_Model_FastChunkIo     (the WRITE side — used here as the
//                                         authoritative field-order reference)
//
// THE .BGF FAST-CHUNK FORMAT (recovered byte-for-byte from the read + write paths)
// -----------------------------------------------------------------------------
// A .BGF file is a script token stream. VIBE_Vfs_FindChunkStart walks it:
//   * first 4 bytes are skipped (a leading tag dword),
//   * then it reads single-byte tokens via VIBE_Script_ReadToken:
//       byte > 0x3A           -> token '\'' (0x27)  (a string/name token)
//       byte == '-' (0x2D)    -> a chunk header follows; stop scanning
//       byte == '+' (0x2B)    -> end of stream (also returned on EOF) -> fail
//       any other byte        -> a sized block: read u32 size, seek past it
//   * at a '-' chunk: read u32 chunkSize, then u32 magic. If magic ==
//     0xFAB7E6C5 this is the fast-chunk; otherwise seek back (size-4) and keep
//     scanning. On match, the stream is positioned right after the magic.
//
// Immediately after the magic, VIBE_Model_LoadFastChunk reads (all u32 LE):
//   u32 materialCount            ; -> model +0x1E0 (field index 120)
//   u32 vertexCount              ; -> model +0x44  (field index 17)
//   u32 polyCount                ; -> model +0x4C  (field index 19)
//   vertex block: (vertexCount + 8) records of:
//       float pos[3]   (12 bytes)   ; the +8 is padding/extra slots the engine
//       float normal[3](12 bytes)   ; always allocates (24*(n+8) bytes)
//   u32 objectFlags              ; -> model +0x1D4 (field index 117) (ReadDword)
//   poly block: polyCount records of 56 bytes:
//       u32  vtxIndex[3]   @ +24/+28/+32   (indices into the vertex block)
//       float uv0[3]       @ +0  (u,?,?)   read as a Vec3 into +0/+8/+16
//       float uv1[3]       @ +4            read as a Vec3 into +4/+12/+20
//       float uv2[3]       @ +44           read as a Vec3 into +44/+48/+52
//       i32   matIndex     @ +40           material index; if materialCount<=254
//                                          it is a single byte (0xFF -> -1),
//                                          else a full u32.
//       (+36 is the resolved texture id, set to -1 here / by the texture stage.)
//   material block: materialCount records, each:
//       string name0, string name1, string name2   (NUL-terminated)
//       u8 flag, u8 a, u8 b, u8 c, u8 d, u8 e       (6 flag/format bytes)
//   u32 dummyCount               ; -> model +0x208 (byte at +0x208)
//   dummy block: dummyCount records of 88 bytes:
//       string name (64 bytes incl. NUL region), float pos[3], float rot[3]
//
// This reconstruction parses that format straight out of a flat byte buffer (the
// production code streamed it through the VFS; the field order and sizes are
// identical). It fills a Bgf model with the engine's own strides so the geometry
// pipeline (mesh.cpp / scene.cpp) can consume it. Texture resolution, vertex/
// material dedup, the morph-rotation bake (dbl_6290CC..) and bounds/normal recompute
// are SEPARATE stages (VIBE_Texture_LoadByName, VIBE_Mesh_Compute*) and are
// intentionally not part of this loader — see report.
// =============================================================================
namespace guild::render {

// Magic dword that marks the fast-chunk record (gilde.exe: -88801275).
constexpr u32 kBgfFastChunkMagic = 0xFAB7E6C5u;

// Script token bytes used by VIBE_Vfs_FindChunkStart / VIBE_Script_ReadToken.
constexpr u8 kBgfTokenChunk = 0x2D;  // '-'  chunk header follows
constexpr u8 kBgfTokenEnd   = 0x2B;  // '+'  end of stream / EOF
constexpr u8 kBgfTokenName  = 0x27;  // '\'' returned for any byte > 0x3A

// One raw vertex as stored in the fast-chunk vertex block (24 bytes).
struct BgfVertex {
    float pos[3];     // +0x00  model-space position
    float normal[3];  // +0x0C  vertex normal (as stored; recomputed elsewhere)
};

// One raw polygon as stored in the fast-chunk poly block (56 bytes).
struct BgfPolygon {
    float uv0[3];     // +0x00  per-corner UV/offset row 0 (read as a Vec3)
    float uv1[3];     // +0x04 base; interleaved with uv0 (read as a Vec3)
    u32   vtx[3];     // +0x18/+0x1C/+0x20  vertex indices
    i32   texId;      // +0x24  resolved texture id (-1 until the texture stage)
    i32   matIndex;   // +0x28  material index (byte or u32 per materialCount)
    float uv2[3];     // +0x2C  per-corner UV/offset row 2 (read as a Vec3)
};

// One "dummy"/locator entry (88 bytes): a named position+rotation marker.
struct BgfDummy {
    char  name[64];   // +0x00  NUL-terminated name (the slack is part of the record)
    float pos[3];     // +0x40  position
    float rot[3];     // +0x4C  rotation
};

// One material entry as stored in the fast-chunk material block.
struct BgfMaterial {
    std::string name0;  // base/diffuse texture name
    std::string name1;  // secondary name
    std::string name2;  // tertiary name
    u8 flag;            // present/format flag (v60)
    u8 b1;              // v61
    u8 b2;              // v62 (bit fields: blend/2-sided)
    u8 b3;              // v64
    u8 b4;              // v67
    u8 b5;              // v68[0]
};

// A loaded .BGF model.
struct BgfModel {
    u32 materialCount = 0;
    u32 vertexCount   = 0;   // logical count (the block stores count + 8 slots)
    u32 polyCount     = 0;
    u32 objectFlags   = 0;   // the +0x1D4 dword
    u32 dummyCount    = 0;

    std::vector<BgfVertex>   vertices;   // size = vertexCount + 8 (engine allocates +8)
    std::vector<BgfPolygon>  polygons;
    std::vector<BgfMaterial> materials;
    std::vector<BgfDummy>    dummies;
};

// gilde.exe 0x5F86FC — VIBE_Vfs_FindChunkStart.
// Scans `data` (size `size`) as the script token stream; on success sets
// `*chunkOffset` to the byte offset just past the verified fast-chunk magic and
// returns true. Returns false on EOF / '+' / a missing magic.
bool BgfFindChunkStart(const u8* data, size_t size, size_t* chunkOffset);

// gilde.exe 0x5F87B8 — VIBE_Model_LoadFastChunk (buffer form).
// Finds the fast-chunk via BgfFindChunkStart, then parses the vertex / poly /
// material / dummy blocks into `out`. Returns false if the chunk is missing or
// the buffer is truncated mid-record. The vertex array is sized vertexCount+8
// exactly as the engine allocates (24*(n+8) bytes).
bool LoadFastChunk(const u8* data, size_t size, BgfModel& out);

// Build engine-stride geometry (Vertex[80] / Polygon[40]) from a parsed BgfModel.
// Copies positions into Vertex.x/y/z, UV0 into Vertex.u/v, and resolves the
// polygon vertex indices into Polygon vertex pointers. The result borrows storage
// owned by the Model below.
struct BgfGeometry {
    std::vector<Vertex>  vertices;
    std::vector<Polygon> polygons;
    MeshGeometry         geom;
    MeshGeometry*        View();  // refresh pointers + return &geom
};
bool BuildGeometry(const BgfModel& m, BgfGeometry& out);

} // namespace guild::render
