// Golden-vector unit tests for the fxrecon particle / mirror / shadow cluster.
// All vectors are derived directly from the gilde.exe decompile (constants,
// control flow, arithmetic). Self-contained.
#include "tests/framework/test.h"
#include "render/fxrecon_particle_mirror_shadow.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild::render::fxrecon;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
}

// ---------------------------------------------------------------------------
// Particle_ResetEmitter — clears bit0 of (slot+81) over slotCount slots, sets
// the two rebind fields, returns base + 84*slotCount.
// ---------------------------------------------------------------------------
TEST(FxReconParticle, ResetEmitterClearsActiveBitAndRebinds) {
    const int N = 3;
    std::vector<guild::u8> slots(84 * N, 0);
    for (int i = 0; i < N; ++i) slots[84 * i + 81] = 0xFF;   // all bits set

    EmitterObject obj{};
    obj.slotBase = slots.data();
    obj.slotCount = N;

    guild::u8* ret = Particle_ResetEmitter(&obj, 0xAAAA1111u, 0xBBBB2222u);

    CHECK_EQ(obj.fieldA, 0xAAAA1111u);
    CHECK_EQ(obj.fieldB, 0xBBBB2222u);
    for (int i = 0; i < N; ++i)
        CHECK_EQ(slots[84 * i + 81], static_cast<guild::u8>(0xFE));  // bit0 cleared
    CHECK(ret == slots.data() + 84 * N);
}

TEST(FxReconParticle, ResetEmitterZeroSlotsReturnsBase) {
    EmitterObject obj{};
    guild::u8 dummy = 0;
    obj.slotBase = &dummy;
    obj.slotCount = 0;
    guild::u8* ret = Particle_ResetEmitter(&obj, 1, 2);
    CHECK(ret == &dummy);       // loop body never runs
    CHECK_EQ(obj.fieldA, 1u);
    CHECK_EQ(obj.fieldB, 2u);
}

// ---------------------------------------------------------------------------
// Particle_SpawnSparkleEffect — forwards the fixed preset to SpawnEffect.
// Golden values from the decompile: pos={0,53,0}, descriptor low byte 50 then
// 0x501E, lifetime 300, a8=50.0f, a9=1.0f, a10=2.0f, a11=a12=0.5f bits.
// ---------------------------------------------------------------------------
namespace {
struct CapturedSpawn {
    bool called = false;
    guild::u32 owner = 0, descriptor = 0;
    const char* tpl = nullptr;
    guild::u8 mode = 0;
    SparkleParams params{};
    guild::i32 lifetime = 0, a7 = 0;
    float a8 = 0;
    guild::u32 a9 = 0, a10 = 0, a11 = 0, a12 = 0;
} g_cap;

guild::i32 CaptureSpawn(guild::u32 owner, guild::u32 desc, const char* tpl,
                        guild::u8 mode, const SparkleParams* p, guild::i32 life,
                        guild::i32 a7, float a8, guild::u32 a9, guild::u32 a10,
                        guild::u32 a11, guild::u32 a12) {
    g_cap.called = true;
    g_cap.owner = owner; g_cap.descriptor = desc; g_cap.tpl = tpl;
    g_cap.mode = mode; g_cap.params = *p; g_cap.lifetime = life; g_cap.a7 = a7;
    g_cap.a8 = a8; g_cap.a9 = a9; g_cap.a10 = a10; g_cap.a11 = a11; g_cap.a12 = a12;
    return 0x1234;
}
}

TEST(FxReconParticle, SpawnSparkleEffectForwardsPreset) {
    g_cap = CapturedSpawn{};
    SetSpawnEffectHook(&CaptureSpawn);

    guild::i32 r = Particle_SpawnSparkleEffect(0xDEAD, 0xBEEF);

    CHECK_EQ(r, 0x1234);
    CHECK(g_cap.called);
    CHECK_EQ(g_cap.owner, 0xDEADu);
    CHECK(std::strcmp(g_cap.tpl, "punkte_nm_2") == 0);
    CHECK_EQ(g_cap.mode, static_cast<guild::u8>(1));
    CHECK_EQ(g_cap.lifetime, 300);
    CHECK_EQ(g_cap.a7, 1);
    CHECK(feq(g_cap.a8, 50.0f));
    CHECK_EQ(g_cap.a9, 1065353216u);
    CHECK_EQ(g_cap.a10, 0x40000000u);
    CHECK_EQ(g_cap.a11, 1056964608u);
    CHECK_EQ(g_cap.a12, 1056964608u);
    // pos = {0, 50.0f, 0}  (1112014848 == 50.0f)
    CHECK(feq(g_cap.params.pos[0], 0.0f));
    CHECK(feq(g_cap.params.pos[1], 50.0f));
    CHECK(feq(g_cap.params.pos[2], 0.0f));
    // descriptor: byte0=50, bytes1..2 = 0x501E (little-endian 20510)
    const guild::u8* db = reinterpret_cast<const guild::u8*>(&g_cap.descriptor);
    CHECK_EQ(db[0], static_cast<guild::u8>(50));
    guild::u16 hi;
    std::memcpy(&hi, db + 1, sizeof(hi));
    CHECK_EQ(hi, static_cast<guild::u16>(20510));

    SetSpawnEffectHook(nullptr);   // restore inert default
}

// ---------------------------------------------------------------------------
// Particle_SetOrientationFromAngle — enable gate + hook ordering.
// ---------------------------------------------------------------------------
namespace {
int g_basisCalls = 0, g_eulerCalls = 0, g_translCalls = 0;
void OrderBasis(const float[3], float, float m[16]) { ++g_basisCalls; for (int i=0;i<16;++i) m[i]=0; }
void OrderEuler(float m[16]) { ++g_eulerCalls; m[0]=0.1f; m[1]=0.2f; m[2]=0.3f; }
float g_seenEuler[3];
guild::u8 OrderTransl(void*, const float e[3]) { ++g_translCalls; g_seenEuler[0]=e[0]; g_seenEuler[1]=e[1]; g_seenEuler[2]=e[2]; return 0x7; }
}

TEST(FxReconParticle, SetOrientationDisabledSkipsHooks) {
    g_basisCalls = g_eulerCalls = g_translCalls = 0;
    SetBuildBasisHook(&OrderBasis);
    SetMatrixToEulerHook(&OrderEuler);
    SetSetWorldTranslationHook(&OrderTransl);
    float dir[3] = {1, 0, 0};
    char obj[256] = {0};
    guild::u8 r = Particle_SetOrientationFromAngle(0, dir, obj, 1.0f);
    CHECK_EQ(g_basisCalls, 0);
    CHECK_EQ(g_eulerCalls, 0);
    CHECK_EQ(g_translCalls, 0);
    // returns (char)a2 ; only the low byte of the pointer matters for == against
    // the same truncation; we just assert no crash and hooks untouched.
    (void)r;
    SetBuildBasisHook(nullptr); SetMatrixToEulerHook(nullptr); SetSetWorldTranslationHook(nullptr);
}

TEST(FxReconParticle, SetOrientationEnabledRunsBasisEulerTranslate) {
    g_basisCalls = g_eulerCalls = g_translCalls = 0;
    SetBuildBasisHook(&OrderBasis);
    SetMatrixToEulerHook(&OrderEuler);
    SetSetWorldTranslationHook(&OrderTransl);
    float dir[3] = {0, 1, 0};
    char obj[256] = {0};
    guild::u8 r = Particle_SetOrientationFromAngle(1, dir, obj, 0.5f);
    CHECK_EQ(g_basisCalls, 1);
    CHECK_EQ(g_eulerCalls, 1);
    CHECK_EQ(g_translCalls, 1);
    CHECK_EQ(r, static_cast<guild::u8>(0x7));   // SetWorldTranslation return
    CHECK(feq(g_seenEuler[0], 0.1f));           // euler triple forwarded
    CHECK(feq(g_seenEuler[1], 0.2f));
    CHECK(feq(g_seenEuler[2], 0.3f));
    SetBuildBasisHook(nullptr); SetMatrixToEulerHook(nullptr); SetSetWorldTranslationHook(nullptr);
}

// ---------------------------------------------------------------------------
// Shadow_AllocCache / InitBuffers / ShutdownBuffers — module state.
// ---------------------------------------------------------------------------
TEST(FxReconShadow, AllocCacheSizesAndRecords) {
    Shadow_SetGlobalLimit(0xFFFFFFFFu);
    void* base = Shadow_AllocCache(5, 7);
    ShadowModuleState& s = ShadowState();
    CHECK_EQ(s.cacheCount, 5u);
    CHECK_EQ(s.cacheParam, 7u);
    CHECK(base != nullptr);
    CHECK(s.cacheBase == base);
    Shadow_ShutdownBuffers();   // frees it
}

TEST(FxReconShadow, InitBuffersClampsAndSeedsRows) {
    Shadow_SetGlobalLimit(8);             // dword_1408078 = 8
    // a1=4, a2=16 -> limitA = min(16,8)=8 ; limitB = min(4,8)=4
    guild::i32 r = Shadow_InitBuffers(/*a1*/4, /*a2*/16, /*a3*/4, /*a4*/0xAB,
                                      /*a5 res*/4, /*a6*/99);
    ShadowModuleState& s = ShadowState();
    CHECK_EQ(r, 16);                      // result*4, result==4
    CHECK_EQ(s.shadowLimitA, 8u);
    CHECK_EQ(s.shadowLimitB, 4u);
    CHECK_EQ(s.paramA5, 4u);
    CHECK_EQ(s.paramA6, 99u);
    CHECK_EQ(s.resSquared, 16u);          // 4*4
    CHECK(s.buildingPoly != nullptr);
    CHECK(s.buildingPoint != nullptr);
    CHECK_EQ(s.enableB, static_cast<guild::u8>(0xAB));
    CHECK_EQ(s.enableA, static_cast<guild::u8>(1));
    CHECK_EQ(s.enableC, static_cast<guild::u8>(1));
    // seeded 1.0f-bit row entries
    CHECK_EQ(s.rowA[1], 1065353216u);
    CHECK_EQ(s.rowA[3], 1065353216u);
    CHECK_EQ(s.rowA[6], 1065353216u);
    CHECK_EQ(s.rowA[8], 1065353216u);
    CHECK_EQ(s.rowA[0], 0u);
    CHECK_EQ(s.slotA0C[0], 0u);
    CHECK_EQ(s.slotA0C[3], 0u);

    Shadow_ShutdownBuffers();
    CHECK_EQ(s.shadowLimitA, 0u);
    CHECK_EQ(s.shadowLimitB, 0u);
    CHECK_EQ(s.resSquared, 0u);
    CHECK(s.buildingPoly == nullptr);
    CHECK(s.buildingPoint == nullptr);
    CHECK(s.cacheBase == nullptr);
    Shadow_SetGlobalLimit(0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Shadow_CastFromAllLights — guard conditions + iteration count.
// ---------------------------------------------------------------------------
namespace {
int g_castCalls = 0;
void* g_lastLight = nullptr;
guild::u8 CountCast(void*, void* light, void*, void*) { ++g_castCalls; g_lastLight = light; return 0x55; }
}

TEST(FxReconShadow, CastFromAllLightsGatedByFlagAndCount) {
    SetCastFromLightHook(&CountCast);
    ShadowModuleState& s = ShadowState();
    s.misc5C = 3;
    s.lights[0] = reinterpret_cast<void*>(0x100);
    s.lights[1] = reinterpret_cast<void*>(0x200);
    s.lights[2] = reinterpret_cast<void*>(0x300);

    char obj[600] = {0};

    // flag +529 bit2 clear -> no casts
    g_castCalls = 0;
    obj[529] = 0;
    Shadow_CastFromAllLights(obj);
    CHECK_EQ(g_castCalls, 0);

    // flag set -> 3 casts
    g_castCalls = 0;
    obj[529] = 0x04;
    Shadow_CastFromAllLights(obj);
    CHECK_EQ(g_castCalls, 3);
    CHECK(g_lastLight == reinterpret_cast<void*>(0x300));

    // count 0 -> no casts even with flag
    g_castCalls = 0;
    s.misc5C = 0;
    Shadow_CastFromAllLights(obj);
    CHECK_EQ(g_castCalls, 0);

    SetCastFromLightHook(nullptr);
}

// WAVE-10 HARDENING — an over-large dword_1408A5C (corrupt light count) must not
// read past the fixed lights[] storage. The loop is clamped to the array size,
// so at most sizeof(lights) entries are visited (no OOB read of s.lights[]).
TEST(FxReconShadow, CastFromAllLightsClampsOverlargeCount) {
    SetCastFromLightHook(&CountCast);
    ShadowModuleState& s = ShadowState();
    char obj[600] = {0};
    obj[529] = 0x04;

    // The storage holds 64 light pointers; claim a wildly-too-large count.
    s.misc5C = 1000000u;
    for (auto& l : s.lights) l = reinterpret_cast<void*>(0x1);
    g_castCalls = 0;
    Shadow_CastFromAllLights(obj);     // ASAN: must not read s.lights[1000000]
    CHECK_EQ(g_castCalls, 64);         // exactly the storage capacity

    s.misc5C = 0;
    for (auto& l : s.lights) l = nullptr;
    SetCastFromLightHook(nullptr);
}

// ---------------------------------------------------------------------------
// Shadow projection math — directional + point light, golden hand-computed.
// ---------------------------------------------------------------------------
TEST(FxReconShadow, ProjectCornerDirectional) {
    // corner above ground at y=10, ground (caster height) = 0,
    // lightDir straight down (0,-1,0): t = (10-0)/-(-1)=10 -> lands at y=0.
    float corner[3] = {5, 10, 7};
    float dir[3] = {0, -1, 0};
    float out[3];
    Shadow_ProjectCornerDirectional(corner, dir, 0.0f, out);
    CHECK(feq(out[0], 5.0f));   // x + t*0
    CHECK(feq(out[1], 0.0f));   // y + t*(-1) = 10 - 10
    CHECK(feq(out[2], 7.0f));

    // angled light dir (1,-1,0): t = (10)/-(-1) = 10 -> x = 5 + 10*1 = 15.
    float dir2[3] = {1, -1, 0};
    Shadow_ProjectCornerDirectional(corner, dir2, 0.0f, out);
    CHECK(feq(out[0], 15.0f));
    CHECK(feq(out[1], 0.0f));
    CHECK(feq(out[2], 7.0f));
}

TEST(FxReconShadow, ProjectCornerPoint) {
    // light at (0,10,0), corner at (4,0,0), ground caster height = 0.
    // d = light-corner = (-4,10,0); t = (10-0)/-(10) = -1.
    // out = light + t*d = (0,10,0) + (-1)*(-4,10,0) = (4,0,0).
    float corner[3] = {4, 0, 0};
    float light[3] = {0, 10, 0};
    float out[3];
    Shadow_ProjectCornerPoint(corner, light, 0.0f, out);
    CHECK(feq(out[0], 4.0f));
    CHECK(feq(out[1], 0.0f));
    CHECK(feq(out[2], 0.0f));
}

TEST(FxReconShadow, TransformProjectedPointIdentity) {
    // Build a float block where origin (offsets 76/80/84) = 0 and the 3x3 at
    // 396.. is identity with zero translation -> out == p. Needs index 452/4=113.
    float m[128] = {0};
    m[396/4] = 1.0f; m[416/4] = 1.0f; m[436/4] = 1.0f;  // diagonal
    // off-diagonals already 0; translations 444/448/452 = 0.
    float p[3] = {3, -2, 5};
    float out[3];
    Shadow_TransformProjectedPoint(p, m, out);
    CHECK(feq(out[0], 3.0f));
    CHECK(feq(out[1], -2.0f));
    CHECK(feq(out[2], 5.0f));
}

// ---------------------------------------------------------------------------
// Mirror plane kernel + tolerance.
// ---------------------------------------------------------------------------
TEST(FxReconMirror, BuildClipPlaneXYTriangle) {
    // Triangle in z=0 plane, CCW: normal should be +/-Z, d = -(n.v0) = 0.
    float v0[3] = {0, 0, 0};
    float v1[3] = {1, 0, 0};
    float v2[3] = {0, 1, 0};
    MirrorClipPlaneEq pl;
    bool ok = Mirror_BuildClipPlane(v0, v1, v2, &pl);
    CHECK(ok);
    // n = (v1-v0)x(v2-v0) via engine formula:
    //  a=(1,0,0) b=(0,1,0); n=( bz*ay-by*az, bx*az-bz*ax, by*ax-bx*ay )
    //   = (0*0-1*0, 0*0-0*1, 1*1-0*0) = (0,0,1) -> normalized (0,0,1)
    CHECK(feq(pl.nx, 0.0f));
    CHECK(feq(pl.ny, 0.0f));
    CHECK(feq(pl.nz, 1.0f));
    CHECK(feq(pl.d, 0.0f));     // v0 on plane through origin
}

TEST(FxReconMirror, BuildClipPlaneOffsetGivesD) {
    // Same orientation but lifted to z=4 -> d = -(n.v0) = -(1*4) = -4.
    float v0[3] = {0, 0, 4};
    float v1[3] = {1, 0, 4};
    float v2[3] = {0, 1, 4};
    MirrorClipPlaneEq pl;
    CHECK(Mirror_BuildClipPlane(v0, v1, v2, &pl));
    CHECK(feq(pl.nz, 1.0f));
    CHECK(feq(pl.d, -4.0f));
}

TEST(FxReconMirror, BuildClipPlaneDegenerateFails) {
    float v0[3] = {0, 0, 0};
    float v1[3] = {1, 0, 0};
    float v2[3] = {2, 0, 0};   // collinear
    MirrorClipPlaneEq pl;
    CHECK(!Mirror_BuildClipPlane(v0, v1, v2, &pl));
}

TEST(FxReconMirror, VectorWithinTolerance) {
    float a[3] = {1.0f, 2.0f, 3.0f};
    float b[3] = {1.0005f, 1.9995f, 3.0005f};
    CHECK(VectorWithinTolerance(a, b, 0.001f));
    float c[3] = {1.5f, 2.0f, 3.0f};
    CHECK(!VectorWithinTolerance(a, c, 0.001f));
}
