// terrain_collision — see terrain_collision.h. Faithful 1:1 ports of the terrain
// height-range scan + mesh-vs-terrain collision resolver.
#include "sim/terrain_collision.h"
#include "render/terrain_scan.h"   // ScanRowHeightRange (0x426da0) + TerrainHeightGrid
#include "util/coord.h"            // ConvertX (truncate toward zero)

#include <cstring>

namespace guild::sim {

// flt_6115B8 == 0.1f (0x3dcccccd).
const float kProbeHeightLerp = 0.10000000149011612f;

using guild::util::ConvertX;

// ConvertX truncates the value on the FPU stack toward zero, then the original
// does `fistp` (a plain int store of the now-integral st0). Net == (int)trunc(x).
static inline int Trunc(double x) { return static_cast<int>(ConvertX(x)); }

// Adapt the rich TerrainGrid to the row-scan's minimal view (width + heights).
static inline guild::render::TerrainHeightGrid RowView(const TerrainGrid* g) {
    guild::render::TerrainHeightGrid v{};
    v.width = g->width;
    v.heights = g->heights;
    return v;
}

// ===========================================================================
// gilde.exe 0x426e2c — VIBE_Terrain_ScanLineHeightRange.
// Args (recovered from the call site + disasm): grid (eax/edi), lowOut (edx),
// v0 (ecx), highOut (ebx), v1 (eax-loaded arg_0), v2 (edx-loaded arg_4). Each
// vertex is a float* laid out {x at +0, _ at +4, z at +8}.
// ===========================================================================
int ScanLineHeightRange(const TerrainGrid* grid, float* lowOut, const float* v0,
                        float* highOut, const float* v1, const float* v2) {
    const float ox = grid->originX;   // *(grid+144)
    const float oz = grid->originZ;   // *(grid+152)
    const double rsx = 1.0 / grid->scaleX;   // 1.0 / *(grid+160)
    const double rsz = 1.0 / grid->scaleZ;   // 1.0 / *(grid+184)

    // Project + truncate the three vertices into grid space. (fistp order:
    // var_68,var_5C,var_64,var_58,var_60,var_54 — see disasm 0x426e33..0x426ed1.)
    // Vertex A = v0, B = v1, C = v2.  x = (.x - ox)*rsx ; row = (.z - oz)*rsz.
    // The original computes `fld dword[v]; fsub dword[origin]` in the 80-bit x87
    // register (the float difference is kept full-precision, NOT rounded back to a
    // float) before the fmul by the reciprocal scale, so we widen each operand to
    // double before the subtraction to model that 80-bit accumulation exactly.
    int Ax = Trunc((static_cast<double>(v0[0]) - ox) * rsx);   // var_68  0x426e5c
    int Ar = Trunc((static_cast<double>(v0[2]) - oz) * rsz);   // var_5C  0x426e79
    int Bx = Trunc((static_cast<double>(v1[0]) - ox) * rsx);   // var_64  0x426e8c
    int Br = Trunc((static_cast<double>(v1[2]) - oz) * rsz);   // var_58  0x426ea0
    int Cx = Trunc((static_cast<double>(v2[0]) - ox) * rsx);   // var_60  0x426eb5
    int Cr = Trunc((static_cast<double>(v2[2]) - oz) * rsz);   // var_54  0x426ed1

    // Sort the three vertices by row ascending (0x426ed5..0x426f27).
    auto swap = [](int& a, int& b) { int t = a; a = b; b = t; };
    if (Ar > Br) { swap(Ax, Bx); swap(Ar, Br); }   // 0x426ed5 jg
    if (Ar > Cr) { swap(Ax, Cx); swap(Ar, Cr); }   // 0x426ee5 (A vs C)
    if (Br > Cr) { swap(Bx, Cx); swap(Br, Cr); }   // 0x426f0b (B vs C)
    // Now Ar <= Br <= Cr.

    const int width = grid->width;                  // *a1

    // Clip against the grid (0x426f2f..0x4270b1).
    if (Ar >= width || Cr < 0)                       // top row off bottom / all above
        return 1;
    if (Ax < 0 && Bx < 0 && Cx < 0)                  // wholly left of column 0
        return 1;
    if (Ax >= width && Bx >= width && Cx >= width)   // wholly right of last column
        return 1;

    guild::render::TerrainHeightGrid rv = RowView(grid);
    int lo = 255;     // v66 — running min elevation seed
    int hi = 0;       // v67 — running max elevation seed
    int clipped;      // v34 — 1 while every scanned row was fully clipped

    if (Ar == Br && Ar == Cr) {
        // Degenerate (single scanline). The original computes the column span as
        // [min(Ax,Bx,Cx) .. max(Ax,Bx,Cx)] via two if/else nests (0x426f93.. and
        // 0x426fd3..); ScanRowHeightRange normalises the span anyway, but we keep
        // both endpoints faithful.
        //
        // xStart = min(Ax,Bx,Cx)  (>= comparison sense)   0x426f93..0x426fbf
        int m = (Ax >= Bx) ? Bx : Ax;               // 0x426f9b  jge
        int colA;
        if (m >= Cx) {                              // 0x426fa9  jge
            colA = Cx;                              // 0x4270cc
        } else {
            colA = (Ax >= Bx) ? Bx : Ax;            // 0x426fb7  jge
        }
        // colA is truncated again through ConvertX in the original (fild/fistp).
        colA = Trunc(static_cast<double>(colA));    // 0x426fc7..0x42700d
        // xEnd = max(Ax,Bx,Cx)  (<= comparison sense)     0x426fd3..0x426ff7.
        // Passed as a raw int (no ConvertX round-trip).
        int colB = (Ax <= Bx) ? Bx : Ax;           // 0x426fd3  jle
        if (colB <= Cx) {                          // 0x426fdf  jle
            colB = Cx;                             // 0x4270e1
        } else {
            colB = (Ax <= Bx) ? Bx : Ax;           // 0x426fef  jle
        }
        clipped = guild::render::ScanRowHeightRange(&rv, &lo, colA, &hi, colB, Ar);
    } else {
        const int dy = Cr - Ar;                     // v71
        const int dxAC = Cx - Ax;                   // v38
        if (Ar == Br) {
            // Flat top: first scan the top edge A..B at row Ar, then walk both
            // edges A->C and B->C down to row Cr (0x427289..0x42735f).
            float xL = static_cast<float>(Ax);      // v80
            float xR = static_cast<float>(Bx);      // i
            clipped = guild::render::ScanRowHeightRange(&rv, &lo, Ax, &hi, Bx, Ar);
            double dL = static_cast<double>(dxAC) / dy;            // v74 (A->C)
            double dR = static_cast<double>(Cx - Bx) / (Cr - Br); // v75 (B->C)
            xL = static_cast<float>(xL + dL);
            xR = static_cast<float>(xR + dR);
            for (int row = Ar + 1; row <= Cr; ++row) {
                int cR = Trunc(static_cast<double>(xR));  // (int)trunc(i)   0x427327
                int cL = Trunc(static_cast<double>(xL));  // (int)trunc(v80) 0x427338
                int r = guild::render::ScanRowHeightRange(&rv, &lo, cL, &hi, cR, row);
                // The original steps BOTH edges each iteration (0x427347 v80+=dL,
                // 0x427357 i+=dR). The earlier version stepped only xL.
                xL = static_cast<float>(xL + dL);    // 0x427347
                xR = static_cast<float>(xR + dR);    // 0x427357
                clipped &= r;
            }
        } else {
            // General triangle. Upper part rows Ar..Br walk edges A->B and A->C;
            // lower part rows Br+1..Cr walk B->C and (continued) A->C.
            float xAC = static_cast<float>(Ax);     // v81 (the long A->C edge)
            float xAB = static_cast<float>(Ax);     // v77 (the short A->B edge)
            double dAC = static_cast<double>(dxAC) / dy;                  // v76
            double dAB = static_cast<double>(Bx - Ax) / (Br - Ar);        // j
            clipped = 1;                            // v34 = 1
            int row = Ar;                           // v39
            for (; row <= Br; ++row) {
                int cAB = Trunc(static_cast<double>(xAB));   // (int)trunc(v77)  0x42717d
                int cAC = Trunc(static_cast<double>(xAC));   // (int)trunc(v81)  0x42718e
                clipped &= guild::render::ScanRowHeightRange(&rv, &lo, cAB, &hi, cAC, row);
                xAC = static_cast<float>(xAC + dAC);
                xAB = static_cast<float>(xAB + dAB);
            }
            // Lower part: the B->C edge starts at Bx and is pre-stepped back by
            // one delta (k = (float)Bx; k -= dBC) before the loop (0x4271e8..).
            float xBC = static_cast<float>(Bx);     // k
            double dBC = static_cast<double>(Cx - Bx) / (Cr - Br);        // v73
            // Pre-step the B->C edge back by one delta (0x427202 k -= v73). The
            // original then USES k at the top of the body and steps it at the
            // BOTTOM (0x42725a k += v73), so iteration n uses Bx + (n-1)*dBC and
            // the first row sees Bx - dBC. (The earlier version stepped k at the
            // top, shifting every row forward by one dBC.)
            xBC = static_cast<float>(xBC - dBC);            // 0x427202
            for (; row <= Cr; ++row) {
                int cBC = Trunc(static_cast<double>(xBC));   // (int)trunc(k)    0x427228
                int cAC = Trunc(static_cast<double>(xAC));   // (int)trunc(v81)  0x427239
                int r = guild::render::ScanRowHeightRange(&rv, &lo, cBC, &hi, cAC, row);
                xAC = static_cast<float>(xAC + dAC);         // 0x42724a
                xBC = static_cast<float>(xBC + dBC);         // 0x42725a
                clipped &= r;
            }
        }
    }

    if (!clipped) {                                 // 0x427022
        // Convert the elevation byte range back to world-Y:
        //   *lowOut  = lo * heightScale + heightBase
        //   *highOut = hi * heightScale + heightBase
        *lowOut  = static_cast<float>(static_cast<double>(lo) * grid->heightScale +
                                      grid->heightBase);                    // 0x427040
        *highOut = static_cast<float>(static_cast<double>(hi) * grid->heightScale +
                                      grid->heightBase);                    // 0x42705e
    }
    return clipped;                                 // 0x427063
}

// ===========================================================================
// gilde.exe 0x427370 — VIBE_Terrain_ScanSegmentHeightRange.
// ===========================================================================
int ScanSegmentHeightRange(const TerrainGrid* grid, float* lowOut, const float* a3,
                           float* highOut, const float* a5, const float* a6,
                           const float* a7) {
    float lo0, hi0, lo1, hi1;
    int c0 = ScanLineHeightRange(grid, &lo0, a3, &hi0, a5, a6);   // v20 0x42737a
    int c1 = ScanLineHeightRange(grid, &lo1, a6, &hi1, a7, a3);   // v12 0x4273b9

    if (c0 && c1)                  // both clipped -> nothing under the quad
        return 1;                  // 0x4273c6
    if (c0 || c1) {                // exactly one clipped -> copy the other
        if (c0) {                  // 0x42742f : first triangle clipped, use second
            *lowOut  = lo1;        // 0x427450
            *highOut = hi1;        // 0x42745b
        } else {                   // second clipped, use first
            *lowOut  = lo0;        // 0x427434
            *highOut = hi0;        // 0x42743f
        }
        return 0;                  // 0x427441
    }
    // both hit -> merge.   *lowOut = min(lo0,lo1); *highOut = max(hi0,hi1).
    *lowOut  = (lo0 >= static_cast<double>(lo1)) ? lo1 : lo0;   // 0x4273dd
    *highOut = (hi0 <= static_cast<double>(hi1)) ? hi1 : hi0;   // 0x4273f8
    return 0;                                                    // 0x42740c
}

// ===========================================================================
// Inert default hooks (defined here so any src/ reference resolves).
// ===========================================================================
namespace {
void DefReparent(int, int, int) {}
void DefSetWorldTransXYZ(int, int, int, int) {}
void DefSetPosXYZ(int, int, int, int) {}
void DefSetPos(int, const int*) {}
void DefSetWorldTrans(int, const int*) {}
int  DefComputeHeightRange(int, float* out) {
    if (out) { out[0] = 0.0f; out[1] = 0.0f; }
    return 1;   // default: "fail" -> caller treats mesh as having no range
}
int  DefDrawBoundingBox(int, float, float*, float*, float*, float*) { return 1; }
int  DefTestAabbOverlap(const int*, int) { return 1; }
int  DefReadScratchField(int, int) { return 0; }
void DefApplyNodeY(int, float) {}

TerrainCollisionHooks MakeDefaults() {
    TerrainCollisionHooks h{};
    h.reparentWithTransform = DefReparent;
    h.setWorldTranslationXYZ = DefSetWorldTransXYZ;
    h.setPositionXYZ = DefSetPosXYZ;
    h.setPosition = DefSetPos;
    h.setWorldTranslation = DefSetWorldTrans;
    h.meshComputeHeightRange = DefComputeHeightRange;
    h.meshDrawBoundingBox = DefDrawBoundingBox;
    h.meshTestAabbOverlapRecursive = DefTestAabbOverlap;
    h.scratchNode = 0;
    h.terrain = nullptr;
    h.readScratchField = DefReadScratchField;
    h.applyNodeY = DefApplyNodeY;
    return h;
}
TerrainCollisionHooks g_hooks = MakeDefaults();
}  // namespace

TerrainCollisionHooks TerrainCollisionSetHooks(const TerrainCollisionHooks* hooks) {
    TerrainCollisionHooks prev = g_hooks;
    g_hooks = hooks ? *hooks : MakeDefaults();
    return prev;
}
const TerrainCollisionHooks& TerrainCollisionGetHooks() { return g_hooks; }

// Helpers replicating the original's nested 4-corner min/max picks. Each corner is
// a float[6] {x,y,z,...}; the scan reads the x component (offset 0). The original
// expanded these as a long if/else nest; we collapse to the equivalent min/max
// while keeping the exact >= / <= comparison sense (>= picks for min, <= for max).
namespace {
float MinX(const float* c0, const float* c1, const float* c2, const float* c3) {
    float v = (c0[0] >= static_cast<double>(c1[0])) ? c1[0] : c0[0];
    v = (v >= static_cast<double>(c2[0])) ? c2[0] : v;
    v = (v >= static_cast<double>(c3[0])) ? c3[0] : v;
    return v;
}
float MaxX(const float* c0, const float* c1, const float* c2, const float* c3) {
    float v = (c0[0] <= static_cast<double>(c1[0])) ? c1[0] : c0[0];
    v = (v <= static_cast<double>(c2[0])) ? c2[0] : v;
    v = (v <= static_cast<double>(c3[0])) ? c3[0] : v;
    return v;
}
}  // namespace

// ===========================================================================
// gilde.exe 0x427b60 — VIBE_Collision_ResolveMeshAgainstTerrain.
// ===========================================================================
int ResolveMeshAgainstTerrain(int node, int applyFlag, int useMaxEdge) {
    const TerrainCollisionHooks& h = g_hooks;

    // Detach from any parent (+0x1F8 == +504) so the probe runs in world space.
    int parent = h.readScratchField(node, 504);          // *(node+504)  0x427b75
    if (parent) {
        h.reparentWithTransform(node, 0, parent);         // 0x427ba2
        h.setWorldTranslationXYZ(node, 0,
                                 h.readScratchField(node, 136), 0);  // 0x427bb3
    }

    // No mesh body (+0x1CC == +460) -> nothing to resolve.
    if (!h.readScratchField(node, 460)) {                 // 0x427b81
        if (parent)
            h.reparentWithTransform(node, parent, parent);
        return 1;                                          // 0x427bbe
    }

    // Snapshot the scratch probe node's transform, then zero it.
    int sx = h.readScratchField(h.scratchNode, 76);       // v49
    int sy = h.readScratchField(h.scratchNode, 80);       // v50
    int sz = h.readScratchField(h.scratchNode, 84);       // v51
    int tx = h.readScratchField(h.scratchNode, 132);      // v52
    int ty = h.readScratchField(h.scratchNode, 136);      // v53
    int tz = h.readScratchField(h.scratchNode, 140);      // v54
    int snap[3]  = { sx, sy, sz };
    int trans[3] = { tx, ty, tz };
    h.setPositionXYZ(h.scratchNode, 0, 0, 0);             // 0x427c15
    h.setWorldTranslationXYZ(h.scratchNode, 0, 0, 0);     // 0x427c2d

    // Mesh vertical extent [meshLow, meshHigh].
    float range[2] = { 0.0f, 0.0f };                      // {v30, v31}
    if (h.meshComputeHeightRange(node, range)) {          // 0x427c34 -> nonzero = fail
        h.setPosition(h.scratchNode, snap);
        h.setWorldTranslation(h.scratchNode, trans);
        if (parent)
            h.reparentWithTransform(node, parent, parent);
        return 1;                                          // 0x427c5e
    }
    const float meshLow  = range[0];                       // v30
    const float meshHigh = range[1];                       // v31

    // Probe slab height: lerp between low/high by 0.1 (flt_6115B8).
    float probeH = (meshHigh - meshLow) * kProbeHeightLerp + meshLow;  // v32 0x427ca3

    // Four world-space AABB corners under the mesh at the probe height.
    float c0[6] = {}, c1[6] = {}, c2[6] = {}, c3[6] = {};
    if (h.meshDrawBoundingBox(node, probeH, c0, c1, c2, c3)) {  // 0x427cad -> fail
        h.setPosition(h.scratchNode, snap);
        h.setWorldTranslation(h.scratchNode, trans);
        if (parent)
            h.reparentWithTransform(node, parent, parent);
        return 1;                                          // 0x427cd7
    }

    // Restore the scratch transform now the corners are captured.
    h.setPosition(h.scratchNode, snap);                   // 0x427cff
    h.setWorldTranslation(h.scratchNode, trans);          // 0x427d10

    // Scan the terrain surface under the corner quad for its [surfLow, surfHigh].
    float surfLow, surfHigh;                               // v33, v34
    if (h.terrain) {                                       // dword_64A028 0x427d1d
        if (ScanSegmentHeightRange(h.terrain, &surfLow, c0, &surfHigh, c1, c2, c3)) {
            // Quad lies entirely off the terrain.
            if (parent)
                h.reparentWithTransform(node, parent, parent);
            return 1;                                       // 0x427d47
        }
    } else {
        if (applyFlag) {
            if (parent)
                h.reparentWithTransform(node, parent, parent);
            return 1;                                       // 0x427d6e/0x427d78
        }
        surfLow  = 1.0e35f;                                  // v33  0x427d9b
        surfHigh = -1.0e35f;                                 // v34  0x427d9f
    }

    if (applyFlag) {                                        // 0x427dab
        // Shift the node's Y (+0x50 == +80) so the chosen surface edge meets the
        // mesh's low edge, then set the +0x210 (==+528) dirty bit (|=4).
        float surf = useMaxEdge ? surfHigh : surfLow;       // v9
        float curY;
        int yRaw = h.readScratchField(node, 80);            // *(float*)(node+80)
        std::memcpy(&curY, &yRaw, sizeof(float));
        float newY = surf - meshLow + curY;                 // 0x427dbc
        h.applyNodeY(node, newY);                            // store + dirty-bit (|=4)
        if (parent)
            h.reparentWithTransform(node, parent, parent);
        return 0;                                           // 0x427dd3
    }

    // applyFlag == 0: build the merged corner AABB and run the recursive overlap
    // test against the node's sibling/child chain.
    (void)MinX; (void)MaxX;
    int clear = 1;                                          // v26
    // Walk down to the deepest +0x1F4 (==+500) child, then iterate +0x1F0 (==+496).
    int i = node;
    while (h.readScratchField(i, 500))
        i = h.readScratchField(i, 500);
    while (i) {
        if (i != node)
            clear &= h.meshTestAabbOverlapRecursive(snap, i);   // ctx=&v29 (node-anchored)
        i = h.readScratchField(i, 496);
    }
    h.setPosition(h.scratchNode, snap);                    // 0x42832f
    h.setWorldTranslation(h.scratchNode, trans);

    if (!clear || h.terrain) {                             // 0x428350
        float surf = useMaxEdge ? surfHigh : surfLow;       // v28
        float curY; int yRaw = h.readScratchField(node, 80);
        std::memcpy(&curY, &yRaw, sizeof(float));
        float newY = surf - meshLow + curY;                 // 0x428382
        h.applyNodeY(node, newY);                            // store + dirty-bit (|=4)
        if (parent)
            h.reparentWithTransform(node, parent, parent);
        return 0;                                           // 0x428399
    }
    if (parent)
        h.reparentWithTransform(node, parent, parent);
    return 1;                                              // 0x428363/0x428354
}

} // namespace guild::sim
