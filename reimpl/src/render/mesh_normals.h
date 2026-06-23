#pragma once
#include "guild/common/types.h"

#include <array>
#include <cstddef>
#include <vector>

// =============================================================================
// guild::render — STATIC (rest-pose) per-vertex NORMAL generation + the per-
// instance source-normal binding that feeds the wave-6/7 lighting + env-map
// passes. Reconstructed 1:1 from gilde.exe.
//
//   0x5D1A6C  VIBE_Mesh_ComputeVertexNormals   — the static normal generator
//             (run once at .BGF load by VIBE_Mesh_LoadBgfFile @0x5d2348, call
//             site @0x5d2f97). Per-poly face normal -> per-vertex averaged
//             normal. (The animated/per-keyframe variant is
//             VIBE_Anim_CalculateAnimNormals @0x5d0020, see anim_normals.*.)
//
// THE NAMED WAVE-7 GAP CLOSED HERE
// -----------------------------------------------------------------------------
// The wave-7 sun-NdotL kernel (object_light_shade, gap @0x5c7129) and the env-map
// reflection walk (env_map_walk, @0x5c9054 non-skinned arm) both consume a
// per-vertex OBJECT-SPACE normal that the engine reaches through a POINTER stored
// in the 80-byte instance/cache vertex at +0x48 (== +72, the 18th dword):
//
//   sun arm        @0x5c717c:  edx = *(instVert + 0x48);  n = edx[0..2]
//                              (mov edx,[ecx+48h]; mov eax,[edx]; [edx+4]; [edx+8])
//   env-map arm    @0x5c92a4:  p = *(instVert + 72);      n = *(p + 12 .. +20)
//   normal-xform   @0x5c9c58:  p = *(instVert + 72);      n = *(p + 12 .. +20)
//   pos-source     @0x5c953c:  p = *(instVert + 72);      pos = *(p + 0 .. +8)
//
// i.e. instVert+0x48 is a pointer to the 24-byte source MeshVertex (pos @+0,
// normal @+12 — exactly the record VIBE_Mesh_ComputeVertexNormals fills). The
// SUN cache arm dereferences that pointer at +0 because its light-cache geometry
// (VIBE_Mesh_SelectLodFrame @0x5adb6c, used by VIBE_Light_BuildObjectCache
// @0x5c8218) binds +0x48 directly to the NORMAL (+12) of the source vertex; the
// draw/env-map geometry binds +0x48 to the source vertex BASE (+0) and reads the
// normal at +12. Both conventions are expressed below as MeshNormalSource so the
// bind site can wire whichever frame it holds. NO bind-site file is edited here
// (city_view3d.* / universe_render.cpp are the orchestrator's).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// The 24-byte source mesh vertex (gilde.exe MeshVertex; == render::MeshVertex in
// mesh_load.h and BgfVertex in bgf_loader.h). Position at +0, normal at +12.
// Re-declared minimally here so the normal generator has no heavy dependency on
// the full Mesh header; it is layout-compatible (static_assert below).
// ---------------------------------------------------------------------------
struct SourceMeshVertex {
    float pos[3];     // +0x00  model-space position
    float normal[3];  // +0x0C  averaged vertex normal (filled by GenerateVertexNormals)
};
static_assert(sizeof(SourceMeshVertex) == 24, "SourceMeshVertex stride must be 24 bytes");

// A triangle's three source-vertex indices (the poly's +24/+28/+32 fields).
struct NormalTriangle {
    u32 vtx[3];
};

// =============================================================================
// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals (portable form).
//
// Two passes, byte-identical to the original:
//   1. per triangle: faceNormal[t] = normalize((v1-v0) x (v2-v0))     (TriangleNormal)
//   2. per vertex i: normal[i] = normalize( sum of faceNormal[t] for every
//                    triangle t with i in {vtx0,vtx1,vtx2} )           (VectorNormalize)
//
// The face normals are UNIT length (TriangleNormal normalizes), so the scheme is
// an UNWEIGHTED average of adjacent face normals (NOT area- or angle-weighted) —
// this is the engine's exact scheme. A vertex with no adjacent triangle, or whose
// summed normal is zero-length, collapses to (0,0,0) (VectorNormalize @0x5cb148).
//
// `verts`     : vertexCount source vertices (pos read, normal[+12] written).
// `triangles` : the index triples (the mesh's poly +24/+28/+32 indices).
// `faceNormalsOut` (optional): if non-null, receives the per-triangle unit face
//               normal (3 floats/triangle) — the engine also stores it at poly+44.
// =============================================================================
void GenerateVertexNormals(SourceMeshVertex* verts, int vertexCount,
                           const NormalTriangle* triangles, int triangleCount,
                           float* faceNormalsOut /* = nullptr */);

// Convenience overload over std::vector storage. Returns the per-triangle unit
// face normals (triangleCount entries, 3 floats each) in `faceNormalsOut`.
void GenerateVertexNormals(std::vector<SourceMeshVertex>& verts,
                           const std::vector<NormalTriangle>& triangles,
                           std::vector<std::array<float, 3>>* faceNormalsOut = nullptr);

// =============================================================================
// PER-INSTANCE SOURCE-NORMAL BINDING — the engine's instVert+0x48 wiring.
//
// The 80-byte instance/cache vertex carries at +0x48 a POINTER the sun /env-map
// /normal-transform passes dereference for the object-space normal. The engine
// sets this when it materialises the instance from the shared source mesh (one
// instance vertex per source vertex, 1:1 by index). Two conventions exist:
//
//   kBindToVertexBase  : +0x48 -> &source[i]            (read normal at +12)
//                        (draw/env-map/normal-transform frame, @0x5c92a4/0x5c9c58)
//   kBindToVertexNormal: +0x48 -> &source[i].normal     (read normal at +0)
//                        (sun light-cache frame, @0x5c717c)
//
// `InstanceVertexNormalBinding` records, per instance vertex, the raw pointer the
// engine stored at +0x48 plus the resolved 3-float object-space normal — so the
// bind site can fill the instance's +0x48 slot AND so the wave-7 entries can read
// the same normal without re-chasing engine records.
// =============================================================================
enum class MeshNormalSource {
    kBindToVertexBase,    // +0x48 -> source vertex (normal at +12)  draw/env-map
    kBindToVertexNormal,  // +0x48 -> source normal (normal at +0)   sun cache
};

struct InstanceVertexNormalBinding {
    const void* sourcePtr;   // the pointer the engine stores at instVert+0x48
    float       normal[3];   // the resolved object-space normal (always the +12 normal)
};

// Build the per-instance +0x48 binding for `count` instance vertices (1:1 with the
// first `count` source vertices). `mode` selects which engine convention the
// pointer follows. The resolved `normal[3]` is always the source vertex's +12
// normal (what every reader ultimately uses), regardless of mode. The number of
// instance vertices must not exceed the source vertex count.
void BindInstanceNormals(const SourceMeshVertex* verts, int count,
                         MeshNormalSource mode,
                         InstanceVertexNormalBinding* out);

void BindInstanceNormals(const std::vector<SourceMeshVertex>& verts,
                         MeshNormalSource mode,
                         std::vector<InstanceVertexNormalBinding>& out);

// Flatten the resolved object-space normals (3 floats per instance vertex) for the
// wave-7 consumers: object_light_shade::LightMeshVertices reads MeshLightVertex
// .vnormal; env_map_walk::ComputeEnvMapVertexUvs reads EnvMapWalkInputs.vertexNormals
// (the non-skinned arm). Writes count*3 floats into `outNormals`.
void FlattenInstanceNormals(const InstanceVertexNormalBinding* bindings, int count,
                            float* outNormals);

} // namespace guild::render
