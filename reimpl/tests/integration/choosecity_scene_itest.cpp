// integration: the native bridge play::SceneChooseCityHooks driving the BYTE-FAITHFUL
// gui::Menu_RunChooseCity @0x52e6d8 over the REAL Menu/ChooseCity.ed3 3D scene — the
// reconstructed pick (play::PickNearestObject == VIBE_Pick_FindNearestObjectAt) wired
// into the live hover->info->confirm loop. Scripted input hovers a real city tower and
// confirms it; the loop must hover that city, render its info, and chain the sub-screens.
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"
#include "play/choosecity_scene.h"
#include "gui/choosecity_run.h"
#include "shim_impl/disk_filesystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {
std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "europe_guild_1400_original";
}

// Project a world point through the bridge camera with the pick's exact mapping
// (a7 = W/2, screen centre W/2,H/2, Y flipped) — to script the cursor onto a tower.
bool ProjectWorld(const play::PerspCamera& cam, int W, int H,
                  const float p[3], float& sx, float& sy) {
    auto len = [](float x, float y, float z) { float l = std::sqrt(x*x+y*y+z*z); return l>1e-6f?l:1.0f; };
    float fwd[3] = {cam.target[0]-cam.eye[0], cam.target[1]-cam.eye[1], cam.target[2]-cam.eye[2]};
    float fl = len(fwd[0],fwd[1],fwd[2]); fwd[0]/=fl; fwd[1]/=fl; fwd[2]/=fl;
    float rx = 1.0f*fwd[2]-0.0f*fwd[1], ry = 0.0f*fwd[0]-0.0f*fwd[2], rz = 0.0f*fwd[1]-1.0f*fwd[0];
    float rl = len(rx,ry,rz); rx/=rl; ry/=rl; rz/=rl;
    float ux = fwd[1]*rz-fwd[2]*ry, uy = fwd[2]*rx-fwd[0]*rz, uz = fwd[0]*ry-fwd[1]*rx;
    float dx = p[0]-cam.eye[0], dy = p[1]-cam.eye[1], dz = p[2]-cam.eye[2];
    float vx = dx*rx+dy*ry+dz*rz, vy = dx*ux+dy*uy+dz*uz, vz = dx*fwd[0]+dy*fwd[1]+dz*fwd[2];
    if (vz <= 1e-3f) return false;
    const float a7 = W*0.5f;
    sx = W*0.5f + a7*vx/vz; sy = H*0.5f - a7*vy/vz; return true;
}
} // namespace

TEST(ChooseCitySceneIntegration, HoverAndConfirmRealCity) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] ChooseCityScene: real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    const int W = 800, H = 600;
    play::SceneChooseCityHooks hooks;
    CHECK(hooks.Load(fs, W, H));

    // Pick whichever city tower projects ON-SCREEN from the real cutscene camera
    // (CameraFromDummy(dummy_A2), which the bridge now uses) and hover it.
    float curX = -1.0f, curY = -1.0f;
    std::string targetCity;
    for (const auto& o : hooks.scene()) {
        if ((o.type != 2 && o.type != 3) || o.name.rfind("dummy_", 0) != 0) continue;
        const std::string city = o.name.substr(6);
        float sx, sy;
        if (ProjectWorld(hooks.camera(), W, H, o.pos, sx, sy) &&
            sx >= 0 && sx < W && sy >= 0 && sy < H) {
            // a real city (not a camera waypoint dummy_A*/B*/C*)
            if (city.size() > 2) { curX = sx; curY = sy; targetCity = city; break; }
        }
    }
    CHECK(!targetCity.empty());
    std::printf("[choosecity] hovering '%s' at (%.0f,%.0f)\n", targetCity.c_str(), curX, curY);

    // Script the input: hover the tower for several frames, then confirm on frame 4.
    const int kConfirmFrame = 4;
    const int kStopFrame = 6;
    play::ChooseCityInput in;
    in.runFrame = [&](int f) { return f < kStopFrame; };
    in.cursor = [&](int, float& x, float& y) { x = curX; y = curY; };
    in.clickedWidgetId = [&](int f) { return f == kConfirmFrame ? gui::kCityConfirmId : 0; };
    in.keyCode = [](int) { return 0; };
    in.rightClick = [](int) { return false; };
    in.chooseCharacterIntro = [] { return true; };
    in.chooseHistory = [] { return true; };
    hooks.SetInput(in);

    gui::ChooseCityHooks* prev = gui::Menu_SetChooseCityHooks(&hooks);

    gui::ChooseCityState st;
    st.network = false;
    gui::ChooseCityRecord rec;
    int result = gui::Menu_RunChooseCity(st, &rec, /*maxFrames=*/kStopFrame + 2);

    gui::Menu_SetChooseCityHooks(prev);

    std::printf("[choosecity] cities=%zu confirmed=%d city='%s' intro=%d hist=%d info='%s'\n",
                rec.cities.size(), (int)rec.confirmed, rec.confirmedCity.c_str(),
                (int)rec.chainIntroRan, (int)rec.chainHistoryRan, hooks.lastInfoCity().c_str());

    // The 5 real cities enumerated + their "stadt_" markers spawned.
    CHECK(rec.cities.size() == 5);
    bool sawTarget = false;
    for (const auto& c : rec.cities) if (c.name == targetCity) sawTarget = true;
    CHECK(sawTarget);

    // The live pick resolved the hovered tower as the target city + rendered its info.
    CHECK(hooks.lastInfoCity() == targetCity);

    // Confirm chained ChooseCharacterIntroVariant -> RunChooseHistory and returned 1.
    CHECK(rec.confirmed);
    CHECK(rec.confirmedCity == targetCity);
    CHECK(rec.chainIntroRan);
    CHECK(rec.chainHistoryRan);
    CHECK(result == 1);
    CHECK(st.result == 1);

    // A HoverChange tag must have fired (the info render path ran).
    bool hover = false;
    for (int i = 0; i < rec.traceCount; ++i)
        if (std::string(rec.trace[i]) == "HoverChange") hover = true;
    CHECK(hover);
}

TEST(ChooseCitySceneIntegration, CancelWithEsc) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        CHECK(true);
        return;
    }
    play::SceneChooseCityHooks hooks;
    CHECK(hooks.Load(fs, 800, 600));

    play::ChooseCityInput in;
    in.runFrame = [&](int f) { return f < 3; };
    in.cursor = [&](int, float& x, float& y) { x = -9999.0f; y = -9999.0f; }; // far off any marker
    in.keyCode = [&](int f) { return f == 1 ? gui::kKeyEsc : 0; };       // Esc on frame 1
    hooks.SetInput(in);

    gui::ChooseCityHooks* prev = gui::Menu_SetChooseCityHooks(&hooks);
    gui::ChooseCityState st;
    gui::ChooseCityRecord rec;
    int result = gui::Menu_RunChooseCity(st, &rec, 8);
    gui::Menu_SetChooseCityHooks(prev);

    CHECK(result == 0);          // cancelled -> no city confirmed
    CHECK(!rec.confirmed);
    CHECK(st.close == 1);        // dword_631614 armed by the Esc edge
}
