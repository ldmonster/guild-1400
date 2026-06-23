#include "render/mirror_silhouette.h"
#include "render/mirror_project.h"   // MirrorAlloc / MirrorFree (the 0x438f10 hook)

#include "util/math.h"   // TriangleNormal/VectorNormalize/VectorWithinTolerance

namespace guild::render {

namespace {
// gilde.exe flt_5CA2E0 == {0,0,0} — the origin reference TriangleNormal cross is
// taken from (reused across the module as the zero vector).
float kZeroVec[3] = {0.0f, 0.0f, 0.0f};
// gilde.exe dbl_62C2F0 == -0.01 (0xBF847AE147AE147B, recovered via get_bytes) —
// the "behind the silhouette plane" tolerance.
constexpr double kSilhouetteTol = -0.01;
} // namespace

// gilde.exe 0x5f5740 — VIBE_Mirror_BuildSilhouettePoints.
bool BuildSilhouettePoints(i32 count, u8* conn, i32* outCount,
                           float* const* points, const float** outPairs) {
    *outCount = 0;
    if (count == 0)
        return true;

    // Outer j (v28): conn row base j*count. Inner i (v6): conn[j*count + i].
    for (i32 j = 0; j < count; ++j) {
        u8* connRowJ = conn + (i32)j * count;   // v25/v17 base (conn + j*count)
        u8* connColJ = conn + j;                // v27 base (conn + j), strides +count
        for (i32 i = 0; i < count; ++i, connColJ += count) {
            // Skip the diagonal (i==j) and edges already taken (conn[j][i] != 0).
            if (i == j || connRowJ[i] != 0)
                continue;

            // normal = TriangleNormal(origin, points[j], points[i]). Original call
            // is (zero, pt[j], OUT, pt[i]); the util reorder puts OUT last, so the
            // arg order becomes (zero, pt[j], pt[i], out).
            float normal[3];
            util::TriangleNormal(kZeroVec, points[j], points[i], normal);

            // All OTHER points must be on the non-negative side of the plane.
            i32 k = 0;
            for (; k < count; ++k) {
                if (k == j || k == i)
                    continue;
                const float* p = points[k];
                double d = (double)normal[0] * p[0] + (double)normal[1] * p[1]
                         + (double)normal[2] * p[2];
                if (d < kSilhouetteTol)
                    break;              // point behind plane -> not a silhouette
            }
            if (k == count) {
                // Silhouette edge (j,i): emit the pointer pair + stamp connectivity.
                outPairs[*outCount] = points[j];   // *(a5 + 4*(*outCount)) = pt[j]
                ++*outCount;
                outPairs[*outCount] = points[i];   // next slot = pt[i]
                ++*outCount;
                connColJ[0] = 1;                   // conn[i*count + j] = 1
                connRowJ[i] = connColJ[0];         // conn[j*count + i] = (that) = 1
            }
        }
    }
    return true;
}

// gilde.exe flt_5CA2E0 == {0,0,0} (already defined above as kZeroVec).

// gilde.exe 0x5F58FC — VIBE_Mirror_CreateOutline.
// Traces the silhouette edges into one (or more) ordered boundary chains, merging
// collinear edges, dedup'ing the result. 1:1 with the decompile; the scratch and
// output buffers come from the module allocator hook (mirror_project.h).
bool CreateOutline(float* const* points, u32 count,
                   const float*** outArray, u32* outCount) {
    if (count < 3)
        return false;                                       // v4 < 3 -> 0

    // The original sizes its pointer buffers in BYTES assuming 4-byte pointers
    // (32-bit x86). This host-portable recon stores real (possibly 8-byte) point
    // pointers, so each pointer buffer is allocated with the SAME slot count
    // (engine_bytes/4 slots) but host pointer width — preserving capacity exactly
    // while staying byte-safe. (kPtrSlot below = the byte size of one host slot.)
    const u32 kPtrSlot = (u32)sizeof(const float*);

    // pairs[] : (a,b) point-pointer pairs of the silhouette edges (2 ptrs/edge).
    // Engine: 32*count bytes == 8*count pointer slots.
    const float** pairs =
        (const float**)MirrorAlloc(8u * count * kPtrSlot, "d3_mir:CreateOutlineMaxOut");
    // conn[] : count*(count+8) BYTES (a genuine u8 matrix — no pointer scaling).
    // BuildSilhouettePoints uses it as the count*count connectivity matrix;
    // afterwards the first 2*pairPtrCount bytes are reused as two parallel byte
    // arrays "emitted" / "visited" (1 per pair).
    u8* conn = (u8*)MirrorAlloc(count * (count + 8u), "d3_mir:track_connections");

    i32 pairPtrCountSigned = 0;
    BuildSilhouettePoints((i32)count, conn, &pairPtrCountSigned, points,
                          pairs);
    const u32 pairPtrCount = (u32)pairPtrCountSigned;   // v49 (== 2 * #edges)

    // Output ordered outline pointer array. Engine: 4*pairPtrCount bytes ==
    // pairPtrCount pointer slots.
    const float** out =
        (const float**)MirrorAlloc(pairPtrCount * kPtrSlot, "d3_mir:CreateOutlineOutput");
    u32 outN = 0;                                          // v59

    if (pairPtrCount != 0) {
        // Two parallel per-pair byte marks packed into conn's head:
        //   emitted[p] = conn[2*p]     (v58 base = conn)
        //   visited[p] = conn[2*p + 1] (v57 base = conn + pairPtrCount)
        // The original zeroes the first 2*pairPtrCount bytes, then marks pair 0.
        u8* emitted = conn;                                // v58
        u8* visited = conn + pairPtrCount;                 // v57
        for (u32 b = 0; b < 2u * pairPtrCount; ++b)
            conn[b] = 0;
        visited[0] = 1;                                    // *v57 = 1
        emitted[0] = visited[0];                           // *conn = *v57 (=1)

        // Current chain endpoints (v4 = chain tail "a", v17 = chain head "b").
        const float* a = pairs[0];                         // v4 = pairs[0]
        const float* b = pairs[1];                         // v17 = pairs[1]
        // Running (normalized) edge direction = b - a.
        float dir[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}; // v46/47/48
        util::VectorNormalize(dir);
        u32 lastFwd = 0;   // v54 (pair index whose visited[] we stamp on a fwd step)
        u32 lastRev = 0;   // v50 (pair index whose visited[] we stamp on a rev step)

        do {
            // Scan all pairs for one that continues the chain at endpoint b (fwd)
            // or endpoint a (rev), is collinear with `dir`, and is not yet emitted.
            u32 i = 0;                                      // pair-ptr index (steps 2)
            bool reverseBreak = false;
            if (pairPtrCount != 0) {
                while (true) {
                    const u32 p = i >> 1;                   // pair index
                    const float* pa = pairs[i];             // pair.a
                    const float* pb = pairs[i + 1];         // pair.b

                    // Forward continuation: an unvisited edge whose .a == b.
                    if (!emitted[i] && b == pa) {
                        float d2[3] = {b[0] - pb[0], b[1] - pb[1], b[2] - pb[2]};
                        util::VectorNormalize(d2);
                        float nd2[3] = {-d2[0], -d2[1], -d2[2]};
                        const float kColTol = 0.001f;
                        if (util::VectorWithinTolerance(dir, d2, kColTol) ||
                            util::VectorWithinTolerance(dir, nd2, kColTol)) {
                            emitted[i] = 1;                 // v58[v7] = 1
                            visited[lastFwd] = 1;           // v57[v54] = 1
                            lastFwd = i;                    // v54 = v7
                            b = pb;                         // advance head: v17 = pair.b
                            goto emitEdge;                  // LABEL_13
                        }
                    }
                    // Reverse continuation: an unvisited edge whose .b == a.
                    if (!emitted[i]) {
                        if (a == pb) {
                            float d2[3] = {pb[0] - pa[0], pb[1] - pa[1],
                                           pb[2] - pa[2]};
                            util::VectorNormalize(d2);
                            float nd2[3] = {-d2[0], -d2[1], -d2[2]};
                            const float kColTol = 0.001f;
                            if (util::VectorWithinTolerance(dir, d2, kColTol) ||
                                util::VectorWithinTolerance(dir, nd2, kColTol)) {
                                reverseBreak = true;        // break -> rev handler
                                break;
                            }
                        }
                    }
                    (void)p;
                    i += 2;
                    if (i >= pairPtrCount)
                        goto emitEdge;                      // LABEL_13
                }
            }
            if (reverseBreak) {
                // Reverse step: extend the tail (a) backwards along this edge.
                emitted[i] = 1;                             // v58[v7] = 1
                visited[lastRev] = 1;                       // v57[v50] = 1
                lastRev = i;                                // v50 = v7
                a = pairs[i];                               // v4 = pair.a
            }

        emitEdge:
            if (i >= pairPtrCount) {
                // Emit the current edge (a,b) unless already present (either order).
                u32 scan = 0;                               // v23
                if (outN != 0) {
                    for (; scan < outN; scan += 2) {
                        if (a == out[scan] && b == out[scan + 1])
                            break;
                        if (a == out[scan + 1] && b == out[scan])
                            break;
                    }
                }
                if (scan >= outN) {
                    out[outN] = a;                          // v55[v59] = v4
                    out[outN + 1] = b;                      // v55[v59+1] = v17
                    outN += 2;
                }

                // Restart the chain from the first not-yet-visited pair.
                i = 0;
                a = nullptr;                                // v4 = 0
                if (pairPtrCount != 0) {
                    u32 vp = 0;                             // index into visited (step 2)
                    while (vp < pairPtrCount && visited[vp]) {
                        vp += 2;
                        i += 2;
                    }
                    if (vp < pairPtrCount) {
                        // Clear the per-pair emitted marks for the new chain.
                        for (u32 z = 0; z < pairPtrCount; ++z)
                            emitted[z] = 0;
                        visited[vp] = 1;                    // *v26 = 1
                        emitted[vp] = visited[vp];          // v58[v7] = *v26
                        a = pairs[vp];                      // v4 = pairs[v7]
                        b = pairs[vp + 1];                  // v17 = pairs[v7+1]
                        dir[0] = b[0] - a[0];
                        dir[1] = b[1] - a[1];
                        dir[2] = b[2] - a[2];
                        util::VectorNormalize(dir);
                        lastRev = vp;                       // v50 = v7
                        lastFwd = vp;                       // v54 = v7
                    }
                }
            }
        } while (a != nullptr);                              // while ( v4 )
    }

    MirrorFree(conn);
    MirrorFree(pairs);
    *outArray = out;       // *a3 = v55 (the ordered outline pointer array)
    *outCount = outN;      // *a4 = v59 (pointer count, always even)
    return true;
}

} // namespace guild::render
