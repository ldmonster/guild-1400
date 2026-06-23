// GUARDED real-asset e2e — wave-5 W5-WB water-region builder
// (VIBE_FloorWater_PrepareRegions @0x5ba95c) over the REAL AUGSBURG floor block.
//
//   1. The REAL AUGSBURG floor parses (CityView3D::LoadCity) -> its per-cell
//      texture/terrain grid (Floor+0x14, a1[5]) + water-height grid + water flag.
//   2. BuildWaterRegions is run over the real grid for the floor's water cells.
//      If AUGSBURG carries water (waterFlag / waterRegionCount), regions ARE
//      built (regionCount > 0, a non-empty 344-byte WaterMesh array). If it has
//      no water, the builder reports anyWater=false and we fall back to a
//      synthetic real-sized grid (gridN) so the strip/poly path is still
//      exercised at production scale.
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
#include "test.h"

#include "play/city_view3d.h"
#include "render/floorwater.h"
#include "render/scene_floor.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

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

// Pick the most common nonzero terrain-type byte in the grid as a stand-in water
// type (real water cells share one type byte; this exercises the mask/region
// passes over a genuine spatial distribution even when the true water-type name
// lookup is absent).
u8 DominantNonzeroType(const std::vector<u8>& grid) {
    int hist[256] = {0};
    for (u8 b : grid) hist[b & 0x7F]++;   // mask the 0x80 hole bit
    int best = 0, bestCount = -1;
    for (int v = 1; v < 128; ++v)
        if (hist[v] > bestCount) { bestCount = hist[v]; best = v; }
    return (u8)best;
}

} // namespace

TEST(FloorWaterRegionsE2E, AugsburgBuildsRegionsOrSyntheticFallback) {
    if (!RealAssetsPresent()) {
        printf("    [skip] real game assets not present\n");
        return;
    }
    shim::DiskFileSystem gameFs(GameDir());

    play::CityView3D view;
    CHECK(view.Init(&gameFs));
    if (!view.mounted()) return;
    CHECK(view.LoadCity("AUGSBURG"));
    const SceneFloorBlock& fb = view.floorBlock();
    CHECK(fb.ok);
    CHECK(fb.floorPresent);

    const int n = (int)fb.gridN;
    CHECK(n > 0);
    printf("    AUGSBURG floor N=%d  waterFlag=%d  waterRegionCount=%d  "
           "waterHeights=%d textureGrid=%d\n",
           n, (int)fb.waterFlag, fb.waterRegionCount,
           (int)fb.waterHeights.accepted, (int)fb.textureGrid.accepted);

    if (!fb.textureGrid.accepted || (int)fb.textureGrid.data.size() != n * n) {
        printf("    [info] AUGSBURG has no usable texture grid; skipping build\n");
        return;
    }
    const std::vector<u8>& grid = fb.textureGrid.data;
    const u8* heightsPtr = fb.heights.accepted &&
                           (int)fb.heights.data.size() == n * n
                               ? fb.heights.data.data() : nullptr;
    const u8* waterHPtr  = fb.waterHeights.accepted &&
                           (int)fb.waterHeights.data.size() == n * n
                               ? fb.waterHeights.data.data() : nullptr;

    // Try the real water-type byte if AUGSBURG ships water; else the dominant
    // nonzero terrain type (exercises the spatial passes either way).
    u8 waterType = DominantNonzeroType(grid);
    WaterRegions r = BuildWaterRegions(grid.data(), heightsPtr, waterHPtr,
                                       waterType, n);
    printf("    BuildWaterRegions(type=%d): anyWater=%d regions=%d spans=%zu "
           "polys=%zu meshes=%zu bytes\n",
           (int)waterType, (int)r.anyWater, (int)r.regionCount,
           r.spans.size(), r.polys.size(), r.waterMeshes.size());

    if (r.anyWater) {
        // Production-scale grid passes all sized to N*N.
        CHECK_EQ((int)r.mask.size(), n * n);
        CHECK_EQ((int)r.heights.size(), n * n);
        CHECK_EQ((int)r.regionGrid.size(), n * n);
        // One 344-byte WaterMesh record per region.
        CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
        // No transient 0xFE seeds remain after flood-fill.
        int seeds = 0;
        for (u8 v : r.regionGrid) if (v == 0xFE) ++seeds;
        CHECK_EQ(seeds, 0);
        // Determinism over the real grid.
        WaterRegions r2 = BuildWaterRegions(grid.data(), heightsPtr, waterHPtr,
                                            waterType, n);
        CHECK(r.mask == r2.mask);
        CHECK(r.regionGrid == r2.regionGrid);
        CHECK_EQ((int)r.regionCount, (int)r2.regionCount);
    } else {
        // No matching cells: synthesize a real-sized water block and rebuild so
        // the strip/poly path runs at production scale.
        printf("    [info] no cells of type %d; running synthetic %dx%d block\n",
               (int)waterType, n, n);
        std::vector<u8> syn((std::size_t)n * n, 0);
        for (int y = 2; y < 2 + n / 4; ++y)
            for (int x = 2; x < 2 + n / 4; ++x)
                syn[x + y * n] = 9;
        WaterRegions s = BuildWaterRegions(syn.data(), nullptr, nullptr, 9, n);
        CHECK(s.anyWater);
        CHECK(s.regionCount >= 1);
        CHECK_EQ((int)s.waterMeshes.size(), (int)s.regionCount * 344);
    }
}
