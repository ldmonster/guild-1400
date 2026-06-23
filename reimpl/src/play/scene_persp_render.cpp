// guild::play — perspective scene-mesh renderer. See header.
#include "play/scene_persp_render.h"

#include "render/d3_projection.h"
#include "render/geometry_types.h"
#include "render/types.h"
#include "render/surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace guild::play {
namespace {

struct V3 { float x, y, z; };
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : a;
}

// Write a packed pixel into the surface at (x,y) (16 or 32 bpp).
inline void PutPixel(render::Surface* s, int x, int y, u8 r, u8 g, u8 b) {
    if (s->bpp == 32) {
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        row[x] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
    } else {  // 16bpp 565
        auto* row = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

} // namespace

float FrameCameraToMesh(const render::MeshGeometry& mesh, PerspCamera& cam,
                        float azimuth, float elevation, float distFactor) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return 0.0f;
    V3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
    for (int i = 0; i < mesh.vertexCount; ++i) {
        const render::Vertex& v = mesh.vertices[i];
        mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
        mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
    }
    V3 c{(mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2};
    float rad = 0.0f;
    for (int i = 0; i < mesh.vertexCount; ++i) {
        const render::Vertex& v = mesh.vertices[i];
        V3 d = sub({v.x, v.y, v.z}, c);
        rad = std::max(rad, std::sqrt(dot(d, d)));
    }
    const float dist = rad * distFactor;
    cam.target[0] = c.x; cam.target[1] = c.y; cam.target[2] = c.z;
    cam.eye[0] = c.x + dist * std::cos(azimuth) * std::cos(elevation);
    cam.eye[1] = c.y + dist * std::sin(elevation);
    cam.eye[2] = c.z + dist * std::sin(azimuth) * std::cos(elevation);
    return rad;
}

PerspRenderStats RenderMeshPerspective(const render::MeshGeometry& mesh,
                                       const PerspCamera& cam, render::Surface* fb,
                                       const PerspRenderOptions& opt) {
    PerspRenderStats st{};
    if (!fb || !fb->pixels) return st;
    const int W = fb->widthPx ? fb->widthPx : fb->width;
    const int H = fb->height;
    if (W <= 0 || H <= 0) return st;

    if (opt.clearFirst) render::SurfaceColorFill(fb, opt.clearR, opt.clearG, opt.clearB);
    if (!mesh.vertices || !mesh.polygons || mesh.polyCount <= 0) return st;

    // Camera basis (right-handed look-at): forward toward the target.
    const V3 eye{cam.eye[0], cam.eye[1], cam.eye[2]};
    const V3 fwd = normalize(sub({cam.target[0], cam.target[1], cam.target[2]}, eye));
    const V3 right = normalize(cross({cam.up[0], cam.up[1], cam.up[2]}, fwd));
    const V3 up = cross(fwd, right);
    const float fproj = 1.0f / std::tan(cam.fovY * 0.5f);
    const float aspect = (float)W / (float)H;
    const V3 light = normalize({opt.lightDir[0], opt.lightDir[1], opt.lightDir[2]});
    // Engine-exact projection (rule 3) when requested; else the host pinhole.
    const render::D3Projection proj =
        render::MakeProjection((float)W, (float)H, cam.nearZ, cam.farZ);

    std::vector<float> zbuf((std::size_t)W * H, 1e30f);

    auto project = [&](const render::Vertex& vv, float& sx, float& sy, float& sz) {
        V3 p = sub({vv.x, vv.y, vv.z}, eye);
        V3 vc{dot(p, right), dot(p, up), dot(p, fwd)};
        if (vc.z < cam.nearZ) vc.z = cam.nearZ;
        if (cam.engineProjection) {
            render::ProjectedPoint pj = render::ProjectViewPoint(proj, vc.x, vc.y, vc.z);
            sx = pj.sx; sy = pj.sy; sz = vc.z;   // monotonic depth for the z-buffer
            return;
        }
        sx = (vc.x * fproj / aspect / vc.z * 0.5f + 0.5f) * W;
        sy = (0.5f - vc.y * fproj / vc.z * 0.5f) * H;
        sz = vc.z;
    };

    for (int pi = 0; pi < mesh.polyCount; ++pi) {
        const render::Polygon& poly = mesh.polygons[pi];
        const render::Vertex* vs[3] = {poly.v0, poly.v1, poly.v2};
        if (!vs[0] || !vs[1] || !vs[2]) continue;

        const V3 a{vs[0]->x, vs[0]->y, vs[0]->z};
        const V3 b{vs[1]->x, vs[1]->y, vs[1]->z};
        const V3 c{vs[2]->x, vs[2]->y, vs[2]->z};
        const V3 n = normalize(cross(sub(b, a), sub(c, a)));
        // Two-sided Lambert so both winding orders shade (scene art mixes them).
        float lambert = std::fabs(dot(n, light));
        float shade = opt.ambient + (1.0f - opt.ambient) * std::max(0.0f, lambert);

        float SX[3], SY[3], SZ[3];
        for (int i = 0; i < 3; ++i) project(*vs[i], SX[i], SY[i], SZ[i]);

        const float area = (SX[1] - SX[0]) * (SY[2] - SY[0]) -
                           (SX[2] - SX[0]) * (SY[1] - SY[0]);
        if (std::fabs(area) < 1e-4f) continue;

        int minx = std::max(0, (int)std::floor(std::min({SX[0], SX[1], SX[2]})));
        int maxx = std::min(W - 1, (int)std::ceil(std::max({SX[0], SX[1], SX[2]})));
        int miny = std::max(0, (int)std::floor(std::min({SY[0], SY[1], SY[2]})));
        int maxy = std::min(H - 1, (int)std::ceil(std::max({SY[0], SY[1], SY[2]})));

        int cr = std::min(255, (int)(shade * opt.baseR));
        int cg = std::min(255, (int)(shade * opt.baseG));
        int cb = std::min(255, (int)(shade * opt.baseB));

        bool wroteAny = false;
        for (int y = miny; y <= maxy; ++y) {
            for (int x = minx; x <= maxx; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((SX[1] - px) * (SY[2] - py) - (SX[2] - px) * (SY[1] - py)) / area;
                float w1 = ((SX[2] - px) * (SY[0] - py) - (SX[0] - px) * (SY[2] - py)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = w0 * SZ[0] + w1 * SZ[1] + w2 * SZ[2];
                const std::size_t idx = (std::size_t)y * W + x;
                if (z < zbuf[idx]) {
                    zbuf[idx] = z;
                    PutPixel(fb, x, y, (u8)cr, (u8)cg, (u8)cb);
                    ++st.pixelsWritten;
                    wroteAny = true;
                }
            }
        }
        if (wroteAny) ++st.trianglesDrawn;
    }
    return st;
}

} // namespace guild::play
