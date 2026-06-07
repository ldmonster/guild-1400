#include "render/clip.h"

// =============================================================================
// guild::render polygon clipper — implementation. See clip.h for the module
// overview and the recovered global layout. The arithmetic mirrors the
// Hex-Rays pseudocode of gilde.exe 0x5AD7D8 verbatim: the same plane dot
// products, the same inside test (dot >= plane.d), the same edge interpolation
// t = (d - dotPrev)/(dotCur - dotPrev), and the same byte-truncated colour lerp.
// =============================================================================
namespace guild::render {

namespace {

// Plane dot: a*Px + b*Py + c*Pz (the original reads the vertex's xyz from +0/+4/+8
// and the plane's a/b/c from the plane quad). Matches v9/v15 in the pseudocode.
inline float PlaneDot(const ClipPlane& pl, const Vertex* p) {
    return pl.a * p->x + pl.b * p->y + pl.c * p->z;
}

// Truncate-toward-zero into a byte the lerped colour channel:
//   out = (int)((double)prevByte + t * (curByte - prevByte))
// (the original computes the float difference, multiplies by t, adds the base,
// and stores the low byte via the FPU->int truncation).
inline u8 LerpByte(u8 prev, u8 cur, float t) {
    return (u8)(int)((double)prev + (double)t * (double)((int)cur - (int)prev));
}

} // namespace

// gilde.exe 0x5AD7D8 — VIBE_Render_ClipPolygonToPlane
//
//   char *__usercall ClipPolygonToPlane@<eax>(unsigned int a1@<eax>)
//
// a1 is the clip context (a1[0] = plane count, planes at a1+8). The working
// polygon's vertex pointers are seeded in dword_13D8798 (list side 0) with
// dword_649D78 = seedCount. Each plane pass clips the current list into the
// other ping-pong side and swaps; the new (interpolated) vertices accumulate in
// the unk_13D8B98 pool. Returns the final list base (a fan of outCount ptrs).
Vertex** ClipPolygonToPlane(ClipScratch& scratch, const ClipContext& ctx,
                            i32 seedCount) {
    scratch.inCount = seedCount;             // dword_649D78 (set by the flush)
    scratch.outCount = 0;                    // dword_649D74 = 0

    // if ( !(*(_DWORD *)a1 | (unsigned int)v2 | a1) ) return 0;  — the original
    // null/empty guard. Here: no planes and no seed -> nothing to clip.
    if (ctx.planeCount == 0 && ctx.planes == nullptr && seedCount == 0)
        return nullptr;

    Vertex* poolCursor = scratch.newVerts;   // v3 = &unk_13D8B98 (NOT reset/plane)
    i32 side = 0;                            // (i & 1): which ping-pong list is input
    u32 i = 0;                               // plane index

    for (;; ++i) {
        side = (i & 1);                      // v4 = (i&1)<<9  -> list side
        Vertex** in = scratch.listPtrs[side];        // v5 = dword_13D8798 + v4
        if ((u32)ctx.planeCount <= i)        // *(_DWORD *)a1 <= i -> done
            break;

        const ClipPlane& pl = ctx.planes[i]; // v27 walked +4 floats per plane

        scratch.outCount = 0;                // dword_649D74 = 0
        Vertex* first = in[0];               // v8 = *v5
        float dotFirst = PlaneDot(pl, first);// v9
        i32 inCount = scratch.inCount;       // v10 = dword_649D78
        in[inCount] = in[0];                 // v5[dword_649D78] = *v5 (close poly)

        Vertex** out = scratch.listPtrs[(i + 1) & 1]; // v12 = other side
        Vertex** outCur = out;
        bool prevInside = (dotFirst >= pl.d);// v23 = v24 >= v27[3]
        float dotPrev = dotFirst;            // v24

        Vertex** walk = in;                  // v11 = v5
        for (u32 e = 0; e < (u32)inCount; ++e) { // v28 < dword_649D78
            Vertex* cur = walk[1];           // v13 = v11[1]
            Vertex* prev = walk[0];          // v14 = *v11
            float dotCur = PlaneDot(pl, cur);// v15
            bool curInside = (dotCur >= pl.d); // v16 = v35 >= v27[3]

            if (prevInside) {                // emit prev
                *outCur++ = prev;            // *(v12-1) = v14 ; ++v12
                ++scratch.outCount;          // dword_649D74 + 1
            }

            if (curInside != prevInside) {   // v16 != v23 -> generate a vertex
                float t = (pl.d - dotPrev) / (dotCur - dotPrev); // v18
                Vertex* nv = poolCursor;     // v3

                // xyz + w (+0/+4/+8/+12) lerp prev->cur.
                nv->x  = t * (cur->x - prev->x) + prev->x;
                nv->y  = t * (cur->y - prev->y) + prev->y;
                nv->z  = t * (cur->z - prev->z) + prev->z;
                nv->_pad0c = t * (cur->_pad0c - prev->_pad0c) + prev->_pad0c;
                // +44 float (v3[11]) — the original interpolates this scalar too.
                {
                    const float* pc = (const float*)((const u8*)cur + 44);
                    const float* pp = (const float*)((const u8*)prev + 44);
                    float* po = (float*)((u8*)nv + 44);
                    *po = (*pc - *pp) * t + *pp;
                }
                // Colour bytes +64..+67 and +79, byte-truncated lerp.
                const u8* cb = (const u8*)cur;
                const u8* pb = (const u8*)prev;
                u8* ob = (u8*)nv;
                ob[64] = LerpByte(pb[64], cb[64], t);
                ob[65] = LerpByte(pb[65], cb[65], t);
                ob[66] = LerpByte(pb[66], cb[66], t);
                ob[67] = LerpByte(pb[67], cb[67], t);
                ob[79] = LerpByte(pb[79], cb[79], t);

                *outCur++ = nv;              // *(v12-1) = (int)v3 ; ++v12
                ++scratch.outCount;          // dword_649D74 + 1
                ++poolCursor;                // v3 += 20 (next 80-byte slot)
            }

            prevInside = curInside;          // v23 = v16
            dotPrev = dotCur;                // v24 = v35
            ++walk;                          // ++v11
        }

        scratch.inCount = scratch.outCount;  // dword_649D78 = dword_649D74
    }

    return scratch.listPtrs[i & 1];          // dword_13D8798 + v4 (final side)
}

} // namespace guild::render
