#include "render/scene_recon5_raster.h"
#include <cstring>

namespace guild::render {

// Float constants from the snow init path (gilde.exe .rdata @0x6117CC..0x6117EC).
static const f32 kSnowGust      = 2500.0f;      // flt_6117CC
static const f32 kSnowWindScale = 0.001f;       // flt_6117D0
static const f32 kRandScale     = 3.0517578e-05f; // flt_6117D4 (= 1/32768)
static const f32 k2             = 2.0f;         // flt_6117D8
static const f32 kHalfD         = 0.5f;         // flt_6117DC
static const f32 k0p01          = 0.01f;        // flt_6117E0
static const f32 kNegHalf       = -0.5f;        // flt_6117E4
static const f32 k0p75          = 0.75f;        // flt_6117E8
static const f32 k0p025         = 0.025f;       // flt_6117EC

// ---------------------------------------------------------------------------
// gilde.exe 0x4351d8 — VIBE_Render_DrawHLine.  base = fb.base (dword_7626F0),
// stride = fb.stride (dword_7626F8, pixels-per-row). Returns the running x
// accumulator like the original.
// ---------------------------------------------------------------------------
i32 DrawHLine(Framebuffer16& fb, i32 x0, i32 y0, i32 y1, i32 x1, u16 color) {
    u16* base = fb.base;          // v5 = dword_7626F0
    i32  stride = fb.stride;      // v6 = dword_7626F8
    i32  result = x0;             // eax

    if (y0 == y1) {               // a2 == a3  (horizontal run on row y0)
        i32 v7;
        if (result <= x1) {       // result <= a4
            v7 = stride * y0 + result;   // dword_7626F8*a2 + result
            result = x1 - result;        // count
        } else {
            result -= x1;                // count
            v7 = stride * y0 + x1;
        }
        i32 v8 = result;
        if (result > 0) {
            // original: result = 2*v7 (byte offset), then steps +2 bytes / +1 px
            for (; v8 > 0; --v8) {
                base[v7] = color;
                ++v7;
            }
            // running accumulator ends at 2*v7 in the original; we return v7-ish
            // via `result`. The original's `result` is left as the final 2*v7.
            result = 2 * v7;
        }
    } else if (result == x1) {    // result == a4  (vertical run on column x0)
        i32 v10;
        if (y0 <= y1) {           // a2 <= a3
            result += stride * y0;
            v10 = y1 - y0;
        } else {
            v10 = y0 - y1;
            result += stride * y1;
        }
        for (i32 i = v10; i > 0; --i) {
            base[result] = color;
            result += stride;     // v6
        }
    } else {                      // Bresenham
        i32 v22, v13;             // x-step, |dx|
        if (result >= x1) {       // result >= a4
            v22 = -1;
            v13 = result - x1;
        } else {
            v13 = x1 - result;
            v22 = 1;
        }
        i32 v17 = v13;            // |dx|
        i32 v23, v14;             // y-step, |dy|
        if (y0 >= y1) {           // a2 >= a3
            v23 = -1;
            v14 = y0 - y1;
        } else {
            v23 = 1;
            v14 = y1 - y0;
        }
        // first pixel
        base[result + stride * y0] = color;
        if (v14 >= v17) {         // y-major
            i32 v19 = 2 * (v17 - v14);
            i32 j = 2 * v17 - v14;
            for (; y0 != y1; ) {
                y0 += v23;
                if (j < 0) {
                    j += 2 * v17;
                } else {
                    result += v22;
                    j += v19;
                }
                base[result + stride * y0] = color;
            }
        } else {                  // x-major
            i32 v18 = 2 * (v14 - v17);
            i32 v20 = 2 * v14;
            i32 k = 2 * v14 - v17;
            for (; result != x1; ) {
                result += v22;
                if (k < 0) {
                    k += v20;
                } else {
                    y0 += v23;
                    k += v18;
                }
                base[result + stride * y0] = color;
            }
        }
    }
    fb.stride = stride;
    fb.base = base;
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4353dc — VIBE_Render_DrawLineLocked. Lock-wrapper around DrawHLine.
// ---------------------------------------------------------------------------
i32 DrawLineLocked(Framebuffer16& fb, i32 x0, i32 y0, i32 y1, i32 x1, u16 color,
                   const DrawLineLockedHooks& hooks, u8 unlockMode, bool lockDirty) {
    bool ok = hooks.beginFrameLock ? hooks.beginFrameLock(hooks.surf) : true;
    if (!ok)
        return 0;                 // result==0 (lock failed)
    i32 r = DrawHLine(fb, x0, y0, y1, x1, color);
    if (lockDirty) {              // dword_7626F0 != 0 gate
        switch (unlockMode) {     // byte_762721
            case 0: case 2: case 4:
                break;
            case 1: case 3:
                if (hooks.endUnlock) hooks.endUnlock(hooks.surf);
                break;
            default:
                return r;         // original returns result unchanged
        }
        // dword_7626F0 = 0; result = 0;
        r = 0;
    }
    return r;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x42a3e4 — VIBE_Snow_GrowFlakeList.
// ---------------------------------------------------------------------------
static f32 randUnit(const SnowRng& rng) {
    i32 r = rng.next ? rng.next(rng.ctx) : 0;
    return (f32)r * kRandScale;     // (double)(int)v * flt_6117D4 in the original
}

void SnowGrowFlakeList(SnowState& st, i32 count, i32 kind, i32 arg4,
                       const SnowRng& rng, const SnowAllocHooks& mem) {
    if (count <= 0)                 // if ( a2 > 0 )
        return;
    for (i32 iter = 0; iter < count; ++iter) {
        switch (kind) {
        case 0:
        case 1: {
            if (kind >= st.capacity) {        // a3 >= *(a1+4)  ->  need grow
                i32 newCap = kind + 100;      // v23 = a3 + 100
                std::size_t bytes = (std::size_t)40 * (std::size_t)newCap;
                void* blk = mem.alloc ? mem.alloc(bytes, mem.ctx) : nullptr;
                if (!blk)
                    return;                   // alloc failed -> bail (as original)
                SnowFlake* nf = static_cast<SnowFlake*>(blk);
                if (st.flakes && st.capacity > 0) {
                    std::memcpy(nf, st.flakes,
                                (std::size_t)40 * (std::size_t)st.capacity);
                    if (mem.free) mem.free(st.flakes, mem.ctx);
                }
                st.flakes = nf;
                st.capacity = newCap;         // *(a1+4) = v23
                // RNG-init the newly grown tail [capacityUsed .. newCap)
                if (st.capacityUsed < newCap) {
                    for (i32 fi = st.capacityUsed; fi < newCap; ++fi) {
                        SnowFlake& f = st.flakes[fi];
                        f.posX = (randUnit(rng) + kNegHalf) * k2;   // [-1,1)
                        f.posY = (randUnit(rng) + kNegHalf) * k2;
                        f.posZ = (randUnit(rng) + kNegHalf) * k2;
                        f.velX = randUnit(rng) * kHalfD + k0p75;    // ~[0.75,1.25)
                        f.swayA = randUnit(rng) * k0p01 + k0p025;
                        f.swayB = randUnit(rng) * k0p01 + k0p025;
                    }
                }
            }
            // LABEL_4
            if (kind) {
                st.activeCount = st.capacityUsed;   // *(a1+8) = *a1
                st.pendingKind = kind;              // *(a1+12) = a3
                i32 base = st.timeBase;             // *(a1+92)
                st.t0 = base;                       // *(a1+96)
                st.t1 = kind + base;                // *(a1+100) = a3 + v18
            } else {
                st.capacityUsed = 0;                // *a1 = 0
            }
            break;
        }
        case 2: {
            st.windScaledX = (f32)((double)kind * (double)kSnowWindScale); // +76
            i32 base = st.timeBase;                 // *(a1+92)
            st.t2 = base;                           // *(a1+104)
            st.windScaledY = (f32)((double)kSnowWindScale * (double)arg4); // +80
            st.windX0 = st.windSrcA;                // *(a1+60) = *(a1+68)
            st.windX1 = st.windSrcB;                // *(a1+64) = *(a1+72)
            st.t3 = arg4 + base;                    // *(a1+108) = a4 + v20
            break;
        }
        case 3: {
            f32 v21 = (f32)((double)kind * (double)kSnowGust); // a3*2500.0
            if (st.gust <= v21)                     // *(a1+84) <= v21
                st.gust = v21;
            // else leave gust unchanged
            break;
        }
        default:
            break;
        }
        // LABEL_6: loop guard (++v22 >= a2 -> return) is the for-condition.
    }
}

} // namespace guild::render
