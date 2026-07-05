// ===========================================================================
// gilde.exe — pick / selection / drag-select / drag-cursor / interaction PURE
// math, reconstructed 1:1. See picksel_recon.h for the provenance manifest.
// ===========================================================================
#include "play/picksel_recon.h"
#include <cmath>
#include <cstring>

namespace guild::play {

using namespace picksel_const;

// ---------------------------------------------------------------------------
// Bucket array (0x5b5aec init loop / 0x5b5b6c tail).
// Original init:  for (i=0;i!=18;) { i+=2; slot[i].dist=1343554297; slot[i-2].obj? }
// The decompile writes dword_13FD460[i]=1343554297 (the dist) then on the loop
// update dword_13FD45C[i]=0 (the obj). Net effect: every slot.obj=0, slot.dist=
// 1e10f. We seed each of the 9 slots identically.
// ---------------------------------------------------------------------------
void PickBucketsSeed(PickBucket buckets[kPickBucketCount]) {
    float seed;
    std::memcpy(&seed, &kBucketDistSeedBits, sizeof(seed));  // 1343554297 -> 1e10f
    for (int i = 0; i < kPickBucketCount; ++i) {
        buckets[i].object   = 0;
        buckets[i].bestDist = seed;
    }
}

// 0x5b5b6c tail:
//   v12 = 1.0e10; for (j=0;j!=18;j+=2) if (slot.obj && slot.dist < v12){...}
i32 PickBucketsNearest(const PickBucket buckets[kPickBucketCount]) {
    i32 best = 0;
    double running = 1.0e10;
    for (int j = 0; j < kPickBucketCount; ++j) {
        if (buckets[j].object && (double)buckets[j].bestDist < running) {
            best    = buckets[j].object;
            running = buckets[j].bestDist;
        }
    }
    return best;
}

// 0x5b5938 inner test:  if (v7 < slot.bestDist && sqrt(v7) < projRadius) store.
bool PickScreenDistTest(float hitDistSq, float projRadius,
                        float curBestDist, float* outDistSq) {
    if (hitDistSq < curBestDist && std::sqrt((double)hitDistSq) < projRadius) {
        if (outDistSq) *outDistSq = hitDistSq;
        return true;
    }
    return false;
}

// ===========================================================================
// 0x5b7134 — VIBE_Pick_ComputeSelectionVolume barycentric ray-quad solve.
//
// 1:1 translation. The scene walk has already produced 4 corner points
// (cornerPos[k]) and 4 |u|,|v| pairs (cornerUV[k]). We mirror the decompile:
//   1. select the corner v95 with max (|u_b|-|u_a|) -> "uAxis" corner,
//      v16 with max (|u|+|v|) -> "far" corner, v15 with min (|u|+|v|) -> "near"
//      corner; reject on degenerate selections.
//   2. build the cursor ray, intersect against triangle (v95,v16,v15); if the
//      hit barycentric (s+t) exceeds 1 fall through to the complementary
//      triangle (k = the 4th corner).
//   3. interpolate (u,v) from the winning barycentric.
// ===========================================================================
char ComputeSelectionVolumeSolve(const float cornerPos[4][3],
                                 const float cornerUV[4][2],
                                 float mouseX, float mouseY,
                                 const ProjectParams& pp,
                                 float* outU, float* outV) {
    // --- min/max over the 4 |u|,|v| (the v80/v81 reduction in the original) ---
    // The original first computes per-corner min |u| and min |v| over the 4
    // corners (v96/v97), then rebases the uv: cornerUV[k] -= (minU,minV), and the
    // |v| component becomes 1 - |v|. We replicate against local copies.
    // The original seeds v80 = loc_5B3E28 and v81 = *(float*)(&loc_5B3E29+3);
    // both byte windows decode to 1e10f, so minU and minV both start at +1e10f
    // and run a per-corner min over |u| / |v|.
    float uv[4][2];
    float minU = kBigDist;
    float minV = kBigDist;
    for (int k = 0; k < 4; ++k) {
        float u = cornerUV[k][0];
        float v = cornerUV[k][1];
        if (minU >= u) minU = u;
        if (minV >= v) minV = v;
    }
    for (int k = 0; k < 4; ++k) {
        uv[k][0] = cornerUV[k][0] - minU;
        uv[k][1] = 1.0f - (cornerUV[k][1] - minV);
    }

    // --- pick the three spanning corners (v95 max span, v16 max sum, v15 min) -
    int v95 = -1, v16 = -1, v15 = -1;
    float maxSpan = -kBigDist, maxSum = -kBigDist, minSum = kBigDist;
    for (int k = 0; k < 4; ++k) {
        double au = std::fabs(uv[k][0]);
        double av = std::fabs(uv[k][1]);
        double span = av - au;
        if (maxSpan < span) { v95 = k; maxSpan = (float)span; }
        double sum = au + av;
        if (maxSum < sum) { v16 = k; maxSum = (float)sum; }
        if (minSum > sum) { v15 = k; minSum = (float)sum; }
    }
    if (v95 == -1 || v16 == -1 || v15 == -1 ||
        v16 == v95 || v15 == v95 || v16 == v15)
        return 0;

    // k = the 4th corner (the one not chosen)
    int k4 = 0;
    while (k4 == v16 || k4 == v95 || k4 == v15) ++k4;

    // --- ray origin/dir from cursor (v65..v70 / v68/v69) ----------------------
    const float ox = 0.0f, oy = 0.0f, oz = 0.0f;          // v65/v66/v67
    const float dx = mouseX - pp.centerX;                  // v68 = a1 - flt_13FCD18
    const float dy = -(mouseY - pp.centerY);               // v69 = -(a2 - flt_13FCD10)
    const float dz = pp.scaleX;                            // v70 = flt_13FCD0C

    // ----- triangle (apex=v95, e1=v16, e2=v15) --------------------------------
    // (`su` receives the FULL-precision t (v31 stays on the x87 stack for the
    //  acceptance compare); the float truncations v53/v58 are taken by the
    //  caller exactly where the original stores them.)
    auto solveTri = [&](int apex, int b, int c, double* su, float* sv) -> char {
        const float* P = cornerPos[apex];
        const float* B = cornerPos[b];
        const float* C = cornerPos[c];
        // e_b = P - B (v74/v75/v76), e_c = P - C (v62/v63/v64)
        float e1x = P[0]-B[0], e1y = P[1]-B[1], e1z = P[2]-B[2];
        float e2x = P[0]-C[0], e2y = P[1]-C[1], e2z = P[2]-C[2];
        // normal n = e_b x e_c   (v71/v72/v73; fsubrp chain @0x5b74b7..0x5b74e6)
        float nx = e2z*e1y - e2y*e1z;   // v71 = v64*v75 - v63*v76
        float ny = e2x*e1z - e2z*e1x;   // v72 = v62*v76 - v64*v74
        float nz = e2y*e1x - e2x*e1y;   // v73 = v63*v74 - v62*v75
        float planeD = nx*P[0] + ny*P[1] + nz*P[2];   // v83/v82
        double denom = nx*dx + ny*dy + nz*dz;          // v24/v41
        if (std::fabs(denom) <= kParallelEps) return 0;
        // fst var_150 @0x5b7583 truncates the denominator to FLOAT before the
        // fdiv @0x5b75c3 (the fabs test above uses the pre-truncation value).
        const float denomF = (float)denom;             // v56
        double tt = (nx*ox + ny*oy + nz*oz + planeD) / denomF;  // v25/v42
        float hx = (float)(dx*tt) + ox;     // v89
        float hy = (float)(dy*tt) + oy;     // v90
        float hz = (float)(dz*tt) + oz;     // v92
        float qx = P[0]-hx;  // v91
        float qy = P[1]-hy;  // v93
        float qz = P[2]-hz;  // v94
        // barycentric s = (e_b.x*q.y - e_c?.. ) — follow the decompile exactly:
        //   v58 = (v75*v91 - v74*v93)/(v75*v62 - v74*v63)
        double s = (e1y*qx - e1x*qy) / (e1y*e2x - e1x*e2y);   // v26 (v58 = float)
        // then re-project q -= s*e_c (with the full-precision s = v27 = -v26)
        // and average the three ratios * 1/3 -> t
        qx = (float)(-s*e2x + qx);   // v91
        qy = (float)(-s*e2y + qy);   // v93
        qz = (float)(-s*e2z + qz);   // v94
        double t = ((qy/e1y) + (qx/e1x) + (qz/e1z)) * (double)kThird;  // v31
        *su = t;
        *sv = (float)s;
        return 1;
    };

    float s_first = 0.0f;
    double t_full = 0.0;
    if (!solveTri(v95, v16, v15, &t_full, &s_first))
        return 0;
    const float t_first = (float)t_full;   // v53 = v31 @0x5b7731

    // first triangle accepted iff t + s <= 1 (the original adds the x87
    // FULL-precision v31 to the float v58 @0x5b7743) and both in [0,1]
    if (t_full + (double)s_first <= 1.0) {
        if (t_first >= 0.0f && t_first <= 1.0f &&
            s_first >= 0.0f && s_first <= 1.0f) {
            // u along v95..v16, v along v95..v15
            double uA = std::fabs(uv[v95][0]);
            *outU = (float)(uA + (std::fabs(uv[v16][0]) - uA) * t_first);
            double vA = std::fabs(uv[v95][1]);
            *outV = (float)(1.0 - (vA + (std::fabs(uv[v15][1]) - vA) * s_first));
            return 1;
        }
        return 0;
    }

    // --- complementary triangle (apex = k4, edges toward v16 and v15) ---------
    {
        const float* P = cornerPos[k4];
        const float* B = cornerPos[v16];
        const float* C = cornerPos[v15];
        float e1x = P[0]-B[0], e1y = P[1]-B[1], e1z = P[2]-B[2]; // v62/63/64 -> e toward v16
        float e2x = P[0]-C[0], e2y = P[1]-C[1], e2z = P[2]-C[2]; // v74/75/76 -> e toward v15
        // n = e1 x e2 (v71=v64*v75-v63*v76 with v62/63/64 = e1, v74/75/76 = e2)
        float nx = e1z*e2y - e1y*e2z;   // v71 = v64*v75 - v63*v76
        float ny = e1x*e2z - e1z*e2x;   // v72 = v62*v76 - v64*v74
        float nz = e1y*e2x - e1x*e2y;   // v73 = v63*v74 - v62*v75
        float planeD = nx*P[0] + ny*P[1] + nz*P[2];   // v82
        double denom = nx*dx + ny*dy + nz*dz;          // v41
        if (std::fabs(denom) <= kParallelEps) return 0;
        // fst/fdiv float-denominator truncation, same as the first triangle
        // (v54 = v41 @0x5b7925, fdiv by the float @0x5b7965).
        const float denomF = (float)denom;             // v54
        double tt = (nx*ox + ny*oy + nz*oz + planeD) / denomF;
        float hx = (float)(dx*tt) + ox;
        float hy = (float)(dy*tt) + oy;
        float hz = (float)(dz*tt) + oz;
        float qx = P[0]-hx, qy = P[1]-hy, qz = P[2]-hz;
        // v86 = (v75*v91 - v74*v93)/(v75*v62 - v74*v63) with v74/75/76=e2, v62/63/64=e1
        double s = (e2y*qx - e2x*qy) / (e2y*e1x - e2x*e1y);   // v43 (*(float*)&v86)
        qx = (float)(-s*e1x + qx);
        qy = (float)(-s*e1y + qy);
        qz = (float)(-s*e1z + qz);
        // v85 = (v45/v75 + v46/v74 + v47/v76) * third  ->  qy/e2y + qx/e2x + qz/e2z
        double tline = ((qy/e2y) + (qx/e2x) + (qz/e2z)) * (double)kThird; // *(float*)&v85
        // The original stores v85/v86 as FLOATS and range-checks/interpolates
        // with those float values (SLODWORD compares @0x5b7ae1..0x5b7b0b).
        const float tlineF = (float)tline;   // *(float*)&v85
        const float sF     = (float)s;       // *(float*)&v86
        if (tlineF < 0.0f || tlineF > 1.0f || sF < 0.0f || sF > 1.0f)
            return 0;
        double uA = std::fabs(uv[v95][0]);
        *outU = (float)(uA + (std::fabs(uv[v16][0]) - uA) * (1.0 - tlineF));
        double vA = std::fabs(uv[v95][1]);
        *outV = (float)(1.0 - (vA + (std::fabs(uv[v15][1]) - vA) * (1.0 - sF)));
        return 1;
    }
}

// ===========================================================================
// Drag-select box clamp + box math.
// ===========================================================================
// EXACT decompiled clamp idiom: t = (v < lo) ? lo : v; then if t >= hi-1 use
// hi-1 else recompute (v<lo?lo:v). Net = clamp(v, lo, hi-1).
int DragClampX(int v, const Viewport& vp) {
    int t = v;
    if (v < vp.x0) t = vp.x0;
    if (t >= vp.x1 - 1) return vp.x1 - 1;
    int r = v;
    if (v < vp.x0) r = vp.x0;
    return r;
}
int DragClampY(int v, const Viewport& vp) {
    int t = v;
    if (v < vp.y0) t = vp.y0;
    if (t >= vp.y1 - 1) return vp.y1 - 1;
    int r = v;
    if (v < vp.y0) r = vp.y0;
    return r;
}

// 0x4bdb98 — seed both corners to the clamped cursor.
void DragSelectBeginBox(DragBox& box, int cursorX16, int cursorY16,
                        const Viewport& vp) {
    int cx = cursorX16 >> 16;   // unk_67220E >> 16
    int cy = cursorY16 >> 16;   // dword_672210 >> 16
    int v1 = DragClampX(cx, vp);
    int v3 = DragClampY(cy, vp);
    box.ax = v1; box.ay = v3;
    box.bx = v1; box.by = v3;
    box.active = 1;
}

// 0x4bdc30
void DragSelectCancel(DragBox& box) { box.active = 0; }

void DragSelectUpdateDragCorner(DragBox& box, int cursorX16, int cursorY16,
                                const Viewport& vp) {
    int cx = cursorX16 >> 16;
    int cy = cursorY16 >> 16;
    box.bx = DragClampX(cx, vp);
    box.by = DragClampY(cy, vp);
}

// v4/v23 (min/max X), v24/v22 (min/max Y) normalisation from ApplyToUnits.
DragRect DragSelectNormalize(const DragBox& box) {
    DragRect r;
    r.minX = (box.ax >= box.bx) ? box.bx : box.ax;   // v4
    r.minY = (box.ay >= box.by) ? box.by : box.ay;   // v24
    r.maxX = (box.ax <= box.bx) ? box.bx : box.ax;   // v23
    r.maxY = (box.ay <= box.by) ? box.by : box.ay;   // v22
    return r;
}

// Apply-loop predicate: minX<=sx && maxX>=sx && minY<=sy && maxY>=sy.
bool DragBoxContains(const DragRect& r, float sx, float sy) {
    return (double)r.minX <= sx && (double)r.maxX >= sx &&
           (double)r.minY <= sy && (double)r.maxY >= sy;
}

bool DragUnitCentroidHit(float sumX, float sumY, float sumZ, float weight,
                         const ProjectParams& pp, const DragRect& r) {
    float cx = sumX * weight;   // v28 = v27 * dbl_61E208 (float store)
    float cy = sumY * weight;   // v30
    double inv = 1.0 / ((double)weight * (double)sumZ);   // v20
    // 0x4bde16 / 0x4bde30: ONE float store each — the + center happens on the
    // x87 stack BEFORE truncation (v25 = scaleX*v28*v20 + flt_13FCD18).
    float sx = (float)((double)pp.scaleX * cx * inv + pp.centerX);
    float sy = (float)(inv * ((double)pp.scaleY * cy) + pp.centerY);
    return DragBoxContains(r, sx, sy);
}

// ===========================================================================
// Drag cursor mode (0x41f860 / 0x41f878 / 0x4c094c).
// ===========================================================================
void DragCursorReset(DragCursorState& st) {
    st.lastButton = -1;     // dword_75BF38 = -1
    // VIBE_Input_ResetMouseButtonState() — coupled input leaf (inert here).
    st.flag = 0;            // byte_67225C = 0
}

i16 DragCursorSetMode(DragCursorState& st, i16 mode) {
    st.mode = mode;         // word_62D310 = mode
    return mode;
}

// 0x4c094c — mode -> (dx,dy) mapping. The render call itself (RenderMouse) is a
// coupled leaf; we return the offsets it is invoked with.
CursorOffset DragCursorRenderForState(const DragCursorState& st) {
    int v0 = 0, v1 = 0;
    i16 m = st.mode;
    if (m == 1) { v1 = -48; return {v0, v1}; }
    if (m == 3 || m == 6) { v0 = -40; return {v0, v1}; }
    if (m != 8) {
        if (m != 7) return {v0, v1};
        v1 = -48; return {v0, v1};   // LABEL_6
    }
    return {-40, -48};
}

// ===========================================================================
// Interaction panel predicates (0x595e54 / 0x595f70 / 0x595e74).
// ===========================================================================
bool InteractionIsPanelModeTwo(const PanelState& ps) {
    return ps.enabled && ps.mode == 2;
}

bool InteractionIsPanelActive(const PanelState& ps) {
    return !ps.enabled || (ps.mode != 2 && ps.kind != 5);
}

int InteractionInvokeHandlerSlot60(const PanelState& ps,
                                   int (*handler)(char,int,int,int),
                                   char a1, int a2, int a3, int a4) {
    if (!ps.enabled) return 1;          // dword_649CD0 == 0
    if (!ps.hasHandler) return 1;       // off_5953F0+20 == 0
    if (ps.blocked) return 0;           // dword_62EB4C
    if (!handler) return 1;             // (+20)+60 == 0
    // original: (*(slot60))(a1, a2, a4, a3) — note the a4/a3 swap at the call.
    return handler(a1, a2, a4, a3);
}

} // namespace guild::play
