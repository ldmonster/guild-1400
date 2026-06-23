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

// =============================================================================
// gilde.exe 0x5F58FC — VIBE_Mirror_CreateOutline
//   __usercall al = fn(points@eax, count@edx, outArray@ecx, outCount@ebx)
//
// Chains the silhouette edges produced by BuildSilhouettePoints into ONE ordered
// boundary loop (the reflection clip outline). Given `count` unique points, it:
//   1. allocates the silhouette pair buffer (32*count bytes) and the connectivity
//      matrix (count*(count+8) bytes), runs BuildSilhouettePoints to fill them and
//      set `pairCount` (= 2 * number of edges),
//   2. walks the edge set, starting at edge 0, repeatedly choosing the next edge
//      that shares the current endpoint AND whose direction is collinear (within
//      0.001 per component, either orientation) with the running edge direction —
//      i.e. it merges colinear edges and follows the boundary — emitting each
//      distinct (a,b) point pair into the output array (dedup'd against pairs
//      already emitted in either orientation),
//   3. when a chain closes (no unvisited edge continues it), it restarts from the
//      first not-yet-taken connectivity entry, until all are consumed.
//
// On success returns true and writes:
//   *outArray = the allocated ordered outline pointer array (caller frees),
//   *outCount = the number of POINTERS written (2 per emitted edge).
// Returns false (writes nothing) when count < 3.
//
// `points`   : `count` vec3 pointers (the deduplicated reflected mirror corners).
// `outArray` : receives the allocated ordered (a0,b0,a1,b1,...) pointer array.
// `outCount` : receives the pointer count (always even).
//
// The connectivity/pair scratch and the output array are obtained from the
// module allocator hook (see SetMirrorAllocHook in mirror_project.h) so this is
// link-clean and standalone-testable; the arithmetic is 1:1 with the decompile.
bool CreateOutline(float* const* points, u32 count,
                   const float*** outArray, u32* outCount);

} // namespace guild::render
