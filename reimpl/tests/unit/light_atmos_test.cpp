#include "test.h"

// UNIT: the ATMOS LIGHTING-TABLE REBUILD — golden vectors straight from the
// Hex-Rays decompile of the gilde.exe lighting cluster:
//   0x5c8218 VIBE_Light_BuildObjectCache   (ambient seed -> reduce -> publish)
//   0x5c7f04 VIBE_Light_ApplyVertexShading (material light-word pass)
//   0x5c7da4 VIBE_Light_RemoveCacheEntry   (cache-list free/unlink)
//   0x5c886c VIBE_Light_RefreshAllObjects  (invalidate / eager walks + serial)
//   0x5add1c ProcessSceneNode trigger arms (EnsureNodeLit, budgets 512/128)
//   0x5b85e4 BlendBandLighting store half  (StoreAmbient) + 0x5c8964 reset
// All float expectations were computed independently with an IEEE-754 float32
// simulation of the decompiled expressions (truncate-toward-zero stores).
#include "render/light_atmos.h"
#include "render/geometry_types.h"
#include "render/sky.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A small caller-owned mesh frame (the obj+460 lighting table).
struct TestMesh {
    std::vector<Vertex>  verts;
    std::vector<Polygon> polys;
    MeshGeometry geom{};

    explicit TestMesh(int nVerts, int nTris = 0) {
        verts.assign((size_t)nVerts, Vertex{});
        polys.assign((size_t)(nTris > 0 ? nTris : 0), Polygon{});
        for (int t = 0; t < nTris; ++t) {
            polys[(size_t)t].v0 = &verts[(size_t)((3 * t + 0) % nVerts)];
            polys[(size_t)t].v1 = &verts[(size_t)((3 * t + 1) % nVerts)];
            polys[(size_t)t].v2 = &verts[(size_t)((3 * t + 2) % nVerts)];
        }
        geom.vertices    = verts.data();
        geom.polygons    = polys.empty() ? nullptr : polys.data();
        geom.vertexCount = nVerts;
        geom.polyCount   = (i32)polys.size();
    }
};

u8 ByteAt(const Vertex& v, int off) {
    return reinterpret_cast<const u8*>(&v)[off];
}
void SetByteAt(Vertex& v, int off, u8 b) {
    reinterpret_cast<u8*>(&v)[off] = b;
}

// Store an explicit ambient triple (the flt_64A074/78/7C state).
void Ambient(float r, float g, float b) {
    SkyAmbient a{r, g, b, 0.0f};
    LightAtmosStoreAmbient(a);
}

} // namespace

// ---------------------------------------------------------------------------
// Globals: the static-image initial values (get_bytes @0x64A050..0x64A07C).
// ---------------------------------------------------------------------------
TEST(LightAtmos, StaticImageDefaults) {
    LightAtmosResetAll();
    const LightAtmosGlobals& g = LightAtmos();
    CHECK_EQ(g.ambientR, 200.0f);          // flt_64A074 = 0x43480000
    CHECK_EQ(g.ambientG, 200.0f);          // flt_64A078
    CHECK_EQ(g.ambientB, 200.0f);          // flt_64A07C
    CHECK_EQ(g.ambientLuma, 200.0f);       // flt_64A070
    CHECK_EQ(g.offscreenBudget, 128u);     // dword_64A054 = 0x80
    CHECK_EQ(g.onscreenBudget, 512u);      // dword_64A05C = 0x200
    CHECK_EQ(g.rebuildSerial, 0u);         // dword_64A064
    CHECK_EQ((int)g.invalidated, 0);       // byte_64A068
    CHECK_EQ((int)g.relightAlways, 0);     // byte_649D54
    CHECK_EQ((int)g.colorMode, 0);         // byte_649D70
}

// gilde.exe 0x5c8964 — VIBE_Light_ResetGlobalState zeroes serial AND budgets.
TEST(LightAtmos, ResetGlobalStateZeroesSerialAndBudgets) {
    LightAtmosResetAll();
    LightAtmos().rebuildSerial = 7;
    LightAtmos().onscreenSpent = 3;
    LightAtmosResetGlobalState();
    CHECK_EQ(LightAtmos().rebuildSerial, 0u);
    CHECK_EQ(LightAtmos().onscreenBudget, 0u);   // dword_64A05C = 0
    CHECK_EQ(LightAtmos().offscreenBudget, 0u);  // dword_64A054 = 0
    CHECK_EQ(LightAtmos().onscreenSpent, 0u);
    CHECK_EQ(LightAtmos().offscreenSpent, 0u);
}

// gilde.exe 0x5b85e4 store half — the BlendBandLighting output lands in the
// flt_64A070..7C globals.
TEST(LightAtmos, StoreAmbientFromBlendBandLighting) {
    LightAtmosResetAll();
    SkyBandColor bands[kSkyBands] = {
        {200.0f, 200.0f, 200.0f}, {80.0f, 90.0f, 100.0f},
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    };
    SkyAmbient a = BlendBandLighting(bands, 0, 0.5f, 1.0f);
    LightAtmosStoreAmbient(a);
    CHECK_EQ(LightAtmos().ambientR, 140.0f);   // lerp(200,80,.5)
    CHECK_EQ(LightAtmos().ambientG, 145.0f);   // lerp(200,90,.5)
    CHECK_EQ(LightAtmos().ambientB, 150.0f);   // lerp(200,100,.5)
    CHECK_EQ(LightAtmos().ambientLuma, a.luma);
}

// ---------------------------------------------------------------------------
// 0x5c8218 luma branch goldens: shade = trunc(g*0.58999997 + r*0.30000001 +
// b*0.10999999), unsigned-saturated at 255, published into lightIdx (+66).
// ---------------------------------------------------------------------------
TEST(LightAtmos, BuildObjectCacheLumaGoldens) {
    struct Case { float r, g, b; int shade; } cases[] = {
        {200.0f, 200.0f, 200.0f, 200},   // the static ambient -> 200 exactly
        {40.0f, 40.0f, 40.0f, 40},
        {120.0f, 90.0f, 60.0f, 95},      // 95.69999694824219
        {255.0f, 255.0f, 255.0f, 255},
        {300.0f, 300.0f, 300.0f, 255},   // saturates (unsigned >= 0xFF)
        {0.0f, 0.0f, 0.0f, 0},
        {80.0f, 90.0f, 100.0f, 88},      // 88.0999984741211
        {140.0f, 145.0f, 150.0f, 144},   // 144.04998779296875
        {170.0f, 172.5f, 175.0f, 172},   // 172.02499389648438
        {150.0f, 125.0f, 110.0f, 130},   // 130.85000610351562
    };
    for (const Case& c : cases) {
        LightAtmosResetAll();
        Ambient(c.r, c.g, c.b);
        TestMesh m(4);
        LightAtmosObject o;
        o.geom = &m.geom;
        CHECK_EQ((int)LightAtmosBuildObjectCache(o, 1), 1);
        for (int i = 0; i < 4; ++i) {
            CHECK_EQ((int)m.verts[(size_t)i].lightIdx, c.shade);  // +66
            CHECK_EQ((int)ByteAt(m.verts[(size_t)i], 70), c.shade);
            // accumulator seed survives the reduce (the +48/52/56 triple)
            float acc[3];
            VertexLightAccumGet(m.verts[(size_t)i], acc);
            CHECK_EQ(acc[0], c.r);
            CHECK_EQ(acc[1], c.g);
            CHECK_EQ(acc[2], c.b);
        }
        CHECK((o.flags528 & 4) != 0);            // cache-built flag
        CHECK_EQ(o.cacheSerial, LightAtmos().rebuildSerial);
        CHECK_EQ(o.lastBuildFrame, 1);
    }
}

// Brightness chain golden: 7-band rig, band 0 -> 1 sweep — the same vectors
// the SessionAtmos brightness step produces (band = trunc(brightness*0.01)%7).
TEST(LightAtmos, BandBlendToShadeSweep) {
    SkyBandColor bands[kSkyBands] = {
        {200.0f, 200.0f, 200.0f}, {80.0f, 90.0f, 100.0f},
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    };
    struct Case { float t; int shade; } cases[] = {
        {0.0f, 200}, {0.25f, 172}, {0.5f, 144}, {1.0f, 88},
    };
    for (const Case& c : cases) {
        LightAtmosResetAll();
        LightAtmosStoreAmbient(BlendBandLighting(bands, 0, c.t, 1.0f));
        TestMesh m(3);
        LightAtmosObject o;
        o.geom = &m.geom;
        LightAtmosBuildObjectCache(o, 0);
        CHECK_EQ((int)m.verts[0].lightIdx, c.shade);
    }
}

// The publish copy is a raw dword move +64 <- +68: only +70 is rewritten by the
// luma branch, the neighbouring bytes ride along (lightIdx = byte +70).
TEST(LightAtmos, LumaPublishCopiesPackedDword) {
    LightAtmosResetAll();
    Ambient(40.0f, 40.0f, 40.0f);
    TestMesh m(1);
    SetByteAt(m.verts[0], 68, 1);
    SetByteAt(m.verts[0], 69, 2);
    SetByteAt(m.verts[0], 70, 3);
    SetByteAt(m.verts[0], 71, 4);
    LightAtmosObject o;
    o.geom = &m.geom;
    LightAtmosBuildObjectCache(o, 0);
    CHECK_EQ((int)ByteAt(m.verts[0], 70), 40);   // the new shade
    CHECK_EQ((int)ByteAt(m.verts[0], 64), 1);    // +64 <- +68 (untouched B)
    CHECK_EQ((int)ByteAt(m.verts[0], 65), 2);    // +65 <- +69
    CHECK_EQ((int)m.verts[0].lightIdx, 40);      // +66 <- +70
    CHECK_EQ((int)ByteAt(m.verts[0], 67), 4);    // +67 <- +71
}

// ---------------------------------------------------------------------------
// 0x5c8218 colour branch (byte_649D70 != 0): max-channel scale when the max's
// float BITS exceed 1132396544 (0x437EFFFF = 254.999985f); bytes R/G/B at
// +70/+69/+68; lightIdx = the R byte.
// ---------------------------------------------------------------------------
TEST(LightAtmos, BuildObjectCacheColorGoldens) {
    struct Case { float r, g, b; int br, bg, bb; } cases[] = {
        {300.0f, 200.0f, 100.0f, 255, 170, 85},          // scaled by 255/300
        {255.0f, 255.0f, 255.0f, 255, 255, 255},         // k = 1.0 exact
        {254.99998474121094f, 1.0f, 2.0f, 254, 1, 2},    // bits == threshold: NO scale
        {100.0f, 50.0f, 25.0f, 100, 50, 25},             // below: untouched
    };
    for (const Case& c : cases) {
        LightAtmosResetAll();
        LightAtmos().colorMode = 1;                      // byte_649D70
        Ambient(c.r, c.g, c.b);
        TestMesh m(2);
        LightAtmosObject o;
        o.geom = &m.geom;
        LightAtmosBuildObjectCache(o, 0);
        CHECK_EQ((int)ByteAt(m.verts[0], 70), c.br);     // R
        CHECK_EQ((int)ByteAt(m.verts[0], 69), c.bg);     // G
        CHECK_EQ((int)ByteAt(m.verts[0], 68), c.bb);     // B
        CHECK_EQ((int)m.verts[0].lightIdx, c.br);        // publish: +66 <- +70
    }
}

// Negative luma saturates to 255 (the original's `(unsigned)(int)v22 >= 0xFF`
// with LOBYTE(v23) = -1) — reachable only through a (hook-supplied) negative
// light accumulation.
TEST(LightAtmos, NegativeLumaSaturates) {
    LightAtmosResetAll();
    Ambient(10.0f, 10.0f, 10.0f);
    LightAtmosHooks h;
    h.accumulateLights = [](LightAtmosObject& o, u8, void*) {
        for (int i = 0; i < o.geom->vertexCount; ++i)
            VertexLightAccumAdd(o.geom->vertices[i], -500.0f, -500.0f, -500.0f);
    };
    SetLightAtmosHooks(h);
    TestMesh m(1);
    LightAtmosObject o;
    o.geom = &m.geom;
    LightAtmosBuildObjectCache(o, 0);
    CHECK_EQ((int)m.verts[0].lightIdx, 255);
    SetLightAtmosHooks(LightAtmosHooks{});
}

// The collect flag handed to the accumulate hook is bit5 of flags528:
// (u8)(4 * flags528) >> 7.
TEST(LightAtmos, CollectFlagIsBit5OfFlags528) {
    LightAtmosResetAll();
    static u8 seen;
    seen = 0xCC;
    LightAtmosHooks h;
    h.accumulateLights = [](LightAtmosObject&, u8 flag, void*) { seen = flag; };
    SetLightAtmosHooks(h);
    TestMesh m(1);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.flags528 = 0x20;                       // bit5 set
    LightAtmosBuildObjectCache(o, 0);
    CHECK_EQ((int)seen, 1);
    o.flags528 = (u8)~0x20u;                 // every bit but 5
    LightAtmosBuildObjectCache(o, 0);
    CHECK_EQ((int)seen, 0);
    SetLightAtmosHooks(LightAtmosHooks{});
}

// Gates: a hidden object (flags530 bit1) and a null mesh frame both return 0
// and leave the table untouched.
TEST(LightAtmos, HiddenAndNullFrameGates) {
    LightAtmosResetAll();
    Ambient(50.0f, 50.0f, 50.0f);
    TestMesh m(1);
    m.verts[0].lightIdx = 123;
    LightAtmosObject o;
    o.geom = &m.geom;
    o.flags530 = 2;                          // hidden
    CHECK_EQ((int)LightAtmosBuildObjectCache(o, 0), 0);
    CHECK_EQ((int)m.verts[0].lightIdx, 123);
    LightAtmosObject o2;                     // null frame
    CHECK_EQ((int)LightAtmosBuildObjectCache(o2, 0), 0);
}

// ---------------------------------------------------------------------------
// 0x5c7f04 — ApplyVertexShading material pass goldens.
// ---------------------------------------------------------------------------
TEST(LightAtmos, ApplyVertexShadingRecomputeArm) {
    // word bit16 set: shade = trunc(min(255, (r*.30+g*.59+b*.11+hi)*(1/256)*lo))
    // broadcast into BOTH packed dwords (+64 and +68).
    struct Case { float amb; u8 hi, lo; int shade; } cases[] = {
        {200.0f, 0, 255, 199},     // 199.21875
        {100.0f, 64, 255, 163},    // 163.359375
        {100.0f, 64, 128, 82},     // 82.0
        {255.0f, 255, 255, 255},   // clamped
    };
    for (const Case& c : cases) {
        LightAtmosResetAll();
        Ambient(c.amb, c.amb, c.amb);
        static LightAtmosPolyMaterial mat;
        mat = LightAtmosPolyMaterial{};
        mat.lightWord108 = 0x10000u | ((u32)c.hi << 8) | c.lo;
        LightAtmosHooks h;
        h.polyMaterial = [](const LightAtmosObject&, const Polygon&, i32,
                            void*) -> const LightAtmosPolyMaterial* {
            return &mat;
        };
        SetLightAtmosHooks(h);
        TestMesh m(3, 1);
        LightAtmosObject o;
        o.geom = &m.geom;
        LightAtmosBuildObjectCache(o, 0);
        for (int i = 0; i < 3; ++i)
            for (int off = 64; off <= 71; ++off)
                CHECK_EQ((int)ByteAt(m.verts[(size_t)i], off), c.shade);
        SetLightAtmosHooks(LightAtmosHooks{});
    }
}

TEST(LightAtmos, ApplyVertexShadingByteArmAndSkips) {
    LightAtmosResetAll();
    Ambient(200.0f, 200.0f, 200.0f);
    static LightAtmosPolyMaterial mat;
    LightAtmosHooks h;
    h.polyMaterial = [](const LightAtmosObject&, const Polygon&, i32,
                        void*) -> const LightAtmosPolyMaterial* { return &mat; };
    SetLightAtmosHooks(h);

    // LOBYTE != 0xFF, bit16 clear: only bytes +67/+71 are written.
    mat = LightAtmosPolyMaterial{};
    mat.lightWord108 = 0x55;
    TestMesh m(3, 1);
    LightAtmosObject o;
    o.geom = &m.geom;
    LightAtmosBuildObjectCache(o, 0);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ((int)ByteAt(m.verts[(size_t)i], 67), 0x55);
        CHECK_EQ((int)ByteAt(m.verts[(size_t)i], 71), 0x55);
        CHECK_EQ((int)m.verts[(size_t)i].lightIdx, 200);   // untouched
    }

    // LOBYTE == 0xFF: the pass leaves the vertices alone.
    mat.lightWord108 = 0xFF;
    TestMesh m2(3, 1);
    LightAtmosObject o2;
    o2.geom = &m2.geom;
    LightAtmosBuildObjectCache(o2, 0);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ((int)ByteAt(m2.verts[(size_t)i], 67), 0);
        CHECK_EQ((int)m2.verts[(size_t)i].lightIdx, 200);
    }

    // No material hook at all: the whole pass is a skip (null *(poly+20)).
    SetLightAtmosHooks(LightAtmosHooks{});
    TestMesh m3(3, 1);
    LightAtmosObject o3;
    o3.geom = &m3.geom;
    LightAtmosBuildObjectCache(o3, 0);
    CHECK_EQ((int)m3.verts[0].lightIdx, 200);
}

TEST(LightAtmos, ApplyVertexShadingFlag38Edit) {
    LightAtmosResetAll();
    static LightAtmosPolyMaterial mat;
    LightAtmosHooks h;
    h.polyMaterial = [](const LightAtmosObject&, const Polygon&, i32,
                        void*) -> const LightAtmosPolyMaterial* { return &mat; };
    SetLightAtmosHooks(h);

    // material byte+104 bit4 -> poly flags38 bit2 (flags530 bit0 clear).
    mat = LightAtmosPolyMaterial{};
    mat.flagByte104 = 0x10;
    mat.lightWord108 = 0xFF;                     // no vertex writes
    TestMesh m(3, 1);
    m.polys[0].flags38 = 0x01;
    LightAtmosObject o;
    o.geom = &m.geom;
    LightAtmosApplyVertexShading(o);
    CHECK_EQ((int)m.polys[0].flags38, 0x05);     // (1 & 0xFB) | 4

    // bit4 clear -> bit2 cleared.
    mat.flagByte104 = 0;
    m.polys[0].flags38 = 0x05;
    LightAtmosApplyVertexShading(o);
    CHECK_EQ((int)m.polys[0].flags38, 0x01);

    // flags530 bit0 set -> the edit is skipped entirely.
    mat.flagByte104 = 0x10;
    m.polys[0].flags38 = 0x01;
    o.flags530 = 1;
    LightAtmosApplyVertexShading(o);
    CHECK_EQ((int)m.polys[0].flags38, 0x01);
    SetLightAtmosHooks(LightAtmosHooks{});
}

// flags529 bit7 set -> the frame fallback word (+376) replaces material+108.
TEST(LightAtmos, ApplyVertexShadingFrameWordFallback) {
    LightAtmosResetAll();
    Ambient(200.0f, 200.0f, 200.0f);
    static LightAtmosPolyMaterial mat;
    mat = LightAtmosPolyMaterial{};
    mat.lightWord108 = 0xFF;                     // material word: no-op arm
    LightAtmosHooks h;
    h.polyMaterial = [](const LightAtmosObject&, const Polygon&, i32,
                        void*) -> const LightAtmosPolyMaterial* { return &mat; };
    SetLightAtmosHooks(h);
    TestMesh m(3, 1);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.flags529 = 0x80;                           // sign bit -> use frame word
    o.frameLightWord376 = 0x100FF;               // bit16 | lo=255, hi=0
    LightAtmosBuildObjectCache(o, 0);
    CHECK_EQ((int)m.verts[0].lightIdx, 199);     // the recompute arm ran
    SetLightAtmosHooks(LightAtmosHooks{});
}

// ---------------------------------------------------------------------------
// 0x5c7da4 — RemoveCacheEntry list semantics.
// ---------------------------------------------------------------------------
TEST(LightAtmos, RemoveCacheEntryList) {
    LightAtmosObject o;
    int k1, k2, k3;
    auto* n3 = new LightCacheNode{&k3, nullptr};
    auto* n2 = new LightCacheNode{&k2, n3};
    auto* n1 = new LightCacheNode{&k1, n2};
    o.cacheList = n1;

    // missing key: no-op
    int kx;
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, &kx), 1);
    CHECK(o.cacheList == n1);

    // middle unlink (scan arm)
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, &k2), 1);
    CHECK(o.cacheList == n1);
    CHECK(n1->next == n3);

    // head unlink
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, &k1), 1);
    CHECK(o.cacheList == n3);

    // key==null: free the whole list
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, nullptr), 1);
    CHECK(o.cacheList == nullptr);

    // empty-list arms
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, nullptr), 1);
    CHECK_EQ((int)LightAtmosRemoveCacheEntry(o, &k1), 1);
}

// ---------------------------------------------------------------------------
// 0x5c886c — RefreshAllObjects walks + serial; 0x5add1c trigger arms.
// ---------------------------------------------------------------------------
TEST(LightAtmos, RefreshInvalidateThenLazyRebuild) {
    LightAtmosResetAll();
    Ambient(200.0f, 200.0f, 200.0f);
    TestMesh m(4);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.cacheList = new LightCacheNode{&m, nullptr};
    LightAtmosRegisterObject(&o);
    LightAtmosBuildObjectCache(o, 0);            // serial 0 cache
    CHECK_EQ((int)m.verts[0].lightIdx, 200);

    // Brightness changed: new ambient + RefreshAllObjects(1) (the force=1 arm).
    Ambient(80.0f, 90.0f, 100.0f);
    u8 res = LightAtmosRefreshAllObjects(1, 5);
    CHECK_EQ((int)res, 1);                       // RemoveCacheEntry returned 1
    CHECK_EQ(LightAtmos().rebuildSerial, 1u);    // ++dword_64A064
    CHECK_EQ((int)LightAtmos().invalidated, 1);  // byte_64A068 = 1
    CHECK(o.cacheList == nullptr);               // the cache list was freed
    CHECK_EQ((int)m.verts[0].lightIdx, 200);     // NOT yet relit (lazy)

    // The draw walk relights it (on-screen arm, within budget).
    LightAtmosBeginUniverseFrame();
    CHECK_EQ((int)LightAtmosEnsureNodeLit(o, false, true, 6), 1);
    CHECK_EQ((int)m.verts[0].lightIdx, 88);      // gray(80,90,100)
    CHECK_EQ(LightAtmos().onscreenSpent, 4u);    // += vertexCount
    CHECK_EQ(o.cacheSerial, 1u);

    // Same frame, serial now current: no second rebuild.
    CHECK_EQ((int)LightAtmosEnsureNodeLit(o, false, true, 6), 0);
    CHECK_EQ(LightAtmos().onscreenSpent, 4u);

    LightAtmosEndUniverseFrame();                // frame end clears byte_64A068
    CHECK_EQ((int)LightAtmos().invalidated, 0);
    LightAtmosUnregisterObject(&o);
}

TEST(LightAtmos, RefreshEagerForceTwoRebuildsImmediately) {
    LightAtmosResetAll();
    Ambient(120.0f, 90.0f, 60.0f);
    TestMesh m(2);
    LightAtmosObject o;
    o.geom = &m.geom;
    LightAtmosRegisterObject(&o);
    u8 res = LightAtmosRefreshAllObjects(2, 9);  // force > 1: eager walk
    CHECK_EQ((int)res, 1);                       // BuildObjectCache returned 1
    CHECK_EQ((int)m.verts[0].lightIdx, 95);      // gray(120,90,60)
    CHECK_EQ((int)LightAtmos().invalidated, 0);  // no invalidate flag
    CHECK_EQ(o.lastBuildFrame, 9);
    LightAtmosUnregisterObject(&o);
}

// force==0 (the soft-window arm: BlendBandLighting passes dword_62D4E8 == 0):
// no walk runs, but the serial bump still marks every cache stale — the lazy
// draw-walk arm relights on the next frame. EXACTLY the original.
TEST(LightAtmos, RefreshForceZeroStillBumpsSerial) {
    LightAtmosResetAll();
    Ambient(200.0f, 200.0f, 200.0f);
    TestMesh m(3);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.cacheList = new LightCacheNode{&m, nullptr};
    LightAtmosRegisterObject(&o);
    LightAtmosBuildObjectCache(o, 0);

    Ambient(40.0f, 40.0f, 40.0f);
    u8 res = LightAtmosRefreshAllObjects(0, 1);
    CHECK_EQ((int)res, 0);                       // returns the incoming force
    CHECK_EQ(LightAtmos().rebuildSerial, 1u);    // ALWAYS bumped
    CHECK(o.cacheList != nullptr);               // NO invalidate walk
    CHECK_EQ((int)LightAtmos().invalidated, 0);

    LightAtmosBeginUniverseFrame();
    CHECK_EQ((int)LightAtmosEnsureNodeLit(o, false, true, 2), 1);
    CHECK_EQ((int)m.verts[0].lightIdx, 40);
    LightAtmosRemoveCacheEntry(o, nullptr);
    LightAtmosUnregisterObject(&o);
}

// relightAlways (byte_649D54) opens the walk arms even at force 0.
TEST(LightAtmos, RelightAlwaysOpensWalk) {
    LightAtmosResetAll();
    LightAtmos().relightAlways = 1;
    TestMesh m(1);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.cacheList = new LightCacheNode{&m, nullptr};
    LightAtmosRegisterObject(&o);
    LightAtmosRefreshAllObjects(0, 0);           // force 0 + relightAlways
    CHECK(o.cacheList == nullptr);               // the invalidate walk ran
    CHECK_EQ((int)LightAtmos().invalidated, 1);
    LightAtmosUnregisterObject(&o);
}

TEST(LightAtmos, BudgetsGateTheLazyArms) {
    LightAtmosResetAll();
    Ambient(40.0f, 40.0f, 40.0f);
    TestMesh mBig(600);                          // exceeds both budgets alone
    TestMesh mSmall(8);
    LightAtmosObject big, small;
    big.geom = &mBig.geom;
    small.geom = &mSmall.geom;
    LightAtmosRefreshAllObjects(1, 0);           // make everything stale
    LightAtmosBeginUniverseFrame();

    // On-screen: the FIRST stale node passes (512 > 0), spends its full count.
    CHECK_EQ((int)LightAtmosEnsureNodeLit(big, false, true, 1), 1);
    CHECK_EQ(LightAtmos().onscreenSpent, 600u);
    // Budget exhausted (512 > 600 fails): the next node stays stale this frame.
    CHECK_EQ((int)LightAtmosEnsureNodeLit(small, false, true, 1), 0);
    CHECK_EQ((int)mSmall.verts[0].lightIdx, 0);

    // Culled arm: its own 128 budget; sets flags528 bit2 before the rebuild.
    CHECK_EQ((int)LightAtmosEnsureNodeLit(small, false, false, 1), 1);
    CHECK_EQ(LightAtmos().offscreenSpent, 8u);
    CHECK((small.flags528 & 4) != 0);
    CHECK_EQ((int)mSmall.verts[0].lightIdx, 40);

    // Next frame the counters reset (BeginUniverseFrame).
    LightAtmosBeginUniverseFrame();
    CHECK_EQ(LightAtmos().onscreenSpent, 0u);
    CHECK_EQ(LightAtmos().offscreenSpent, 0u);
}

TEST(LightAtmos, FreshFrameArmAndTypeGates) {
    LightAtmosResetAll();
    Ambient(40.0f, 40.0f, 40.0f);
    TestMesh m(2);
    LightAtmosObject o;
    o.geom = &m.geom;
    o.cacheSerial = LightAtmos().rebuildSerial;  // current serial...
    LightAtmos().invalidated = 1;                // ...but byte_64A068 set
    CHECK_EQ((int)LightAtmosEnsureNodeLit(o, true, true, 0), 1);
    CHECK_EQ((int)m.verts[0].lightIdx, 40);

    // type byte != 4 and the floor node are excluded from every arm.
    LightAtmosObject notLit;
    notLit.geom = &m.geom;
    notLit.type533 = 5;
    CHECK_EQ((int)LightAtmosEnsureNodeLit(notLit, true, true, 0), 0);
    LightAtmosObject floorNode;
    floorNode.geom = &m.geom;
    floorNode.isFloorNode = true;
    CHECK_EQ((int)LightAtmosEnsureNodeLit(floorNode, true, true, 0), 0);
}

// The floor arm of RefreshAllObjects: when a floor is bound (dword_64A028),
// Floor_BuildTilePolys' result becomes the return value.
TEST(LightAtmos, FloorHookResultPropagates) {
    LightAtmosResetAll();
    int floorRec = 0;
    LightAtmosHooks h;
    h.floor = &floorRec;
    h.floorBuildTilePolys = [](void*) -> u8 { return 7; };
    SetLightAtmosHooks(h);
    CHECK_EQ((int)LightAtmosRefreshAllObjects(0, 0), 7);
    SetLightAtmosHooks(LightAtmosHooks{});
}

// Registry hygiene: register is idempotent; unregister removes.
TEST(LightAtmos, RegistryIdempotent) {
    LightAtmosResetAll();
    LightAtmosObject a, b;
    LightAtmosRegisterObject(&a);
    LightAtmosRegisterObject(&a);
    LightAtmosRegisterObject(&b);
    CHECK_EQ(LightAtmosRegisteredCount(), 2);
    LightAtmosUnregisterObject(&a);
    CHECK_EQ(LightAtmosRegisteredCount(), 1);
    LightAtmosUnregisterObject(&a);              // double-unregister: no-op
    CHECK_EQ(LightAtmosRegisteredCount(), 1);
    LightAtmosResetAll();
    CHECK_EQ(LightAtmosRegisteredCount(), 0);
}
