#include "test.h"

// =============================================================================
// GUARDED real-asset e2e — terrain-ground wave 4: the REAL AUGSBURG ground drawn
// through the frame spine at the VERIFIED position (BeginUniverseFrame @0x5B3900,
// the 0x5b3a2f VIBE_Floor_RenderTerrain arm — after the clear, BEFORE the object
// walk), from the REAL parsed floor block of the city scene (LoadFloorRegions
// @0x5e78a8), under the SAME camera/projection the city objects use.
//
//   1. CityView3D Init + LoadCity("AUGSBURG"): the floor block parses, the
//      walk-ready FloorGround builds (128 grid, tileSpan 16).
//   2. Options::terrain=false vs true: the additive default keeps the frame
//      byte-identical; the opt-in fills the below-horizon band with ground.
//   3. Ground meets the buildings: FloorGroundWorldY at each placed instance's
//      (x,z) tracks the instance's world Y.
//   4. cityHeightmap(): the @0x5c47dc-filled heights/entries are live.
//   5. Determinism + the visual artifact /tmp/guild_t1_ground.ppm.
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
// =============================================================================
#include "play/city_view3d.h"
#include "render/heightmap.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") && fs.exists("Resources/scenes.BIN") &&
           fs.exists("Resources/Objects.BIN");
}

std::vector<u8> Snapshot(render::Surface* s, int w, int h) {
    std::vector<u8> out;
    out.reserve((std::size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            out.push_back(px[0]); out.push_back(px[1]); out.push_back(px[2]);
        }
    return out;
}

int NonClear(const std::vector<u8>& snap, int from, int to, int w,
             u8 cr, u8 cg, u8 cb) {
    int n = 0;
    for (int y = from; y < to; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = 3u * ((std::size_t)y * (std::size_t)w + (std::size_t)x);
            if (snap[i] != cr || snap[i + 1] != cg || snap[i + 2] != cb) ++n;
        }
    return n;
}

bool DumpPpm(const char* path, const std::vector<u8>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, (std::size_t)w * h * 3, f);
    std::fclose(f);
    return true;
}

} // namespace

TEST(TerrainGroundE2E, AugsburgGroundThroughFrameSpine) {
    if (!RealAssetsPresent()) {
        printf("    [skip] real game assets not present\n");
        return;
    }
    shim::DiskFileSystem fs(GameDir());

    CityView3D view;
    CHECK(view.Init(&fs));
    if (!view.mounted()) return;
    CHECK(view.LoadCity("AUGSBURG"));

    // ---- 1. the REAL floor block parsed from the loaded scene ---------------
    const render::SceneFloorBlock& fb = view.floorBlock();
    CHECK(fb.ok);
    CHECK(fb.floorPresent);
    CHECK_EQ((int)fb.gridN, 128);
    CHECK(fb.heights.accepted);
    CHECK(fb.textureGrid.accepted);
    CHECK(view.hasGround());
    const FloorGround& g = view.ground();
    CHECK_EQ(g.size, 128);
    CHECK_EQ(g.tileSpan, 16);
    // the floor heights genuinely vary (real terrain, not a flat fill)
    u8 hMin = 255, hMax = 0;
    for (u8 h : g.heights) { if (h < hMin) hMin = h; if (h > hMax) hMax = h; }
    CHECK((int)hMax - (int)hMin > 10);

    // ---- 2. frame with vs without the ground pass ---------------------------
    CityView3D::Options opt;
    opt.fbW = 320; opt.fbH = 240;
    opt.textured = true;
    const CityCamera3D cam = view.OverviewCamera();

    opt.terrain = false;
    CityView3D::Result base = view.RenderFrame(cam, opt);
    CHECK(base.instancesDrawn > 0);
    CHECK(!base.terrainDrawn);
    CHECK_EQ(base.terrainRasterTris, 0);
    std::vector<u8> baseSnap = Snapshot(view.surface(), opt.fbW, opt.fbH);

    opt.terrain = true;
    CityView3D::Result ground = view.RenderFrame(cam, opt);
    CHECK(ground.terrainDrawn);
    CHECK(ground.terrainTiles > 0);
    CHECK(ground.terrainPolys > 0);
    CHECK(ground.terrainRasterTris > 0);
    CHECK(ground.nonClearPixels > base.nonClearPixels);
    std::vector<u8> groundSnap = Snapshot(view.surface(), opt.fbW, opt.fbH);

    // below-horizon: the bottom quarter of the overview frame is mostly ground/
    // city (non-clear), and the ground pass strictly grows that band.
    const int bandFrom = opt.fbH * 3 / 4;
    const int bandBase = NonClear(baseSnap, bandFrom, opt.fbH, opt.fbW,
                                  opt.clearR, opt.clearG, opt.clearB);
    const int bandGround = NonClear(groundSnap, bandFrom, opt.fbH, opt.fbW,
                                    opt.clearR, opt.clearG, opt.clearB);
    printf("    [info] nonClear base=%d ground=%d band base=%d ground=%d "
           "tiles=%d polys=%d tris=%d\n",
           base.nonClearPixels, ground.nonClearPixels, bandBase, bandGround,
           ground.terrainTiles, ground.terrainPolys, ground.terrainRasterTris);
    CHECK(bandGround >= bandBase);
    CHECK(bandGround > (opt.fbW * (opt.fbH - bandFrom)) / 2);  // mostly covered

    // ---- 3. the ground meets the building bases -----------------------------
    // For the placed city instances, the floor-lattice ground Y at the
    // instance's (x,z) tracks the instance's world Y (buildings sit ON the
    // terrain). Tolerance: a building cell can sit on a slope / on a levelled
    // pad, so compare against the floor's own height span.
    float ySpan = (float)((int)hMax - (int)hMin) * g.axisH[1];
    int sampled = 0, near = 0;
    double sumAbs = 0;
    for (const CityView3D::Instance& inst : view.instances()) {
        if (inst.sceneIndex < 0) continue;
        float gy = 0;
        if (!FloorGroundWorldY(g, inst.pos[0], inst.pos[2], &gy)) continue;
        const float d = std::fabs(gy - inst.pos[1]);
        sumAbs += d;
        ++sampled;
        if (d < 0.10f * ySpan) ++near;   // within 10% of the terrain Y span
    }
    CHECK(sampled > 50);
    printf("    [info] base-vs-ground: sampled=%d near=%d meanAbs=%.2f ySpan=%.2f\n",
           sampled, near, sampled ? sumAbs / sampled : 0.0, ySpan);
    // at least 60% of placed instances sit within 10% of the terrain span of
    // their cell's ground level.
    CHECK(near * 10 >= sampled * 6);

    // ---- 4. the @0x5c47dc-filled city heightmap is live ----------------------
    const render::Heightmap* hm = view.cityHeightmap();
    CHECK(hm != nullptr);
    if (hm) {
        CHECK_EQ(hm->size, 128);
        // the lit fill produced varied elevation bytes + terrain classes
        u8 eMin = 255, eMax = 0;
        bool classVariety = false;
        u8 cls0 = hm->entries[0];
        for (i32 i = 0; i < hm->size * hm->size; ++i) {
            const u8 e = hm->heights[i];
            if (e < eMin) eMin = e;
            if (e > eMax) eMax = e;
            if (hm->entries[24 * i] != cls0) classVariety = true;
        }
        CHECK((int)eMax - (int)eMin > 10);
        CHECK(classVariety);
        // TileToWorld round trip stays inside the floor's world Y band.
        float out[3];
        CHECK(render::TileToWorld(hm, hm->size / 2, hm->size / 2, out));
        CHECK(out[1] >= hm->originY - 1.0f);
        CHECK(out[1] <= 255.5f * hm->scaleY + hm->originY + 1.0f);
    }

    // ---- 5. determinism + the visual artifact -------------------------------
    CityView3D::Result again = view.RenderFrame(cam, opt);
    CHECK_EQ(again.terrainPolys, ground.terrainPolys);
    CHECK_EQ(again.terrainRasterTris, ground.terrainRasterTris);
    std::vector<u8> againSnap = Snapshot(view.surface(), opt.fbW, opt.fbH);
    CHECK(againSnap == groundSnap);

    CHECK(DumpPpm("/tmp/guild_t1_ground.ppm", groundSnap, opt.fbW, opt.fbH));
    printf("    [info] wrote /tmp/guild_t1_ground.ppm\n");
}
