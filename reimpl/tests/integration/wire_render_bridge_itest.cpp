// tests/integration/wire_render_bridge_itest.cpp — INTEGRATION: drive a frame's
// worth of attached-actor PLACEMENT through the real Character attach leaf
// (VIBE_Character_ApplyAttachOffset @0x404964) and prove the bridge's REAL pivot +
// mesh-root leaves are actually invoked at the placement step — the seated world
// position differs from the inert (no-op) path.
//
// The placement step is: ApplyAttachOffset -> ComputeAttachOffset (real pivot +
// real mesh-root via the installed bridge) -> setObjectPosition(seated offset).
// We capture the seated offset by routing setObjectPosition into a recorder, while
// the geometry leaves come from the real bridge. With the inert default hooks the
// seated position is the bare per-slot constant; with the bridge it is the
// bone-chain-pivot-transformed + root-translated world position.
#include "test.h"

#include "play/wire_render_bridge.h"
#include "sim/character_render.h"

#include <cmath>
#include <vector>

using namespace guild;

namespace {

// Recorder for the seated position (the engine's VIBE_Object_SetPosition target).
float g_seatedPos[3] = {0, 0, 0};
int   g_seatCalls = 0;
void RecordSeatPos(const float p[3]) {
    g_seatedPos[0] = p[0]; g_seatedPos[1] = p[1]; g_seatedPos[2] = p[2];
    ++g_seatCalls;
}

// A mesh frame whose pivot/3x3/root translation are non-trivial (so the real
// transform is clearly distinct from the inert identity). float-indexed layout.
std::vector<float> MakeMeshFrame() {
    std::vector<float> f(200, 0.0f);
    // yaw-ish 3x3 (rows at 99/103/107, 100/104/108, 101/105/109)
    f[99] = 0.6f;  f[103] = 0.0f; f[107] = 0.8f;
    f[100] = 0.0f; f[104] = 1.0f; f[108] = 0.0f;
    f[101] = -0.8f; f[105] = 0.0f; f[109] = 0.6f;
    f[27] = 5.0f; f[28] = -2.0f; f[29] = 3.0f;     // pivot
    f[19] = 2.0f; f[20] = 1.0f;  f[21] = 0.5f;     // translation term A
    f[33] = 300.0f; f[34] = 120.0f; f[35] = -40.0f; // mesh ROOT translation
    return f;                                        // parent link (byte 504) null
}

bool Differs(const float a[3], const float b[3]) {
    return std::fabs(a[0] - b[0]) > 1e-3f ||
           std::fabs(a[1] - b[1]) > 1e-3f ||
           std::fabs(a[2] - b[2]) > 1e-3f;
}

// A hook table whose geometry leaves are INERT (identity / zero) but whose
// setObjectPosition records — to capture the inert-path seated position.
void InertPivot(void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void InertRoot(void*, float o[3]) { o[0]=o[1]=o[2]=0.0f; }
void NoopWorldTrans(const float[3]) {}

// A hook table whose geometry leaves are the REAL bridge leaves and whose
// setObjectPosition records — to capture the real-path seated position.
void RealPivot(void* m, const float in[3], float out[3]) { play::BridgePointThroughPivot(m, in, out); }
void RealRoot(void* m, float o[3]) { play::BridgeMeshRootTranslation(m, o); }

} // namespace

TEST(WireRenderBridgeItest, AttachPlacementInvokesRealLeaf) {
    std::vector<float> frame = MakeMeshFrame();
    sim::RenderActor actor{};
    actor.mesh = frame.data();
    actor.flagsA = 0;

    // --- INERT placement path -----------------------------------------------
    sim::CharRenderHooks inert{};
    inert.pointThroughPivot   = &InertPivot;
    inert.meshRootTranslation = &InertRoot;
    inert.setWorldTranslation = &NoopWorldTrans;
    inert.setObjectPosition   = &RecordSeatPos;
    // (other slots left null/default-constructed; ApplyAttachOffset only calls
    //  setWorldTranslation + setObjectPosition + the two geometry leaves.)
    sim::SetCharRenderHooks(&inert);
    g_seatCalls = 0;
    sim::ApplyAttachOffset(&actor, /*slot=*/1);   // slot 1 const offset = {-10,63,-14}
    CHECK_EQ(g_seatCalls, 1);
    float inertSeat[3] = {g_seatedPos[0], g_seatedPos[1], g_seatedPos[2]};
    // Inert path: seated == bare per-slot constant (no transform, no root xlate).
    CHECK(std::fabs(inertSeat[0] - (-10.0f)) < 1e-3f);
    CHECK(std::fabs(inertSeat[1] - 63.0f) < 1e-3f);
    CHECK(std::fabs(inertSeat[2] - (-14.0f)) < 1e-3f);

    // --- REAL placement path (bridge leaves) --------------------------------
    sim::CharRenderHooks real{};
    real.pointThroughPivot   = &RealPivot;
    real.meshRootTranslation = &RealRoot;
    real.setWorldTranslation = &NoopWorldTrans;
    real.setObjectPosition   = &RecordSeatPos;
    sim::SetCharRenderHooks(&real);
    g_seatCalls = 0;
    sim::ApplyAttachOffset(&actor, /*slot=*/1);
    CHECK_EQ(g_seatCalls, 1);
    float realSeat[3] = {g_seatedPos[0], g_seatedPos[1], g_seatedPos[2]};

    // The real leaf must have been invoked: the seated world position DIFFERS
    // from the inert constant (transformed through the bone-chain pivot + the
    // mesh root translation of 300/120/-40).
    CHECK(Differs(realSeat, inertSeat));
    // The mesh root translation (300/120/-40) dominates -> the seated X lands well
    // past the inert -10, proving the real root read fired at runtime.
    CHECK(realSeat[0] > 150.0f);
    CHECK(realSeat[1] > 100.0f);

    std::printf("  inert seat = (%.2f,%.2f,%.2f)  real seat = (%.2f,%.2f,%.2f)\n",
                inertSeat[0], inertSeat[1], inertSeat[2],
                realSeat[0], realSeat[1], realSeat[2]);

    // --- Now via the actual installer (full bridge table) -------------------
    // InstallRealRenderBridge installs the production table whose setObjectPosition
    // is inert (no recorder), so we can't read the seat back through it; instead
    // assert the install path produces the SAME real geometry through the public
    // accessors that the production table routes to.
    play::InstallRealRenderBridge();
    CHECK(play::RealRenderBridgeInstalled());
    float prodOff[3] = {-10.0f, 63.0f, -14.0f};   // slot-1 const, pre-sit
    float piv[3] = {0, 0, 0};
    play::BridgePointThroughPivot(frame.data(), prodOff, piv);
    float root[3] = {0, 0, 0};
    play::BridgeMeshRootTranslation(frame.data(), root);
    float prodSeat[3] = {piv[0] + root[0], piv[1] + root[1], piv[2] + root[2]};
    CHECK(!Differs(prodSeat, realSeat));   // production bridge == the real path

    sim::SetCharRenderHooks(nullptr);
    play::UninstallRealRenderBridge();
}
