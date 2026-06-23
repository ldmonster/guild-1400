// e2e: the reconstructed scene infra (BuildSceneCamera / engine projection / baked
// lighting / pick) is GENERAL — it renders the CharCreate player-selection scene
// (Cutscenes/Spielerauswahl.ed3, the village where RunChooseCharacter @0x52bcd4 stands
// the 9 ancestry actors) from its real MegaCam, exactly like ChooseCity.
// GUARDED on the real game dir; honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/scene_persp_render.h"
#include "play/real_texture_source.h"
#include "render/surface.h"
#include "render/sky.h"
#include "shim_impl/disk_filesystem.h"

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
} // namespace

TEST(CharCreateSceneE2E, RenderPlayerSelectionVillage) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] CharCreateScene: game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", true));
    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Cutscenes/Spielerauswahl.ed3", ed3));
    CHECK(!ed3.empty());

    u32 ver = 0;
    auto scene = play::ParseSceneObjects(ed3.data(), ed3.size(), &ver);
    CHECK(ver == 0x3A6C00BBu);
    CHECK(scene.size() > 100);              // 156 in the real scene

    // The 9 ancestry actor dummies RunChooseCharacter populates are present.
    const char* kAncestors[] = {
        "dummy_NACHT_UND_NEBEL_MANN", "dummy_NACHT_UND_NEBEL_FRAU",
        "dummy_HANDWERKSKUNST_MANN",  "dummy_HANDWERKSKUNST_FRAU",
        "dummy_KAMPF_MANN", "dummy_VERHANDELN_MANN", "dummy_VERHANDELN_FRAU",
        "dummy_RHETORIK_MANN", "dummy_RHETORIK_FRAU"};
    int found = 0;
    for (const char* a : kAncestors)
        for (const auto& o : scene) if (o.name == a) { ++found; break; }
    CHECK(found == 9);
    // The MegaCam camera object exists -> BuildSceneCamera works on this scene too.
    bool hasMega = false;
    for (const auto& o : scene) if (o.name == "MegaCam") hasMega = true;
    CHECK(hasMega);

    play::PerspCamera cam;
    CHECK(play::BuildSceneCamera(ed3.data(), ed3.size(), scene, "MegaCam", cam));

    // RunChooseCharacter lights this scene with band 4 (BlendBandLighting(4,..)); it is
    // a warmer day band than ChooseCity's band 0.
    render::SkyAmbient amb0 = play::ComputeSceneAmbient(ed3.data(), ed3.size(), 0, 0.0f, 1.0f);
    render::SkyAmbient amb4 = play::ComputeSceneAmbient(ed3.data(), ed3.size(), 4, 0.0f, 1.0f);
    CHECK(amb4.r > amb0.r);                 // band 4 is warmer/brighter in red

    const int W = 800, H = 600;
    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    CHECK(fb != nullptr);
    play::RealTextureSource tex;
    const bool haveTex = fs.exists("Resources/Textures.BIN") &&
                         tex.Mount(&fs, "Resources/Textures.BIN");
    play::PerspRenderOptions ropt;
    ropt.bakedLighting = true;
    ropt.ambientRGB[0] = amb4.r; ropt.ambientRGB[1] = amb4.g; ropt.ambientRGB[2] = amb4.b;
    play::SceneViewStats st = play::RenderSceneObjects(scene, objs, cam, fb, ropt,
                                                       haveTex ? &tex : nullptr);
    std::printf("[charcreate] eye=(%.0f,%.0f,%.0f) meshes=%d missing=%d tris=%d px=%d\n",
                cam.eye[0], cam.eye[1], cam.eye[2], st.meshesDrawn, st.meshesMissing,
                st.trisDrawn, st.pixelsWritten);
    CHECK(st.meshesDrawn > 20);             // the village (houses, tent, fence...) composites
    CHECK(st.pixelsWritten > 20000);
    render::SurfaceDestroy(fb);
}
