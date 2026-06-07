#pragma once
#include "render/geometry_types.h"
#include <vector>

// Model buffer loading for the guild::render geometry stage.
//
// The production loaders in gilde.exe are very large binary parsers:
//   VIBE_Mesh_LoadBgfFile     @0x5d2348 (0xF51 bytes, the main .BGF mesh format)
//   VIBE_Model_FastChunkIo    @0x5f9558 (0x1011 bytes, fast binary chunk IO)
//   VIBE_Model_LoadFastChunk  @0x5f87b8 (0x6EC bytes)
//   VIBE_ModelIo_LoadBinaryAnimation @0x5e450c (0xF5E bytes)
// These are DEFERRED (see report) — they pull in the whole texture/material/LOD
// subsystem and the guild::io file layer. What this module provides is the record
// layout those loaders fill (Vertex/Polygon/MeshGeometry, see geometry_types.h) and
// a compact in-memory loader that materialises a MeshGeometry from a flat synthetic
// model buffer using the SAME 80-byte vertex / 40-byte polygon strides, so the
// geometry/transform/cull pipeline can be exercised end-to-end without the BGF
// parser. The synthetic format is documented in model_io.cpp.
namespace guild::render {

// A loaded model: owns its vertex/polygon storage and exposes a MeshGeometry view.
struct Model {
    std::vector<Vertex>  vertices;
    std::vector<Polygon> polygons;
    MeshGeometry         geom;  // points into the vectors above

    MeshGeometry* View();  // refresh geom pointers and return &geom
};

// Synthetic model buffer header (little-endian), used by tests and tools. This is
// NOT the real BGF layout — it is a thin envelope around the engine record strides:
//   u32 magic   = 'GMDL'
//   u32 vertexCount
//   u32 polyCount
//   then vertexCount * { float x,y,z; float u,v; u8 light }
//   then polyCount  * { u32 v0,v1,v2; float ux,uy,uz; u8 f36, f38 }
// Returns false on a malformed/truncated buffer.
bool LoadSyntheticModel(const u8* data, size_t size, Model& out);

} // namespace guild::render
