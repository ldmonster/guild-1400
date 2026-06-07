#pragma once
// gilde.exe — REAL-asset mesh/object -> geometry driver (guild::app).
//
// INTEGRATION GLUE, not a translation. This module wires the already-
// reconstructed .BGF mesh stack into one end-to-end path that proves REAL
// shipped object bytes flow through the reconstructed loader to inspectable
// geometry (and optionally a rasterized frame):
//
//   1. (optionally) MountRealGameAssets over a shim::IFileSystem rooted at a
//      real "Die Gilde" install (real_boot.h);
//   2. open Resources/Objects.BIN with the reconstructed io::ZipArchive and walk
//      its central directory, inventorying the real .BGF mesh members;
//   3. drive the reconstructed mesh loader (render::LoadBgfFile @0x5D2348 ->
//      fast-chunk parse @0x5F87B8 -> vertex dedup -> morph-bake -> material
//      dedup + render::TextureLoadByName resolution -> render::ComputeBounding-
//      Extents @0x5D1B54 + render::ComputeVertexNormals @0x5D1A6C) over each
//      real member, classifying the on-disk container format and parsing the
//      members the reconstructed front-end accepts;
//   4. for a chosen loaded mesh, optionally rasterize its polygons (orthographic
//      model->screen) into a 16bpp render::Surface with the REAL textured-
//      triangle rasterizer and present that frame through a
//      shim::FileDumpGraphicsDevice -> a standard 24-bit BMP on disk.
//
// THE SHIPPED CONTAINER FORMAT. The reconstructed mesh front-end is the binary
// FAST-CHUNK reader (magic 0xFAB7E6C5). The shipped Objects.BIN members are the
// 'BGF\0'-tagged AGF token stream, whose generic token parser is a separate,
// still-deferred leaf (see render/mesh_load.h). The driver therefore RUNS the
// reconstructed loader over every real member (proving the real-byte path), and
// reports the observed format distribution; geometry assertions for the full
// dedup/bounds/normals pipeline are driven over a fast-chunk mesh the
// reconstructed render::Model_WriteFastChunk writer emits, plus any real member
// the fast-chunk reader accepts.
//
// Everything reached here is real reconstructed code over real bytes; the only
// OS boundary is shim::IFileSystem (asset reads) and the optional file dump.
// No DDraw/GDI: the present layer is the inert FileDumpGraphicsDevice shim.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"

#include <string>
#include <vector>

namespace guild::app {

// ---------------------------------------------------------------------------
// Geometry summary for one mesh driven through the full reconstructed pipeline.
// ---------------------------------------------------------------------------
struct MeshGeometryInfo {
    std::string name;            // mesh name (the upper-cased member path)
    bool  loaded = false;        // LoadBgfFile succeeded
    int   vertexCount = 0;       // logical vertex count (+0x44)
    int   polyCount = 0;         // polygon count (+0x4C)
    int   materialCount = 0;     // material count after dedup (+0x1E0)
    int   dummyCount = 0;        // dummy/locator records
    float radius = 0.0f;         // bounding radius (+0x1D8; max |vertex|)
    float aabbDiag = 0.0f;       // AABB diagonal length (radius2, +0x1D4)
    float aabbMin[3] = {0, 0, 0};// axis-aligned bounds (recomputed from verts)
    float aabbMax[3] = {0, 0, 0};
    float centroid[3] = {0, 0, 0};// centroid (+0x68; mean of the 8 AABB corners)
    bool  normalsUnit = false;   // every vertex normal is ~unit length (post pass)
    bool  boundsSane = false;    // aabbMin<=aabbMax, radius>0, diag>0 (non-degenerate)
};

// ---------------------------------------------------------------------------
// Result of a real-archive mesh-driver run.
// ---------------------------------------------------------------------------
struct MeshDriveResult {
    // ---- inventory tier (real container walk) ----
    int archiveMembers = 0;      // total members listed in the archive
    int bgfMembers = 0;          // members with a .BGF/.bgf suffix
    int fastChunkMembers = 0;    // members the reconstructed fast-chunk reader parsed
    int agfTaggedMembers = 0;    // members carrying the 'BGF\0' AGF container tag
                                 // (the still-deferred token-parser front-end)
    int otherMembers = 0;        // .BGF members with neither signature

    // ---- geometry tier ----
    int meshesLoaded = 0;        // members fully driven through LoadBgfFile
    long totalVertices = 0;      // sum of loaded mesh vertex counts
    long totalPolys = 0;         // sum of loaded mesh polygon counts
    std::vector<MeshGeometryInfo> samples;  // a small spread of loaded meshes

    // ---- render tier ----
    bool rendered = false;       // a mesh frame was rasterized + presented
    std::string renderedMesh;    // which mesh was rasterized
    int  renderWidth = 0;
    int  renderHeight = 0;
    long nonBlankPixels = 0;     // non-zero 16bpp framebuffer pixels after raster
    std::string framePath;       // dumped image path ("" if no dump configured)
};

// ---------------------------------------------------------------------------
// Drive ONE mesh from a .BGF byte buffer all the way through the reconstructed
// LoadBgfFile pipeline (fast-chunk parse -> dedup -> morph-bake -> material
// dedup + texture resolve -> ComputeBoundingExtents + ComputeVertexNormals) and
// summarize its geometry. Returns info.loaded=false if the reconstructed
// front-end does not accept the buffer (e.g. an AGF-tagged container).
// ---------------------------------------------------------------------------
MeshGeometryInfo LoadMeshGeometry(const std::vector<u8>& bgf, const std::string& name);

// ---------------------------------------------------------------------------
// Rasterize a loaded mesh's polygons into a `fbW` x `fbH` 16bpp surface using
// the REAL textured-triangle rasterizer, with an orthographic model->screen fit
// (the mesh AABB is scaled to fill the frame; depth fakes a flat shade via a
// per-poly texel ramp). Optionally dump the presented frame to `dumpDir`.
// Re-loads the mesh from `bgf` internally so the geometry it draws is the same
// pipeline output. Returns a result with rendered=true / nonBlankPixels>0 on
// success.
// ---------------------------------------------------------------------------
MeshDriveResult RenderMeshToImage(const std::vector<u8>& bgf, const std::string& name,
                                  int fbW, int fbH, const std::string& dumpDir = "",
                                  const std::string& dumpPrefix = "mesh");

// ---------------------------------------------------------------------------
// The full real-asset path: open `archiveRel` (default Resources/Objects.BIN)
// through `fs`, walk the central directory inventorying every .BGF member,
// drive the reconstructed loader over each (classifying the container format
// and fully processing any the reconstructed reader accepts), collect a sample
// geometry spread, and rasterize ONE loaded mesh to a frame (optionally dumped).
//
//   `renderMember` selects which loaded mesh is rasterized; if empty (or not
//   found) the first successfully loaded mesh is used. `maxParse` caps the
//   number of members handed to the loader (<=0 == all) to bound test runs.
// ---------------------------------------------------------------------------
MeshDriveResult LoadRealMeshesFromArchive(
    shim::IFileSystem* fs,
    const std::string& archiveRel = "Resources/Objects.BIN",
    const std::string& renderMember = "",
    int fbW = 256, int fbH = 256,
    const std::string& dumpDir = "",
    int maxParse = 0);

} // namespace guild::app
