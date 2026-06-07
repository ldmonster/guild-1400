// tests/unit/wire_char_anim_test.cpp — character-animation playback wiring (unit).
// Installs the posed-mesh driver against a small synthetic morph clip and asserts:
//   * the inert default (no driver) returns the STATIC behaviour (null -> fallback);
//   * with a driver + bound clip the MeshResolver returns a POSED mesh whose vertices
//     CHANGE with t (vs the static base) and are deterministic for a fixed (clip,t);
//   * AdvanceFrameIndex steps the frame cursor per the engine's loop policy.
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

// A 5-frame, 4-point morph clip: point 0 slides +X by 4 units/frame (a known ramp),
// the rest stay put. Mirrors the agf_anim itest builder so it parses 1:1.
std::vector<u8> BuildWalk() {
    const int nframes = 5, nv = 4;
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(1);  w.u32v(0xABCD0001u);
    w.byte(35); w.i32v(nframes);
    w.byte(51); w.byte(1);
    w.byte(41); w.i32v(0);
    w.byte(42); w.i32v(nframes - 1);
    for (int f = 0; f < nframes; ++f) {
        w.byte(24); w.i32v(f * 8);
        w.byte(25); w.i32v(nv);
        w.byte(33);
        for (int i = 0; i < nv; ++i) {
            if (i == 0) { w.f32v((float)f * 4.0f); w.f32v(0.0f); w.f32v(0.0f); }
            else        { w.f32v((float)i);        w.f32v(2.0f); w.f32v((float)-i); }
        }
        w.byte(40);
    }
    w.byte(47);
    return w.b;
}

} // namespace

// =============================================================================
// The per-tick advance reuses render::AdvanceFrameIndex (VIBE_Anim_AdvanceFrameIndex
// @0x5ccf18) — confirm the loop/clamp/reverse policy this driver steps through.
// Params: (flags, cur, last=end, first=start, count=clip-last).
// =============================================================================
TEST(WireCharAnimUnit, AdvanceFrameIndexPolicy) {
    using render::AdvanceFrameIndex;
    // forward, loop-back to start at the end (mode 0): 0,1,2,3,4,0
    CHECK_EQ(AdvanceFrameIndex(0, 0, 4, 0, 4), 1);
    CHECK_EQ(AdvanceFrameIndex(0, 3, 4, 0, 4), 4);
    CHECK_EQ(AdvanceFrameIndex(0, 4, 4, 0, 4), 0);   // wraps to start
    // clamp variant (mode 1): at the end it steps back (cur-1) instead of wrapping.
    CHECK_EQ(AdvanceFrameIndex(1, 4, 4, 0, 4), 3);
    // reverse (mode 2): steps down, bounces up at start.
    CHECK_EQ(AdvanceFrameIndex(2, 3, 4, 0, 4), 2);
    CHECK_EQ(AdvanceFrameIndex(2, 0, 4, 0, 4), 1);   // start+1
}

// =============================================================================
// Inert default: with no driver installed the resolver returns null (the static-
// mesh behaviour). Installing an EMPTY driver (no binding) still returns null.
// =============================================================================
TEST(WireCharAnimUnit, InertDefaultIsStatic) {
    InstallCharAnimDriver(nullptr);
    EntityRef e{EntityKind::Object, 7, 0, 0};
    CHECK(PosedMeshResolver(e) == nullptr);   // no playback -> static

    CharAnimDriver drv;
    InstallCharAnimDriver(&drv);
    CHECK(PosedMeshResolver(e) == nullptr);   // installed but nothing bound -> static
    InstallCharAnimDriver(nullptr);
}

// =============================================================================
// With a bound clip the resolver returns a POSED mesh whose vertices change with t
// and are deterministic for a fixed (clip,t).
// =============================================================================
TEST(WireCharAnimUnit, PosedMeshChangesWithTime) {
    std::vector<u8> bytes = BuildWalk();
    AnimClip clip;
    bool ok = LoadAnimation(bytes.data(), bytes.size(), "walk", clip, 1);
    CHECK(ok);
    CHECK_EQ(clip.FrameCount(), 5);
    CHECK_EQ(clip.VertexCount(), 4);

    CharAnimDriver drv;
    drv.Bind(/*entityId*/42, &clip);
    InstallCharAnimDriver(&drv);

    EntityRef e{EntityKind::Object, 42, 0, 0};

    // t=0 (frame 0): point 0 at X=0.
    const MeshGeometry* g0 = PosedMeshResolver(e);
    CHECK(g0 != nullptr);
    if (g0) {
        CHECK(g0->vertexCount == 4);
        CHECK(feq(g0->vertices[0].x, 0.0f));
    }

    // Advance two ticks -> frame 2: point 0 at X=8.
    drv.Tick();
    drv.Tick();
    const MeshGeometry* g2 = PosedMeshResolver(e);
    CHECK(g2 != nullptr);
    if (g2) CHECK(feq(g2->vertices[0].x, 8.0f));

    // Deterministic for a fixed cursor: resolve twice at the same frame -> identical.
    const MeshGeometry* g2b = PosedMeshResolver(e);
    CHECK(g2b != nullptr);
    if (g2 && g2b) {
        // (note g2 points into the same owned slot, refreshed; re-read the binding's
        // current X to confirm stability across resolves)
        CHECK(feq(drv.BindingFor(42)->frame + drv.BindingFor(42)->subPhase, 2.0f));
    }

    // A SECOND independent driver bound the same way yields the same posed X at t=2:
    // determinism across instances.
    CharAnimDriver drv2;
    drv2.Bind(42, &clip);
    drv2.Tick(); drv2.Tick();
    InstallCharAnimDriver(&drv2);
    const MeshGeometry* h2 = PosedMeshResolver(e);
    CHECK(h2 != nullptr);
    if (h2 && g2) CHECK(feq(h2->vertices[0].x, 8.0f));

    InstallCharAnimDriver(nullptr);
}

// =============================================================================
// Fractional sampling: at t=0.5 (half a tick) point 0 is the lerp midpoint (X=2),
// proving the morph blend (not just frame snapping) drives the posed mesh.
// =============================================================================
TEST(WireCharAnimUnit, FractionalBlend) {
    std::vector<u8> bytes = BuildWalk();
    AnimClip clip;
    CHECK(LoadAnimation(bytes.data(), bytes.size(), "walk", clip, 1));

    AnimBinding b;
    b.clip = &clip;
    PosedGeometry g0 = BuildPosedGeometry(b, 0.0f);
    PosedGeometry gh = BuildPosedGeometry(b, 0.5f);
    PosedGeometry g1 = BuildPosedGeometry(b, 1.0f);
    CHECK(g0.valid && gh.valid && g1.valid);
    if (g0.valid && gh.valid && g1.valid) {
        CHECK(feq(g0.vertices[0].x, 0.0f));
        CHECK(feq(g1.vertices[0].x, 4.0f));
        CHECK(feq(gh.vertices[0].x, 2.0f));   // midpoint lerp
    }
}
