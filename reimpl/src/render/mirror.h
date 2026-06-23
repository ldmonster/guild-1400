#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"
#include "render/mesh.h"   // DrawList

// =============================================================================
// guild::render — MIRROR reflection geometry: reflect a mesh's vertices across a
// mirror plane, cull the reflected box against the mirror's clip planes, and append
// the reflected (re-wound) polygons to the draw list. Faithful 1:1 reconstruction
// of the geometry cores of:
//
//   0x5F637C  VIBE_Mirror_BuildMirroredGeometry  (reflected-poly draw-list append)
//   0x5F6148  VIBE_Mirror_ClipPolygonToPlanes    (reflect + 8-corner clip-cull)
//
// THE REFLECTION (byte-for-byte, ClipPolygonToPlanes inner loop)
// ---------------------------------------------------------------------------
// A point P is reflected across the mirror plane (n = (nx,ny,nz), d) by:
//   t = -(P·n - d) * 2.0           (flt_62C3A0 = 2.0)
//   P' = P + t·n
// (the engine's plane normal is unit-length, so this is the standard mirror
// reflection). The mirror plane lives at mirror+24/28/32 (normal) and +36 (d).
//
// THE BOX CLIP-CULL (ClipPolygonToPlanes, byte-for-byte)
// ---------------------------------------------------------------------------
// The 8 reflected box corners (stride 20 floats) are tested against the mirror's
// clip-plane list (count at plane[0], each plane: normal at +0/+4/+8, d at +5
// floats / +20 bytes). A corner is INSIDE plane k when n·P >= plane.d. If for ANY
// plane all 8 corners are outside (>= 8 outside), the mirror is fully culled
// (returns false). Otherwise it expands the running near/far depth bounds
// (flt_13FD168 min, flt_13FCF3C max) over the reflected corners' z and returns true.
//
// THE REFLECTED-POLY APPEND (BuildMirroredGeometry, LABEL_22, byte-for-byte)
// ---------------------------------------------------------------------------
// For each mirrored poly (40-byte stride), the engine appends it to the PolyList
// when the poly is front-facing AFTER reflection (reflection flips winding, so the
// signed screen area test is reversed vs the normal path) and at least one vertex
// is near enough (z field < 0x33D6E555). Sort key = ((tex - dword_1406A84) >> 7) + 1
// when textured, else 0.
// =============================================================================
namespace guild::render {

// A mirror plane: unit normal + plane distance d (mirror+24/28/32/36).
struct MirrorPlane { float nx, ny, nz, d; };

// gilde.exe 0x5f6148 — reflect a point across the mirror plane (the inner reflect).
//   t = -(P·n - d) * 2.0 ; out = P + t·n.
void ReflectPointAcrossPlane(const float P[3], const MirrorPlane& m, float out[3]);

// A clip plane for the mirror frustum-style cull: inside when nx*x+ny*y+nz*z >= d.
struct MirrorClipPlane { float nx, ny, nz, d; };

// gilde.exe 0x5f6148 — VIBE_Mirror_ClipPolygonToPlanes (the box clip-cull, after
// the 8 corners have been reflected). `reflected` is the 8 reflected corner points
// (stride `cornerStrideFloats` floats, default 20 == the engine's vertex stride).
// `planes`/`planeCount` are the mirror clip planes. Returns false (culled) when any
// plane has all 8 corners outside; otherwise expands [*near,*far] over the corners'
// z and returns true. `runningNear`/`runningFar` mirror flt_13FD168/flt_13FCF3C.
bool ClipReflectedBox(const float* reflected, int cornerStrideFloats,
                      const MirrorClipPlane* planes, int planeCount,
                      float* runningNear, float* runningFar);

// gilde.exe 0x5f637c — VIBE_Mirror_BuildMirroredGeometry (the reflected-poly append).
// Append the mirror's reflected polygons to `out`. The reflected vertices live in
// `polys[i].v0/v1/v2` (already reflected + projected, screen x/y at +16/+20, depth
// gate at +28). A poly is appended when:
//   - its texture id (texSortId[i]) differs from `mirrorViewTexId` (skip the mirror
//     surface itself), AND
//   - at least one vertex's depth field (v->screenY-adjacent +28, modelled here as
//     `vertexDepthLt[i*3 + k]` < 0x33D6E555 — the near gate), AND
//   - either the poly's no-cull bit (flags38 & 4) is set, OR the reflected signed
//     screen area is positive (winding flipped by the reflection).
// sortKey = texSortId[i] (== ((tex-base)>>7)+1) when textured, else 0. Honors the
// remaining capacity. Returns the number of polys appended.
i32 AppendMirroredPolys(const Polygon* polys, i32 polyCount,
                        const u32* texSortId, u32 mirrorViewTexId,
                        const bool* anyVertexNear, DrawList* out);

// =============================================================================
// THE CLEAN MIRROR-PASS ENTRY (the CityView3D / BeginUniverseFrame handoff)
// =============================================================================
// In the original, the reflection pass is the LAST geometry stage of
// render::BeginUniverseFrame @0x5b3900, emitted right after the main scene walk,
// the particle systems and the sky flares, just before the depth-bound fixups:
//
//   /*0x5b3af0*/ if ( (byte_14080EC[0] & 0x40) != 0   // mirror feature enabled
//                  && dword_649D6C                     // a reflection node was prepared
//                  && dword_1408A74 && dword_1408A78   // mirror plane params present
//                  && (byte_1408A98 & 1) != 0 )        // mirror runtime-active bit
//       VIBE_SceneGraph_WalkAndInvoke(off_649D64, 0,
//           /*per-node cb=*/VIBE_Mirror_BuildMirroredGeometry, frame, &dword_1408A70);
//
// i.e. it re-walks the scene graph and invokes BuildMirroredGeometry @0x5F637C
// (-> ClipPolygonToPlanes @0x5F6148 -> AppendMirroredPolys above) on every node,
// appending each node's reflected polygons into the SAME global draw list the
// main pass filled, so the reflection is rasterized together with the scene.
//
// `dword_649D6C` is set non-zero by VIBE_Mirror_PrepareReflectionNode @0x5F676C
// (reached from the scene walk) once a reflective surface has been bound and its
// clip planes built (VIBE_Mirror_CreateClippingPlanes @0x5F5D08 ->
// VIBE_Mirror_CreateOutline @0x5F58FC -> BuildSilhouettePoints above).
//
// MirrorPassGate captures the exact 4-term enable predicate so the bind site
// (play::CityView3D, which the orchestrator owns) can gate the pass identically.
struct MirrorPassGate {
    bool  featureEnabled;     // byte_14080EC[0] & 0x40
    bool  reflectionPrepared; // dword_649D6C != 0
    bool  planeParamA;        // dword_1408A74 != 0
    bool  planeParamB;        // dword_1408A78 != 0
    bool  runtimeActive;      // byte_1408A98 & 1
};

// gilde.exe 0x5b3af0 (predicate) — true when the mirror reflection pass should
// run this frame. The caller then walks the scene graph invoking the per-node
// AppendMirroredPolys path. Pure (no side effects) so it is trivially testable.
inline bool ShouldRenderMirrorPass(const MirrorPassGate& g) {
    return g.featureEnabled && g.reflectionPrepared && g.planeParamA
        && g.planeParamB && g.runtimeActive;
}

} // namespace guild::render
