#pragma once
#include "guild/common/types.h"
#include "render/bgf_loader.h"

#include <string>
#include <vector>

// =============================================================================
// guild::render — .BGF mesh LOAD ORCHESTRATOR + post-load passes.
//
// Faithful 1:1 reconstruction of the outer model loader and its post-parse
// geometry passes in gilde.exe (d3_io.c):
//
//   0x5D2348  VIBE_Mesh_LoadBgfFile           (the 0xF51-byte orchestrator)
//   0x5D1B54  VIBE_Mesh_ComputeBoundingExtents (radius + AABB + 8 corner verts +
//                                               centroid; in mesh_postprocess.cpp)
//   0x5D1A6c  VIBE_Mesh_ComputeVertexNormals   (per-poly + averaged per-vertex
//                                               normals; in mesh_postprocess.cpp)
//
// The orchestrator at 0x5D2348 has TWO parse front-ends sharing one back-end:
//   (a) the binary FAST-CHUNK reader VIBE_Model_LoadFastChunk @0x5F87B8 — already
//       reconstructed in bgf_loader.cpp. If it succeeds the orchestrator simply
//       runs ComputeBoundingExtents on its result and returns (the fast-chunk
//       reader does its own dedup/normals internally).
//   (b) the AGF text/script reader (VIBE_ModelIo_ReadChunkTag @0x5E44B4 ->
//       VIBE_Script_ParseBlock @0x5E3C44, a generic token-handler-table parser).
//       When this path is taken the orchestrator runs the FULL post-load
//       pipeline on the parsed intermediate arrays:
//         1. vertex dedup        (collapse coincident verts; remap poly indices)
//         2. allocate vertex/poly storage (24*(n+8) / 56*n bytes)
//         3. MORPH-TARGET bake    (rotate every vertex by the fixed dbl_6290CC..
//                                  euler triple — see kMorph* constants below)
//         4. material dedup       (drop byte-identical 224-byte material records,
//                                  remap poly material indices; pass1 by content,
//                                  pass2 dropping unreferenced materials)
//         5. material re-order + texture resolution
//                                  (VIBE_Texture_LoadByName per material; store
//                                  the resolved texture slot id into each poly +36)
//         6. dummy/locator copy    (88-byte source -> 88-byte mesh dummy records)
//         7. ComputeBoundingExtents + ComputeVertexNormals
//
// The generic AGF *token* parser (step b's front-end) is a separate, deferred
// module; this reconstruction models the parsed intermediate (ParsedModel below,
// matching the orchestrator's v140 stack descriptor byte-for-byte) and translates
// the shared post-parse pipeline 1:1. A BgfModel (from the fast-chunk reader) is
// adapted into a ParsedModel by BuildParsedFromBgf(), so the whole orchestrator can
// be driven end-to-end from a real .BGF buffer.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Morph-bake euler constants (gilde.exe data segment). Every loaded vertex is
// rotated by this fixed orientation as it is copied into the mesh (the engine
// stores .BGF geometry in a Z-up/Y-up swapped frame).
// ---------------------------------------------------------------------------
constexpr double kMorphAngleA = -1.5707963267948966;  // dbl_6290CC = -pi/2
constexpr double kMorphAngleB =  3.141592653589793;   // dbl_6290D4 =  pi
constexpr double kMorphScaleZ = -1.0;                 // dbl_6290DC = -1.0

// Dummy UV-rotate constants (used when baking dummy/locator transforms).
constexpr float  kDummyHalf  = 0.5f;                  // flt_6290E4 = 0.5
constexpr float  kDummyPi    = 3.1415927410125732f;   // flt_6290E8 = pi (float)

// Centroid averaging weight over the 8 AABB corner vertices (1/8).
constexpr float  kCentroidWeight = 0.125f;            // flt_628FC0 = 0.125

// Vertex-dedup position tolerance (gilde.exe literal 0.001).
constexpr float  kVertexDedupTol = 0.001f;

// ---------------------------------------------------------------------------
// Raw vertex stored in the mesh (24-byte stride). Position at +0, normal at +12.
// Iterated `+= 24` in ComputeBoundingExtents / `+= 6 floats` in
// ComputeVertexNormals. This is the same record bgf_loader emits (BgfVertex).
// ---------------------------------------------------------------------------
struct MeshVertex {
    float pos[3];     // +0x00  model-space position (post morph-bake)
    float normal[3];  // +0x0C  averaged vertex normal (filled by ComputeVertexNormals)
};
static_assert(sizeof(MeshVertex) == 24, "MeshVertex stride must be 24 bytes");

// ---------------------------------------------------------------------------
// Raw polygon stored in the mesh (56-byte stride). Iterated `+= 56`.
//   +0/+4/+8/+12/+16/+20  UV / per-corner data (3 vec3s, as bgf_loader fills)
//   +24/+28/+32           vertex indices (remapped by dedup)
//   +36                   resolved texture slot id (-1 until texture stage)
//   +40                   material index (remapped by material dedup/reorder)
//   +44/+48/+52           polygon face normal (filled by ComputeVertexNormals)
// ---------------------------------------------------------------------------
struct MeshPolygon {
    float uv0[3];     // +0x00
    float uv1[3];     // +0x04 (interleaved row, see bgf_loader)
    u32   vtx[3];     // +0x18/+0x1C/+0x20  vertex indices
    i32   texId;      // +0x24  resolved texture id
    i32   matIndex;   // +0x28  material index
    float normal[3];  // +0x2C  face normal
};
static_assert(sizeof(MeshPolygon) == 56, "MeshPolygon stride must be 56 bytes");

// ---------------------------------------------------------------------------
// Material record — the 224-byte AGF material struct (gilde.exe v142 stride).
// Byte offsets recovered from the texref-resolution block of LoadBgfFile.
//   +0    char name0[64]   diffuse texture name
//   +64   char name1[64]   secondary name
//   +128  char name2[64]   tertiary name (+128 nonzero => use as the load name)
//   +192  u8  presentFlag  (==2 => set extra bit in the load flag word)
//   +193  u8  paletteByte
//   +194  u8  blendBit      (bit0 => blend)
//   +195  u8  shiftHi       (<<6 into the format byte)
//   +196  u8  reserved
//   +197  u8  mul2          (<<1)
//   +198  u8  mul4          (<<2)
//   +199  u8  byte1         (-> BYTE1 of the flag word)
//   +200  u8  mul16         (<<4)
//   +201  u8  lowNibble
//   +202..+203 reserved
//   +204  float uScale
//   +208  float vScale
//   +212  float uOffset
//   +216  float vOffset
//   +220  float rotation
// ---------------------------------------------------------------------------
struct MeshMaterial {
    char  name0[64];   // +0x00
    char  name1[64];   // +0x40
    char  name2[64];   // +0x80
    u8    presentFlag; // +0xC0  (192)
    u8    paletteByte; // +0xC1  (193)
    u8    blendBit;    // +0xC2  (194)
    u8    shiftHi;     // +0xC3  (195)
    u8    reserved196; // +0xC4  (196)
    u8    mul2;        // +0xC5  (197)
    u8    mul4;        // +0xC6  (198)
    u8    byte1;       // +0xC7  (199)
    u8    mul16;       // +0xC8  (200)
    u8    lowNibble;   // +0xC9  (201)
    u8    reserved202; // +0xCA  (202)
    u8    reserved203; // +0xCB  (203)
    float uScale;      // +0xCC  (204)
    float vScale;      // +0xD0  (208)
    float uOffset;     // +0xD4  (212)
    float vOffset;     // +0xD8  (216)
    float rotation;    // +0xDC  (220)
};
static_assert(sizeof(MeshMaterial) == 224, "MeshMaterial stride must be 224 bytes");

// ---------------------------------------------------------------------------
// Dummy/locator source record (88-byte stride, the v148 buffer).
//   +0   char name[64]
//   +64  float pos[3]
//   +76  float rot[3]
// ---------------------------------------------------------------------------
struct MeshDummySource {
    char  name[64];   // +0x00
    float pos[3];     // +0x40
    float rot[3];     // +0x4C
};
static_assert(sizeof(MeshDummySource) == 88, "MeshDummySource stride must be 88 bytes");

// ---------------------------------------------------------------------------
// Mesh dummy record as stored in the mesh header (22-float / 88-byte stride at
// mesh +116; v185 = v155+29, advanced by 22 floats each).
//   +0   char name[64]   (StrNCopyPad, 63 + NUL)
//   +92  float ?          (source +64)
//   +96  float ?          (source +68)
//   +100 dword            (source +72)  ... copied as raw dwords
//   +104 dword            (source +76)
//   +108 dword            (source +80)
//   +112 dword            (source +84)
// ---------------------------------------------------------------------------
struct MeshDummy {
    char  name[64];   // +0x00 .. +0x3F (NUL at +91 region in original 88B record)
    float f92;        // +0x5C  source +64
    float f96;        // +0x60  source +68
    u32   d100;       // +0x64  source +72
    u32   d104;       // +0x68  source +76
    u32   d108;       // +0x6C  source +80
    u32   d112;       // +0x70  source +84
};

// ---------------------------------------------------------------------------
// Parsed intermediate (the orchestrator's v140 stack descriptor). Produced by
// either parse front-end; consumed by the post-load pipeline.
//   skipVertexDedup  = v140[1]  (when set, the vertex-dedup pass is skipped)
//   materials/polys/vertices/dummies = the 224/56/24/88-byte arrays
// ---------------------------------------------------------------------------
struct ParsedModel {
    bool skipVertexDedup = false;        // v140[1]
    std::vector<MeshMaterial>    materials;  // v142 / count v141
    std::vector<MeshPolygon>     polygons;   // v144 / count v143
    std::vector<MeshVertex>      vertices;   // v146 / count v145
    std::vector<MeshDummySource> dummies;    // v148 / count v147
};

// ---------------------------------------------------------------------------
// Loaded mesh header (gilde.exe 0x20C-byte object, base = v155). Only the fields
// the geometry pipeline touches are modelled; offsets are the ORIGINAL ones.
//   +0    char name[64]        (StrNCopyPad of the file path, 63 + NUL)
//   +64   MeshVertex* vertices (v155+16)
//   +68   i32 vertexCount      (v155+17)
//   +72   MeshPolygon* polys   (v155+18)
//   +76   i32 polyCount        (v155+19)
//   +104  float centroid[3]    (v155+26/27/28; set by ComputeBoundingExtents)
//   +116  MeshDummy[] dummies  (v155+29, 88-byte stride)
//   +468  float radius2        (v155+117; AABB diagonal length)
//   +472  float radius         (v155+118; max |vertex|)
//   +480  i32 materialCount    (v155+120)
//   +484  i32 morphFrameCount  (v155+121)
//   +516  void* materialNames  (v155+129; packed 64-byte name block)
// ---------------------------------------------------------------------------
struct Mesh {
    std::string name;                    // +0x00
    std::vector<MeshVertex>   vertices;   // +0x40 base / +0x44 count (engine sizes count+8)
    std::vector<MeshPolygon>  polygons;   // +0x48 base / +0x4C count
    i32   polyCount = 0;                   // +0x4C  (== polygons.size())
    float centroid[3] = {0, 0, 0};        // +0x68
    std::vector<MeshDummy>    dummies;     // +0x74
    float radius2 = 0.0f;                  // +0x1D4
    float radius  = 0.0f;                  // +0x1D8
    i32   materialCount   = 0;             // +0x1E0
    i32   morphFrameCount = 0;             // +0x1E4
    std::vector<char>         materialNames; // +0x204 (packed name block)

    // The logical vertex count (engine stores count, allocates count+8 slots for
    // the 8 AABB corner verts written by ComputeBoundingExtents).
    i32 vertexCount = 0;
};

// gilde.exe 0x5D2348 — VIBE_Mesh_LoadBgfFile (post-parse pipeline).
// Runs the full orchestration on an already-parsed model: vertex dedup, storage
// allocation + morph-bake, material dedup (2 passes) + reorder + texture
// resolution, dummy copy, then ComputeBoundingExtents + ComputeVertexNormals.
//   `name`        : the mesh name (upper-cased file path in the original).
//   morphFrames   : the morph-frame count argument (a3/v192); when (morphFlag &&
//                   morphFrames>0 && materialNames preallocated) the material name
//                   block is taken as-is, else it is (re)allocated here.
//   morphFlag     : a4/v191 (the morph-target gate).
// Returns true on success; fills `out`.
bool LoadBgfPostProcess(ParsedModel& pm, const std::string& name,
                        i32 morphFrames, bool morphFlag, Mesh& out);

// Adapt a fast-chunk BgfModel (bgf_loader output) into a ParsedModel so the full
// orchestrator can be driven from a real .BGF byte buffer.
void BuildParsedFromBgf(const BgfModel& m, ParsedModel& out);

// gilde.exe 0x5D2348 (top) — full entry point: parse a .BGF byte buffer via the
// fast-chunk reader, adapt it, and run the post-process pipeline.
bool LoadBgfFile(const u8* data, size_t size, const std::string& name, Mesh& out);

} // namespace guild::render
