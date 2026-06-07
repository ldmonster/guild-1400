#include "render/mirror_silhouette.h"

#include "util/math.h"   // TriangleNormal (VIBE_Math_TriangleNormal @0x5cb824)

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

} // namespace guild::render
