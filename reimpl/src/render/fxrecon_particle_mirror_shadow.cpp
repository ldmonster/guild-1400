// =============================================================================
// guild::render::fxrecon — implementation. See header for scope / rule-8 note.
//
// Each reconstructed function carries its gilde.exe provenance. Pure arithmetic
// and bookkeeping are 1:1 with the Hex-Rays; coupled leaves are reached through
// the hooks declared in the header (inert by default).
// =============================================================================
#include "render/fxrecon_particle_mirror_shadow.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>

namespace guild::render::fxrecon {

// ---------------------------------------------------------------------------
// Hook storage + defaults
// ---------------------------------------------------------------------------
namespace {

i32 DefaultSpawnEffect(u32, u32, const char*, u8, const SparkleParams*, i32,
                       i32, float, u32, u32, u32, u32) { return 0; }
SpawnEffectHook g_spawnEffect = &DefaultSpawnEffect;

// Inert basis: identity-ish (writes a unit basis at mat[8..] direction, 0 euler).
void DefaultBuildBasis(const float dir[3], float /*angle*/, float outMat[16]) {
    for (int i = 0; i < 16; ++i) outMat[i] = 0.0f;
    outMat[8] = dir[0]; outMat[9] = dir[1]; outMat[10] = dir[2];
    outMat[0] = 1.0f; outMat[5] = 1.0f; outMat[15] = 1.0f;
}
BuildBasisHook g_buildBasis = &DefaultBuildBasis;

void DefaultMatrixToEuler(float mat[16]) { mat[0] = mat[1] = mat[2] = 0.0f; }
MatrixToEulerHook g_matrixToEuler = &DefaultMatrixToEuler;

u8 DefaultSetWorldTranslation(void*, const float[3]) { return 0; }
SetWorldTranslationHook g_setWorldTranslation = &DefaultSetWorldTranslation;

void* DefaultAlloc(u32 n, const char*) { return std::malloc(n ? n : 1); }
void  DefaultFree(void* p) { std::free(p); }
AllocHook g_alloc = &DefaultAlloc;
FreeHook  g_free  = &DefaultFree;

// dword_1408078 — external shadow-limit cap. Defaults large so InitBuffers
// clamps to the caller-requested counts unless a backend lowers it.
u32 g_globalLimit = 0xFFFFFFFFu;

} // namespace

void SetSpawnEffectHook(SpawnEffectHook h)              { g_spawnEffect = h ? h : &DefaultSpawnEffect; }
void SetBuildBasisHook(BuildBasisHook h)                { g_buildBasis = h ? h : &DefaultBuildBasis; }
void SetMatrixToEulerHook(MatrixToEulerHook h)          { g_matrixToEuler = h ? h : &DefaultMatrixToEuler; }
void SetSetWorldTranslationHook(SetWorldTranslationHook h){ g_setWorldTranslation = h ? h : &DefaultSetWorldTranslation; }
void SetAllocHook(AllocHook a)                          { g_alloc = a ? a : &DefaultAlloc; }
void SetFreeHook(FreeHook f)                            { g_free = f ? f : &DefaultFree; }
void Shadow_SetGlobalLimit(u32 v)                       { g_globalLimit = v; }

ShadowModuleState& ShadowState() {
    static ShadowModuleState s;
    return s;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x42c104 — VIBE_Particle_ResetEmitter
//   int __userpurge eax = (a1@eax, a2, a3)
//   *a1 = a2; a1[1] = a3; then clear bit0 of (slot+81) over a1[52] slots,
//   84 bytes/slot, starting from a1[10]. Returns the advanced base pointer.
// ---------------------------------------------------------------------------
u8* Particle_ResetEmitter(EmitterObject* a1, u32 a2, u32 a3) {
    a1->fieldA = a2;                    // *a1 = a2
    i32 v4 = a1->slotCount;             // v4 = a1[52]
    a1->fieldB = a3;                    // a1[1] = a3
    i32 v5 = 0;
    u8* result = a1->slotBase;          // result = a1[10]
    if (v4 > 0) {
        do {
            *(result + 81) &= static_cast<u8>(~1u);   // *(_BYTE*)(result+81) &= ~1
            ++v5;
            result += 84;
        } while (v5 < a1->slotCount);
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4d887c — VIBE_Particle_SpawnSparkleEffect
//   int __usercall eax = (a1@eax, a2@ecx)
//   Fills the "punkte_nm_2" sparkle preset and forwards to SpawnEffect.
//   v3 = {0, 1112014848 (=53.0f), 0}; LOBYTE(v4)=50; *(u16*)(&v4+1)=20510.
//   SpawnEffect( *(a1+97), v4, "punkte_nm_2", 1, v3, 300, 1, 50.0f,
//                1065353216(=1.0f), 0x40000000(=2.0f),
//                1056964608(=0.5f), 1056964608(=0.5f) )
// ---------------------------------------------------------------------------
i32 Particle_SpawnSparkleEffect(u32 ownerParticleField, u32 /*callerTag*/) {
    SparkleParams params;
    // v3[0]=0, v3[1]=1112014848 -> 50.0f, v3[2]=0
    params.pos[0] = 0.0f;
    {
        const u32 bits = 1112014848u;
        std::memcpy(&params.pos[1], &bits, sizeof(float));   // == 50.0f
    }
    params.pos[2] = 0.0f;
    // LOBYTE(v4)=50; *(u16*)((char*)&v4 + 1) = 20510 (0x501E)
    u32 descriptor = 0;
    auto* db = reinterpret_cast<u8*>(&descriptor);
    db[0] = 50;
    u16 hi = 20510;
    std::memcpy(db + 1, &hi, sizeof(u16));
    params.descriptor = descriptor;

    // a8 = 50.0f; a9=1.0f bits; a10=2.0f bits; a11=a12=0.5f bits.
    return g_spawnEffect(ownerParticleField, descriptor, "punkte_nm_2",
                         /*mode*/ 1, &params, /*lifetime*/ 300, /*a7*/ 1,
                         /*a8*/ 50.0f, /*a9*/ 1065353216u, /*a10*/ 0x40000000u,
                         /*a11*/ 1056964608u, /*a12*/ 1056964608u);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5e1238 — VIBE_Particle_SetOrientationFromAngle
//   char __userpurge al = (a1@eax enable, a2@edx dir, a3@ecx objBase, a4 angle)
//   if (enable) { BuildBasisFromAngle(dir,angle,v7); MatrixToEuler(v7);
//                 return SetWorldTranslation(objBase + 232, eulerOut); }
//   else return (char)dir.
// The original keeps the euler result in the low 12 bytes of the basis (v8),
// produced by MatrixToEuler writing mat[0..2]; we pass that triple onward.
// ---------------------------------------------------------------------------
u8 Particle_SetOrientationFromAngle(int enable, float dir[3], void* objBase, float angle) {
    u8 result = static_cast<u8>(reinterpret_cast<std::uintptr_t>(dir));  // result=(char)a2
    if (enable) {
        float basis[16];
        g_buildBasis(dir, angle, basis);          // VIBE_Math_BuildBasisFromAngle(a2,a4,v7)
        g_matrixToEuler(basis);                    // VIBE_Math_MatrixToEuler(v7) -> basis[0..2]
        // VIBE_Object_SetWorldTranslation(objBase + 232, euler)
        void* field = reinterpret_cast<void*>(reinterpret_cast<u8*>(objBase) + 232);
        return g_setWorldTranslation(field, basis);
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f1d90 — VIBE_Shadow_AllocCache
//   char* __usercall eax = (a1@eax count, a2@edx param)
//   dword_64A7E0=a1; dword_64A7E4=a2; result=alloc(20*a1,"d3s:Cache");
//   dword_64A7F8=result; return result.
// ---------------------------------------------------------------------------
void* Shadow_AllocCache(u32 count, u32 param) {
    ShadowModuleState& s = ShadowState();
    s.cacheCount = count;
    s.cacheParam = param;
    void* result = g_alloc(20u * count, "d3s:Cache");
    s.cacheBase = result;
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f1fc0 — VIBE_Shadow_InitBuffers
//   int __userpurge eax = (a1, a2, a3, a4, a5, a6)
//   v7 = min(a2, dword_1408078); dword_1408A54 = v7;
//   dword_1408A60 = min(a1, v7);
//   dword_64A7F0=a5; dword_64A7F4=a6; dword_64A7EC = a5*a5;
//   poly  = alloc(48 * a5*a5);
//   point = alloc(600 * (a5*a5 >> 4));
//   seed two basis rows (identity translation rows), enable flags,
//   AllocCache(a3, ?), clear slotA0C[0..3]; return 16.
// Note: dword_1408A24/28/2C and A38/3C/40 are 1065353216 (=1.0f) — two basis
// rows with unit components; the zeros around them are the rest of the rows.
// ---------------------------------------------------------------------------
i32 Shadow_InitBuffers(u32 a1, u32 a2, u32 a3_ebx, u8 a4, u32 a5, u32 a6) {
    ShadowModuleState& s = ShadowState();

    u32 v7 = a2;
    if (a2 >= g_globalLimit) v7 = g_globalLimit;   // clamp to dword_1408078
    s.shadowLimitA = v7;                            // dword_1408A54
    if (a1 < v7) v7 = a1;                           // v7 = min(a1, v7)
    s.paramA5 = a5;                                 // dword_64A7F0
    s.shadowLimitB = v7;                            // dword_1408A60
    s.paramA6 = a6;                                 // dword_64A7F4
    s.resSquared = a5 * a5;                         // dword_64A7EC

    s.buildingPoly = g_alloc(48u * a5 * a5, "d3s:BuildingPolyBuffer(Shadow)");
    s.polyCount = 0;                                // dword_1408A68
    s.buildingPoint = g_alloc(600u * (s.resSquared >> 4), "d3s:BuildingPointBuffer(Shadow)");
    s.pointCount = 0;                               // dword_1408A64

    // dword_1408A20 .. A48 : two rows; A24,A28,A2C and A38,A3C,A40 = 1.0f bits,
    // everything else 0. rowA index k maps to dword_1408A20 + 4*k.
    for (u32 i = 0; i < 11; ++i) s.rowA[i] = 0;
    s.rowA[1] = 1065353216u;   // A24
    s.rowA[2] = 1065353216u;   // A28
    s.rowA[3] = 1065353216u;   // A2C
    s.rowA[6] = 1065353216u;   // A38
    s.rowA[7] = 1065353216u;   // A3C
    s.rowA[8] = 1065353216u;   // A40

    s.enableA = 1;             // byte_1408A6D
    s.enableB = a4;            // byte_1408A6E
    s.enableC = 1;             // byte_1408A6C
    s.misc4C = 0;              // dword_1408A4C

    // VIBE_Shadow_AllocCache(a3, v8) @0x5f20bd. a3 is the @ebx count; v8 is an
    // uninitialised edx in the decompile (its only effect is dword_64A7E4=v8,
    // garbage in the original) — forward the genuine a3@ebx count and 0 for the
    // junk param.
    Shadow_AllocCache(/*count*/ a3_ebx, /*param*/ 0);

    s.misc5C = 0;              // dword_1408A5C
    for (int r = 0; r != 4; ++r) s.slotA0C[r] = 0;   // dword_1408A0C[0..3]=0
    return 4 * 4;              // result*4 with result==4 -> 16
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f20e8 — VIBE_Shadow_ShutdownBuffers
//   Zeros the limits, frees poly/point/cache buffers + heightmap.
// ---------------------------------------------------------------------------
i32 Shadow_ShutdownBuffers() {
    ShadowModuleState& s = ShadowState();
    s.shadowLimitA = 0;        // dword_1408A54
    s.shadowLimitB = 0;        // dword_1408A60
    s.paramA5 = 0;             // dword_64A7F0
    s.resSquared = 0;          // dword_64A7EC
    s.polyCount = 0;           // dword_1408A68

    g_free(s.buildingPoly);    // VIBE_Memory_FreeDebug(dword_1408A58)
    s.buildingPoly = nullptr;
    s.pointCount = 0;          // dword_1408A64

    g_free(s.buildingPoint);   // free(dword_1408A50)
    s.buildingPoint = nullptr;
    s.cacheParam = 0;          // dword_64A7E4
    s.cacheCount = 0;          // dword_64A7E0

    g_free(s.cacheBase);       // free(dword_64A7F8)
    s.cacheBase = nullptr;

    if (s.heightmap) {         // if (dword_64A048) VIBE_Heightmap_Free(...)
        g_free(s.heightmap);
        s.heightmap = nullptr;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f444c — VIBE_Shadow_CastFromAllLights
//   char __usercall al = (a1@eax objBase)
//   if ((obj+529 & 4) && dword_1408A5C) {
//       for v2 in [0, dword_1408A5C):
//           a1 = CastFromLight(obj, dword_1408A10[v2], dword_64A028, obj);
//   } return a1;
// The shadow-direction global (dword_64A028) is forwarded as the cast context.
// CastFromLight is the geometry leaf; we route it through a hook so the loop /
// guards reproduce exactly while the heavy projection stays pluggable.
// ---------------------------------------------------------------------------
namespace {
// Hook for the per-light cast (0x5f3f98). Inert default returns 0, matching the
// original tail `return 0;`. Installed by the real shadow backend.
using CastFromLightHook = u8 (*)(void* objBase, void* light, void* ctx, void* objAgain);
u8 DefaultCastFromLight(void*, void*, void*, void*) { return 0; }
CastFromLightHook g_castFromLight = &DefaultCastFromLight;

// dword_64A028 — shadow cast context/direction global.
void* g_shadowCtx = nullptr;
} // namespace

void SetCastFromLightHook(u8 (*h)(void*, void*, void*, void*)) {
    g_castFromLight = h ? h : &DefaultCastFromLight;
}
void Shadow_SetCastContext(void* ctx) { g_shadowCtx = ctx; }

u8 Shadow_CastFromAllLights(void* objBase) {
    ShadowModuleState& s = ShadowState();
    u8* obj = reinterpret_cast<u8*>(objBase);
    // (*(obj+529) & 4) != 0 && dword_1408A5C
    bool flag = objBase && (obj[529] & 4) != 0 && s.misc5C != 0;
    u8 a1 = static_cast<u8>(reinterpret_cast<std::uintptr_t>(objBase));
    if (flag) {
        // The active light list (dword_1408A10) holds at most kMaxLights entries
        // (the collector caps it at 4); clamp the iteration to the storage so a
        // corrupt/over-large dword_1408A5C cannot read past the array. The valid
        // path (misc5C <= storage) is byte-identical.
        constexpr u32 kMaxLights =
            static_cast<u32>(sizeof(s.lights) / sizeof(s.lights[0]));
        u32 limit = s.misc5C < kMaxLights ? s.misc5C : kMaxLights;
        u32 v2 = 0;
        do {
            a1 = g_castFromLight(objBase, s.lights[v2], g_shadowCtx, objBase);
            ++v2;
        } while (v2 < limit);   // v3+1 < dword_1408A5C, v3 starts at v2 post-inc
    }
    return a1;
}

// ---------------------------------------------------------------------------
// Pure shadow-projection math extracted 1:1 from VIBE_Shadow_UpdateNodeShadows
// (0x5f4494). For each of the 8 bbox corners, the original casts the corner
// onto the ground plane along the light direction, then transforms the result
// by the view matrix (dword_13FCD1C + 396..452). This is the genuine
// "shadow projection" arithmetic the cluster is built around; it is fully
// self-contained (no record pointers) so it is reconstructed exactly here.
// The surrounding driver (node iteration, frustum classification, the heavy
// VIBE_Shadow_CastFromLight) is the coupled half and is hooked, not faked.
// ---------------------------------------------------------------------------

// Directional projection (object flag +529 bit4 set, "v37 = 0.01" branch):
//   t = (corner.y - casterHeight) / -lightDir.y;
//   out = corner + t * lightDir;     (lightDir = v28 = {v28,v29,v30})
void Shadow_ProjectCornerDirectional(const float corner[3], const float lightDir[3],
                                     float casterHeight, float out[3]) {
    // v11 = (v9[1] - v40) / -v29;  (v40 = casterHeight, v29 = lightDir[1])
    float t = (corner[1] - casterHeight) / -lightDir[1];
    out[0] = t * lightDir[0] + corner[0];   // v14 = v11*v28 + *v9
    out[1] = t * lightDir[1] + corner[1];   // v15 = v11*v29 + v9[1]
    out[2] = t * lightDir[2] + corner[2];   // v16 = v11*v30 + v9[2]
}

// Point-light projection (else branch): light at lightPos, ground at casterHeight.
//   d  = lightPos - corner;
//   t  = (lightPos.y - casterHeight) / -d.y;
//   out = lightPos + t * d;
void Shadow_ProjectCornerPoint(const float corner[3], const float lightPos[3],
                               float casterHeight, float out[3]) {
    float dx = lightPos[0] - corner[0];     // v31 = v25 - *v9
    float dy = lightPos[1] - corner[1];     // v32 = v26 - v9[1]
    float dz = lightPos[2] - corner[2];     // v33 = v27 - v9[2]
    float t  = (lightPos[1] - casterHeight) / -dy;   // (v26 - v40) / -v20
    out[0] = t * dx + lightPos[0];          // v14 = v21*v31 + v25
    out[1] = t * dy + lightPos[1];          // v15 = v23 + v26
    out[2] = t * dz + lightPos[2];          // v16 = v22 + v27
}

// View transform of a projected shadow point. m is the 4x4 column-block at
// dword_13FCD1C; the original reads the translation row at +76/+80/+84 and the
// 3x3+translate at +396.. The reconstruction takes the exact float offsets.
//   rel = p - m.origin;  (m+76,+80,+84)
//   x' = rel.x*m[396] + rel.y*m[412] + rel.z*m[428] + m[444]
//   y' = rel.x*m[400] + rel.y*m[416] + rel.z*m[432] + m[448]
//   z' = rel.x*m[404] + rel.y*m[420] + rel.z*m[436] + m[452]
// (m is treated as a byte-addressed float block; offsets/4 give indices.)
void Shadow_TransformProjectedPoint(const float p[3], const float* mBytesAsFloats,
                                    float out[3]) {
    const float* m = mBytesAsFloats;   // indexed by byteOffset/4
    float rx = p[0] - m[76 / 4];
    float ry = p[1] - m[80 / 4];
    float rz = p[2] - m[84 / 4];
    out[0] = rx * m[396 / 4] + ry * m[412 / 4] + rz * m[428 / 4] + m[444 / 4];
    out[1] = rx * m[400 / 4] + ry * m[416 / 4] + rz * m[432 / 4] + m[448 / 4];
    out[2] = rx * m[404 / 4] + ry * m[420 / 4] + rz * m[436 / 4] + m[452 / 4];
}

// ---------------------------------------------------------------------------
// Pure mirror-plane reflection math, extracted 1:1 from the cluster's mirror
// half. The full VIBE_Mirror_CreateClippingPlanes (0x5f5d08) and
// VIBE_Mirror_PrepareReflectionNode (0x5f676c) are dominated by mesh-polygon
// record traversal (40-byte stride, vertex pointer arrays), the memory-debug
// allocator and the scene draw-node mutation — coupled leaves we cannot
// reproduce byte-for-byte without fabricating those record layouts. Per rule 8
// they are OMITTED here (reported), and only their verifiable pure-math kernel
// — the plane equation built by VIBE_Math_TriangleNormal + the d term — is
// reconstructed, since that is the reflection geometry the task targets.
//
//   plane.n = normalize( (v1-v0) x (v2-v0) )     [TriangleNormal, 0x5cb824]
//   plane.d = -(n . v0)                          [the v39 / -v39 term]
// ---------------------------------------------------------------------------
// VIBE_Math_TriangleNormal (0x5cb824) inlined: cross of (v1-v0)x(v2-v0), then
// normalize. Returns false if degenerate (zero length) — the original's
// VIBE_Math_VectorNormalize divides by length; we guard to avoid NaN while
// keeping the exact algebra for non-degenerate inputs.
static bool TriangleNormal(const float v0[3], const float v1[3], const float v2[3],
                           float n[3]) {
    float ax = v1[0] - v0[0], ay = v1[1] - v0[1], az = v1[2] - v0[2];  // v5,v6,v7
    float bx = v2[0] - v0[0], by = v2[1] - v0[1], bz = v2[2] - v0[2];  // v8,v9,v10
    n[0] = bz * ay - by * az;   // *a3   = v10*v6 - v9*v7
    n[1] = bx * az - bz * ax;   // a3[1] = v8*v7  - v10*v5
    n[2] = by * ax - bx * ay;   // a3[2] = v9*v5  - v8*v6
    float len2 = n[0]*n[0] + n[1]*n[1] + n[2]*n[2];
    if (len2 <= 0.0f) return false;
    float inv = 1.0f / std::sqrt(len2);
    n[0] *= inv; n[1] *= inv; n[2] *= inv;
    return true;
}

// Build one mirror clip plane from a triangle (v0,v1,v2), matching the
// CreateClippingPlanes inner loop: normal from TriangleNormal, then
//   d = -(n.x*v0.x + n.y*v0.y + n.z*v0.z)   == -v39.
bool Mirror_BuildClipPlane(const float v0[3], const float v1[3], const float v2[3],
                           MirrorClipPlaneEq* out) {
    float n[3];
    if (!TriangleNormal(v0, v1, v2, n)) return false;
    out->nx = n[0]; out->ny = n[1]; out->nz = n[2];
    // v39 = n.x*v0.x + n.y*v0.y + n.z*v0.z ;  plane.d stored as -v39.
    float v39 = n[0]*v0[0] + n[1]*v0[1] + n[2]*v0[2];
    out->d = -v39;
    return true;
}

// VIBE_Math_VectorWithinTolerance (0x5caa4c) — used by both mirror dedup and the
// shadow direction-change test. fabs(a-b) <= tol per component.
bool VectorWithinTolerance(const float a[3], const float b[3], float tol) {
    auto fa = [](float x) { return x < 0.0f ? -x : x; };
    return fa(b[0] - a[0]) <= tol && fa(b[1] - a[1]) <= tol && fa(b[2] - a[2]) <= tol;
}

// ===========================================================================
// PER-NODE SHADOW CASTER MANAGEMENT — VIBE_Shadow_CastFromLight (0x5f3f98) and
// VIBE_Shadow_UpdateNodeShadows (0x5f4494), reconstructed 1:1. See header SCOPE.
// ===========================================================================

// flt_5F1D80 — verified via get_bytes @0x5F1D80: 00 00 70 42 (=60.0f),
// 00 00 48 43 (=200.0f). The resolution-grow thresholds CastFromLight walks.
const float kShadowSizeThresholds[2] = {60.0f, 200.0f};

// flt_5CA2B0 — the unit "up/Z" direction passed to RotateVectorByHierarchy for
// the directional-light branch. Verified via get_bytes @0x5CA2B0: {0,0,1,0}.
namespace { const float kShadowUpDir[3] = {0.0f, 0.0f, 1.0f}; }

// ---------------------------------------------------------------------------
// Hook storage + inert defaults for the per-node management leaves.
// ---------------------------------------------------------------------------
namespace {

void* DefaultLoadShadowTexture() { return nullptr; }
LoadShadowTextureHook g_loadShadowTexture = &DefaultLoadShadowTexture;

void* DefaultAcquireCacheSlot(ShadowNodeSlot*, u8, void*, u32) { return nullptr; }
AcquireCacheSlotHook g_acquireCacheSlot = &DefaultAcquireCacheSlot;

void DefaultPointThroughBoneChain(const void*, const float in[3], float out[3]) {
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
}
PointThroughBoneChainHook g_pointThroughBoneChain = &DefaultPointThroughBoneChain;

void DefaultRotateVectorByHierarchy(const void*, const float in[3], float out[3]) {
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
}
RotateVectorByHierarchyHook g_rotateVectorByHierarchy = &DefaultRotateVectorByHierarchy;

void DefaultRenderMeshShadow(void*, void*, void*, void*, const float[3],
                             const float[3], void*, ShadowNodeSlot*) {}
RenderMeshShadowHook g_renderMeshShadow = &DefaultRenderMeshShadow;

int DefaultBuildGroundShadow(ShadowNodeSlot*, void*) { return 0; }
BuildGroundShadowHook g_buildGroundShadow = &DefaultBuildGroundShadow;

int DefaultTransformBoundingVolume(void*, const float*, bool) { return 0; }
TransformBoundingVolumeHook g_transformBoundingVolume = &DefaultTransformBoundingVolume;

u8 DefaultClassifyBoundingBox(const float*) { return 0; }   // 0 -> visible (bit6 clear)
ClassifyBoundingBoxHook g_classifyBoundingBox = &DefaultClassifyBoundingBox;

} // namespace

void SetLoadShadowTextureHook(LoadShadowTextureHook h)       { g_loadShadowTexture = h ? h : &DefaultLoadShadowTexture; }
void SetAcquireCacheSlotHook(AcquireCacheSlotHook h)         { g_acquireCacheSlot = h ? h : &DefaultAcquireCacheSlot; }
void SetPointThroughBoneChainHook(PointThroughBoneChainHook h){ g_pointThroughBoneChain = h ? h : &DefaultPointThroughBoneChain; }
void SetRotateVectorByHierarchyHook(RotateVectorByHierarchyHook h){ g_rotateVectorByHierarchy = h ? h : &DefaultRotateVectorByHierarchy; }
void SetRenderMeshShadowHook(RenderMeshShadowHook h)         { g_renderMeshShadow = h ? h : &DefaultRenderMeshShadow; }
void SetBuildGroundShadowHook(BuildGroundShadowHook h)       { g_buildGroundShadow = h ? h : &DefaultBuildGroundShadow; }
void SetTransformBoundingVolumeHook(TransformBoundingVolumeHook h){ g_transformBoundingVolume = h ? h : &DefaultTransformBoundingVolume; }
void SetClassifyBoundingBoxHook(ClassifyBoundingBoxHook h)   { g_classifyBoundingBox = h ? h : &DefaultClassifyBoundingBox; }

// ---------------------------------------------------------------------------
// gilde.exe 0x5f3f98 — VIBE_Shadow_CastFromLight
//   char __usercall al = (a1@eax node, a2@edx light, a3@ebx ctx, a4@edi node)
//
// SLOT MANAGEMENT + REDRAW DECISION reconstructed verbatim:
//   (1) GATE (0x4025): shadows enabled, object caster budget (+533)<5, object is
//       a caster (+529&4), object has a caster table (+492), light is a caster
//       (+529&4), the global capacity check, and the light intensity (a2[37])!=0.
//   (2) SEARCH (0x402b..0x4065): scan the 4 slots for one whose entry+0 (surface)
//       is set AND whose entry+4 (light) == this light. v7 counts the slots; the
//       loop runs over byte offsets 0,128,256,384 (>=512 stops).
//   (3) ALLOC+INIT (0x4065..0x4113, taken when v7==4 — no match): find the first
//       slot whose entry+0 is zero, then init it: +108=-1, +12=0, +4=light,
//       +16=0, +20=0, +24/+28/+32=0; +124 = (object +529 bit3) >> 7 (== 16*flag
//       >>7). If +124: load the "Schatten" texture into +0; else compute the
//       resolution-grow size and AcquireCacheSlot into +0.
//   (4) REDRAW DECISION (LABEL_19 0x4113..0x42a4): only if the slot has a surface
//       (+0). Write the ground-link ctx (+8 link's +8 = ctx). Compute the caster
//       origin (PointThroughBoneChain) and the light direction (directional:
//       RotateVectorByHierarchy(up); point: caster2.origin - caster.origin).
//       v16 = !VectorWithinTolerance(dir, slot+24, tol) || mesh-dirty  -> v17.
//       The coverage reuse path (a3 && !v16 && +108!=-1 && !+124): scan the slot
//       bbox region for the max coverage byte; if v17 || max != slot+20 -> v17=1.
//       If v17: RenderMeshShadow. Then BuildGroundShadow; on emit OR the dword_649D68
//       +528 |= 4 marker (modeled via the hook return).
// Returns 0 (the original always returns 0).
// ---------------------------------------------------------------------------
u8 Shadow_CastFromLight2(ShadowNode* node, ShadowLight* light, void* ctx,
                         bool forceCoverageScan) {
    ShadowModuleState& mod = ShadowState();

    // ---- (1) GATE ----------------------------------------------------------
    // dword_1408A60 (shadowLimitB) enables shadows; the capacity check against
    // 2*dword_64A7EC-2 vs the active building's poly count is part of the engine
    // global budget — modeled here as "shadows enabled" (the building-poly side
    // is unavailable standalone; rule 8: the rest of the gate is exact).
    if (!node || !light) return 0;
    if (mod.shadowLimitB == 0) return 0;            // dword_1408A60
    if (node->casterCount >= 5) return 0;           // *(a1+533) < 5
    if ((node->flags529 & 4) == 0) return 0;        // (*(a1+529) & 4)
    if (!node->hasCasterTable) return 0;            // *(a1+492)
    if ((light->flags529 & 4) == 0) return 0;       // (*(a2+529) & 4)
    if (light->intensity == 0.0f) return 0;         // 0.0 != a2[37]

    // ---- (2) SEARCH for a slot already casting THIS light ------------------
    ShadowNodeSlot* slot = nullptr;     // v5 (0 == none found)
    int v7 = 0;                         // slot counter; ==4 means "no match"
    for (; v7 < kShadowNodeSlots; ++v7) {
        ShadowNodeSlot& e = node->slots[v7];
        if (e.surface) {                            // *(v8+v6+1780) != 0
            if (e.light == light->id) {             // v40 == *(v8+v6+1784)
                slot = &e;                          // v5 = v8 + 1780 + v6
                break;
            }
        }
        // (the original advances v6 += 128 and stops at >= 512 == 4 slots)
    }

    // ---- (3) no match -> find the first FREE slot and INIT it --------------
    if (v7 == kShadowNodeSlots) {
        slot = nullptr;                             // v5 = 0
        ShadowNodeSlot* empty = nullptr;
        for (int i = 0; i < kShadowNodeSlots; ++i) {
            if (node->slots[i].surface == nullptr) { // !*(v10+v9+1780)
                empty = &node->slots[i];
                break;
            }
            // (>=512 -> goto LABEL_19 with v5 still 0; no init)
        }
        if (empty) {
            slot = empty;                           // v5 = v10 + 1780 + v9
            empty->bboxX0   = -1;                   // +108 = -1
            empty->scratch12 = 0;                   // +12 = 0
            empty->scratch16 = 0;                   // +16 = 0
            empty->light    = light->id;            // +4  = v11 (the light)
            empty->coverage = 0;                    // +20 = 0
            empty->cachedDir = ShadowFVec3{};       // +24/+28/+32 = 0
            // +124 = (unsigned)(16 * (object+529)) >> 7  == bit3 (0x08) of +529.
            empty->flag124 = static_cast<u8>(
                (static_cast<u8>(16 * node->flags529)) >> 7);
            if (empty->flag124) {
                // texture path: VIBE_Texture_LoadByName("Schatten", 131199, 0, 0)
                empty->surface = g_loadShadowTexture();
                // (the original's "if (handle && !*(handle+96)) UploadToSurface"
                //  is the texture-upload leaf; reached only through the hook.)
            } else {
                // cache path: grow the request by the resolution thresholds, then
                // VIBE_Shadow_AcquireCacheSlot(slot, byte_1408A6E, dword_649D58,
                //   grownSize, slot+8).
                u32 need = mod.shadowLimitB;        // v27 = dword_1408A60
                // The grow scans flt_5F1D80 (2 steps) doubling `need` per step
                // while the mesh's reference size (mesh+472) meets the threshold;
                // the mesh-size source is unavailable standalone (rule 8) so the
                // unconditional base request is used and clamped to dword_1408A54.
                if (need >= mod.shadowLimitA)       // v27 >= dword_1408A54
                    need = mod.shadowLimitA;
                empty->surface = g_acquireCacheSlot(empty, mod.enableB, ctx, need);
            }
        }
    }

    // ---- (4) REDRAW vs REUSE decision -------------------------------------
    if (slot && slot->surface) {                    // v5 && *v5
        // *(slot+8 link + 8) = ctx (dword_649D58) — the ground-link ctx write.
        // (modeled: the link record is the slot's groundLink; only meaningful to
        //  the BuildGroundShadow leaf, reached through the hook.)
        // (void) intentional: no standalone link record.

        // origin = PointThroughBoneChain(node, node+76 floats)  -> v30
        float origin[3] = {0, 0, 0};
        {
            // a2 (light) holds the position at +76 (light->pos) for the point case;
            // the bone-chain transforms the LIGHT origin in the original
            // (PointThroughBoneChain(v40, v40+19, v30)).
            float in[3] = {light->pos.x, light->pos.y, light->pos.z};
            g_pointThroughBoneChain(light, in, origin);
        }

        float dir[3] = {0, 0, 0};                   // v31
        float tol;                                  // v37
        if ((light->flags529 & 0x10) != 0) {        // directional light (+529 bit4)
            tol = 0.0099999998f;                    // v37 = 0.01
            g_rotateVectorByHierarchy(light, kShadowUpDir, dir); // RotateVectorByHierarchy(v40, flt_5CA2B0)
        } else {
            tol = 1.0f;                             // v37 = 1.0
            // point light: dir = PointThroughBoneChain(node) - origin
            float origin2[3] = {0, 0, 0};           // v32
            float in2[3] = {0, 0, 0};
            g_pointThroughBoneChain(node, in2, origin2);
            dir[0] = origin2[0] - origin[0];        // v31 = v32 - v30
            dir[1] = origin2[1] - origin[1];
            dir[2] = origin2[2] - origin[2];
        }

        // v16 = !VectorWithinTolerance(dir, slot+24, tol) || *(mesh+380)
        float cached[3] = {slot->cachedDir.x, slot->cachedDir.y, slot->cachedDir.z};
        bool dirChanged = !VectorWithinTolerance(dir, cached, tol);
        bool v16 = dirChanged || node->meshDirty;   // mesh-dirty forces redraw
        bool v17 = v16;

        // The coverage REUSE path (0x5f41ba..0x5f425c). LABEL_45 (the render +
        // ground-shadow emit) is reached EITHER by a direct goto (any of the four
        // skip gates below short-circuit straight to it) OR by falling through the
        // `if (v19)` block when the scan ran. CRITICAL control-flow detail: when
        // the scan runs and yields v19 == 0, the `if (v19){...}` block (which
        // CONTAINS LABEL_45) is skipped entirely — i.e. NO RenderMeshShadow and NO
        // BuildGroundShadow. The original gotos are:
        //   if (!a3)                    goto LABEL_45;   (no coverage ctx)
        //   if (v16)                    goto LABEL_45;   (dir changed / mesh dirty)
        //   if (slot+108 == -1)         goto LABEL_45;   (no valid bbox)
        //   if (slot+124)               goto LABEL_45;   (texture path)
        // Otherwise scan [bboxX0..bboxX1] x [bboxY0..bboxY1] of the ground coverage
        // grid (*(a3 + 100*col + 800*row + 318)) for the MAX byte v19 (seeded 0),
        // then: if (v19) { if (v17 || v19 != slot.coverage) v17 = 1; <LABEL_45> }
        bool reachLabel45 = true;          // true == fall into the render+emit block
        if (forceCoverageScan && !v16 && slot->bboxX0 != -1 && !slot->flag124) {
            // v19 starts at slot+124 (== 0 here, since the !flag124 gate passed),
            // then takes the max over the coverage grid. The ground coverage grid
            // lives in the ground ctx (a3) record and is reached only through the
            // render leaf; standalone it is unavailable (rule 8), so the scan body
            // yields v19 == 0 (empty grid) and the slot's cached coverage stands in
            // as the comparison value.
            u8 v19 = 0;
            // (deferred: max over *(ctx + 100*col + 800*row + 318) for the bbox.)
            if (v19 == 0) {
                reachLabel45 = false;       // if (v19) block skipped -> no emit
            } else {
                if (v17 || v19 != static_cast<u8>(slot->coverage))
                    v17 = true;
            }
        }

        if (reachLabel45) {                 // LABEL_45
            if (v17) {
                // RenderMeshShadow(node, mesh, ctx, viewNode, dir, origin, light, slot)
                g_renderMeshShadow(node, node->mesh, ctx, /*viewNode*/ nullptr,
                                   dir, origin, light->id, slot);
                // On a redraw the cached direction is refreshed for next frame's compare.
                slot->cachedDir = ShadowFVec3{dir[0], dir[1], dir[2]};
            }
            // BuildGroundShadow(slot, dword_64A028); on emit the engine ORs +528
            // bit2 on the active node (dword_649D68+528 |= 4) — modeled via return.
            g_buildGroundShadow(slot, ctx);
        }
    }
    return 0;       // the original always returns 0
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f4494 — VIBE_Shadow_UpdateNodeShadows
//   char __usercall al = (a1@eax node)
//
//   (a) clear the object's +531 bit1 ("cast this frame").
//   (b) if (+530 >= 0) refresh the cached bounding volume:
//         TransformBoundingVolume(node, (+528>=0 ? dword_13FCD1C : 0), 0).
//   (c) if dword_1408A5C (active light count): for each collected light i:
//         - transform the 8 bbox corners through the light onto the ground and
//           into view space (PointThroughBoneChain + the directional/point
//           projection + the dword_13FCD1C view transform);
//         - classify the 8 transformed corners against the frustum
//           (ClassifyBoundingBoxPlanes); if bit6 (0x40) is CLEAR (visible):
//             set +531 bit1, and CastFromLight(node, light, ctx, node).
//   Returns the last classify/cast byte (the original returns (char)a1).
//
// The 8-corner projection arithmetic is the wave-6 Shadow_ProjectCorner* /
// Shadow_TransformProjectedPoint math (already in this module). The corner
// SOURCE (the cached bbox at +492's caster table) and the classifier are the
// coupled leaves, reached through the hooks; the DRIVER control flow + the gate
// + the +531 bit management are reconstructed exactly.
// ---------------------------------------------------------------------------
u8 Shadow_UpdateNodeShadows(ShadowNode* node, ShadowLight* const* lights,
                            u32 lightCount, const float* viewMat, void* ctx,
                            bool forceCoverageScan) {
    if (!node) return 0;
    u8 result = 0;

    // (a) a1[531] &= ~2u  — clear "cast this frame".
    node->flag531 &= static_cast<u8>(~2u);

    // (b) if (a1[530] >= 0) TransformBoundingVolume(node, viewMat-or-0, 0).
    if (node->flag530sign >= 0) {
        // v3 = (a1[528] >= 0) ? dword_13FCD1C : 0
        const float* mat = (node->flag528sign >= 0) ? viewMat : nullptr;
        result = static_cast<u8>(g_transformBoundingVolume(node, mat, false));
    }

    // (c) per-light loop, gated by the collected-light count.
    if (lightCount == 0 || lights == nullptr)
        return result;

    for (u32 i = 0; i < lightCount; ++i) {
        ShadowLight* light = lights[i];
        if (!node->mesh)            // a1 = *((_DWORD*)v1 + 115); if (!a1) break;
            break;
        if (!light)
            continue;

        // The 8 transformed bbox corners feed ClassifyBoundingBoxPlanes. The
        // corner source is the cached caster bbox (deferred leaf); the projection
        // math itself is Shadow_ProjectCorner*/Shadow_TransformProjectedPoint
        // (this module). The classifier returns the plane mask byte.
        u8 mask = g_classifyBoundingBox(/*corners8*/ nullptr); // a1 = Classify(...)
        result = mask;
        if ((mask & 0x40) == 0) {           // visible (bit6 clear)
            node->flag531 |= 2u;            // v1[531] |= 2u  ("cast this frame")
            result = Shadow_CastFromLight2(node, light, ctx, forceCoverageScan);
        }
    }
    return result;
}

} // namespace guild::render::fxrecon
