#pragma once
// guild::render — per-frame animation NORMAL + bounds recompute, reconstructed 1:1.
//
//   VIBE_Anim_CalculateAnimNormals @0x5d0020 — after a morph/skeletal animation deforms a
//   mesh, the per-vertex normals and the per-frame bounding box are STALE; this pass
//   recomputes them for every frame of the clip. For each frame it:
//     1. takes the frame's DEFORMED vertex positions (the morph base + the frame's point
//        deltas; the engine builds these into a scratch `points` buffer first),
//     2. computes each triangle's face normal (VIBE_Math_TriangleNormal @0x5cb824),
//     3. sets each vertex normal = normalized sum of its adjacent face normals
//        (VIBE_Math_VectorNormalize @0x5cb148) — identical to the static
//        VIBE_Mesh_ComputeVertexNormals @0x5D1A6C, but per animation frame,
//     4. records the frame's min/max bbox,
//   then a second pass SMOOTHS each frame's bbox to the union of its (f-1, f, f+1) neighbour
//   bboxes (the engine's anti-popping pass over dword frame+60..80).
//
// The engine additionally QUANTIZES each normal to a byte (frame+184): it adds the bias
// dword_5CBA30 == (1,1,1) and scales by dbl_628F04 (0.5) * dbl_628F0C, mapping [-1,1] -> a
// 0..255 byte (storage compression). This reconstruction keeps the FLOAT normals (what the
// reimpl's lit posed-mesh path consumes); `QuantizeNormalByte` exposes the engine's byte
// encoding for callers that need the exact stored form.
#include <array>
#include <cstdint>
#include <vector>

namespace guild::render {

struct AnimFrameBounds {
    float bbMin[3] = { 1e10f,  1e10f,  1e10f};
    float bbMax[3] = {-1e10f, -1e10f, -1e10f};
};

// gilde.exe 0x5d0020 — VIBE_Anim_CalculateAnimNormals (math core).
// `frames[f]` is frame f's deformed vertex positions (vertexCount*3 floats). `triangles`
// is the shared index list (vertex index triples). Fills `outNormals[f]` (vertexCount*3
// unit normals) and `outBounds[f]` (the neighbour-smoothed bbox).
void CalculateAnimNormals(const std::vector<std::vector<float>>& frames,
                          const std::vector<std::array<int, 3>>& triangles,
                          int vertexCount,
                          std::vector<std::vector<float>>& outNormals,
                          std::vector<AnimFrameBounds>& outBounds);

// The engine's per-component normal -> byte encoding (frame+184): (n + 1) * 0.5 * 255,
// clamped/truncated toward zero (VIBE_Coord_ConvertX). Exposed for the exact stored form.
std::uint8_t QuantizeNormalByte(float n);

struct AnimClip;   // render/agf_anim.h — the loaded morph clip (per-frame point arrays)

// Clip-level wrapper: the engine runs CalculateAnimNormals once at load over EVERY frame of
// a clip (the per-frame morph points) using the base mesh's `triangles` topology. Produces
// per-frame per-vertex normals + the neighbour-smoothed per-frame bounds. The per-frame
// relight (VIBE_Mesh_ComputeVertexLighting) then consumes outNormals[f] for frame f.
void CalculateClipNormals(const AnimClip& clip,
                          const std::vector<std::array<int, 3>>& triangles,
                          std::vector<std::vector<float>>& outNormals,
                          std::vector<AnimFrameBounds>& outBounds);

} // namespace guild::render
