// Unit: REAL object/scene-node WORLD TRANSFORM decode (object_transform.h) over a
// SYNTHETIC engine render node (a raw byte buffer with the recovered offsets).
// Golden translation/yaw vectors are independently computed in Python from
// VIBE_Math_MatrixFromEuler @0x5cb1bc (see the brief / build log).
#include "test.h"

#include <cmath>
#include <cstring>

#include "play/object_transform.h"

using namespace guild;

namespace {

constexpr int kNodeSize = 600;   // cover +533 type byte + +396..+460 matrix   // big enough to cover +396 matrix (+396..+460)

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void WrF(unsigned char* base, int off, float v) {
    std::memcpy(base + off, &v, sizeof v);
}

// Build a 16-float column-major rotation matrix exactly as VIBE_Math_MatrixFromEuler
// (ax=pitch, ay=yaw, az=roll). Mirrors the engine writer 1:1.
void MatFromEuler(float ax, float ay, float az, float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    float s_ay = std::sin(ay), c_ay = std::cos(ay);
    float c_ax = std::cos(ax), s_ax = std::sin(ax);
    float c_az = std::cos(az), s_az = std::sin(az);
    m[0]  = c_ay * c_az;
    m[4]  = c_ay * s_az;
    m[8]  = -s_ay;
    m[9]  = s_ax * c_ay;
    m[10] = c_ax * c_ay;
    m[15] = 1.0f;
    m[1]  = s_ax * (s_ay * c_az) - c_ax * s_az;
    m[2]  = (s_ay * c_az) * c_ax + s_ax * s_az;
    m[5]  = c_ax * c_az + s_ax * (s_ay * s_az);
    m[6]  = (s_ay * s_az) * c_ax - s_ax * c_az;
    // translation column (m[12..14]) stays 0 — rotation only, as the engine writes.
}

// Seat a synthetic render node: type byte, world position, +396 frame matrix.
void MakeNode(unsigned char* n, u8 type, float px, float py, float pz,
              float ax, float ay, float az) {
    std::memset(n, 0, kNodeSize);
    n[play::kNodeTypeByte] = type;
    WrF(n, play::kNodePosX, px);
    WrF(n, play::kNodePosY, py);
    WrF(n, play::kNodePosZ, pz);
    float m[16];
    MatFromEuler(ax, ay, az, m);
    std::memcpy(n + play::kNodeFrameMatrix, m, sizeof m);
}

} // namespace

// --- matrix readers: translation from col4 (golden) -------------------------
TEST(ObjectTransformUnit, WorldMatrixTranslationCol4) {
    float m[16];
    std::memset(m, 0, sizeof m);
    m[12] = 12.5f; m[13] = 3.0f; m[14] = -7.25f;   // python golden
    float t[3] = {0, 0, 0};
    play::WorldMatrixTranslation(m, t);
    CHECK(Near(t[0], 12.5f));
    CHECK(Near(t[1], 3.0f));
    CHECK(Near(t[2], -7.25f));
}

// --- matrix readers: yaw = atan2(-m[8], m[0]) (golden) ----------------------
TEST(ObjectTransformUnit, WorldMatrixYawGolden) {
    float m[16];
    MatFromEuler(0, 0, 0, m);
    CHECK(Near(play::WorldMatrixYaw(m), 0.0f));

    MatFromEuler(0, (float)(M_PI / 6.0), 0, m);          // 30 deg
    CHECK(Near(play::WorldMatrixYaw(m), 0.523599f));     // python: 0.523599

    MatFromEuler(0, (float)(M_PI / 2.0), 0, m);          // 90 deg
    CHECK(Near(play::WorldMatrixYaw(m), 1.570796f));     // python: 1.570796

    MatFromEuler(0, -(float)(M_PI / 4.0), 0, m);         // -45 deg
    CHECK(Near(play::WorldMatrixYaw(m), -0.785398f));    // python: -0.785398

    MatFromEuler(0.2f, (float)(M_PI / 3.0), 0.1f, m);    // pitch/yaw60/roll
    CHECK(Near(play::WorldMatrixYaw(m), 1.049364f, 1e-3f)); // python: 1.049364
}

// --- SceneNodeWorldPlacement over a synthetic node --------------------------
TEST(ObjectTransformUnit, SceneNodePlacementMesh) {
    unsigned char n[kNodeSize];
    MakeNode(n, play::kNodeTypeMeshA, 100.0f, 5.0f, 250.0f, 0, (float)(M_PI / 6.0), 0);
    play::WorldPlacement p = play::SceneNodeWorldPlacement(n);
    CHECK(p.visible);                       // type 1 -> drawable
    CHECK(Near(p.x, 100.0f));               // +76 world pos
    CHECK(Near(p.y, 5.0f));
    CHECK(Near(p.z, 250.0f));
    CHECK(Near(p.yaw, 0.523599f));          // +396 matrix yaw 30deg
}

TEST(ObjectTransformUnit, SceneNodePlacementCameraDrawable) {
    unsigned char n[kNodeSize];
    MakeNode(n, play::kNodeTypeCamera, 1.0f, 2.0f, 3.0f, 0, 0, 0);
    play::WorldPlacement p = play::SceneNodeWorldPlacement(n);
    CHECK(p.visible);                       // type 3 (camera) -> TestNodeFlag &1 passes
    CHECK(Near(p.x, 1.0f));
}

TEST(ObjectTransformUnit, SceneNodePlacementLightNotDrawn) {
    unsigned char n[kNodeSize];
    MakeNode(n, play::kNodeTypeLight, 9.0f, 9.0f, 9.0f, 0, 0, 0);
    play::WorldPlacement p = play::SceneNodeWorldPlacement(n);
    CHECK(!p.visible);                      // type 4 (light) -> NOT drawn by mask 0x181
}

TEST(ObjectTransformUnit, SceneNodePlacementEmptyNotDrawn) {
    unsigned char n[kNodeSize];
    MakeNode(n, play::kNodeTypeEmpty, 9.0f, 9.0f, 9.0f, 0, 0, 0);
    play::WorldPlacement p = play::SceneNodeWorldPlacement(n);
    CHECK(!p.visible);                      // type 0 (empty) -> not drawn
}

TEST(ObjectTransformUnit, SceneNodePlacementNullSafe) {
    play::WorldPlacement p = play::SceneNodeWorldPlacement(nullptr);
    CHECK(!p.visible);
    CHECK(Near(p.x, 0.0f));
    CHECK(Near(p.yaw, 0.0f));
}

// --- ObjectWorldPlacement delegates to the node -----------------------------
TEST(ObjectTransformUnit, ObjectPlacementDelegatesToNode) {
    unsigned char node[kNodeSize];
    MakeNode(node, play::kNodeTypeMeshB, -40.0f, 0.0f, 60.0f, 0, (float)(M_PI / 2.0), 0);
    // The object record itself is opaque here; the transform lives on the node.
    unsigned char objRec[169] = {1};
    play::WorldPlacement p = play::ObjectWorldPlacement(objRec, node);
    CHECK(p.visible);
    CHECK(Near(p.x, -40.0f));
    CHECK(Near(p.z, 60.0f));
    CHECK(Near(p.yaw, 1.570796f));          // 90deg
}

TEST(ObjectTransformUnit, ObjectPlacementNullNodeNotVisible) {
    unsigned char objRec[169] = {1};
    play::WorldPlacement p = play::ObjectWorldPlacement(objRec, nullptr);
    CHECK(!p.visible);                      // no node bound -> unplaced
}

// --- distinct nodes -> distinct decoded positions (not a uniform grid) ------
TEST(ObjectTransformUnit, DistinctNodesDistinctPositions) {
    unsigned char a[kNodeSize], b[kNodeSize], c[kNodeSize];
    MakeNode(a, play::kNodeTypeMeshA, 10.0f, 0, 20.0f, 0, 0, 0);
    MakeNode(b, play::kNodeTypeMeshA, 33.0f, 0, 77.0f, 0, 0, 0);
    MakeNode(c, play::kNodeTypeMeshA, -5.0f, 0, 5.0f, 0, 0, 0);
    play::WorldPlacement pa = play::SceneNodeWorldPlacement(a);
    play::WorldPlacement pb = play::SceneNodeWorldPlacement(b);
    play::WorldPlacement pc = play::SceneNodeWorldPlacement(c);
    CHECK(!(Near(pa.x, pb.x) && Near(pa.z, pb.z)));
    CHECK(!(Near(pb.x, pc.x) && Near(pb.z, pc.z)));
    CHECK(!(Near(pa.x, pc.x) && Near(pa.z, pc.z)));
}
