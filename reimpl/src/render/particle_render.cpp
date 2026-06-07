#include "render/particle_render.h"

// =============================================================================
// guild::render weather render kernels — implementation. See particle_render.h.
// The D3D submission (dword_64A320 vtbl+112 DrawPrimitive) is omitted: these
// builders produce the exact TLVERTEX stream the original hands to the device.
// =============================================================================
namespace guild::render {

namespace {
// 1065353216 == bit pattern of 1.0f ; 0x3F000000 == 0.5f.
constexpr u32 kSnowDiffuse = 1348756580u; // 0x506E4FE4
inline float f1() { return 1.0f; }
} // namespace

// ---------------------------------------------------------------------------
// gilde.exe head of both render fns — fade window interpolation.
//   if (end > now): result = valA*(now-start)/(end-start)
//                          + (end-now)*valB/(end-start)
//   else:           result = valA  (clamped at the window start value)
// (the original's `result[3]` is valA, `result[2]` is valB.)
// ---------------------------------------------------------------------------
i32 InterpolateFade(i32 start, i32 end, i32 valA, i32 valB, i32 now) {
    if ((u32)end > (u32)now) {
        return valA * (now - start) / (end - start)
             + (i32)((u32)(end - now) * (u32)valB) / (end - start);
    }
    return valA;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x42b5b0 — VIBE_Snow_Render vertex build (3 verts / flake).
// ---------------------------------------------------------------------------
int BuildSnowVertices(const SnowSystem& sys, const SnowViewport& vp,
                      std::vector<Tlvertex>& out) {
    const SnowFlake* f = sys.flakes;
    int n = sys.count;
    if (!f || n <= 0)
        return 0;
    int emitted = 0;
    for (int i = 0; i < n; ++i) {
        const SnowFlake& s = f[i];
        // Clip: x0 <= sx, sx2 < x1, y0 <= sy, sy2 < y1.
        if (!((double)vp.x0 <= s.sx && (double)vp.x1 > s.sx2 &&
              (double)vp.y0 <= s.sy && (double)vp.y1 > s.sy2))
            continue;
        float z = (1.0f - s.pz) * kSnowZFade; // (1-pz)*0.025
        // Vertex 0: midpoint X, head Y; u=0.5, v=0.
        Tlvertex v0{};
        v0.x = (s.sx + s.sx2) * kSnowXMid;
        v0.y = s.sy;
        v0.z = z; v0.rhw = f1(); v0.diffuse = kSnowDiffuse; v0.specular = 0;
        v0.u = 0.5f; v0.v = 0.0f;
        // Vertex 1: tail point; u=1, v=1.
        Tlvertex v1{};
        v1.x = s.sx2; v1.y = s.sy2;
        v1.z = z; v1.rhw = f1(); v1.diffuse = kSnowDiffuse; v1.specular = 0;
        v1.u = 1.0f; v1.v = 1.0f;
        // Vertex 2: head X, tail Y; u=0, v=1.
        Tlvertex v2{};
        v2.x = s.sx; v2.y = s.sy2;
        v2.z = z; v2.rhw = f1(); v2.diffuse = kSnowDiffuse; v2.specular = 0;
        v2.u = 0.0f; v2.v = 1.0f;
        out.push_back(v0);
        out.push_back(v1);
        out.push_back(v2);
        emitted += 3;
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x429c38 — VIBE_Rain_Render vertex build (2 verts / drop).
// The original clips each drop's head (sx,sy) and tail (sx2,sy2) against the
// viewport (all four must be inside) and emits a 2-vertex line. `diffuse` is the
// streak-coloured word v40 the caller packs from the projected streak length.
// ---------------------------------------------------------------------------
int BuildRainVertices(const RainSystem& sys, const SnowViewport& vp,
                      std::vector<Tlvertex>& out, u32 diffuse) {
    const RainDrop* d = sys.drops;
    int n = sys.count;
    if (!d || n <= 0)
        return 0;
    int emitted = 0;
    for (int i = 0; i < n; ++i) {
        const RainDrop& s = d[i];
        float fx0 = (float)vp.x0, fx1 = (float)vp.x1;
        float fy0 = (float)vp.y0, fy1 = (float)vp.y1;
        // Clip: head X and tail X both in [x0,x1); head Y and tail Y in [y0,y1).
        if (!((double)s.sx >= fx0 && (double)s.sx < fx1 &&
              (double)s.sx2 >= fx0 && (double)s.sx2 < fx1))
            continue;
        if (!((double)s.sy >= fy0 && (double)s.sy < fy1 &&
              (double)s.sy2 >= fy0 && (double)s.sy2 < fy1))
            continue;
        float z = (1.0f - s.pz) * kRainZFade; // (1-pz)*0.025
        Tlvertex h{};
        h.x = s.sx; h.y = s.sy; h.z = z; h.rhw = f1();
        h.diffuse = diffuse; h.specular = 0; h.u = 0.0f; h.v = 0.0f;
        Tlvertex t{};
        t.x = s.sx2; t.y = s.sy2; t.z = z; t.rhw = f1();
        t.diffuse = diffuse; t.specular = 0; t.u = 0.0f; t.v = 0.0f;
        out.push_back(h);
        out.push_back(t);
        emitted += 2;
    }
    return emitted;
}

} // namespace guild::render
