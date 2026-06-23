#include "render/scene_lights.h"
#include "render/object_light_shade.h"  // LightMeshVertices (the handoff consumer)
#include "render/light.h"               // BuildFalloffLUT

#include "tests/framework/test.h"

#include <cmath>
#include <vector>

// =============================================================================
// SceneLights — golden tests for the scene-light COLLECTION + per-object
// affected-light CULL (VIBE_Light_CollectAffectedObject @0x5c80a0 / the two-pass
// collect driver in VIBE_Light_BuildObjectCache @0x5c8218), and the handoff into
// the wave-7 per-vertex core (LightMeshVertices @0x5c6f90). No assets.
// =============================================================================
using namespace guild;
using guild::render::SceneLightNode;
using guild::render::LitObjectCullParams;
using guild::render::SceneLightSet;
using guild::render::CollectedObjectLights;
using guild::render::LightAffectsObject;
using guild::render::CollectObjectLights;
using guild::render::CullForObject;
using guild::render::kSunLightType;

static SceneLightNode MakePoint(float x, float y, float z, float range,
                                float intensity) {
    SceneLightNode L;
    L.type = 1;  // any non-7 == point
    L.pos[0] = x; L.pos[1] = y; L.pos[2] = z;
    L.color[0] = 1.0f; L.color[1] = 1.0f; L.color[2] = 1.0f;
    L.range = range;
    L.intensity = intensity;
    L.rangeParam = 1.0f;
    return L;
}

static SceneLightNode MakeSun(float intensity, u32 flags) {
    SceneLightNode L;
    L.type = kSunLightType;  // 7
    L.dir[0] = 0.0f; L.dir[1] = -1.0f; L.dir[2] = 0.0f;
    L.color[0] = 1.0f; L.color[1] = 0.9f; L.color[2] = 0.8f;
    L.intensity = intensity;
    L.flags = flags;
    return L;
}

// -----------------------------------------------------------------------------
// CULL PREDICATE (CollectAffectedObject @0x5c80a0, count pass)
// -----------------------------------------------------------------------------

// (a) Point in range -> affected. cullRadius(L.range) + objRadius(O.cullRadius) > dist.
TEST(SceneLights, PointInRangeAffects) {
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0; O.cullRadius = 1.0f;
    // dist = 5 ; L.range(4) + O.cullRadius(1) = 5 ; 5 <= 5 -> NOT affected (the <=).
    SceneLightNode at5 = MakePoint(5, 0, 0, 4.0f, 1.0f);
    CHECK(!LightAffectsObject(at5, O));
    // dist = 5 ; L.range(4.5) + 1 = 5.5 > 5 -> affected.
    SceneLightNode at5b = MakePoint(5, 0, 0, 4.5f, 1.0f);
    CHECK(LightAffectsObject(at5b, O));
}

// (b) Point out of range -> not affected (cullRadius + objRadius <= dist).
TEST(SceneLights, PointOutOfRangeCulled) {
    LitObjectCullParams O; O.cullRadius = 0.5f;
    SceneLightNode L = MakePoint(100, 0, 0, 1.0f, 1.0f);  // dist=100, 1.5 <= 100
    CHECK(!LightAffectsObject(L, O));
}

// (c) Zero-intensity point/sun -> never affected (the `0.0 == +148` early-out).
TEST(SceneLights, ZeroIntensityCulled) {
    LitObjectCullParams O; O.cullRadius = 100.0f;
    SceneLightNode p = MakePoint(0, 0, 0, 1000.0f, 0.0f);  // overlapping but off
    CHECK(!LightAffectsObject(p, O));
    SceneLightNode s = MakeSun(0.0f, 0);
    CHECK(!LightAffectsObject(s, O));
}

// (d) Sun: affected when intensity!=0 and (flags & 0x10)==0; culled by the 0x10 bit.
TEST(SceneLights, SunFlagCull) {
    LitObjectCullParams O;
    CHECK(LightAffectsObject(MakeSun(1.0f, 0x00), O));
    CHECK(!LightAffectsObject(MakeSun(1.0f, 0x10), O));   // disable bit set
    CHECK(LightAffectsObject(MakeSun(1.0f, 0x0F), O));    // other bits ignored
    CHECK(LightAffectsObject(MakeSun(1.0f, 0x20), O));    // other bits ignored
}

// (e) ENGINE QUIRK (1:1): the sun cull IGNORES position/distance entirely — a sun
// at any distance with non-zero intensity affects the object (no range term).
TEST(SceneLights, SunIgnoresDistance) {
    LitObjectCullParams O; O.pos[0] = 0; O.cullRadius = 0.0f;
    SceneLightNode farSun = MakeSun(1.0f, 0);
    farSun.pos[0] = 1.0e9f;  // absurdly far; sun arm never reads position
    CHECK(LightAffectsObject(farSun, O));
}

// -----------------------------------------------------------------------------
// COLLECT DRIVER (the two-pass walk in BuildObjectCache @0x5c8218)
// -----------------------------------------------------------------------------

// (f) Collect partitions point vs sun; out-of-range points dropped; order preserved.
TEST(SceneLights, CollectPartitionsAndOrders) {
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0; O.cullRadius = 1.0f;
    std::vector<SceneLightNode> scene;
    scene.push_back(MakePoint(0, 0, 0, 10.0f, 2.0f));     // [0] near -> affected
    scene.push_back(MakeSun(1.0f, 0));                    // [1] sun -> affected
    scene.push_back(MakePoint(1000, 0, 0, 1.0f, 1.0f));   // [2] far -> culled
    scene.push_back(MakePoint(2, 0, 0, 10.0f, 0.5f));     // [3] near -> affected

    CollectedObjectLights c = CollectObjectLights(scene, O);
    CHECK_EQ((int)c.pointLights.size(), 2);
    CHECK(c.hasSun);
    // Order preserved: first point is scene[0] (intensity 2), second is scene[3] (0.5).
    CHECK(std::fabs(c.pointLights[0].intensity - 2.0f) < 1e-6f);
    CHECK(std::fabs(c.pointLights[1].intensity - 0.5f) < 1e-6f);
    // Field mapping (scene[0]).
    CHECK(std::fabs(c.pointLights[0].range - 10.0f) < 1e-6f);
    CHECK(std::fabs(c.pointLights[0].pos[0] - 0.0f) < 1e-6f);
}

// (g) Only the FIRST affected sun is surfaced (LightMeshVertices is single-sun;
//     ApplyToCachedVertices @0x5c704d locates the first type-7 entry).
TEST(SceneLights, FirstSunWins) {
    LitObjectCullParams O;
    std::vector<SceneLightNode> scene;
    SceneLightNode s1 = MakeSun(3.0f, 0); s1.color[0] = 0.1f;
    SceneLightNode s2 = MakeSun(7.0f, 0); s2.color[0] = 0.9f;
    scene.push_back(s1);
    scene.push_back(s2);
    CollectedObjectLights c = CollectObjectLights(scene, O);
    CHECK(c.hasSun);
    CHECK(std::fabs(c.sunIntensity - 3.0f) < 1e-6f);     // first sun
    CHECK(std::fabs(c.sunColor[0] - 0.1f) < 1e-6f);
}

// (h) Empty scene / all-culled -> no point lights, no sun.
TEST(SceneLights, EmptyAndAllCulled) {
    LitObjectCullParams O; O.cullRadius = 0.0f;
    CollectedObjectLights e = CollectObjectLights({}, O);
    CHECK_EQ((int)e.pointLights.size(), 0);
    CHECK(!e.hasSun);

    std::vector<SceneLightNode> far;
    far.push_back(MakePoint(1e6f, 0, 0, 1.0f, 1.0f));
    CollectedObjectLights c = CollectObjectLights(far, O);
    CHECK_EQ((int)c.pointLights.size(), 0);
    CHECK(!c.hasSun);
}

// -----------------------------------------------------------------------------
// HANDOFF: collect-once SceneLightSet -> per-object cull -> LightMeshVertices.
// This is the exact CityView3D object-arm sequence the orchestrator wires.
// -----------------------------------------------------------------------------
TEST(SceneLights, FeedsLightMeshVertices) {
    // Build the per-frame scene light set ONCE.
    SceneLightSet set;
    set.lights.push_back(MakePoint(0, 0, 10, 100.0f, 5.0f));  // a near point light
    set.lights.push_back(MakeSun(1.0f, 0));                   // the sun

    // Per object: cull, then light its vertices.
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0; O.cullRadius = 1.0f;
    CollectedObjectLights c = CullForObject(set, O);
    CHECK_EQ((int)c.pointLights.size(), 1);
    CHECK(c.hasSun);

    float lut[1024];
    render::BuildFalloffLUT(lut);

    render::MeshLightVertex v;
    v.vpos[0] = 0; v.vpos[1] = 0; v.vpos[2] = 0;
    v.vnormal[0] = 0; v.vnormal[1] = 0; v.vnormal[2] = -1;  // faces the +z light

    const float ambient[3] = {200.0f, 200.0f, 200.0f};
    render::ShadeBytes out{};
    // The handoff call: collected sun + point lights feed LightMeshVertices.
    render::LightMeshVertices(&v, 1, ambient,
                              c.sunDir, c.sunColor, c.hasSun ? c.sunIntensity : 0.0f,
                              c.pointLights.data(), (int)c.pointLights.size(),
                              /*objScale=*/1.0f, lut, &out);
    // Must produce a valid shade (ambient-dominant given the zero static ramp; the
    // call path is exercised end-to-end). The ambient floor (200) is preserved.
    CHECK((int)out.r >= 200);
    CHECK((int)out.g >= 200);
    CHECK((int)out.b >= 200);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / boundary coverage (ASAN+UBSAN).
// =============================================================================

// (w1) Object exactly AT a point light's origin (dist == 0): sqrt(0) is well
//      defined (no UB), and 0 < range+objRadius -> affected. No OOB on the walk.
TEST(SceneLights, HardenObjectAtLightOriginDistZero) {
    LitObjectCullParams O; O.pos[0] = 7; O.pos[1] = -3; O.pos[2] = 2; O.cullRadius = 0.0f;
    SceneLightNode L = MakePoint(7, -3, 2, 0.001f, 1.0f);  // same position -> dist 0
    // dist 0 ; range(0.001) + objR(0) = 0.001 ; 0.001 <= 0 ? no -> affected.
    CHECK(LightAffectsObject(L, O));
    // Even a zero-range light at dist 0 is culled by the `<=` (0 <= 0).
    SceneLightNode z = MakePoint(7, -3, 2, 0.0f, 1.0f);
    CHECK(!LightAffectsObject(z, O));
}

// (w2) Zero lights: collect over an empty scene + an empty SceneLightSet -> no
//      point lights, no sun, no OOB on the (empty) vector walk.
TEST(SceneLights, HardenZeroLights) {
    LitObjectCullParams O; O.cullRadius = 5.0f;
    std::vector<SceneLightNode> none;
    CollectedObjectLights c = CollectObjectLights(none, O);
    CHECK_EQ((int)c.pointLights.size(), 0);
    CHECK(!c.hasSun);

    SceneLightSet empty;                    // .lights default-empty
    CollectedObjectLights c2 = CullForObject(empty, O);
    CHECK_EQ((int)c2.pointLights.size(), 0);
    CHECK(!c2.hasSun);
}

// (w3) A large light list (capacity stress): every affected point light is
//      collected in walk order; the growing output vector stays in bounds.
TEST(SceneLights, HardenManyLights) {
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0; O.cullRadius = 1.0f;
    std::vector<SceneLightNode> scene;
    const int N = 1024;
    for (int i = 0; i < N; ++i)
        scene.push_back(MakePoint(0, 0, 0, 100.0f, 1.0f));  // all overlap the object
    scene.push_back(MakeSun(1.0f, 0));                      // one sun at the end
    CollectedObjectLights c = CollectObjectLights(scene, O);
    CHECK_EQ((int)c.pointLights.size(), N);                 // all N points affect
    CHECK(c.hasSun);
}

// =============================================================================
// WAVE-13 1:1 CONSTANT / QUIRK PINNING — pin the exact recovered values the cull
// reads (scene-lights-wave8.md): the sun type tag, the 0x10 sun-disable bit, and
// the mixed-unit operand quirk (L.range linear + O.cullRadius squared vs linear
// distance). All values trace to the progress doc — none invented.
// =============================================================================

// (W13-a) The directional "sun" type tag is exactly 7 (object byte +533 == 7).
TEST(SceneLights, ConstSunTypeIs7) {
    CHECK_EQ(kSunLightType, 7);
    // A type-7 light takes the sun arm (no distance term); any other type is a point.
    LitObjectCullParams O; O.cullRadius = 0.0f;
    SceneLightNode sun = MakeSun(1.0f, 0);
    sun.type = 7;
    CHECK(LightAffectsObject(sun, O));   // sun arm: intensity!=0, no 0x10 -> affected
    SceneLightNode notSun = MakeSun(1.0f, 0);
    notSun.type = 6;                     // not the sun tag -> point arm
    notSun.pos[0] = 1.0e9f; notSun.range = 1.0f;  // far point -> culled by distance
    CHECK(!LightAffectsObject(notSun, O));
}

// (W13-b) The sun-disable bit is exactly 0x10 in the flags low byte (+529 & 0x10);
// every OTHER bit is ignored by the cull (only 0x10 disables).
TEST(SceneLights, ConstSunDisableBitIs0x10) {
    LitObjectCullParams O;
    for (u32 bit = 0; bit < 8; ++bit) {
        const u32 flags = 1u << bit;
        const bool affected = LightAffectsObject(MakeSun(1.0f, flags), O);
        // bit 4 (0x10) disables; every other single bit leaves the sun affecting.
        CHECK_EQ(affected, (flags & 0x10u) == 0u);
    }
}

// (W13-c) The mixed-unit point cull QUIRK (1:1): the comparison is
//   L.range(LINEAR) + O.cullRadius(SQUARED) <= dist(LINEAR).
// Pin it with an object whose SQUARED bound is what tips the boundary — proving the
// operand is added verbatim (not sqrt'd), exactly as gilde.exe stores it (+484).
TEST(SceneLights, ConstMixedUnitCullQuirk) {
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0;
    O.cullRadius = 16.0f;                          // a SQUARED bound (radius 4)
    // dist = 20 ; L.range(linear) 4 + O.cullRadius 16 = 20 ; 20 <= 20 -> NOT affected.
    SceneLightNode at20 = MakePoint(20, 0, 0, 4.0f, 1.0f);
    CHECK(!LightAffectsObject(at20, O));
    // Bump the linear range a hair: 4.5 + 16 = 20.5 > 20 -> affected. Confirms the
    // squared bound is consumed as-is (a sqrt'd 4.0 would never reach 20).
    SceneLightNode at20b = MakePoint(20, 0, 0, 4.5f, 1.0f);
    CHECK(LightAffectsObject(at20b, O));
}

// (W15) CollectAffectedObject @0x5c80a0 point-arm distance metric: the live
// decompile computes v10 = sqrt(v7*v7 + v8*v8 + v9*v9) over ALL THREE world-delta
// axes (a1+472/476/480 minus the object's a2[0]+472/476/480). Pin a 3-4-12 triple
// (sqrt(9+16+144) = 13) so the metric can't silently regress to a 2D / squared form.
TEST(SceneLights, ConstPointDistanceIs3DEuclidean) {
    LitObjectCullParams O; O.pos[0] = 0; O.pos[1] = 0; O.pos[2] = 0; O.cullRadius = 0.0f;
    // Light at (3,4,12): true 3D distance = 13. range 12.9 + 0 = 12.9 < 13 -> NOT affected.
    SceneLightNode below = MakePoint(3, 4, 12, 12.9f, 1.0f);
    CHECK(!LightAffectsObject(below, O));
    // range 13.1 + 0 = 13.1 > 13 -> affected. (A 2D x/z metric would give sqrt(9+144)=12.37
    // and a squared metric 169; both would flip these two assertions.)
    SceneLightNode above = MakePoint(3, 4, 12, 13.1f, 1.0f);
    CHECK(LightAffectsObject(above, O));
    // Confirm the y axis genuinely participates: move all distance into y only.
    SceneLightNode yOnly = MakePoint(0, 13, 0, 12.9f, 1.0f);
    CHECK(!LightAffectsObject(yOnly, O));
    SceneLightNode yOnlyAff = MakePoint(0, 13, 0, 13.1f, 1.0f);
    CHECK(LightAffectsObject(yOnlyAff, O));
}

// (i) Disabled sun (flags 0x10) does NOT feed LightMeshVertices as a sun.
TEST(SceneLights, DisabledSunNotFed) {
    SceneLightSet set;
    set.lights.push_back(MakeSun(1.0f, 0x10));  // disabled
    LitObjectCullParams O;
    CollectedObjectLights c = CullForObject(set, O);
    CHECK(!c.hasSun);
    CHECK_EQ((int)c.pointLights.size(), 0);
}
