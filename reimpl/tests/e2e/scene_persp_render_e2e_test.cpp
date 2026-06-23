// e2e: play::RenderMeshPerspective draws the REAL sp_STADTTURM city-tower (the 3D
// marker the ChooseCity new-game scene spawns per city) — decoded from the shipped
// Objects.BIN via the now-working fast-chunk bgf_loader — through a perspective
// MegaCam-style look-at camera into a software surface, and dumps a BMP.
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "render/bgf_loader.h"
#include "render/geometry_types.h"
#include "render/surface.h"
#include "render/types.h"
#include "play/scene_persp_render.h"
#include "shim_impl/disk_filesystem.h"

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
    return fs.exists("Resources/Objects.BIN");
}
} // namespace

TEST(ScenePerspRenderE2E, RealCityTowerRendersInPerspective) {
    if (!Present()) {
        std::printf("  [skip] ScenePerspRenderE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    io::ArchiveMount objs;
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));

    std::vector<u8> bytes;
    CHECK(objs.OpenMember("_DYNAMIC/X_STUFF/sp_STADTTURM.bgf", bytes));
    CHECK(!bytes.empty());

    // Decode the real model (exercises the fixed fast-chunk loader).
    render::BgfModel model;
    CHECK(render::LoadFastChunk(bytes.data(), bytes.size(), model));
    CHECK(model.polyCount > 0);
    render::BgfGeometry geo;
    CHECK(render::BuildGeometry(model, geo));
    render::MeshGeometry* mesh = geo.View();
    CHECK(mesh != nullptr);
    CHECK(mesh->polyCount > 100);     // a real multi-part tower, not a quad
    std::printf("[persp] sp_STADTTURM: verts=%d polys=%d\n",
                mesh->vertexCount, mesh->polyCount);

    // Frame a look-at camera onto the tower and render it.
    const int W = 256, H = 256;
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    CHECK(fb != nullptr);

    play::PerspCamera cam;
    float rad = play::FrameCameraToMesh(*mesh, cam, /*azimuth=*/0.7f,
                                        /*elevation=*/0.28f, /*distFactor=*/2.6f);
    CHECK(rad > 0.0f);

    play::PerspRenderStats st = play::RenderMeshPerspective(*mesh, cam, fb);
    std::printf("[persp] rad=%.2f trianglesDrawn=%d pixelsWritten=%d\n",
                rad, st.trianglesDrawn, st.pixelsWritten);

    // The tower drew a substantial, depth-sorted silhouette (recognizable 3D, not
    // the ~2 faces the top-down city projection leaves of a vertical model).
    CHECK(st.trianglesDrawn > 100);
    CHECK(st.pixelsWritten > 2000);

    render::SurfaceDestroy(fb);
}
