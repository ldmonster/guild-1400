#include "test.h"

// Unit tier for play::CityView3D — the engine math helpers (frustum build 1:1
// with VIBE_Render_BuildViewMatrix @0x5accd0, the record+72 model->view compose,
// the camera-forward inverse) and the hook/bind contract, all asset-free.
#include "play/city_view3d.h"
#include "render/cull.h"
#include "render/scene_transform.h"
#include "sim/entity.h"

#include <cmath>
#include <cstring>

using namespace guild;
using play::CityCamera3D;
using play::CityPlacement;
using play::CityView3D;

namespace {
inline u32 FloatBits(float f) {
    u32 b;
    std::memcpy(&b, &f, sizeof b);
    return b;
}
constexpr float kPi = 3.14159265358979f;
} // namespace

// ---------------------------------------------------------------------------
// BuildEngineFrustum: the 0x5accd0 plane build — ang = atan2(W/2, scale) (= pi/4
// at the engine scale W/2) for the x planes, ang2 = atan2(H/2, scale) for the y
// planes (operands from the disassembly at 0x5acce1/0x5acd15), with the exact
// epsilon d dwords 0x360637BD / 0xB60637BD (the constants at 13DCDAC/BC/CC/DC).
// ---------------------------------------------------------------------------
TEST(CityView3D, FrustumMatchesEngineConstants) {
    render::Frustum fr{};
    play::BuildEngineFrustum(640.0f, 480.0f, 320.0f, 10.0f, 5000.0f, fr);

    const float ang = std::atan2(320.0f, 320.0f);     // x planes: atan2(W/2, scale)
    CHECK(std::fabs(ang - kPi / 4.0f) < 1e-6f);
    const float c = std::cos(ang), s = std::sin(ang);
    const float ang2 = std::atan2(240.0f, 320.0f);    // y planes: atan2(H/2, scale)
    const float c2 = std::cos(ang2), s2 = std::sin(ang2);

    // plane0 { cosA, 0, sinA, -eps } / plane1 { -cosA, 0, sinA, +eps }
    CHECK(std::fabs(fr.plane[0][0] - c) < 1e-7f);
    CHECK_EQ(0.0f, fr.plane[0][1]);
    CHECK(std::fabs(fr.plane[0][2] - s) < 1e-7f);
    CHECK_EQ(0xB60637BDu, FloatBits(fr.plane[0][3]));   // dword_13DCDAC
    CHECK(std::fabs(fr.plane[1][0] + c) < 1e-7f);
    CHECK_EQ(0x360637BDu, FloatBits(fr.plane[1][3]));   // dword_13DCDBC
    // plane2 { 0, cosB, sinB, +eps } / plane3 { 0, -cosB, sinB, -eps }
    CHECK_EQ(0.0f, fr.plane[2][0]);
    CHECK(std::fabs(fr.plane[2][1] - c2) < 1e-7f);
    CHECK(std::fabs(fr.plane[2][2] - s2) < 1e-7f);
    CHECK_EQ(0x360637BDu, FloatBits(fr.plane[2][3]));   // dword_13DCDCC
    CHECK(std::fabs(fr.plane[3][1] + c2) < 1e-7f);
    CHECK_EQ(0xB60637BDu, FloatBits(fr.plane[3][3]));   // dword_13DCDDC
    CHECK_EQ(10.0f, fr.nearZ);
    CHECK_EQ(5000.0f, fr.farZ);
}

// A frustum sanity drive through the REAL clip-classify leaf (0x5ad614): a point
// dead ahead is inside; far left/right/above/below/behind points carry outcodes.
TEST(CityView3D, FrustumClassifiesViewPoints) {
    render::Frustum fr{};
    play::BuildEngineFrustum(320.0f, 240.0f, 160.0f, 10.0f, 1000.0f, fr);

    render::Vertex v[6] = {};
    render::Polygon p[2] = {};
    // ahead / left-out / right-out / below-out / behind-near / past-far
    v[0].x = 0;     v[0].y = 0;    v[0].z = 100.0f;
    v[1].x = -500;  v[1].y = 0;    v[1].z = 100.0f;
    v[2].x = 500;   v[2].y = 0;    v[2].z = 100.0f;
    v[3].x = 0;     v[3].y = -500; v[3].z = 100.0f;
    v[4].x = 0;     v[4].y = 0;    v[4].z = 5.0f;
    v[5].x = 0;     v[5].y = 0;    v[5].z = 5000.0f;
    p[0].v0 = &v[0]; p[0].v1 = &v[0]; p[0].v2 = &v[0];   // kept (all inside)
    p[1].v0 = &v[1]; p[1].v1 = &v[1]; p[1].v2 = &v[1];   // culled (shared plane)

    render::ComputeVertexClipFlags(0x3F, v, 6, p, 2, fr);
    CHECK_EQ(0u, (unsigned)(v[0].clipFlags & render::kClipOutMask));
    CHECK((v[1].clipFlags & render::kClipOutMask) != 0);
    CHECK((v[2].clipFlags & render::kClipOutMask) != 0);
    CHECK((v[3].clipFlags & render::kClipOutMask) != 0);
    CHECK((v[4].clipFlags & render::kClipNear) != 0);
    CHECK((v[5].clipFlags & render::kClipFar) != 0);
    CHECK((p[0].flags36 & 0x80) != 0);    // kept polygon
    CHECK_EQ(0u, (unsigned)p[1].flags36); // wholly outside one plane -> culled
}

// ---------------------------------------------------------------------------
// AimCamera <-> the engine camera basis: a camera aimed at a target must see the
// target dead ahead through the REAL view transform (R = MatrixFromEuler(-rot),
// view = R^T*(world - eye) — render::WorldToView / CameraForward round-trip).
// ---------------------------------------------------------------------------
TEST(CityView3D, AimCameraLooksAtTargetThroughEngineView) {
    const float eye[3] = {10.0f, 50.0f, -30.0f};
    const float target[3] = {-40.0f, 12.0f, 120.0f};
    CityCamera3D cam;
    play::AimCamera(cam, eye, target);

    const float negRot[3] = {-cam.rot[0], -cam.rot[1], -cam.rot[2]};
    const render::Mat3 R = render::MatrixFromEuler(negRot);

    // CameraForward (column 2 of R) points from eye to target.
    float fwd[3];
    render::CameraForward(R, fwd);
    float d[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < 3; ++k)
        CHECK(std::fabs(fwd[k] - d[k] / len) < 1e-4f);

    // The target lands on the +z view axis at the eye distance.
    float view[3];
    render::WorldToView(R, eye, target, view);
    CHECK(std::fabs(view[0]) < 1e-3f);
    CHECK(std::fabs(view[1]) < 1e-3f);
    CHECK(std::fabs(view[2] - len) < 1e-3f);
}

// ---------------------------------------------------------------------------
// ComposeModelViewMatrix: the composed record+72 matrix maps (a) the instance's
// world position to the camera-relative view position and (b) a model-space
// offset through the local->world rotation — exactly view = R^T*(l2w*v + t - eye).
// ---------------------------------------------------------------------------
TEST(CityView3D, ComposeModelViewMatrixSeatsInstanceInView) {
    // Instance: yawed 90 deg about +Y, seated at (100, 0, 200).
    const float instEuler[3] = {0.0f, kPi * 0.5f, 0.0f};
    const render::Mat3 l2w = render::Transpose(render::MatrixFromEuler(instEuler));
    const float pos[3] = {100.0f, 0.0f, 200.0f};

    CityCamera3D cam;
    cam.eye[0] = 100.0f; cam.eye[1] = 0.0f; cam.eye[2] = 100.0f;  // 100 behind
    cam.rot[0] = cam.rot[1] = cam.rot[2] = 0.0f;                  // looking +z

    float m[16];
    play::ComposeModelViewMatrix(l2w, pos, cam, m);

    // Model origin -> view (0, 0, 100): the 4th column is the view translation.
    CHECK(std::fabs(m[12] - 0.0f) < 1e-4f);
    CHECK(std::fabs(m[13] - 0.0f) < 1e-4f);
    CHECK(std::fabs(m[14] - 100.0f) < 1e-4f);

    // A model +x point through the REAL vertex-walk arithmetic
    // (out = v.x*m[0..2] + v.y*m[4..6] + v.z*m[8..10] + m[12..14], 0x5c953c):
    // yaw +90 about Y (l2w = MatrixFromEuler^T) maps model +x onto world +z
    // => view (0, 0, 100+7).
    const float vx = 7.0f, vy = 0.0f, vz = 0.0f;
    const float ox = vx * m[0] + vy * m[4] + vz * m[8] + m[12];
    const float oy = vx * m[1] + vy * m[5] + vz * m[9] + m[13];
    const float oz = vx * m[2] + vy * m[6] + vz * m[10] + m[14];
    float lp[3] = {vx, vy, vz}, wp[3];
    render::Apply(l2w, lp, wp);   // expected world offset of the model point
    CHECK(std::fabs(ox - wp[0] - (pos[0] - cam.eye[0])) < 1e-3f);
    CHECK(std::fabs(oy - wp[1] - (pos[1] - cam.eye[1])) < 1e-3f);
    CHECK(std::fabs(oz - wp[2] - (pos[2] - cam.eye[2])) < 1e-3f);
    CHECK(std::fabs(oz - 107.0f) < 1e-3f);
}

// Rotating ONLY the camera changes the composed matrix (the rotation drives the
// view — the wave-2 "camera rotation changes the frame" contract at matrix level).
TEST(CityView3D, CameraRotationChangesComposedMatrix) {
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const render::Mat3 ident = render::Transpose(render::MatrixFromEuler(zero));
    const float pos[3] = {10.0f, 0.0f, 50.0f};
    CityCamera3D a, b;
    b.rot[1] = 0.4f;
    float ma[16], mb[16];
    play::ComposeModelViewMatrix(ident, pos, a, ma);
    play::ComposeModelViewMatrix(ident, pos, b, mb);
    bool differs = false;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(ma[i] - mb[i]) > 1e-6f) { differs = true; break; }
    CHECK(differs);
}

// ---------------------------------------------------------------------------
// Hook contract over a live (synthetic) world, no assets mounted: the placement
// hook places, the empty model resolution is COUNTED as the named gap, and an
// unhooked/unloaded view binds nothing (inert default with no scene).
// ---------------------------------------------------------------------------
TEST(CityView3D, BindWorldObjectsHookAndNamedGapCounts) {
    sim::ResetEntityArrays();
    sim::g_objects[0].alive = 1; sim::g_objects[0].id = 41;
    sim::g_objects[3].alive = 2; sim::g_objects[3].id = 77;

    CityView3D view;

    // No scene loaded, no hooks: nothing placeable (the owner-id map is empty).
    CHECK_EQ(0, view.BindWorldObjects());
    CHECK_EQ(2, view.unplacedObjects());

    // Placement hook seats both; model resolution stays the named gap (no
    // Objects.BIN mounted => no member), counted in modelUnresolved().
    play::CityView3DHooks hooks;
    hooks.resolveObjectPlacement =
        [](const sim::ObjectRec& rec, CityPlacement& out) {
            out.pos[0] = (float)rec.id; out.pos[1] = 0.0f; out.pos[2] = 2.0f * (float)rec.id;
            out.euler[1] = 0.25f;
            return true;
        };
    view.SetHooks(std::move(hooks));
    CHECK_EQ(2, view.BindWorldObjects());
    CHECK_EQ(0, view.unplacedObjects());
    CHECK_EQ(2, view.modelUnresolved());
    CHECK_EQ(2u, (unsigned)view.boundObjects().size());
    CHECK_EQ(41, view.boundObjects()[0].id);
    CHECK_EQ(41.0f, view.boundObjects()[0].place.pos[0]);
    CHECK_EQ(154.0f, view.boundObjects()[1].place.pos[2]);

    // No drawable instances -> RenderFrame is a clean no-op Result.
    CityView3D::Options opt;
    opt.fbW = 32; opt.fbH = 24;
    CityView3D::Result r = view.RenderFrame(CityCamera3D{}, opt);
    CHECK_EQ(0, r.instancesDrawn);
    CHECK_EQ(0, r.rasterTris);

    sim::ResetEntityArrays();
}

// Uninitialized view: Init(null) fails, LoadCity without mounts fails.
TEST(CityView3D, InitAndLoadFailCleanlyWithoutAssets) {
    CityView3D view;
    CHECK(!view.Init(nullptr));
    CHECK(!view.mounted());
    CHECK(!view.LoadCity("AUGSBURG"));
    CHECK_EQ(0u, (unsigned)view.instances().size());
}
