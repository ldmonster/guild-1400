// e2e: parse the REAL Menu/ChooseCity.ed3 (the new-game city-select scene — the
#include <cctype>
// guild Secretariat interior with the map table + city markers) out of scenes.BIN,
// and composite its mesh objects (resolved from Objects.BIN via the fast-chunk
// loader) through a perspective camera with play::RenderSceneObjects.
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/scene_persp_render.h"
#include "play/real_texture_source.h"
#include "render/surface.h"
#include "render/types.h"
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
bool Present() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/scenes.BIN") && fs.exists("Resources/Objects.BIN");
}
std::string Upper(std::string s) { for (auto& c : s) c = (char)std::toupper((unsigned char)c); return s; }
// Case-insensitive substring match over the object name AND its mesh basename.
int CountNamed(const std::vector<play::SceneObjectInst>& v, const char* needle) {
    const std::string nd = Upper(needle);
    int n = 0;
    for (const auto& o : v)
        if (Upper(o.name).find(nd) != std::string::npos ||
            Upper(o.mesh).find(nd) != std::string::npos) ++n;
    return n;
}
} // namespace

TEST(SceneViewE2E, ParseAndRenderChooseCityScene) {
    if (!Present()) {
        std::printf("  [skip] SceneViewE2E: real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", /*caseInsensitive=*/true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));

    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    CHECK(!ed3.empty());

    // Parse the scene object tree.
    u32 ver = 0;
    std::vector<play::SceneObjectInst> objsParsed = play::ParseSceneObjects(ed3.data(), ed3.size(), &ver);
    std::printf("[sceneview] tag=0x%X objects=%zu\n", ver, objsParsed.size());
    CHECK(ver == 0x3A6C00BBu);
    CHECK(objsParsed.size() > 20);   // 47 in the real scene

    // The scene must carry mesh objects (furniture/walls) AND the per-city marker
    // dummies the new-game flow spawns towers on.
    int meshObjs = 0;
    for (const auto& o : objsParsed)
        if ((o.type == 1 || o.type == 4) && o.hasMesh) ++meshObjs;
    std::printf("[sceneview] mesh-objects=%d city-dummies: HANNOVER=%d BERLIN=%d AUGSBURG=%d\n",
                meshObjs, CountNamed(objsParsed, "HANNOVER"),
                CountNamed(objsParsed, "BERLIN"), CountNamed(objsParsed, "AUGSBURG"));
    CHECK(meshObjs > 10);
    CHECK(CountNamed(objsParsed, "dummy_HANNOVER") == 1);
    CHECK(CountNamed(objsParsed, "dummy_AUGSBURG") == 1);
    CHECK(CountNamed(objsParsed, "KARTENTISCH") >= 1);   // the map table the towers sit on

    // Composite the mesh objects through a perspective camera framing the room.
    const int W = 320, H = 240;
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    CHECK(fb != nullptr);

    // Use the REAL cutscene camera: A_Stadtwahl.esc flies dummy_A1 -> dummy_A2
    // (CameraFlightEnhanced); seat the camera at dummy_A1 with its own orientation.
    play::PerspCamera cam;
    CHECK(play::CameraFromDummy(objsParsed, "dummy_A1", cam));
    std::printf("[sceneview] cam eye=(%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)\n",
                cam.eye[0], cam.eye[1], cam.eye[2], cam.target[0], cam.target[1], cam.target[2]);

    play::PerspRenderOptions ropt;
    ropt.backfaceCull = 1;   // camera is inside the enclosed office -> cull wall back-faces
    play::SceneViewStats st = play::RenderSceneObjects(objsParsed, objs, cam, fb, ropt);
    std::printf("[sceneview] meshesDrawn=%d missing=%d trisDrawn=%d pixels=%d\n",
                st.meshesDrawn, st.meshesMissing, st.trisDrawn, st.pixelsWritten);

    // Many real meshes resolved + rasterized through the perspective camera.
    // From the real cutscene-cam (dummy_A1) the enclosed Secretariat room renders
    // (walls/floor/furniture via the parent-composed world transforms).
    CHECK(st.meshesDrawn >= 5);
    CHECK(st.trisDrawn > 200);
    CHECK(st.pixelsWritten > 5000);

    // Place an sp_STADTTURM city tower at each city marker (dummy_<CITY>) — the
    // host-side VIBE_Map_SpawnCityPointMarker analogue — and confirm they resolve
    // and add geometry (camera waypoints dummy_A* are NOT matched).
    const char* kCities[] = {"HANNOVER", "BERLIN", "AUGSBURG", "DRESDEN", "KOELN"};
    int placed = 0;
    for (const char* c : kCities)
        placed += play::PlaceMeshAtMarkers(objsParsed, (std::string("dummy_") + c).c_str(),
                                           "sp_STADTTURM");
    std::printf("[sceneview] towers placed=%d\n", placed);
    CHECK(placed == 5);

    // Render the towers from a camera framing the map table (the A1 cutscene cam
    // looks the other way, at the office detail). The towers must resolve + draw.
    play::PerspCamera tableCam;
    tableCam.eye[0] = -90; tableCam.eye[1] = 78; tableCam.eye[2] = 172;
    tableCam.target[0] = -90; tableCam.target[1] = 40; tableCam.target[2] = 240;
    tableCam.fovY = 0.85f;
    play::SceneViewStats st2 = play::RenderSceneObjects(objsParsed, objs, tableCam, fb, ropt);
    std::printf("[sceneview] table view: meshesDrawn=%d missing=%d trisDrawn=%d\n",
                st2.meshesDrawn, st2.meshesMissing, st2.trisDrawn);
    CHECK(st2.meshesMissing == 0);    // sp_STADTTURM resolved for each marker
    CHECK(st2.meshesDrawn >= 5);      // the 5 towers (+ table) drew

    // TEXTURED render: mount Textures.BIN and re-render the office from the cutscene
    // camera with the real per-material textures sampled through the mesh UVs.
    if (fs.exists("Resources/Textures.BIN")) {
        play::RealTextureSource tex;
        CHECK(tex.Mount(&fs, "Resources/Textures.BIN"));
        play::SceneViewStats st3 = play::RenderSceneObjects(objsParsed, objs, cam, fb, ropt, &tex);
        std::printf("[sceneview] textured: meshesDrawn=%d trisDrawn=%d pixels=%d\n",
                    st3.meshesDrawn, st3.trisDrawn, st3.pixelsWritten);
        CHECK(st3.meshesDrawn > 8);      // the textured office still composites
        CHECK(st3.pixelsWritten > 5000);
    }

    render::SurfaceDestroy(fb);
}

// e2e: 3D city-tower picking (play::PickNearestObject — the host reconstruction of
// VIBE_Pick_FindNearestObjectAt @0x5b5a38) over the REAL ChooseCity scene. Place a
// tower at each city marker, then click where each tower projects and confirm the
// pick returns a selectable "stadt_"/tower city object — exactly the mouse-over the
// engine's RunChooseCity loop does to drive the city info forms.
TEST(SceneViewE2E, PickCityTower) {
    if (!Present()) {
        std::printf("  [skip] SceneViewE2E.Pick: real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", true));
    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    std::vector<play::SceneObjectInst> objsParsed = play::ParseSceneObjects(ed3.data(), ed3.size());
    CHECK(objsParsed.size() > 20);

    const char* kCities[] = {"HANNOVER", "BERLIN", "AUGSBURG", "DRESDEN", "KOELN"};
    int placed = 0;
    for (const char* c : kCities)
        placed += play::PlaceMeshAtMarkers(objsParsed, (std::string("dummy_") + c).c_str(),
                                           "sp_STADTTURM");
    CHECK(placed == 5);

    // Camera framing the map table (same as the render test) — sees the 5 towers.
    const int W = 800, H = 600;
    play::PerspCamera cam;
    cam.eye[0] = -90; cam.eye[1] = 78; cam.eye[2] = 172;
    cam.target[0] = -90; cam.target[1] = 40; cam.target[2] = 240;
    cam.engineProjection = true;

    // Replicate the pick's projection (a7 = W/2, screenY flipped) to find where a
    // tower lands on screen — this is the SELECTION test (which object wins), not
    // the projection formula (covered by d3_projection_test).
    auto nrm3 = [](float x, float y, float z) {
        float l = std::sqrt(x*x + y*y + z*z); return l > 1e-6f ? l : 1.0f; };
    const float fl = nrm3(cam.target[0]-cam.eye[0], cam.target[1]-cam.eye[1], cam.target[2]-cam.eye[2]);
    const float fwd[3] = {(cam.target[0]-cam.eye[0])/fl, (cam.target[1]-cam.eye[1])/fl, (cam.target[2]-cam.eye[2])/fl};
    // right = normalize(cross(up=+Y, fwd)); up = cross(fwd, right)
    float rx = 1.0f*fwd[2] - 0.0f*fwd[1], ry = 0.0f*fwd[0] - 0.0f*fwd[2], rz = 0.0f*fwd[1] - 1.0f*fwd[0];
    const float rl = nrm3(rx, ry, rz); rx/=rl; ry/=rl; rz/=rl;
    const float ux = fwd[1]*rz - fwd[2]*ry, uy = fwd[2]*rx - fwd[0]*rz, uz = fwd[0]*ry - fwd[1]*rx;
    const float a7 = W * 0.5f, cx = W * 0.5f, cy = H * 0.5f;
    auto projectPos = [&](const play::SceneObjectInst& o, float& sx, float& sy) -> bool {
        const float px = o.pos[0]-cam.eye[0], py = o.pos[1]-cam.eye[1], pz = o.pos[2]-cam.eye[2];
        const float vx = px*rx + py*ry + pz*rz;
        const float vy = px*ux + py*uy + pz*uz;
        const float vz = px*fwd[0] + py*fwd[1] + pz*fwd[2];
        if (vz <= 1e-3f) return false;
        sx = cx + a7*vx/vz; sy = cy - a7*vy/vz; return true;
    };

    int cityHits = 0, exactHits = 0;
    for (std::size_t i = 0; i < objsParsed.size(); ++i) {
        const auto& o = objsParsed[i];
        if (o.name.rfind("tower:dummy_", 0) != 0) continue;
        float sx = 0, sy = 0;
        if (!projectPos(o, sx, sy)) continue;
        if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;   // off-screen tower
        int picked = play::PickNearestObject(objsParsed, objs, cam, W, H, sx, sy);
        CHECK(picked >= 0);
        if (picked >= 0) {
            CHECK(play::IsCityMarkerName(objsParsed[picked].name));
            ++cityHits;
            if ((std::size_t)picked == i) ++exactHits;
        }
    }
    std::printf("[pick] cityHits=%d exactHits=%d of placed=%d\n", cityHits, exactHits, placed);
    CHECK(cityHits >= 3);        // at least 3 of the 5 towers project on-screen + pick
    CHECK(exactHits >= 2);       // and the pick resolves the very tower under the cursor

    // Clicking empty space (far outside every tower's projected disc) selects nothing.
    int none = play::PickNearestObject(objsParsed, objs, cam, W, H, -5000.0f, -5000.0f);
    CHECK(none == -1);

    // Determinism: the same click always resolves to the same object.
    float ax = 0, ay = 0;
    bool found = false;
    for (const auto& o : objsParsed)
        if (o.name == "tower:dummy_AUGSBURG" && projectPos(o, ax, ay)) { found = true; break; }
    CHECK(found);
    int p1 = play::PickNearestObject(objsParsed, objs, cam, W, H, ax, ay);
    int p2 = play::PickNearestObject(objsParsed, objs, cam, W, H, ax, ay);
    CHECK(p1 == p2);

    // IsCityMarkerName matches the real and host marker naming, rejects others.
    CHECK(play::IsCityMarkerName("stadt_HANNOVER"));
    CHECK(play::IsCityMarkerName("tower:dummy_BERLIN"));
    CHECK(!play::IsCityMarkerName("ob_KARTENTISCH"));
    CHECK(!play::IsCityMarkerName("dummy_A1"));
}
