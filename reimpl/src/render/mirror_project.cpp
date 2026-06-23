#include "render/mirror_project.h"
#include "render/mirror_silhouette.h"   // CreateOutline (0x5F58FC)

#include "util/math.h"   // TriangleNormal / VectorWithinTolerance

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace guild::render {

// flt_62C39C = 2.0f (0x40000000) — the reflection scale (same constant as
// flt_62C3A0 used by ReflectPointAcrossPlane).
static constexpr float kReflectScale = 2.0f;

// gilde.exe 0x5F6084 — VIBE_Mirror_ProjectReflectedVertices.
void ReflectAndProjectVertices(MirrorVertex* verts, int count,
                               const MirrorPlane& plane,
                               const ProjectionParams& proj) {
    for (int i = 0; i < count; ++i) {
        MirrorVertex& v = verts[i];
        // v5 = -(x*nx + y*ny + z*nz - d) * 2.0
        float t = -(v.x * plane.nx + v.y * plane.ny + v.z * plane.nz - plane.d)
                  * kReflectScale;                       // v12 = v5
        // Reflect in place: P' = P + t*n  (order: x, then y, then z).
        v.x = t * plane.nx + v.x;
        float vx = proj.projX * v.x;                     // v6 = flt_13FCD0C * x'
        v.y = t * plane.ny + v.y;
        float vy = proj.projY * v.y;                     // v7 = flt_13FCAF8 * y'
        v.z = t * plane.nz + v.z;
        float invZ = 1.0f / v.z;                         // v9 = 1.0 / z'
        // Outputs: screenX = vx*invZ + offX ; screenY = invZ*vy + offY ; t.
        v.t = t;                                         // *(v2-13)
        v.screenY = invZ * vy + proj.offY;               // v11 = v9*v7 + flt_13FCD10
        v.screenX = vx * invZ + proj.offX;               // v10 + flt_13FCD18
    }
}

// =============================================================================
// Allocator hook (gilde.exe 0x438f10 / 0x43923c). See header.
// =============================================================================
namespace {
// VIBE_Memory_AllocDebug (0x438f10) ZERO-FILLS the allocation (memset(p,0,n)) —
// the dedup/outline code relies on that, so the default zero-fills too (calloc).
void* DefaultMirrorAlloc(u32 n, const char*) { return std::calloc(n ? n : 1u, 1u); }
void  DefaultMirrorFree(void* p) { std::free(p); }
MirrorAllocHook g_alloc = &DefaultMirrorAlloc;
MirrorFreeHook  g_free  = &DefaultMirrorFree;

// gilde.exe dword_13DB398 — the runtime per-orientation frustum table. All-zero
// at static analysis (a runtime-populated buffer), so the standalone default
// appends no frustum planes. See header (rule-8 hook).
MirrorFrustumPlanes DefaultFrustumTable(u32) { return MirrorFrustumPlanes{0, nullptr}; }
MirrorFrustumTableHook g_frustum = &DefaultFrustumTable;

// gilde.exe VIBE_Transform_RotateVectorWithFrame (0x5c8ab4) — default is IDENTITY:
// with no active camera node (dword_13FCD1C == 0) the engine leaves the surface
// normal unrotated, so the stored normal is used directly.
void DefaultRotateNormal(const float* /*node*/, const float* src, float* out) {
    out[0] = src[0]; out[1] = src[1]; out[2] = src[2];
}
MirrorRotateNormalHook g_rotate = &DefaultRotateNormal;
} // namespace

void SetMirrorAllocHook(MirrorAllocHook a) { g_alloc = a ? a : &DefaultMirrorAlloc; }
void SetMirrorFreeHook(MirrorFreeHook f)   { g_free  = f ? f : &DefaultMirrorFree; }
void* MirrorAlloc(u32 n, const char* tag)  { return g_alloc(n, tag); }
void  MirrorFree(void* p)                  { if (p) g_free(p); }
void SetMirrorFrustumTableHook(MirrorFrustumTableHook h) {
    g_frustum = h ? h : &DefaultFrustumTable;
}
void SetMirrorRotateNormalHook(MirrorRotateNormalHook h) {
    g_rotate = h ? h : &DefaultRotateNormal;
}

// =============================================================================
// gilde.exe 0x5F5D08 — VIBE_Mirror_CreateClippingPlanes.
// =============================================================================
MirrorClipPlaneList* CreateClippingPlanes(const MirrorMeshBlock* mesh,
                                          u32 surfaceKey) {
    if (!mesh)                                     // if ( !a1 ) return 0
        return nullptr;
    const MirrorPoly* polys = mesh->polys;         // v4 = *(a1+460) sub-block; here
    const u32 polyCount = (u32)mesh->polyCount;    //   v6 = block[3]; v12 = block[1]
    if (polyCount == 0 || surfaceKey == 0)         // !v4 || !a2 -> 0
        return nullptr;

    // Pass 1: count the polys belonging to this mirror surface (poly[20]==a2).
    u32 surfPolyCount = 0;                          // v8
    for (u32 i = 0; i < polyCount; ++i)
        if (polys[i].surfaceKey == surfaceKey)
            ++surfPolyCount;
    if (surfPolyCount == 0)
        return nullptr;

    // (As in CreateOutline: the original sizes pointer buffers in bytes assuming
    // 4-byte pointers. We keep the SAME slot counts at host pointer width.)
    const u32 kPtrSlot = (u32)sizeof(void*);

    // Pass 2: collect the back-facing (sign byte<0) surface polys' pointers.
    // Engine: 4*surfPolyCount bytes == surfPolyCount slots.
    const MirrorPoly** polyList =
        (const MirrorPoly**)MirrorAlloc(surfPolyCount * kPtrSlot, "d3_mir:CrClippingPolys");
    u32 kept = 0;                                   // v11
    for (u32 i = 0; i < polyCount; ++i) {
        const MirrorPoly& p = polys[i];
        if (p.surfaceKey == surfaceKey && (i8)(p.signByte & 0xFF) < 0)
            polyList[kept++] = &p;
    }

    // Gather the kept polys' vertex points; the unique-point buffer follows the
    // 12-ptr-per-poly raw region (v56[0] = v57 + 12*kept). Original frustum-cull:
    // a poly is dropped when (oc0 & oc1 & oc2) & 0x3F != 0; the OR'd outcode is
    // accumulated into `orient`. Vertex records carry no outcode in the portable
    // view (a runtime view-clip field), so all kept polys contribute and orient
    // stays 0 — the same path the engine takes when nothing is frustum-clipped.
    // Engine: 24*kept bytes == 6*kept pointer slots; unique = points + 12*kept
    // bytes == +3*kept slots. (points holds rawPts<=3*kept; unique the dedup set.)
    float** points =
        (float**)MirrorAlloc(6u * kept * kPtrSlot, "d3_mir:CrClippingPoints");
    float** unique = points + 3u * kept;             // v56[0]
    u32 orient = 0;                                  // v64 (OR'd outcode & 0x3F)
    u32 rawPts = 0;                                  // v16 (raw collected ptr count)
    for (u32 i = 0; i < kept; ++i) {
        const MirrorPoly* p = polyList[i];
        // (oc0 & oc1 & oc2) & 0x3F == 0 always here (no outcodes in the view).
        for (int j = 0; j < 3; ++j)
            points[rawPts++] = p->v[j];
    }
    MirrorFree(polyList);

    // Dedup the gathered points into `unique` (by pointer-eq or 0.001 tolerance).
    u32 uniqueCount = 0;                             // v62
    for (u32 r = 0; r < rawPts; ++r) {
        float* pt = points[r];
        u32 s = 0;                                   // v25
        for (; s < rawPts; ++s) {
            if (unique[s] == nullptr) break;         // first empty slot
            if (unique[s] == pt) break;              // identical pointer
            if (util::VectorWithinTolerance(unique[s], pt, 0.001f))
                break;                               // near-coincident
        }
        if (s < rawPts && unique[s] == nullptr) {
            unique[s] = pt;
            ++uniqueCount;
        }
    }

    // Trace the outline; need >= 3 unique points and a valid (>=6 ptr) outline.
    const float** outline = nullptr;                 // overwrites v56[0]
    u32 outlinePtrs = 0;                             // v55
    if (uniqueCount < 3 ||
        !CreateOutline(unique, uniqueCount, &outline, &outlinePtrs)) {
        MirrorFree(points);
        return nullptr;
    }
    if (outlinePtrs < 6) {                           // degenerate outline
        MirrorFree(outline);
        MirrorFree(points);
        return nullptr;
    }

    const u32 edgeCount = outlinePtrs >> 1;          // v55 >>= 1
    MirrorFrustumPlanes frustum = g_frustum(orient & 0x3F); // dword_13DB398[26*code]
    const u32 frustumCount = (u32)frustum.count;     // v29
    const u32 total = edgeCount + frustumCount;      // v35

    // Final buffer: 8-byte header + 16 bytes per plane.
    u8* buf = (u8*)MirrorAlloc(16u * total + 8u, "d3_mir:CrClippingPlanesFinal");
    *(i32*)(buf + 0) = (i32)total;                   // *v32 = count
    buf[4] = 1;                                       // v32[4] = 1 (flag byte)

    MirrorClipPlaneOut* rec = (MirrorClipPlaneOut*)(buf + 8); // v53 = v32+8
    for (u32 e = 0; e < edgeCount; ++e) {
        const float* a = outline[2u * e];            // v38 = out[2e]
        const float* b = outline[2u * e + 1];        // v37 = out[2e+1]
        static float origin[3] = {0.0f, 0.0f, 0.0f}; // flt_5CA2E0
        float n[3];
        // TriangleNormal(origin, a, n_out, b) -> util reorder (origin,a,b,out).
        util::TriangleNormal(origin, (float*)a, (float*)b, n);
        rec[e].nx = n[0];
        rec[e].ny = n[1];
        rec[e].nz = n[2];
        // d = -(n . a)  (the original dots n with out[2e] == a).
        rec[e].d = -(n[0] * a[0] + n[1] * a[1] + n[2] * a[2]);
    }

    MirrorFree(outline);   // free the outline pointer array (v56[0] = 0 after)

    // Append the per-orientation frustum planes verbatim (16 bytes each).
    if (frustumCount && frustum.planes) {
        std::memcpy(rec + edgeCount, frustum.planes,
                    16u * frustumCount);
    }

    MirrorFree(points);

    // The engine returns the raw buffer (header at +0/+4, records at +8). The
    // typed view reads it in place — no extra allocation.
    return (MirrorClipPlaneList*)buf;                 // return v59
}

// =============================================================================
// gilde.exe 0x5F676C — VIBE_Mirror_PrepareReflectionNode.
// =============================================================================
void* PrepareReflectionNode(ReflectionNode* node, ReflectionBindContext* ctx,
                            void** reflectionPreparedGlobal,
                            float* /*frame*/, const float* /*basis*/) {
    void* result = node;                             // result/v20 = node
    // Early-out while a reflection node is already published (dword_649D6C).
    if (reflectionPreparedGlobal && *reflectionPreparedGlobal != nullptr)
        return result;
    if (!node || !node->mesh)                         // if (!v3) / if (!v3[4])
        return result;
    MirrorMeshBlock* mesh = node->mesh;               // v3
    if (!mesh->parent)                                // v3[4] (parent sub-object)
        return result;

    // Child count = *(parent + 480) ; child array = mesh->childArray (v3[5]).
    const u32 childCount = *((u32*)mesh->parent + (480 / 4)); // v5
    void** children = (void**)mesh->childArray;       // v6
    if (childCount == 0)
        return result;

    // Find the reflective child: byte[104] & 0x20 set. The engine assigns the
    // loop's `result` = *v6++ on EVERY iteration (0x5f67b3), so when no reflective
    // child is found it returns the LAST child examined (children[childCount-1]),
    // not the node — match that exactly.
    void* reflective = nullptr;                        // result on break
    for (u32 c = 0; c < childCount; ++c) {
        void* child = children[c];                     // result = *v6++
        result = child;                                // result tracks last child
        if (child && (((u8*)child)[104] & 0x20) != 0) {
            reflective = child;
            break;
        }
    }
    if (!reflective)
        return result;                                 // ran off the end -> last child

    // Bind: install the project callback + the surface poly into ctx.
    ctx->flags &= (u8)~0x02;                           // clear rebuild bit (v7&0xFD)
    void* prevMesh = ctx->currentMesh;                 // v9 = *(a2+4)
    ctx->surfacePoly = reflective;                     // *(a2+12) = result

    const u32 polyCount = (u32)mesh->polyCount;        // v8 = v3[3]
    if (mesh != prevMesh) {                            // if ( v3 != v9 )
        // Find the poly whose surface key (+20) matches the reflective child. The
        // engine compares `result` (the child) to *(poly+20); in this u32 view the
        // key is the child pointer reinterpreted.
        const u32 childKey = (u32)(uintptr_t)reflective;
        const MirrorPoly* polys = mesh->polys;         // v10 = v3[1]
        u32 k = 0;                                      // v11
        if ((i32)polyCount > 0) {
            for (; k < polyCount; ++k) {
                if (polys[k].surfaceKey == childKey)    // result == *(v10+20)
                    break;
            }
            if (k < polyCount) {
                ctx->projectCallback = (void*)&ReflectAndProjectVertices; // a2+20
                ctx->currentMesh = mesh;               // *(a2+4) = v3
                ctx->boundPoly = &polys[k];            // *(a2+16) = v10
                ctx->node = node;                      // *a2 = v20
            }
        }
    }

    // Recompute the active/rebuild flag from the node's +528 byte (bit3 test:
    // 2 * ((u8)(16*b) >> 7) == ((b>>3)&1) << 1).
    u8 f = ctx->flags & (u8)0xFD;                      // v12
    u8 bit = (u8)(2u * (((u8)(16u * node->activeFlag)) >> 7)); // v13
    ctx->flags = (u8)(bit | f);

    if (((bit | f) & 0x02) != 0) {
        // A rebuild is needed: re-locate the matched (back-facing) surface poly,
        // rebuild the clip planes, and (re)publish.
        const u32 childKey2 = (u32)(uintptr_t)reflective;
        const MirrorPoly* polys = mesh->polys;
        u32 k = 0;                                      // i
        for (; k < polyCount; ++k) {
            if ((i8)(polys[k].signByte & 0xFF) < 0 &&
                polys[k].surfaceKey == childKey2)       // *(v14+36)<0 && match poly+20
                break;
        }
        bool found = (k < (u32)mesh->polyCount);        // v17 = i < v16
        u8 f2 = ctx->flags & (u8)0xFE;
        ctx->flags = (u8)(((u8)found) | f2);            // bit0 = found

        if (ctx->clipPlanes) {                          // free the old planes
            MirrorFree(ctx->clipPlanes);
            ctx->clipPlanes = nullptr;
        }
        if ((ctx->flags & 0x01) != 0) {                 // a valid surface poly
            // Mirror plane = ROTATE the bound poly's STORED normal by the camera
            // node's frame, then d = n . firstVertexPos. This is exactly the engine
            // sequence at 0x5f688a..0x5f68c8:
            //   RotateVectorWithFrame(node, dword_13FCD1C, ctx+24,
            //                         *(boundPoly+16)+44 /*stored normal*/);
            //   *(ctx+36) = n.x*v0.x + n.y*v0.y + n.z*v0.z;   (d = n . v0)
            const MirrorPoly* sp = ctx->boundPoly;
            g_rotate((const float*)node, sp->normal, ctx->planeNormal); // ctx+24..32
            const float* p0 = sp->v[0];                  // **(a2+16) — first vertex
            ctx->planeD = ctx->planeNormal[0] * p0[0]    // *(ctx+36) = n . v0
                        + ctx->planeNormal[1] * p0[1]
                        + ctx->planeNormal[2] * p0[2];
            // The engine builds the planes for this mirror surface (*(a2+12) ==
            // the reflective child key == the matched poly's surface key +20).
            ctx->clipPlanes = CreateClippingPlanes(mesh, sp->surfaceKey);
        }
    }

    if (ctx->clipPlanes) {
        if (reflectionPreparedGlobal)
            *reflectionPreparedGlobal = node;           // dword_649D6C = v20
        return node;                                    // return v20
    }
    return result;
}

} // namespace guild::render
