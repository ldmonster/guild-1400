#include "render/render_leaves4.h"

#include "render/mesh_scene.h"   // ComputeAabbExtents (0x42756c, reused)
#include "render/colorformat.h"  // PackColor / UnpackColor (0x434f30 / 0x434f7c, reused)
#include "crt/rand.h"            // crt::RandNext (0x5cb8bc, reused)

#include <cmath>
#include <cstdint>
#include <cstring>

namespace guild::render {

// ===========================================================================
// Recovered module globals (defined once here — owning module is render).
// ===========================================================================
u32 g_flickerTick      = 0;   // dword_62EB38
u8  g_rawLightingFlag  = 0;   // byte_649D70

// ===========================================================================
// Byte-offset accessors. The original IDB had no named UDTs; every record is
// touched as *(type*)(base + off). We mirror that exactly with small helpers so
// the arithmetic stays visibly 1:1 with the disassembly.
// ===========================================================================
namespace {

inline u8*        BP(void* p)               { return static_cast<u8*>(p); }
inline const u8*  BP(const void* p)         { return static_cast<const u8*>(p); }

template <class T> inline T  Rd(const void* p, int off) { T v; std::memcpy(&v, BP(p) + off, sizeof(T)); return v; }
template <class T> inline void Wr(void* p, int off, T v) { std::memcpy(BP(p) + off, &v, sizeof(T)); }

inline void*  RdP(const void* p, int off) { return Rd<void*>(p, off); }
inline float  RdF(const void* p, int off) { return Rd<float>(p, off); }
inline i32    RdI(const void* p, int off) { return Rd<i32>(p, off); }
inline u32    RdU(const void* p, int off) { return Rd<u32>(p, off); }

// ===========================================================================
// Cross-module hooks (inert defaults).
// ===========================================================================
void DefaultAssignMeshData(void*) {}
void DefaultPropagateDirtyFlag(void*, u32) {}

RenderLeaves4Hooks g_hooks = {
    &DefaultAssignMeshData,
    &DefaultPropagateDirtyFlag,
};

} // namespace

void InstallRenderLeaves4Hooks(const RenderLeaves4Hooks& h) { g_hooks = h; }
const RenderLeaves4Hooks& CurrentRenderLeaves4Hooks() { return g_hooks; }

// ===========================================================================
// 0x5c6b30 — VIBE_Light_RefreshChildBrightness  (offsets per raw disassembly)
//   node = [root+0x1E8][+0x198]                      ; list head
//   for (; node; node = [node+8]) {
//     obj  = [node+4];          if (!obj) continue;
//     mesh = [obj+0x1EC];       if (!mesh) continue;
//     if ([mesh+0x90D] & 0x40) continue;             ; hidden
//     sub  = [obj+0x1CC];       if (!sub) continue;
//     if ((i32)[sub+8] < (i32)[node]) continue;      ; count guard
//     count = [node];  pair = node + 0xC;
//     for (i = 0; i < count; ++i, pair += 8) {
//       child = [pair];
//       if (raw) child[+0x40] = child[+0x44];        ; dword copy
//       else     child[+0x42] = (i8)child[+0x46] >> 2; ; byte arith-shift
//     }
//   }
// ===========================================================================
void* RefreshChildBrightness(void* root) {
    void* result = nullptr;
    LightNode* node = static_cast<LightNode*>(RdP(RdP(root, 0x1E8), 0x198));
    while (node) {
        result = node;
        void* obj = node->obj;
        if (!obj) { node = node->next; continue; }
        void* mesh = RdP(obj, 0x1EC);
        if (!mesh) { node = node->next; continue; }
        if (Rd<u8>(mesh, 0x90D) & 0x40) { node = node->next; continue; }
        void* sub = RdP(obj, 0x1CC);
        if (!sub) { node = node->next; continue; }
        i32 count = node->count;
        if (RdI(sub, 8) < count) { node = node->next; continue; }
        const LightChildPair* pair = node->pairs;
        if (g_rawLightingFlag) {
            for (i32 i = 0; i < count; ++i) {
                void* child = pair[i].vbase;
                Wr<u32>(child, 0x40, RdU(child, 0x44));
            }
        } else {
            for (i32 i = 0; i < count; ++i) {
                void* child = pair[i].vbase;
                // movzx ebx,[child+0x46]; sar ebx,2 — zero-extend the byte then
                // shift; since the value is 0..255 the shift is logical in effect.
                u8 v = static_cast<u8>(static_cast<i32>(Rd<u8>(child, 0x46)) >> 2);
                Wr<u8>(child, 0x42, v);
            }
        }
        node = node->next;
    }
    return result;
}

// ===========================================================================
// 0x5c6be0 — VIBE_Light_UpdateFlickerIntensity
// ---------------------------------------------------------------------------
// Envelope (deterministic, golden-tested in isolation via FlickerEnvelope):
//   cfg    = [light+0x1E8]
//   elapsed= g_flickerTick - [cfg+0x194]
//   period = [cfg+0x1A0]
//   if (elapsed >= period) {                         ; period elapsed -> reseed
//     // peak = max(light[+0x60], max over the 7 sub-records at cfg+0x24 stride 56,
//     //            field +0x24) — clamps the normalisation peak
//     peak = light->f[+0x60];
//     for (rec = cfg; rec != cfg+0x188; rec += 56) peak = max(peak, rec->f[+0x24]);
//     amp = [cfg+0x19C] * light->f[+0x60] / peak;
//     if ([cfg+0x194]) next = light->f[+0x58];        ; keep previous next
//     else next = (double)(int)RandNext()*kRandScale*(amp*kFlickerTwo) - amp + 1;
//     light->f[+0x68] = next; (a1+104)
//     prev = (double)(int)RandNext()*kRandScale*(amp*kFlickerTwo) - amp + 1;
//     light->f[+0x58] = prev; (a1+88)
//     [cfg+0x194] = g_flickerTick;  elapsed = 0;
//   }
//   frac = (double)elapsed / period;
//   intensity = frac*light->f[+0x58] + (1-frac)*light->f[+0x68];   ; v27
// The inner colour-OR loop over the child meshes uses the same node walk as
// RefreshChildBrightness; it scales each affected vertex colour by `intensity`
// (raw path) or by intensity*kFlickerQuarter (non-raw path) and clamps to a
// 6-bit (0..63) channel. Reproduced over byte offsets.
// ===========================================================================

// Exposed for unit testing: the deterministic flicker envelope. `prevOut`/
// `nextOut` receive the (re)seeded endpoints; returns the blended intensity.
// Mirrors the FPU arithmetic of the original exactly (double-precision blend).
float FlickerEnvelopeStep(u32 elapsed, u32 period, bool reseedSuppressed,
                          float prevTarget, float nextTarget, float amp,
                          float* prevOut, float* nextOut) {
    float prev = prevTarget;
    float next = nextTarget;
    if (elapsed >= period) {
        if (reseedSuppressed) {
            next = prevTarget;  // *(a1+104) = *(a1+88)
        } else {
            next = static_cast<float>(static_cast<double>(crt::RandNext()) * kRandScale
                       * (amp * kFlickerTwo) - amp + 1.0);
        }
        prev = static_cast<float>(static_cast<double>(crt::RandNext()) * kRandScale
                   * (amp * kFlickerTwo) - amp + 1.0);
        elapsed = 0;
    }
    if (prevOut) *prevOut = prev;
    if (nextOut) *nextOut = next;
    double frac = static_cast<double>(elapsed) / static_cast<double>(period);
    return static_cast<float>(frac * prev + (1.0 - frac) * next);
}

u8 UpdateFlickerIntensity(void* light) {
    u32 result = 0;
    void* cfg = RdP(light, 0x1E8);
    u32 tick0 = g_flickerTick;
    u32 period = RdU(cfg, 0x1A0);
    u32 elapsed = tick0 - RdU(cfg, 0x194);

    if (elapsed >= period) {
        // Normalisation peak: max(light[+0x60], the 7 sub-records' [+0x24]).
        float peak = RdF(light, 0x60);
        const u8* rec = BP(cfg);
        const u8* end = BP(cfg) + 0x188;  // 392
        do {
            float c = RdF(rec, 0x24);
            if (peak <= c) peak = c;
            rec += 56;
        } while (rec != end);

        float amp = RdF(cfg, 0x19C) * RdF(light, 0x60) / peak;  // v31
        float next;
        if (RdU(cfg, 0x194)) {
            next = RdF(light, 0x58);  // keep previous "next" endpoint
        } else {
            next = static_cast<float>(static_cast<double>(crt::RandNext()) * kRandScale
                       * (amp * kFlickerTwo) - amp + 1.0);
        }
        Wr<float>(light, 0x68, next);
        float prev = static_cast<float>(static_cast<double>(crt::RandNext()) * kRandScale
                         * (amp * kFlickerTwo) - amp + 1.0);
        Wr<float>(light, 0x58, prev);
        Wr<u32>(cfg, 0x194, tick0);
        elapsed = 0;
    }

    double frac = static_cast<double>(elapsed) / static_cast<double>(period);
    float intensity = static_cast<float>(
        frac * RdF(light, 0x58) + (1.0 - frac) * RdF(light, 0x68));  // v27

    LightNode* node = static_cast<LightNode*>(RdP(RdP(light, 0x1E8), 0x198));
    if (!node) return static_cast<u8>(result);

    while (node) {
        void* obj = node->obj;
        bool handled = false;
        if (obj) {
            void* mesh = RdP(obj, 0x1EC);
            if (mesh && (Rd<u8>(mesh, 0x90D) & 0x40) == 0) {
                void* sub = RdP(obj, 0x1CC);
                if (sub && RdI(sub, 8) >= node->count) {
                    i32 count = node->count;
                    const LightChildPair* pair = node->pairs;
                    if (g_rawLightingFlag) {
                        for (i32 i = 0; i < count; ++i) {
                            void* vbase = pair[i].vbase; // v14
                            u32  c29   = pair[i].color;  // v29 (packed colour DWORD)
                            // skip the "untouched" sentinel: low 24 bits of the
                            // vertex's [+64] colour word equal 0xFFFFFF.
                            if ((RdU(vbase, 64) & 0xFFFFFFu) == 0xFFFFFFu) continue;
                            u8 cr = static_cast<u8>(c29);          // (u8)v29
                            u8 cg = static_cast<u8>(c29 >> 8);     // BYTE1(v29)
                            u8 cb = static_cast<u8>(c29 >> 16);    // BYTE2(v29)
                            // base colour bytes at vbase[+64..+66] (b,g,r order).
                            float v30 = static_cast<float>(cb) * intensity + static_cast<float>(Rd<u8>(vbase, 66));
                            float v34 = static_cast<float>(cg) * intensity + static_cast<float>(Rd<u8>(vbase, 65));
                            float v28 = static_cast<float>(Rd<u8>(vbase, 64)) + intensity * static_cast<float>(cr);
                            // v17 = max(v30, v34, v28) (matching the chained mins)
                            float v16 = (v30 <= v34) ? v34 : v30;
                            float v17 = (v16 <= v28) ? v28 : ((v30 <= v34) ? v34 : v30);
                            std::uint32_t mb;
                            std::memcpy(&mb, &v17, 4);
                            if (static_cast<std::int32_t>(mb) > 1132396544 /* 255.0f */) {
                                float s = kFlickerByteMax / v17;
                                Wr<u8>(vbase, 66, static_cast<u8>(static_cast<i32>(v30 * s)));
                                Wr<u8>(vbase, 65, static_cast<u8>(static_cast<i32>(v34 * s)));
                                Wr<u8>(vbase, 64, static_cast<u8>(static_cast<i32>(v28 * s)));
                            } else {
                                Wr<u8>(vbase, 66, static_cast<u8>(static_cast<i32>(v30)));
                                Wr<u8>(vbase, 65, static_cast<u8>(static_cast<i32>(v34)));
                                Wr<u8>(vbase, 64, static_cast<u8>(static_cast<i32>(v28)));
                            }
                        }
                        handled = true;
                    } else {
                        // Non-raw path: scale the blue component (BYTE2) by
                        // intensity*0.25 and clamp to a 6-bit (0..63) channel.
                        float blend = intensity * static_cast<float>(kFlickerQuarter);
                        for (i32 i = 0; i < count; ++i) {
                            void* vbase = pair[i].vbase;
                            u32  c29   = pair[i].color;
                            u8   cb    = static_cast<u8>(c29 >> 16);  // BYTE2(v12[1])
                            if (Rd<u8>(vbase, 66) == 63) continue;
                            i32 v = static_cast<i32>(static_cast<float>(cb) * blend);
                            if (v >= 0x3F) v = 0x3F;
                            Wr<u8>(vbase, 66, static_cast<u8>(v));
                        }
                        handled = true;
                    }
                }
            }
        }
        (void)handled;
        result = static_cast<u32>(reinterpret_cast<std::uintptr_t>(node));
        node = node->next;
    }
    return static_cast<u8>(result);
}

// ===========================================================================
// 0x4283ac — VIBE_Mesh_AccumulateAabbRecursive
// ---------------------------------------------------------------------------
//   if (!obj[+0x1CC]) return;                         ; (a2[115])
//   AssignMeshData(obj);  PropagateDirtyFlag(obj, 1);
//   v = (float*)( [ [obj+0x1CC] ] + 80 * [ [obj+0x1CC] + 8 ] );  ; corner array
//   for (i = 0; i < 8; ++i, v += 20) {
//     box[0] = min(box[0], v[0]); box[1]=min(box[1],v[1]); box[2]=min(box[2],v[2]);
//     box[4] = max(box[4], v[0]); box[5]=max(box[5],v[1]); box[6]=max(box[6],v[2]);
//   }
//   for (c = obj[+0x1FC]; c; c = c[+0x1F0]) AccumulateAabbRecursive(box, c);
// box[3] is padding (never touched). The compares use the float<-double widen of
// the original; equality ties keep `box` per the >=/<= sense (faithful).
// ===========================================================================
void AccumulateAabbRecursive(float* box, void* obj) {
    const MeshGeom* mesh = static_cast<const MeshGeom*>(RdP(obj, 0x1CC));
    if (!mesh) return;
    g_hooks.assignMeshData(obj);
    g_hooks.propagateDirtyFlag(obj, 1u);
    const u8* v = BP(mesh->corners) + 80 * mesh->startIndex;
    for (int i = 0; i < 8; ++i, v += 80) {
        float x = RdF(v, 0), y = RdF(v, 4), z = RdF(v, 8);
        if (box[0] >= x) box[0] = x;
        if (box[1] >= y) box[1] = y;
        if (box[2] >= z) box[2] = z;
        if (box[4] <= x) box[4] = x;
        if (box[5] <= y) box[5] = y;
        if (box[6] <= z) box[6] = z;
    }
    for (void* c = RdP(obj, 0x1FC); c; c = RdP(c, 0x1F0)) {
        AccumulateAabbRecursive(box, c);
    }
}

// ===========================================================================
// 0x427820 — VIBE_Mesh_TestAabbOverlapRecursive
// ---------------------------------------------------------------------------
//   v3 = 1;
//   if (obj[+0x215 /*533*/] == 4) {                   ; class byte
//     if (!obj[+0x1CC]) return 1;
//     AssignMeshData(obj); PropagateDirtyFlag(obj,1);
//     v = corner array (as above);  fold 8 corners into local min(34/35/36)/max(33/31/32)
//     if (probe[+104] > minX && probe[+88] < maxX && probe[+108] > minY
//          && probe[+112] > minZ && probe[+96] < maxZ) {
//        tri = [obj[+0x1CC]+4];                        ; triangle vertex-ptr array
//        for (k = 0; k < [obj[+0x1CC]+12]; ++k, tri += 40 /*10 dwords*/)
//          if (ComputeAabbExtents(tri, &probe[+88], &probe[+104]))
//             accumulate probe[+16]=min(..), probe[+20]=max(..) over the 3 verts' Y.
//     }
//     v3 = 0;
//   }
//   for (c = obj[+0x1FC]; c; c = c[+0x1F0]) v3 &= TestAabbOverlapRecursive(probe, c);
//   return v3;
// probe interval box: [+88]=minX,[+92]=minY?,[+96]=minZ; [+104]=maxX,[+108]=maxY,
// [+112]=maxZ — matched to the original's compares. The Y-span accumulation into
// [+16]/[+20] takes vertex[1] (the Y component) of each of the 3 triangle verts.
// ===========================================================================
int TestAabbOverlapRecursive(void* probe, void* obj) {
    int v3 = 1;
    if (Rd<u8>(obj, 533) == 4) {
        const MeshGeom* mesh = static_cast<const MeshGeom*>(RdP(obj, 0x1CC));
        if (!mesh) return 1;
        g_hooks.assignMeshData(obj);
        g_hooks.propagateDirtyFlag(obj, 1u);
        const u8* v = BP(mesh->corners) + 80 * mesh->startIndex;
        float minX = RdF(v, 0), minY = RdF(v, 4), minZ = RdF(v, 8);
        float maxX = minX, maxY = minY, maxZ = minZ;
        const u8* p = v + 80;
        for (int i = 1; i < 8; ++i, p += 80) {
            float x = RdF(p, 0), y = RdF(p, 4), z = RdF(p, 8);
            if (minX >= x) minX = x;
            if (minY >= y) minY = y;
            if (minZ >= z) minZ = z;
            if (maxX <= x) maxX = x;
            if (maxY <= y) maxY = y;
            if (maxZ <= z) maxZ = z;
        }
        if (RdF(probe, 104) > minX && RdF(probe, 88) < maxX &&
            RdF(probe, 108) > minY && RdF(probe, 112) > minZ &&
            RdF(probe, 96) < maxZ) {
            float* lo = reinterpret_cast<float*>(BP(probe) + 88);
            float* hi = reinterpret_cast<float*>(BP(probe) + 104);
            int triCount = mesh->triCount;
            const MeshTriangle* triArr = static_cast<const MeshTriangle*>(mesh->triangles);
            for (int k = 0; k < triCount; ++k) {
                float* tri[3] = { triArr[k].v[0], triArr[k].v[1], triArr[k].v[2] };
                if (ComputeAabbExtents(tri, lo, hi)) {
                    // accumulate the probe's Y-span over the 3 vertices' [1] field
                    for (int t = 0; t < 3; ++t) {
                        float y = tri[t][1];
                        if (RdF(probe, 16) >= y) Wr<float>(probe, 16, y);
                        if (RdF(probe, 20) <= y) Wr<float>(probe, 20, y);
                    }
                }
            }
        }
        v3 = 0;
    }
    for (void* c = RdP(obj, 508); c; c = RdP(c, 496)) {
        v3 &= TestAabbOverlapRecursive(probe, c);
    }
    return v3;
}

// ===========================================================================
// 0x423d74 — VIBE_Surface_GetPixelRgb
// ---------------------------------------------------------------------------
//   bpp = (u8)surf[+20]; shift = bpp >> 3;
//   pitch = surf[+16]; pix = surf[+28];
//   switch by bpp:
//     15/16 -> UnpackColor( ((u16*)pix)[x + pitch*y] )           -> (r,g,b)
//     24    -> b = pix[shift*x + shift*pitch*y + 0]
//              g = pix[... + 1];  r = pix[... + 2]      (shift==3)
//     32    -> b = pix[4*(x + pitch*y) + 0]; g = +4; r = +8
//   out[0]=b, out[1]=g, out[2]=r  (the v12/v10/v11 store order).
// The 15/16bpp branch routes the unpacked bytes the SAME way (v12=r? no — the
// original assigns v12=b, v10=g, v11=r from UnpackColor's (v12,v11,v10) args).
// ===========================================================================
u8 GetPixelRgb(const ColorFormat& fmt, int x, int y, u8 out[3], const void* surf) {
    u8 bpp = Rd<u8>(surf, 20);
    int shift = static_cast<int>(bpp) >> 3;          // (int)v6 >> 3
    i32 pitch = RdI(surf, 16);
    const u8* pix = static_cast<const u8*>(RdP(surf, 28));

    u8 b = 0, g = 0, r = 0;
    if (bpp == 15 || bpp == 16) {
        u16 px = Rd<u16>(pix, 2 * (x + pitch * y));
        // Original: VIBE_Render_UnpackColor(px, &v12, &v11, &v10) where the
        // params are (pixel, rOut, gOut, bOut), so v12=r, v11=g, v10=b. The
        // stores are out[0]=v12(r), out[1]=v10(b), out[2]=v11(g).
        UnpackColor(fmt, px, r, g, b);
        out[0] = r; out[1] = b; out[2] = g;
        return out[2];
    } else if (bpp == 24) {
        int base = shift * x + shift * pitch * y;     // v9 + v7*pitch*y
        b = Rd<u8>(pix, base + 0);
        g = Rd<u8>(pix, base + 1);
        r = Rd<u8>(pix, base + 2);
    } else if (bpp == 32) {
        int base = 4 * (x + pitch * y);
        b = Rd<u8>(pix, base + 0);
        g = Rd<u8>(pix, base + 4);
        r = Rd<u8>(pix, base + 8);
    }
    out[0] = b; out[1] = g; out[2] = r;               // v12, v10, v11
    return out[2];
}

// ===========================================================================
// 0x423e5c — VIBE_Surface_SetPixelRgb
// ---------------------------------------------------------------------------
//   reject if !surf || x<surf[+36] || x>=surf[+44] || y<surf[+40] || y>=surf[+48].
//   shift = (u8)surf[+20] >> 3;  packed = PackColor(r,g,b);  bpp = surf[+20];
//   switch by bpp:
//     <8   -> return 1 (no write)
//     8    -> pix[pitch*y + x] = (r+g+b)/3
//     15/16-> ((u16*)pix)[pitch*y + x] = packed
//     24   -> pix[shift*x + pitch*y]=g; [+1]=r; [+2]=b   (a4,a3,a5 == g,r,b)
//     32   -> ((u32*)pix)[x + pitch*y] = packed
// Original arg mapping: cl=r(a3), bl=g(a4), a5=b. PackColor is called as
// VIBE_Result_Handler_Final(a4,a3,a5) == PackColor(g,r,b)?  The reconstructed
// PackColor(fmt,r,g,b) expects (r,g,b); the original packs with (al=r,dl=g,bl=b)
// so we call PackColor(fmt, r, g, b).
// ===========================================================================
int SetPixelRgb(const ColorFormat& fmt, int x, int y, u8 r, u8 g, u8 b, void* surf) {
    if (!surf || x < RdI(surf, 36) || x >= RdI(surf, 44) ||
        y < RdI(surf, 40) || y >= RdI(surf, 48)) {
        return 0;
    }
    int shift = static_cast<int>(Rd<u8>(surf, 20)) >> 3;
    // Original packs VIBE_Result_Handler_Final(a4, a3, a5) == PackColor(al=a4, dl=a3,
    // bl=a5). With a3==r, a4==g, a5==b this is PackColor(fmt, g, r, b): the r and g
    // PACK arguments are swapped relative to the byte-write order (which uses g,r,b).
    u16 packed = static_cast<u16>(PackColor(fmt, g, r, b));
    i32 pitch = RdI(surf, 16);
    u8* pix = static_cast<u8*>(RdP(surf, 28));
    u8 bpp = Rd<u8>(surf, 20);

    if (bpp < 8) {
        return 1;
    } else if (bpp == 8) {
        Wr<u8>(pix, pitch * y + x, static_cast<u8>((b + g + r) / 3));
        return 1;
    } else if (bpp == 15 || bpp == 16) {
        Wr<u16>(pix, 2 * (pitch * y + x), packed);
        return 1;
    } else if (bpp == 24) {
        int base = shift * x + y * pitch;             // v12 + a2*pitch
        Wr<u8>(pix, base + 0, g);                     // a4
        Wr<u8>(pix, base + 1, r);                     // a3
        Wr<u8>(pix, base + 2, b);                     // a5
        return 1;
    } else if (bpp == 32) {
        Wr<u32>(pix, 4 * (x + pitch * y), packed);
        return 1;
    }
    return 1;
}

// ===========================================================================
// 0x422ee4 — VIBE_Surface_BlitRgbToPixels
// ---------------------------------------------------------------------------
//   for (row = 0; row < rows; ++row) {
//     src = a3 + 3*cols*row;
//     for (col = 0; col < cols; ++col, src += 3) {
//       px = PackColor(src[0], src[1], src[2]);          // (r,g,b) bytes
//       ((u16*)surf[+28])[col + surf[+16]*row] = px;
//     }
//   }
// The original reads src[0],src[1],src[2] and packs via VIBE_Result_Handler_Final
// (*v7, v7[1], v7[2]) == PackColor(r,g,b); destination is 16bpp.
// ===========================================================================
void BlitRgbToPixels(const ColorFormat& fmt, u32 rows, u32 cols,
                     const u8* src, void* surf) {
    if (rows == 0) return;
    i32 pitch = RdI(surf, 16);
    u8* dst = static_cast<u8*>(RdP(surf, 28));
    for (u32 row = 0; row < rows; ++row) {
        const u8* sp = src + 3u * cols * row;
        for (u32 col = 0; col < cols; ++col, sp += 3) {
            u16 px = static_cast<u16>(PackColor(fmt, sp[0], sp[1], sp[2]));
            Wr<u16>(dst, 2 * (col + static_cast<u32>(pitch) * row), px);
        }
    }
}

} // namespace guild::render
