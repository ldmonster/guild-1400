// tests/integration/wire_char_anim_itest.cpp — character-animation playback wired
// into a mesh resolver over several ticks (integration). Builds a synthetic morph
// clip + a matching static base mesh, advances the driver over a tick sequence, and
// asserts:
//   * WITHOUT playback the resolved mesh stays the STATIC base (no motion);
//   * WITH playback the resolved (posed) vertices MOVE across ticks, reusing the base
//     topology (same polygon count) and base UVs;
//   * the whole advance sequence is deterministic across reruns (byte-identical posed
//     vertex stream).
// No assets.
#include "test.h"

#include "play/wire_char_anim.h"
#include "render/agf_anim.h"
#include "render/geometry_types.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::render;

namespace {

bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

struct Builder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void i32v(i32 v) { u32v((u32)v); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// 6-frame, 6-point morph clip. Every point rises in Y by (frame * 0.5) (a known
// "stand up" ramp) so the posed bbox grows monotonically; X/Z stay put.
std::vector<u8> BuildRise() {
    const int nframes = 6, nv = 6;
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(1);  w.u32v(0xABCD0001u);   // magic guard (<= kMagicMax)
    w.byte(35); w.i32v(nframes);
    w.byte(51); w.byte(1);
    w.byte(41); w.i32v(0);
    w.byte(42); w.i32v(nframes - 1);
    for (int f = 0; f < nframes; ++f) {
        w.byte(24); w.i32v(8);          // uniform per-frame timing
        w.byte(25); w.i32v(nv);
        w.byte(33);
        for (int i = 0; i < nv; ++i) {
            w.f32v((float)i);                 // X stays
            w.f32v((float)f * 0.5f);          // Y rises with frame
            w.f32v((float)-i);                // Z stays
        }
        w.byte(40);
    }
    w.byte(47);
    return w.b;
}

// A static base mesh with the same vertex count as the morph clip, so the posed
// geometry reuses the base topology + UVs. Owns its storage.
struct BaseMesh {
    std::vector<Vertex>  verts;
    std::vector<Polygon> polys;
    MeshGeometry         geom{};
    MeshGeometry* View() {
        geom.vertices = verts.data();
        geom.polygons = polys.data();
        geom.polyCount = (i32)polys.size();
        geom.polyCap = (i32)polys.size();
        geom.vertexCount = (i32)verts.size();
        return &geom;
    }
    void build(int nv) {
        verts.assign((size_t)nv, Vertex{});
        for (int i = 0; i < nv; ++i) {
            verts[i].x = (float)i; verts[i].y = 0.0f; verts[i].z = (float)-i;
            verts[i].u = (float)i * 0.1f; verts[i].v = 0.25f;  // distinct UVs
        }
        // Two triangles over the first 4 verts (a real, non-sequential topology).
        polys.clear();
        Polygon p0{}; p0.v0 = &verts[0]; p0.v1 = &verts[1]; p0.v2 = &verts[2];
        Polygon p1{}; p1.v0 = &verts[2]; p1.v1 = &verts[3]; p1.v2 = &verts[0];
        polys.push_back(p0); polys.push_back(p1);
    }
};

// Capture the posed vertex positions for an entity at the current cursor.
std::vector<float> capture(CharAnimDriver& drv, const EntityRef& e) {
    std::vector<float> out;
    const MeshGeometry* g = PosedMeshResolver(e);
    if (!g) return out;
    for (int i = 0; i < g->vertexCount; ++i) {
        out.push_back(g->vertices[i].x);
        out.push_back(g->vertices[i].y);
        out.push_back(g->vertices[i].z);
    }
    return out;
}

} // namespace

// =============================================================================
// Without playback the entity's mesh is the static base (no motion across ticks).
// With playback the posed vertices move; topology/UVs come from the base.
// =============================================================================
TEST(WireCharAnimItest, PlaybackMovesOnlyWhenInstalled) {
    std::vector<u8> bytes = BuildRise();
    AnimClip clip;
    CHECK(LoadAnimation(bytes.data(), bytes.size(), "rise", clip, 1));
    CHECK_EQ(clip.VertexCount(), 6);

    BaseMesh base; base.build(6);
    MeshGeometry* baseGeom = base.View();
    EntityRef e{EntityKind::Person, 11, 0, 1};

    // --- No playback installed: resolver is inert -> caller would use static base.
    InstallCharAnimDriver(nullptr);
    CHECK(PosedMeshResolver(e) == nullptr);   // static path

    // --- Playback installed + clip bound (with base topology pass-through).
    CharAnimDriver drv;
    drv.Bind(11, &clip, baseGeom, /*mode*/0, /*stepPerTick*/1.0f);
    InstallCharAnimDriver(&drv);

    std::vector<float> f0 = capture(drv, e);   // frame 0: all Y == 0
    CHECK(!f0.empty());
    bool allZeroY = true;
    for (size_t i = 1; i < f0.size(); i += 3) if (!feq(f0[i], 0.0f)) allZeroY = false;
    CHECK(allZeroY);

    // Advance 3 ticks -> frame 3: Y == 1.5 for every point (the rise ramp).
    drv.Tick(); drv.Tick(); drv.Tick();
    std::vector<float> f3 = capture(drv, e);
    CHECK_EQ(f3.size(), f0.size());
    bool rose = false;
    for (size_t i = 1; i < f3.size(); i += 3) { if (feq(f3[i], 1.5f)) rose = true; else { rose = false; break; } }
    CHECK(rose);

    // The posed mesh reused the base topology + UVs.
    const MeshGeometry* g3 = PosedMeshResolver(e);
    CHECK(g3 != nullptr);
    if (g3) {
        CHECK_EQ(g3->polyCount, baseGeom->polyCount);     // 2 base tris, not synth
        CHECK(feq(g3->vertices[1].u, 0.1f));              // base UV carried through
    }

    InstallCharAnimDriver(nullptr);
}

// =============================================================================
// The full advance sequence is deterministic across reruns: two independent drivers
// stepped through the same tick schedule produce byte-identical posed vertices at
// every step.
// =============================================================================
TEST(WireCharAnimItest, DeterministicAcrossReruns) {
    std::vector<u8> bytes = BuildRise();
    AnimClip clip;
    CHECK(LoadAnimation(bytes.data(), bytes.size(), "rise", clip, 1));

    BaseMesh base; base.build(6);
    EntityRef e{EntityKind::Person, 99, 0, 1};

    auto runSequence = [&](std::vector<std::vector<float>>& frames) {
        BaseMesh b2; b2.build(6);
        CharAnimDriver drv;
        drv.Bind(99, &clip, b2.View(), 0, 1.0f);
        InstallCharAnimDriver(&drv);
        for (int t = 0; t < 8; ++t) {           // sweep past the loop wrap
            frames.push_back(capture(drv, e));
            drv.Tick();
        }
        InstallCharAnimDriver(nullptr);
    };

    std::vector<std::vector<float>> runA, runB;
    runSequence(runA);
    runSequence(runB);

    CHECK_EQ(runA.size(), runB.size());
    bool identical = (runA.size() == runB.size());
    for (size_t s = 0; s < runA.size() && identical; ++s) {
        if (runA[s].size() != runB[s].size()) { identical = false; break; }
        for (size_t k = 0; k < runA[s].size(); ++k)
            if (!feq(runA[s][k], runB[s][k], 0.0f)) { identical = false; break; }
    }
    CHECK(identical);

    // And the sequence actually MOVED (not a constant frame).
    bool moved = false;
    if (runA.size() >= 4 && runA[0].size() == runA[3].size())
        for (size_t k = 0; k < runA[0].size(); ++k)
            if (!feq(runA[0][k], runA[3][k])) { moved = true; break; }
    CHECK(moved);
}
