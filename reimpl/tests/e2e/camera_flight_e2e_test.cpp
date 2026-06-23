// e2e: the real CameraFlightEnhanced(1500,"dummy_A1","dummy_A2") motion over the
// REAL ChooseCity.ed3 — play::CameraFlightAt (render::object_anim spline). The
// camera starts at dummy_A1, ends at dummy_A2, and moves smoothly between.
// GUARDED on the real game dir; honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/scene_persp_render.h"
#include "shim_impl/disk_filesystem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace guild;
namespace {
std::string GameDir() { if (const char* e = std::getenv("GUILD_GAME_DIR")) return e; return "europe_guild_1400_original"; }
float dist(const float a[3], const float b[3]) {
    float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2]; return std::sqrt(dx*dx+dy*dy+dz*dz);
}
}
TEST(CameraFlightE2E, A1ToA2OverTicks) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")) { std::printf("  [skip] no game dir\n"); CHECK(true); return; }
    io::ArchiveMount scenes; CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    auto scene = play::ParseSceneObjects(ed3.data(), ed3.size());

    // The flight's two waypoints exist in the scene. The camera EYE is the dummy's
    // +92 field (parsed into `rot`), per VIBE_Camera_SetToDummy — not +76.
    auto findPos = [&](const char* nm, float out[3]) -> bool {
        for (const auto& o : scene) if (o.name == nm) { out[0]=o.rot[0];out[1]=o.rot[1];out[2]=o.rot[2]; return true; }
        return false;
    };
    float a1[3], a2[3];
    CHECK(findPos("dummy_A1", a1));
    CHECK(findPos("dummy_A2", a2));

    const std::vector<std::string> wp = {"dummy_A1", "dummy_A2"};
    const float T = 1500.0f;
    play::PerspCamera c0, cmid, c1;
    CHECK(play::CameraFlightAt(scene, wp, T, 0.0f, c0));
    CHECK(play::CameraFlightAt(scene, wp, T, T * 0.5f, cmid));
    CHECK(play::CameraFlightAt(scene, wp, T, T, c1));

    // Endpoints sit exactly on the waypoints.
    CHECK(dist(c0.eye, a1) < 0.5f);
    CHECK(dist(c1.eye, a2) < 0.5f);
    // The midpoint is strictly between (the camera actually moved).
    CHECK(dist(c0.eye, c1.eye) > 1.0f);
    CHECK(dist(cmid.eye, a1) > 0.1f && dist(cmid.eye, a2) > 0.1f);
    std::printf("[flight] A1=(%.1f,%.1f,%.1f) mid=(%.1f,%.1f,%.1f) A2=(%.1f,%.1f,%.1f)\n",
                c0.eye[0],c0.eye[1],c0.eye[2], cmid.eye[0],cmid.eye[1],cmid.eye[2], c1.eye[0],c1.eye[1],c1.eye[2]);

    // Monotone progress along the flight: distance from A1 grows over t.
    float prev = 0.0f; bool monotone = true;
    for (int i = 0; i <= 10; ++i) {
        play::PerspCamera c; CHECK(play::CameraFlightAt(scene, wp, T, T * i / 10.0f, c));
        float d = dist(c.eye, a1);
        if (d < prev - 1e-2f) monotone = false;
        prev = d;
    }
    CHECK(monotone);
}
