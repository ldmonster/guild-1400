#include "render/render_leaves2.h"

#include "render/mesh_transform.h"  // SetVertexColors (0x428928)
#include "util/math_rng_float.h"    // RandomFloatScaled (0x58b910)

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace guild::render {

// ===========================================================================
// Cross-module hooks (inert defaults) + recovered globals.
// ===========================================================================
namespace {

bool  DefaultBeginFrameLock(void*) { return true; }   // assume lockable
void  DefaultRefreshAllObjects(u32) {}                // no-op
void* DefaultCloneRecord(void* rec, i8, i8) { return rec; }
void  DefaultBlit(void*, void*, const i32*, const i32*) {}
i8    DefaultWalkAndInvoke(void*, void*, void*, int, const u8*) { return 1; }

RenderLeaves2Hooks g_hooks = {
    &DefaultBeginFrameLock,
    &DefaultRefreshAllObjects,
    &DefaultCloneRecord,
    &DefaultBlit,
    &DefaultWalkAndInvoke,
};

GlobalLightDir g_globalLight = {0.0f, 0.0f, 0.0f};

// Recovered FP constants (gilde.exe, decoded from raw bytes):
//   dbl_61DD38 = 0.6   dbl_61DD40 = -0.3   dbl_61DD48 = 0.3   (SetSunHeight)
constexpr double kSunRandSpan = 0.6;   // dbl_61DD38
constexpr double kSunBaseUp   = -0.3;  // dbl_61DD40
constexpr double kSunBaseDown = 0.3;   // dbl_61DD48

} // namespace

void InstallRenderLeaves2Hooks(const RenderLeaves2Hooks& hooks) {
    g_hooks.beginFrameLock    = hooks.beginFrameLock    ? hooks.beginFrameLock    : &DefaultBeginFrameLock;
    g_hooks.refreshAllObjects = hooks.refreshAllObjects ? hooks.refreshAllObjects : &DefaultRefreshAllObjects;
    g_hooks.cloneRecord       = hooks.cloneRecord       ? hooks.cloneRecord       : &DefaultCloneRecord;
    g_hooks.blit              = hooks.blit              ? hooks.blit              : &DefaultBlit;
    g_hooks.walkAndInvoke     = hooks.walkAndInvoke     ? hooks.walkAndInvoke     : &DefaultWalkAndInvoke;
}
const RenderLeaves2Hooks& GetRenderLeaves2Hooks() { return g_hooks; }
GlobalLightDir& GlobalLight() { return g_globalLight; }

// ===========================================================================
// 0x435748 — VIBE_Render_PutPixel
// ===========================================================================
i32 PutPixel(FrameTarget& t, i32 x, i32 y, void* ctx, u8 g, u8 r, u8 b) {
    if (x < 0)            return x;
    if (y < 0)            return x;
    if (y >= t.height)    return x;
    if (x >= t.width)     return x;

    if (!g_hooks.beginFrameLock(ctx)) return 0;  // VIBE_Render_BeginFrameLock

    // v9 = pack via the per-channel pos/prec table (byte_762719..76271E):
    //   (b >> bPrec << bPos) | (g >> gPrec << gPos) | (r >> rPrec << rPos)
    const u32 v9 = (u32)(((int)b >> t.fmt.bPrec << t.fmt.bPos)
                       | ((int)g >> t.fmt.gPrec << t.fmt.gPos)
                       | ((int)r >> t.fmt.rPrec << t.fmt.rPos));

    const i32 bpp = t.bpp;             // dword_62D590
    const int v10 = x + y * t.pitch;   // v6 + v8*dword_7626F8

    if ((u32)bpp >= 0x10) {
        if ((u32)bpp > 0x10) {
            if ((u32)bpp >= 0x18) {
                if ((u32)bpp > 0x18) {
                    if (bpp == 32 && t.base) {
                        ((u32*)t.base)[v10] = v9;
                    }
                } else {                  // ==24
                    if (t.base) {
                        u8* p = t.base + (size_t)t.byteStride * x + (size_t)t.pitchBytes * y;
                        ((u16*)p)[0] = (u16)v9;
                        p[2] = (u8)(v9 >> 16);
                    }
                }
            }
            // 17..23: fall through (no write)
        } else {                          // ==16 -> LABEL_12
            if (t.base) ((u16*)t.base)[v10] = (u16)v9;
        }
    } else {
        if ((u32)bpp < 8) {
            // no write
        } else if ((u32)bpp <= 8) {       // ==8 grey
            if (t.base) t.base[v10] = (u8)(((int)b + (int)r + (int)g) / 3);
        } else if (bpp == 15) {           // LABEL_12 16bpp
            if (t.base) ((u16*)t.base)[v10] = (u16)v9;
        }
    }
    return 1;
}

// ===========================================================================
// 0x4351d8 — VIBE_Render_DrawHLine (16bpp)
// ===========================================================================
i32 DrawHLine(FrameTarget& t, i32 x0, i32 y0, i32 y1, i32 x1, u16 color) {
    u16* const base = (u16*)t.base;
    const int  pitch = t.pitch;
    i32 result = x0;
    const i32 a2 = y0, a3 = y1, a4 = x1;

    if (y0 == y1) {                       // horizontal run
        int v7, v8;
        if (result <= a4) {               // x0 <= x1
            v7 = pitch * a2 + result;
            v8 = a4 - result;
        } else {
            v8 = result - a4;
            v7 = pitch * a2 + a4;
        }
        result = v7;
        for (; v8 > 0; --v8) {
            base[result] = color;
            ++result;
        }
    } else if (result == a4) {            // x0 == x1: vertical line
        int idx, count;
        if (a2 <= a3) {
            idx   = result + pitch * a2;
            count = a3 - a2;
        } else {
            count = a2 - a3;
            idx   = result + pitch * a3;
        }
        result = idx;
        for (int i = count; i > 0; --i) {
            base[result] = color;
            result += pitch;
        }
    } else {                              // Bresenham
        int sx, dy, sy, dx;
        if (result >= a4) { sx = -1; dx = result - a4; }
        else              { dx = a4 - result; sx = 1; }
        int yy = a2;
        if (a2 >= a3) { sy = -1; dy = a2 - a3; }
        else          { sy = 1;  dy = a3 - a2; }

        base[result + pitch * yy] = color;   // first pixel
        if (dy >= dx) {                       // y-major
            const int inc = 2 * (dx - dy);
            int err = 2 * dx - dy;
            while (yy != a3) {
                yy += sy;
                if (err < 0) {
                    err += 2 * dx;
                } else {
                    result += sx;
                    err += inc;
                }
                base[result + pitch * yy] = color;
            }
        } else {                              // x-major
            const int inc = 2 * (dy - dx);
            const int two = 2 * dy;
            int err = 2 * dy - dx;
            while (result != a4) {
                result += sx;
                if (err < 0) {
                    err += two;
                } else {
                    yy += sy;
                    err += inc;
                }
                base[result + pitch * yy] = color;
            }
        }
    }
    return result;
}

// ===========================================================================
// 0x423ab8 — VIBE_Surface_BlitClipped
// ===========================================================================
i32 BlitClipped(BlitClipRect& out, i32 a1, i32 a2, i32 a3, i32 a4,
                void* a5, i32 a6, i32 a7, void* a8) {
    int v8 = a6;   // clamped srcX
    int v9 = a7;   // clamped srcY

    if (a2 < 0) { a3 += a2; a2 = 0; }      // dstY clamp shrinks height
    if (a1 < 0) { a4 += a1; a1 = 0; }      // dstX clamp shrinks width
    if (a6 < 0) { a1 -= a6; a4 += a6; v8 = 0; }  // srcX clamp
    if (a7 < 0) { a2 -= a7; a3 += a7; v9 = 0; }  // srcY clamp

    out.dstX = a1; out.dstY = a2; out.w = a4; out.h = a3;
    out.srcX = v8; out.srcY = v9; out.issued = false;

    if (!a3 || !a4) return 0;              // collapsed span

    if (a5) {
        const i32 dstRect[4] = {a1, a2, a4 + a1, a3 + a2};   // v11[0..3]
        const i32 srcRect[4] = {v8, v9, a4 + v8, a3 + v9};   // v12[0..3]
        g_hooks.blit(a8, a5, dstRect, srcRect);
        out.issued = true;
    }
    return 1;
}

// ===========================================================================
// 0x5dae38 — VIBE_Texture_PackColorFlags
// ===========================================================================
void PackColorFlags(const u8* rec, u8* out0, u8* out1) {
    if (rec && out0) {
        if (out1) {
            const u8 c104 = rec[104];
            *out0 = (u8)((((u8)(4 * c104) >> 7) << 6)
                       | (4 * (rec[114] & 0xF))
                       | (c104 & 1)
                       | (2 * ((u8)(8 * c104) >> 7)));
            *out1 = (u8)(((rec[105] & 0x1F) << 4) | (rec[106] & 0x1F));
        }
    }
}

// ===========================================================================
// 0x4289f0 — VIBE_Mesh_SetVertexColorRgb
// ===========================================================================
i8 SetVertexColorRgb(void* obj, const u8* rgb) {
    // SetVertexColors(obj, b=rgb[0], g=rgb[2], r=rgb[1])
    SetVertexColors(obj, rgb[0], rgb[2], rgb[1]);
    return 1;
}

// ===========================================================================
// 0x428a10 — VIBE_Mesh_SetGlobalColorTemp
// ===========================================================================
i8 SetGlobalColorTemp(void* obj, u8 b, u8 g, u8 r) {
    // v9[0]=a2(b), v9[1]=a4(r), v9[2]=a3(g)
    const u8 payload[3] = {b, r, g};
    if (!obj) return 0;
    u8* o = reinterpret_cast<u8*>(obj);
    i8 result;
    if ((o[528] & 1) != 0) {
        result = g_hooks.walkAndInvoke(/*table=*/nullptr, obj,
                                       (void*)&SetVertexColorRgb, 511, payload);
    } else {
        // temporarily zero obj+496 (light-cache ptr) across the walk, then restore
        std::uint32_t saved;
        std::memcpy(&saved, o + 496, sizeof(saved));
        std::uint32_t zero = 0;
        std::memcpy(o + 496, &zero, sizeof(zero));
        result = g_hooks.walkAndInvoke(nullptr, obj,
                                       (void*)&SetVertexColorRgb, 511, payload);
        std::memcpy(o + 496, &saved, sizeof(saved));
    }
    return result;
}

// ===========================================================================
// 0x5d329c — VIBE_Mesh_GetBoundingRadius
// ===========================================================================
double GetBoundingRadius(const void* obj) {
    if (!obj) return 0.0;
    const void* mesh = *reinterpret_cast<const void* const*>(
        reinterpret_cast<const u8*>(obj) + 16);     // obj+16
    if (!mesh) return 0.0;
    return *reinterpret_cast<const float*>(
        reinterpret_cast<const u8*>(mesh) + 468);   // mesh+468
}

// ===========================================================================
// 0x4b24b0 — VIBE_Light_SetSunHeight
// ===========================================================================
i8 SetSunHeight(float* sunPitchSlot, int raise) {
    double v3;
    if (raise)
        v3 = kSunBaseUp - util::RandomFloatScaled() * kSunRandSpan;   // -0.3 - r*0.6
    else
        v3 = util::RandomFloatScaled() * kSunRandSpan + kSunBaseDown; //  r*0.6 + 0.3
    if (sunPitchSlot)
        *sunPitchSlot = (float)v3;
    return 1;
}

// ===========================================================================
// 0x43ea0c — VIBE_Light_SetGlobalDirection
// ===========================================================================
i32 SetGlobalDirection(const i32* x, const i32* y, const i32* z) {
    g_globalLight.x = (float)*x;   // flt_64A074
    g_globalLight.y = (float)*y;   // flt_64A078
    g_globalLight.z = (float)*z;   // flt_64A07C
    g_hooks.refreshAllObjects(1u); // VIBE_Light_RefreshAllObjects(1)
    return 1;
}

// ===========================================================================
// 0x5db694 — VIBE_Texture_SetTransparencyFlag (core bit math)
// ===========================================================================
i8 SetTransparencyFlag(u8* rec, i8 on) {
    if (!rec) return 0;
    // current bit = (32 * rec[104]) >> 7  i.e. bit2 (0x04) of rec[104]
    if ((u8)((u8)(32 * rec[104]) >> 7) == (u8)(on & 1))
        return 0;                              // no change
    const u8 cleared = (u8)(rec[104] & 0xFB);  // clear bit2
    rec[104] = (u8)((4 * (on & 1)) | cleared); // set bit2 to on&1
    return 1;
}

// ===========================================================================
// 0x5dbde0 — VIBE_Texture_CloneIfPaletteMatch
// ===========================================================================
void* CloneIfPaletteMatch(void* rec, i8 pal, i8 a3) {
    u8* r = reinterpret_cast<u8*>(rec);
    if (r[108] == 0xFF) {
        const u8 v5 = r[110];
        if ((v5 & 1) == 0 && (pal != -1 || (v5 & 1) != 0))
            return g_hooks.cloneRecord(rec, pal, a3);  // VIBE_Texture_CloneRecord
    }
    return rec;
}

} // namespace guild::render
