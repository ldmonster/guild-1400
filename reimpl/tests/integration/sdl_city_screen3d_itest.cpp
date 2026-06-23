// integration: play::RunCityScreen3D — the native 3D city pick wired into New Game.
// Scripts the cursor onto a real city tower (projected from Menu/ChooseCity.ed3),
// clicks to select it, and presses Enter to confirm; asserts the right city.
// GUARDED on the real game dir (the 3D scene assets); honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/sdl_city_screen3d.h"
#include "play/scene_view.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/disk_filesystem.h"
#include "shim/IPlatform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}

struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; std::uint32_t t = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return at().t; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};

// Project a world point through the given camera with the pick's exact mapping
// (a7=W/2, centre W/2,H/2, Y flipped), to place the cursor on a tower.
bool ProjectTower(const play::PerspCamera& cam, const float pos[3], int W, int H,
                  int& sx, int& sy) {
    auto len = [](float x, float y, float z){ float l=std::sqrt(x*x+y*y+z*z); return l>1e-6f?l:1.0f; };
    float fwd[3] = {cam.target[0]-cam.eye[0], cam.target[1]-cam.eye[1], cam.target[2]-cam.eye[2]};
    float fl=len(fwd[0],fwd[1],fwd[2]); fwd[0]/=fl; fwd[1]/=fl; fwd[2]/=fl;
    float rx=fwd[2], ry=0.0f, rz=-fwd[0]; float rl=len(rx,ry,rz); rx/=rl; ry/=rl; rz/=rl;
    float ux=fwd[1]*rz-fwd[2]*ry, uy=fwd[2]*rx-fwd[0]*rz, uz=fwd[0]*ry-fwd[1]*rx;
    float dx=pos[0]-cam.eye[0], dy=pos[1]-cam.eye[1], dz=pos[2]-cam.eye[2];
    float vx=dx*rx+dy*ry+dz*rz, vy=dx*ux+dy*uy+dz*uz, vz=dx*fwd[0]+dy*fwd[1]+dz*fwd[2];
    if (vz <= 1e-3f) return false;
    const float a7 = W * 0.5f;
    sx = (int)(W*0.5f + a7*vx/vz); sy = (int)(H*0.5f - a7*vy/vz);
    return true;
}
} // namespace

TEST(SdlCityScreen3D, ClickTowerConfirmsCity) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] SdlCityScreen3D: game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    const int W = 800, H = 600;

    // Use the REAL pick camera (CameraFromDummy(dummy_A2), as RunCityScreen3D does)
    // and pick whichever city tower projects on-screen; script the cursor onto it.
    io::ArchiveMount scenes; CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    auto scene = play::ParseSceneObjects(ed3.data(), ed3.size());
    play::PerspCamera cam;
    CHECK(play::BuildSceneCamera(ed3.data(), ed3.size(), scene, "dummy_A2", cam));
    int sx = -1, sy = -1; std::string targetCity, targetPath;
    for (const auto& o : scene) {
        if ((o.type != 2 && o.type != 3) || o.name.rfind("dummy_", 0) != 0) continue;
        const std::string city = o.name.substr(6);
        if (city.size() <= 2) continue;            // skip camera waypoints (A1/A2/...)
        int px, py;
        if (ProjectTower(cam, o.pos, W, H, px, py) && px >= 0 && px < W && py >= 0 && py < H) {
            sx = px; sy = py; targetCity = city;
            targetPath = "Resources/gamedata/Cities/" + city + ".cty";
            break;
        }
    }
    CHECK(!targetCity.empty());
    std::printf("[city3d] target='%s' at (%d,%d)\n", targetCity.c_str(), sx, sy);

    play::CityScreenConfig cfg;
    cfg.gameDir = GameDir(); cfg.fbW = W; cfg.fbH = H; cfg.frameCapMs = 0; cfg.maxFrames = 40; cfg.introFrames = 0;
    cfg.cities = { {"HANNOVER", "Resources/gamedata/Cities/HANNOVER.cty"},
                   {"BERLIN",   "Resources/gamedata/Cities/BERLIN.cty"},
                   {"AUGSBURG", "Resources/gamedata/Cities/AUGSBURG.cty"},
                   {"DRESDEN",  "Resources/gamedata/Cities/DRESDEN.cty"},
                   {"KOELN",    "Resources/gamedata/Cities/KOELN.cty"} };

    // Cursor stays on the target tower throughout: hover, a click (left edge ->
    // select), release, then Enter (-> confirm the selected city).
    SeqPlatform plat;
    auto on = [&](bool left, int key) { SeqPlatform::Step s; s.x = sx; s.y = sy;
        s.left = left; s.key = key; s.t = 0; return s; };
    for (int i = 0; i < 4; ++i) plat.steps.push_back(on(false, 0)); // hover
    for (int i = 0; i < 3; ++i) plat.steps.push_back(on(true, 0));  // click -> select
    for (int i = 0; i < 3; ++i) plat.steps.push_back(on(false, 0)); // release
    for (int i = 0; i < 6; ++i) plat.steps.push_back(on(false, 0x0D)); // Enter -> confirm

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(W, H, 32, false));
    play::CityScreenResult r = play::RunCityScreen3D(dev, plat, cfg);
    std::printf("[city3d] confirmed=%d city='%s' hovered=%d frames=%d\n",
                (int)r.confirmed, r.cityName.c_str(), r.hoveredItem, r.framesPresented);

    CHECK(r.confirmed);
    CHECK(r.cityName == targetCity);
    CHECK(r.cityPath == targetPath);
    CHECK(r.framesPresented > 0);
}

// Clicking the _AUSWAHL choose button (gfx 1210) in the bottom-centre info window
// confirms the currently selected (default = first) city — the 1:1 confirm control.
TEST(SdlCityScreen3D, ChooseButtonConfirms) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("gfx/gilde.gfx")) {
        std::printf("  [skip] ChooseButtonConfirms: assets absent\n"); CHECK(true); return; }
    const int W = 800, H = 600;
    play::CityScreenConfig cfg;
    cfg.gameDir = GameDir(); cfg.fbW = W; cfg.fbH = H; cfg.frameCapMs = 0; cfg.maxFrames = 40; cfg.introFrames = 0;
    cfg.cities = { {"HANNOVER", "Resources/gamedata/Cities/HANNOVER.cty"},
                   {"BERLIN",   "Resources/gamedata/Cities/BERLIN.cty"} };

    // The _AUSWAHL button sits at the bottom-right of the panel (form rect scaled):
    // strip (128,545,529,43) -> button right-aligned. Click its centre.
    const int bx = 606 + 43/2, by = 547 + 39/2;   // matches RenderCityInfoWindow layout
    SeqPlatform plat;
    auto step = [&](bool left){ SeqPlatform::Step s; s.x = bx; s.y = by; s.left = left; return s; };
    for (int i = 0; i < 4; ++i) plat.steps.push_back(step(false)); // hover (window renders, default city selected)
    for (int i = 0; i < 4; ++i) plat.steps.push_back(step(true));  // click the choose button

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(W, H, 32, false));
    play::CityScreenResult r = play::RunCityScreen3D(dev, plat, cfg);
    std::printf("[city3d] choose-button confirmed=%d city='%s'\n", (int)r.confirmed, r.cityName.c_str());
    CHECK(r.confirmed);
    CHECK(r.cityName == "HANNOVER");   // the default (first) city
}

// ESC on the 3D screen cancels (no city), same contract as the 2D front.
TEST(SdlCityScreen3D, EscCancels) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")) { CHECK(true); return; }
    play::CityScreenConfig cfg;
    cfg.gameDir = GameDir(); cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 10; cfg.introFrames = 0;
    cfg.cities = { {"AUGSBURG", "Resources/gamedata/Cities/AUGSBURG.cty"} };

    SeqPlatform plat;
    SeqPlatform::Step esc; esc.x = 10; esc.y = 10; esc.key = 0x1B;
    for (int i = 0; i < 4; ++i) plat.steps.push_back(esc);

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    play::CityScreenResult r = play::RunCityScreen3D(dev, plat, cfg);
    CHECK(!r.confirmed);
    CHECK(r.back);
    CHECK(r.backByEsc);
}
