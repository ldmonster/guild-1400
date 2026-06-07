// tests/unit/wire_render_bridge_test.cpp — UNIT: prove InstallRealRenderBridge
// swaps the inert CharRenderHooks slots (pivot / mesh-root-translation /
// texture-set-select) for the REAL reconstructed leaves, observed as a behaviour
// change on a synthetic input run through the Character attach/head-variant leaves.
#include "test.h"

#include "play/wire_render_bridge.h"
#include "sim/character_render.h"
#include "sim/object_lifecycle8.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

// Build a faithful mesh FRAME buffer (the actor+52 mesh handle). The engine frame
// is addressed by float index: pivot at 27..29 / 19..21, root translation at
// 33..35 (bytes 132/136/140), 3x3 rotation at 99..109, parent pointer embedded at
// byte 504 (kept null => leaf frame, no parent walk). 200 floats covers byte 504.
std::vector<float> MakeMeshFrame() {
    std::vector<float> f(200, 0.0f);
    // A non-identity 3x3 (90-degree yaw about Y): rows at idx 99/103/107,
    // 100/104/108, 101/105/109.
    // row0 = ( 0,0,1), row1 = (0,1,0), row2 = (-1,0,0)
    f[99] = 0.0f;  f[103] = 0.0f; f[107] = 1.0f;
    f[100] = 0.0f; f[104] = 1.0f; f[108] = 0.0f;
    f[101] = -1.0f; f[105] = 0.0f; f[109] = 0.0f;
    // pivot offset (27..29) and the two translation terms (19..21 / 30..32).
    f[27] = 2.0f; f[28] = 0.0f; f[29] = -1.0f;
    f[19] = 1.0f; f[20] = 0.5f; f[21] = 0.25f;
    f[30] = 0.0f; f[31] = 0.0f; f[32] = 0.0f;
    // mesh ROOT translation (33..35 == bytes 132/136/140).
    f[33] = 100.0f; f[34] = 50.0f; f[35] = 25.0f;
    // parent link at byte 504 stays zero (null) -> no parent chain walk.
    return f;
}

bool Differs(const float a[3], const float b[3]) {
    return std::fabs(a[0] - b[0]) > 1e-4f ||
           std::fabs(a[1] - b[1]) > 1e-4f ||
           std::fabs(a[2] - b[2]) > 1e-4f;
}

} // namespace

// --- pivot + mesh-root: inert default vs real bridge leaf -------------------
TEST(WireRenderBridge, AttachOffsetSwapsInertToReal) {
    play::UninstallRealRenderBridge();   // start from the inert default table
    CHECK(!play::RealRenderBridgeInstalled());

    std::vector<float> frame = MakeMeshFrame();

    sim::RenderActor actor{};
    actor.mesh = frame.data();
    actor.flagsA = 0;   // not sitting

    // INERT: pivot is identity-copy, mesh root translation is dropped (0). So
    // ComputeAttachOffset's offset is just the per-slot constant (slot 0 = {-3,63,36}).
    sim::AttachGeom inert{};
    sim::ComputeAttachOffset(&actor, /*slot=*/0, &inert);

    // The per-slot constant must come through unchanged under inert hooks.
    CHECK(std::fabs(inert.offset[0] - (-3.0f)) < 1e-4f);
    CHECK(std::fabs(inert.offset[1] - 63.0f) < 1e-4f);
    CHECK(std::fabs(inert.offset[2] - 36.0f) < 1e-4f);

    // NOW install the real bridge.
    play::InstallRealRenderBridge();
    CHECK(play::RealRenderBridgeInstalled());

    sim::AttachGeom real{};
    sim::ComputeAttachOffset(&actor, /*slot=*/0, &real);

    // The real pivot transforms the offset through the bone-chain pivot and then
    // the mesh root translation (100/50/25) is ADDED. The result must differ from
    // the inert (untransformed) offset.
    CHECK(Differs(real.offset, inert.offset));

    // The mesh root translation (100/50/25) makes the X component land well past
    // the inert -3 / 63 / 36 — proves the real root-translation read is wired.
    CHECK(real.offset[0] > 50.0f);   // -3 const + pivot-rotated + 100 root >> -3
    CHECK(real.offset[1] > 40.0f);   // 63 + ... + 50 root

    // The bridge accessor reproduces the exact mesh root translation read.
    float root[3] = {0, 0, 0};
    play::BridgeMeshRootTranslation(frame.data(), root);
    CHECK(std::fabs(root[0] - 100.0f) < 1e-4f);
    CHECK(std::fabs(root[1] - 50.0f) < 1e-4f);
    CHECK(std::fabs(root[2] - 25.0f) < 1e-4f);

    // Inert mesh-root accessor (null mesh) returns zero — the dropped translation.
    float z[3] = {9, 9, 9};
    play::BridgeMeshRootTranslation(nullptr, z);
    CHECK(z[0] == 0.0f && z[1] == 0.0f && z[2] == 0.0f);

    play::UninstallRealRenderBridge();
}

// --- pivot leaf vs identity directly ----------------------------------------
TEST(WireRenderBridge, PivotLeafIsNotIdentity) {
    std::vector<float> frame = MakeMeshFrame();
    const float in[3] = {3.0f, 4.0f, 5.0f};

    float out[3] = {0, 0, 0};
    play::BridgePointThroughPivot(frame.data(), in, out);

    // With the non-identity 3x3 + pivot + translation, the pivot leaf must NOT be
    // an identity copy (the inert default).
    CHECK(Differs(out, in));

    // Null mesh => inert identity (safe meshless path).
    float id[3] = {0, 0, 0};
    play::BridgePointThroughPivot(nullptr, in, id);
    CHECK(id[0] == in[0] && id[1] == in[1] && id[2] == in[2]);
}

// --- head-variant texture-set select: inert (0) vs real (applied) -----------
TEST(WireRenderBridge, HeadVariantSelectSwapsInertToReal) {
    // ApplyHeadVariant routes through CharRenderHooks::selectTextureSet on the
    // attached actor's scene node. Inert default returns 0; the real bridge runs
    // sim::ObjectSelectTextureSet which applies (returns 1) when the node passes
    // the +492 (kMesh) gate and the variant differs from the current set.
    sim::SceneNode8 node;          // raw[0x21C], zeroed
    node.d(492) = 0x1234;          // kMesh non-zero => passes the gate

    sim::RenderActor host{};
    sim::RenderActor attached{};
    attached.universe = (void*)0x1;        // not the wild universe -> variant write
    attached.mesh = &node;                 // the scene node the selector receives
    host.attached = &attached;
    host.id = 1;                           // variant = id % headCount

    const int headCount = 3;               // <=4 -> variant = id % headCount = 1
    const int activeMeshId = host.id;      // gate: equal -> selectTextureSet called

    // INERT default.
    play::UninstallRealRenderBridge();
    u8 inertResult = sim::ApplyHeadVariant(&host, headCount, activeMeshId);
    CHECK_EQ((int)inertResult, 0);         // inert selectTextureSet returns 0

    // REAL bridge.
    play::InstallRealRenderBridge();
    u8 realResult = sim::ApplyHeadVariant(&host, headCount, activeMeshId);
    // variant = 1; setCount = variant+1 = 2; cur sentinel = 0 != 1 -> apply -> 1.
    CHECK_EQ((int)realResult, 1);

    // A node that FAILS the +492 gate makes the real selector return 0 too,
    // proving we're actually running the real gate logic, not a constant.
    sim::SceneNode8 gateless;              // d(492) == 0
    attached.mesh = &gateless;
    u8 gatedOff = sim::ApplyHeadVariant(&host, headCount, activeMeshId);
    CHECK_EQ((int)gatedOff, 0);

    play::UninstallRealRenderBridge();
}
