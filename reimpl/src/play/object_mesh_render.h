#pragma once
// =============================================================================
// guild::play — REAL OBJECT MESH RENDER (PLAYABLE_PLAN P2, mesh tier).
//
// world_render.{h,cpp} draws each live entity as a FLAT QUAD at its decoded world
// transform. This module pushes the last step of P2: draw each object as its
// ACTUAL GEOMETRY — the mesh loaded from gfx/gilde.gfx (render::BgfModel ->
// render::MeshGeometry) — seated at the object's REAL world placement
// (play::object_transform: +76 world pos, +396 frame-matrix yaw) and pushed
// through the SAME real render leaves the engine runs:
//
//   place mesh in world  -> compose the +72 world matrix from the WorldPlacement
//                           (yaw rotation about +Y seated with the world position)
//                           and apply it to every model-space vertex, exactly as
//                           VIBE_Mesh_InterpolateMorphVertices does
//                           (sx = vx*m0 + vy*m4 + vz*m8 + m12, ...),
//   project              -> render::ProjectVerticesToScreen  (0x5c5120)
//   sort                 -> render::RadixSortDrawList         (0x5AEF34)
//   rasterize            -> render::RasterizeMeshList         (0x5AEC88)
//
// so an object draws as its true mesh at its true city position. When a mesh
// can't be resolved (no gfx bound / resolver inert) the object FALLS BACK to a
// flat quad at the same placement, so a mesh-less load still renders.
//
// THE WORLD-SEAT MATRIX (grounding)
// ---------------------------------------------------------------------------
// object_transform.h recovered that the projection reader applies the node's
// composed 4x4 world matrix (record+72) to every vertex as
//     sx = vx*m[0] + vy*m[4] + vz*m[8]  + m[12]
//     sy = vx*m[1] + vy*m[5] + vz*m[9]  + m[13]
//     sz = vx*m[2] + vy*m[6] + vz*m[10] + m[14]
// and that the rotation half is the +396 frame matrix (MatrixFromEuler: for a
// ground object M[0]=cos(yaw), M[8]=-sin(yaw)) with the world position seated in
// the 4th column. We rebuild that matrix from a WorldPlacement (yaw + x/y/z) and
// apply it before projection — i.e. the model-space mesh is placed in the world
// EXACTLY as the engine seats it, then the existing ProjectVerticesToScreen does
// the (eye/scale) screen projection. This module is additive (new file); it wires
// nothing — the mesh source is supplied through an installable MeshResolver hook
// whose inert default returns null (-> quad fallback), the same "subsystem not
// present" pattern world_render uses for its NodeResolver.
// =============================================================================
#include "guild/common/types.h"
#include "play/object_transform.h"
#include "play/world_render.h"     // EntityRef / EntityKind / WorldObject reuse
#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/clip.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace guild::render { struct Surface; struct DecodedBmp; }

namespace guild::play {

class RealTextureSource;
struct MaterialTextureTable;

// ---------------------------------------------------------------------------
// Compose the engine's 4x4 column-major world matrix (the record+72 matrix the
// projection reads) from a decoded WorldPlacement: a yaw rotation about +Y
// (M[0]=cos(yaw), M[8]=-sin(yaw), M[2]=sin(yaw), M[10]=cos(yaw)) seated with the
// world translation in the 4th column (M[12]/M[13]/M[14]). Identity 4th row.
// 1:1 with VIBE_Math_MatrixFromEuler (yaw branch) + the SetPosition seat.
// ---------------------------------------------------------------------------
void ComposeWorldMatrix(const WorldPlacement& wp, float m[16]);

// ---------------------------------------------------------------------------
// Apply the composed world matrix to every vertex of `src` (model space),
// writing world-space x/y/z into `dst` (same count, UV/light copied through),
// exactly as VIBE_Mesh_InterpolateMorphVertices transforms each vertex:
//   wx = vx*m[0] + vy*m[4] + vz*m[8]  + m[12]
//   wy = vx*m[1] + vy*m[5] + vz*m[9]  + m[13]
//   wz = vx*m[2] + vy*m[6] + vz*m[10] + m[14]
// `dst` is resized to src.vertexCount. The polygons are NOT copied here (the
// caller rebinds poly vertex pointers into `dst`); use TransformMeshGeometry for
// a fully-rebound MeshGeometry.
// ---------------------------------------------------------------------------
void TransformVertices(const render::MeshGeometry& src, const float m[16],
                       std::vector<render::Vertex>& dst);

// A self-contained, world-seated copy of a mesh: transformed vertices + polygons
// rebound to them, plus a MeshGeometry view. Produced by TransformMeshGeometry;
// owns its storage so the projection/raster can run on it directly.
struct WorldMesh {
    std::vector<render::Vertex>  vertices;
    std::vector<render::Polygon> polygons;
    render::MeshGeometry         geom{};
    // Refresh geom pointers/counts to the owned vectors and return &geom.
    render::MeshGeometry* View();
};

// Seat `src` (model-space mesh) at `wp` (world placement): build the world matrix,
// transform the vertices, rebind the polygons (preserving winding + UVs + flags),
// into `out`. Returns false (and leaves out empty) when src has no geometry.
bool TransformMeshGeometry(const render::MeshGeometry& src, const WorldPlacement& wp,
                           WorldMesh& out);

// ---------------------------------------------------------------------------
// MeshResolver hook — map an entity to its model-space mesh geometry, or null.
//
// The portable reimpl entity arrays (ObjectRec / SceneNode) only carry id+type;
// the object's mesh handle (gilde.exe object +0x34 / +460 material+geometry) lives
// in the full engine struct. So the mesh source is supplied by an installable
// resolver: given an EntityRef it returns a pointer to a render::MeshGeometry the
// caller owns (a render::BgfGeometry view from a loaded gilde.gfx object), or null
// when no mesh is bound. The inert default returns null -> the renderer falls back
// to a flat quad, so a mesh-less load still produces a frame.
// ---------------------------------------------------------------------------
using MeshResolver = const render::MeshGeometry* (*)(const EntityRef& e);

// The inert default mesh resolver: always null (no mesh bound).
const render::MeshGeometry* DefaultMeshResolver(const EntityRef& e);

// ---------------------------------------------------------------------------
// TexTableResolver hook — map an entity to the per-poly MaterialTextureTable for
// the mesh it draws (built by RealTextureSource::BuildTableFor: material name0 ->
// Textures.BIN BMP -> poly texId). The table's polyTexId[] is indexed by the
// SAME polygon order the MeshResolver's MeshGeometry uses, so the textured raster
// can bind each transformed polygon to its DecodedBmp. Returns null when the
// entity has no texture table -> that object renders untextured. The inert
// default returns null so the legacy (untextured) path is unchanged.
// ---------------------------------------------------------------------------
using TexTableResolver = const MaterialTextureTable* (*)(const EntityRef& e);

// The inert default texture-table resolver: null -> untextured.
const MaterialTextureTable* DefaultTexTableResolver(const EntityRef& e);

// ---------------------------------------------------------------------------
// ObjectMeshRenderer — projects + rasterizes live objects as their real meshes.
//
// For each scanned entity it: decodes the WorldPlacement (via a NodeResolver, the
// same hook world_render uses), resolves the mesh (via a MeshResolver), seats the
// mesh in the world, projects it (ProjectVerticesToScreen), and — after all
// entities are projected and the draw list radix-sorted — rasterizes the whole
// sorted list once (RasterizeMeshList) into the target surface. Entities whose
// mesh the resolver can't supply, or whose node has no placement, fall back to a
// single world-seated quad (two tris) so the frame is never empty.
// ---------------------------------------------------------------------------
struct MeshRenderStats {
    int meshObjects   = 0;   // entities drawn as a resolved mesh
    int quadFallbacks = 0;   // entities drawn as a fallback quad
    int meshTris      = 0;   // polygons sourced from resolved meshes (pre-cull)
    int appendedPolys = 0;   // draw-list entries projected+sorted (post-cull)
    int rasterTris    = 0;   // triangles RasterizeMeshList actually flushed
    int texturedPolys = 0;   // poly draw-list entries bound to a real texture
    int objects() const { return meshObjects + quadFallbacks; }
};

class ObjectMeshRenderer {
public:
    struct Options {
        // -- placement / projection -----------------------------------------
        NodeResolver nodeResolver = &DefaultNodeResolver;  // entity -> engine node
        MeshResolver meshResolver = &DefaultMeshResolver;  // entity -> mesh geom
        float pixelsPerUnit = 1.0f;                        // world unit -> pixels
        float eyeX = 0.0f, eyeZ = 0.0f;                    // city center -> frame center
        // -- scan controls (mirror world_render) ----------------------------
        bool scanObjects = true;                           // alive g_objects
        bool scanScene   = true;                           // g_sceneNodes (tree)
        int  maxObjects  = 64;                             // cap on entities drawn
        // -- fallback quad half-extent (model-space, pre-projection) --------
        float quadHalf = 6.0f;
        // -- TEXTURED raster (default OFF -> the legacy flat/shaded path) ----
        // When `textured` is set AND a TexTableResolver yields a table for an
        // entity, each mesh polygon is sampled from its resolved Textures.BIN BMP
        // per pixel (AFFINE U/V interpolation, matching the engine's RGBZ span
        // path VIBE_Raster_FillSpanLoop @0x5F6B34 — NOT perspective-correct).
        // Polys with no resolvable texture (texId == -1) keep the flat/shaded path.
        bool                textured       = false;
        TexTableResolver    texTableResolver = &DefaultTexTableResolver;
    };

    ObjectMeshRenderer() = default;

    // Render the live entity arrays as meshes (+ quad fallbacks) into `fb`
    // (an already-created software surface). Does NOT clear `fb` (the caller
    // owns the clear, matching the engine's clearRect-then-walk order). Returns
    // the per-render stats.
    MeshRenderStats render(const Options& opt, render::Surface* fb);

    const MeshRenderStats& stats() const { return stats_; }

private:
    // Per-render draw-list ping-pong + clip/dispatch scratch (re-entrant).
    static constexpr int kMaxPolys = 4096;
    std::vector<render::DrawListEntry> pool1_;
    std::vector<render::DrawListEntry> pool2_;
    render::DrawListBuffers db_{};
    render::ClipScratch     scratch_{};
    render::SpanDispatch    dispatch_{};

    // World-seated mesh storage for the current frame (kept alive until raster).
    std::vector<WorldMesh>  worldMeshes_;

    // Per-frame TEXTURE BINDING: WorldMesh Polygon* -> the DecodedBmp to sample
    // for that triangle (null/absent -> untextured flat path). The textured span
    // dispatch consults this via a frame-scoped active-renderer pointer (a free
    // function span fn can't capture). Cleared each frame.
    std::unordered_map<const render::Polygon*, const render::DecodedBmp*> polyTex_;

    MeshRenderStats stats_{};

    // Project one already-world-seated mesh into db_ (returns appended count).
    int projectMesh(render::MeshGeometry* geom, const render::ProjectParams& pp);
    // Build + project a fallback quad at `wp` into db_ (returns appended count).
    int projectQuad(const WorldPlacement& wp, const Options& opt,
                    const render::ProjectParams& pp, render::Surface* fb);
};

// ---------------------------------------------------------------------------
// RasterTexturedTriangleAffine — the textured span path this wave wires into the
// live mesh raster. Given one triangle (3 vertices carrying screen x/y in
// Vertex::screenX/screenY and texel U/V in Vertex::u/v) and a decoded BMP, it
// AFFINELY interpolates U/V across the triangle (linear in screen space, the
// engine's RGBZ behaviour — no per-pixel perspective divide) and writes each
// covered pixel by sampling the texture (SampleTexel, wrap addressing) into the
// surface via its colour format. Returns the number of pixels written. This is
// the per-pixel kernel the SpanDispatch slots call during a textured render; it
// is exposed so the unit tier can assert per-pixel texel fidelity directly.
// ---------------------------------------------------------------------------
int RasterTexturedTriangleAffine(render::Surface* fb, const render::Vertex* v0,
                                 const render::Vertex* v1, const render::Vertex* v2,
                                 const render::DecodedBmp& bmp);

} // namespace guild::play
