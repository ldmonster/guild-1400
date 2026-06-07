#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — MIRROR REFLECTION SILHOUETTE: the convex-hull silhouette-edge
// extractor that seeds the reflection clip outline. Faithful 1:1 reconstruction of
//
//   0x5F5740  VIBE_Mirror_BuildSilhouettePoints
//
// Given a cloud of `count` points (each a vec3, viewed as direction vectors from
// the origin — the reflected mirror corners), this finds every SILHOUETTE EDGE of
// the point set: an ordered pair (j,i) is a silhouette edge when the plane through
// the origin spanned by points j and i has ALL other points on its non-negative
// side. The plane normal is TriangleNormal(0, pt[j], pt[i]) (the normalized cross
// product, reused from util/math); a point pt[k] is on the negative side when
//   normal · pt[k] < dbl_62C2F0   (dbl_62C2F0 == -0.01, a small tolerance).
//
// Found edges are emitted as consecutive (pt[j], pt[i]) pointer pairs into the
// caller's output array, and the symmetric connectivity matrix entries
// conn[j*count+i] and conn[i*count+j] are stamped 1 so the outline tracer
// (VIBE_Mirror_CreateOutline @0x5F58FC) can chain them. The original returns 1.
//
// The original signature is __userpurge (count@eax, conn@edx, outCount@ecx,
// points@ebx, outPairs@stack). `conn` is a count*count byte matrix (0 == edge not
// yet taken), `points` a count-entry array of vec3 pointers, `outPairs` receives
// 2 pointers per silhouette edge, `*outCount` the running pointer count.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5f5740 — VIBE_Mirror_BuildSilhouettePoints.
//   conn      : count*count connectivity matrix (zeroed by the caller); on output
//               conn[j*count+i] == conn[i*count+j] == 1 for each emitted edge.
//   outCount  : running count of pointers written to outPairs (set to 0 here,
//               incremented by 2 per edge).
//   points    : `count` vec3 direction vectors (the reflected mirror corners).
//   outPairs  : receives (points[j], points[i]) for each silhouette edge.
// For every ordered pair (j,i), j!=i, with conn[j*count+i]==0, the edge is a
// silhouette when no OTHER point lies on the negative side of the origin plane
// normal = TriangleNormal(0, points[j], points[i]). Always returns true.
bool BuildSilhouettePoints(i32 count, u8* conn, i32* outCount,
                           float* const* points, const float** outPairs);

} // namespace guild::render
