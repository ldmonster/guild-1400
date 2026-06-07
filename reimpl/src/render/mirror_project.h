#pragma once
#include "guild/common/types.h"
#include "render/mirror.h"   // MirrorPlane

// =============================================================================
// guild::render — Mirror remainder: reflect + project a vertex stream.
//
// Faithful 1:1 reconstruction of:
//   0x5F6084  VIBE_Mirror_ProjectReflectedVertices
//
// For each vertex (stride 20 floats: xyz at +0/+4/+8) the engine reflects the
// point across the mirror plane (flt_1408A88..90 = unit normal, flt_1408A94 = d,
// flt_62C39C = 2.0) IN PLACE, then perspective-projects it, writing:
//   t  (the reflection scalar)        -> +7   (*(v2-13) after the +=20 step)
//   screenX = (px*projX)*(1/z) + ox   -> +4   (*(v2-16))
//   screenY = (py*projY)*(1/z) + oy   -> +5   (*(v2-15))
// where projX=flt_13FCD0C, projY=flt_13FCAF8, ox=flt_13FCD18, oy=flt_13FCD10.
//
// The reflection is the standard mirror reflection (unit-length plane normal):
//   t  = -(P·n - d) * 2.0
//   P' = P + t·n     (written back to +0/+4/+8)
// matching ReflectPointAcrossPlane (render/mirror). The projection scalars were
// file-scope globals; we gather them into ProjectionParams so the routine is
// re-entrant and testable.
// =============================================================================
namespace guild::render {

// One vertex of the engine's 20-float stride record (only the fields this
// routine touches; the rest of the 20 floats are opaque scratch).
struct MirrorVertex {
    float x, y, z;     // +0/+4/+8  model-space (reflected in place)
    float screenX;     // +16 (index 4) projected screen X
    float screenY;     // +20 (index 5) projected screen Y
    float t;           // +28 (index 7) reflection scalar
};

// Perspective projection scalars (mirror flt_13FCD0C/AF8/D18/D10).
struct ProjectionParams {
    float projX;  // flt_13FCD0C  x scale
    float projY;  // flt_13FCAF8  y scale
    float offX;   // flt_13FCD18  x offset
    float offY;   // flt_13FCD10  y offset
};

// gilde.exe 0x5F6084 — VIBE_Mirror_ProjectReflectedVertices (result@eax=verts,
// edx=count). Reflect each of `count` vertices across `plane` and project, in the
// exact FPU evaluation order of the original.
void ReflectAndProjectVertices(MirrorVertex* verts, int count,
                               const MirrorPlane& plane,
                               const ProjectionParams& proj);

} // namespace guild::render
