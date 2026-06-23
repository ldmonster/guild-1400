// guild::render — reference CPU rasteriser for a Scene3DDrawList. See header.
//
// This is the exact back half of play::RenderSceneObjects (projection + winding
// cull + depth-buffered barycentric raster with perspective-correct UV and
// screen-space Gouraud colour), lifted to operate on the backend-neutral draw
// list so the Vulkan pipeline can be validated against identical output.
#include "render/scene_drawlist.h"

#include "render/d3_projection.h"
#include "render/surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace guild::render {
namespace {

inline void ReadPixel(const Surface* s, int x, int y, int& r, int& g, int& b) {
    if (s->bpp == 32) {
        const std::uint32_t p = *(reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x);
        r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
    } else {
        const std::uint16_t p = *(reinterpret_cast<const std::uint16_t*>(
            static_cast<const std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x);
        r = ((p >> 11) & 0x1F) << 3; g = ((p >> 5) & 0x3F) << 2; b = (p & 0x1F) << 3;
    }
}

inline void PutPixel(Surface* s, int x, int y, int r, int g, int b) {
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (s->bpp == 32) {
        auto* px = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x;
        *px = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
    } else if (s->bpp == 16) {
        auto* px = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x;
        *px = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

} // namespace

void BuildSceneTextureMips(SceneDrawTexture& t) {
    if (!t.mips.empty() || t.w <= 1 || t.h <= 1) return;
    int pw = t.w, ph = t.h;
    const std::uint32_t* prev = t.argb.data();
    std::vector<std::uint32_t> prevBuf;   // owns the previous level after the first
    while (pw > 1 || ph > 1) {
        const int nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
        std::vector<std::uint32_t> lvl((std::size_t)nw * nh);
        for (int y = 0; y < nh; ++y) {
            const int sy0 = (ph > 1) ? y * 2 : 0, sy1 = (ph > 1) ? sy0 + 1 : 0;
            for (int x = 0; x < nw; ++x) {
                const int sx0 = (pw > 1) ? x * 2 : 0, sx1 = (pw > 1) ? sx0 + 1 : 0;
                const std::uint32_t a = prev[(std::size_t)sy0 * pw + sx0], b = prev[(std::size_t)sy0 * pw + sx1];
                const std::uint32_t c = prev[(std::size_t)sy1 * pw + sx0], d = prev[(std::size_t)sy1 * pw + sx1];
                auto avg = [&](int sh){ return (((a>>sh)&0xFF)+((b>>sh)&0xFF)+((c>>sh)&0xFF)+((d>>sh)&0xFF)+2)/4; };
                lvl[(std::size_t)y * nw + x] = 0xFF000000u | (avg(16)<<16) | (avg(8)<<8) | avg(0);
            }
        }
        t.mips.push_back(std::move(lvl));
        prevBuf = t.mips.back();          // copy: next level reads from this
        prev = prevBuf.data();
        pw = nw; ph = nh;
    }
}

namespace {
// Bilinear sample of texture level (w,h,px) at repeating uv -> r,g,b.
inline void SampleLevel(const std::uint32_t* px, int w, int h, float u, float v,
                        int& r, int& g, int& b) {
    auto tex = [&](long ix, long iy, int& rr, int& gg, int& bb) {
        ix %= w; if (ix < 0) ix += w; iy %= h; if (iy < 0) iy += h;
        const std::uint32_t t = px[(std::size_t)iy * w + ix];
        rr = (t >> 16) & 0xFF; gg = (t >> 8) & 0xFF; bb = t & 0xFF;
    };
    const float fu = u * w - 0.5f, fv = v * h - 0.5f;
    const long x0 = (long)std::floor(fu), y0 = (long)std::floor(fv);
    const float ax = fu - x0, ay = fv - y0;
    int r00,g00,b00,r10,g10,b10,r01,g01,b01,r11,g11,b11;
    tex(x0,y0,r00,g00,b00); tex(x0+1,y0,r10,g10,b10); tex(x0,y0+1,r01,g01,b01); tex(x0+1,y0+1,r11,g11,b11);
    const float w00=(1-ax)*(1-ay), w10=ax*(1-ay), w01=(1-ax)*ay, w11=ax*ay;
    r=(int)(r00*w00+r10*w10+r01*w01+r11*w11);
    g=(int)(g00*w00+g10*w10+g01*w01+g11*w11);
    b=(int)(b00*w00+b10*w10+b01*w01+b11*w11);
}
} // namespace

DrawListRasterStats RasterizeDrawList(const Scene3DDrawList& dl, Surface* fb) {
    DrawListRasterStats st{};
    if (!fb || !fb->pixels) return st;
    const int W = fb->widthPx ? fb->widthPx : fb->width;
    const int H = fb->height;
    if (W <= 0 || H <= 0) return st;
    if (dl.clearFirst) SurfaceColorFill(fb, dl.clearR, dl.clearG, dl.clearB);

    const SceneDrawCamera& c = dl.cam;
    // Rebuild the D3Projection from the captured terms (no recompute of clip window).
    D3Projection proj;
    proj.width = c.width; proj.height = c.height;
    proj.originX = c.originX; proj.originY = c.originY;
    proj.clipX = c.clipX; proj.clipWidth = c.clipWidth;
    proj.clipY = c.clipY; proj.clipHeight = c.clipHeight;
    proj.nearZ = c.nearZ; proj.q = c.q;

    std::vector<float> zbuf((std::size_t)W * H, 1e30f);

    for (const SceneDrawBatch& batch : dl.batches) {
        const SceneDrawTexture* tex =
            (batch.texId >= 0 && (std::size_t)batch.texId < dl.textures.size())
                ? &dl.textures[(std::size_t)batch.texId] : nullptr;
        const bool textured = tex && tex->w > 0 && tex->h > 0 && !tex->argb.empty();
        const bool isShadow = (batch.texId == kSceneShadowBatch);
        // gilde.exe 0x5e0358 VIBE_Render_SetBlendMode: transparent batches are depth-
        // tested but never write depth, and composite over the destination.
        const int  blendMode  = isShadow ? kBlendOpaque : batch.blend;
        const bool transparent = (blendMode == kBlendAdditive || blendMode == kBlendAlpha);
        const float blendOpacity = batch.opacity;

        // Rasterise one (already near-clipped) triangle.
        auto drawTri = [&](const SceneDrawVertex& V0, const SceneDrawVertex& V1, const SceneDrawVertex& V2) {
            const SceneDrawVertex* VV[3] = {&V0, &V1, &V2};

            float SX[3], SY[3], SZ[3];
            for (int i = 0; i < 3; ++i) {
                const float dx = VV[i]->pos[0] - c.eye[0];
                const float dy = VV[i]->pos[1] - c.eye[1];
                const float dz = VV[i]->pos[2] - c.eye[2];
                float vx = dx * c.right[0] + dy * c.right[1] + dz * c.right[2];
                float vy = dx * c.up[0]    + dy * c.up[1]    + dz * c.up[2];
                float vz = dx * c.fwd[0]   + dy * c.fwd[1]   + dz * c.fwd[2];
                if (vz < c.nearZ) vz = c.nearZ;   // safety; near-clip already removed behind verts
                if (c.engineProjection) {
                    ProjectedPoint pj = ProjectViewPoint(proj, vx, vy, vz);
                    SX[i] = pj.sx; SY[i] = pj.sy; SZ[i] = vz;
                } else {
                    SX[i] = (vx * c.fproj / c.aspect / vz * 0.5f + 0.5f) * W;
                    SY[i] = (0.5f - vy * c.fproj / vz * 0.5f) * H;
                    SZ[i] = vz;
                }
            }
            const float area = (SX[1] - SX[0]) * (SY[2] - SY[0]) - (SX[2] - SX[0]) * (SY[1] - SY[0]);
            if (std::fabs(area) < 1e-4f) return;
            if (!isShadow && ((dl.backfaceCull == 1 && area < 0.0f) ||
                              (dl.backfaceCull == 2 && area > 0.0f))) return;  // shadows two-sided

            int minx = std::max(0, (int)std::floor(std::min({SX[0], SX[1], SX[2]})));
            int maxx = std::min(W - 1, (int)std::ceil(std::max({SX[0], SX[1], SX[2]})));
            int miny = std::max(0, (int)std::floor(std::min({SY[0], SY[1], SY[2]})));
            int maxy = std::min(H - 1, (int)std::ceil(std::max({SY[0], SY[1], SY[2]})));

            // Perspective-correct UV setup (matches RenderSceneObjects).
            float uz[3] = {0, 0, 0}, vz_[3] = {0, 0, 0}, iz[3] = {0, 0, 0};
            // Mipmap LOD for this triangle: texels-per-pixel = (uv-area * w*h)/screen-area.
            // Constant per triangle (cheap), enough to anti-alias minified surfaces.
            const bool mipped = dl.bilinear && tex && !tex->mips.empty();
            float lod = 0.0f; int lodLo = 0, lodHi = 0; float lodFrac = 0.0f;
            if (textured) {
                for (int k = 0; k < 3; ++k) {
                    iz[k] = 1.0f / SZ[k];
                    uz[k] = VV[k]->uv[0] * iz[k];
                    vz_[k] = VV[k]->uv[1] * iz[k];
                }
                if (mipped) {
                    const float uvA = std::fabs((VV[1]->uv[0]-VV[0]->uv[0])*(VV[2]->uv[1]-VV[0]->uv[1])
                                              - (VV[2]->uv[0]-VV[0]->uv[0])*(VV[1]->uv[1]-VV[0]->uv[1]));
                    const float scrA = std::fabs(area);
                    const float tpp = (scrA > 1e-6f) ? (uvA * (float)tex->w * (float)tex->h / scrA) : 0.0f;
                    lod = (tpp > 1.0f) ? 0.5f * std::log2(tpp) : 0.0f;
                    const int maxL = (int)tex->mips.size();   // base=0, mips=1..maxL
                    if (lod < 0.0f) lod = 0.0f; if (lod > (float)maxL) lod = (float)maxL;
                    lodLo = (int)lod; lodHi = lodLo < maxL ? lodLo + 1 : lodLo; lodFrac = lod - lodLo;
                }
            }
            auto levelPx = [&](int L, int& lw, int& lh) -> const std::uint32_t* {
                if (L <= 0) { lw = tex->w; lh = tex->h; return tex->argb.data(); }
                lw = tex->w >> L; if (lw < 1) lw = 1; lh = tex->h >> L; if (lh < 1) lh = 1;
                return tex->mips[(std::size_t)L - 1].data();
            };
            bool wrote = false;
            for (int y = miny; y <= maxy; ++y) {
                for (int x = minx; x <= maxx; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((SX[1] - px) * (SY[2] - py) - (SX[2] - px) * (SY[1] - py)) / area;
                    float w1 = ((SX[2] - px) * (SY[0] - py) - (SX[0] - px) * (SY[2] - py)) / area;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float z = w0 * SZ[0] + w1 * SZ[1] + w2 * SZ[2];
                    const std::size_t idx = (std::size_t)y * W + x;
                    // Coplanar tie-break: an opaque fragment must be more than a small slack
                    // CLOSER to overwrite, so near-coplanar surfaces (e.g. furniture flush
                    // against a wall) keep the first-drawn winner instead of flickering as
                    // the camera sub-pixel-moves. Shadows (lifted onto their plane) use the
                    // plain test so they still draw over the receiver.
                    const float zlimit = (isShadow || transparent) ? zbuf[idx]
                                                                    : (zbuf[idx] - z * kCoplanarSlack);
                    if (z >= zlimit) continue;

                    // Screen-space Gouraud colour (0..1 modulator).
                    float cmR = w0 * V0.color[0] + w1 * V1.color[0] + w2 * V2.color[0];
                    float cmG = w0 * V0.color[1] + w1 * V1.color[1] + w2 * V2.color[1];
                    float cmB = w0 * V0.color[2] + w1 * V1.color[2] + w2 * V2.color[2];

                    int cr, cg, cb;
                    if (isShadow) {
                        // Darken the receiver: dest *= interpolated factor (the shadow
                        // alpha carried in the vertex colour). Single layer (writes z).
                        int dr, dg, db; ReadPixel(fb, x, y, dr, dg, db);
                        cr = (int)(dr * cmR); cg = (int)(dg * cmG); cb = (int)(db * cmB);
                    } else if (textured) {
                        const float izp = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
                        if (izp <= 1e-9f) continue;
                        const float u = (w0 * uz[0] + w1 * uz[1] + w2 * uz[2]) / izp;
                        const float v = (w0 * vz_[0] + w1 * vz_[1] + w2 * vz_[2]) / izp;
                        int tr, tg, tb;
                        if (mipped) {
                            // Trilinear: bilinear in two adjacent mip levels, lerp by LOD frac.
                            int lw,lh; const std::uint32_t* p0 = levelPx(lodLo, lw, lh);
                            int r0,g0,b0; SampleLevel(p0, lw, lh, u, v, r0, g0, b0);
                            if (lodFrac > 0.0f && lodHi != lodLo) {
                                int hw,hh; const std::uint32_t* p1 = levelPx(lodHi, hw, hh);
                                int r1,g1,b1; SampleLevel(p1, hw, hh, u, v, r1, g1, b1);
                                tr=(int)(r0+(r1-r0)*lodFrac); tg=(int)(g0+(g1-g0)*lodFrac); tb=(int)(b0+(b1-b0)*lodFrac);
                            } else { tr=r0; tg=g0; tb=b0; }
                        } else if (dl.bilinear) {
                            SampleLevel(tex->argb.data(), tex->w, tex->h, u, v, tr, tg, tb);
                        } else {
                            long iu = (long)std::floor(u * tex->w), iv = (long)std::floor(v * tex->h);
                            iu %= tex->w; if (iu < 0) iu += tex->w; iv %= tex->h; if (iv < 0) iv += tex->h;
                            const std::uint32_t t = tex->argb[(std::size_t)iv * tex->w + iu];
                            tr = (t >> 16) & 0xFF; tg = (t >> 8) & 0xFF; tb = t & 0xFF;
                        }
                        cr = (int)(cmR * tr); cg = (int)(cmG * tg); cb = (int)(cmB * tb);
                    } else {
                        cr = (int)(cmR * 255.0f); cg = (int)(cmG * 255.0f); cb = (int)(cmB * 255.0f);
                    }
                    // Blend the source (cr,cg,cb) over the destination. Additive adds the
                    // opacity-scaled source (the light-shaft / flame glow); alpha lerps.
                    if (transparent) {
                        int dr, dg, db; ReadPixel(fb, x, y, dr, dg, db);
                        if (blendMode == kBlendAdditive) {
                            cr = dr + (int)(cr * blendOpacity);
                            cg = dg + (int)(cg * blendOpacity);
                            cb = db + (int)(cb * blendOpacity);
                        } else {  // kBlendAlpha
                            cr = (int)(dr * (1.0f - blendOpacity) + cr * blendOpacity);
                            cg = (int)(dg * (1.0f - blendOpacity) + cg * blendOpacity);
                            cb = (int)(db * (1.0f - blendOpacity) + cb * blendOpacity);
                        }
                        if (cr > 255) cr = 255; if (cg > 255) cg = 255; if (cb > 255) cb = 255;
                    }
                    if (!transparent) zbuf[idx] = z;   // transparent: test only, no z-write
                    PutPixel(fb, x, y, cr, cg, cb);
                    ++st.pixelsWritten;
                    wrote = true;
                }
            }
            if (wrote) ++st.trianglesDrawn;
        };  // drawTri

        // View-space z (distance along the camera forward) of a vertex.
        auto viewZ = [&](const SceneDrawVertex& v) {
            return (v.pos[0] - c.eye[0]) * c.fwd[0] + (v.pos[1] - c.eye[1]) * c.fwd[1]
                 + (v.pos[2] - c.eye[2]) * c.fwd[2];
        };
        auto lerpV = [](const SceneDrawVertex& a, const SceneDrawVertex& b, float t) {
            SceneDrawVertex r;
            for (int k = 0; k < 3; ++k) r.pos[k] = a.pos[k] + (b.pos[k] - a.pos[k]) * t;
            for (int k = 0; k < 3; ++k) r.color[k] = a.color[k] + (b.color[k] - a.color[k]) * t;
            for (int k = 0; k < 2; ++k) r.uv[k] = a.uv[k] + (b.uv[k] - a.uv[k]) * t;
            return r;
        };

        const float NZ = c.nearZ;
        const int last = batch.firstVertex + batch.vertexCount;
        for (int base = batch.firstVertex; base + 2 < last; base += 3) {
            const SceneDrawVertex& A = dl.verts[(std::size_t)base + 0];
            const SceneDrawVertex& B = dl.verts[(std::size_t)base + 1];
            const SceneDrawVertex& C = dl.verts[(std::size_t)base + 2];
            const float za = viewZ(A), zb = viewZ(B), zc = viewZ(C);
            if (za >= NZ && zb >= NZ && zc >= NZ) { drawTri(A, B, C); continue; }
            if (za < NZ && zb < NZ && zc < NZ) continue;     // wholly behind the near plane
            // Near-plane clip (Sutherland-Hodgman) -> a 3- or 4-vertex polygon in front
            // of NZ; fan-triangulate. Prevents behind-camera verts from being clamped into
            // a huge near triangle that blacks out the screen as the camera flies.
            const SceneDrawVertex* vp[3] = {&A, &B, &C};
            const float vzs[3] = {za, zb, zc};
            SceneDrawVertex poly[4]; int np = 0;
            for (int i = 0; i < 3 && np < 4; ++i) {
                const SceneDrawVertex& cur = *vp[i]; const float zCur = vzs[i];
                const SceneDrawVertex& nxt = *vp[(i + 1) % 3]; const float zNxt = vzs[(i + 1) % 3];
                if (zCur >= NZ) poly[np++] = cur;
                if (np < 4 && (zCur >= NZ) != (zNxt >= NZ)) {
                    const float t = (NZ - zCur) / (zNxt - zCur);
                    poly[np++] = lerpV(cur, nxt, t);
                }
            }
            for (int i = 1; i + 1 < np; ++i) drawTri(poly[0], poly[i], poly[i + 1]);
        }
    }
    return st;
}

} // namespace guild::render
