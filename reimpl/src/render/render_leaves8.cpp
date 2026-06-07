#include "render/render_leaves8.h"

#include <cstring>

namespace guild::render {

// ===========================================================================
// Byte-offset accessors. The original IDB had no named UDTs; every record is
// touched as *(type*)(base + off). We mirror that exactly with small helpers so
// the arithmetic stays visibly 1:1 with the disassembly.
// ===========================================================================
namespace {

inline u8*        BP(void* p)        { return static_cast<u8*>(p); }
inline const u8*  BP(const void* p)  { return static_cast<const u8*>(p); }

template <class T> inline T  Rd(const void* p, int off) { T v; std::memcpy(&v, BP(p) + off, sizeof(T)); return v; }
template <class T> inline void Wr(void* p, int off, T v) { std::memcpy(BP(p) + off, &v, sizeof(T)); }

inline void*  RdP(const void* p, int off) { return Rd<void*>(p, off); }
inline float  RdF(const void* p, int off) { return Rd<float>(p, off); }
inline i32    RdI(const void* p, int off) { return Rd<i32>(p, off); }

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults).
// ---------------------------------------------------------------------------
void* DefaultDeleteStockObject(void* obj)             { return obj; }
u8    DefaultRefreshAllObjects(u8 flag)               { return flag; }
u8    DefaultTraverseTree(void*, void*, void (*)(void*), i32) { return 0; }
void  DefaultBuildObjectCache(void*)                  {}
void  DefaultComputeBoneWorldMatrix(void*, void*, int, float* m) {
    // Identity 4x4 so the matrix->euler tail is observable in isolation.
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
void  DefaultMatrixToEuler(float*)                    {}
void  DefaultRemoveCacheEntry(void*, void*, void*)    {}
void  DefaultPointToBoneLocalSpace(void*, const float* ref, float* dstA, float* dstB) {
    if (ref) {
        if (dstA) std::memcpy(dstA, ref, 3 * sizeof(float));
        if (dstB) std::memcpy(dstB, ref, 3 * sizeof(float));
    }
}
u8    DefaultIlluminateObject(void*, void**)          { return 0; }
void* DefaultSelectLodFrame(void*)                    { return nullptr; }
void  DefaultAssignMeshData(void*)                    {}
void  DefaultPropagateDirtyFlag(void*, u32)           {}
void  DefaultSetPosition(void*, const float*)         {}
void  DefaultSetWorldTranslation(void*, const float*) {}
void  DefaultAccumulateAabbRecursive(float*, void*)   {}

RenderLeaves8Hooks g_hooks = {
    /*activeFrameObject*/        nullptr,
    /*deleteStockObject*/        &DefaultDeleteStockObject,
    /*refreshAllObjects*/        &DefaultRefreshAllObjects,
    /*traverseTree*/             &DefaultTraverseTree,
    /*buildObjectCache*/         &DefaultBuildObjectCache,
    /*computeBoneWorldMatrix*/   &DefaultComputeBoneWorldMatrix,
    /*matrixToEuler*/            &DefaultMatrixToEuler,
    /*removeCacheEntry*/         &DefaultRemoveCacheEntry,
    /*pointToBoneLocalSpace*/    &DefaultPointToBoneLocalSpace,
    /*illuminateObject*/         &DefaultIlluminateObject,
    /*selectLodFrame*/           &DefaultSelectLodFrame,
    /*assignMeshData*/           &DefaultAssignMeshData,
    /*propagateDirtyFlag*/       &DefaultPropagateDirtyFlag,
    /*setPosition*/              &DefaultSetPosition,
    /*setWorldTranslation*/      &DefaultSetWorldTranslation,
    /*accumulateAabbRecursive*/  &DefaultAccumulateAabbRecursive,
};

RenderLeaves8Hooks MakeDefaultHooks() { return g_hooks; }   // copy of the inert table

} // namespace

void InstallRenderLeaves8Hooks(const RenderLeaves8Hooks& h) { g_hooks = h; }
const RenderLeaves8Hooks& CurrentRenderLeaves8Hooks() { return g_hooks; }

RenderLeaves8Hooks DefaultRenderLeaves8Hooks() { return MakeDefaultHooks(); }

// ===========================================================================
// 0x4226bc — VIBE_Color_NotEqualRgb
//   return *a1 != *a2 || a1[1] != a2[1] || a1[2] != a2[2];
// ===========================================================================
int NotEqualRgb(const u8* a, const u8* b) {
    return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}

// ===========================================================================
// 0x4226e0 — VIBE_Color_SetRgb (__usercall result@eax, r@dl, g@cl, b@bl)
//   *result = r;  result[1] = b;  result[2] = g;  return result;
// NB: the original stores blue into [1] and green into [2] (verbatim ordering).
// ===========================================================================
u8* SetRgb(u8* dst, u8 r, u8 g, u8 b) {
    dst[0] = r;
    dst[1] = b;
    dst[2] = g;
    return dst;
}

// ===========================================================================
// 0x433694 — VIBE_Render_SetWindowRect
//   dword_75FB40 = eax;  dword_75FB44 = edx;  dword_75FB48 = ebx;  dword_75FB4C = ecx;
//   return eax;
// ===========================================================================
i32 SetWindowRect(WindowRect& out, i32 a1, i32 a2, i32 a3, i32 a4) {
    out.xMax = a1;   // dword_75FB40 <- eax
    out.yMax = a2;   // dword_75FB44 <- edx
    out.xMin = a4;   // dword_75FB48 <- ebx (a4)
    out.yMin = a3;   // dword_75FB4C <- ecx (a3)
    return a1;
}

// ===========================================================================
// 0x5dd9c8 — VIBE_Render_GetVertexBufferInfo
//   *a1 = dword_64A318;  *a2 = 2048;  return 0;
// ===========================================================================
int GetVertexBufferInfo(u32 vbHandle, u32* outHandle, u32* outCap) {
    if (outHandle) *outHandle = vbHandle;   // dword_64A318
    if (outCap)    *outCap    = 2048;
    return 0;
}

// ===========================================================================
// 0x5d3668 — VIBE_Mesh_ReleaseStockObject
//   if (result && (i32)result[119] > 0) return DeleteStockObject(result);
//   return result;
// result[119] is the dword at byte offset 476 (refcount).
// ===========================================================================
void* ReleaseStockObject(void* obj) {
    if (obj) {
        if (RdI(obj, 476) > 0)
            return g_hooks.deleteStockObject(obj);
    }
    return obj;
}

// ===========================================================================
// 0x43ea34 — VIBE_Light_RefreshAllToggle
//   RefreshAllObjects(*a1);  return 1;
// ===========================================================================
int RefreshAllToggle(const u8* flagPtr) {
    g_hooks.refreshAllObjects(*flagPtr);
    return 1;
}

// ===========================================================================
// 0x5c8538 — VIBE_Light_RequestObjectCache
//   if (a1) LOBYTE(a1) = TraverseTree(a1[130], a1, BuildObjectCache, 576);
//   return (char)a1;
// a1[130] is the dword at byte offset 520 (subtree root handle).
// ===========================================================================
u8 RequestObjectCache(void* root) {
    if (!root) return 0;
    return g_hooks.traverseTree(RdP(root, 520), root, g_hooks.buildObjectCache, 576);
}

// ===========================================================================
// 0x5c8a78 — VIBE_Light_AttachAtFrameMatrix
//   v1 = (*(i8*)(a1+528) >= 0) ? dword_13FCD1C : 0;
//   ComputeBoneWorldMatrix(a1, v1, 1);
//   MatrixToEuler(&v2);        ; v2 is the engine scratch matrix
// ===========================================================================
void AttachAtFrameMatrix(void* obj, float outMatrix[16]) {
    void* ref = (Rd<i8>(obj, 528) >= 0) ? g_hooks.activeFrameObject : nullptr;
    g_hooks.computeBoneWorldMatrix(obj, ref, 1, outMatrix);
    g_hooks.matrixToEuler(outMatrix);
}

// ===========================================================================
// 0x5c7cf8 — VIBE_Light_RecomputeForObject (eax=light, edx=obj, ecx=a3)
//   v7 = (u8)(4 * *(u8*)(obj+528)) >> 7;          ; = (byte528 >> 5) & 1
//   v6 = light;
//   RemoveCacheEntry(light, obj, a3, obj);
//   if (0.0 != light[37]) {                       ; light[+148] != 0 (range gate)
//     PointToBoneLocalSpace(light, dword_13FCD1C, light+118, light+19);
//     light[121] = light[36] * light[36];          ; [+484] = [+144]^2
//     IlluminateObject(obj, &v6);
//   }
//   return 1;
// (v7 is computed but unused by the body — kept for trace fidelity.)
// ===========================================================================
int RecomputeForObject(void* light, void* obj, void* a3) {
    void* v6 = light;
    (void)((u8)(4 * Rd<u8>(obj, 528)) >> 7);   // v7 — verbatim, unused
    g_hooks.removeCacheEntry(light, obj, a3);
    if (RdF(light, 148) != 0.0f) {
        float* ref = static_cast<float*>(g_hooks.activeFrameObject
                         ? (void*)BP(g_hooks.activeFrameObject) : nullptr);
        // light+118 (float index) = byte +472; light+19 = byte +76.
        g_hooks.pointToBoneLocalSpace(light, ref,
                                      reinterpret_cast<float*>(BP(light) + 472),
                                      reinterpret_cast<float*>(BP(light) + 76));
        float r = RdF(light, 144);             // light[36]
        Wr<float>(light, 484, r * r);          // light[121] = r*r
        g_hooks.illuminateObject(obj, &v6);
    }
    return 1;
}

// ===========================================================================
// 0x5c7e58 — VIBE_Light_PrepareObjectCache (eax=obj)
//   v6 = 0.0;
//   if (obj[+460] || (obj[+460] = SelectLodFrame(obj)) != 0) {
//     PointToBoneLocalSpace(obj, dword_13FCD1C, obj+472, obj+76);
//     v2 = *(obj[+460] + 16);                       ; mesh record
//     if (v2) v6 = (vtable +504)();                 ; bounding radius
//     v3 = *(i8*)(obj+528);
//     obj[+484] = v6 * v6;                           ; radius^2 cache
//     if (v3 & 4)  AssignMeshData(obj);
//     if (*(u8*)(obj+528) & 0x20) {
//       v4 = *(obj[+460] + 16);
//       if (v4) (vtable +492)();                     ; per-frame refresh
//     }
//   }
// The two vtable calls (+504 radius, +492 refresh) are mesh-record virtuals not
// reconstructed here; in isolation the radius defaults to 0 (so obj[+484] = 0).
// ===========================================================================
void PrepareObjectCache(void* obj) {
    float v6 = 0.0f;
    void* frame = RdP(obj, 460);
    if (!frame) {
        frame = g_hooks.selectLodFrame(obj);
        Wr<void*>(obj, 460, frame);
    }
    if (!frame) return;

    float* ref = static_cast<float*>(g_hooks.activeFrameObject
                     ? (void*)BP(g_hooks.activeFrameObject) : nullptr);
    g_hooks.pointToBoneLocalSpace(obj, ref,
                                  reinterpret_cast<float*>(BP(obj) + 472),
                                  reinterpret_cast<float*>(BP(obj) + 76));
    // v2 = *(frame + 16): the bound mesh record. Its vtable slot +504 returns the
    // bounding radius. Not reconstructed -> radius stays v6 = 0 in isolation.
    void* meshRec = RdP(frame, 16);
    (void)meshRec;   // radius query (vtable +504) — inert here
    i8 flags = Rd<i8>(obj, 528);
    Wr<float>(obj, 484, v6 * v6);
    if ((flags & 4) != 0)
        g_hooks.assignMeshData(obj);
    if ((Rd<u8>(obj, 528) & 0x20) != 0) {
        void* meshRec2 = RdP(frame, 16);
        (void)meshRec2;   // per-frame refresh (vtable +492) — inert here
    }
}

// ===========================================================================
// 0x4284f4 — VIBE_Mesh_ComputeWorldAabb (eax=outMin, ebx=obj, edx=outMax)
//   if (!obj[+0x1CC /*460*/]) return 0;            ; no bound mesh
//   save frame pose: pos = dword_13FCD1C[+76..84], wt = dword_13FCD1C[+132..140]
//   SetPositionXYZ(frame,0,0,0); SetWorldTranslationXYZ(frame,0,0,0);
//   AssignMeshData(obj); PropagateDirtyFlag(obj,1);
//   v = (float*)( [ [obj+460] ] + 80 * [ [obj+460] + 8 ] );   ; first corner
//   outMin[0..2] = outMax[0..2] = v[0..2];
//   for (i = 1; i < 8; ++i, v += 20) {             ; remaining 7 corners (80B stride)
//     outMin[k] = min(outMin[k], v[k]);  outMax[k] = max(outMax[k], v[k]);
//   }
//   for (c = obj[+0x1FC /*508*/]; c; c = c[+0x1F0 /*496*/])
//       AccumulateAabbRecursive(outMax, c);        ; child fold (edx box = outMax)
//   restore frame pose (SetPosition/SetWorldTranslation).
// The original folds the FIRST box (eax, outMin) min-only on [0..2] and the SECOND
// box (ecx/edx, outMax) max-only — faithful to the >=/<= compare senses.
// dword_13FCD1C is the active frame object (hook-supplied; null => poses skipped).
// ===========================================================================
bool ComputeWorldAabb(float outMin[3], void* obj, float outMax[3]) {
    void* mesh = RdP(obj, 460);   // obj[+0x1CC]
    if (!mesh) return false;

    void* frame = g_hooks.activeFrameObject;
    float savedPos[3] = {0, 0, 0};
    float savedWt[3]  = {0, 0, 0};
    if (frame) {
        savedPos[0] = RdF(frame, 76);  savedPos[1] = RdF(frame, 80);  savedPos[2] = RdF(frame, 84);
        savedWt[0]  = RdF(frame, 132); savedWt[1]  = RdF(frame, 136); savedWt[2]  = RdF(frame, 140);
        const float zero[3] = {0, 0, 0};
        g_hooks.setPosition(frame, zero);
        g_hooks.setWorldTranslation(frame, zero);
    }
    g_hooks.assignMeshData(obj);
    g_hooks.propagateDirtyFlag(obj, 1u);

    // first corner: base = [[mesh]] + 80 * [[mesh]+8]
    void* cornerBase = RdP(mesh, 0);
    i32   startIdx   = RdI(mesh, 8);
    const u8* v = BP(cornerBase) + 80 * startIdx;

    float x0 = RdF(v, 0), y0 = RdF(v, 4), z0 = RdF(v, 8);
    outMin[0] = x0; outMin[1] = y0; outMin[2] = z0;
    outMax[0] = x0; outMax[1] = y0; outMax[2] = z0;

    v += 80;
    for (int i = 1; i < 8; ++i, v += 80) {
        float x = RdF(v, 0), y = RdF(v, 4), z = RdF(v, 8);
        if (outMin[0] >= x) outMin[0] = x;
        if (outMin[1] >= y) outMin[1] = y;
        if (outMin[2] >= z) outMin[2] = z;
        if (outMax[0] <= x) outMax[0] = x;
        if (outMax[1] <= y) outMax[1] = y;
        if (outMax[2] <= z) outMax[2] = z;
    }

    // child fold: AccumulateAabbRecursive(box, child) over obj's child list.
    for (void* c = RdP(obj, 508); c; c = RdP(c, 496))
        g_hooks.accumulateAabbRecursive(outMax, c);

    if (frame) {
        g_hooks.setPosition(frame, savedPos);
        g_hooks.setWorldTranslation(frame, savedWt);
    }
    return true;
}

// ===========================================================================
// 0x5b2a58 — VIBE_Mesh_ComputeObjectAabb (eax=outMin, edx=outMax)
//   save frame pose, zero it (SetPosition/SetWorldTranslation with {0,0,0});
//   TraverseTree(off_649D64, 0, 0, 64);            ; pre-pass (refresh transforms)
//   box[0..2] = +1e10 (min seed);  box[3..5] = -1e10 (max seed);
//   WalkAndInvoke(off_649D64, 0, AccumulateVertexAabb, 64, &box);
//   restore frame pose;
//   outMin[0..2] = box[0..2];  outMax[0..2] = box[3..5];  return box[5];
// The scene walk + per-object accumulation is driven through traverseTree (the
// engine's WalkAndInvoke); in isolation the default walk is empty, so the result
// is the sentinel seeds. (The pre-pass TraverseTree call has a null callback.)
// ===========================================================================
i32 ComputeObjectAabb(float outMin[3], float outMax[3]) {
    void* frame = g_hooks.activeFrameObject;
    float savedPos[3] = {0, 0, 0};
    float savedWt[3]  = {0, 0, 0};
    if (frame) {
        savedPos[0] = RdF(frame, 76);  savedPos[1] = RdF(frame, 80);  savedPos[2] = RdF(frame, 84);
        savedWt[0]  = RdF(frame, 132); savedWt[1]  = RdF(frame, 136); savedWt[2]  = RdF(frame, 140);
        const float zero[3] = {0, 0, 0};
        g_hooks.setPosition(frame, zero);
        g_hooks.setWorldTranslation(frame, zero);
    }

    // Pre-pass walk with a null per-node callback (mask 64).
    g_hooks.traverseTree(nullptr, nullptr, nullptr, 64);

    float box[6];
    box[0] = box[1] = box[2] = kAabbSentinelMax;   // 1343554297 = +1e10
    box[3] = box[4] = box[5] = kAabbSentinelMin;    // -803929351 = -1e10
    // Accumulation walk (AccumulateVertexAabb over &box) — driven by the engine's
    // WalkAndInvoke; modelled through traverseTree (inert default leaves seeds).
    g_hooks.traverseTree(nullptr, nullptr, nullptr, 64);

    if (frame) {
        g_hooks.setPosition(frame, savedPos);
        g_hooks.setWorldTranslation(frame, savedWt);
    }

    outMin[0] = box[0]; outMin[1] = box[1]; outMin[2] = box[2];
    outMax[0] = box[3]; outMax[1] = box[4]; outMax[2] = box[5];

    i32 r;
    std::memcpy(&r, &box[5], sizeof(r));
    return r;
}

} // namespace guild::render
