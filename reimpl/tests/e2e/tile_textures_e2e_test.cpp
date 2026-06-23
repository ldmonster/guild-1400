// GUARDED real-asset e2e — wave-5 TILE TEXTURE resolver over the REAL AUGSBURG
// floor + the REAL VFS+BMP decode path (VIBE_Texture_LoadByName @0x5da714 body in
// render/texture_asset).
//
//   1. The REAL AUGSBURG floor block parses -> its 8 terrain-type/texture-slot
//      names (Floor+0x1A64) + the min-normalized per-cell texture grid are live.
//   2. A real square indexed BMP, written to a temp DiskFileSystem and decoded
//      through TextureAssetCache::LoadByName (the real VFS slurp + BMP decode),
//      binds to a FloorTextureResolver under the FIRST real AUGSBURG slot name.
//   3. Resolve(typeByte) maps a per-cell texture id (the real grid's bytes) to the
//      decoded texture record member; the hole bit + empty slots fall back to null.
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
#include "test.h"

#include "play/city_view3d.h"
#include "render/bmp.h"
#include "render/floorgfx_recon.h"
#include "render/scene_floor.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstring>
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

// Write a real 8x8 indexed BMP (a recognisable diagonal ramp) to `dir/<file>`.
bool WriteSquareBmp(const std::string& dir, const std::string& file) {
    const int W = 8;
    u8 pixels[W * W];
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x)
            pixels[y * W + x] = (u8)((x + y) & 0x3F);   // distinct ramp
    std::vector<u8> bmp = BmpSaveIndexed(W, W, pixels, nullptr);
    std::string path = dir + "/" + file;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(bmp.data(), 1, bmp.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

TEST(TileTexE2E, AugsburgFloorSlotResolvesThroughRealDecode) {
    if (!RealAssetsPresent()) {
        printf("    [skip] real game assets not present\n");
        return;
    }
    shim::DiskFileSystem gameFs(GameDir());

    // ---- 1. the REAL AUGSBURG floor: slot names + per-cell texture grid ------
    play::CityView3D view;
    CHECK(view.Init(&gameFs));
    if (!view.mounted()) return;
    CHECK(view.LoadCity("AUGSBURG"));
    const SceneFloorBlock& fb = view.floorBlock();
    CHECK(fb.ok);
    CHECK(fb.floorPresent);
    CHECK(fb.textureGrid.accepted);

    // The 8 floor slot names (Floor+0x1A64). At least slot 0 is a real terrain name.
    char names[8][64];
    std::memset(names, 0, sizeof(names));
    int namedSlots = 0;
    for (int i = 0; i < 8 && i < fb.typeNameCount; ++i) {
        std::strncpy(names[i], fb.typeNames[i].c_str(), 63);
        if (names[i][0]) ++namedSlots;
    }
    CHECK(namedSlots >= 1);                       // AUGSBURG has real terrain slots
    printf("    AUGSBURG floor slot[0] = \"%s\"  (%d named)\n", names[0], namedSlots);

    // ---- 2. a real BMP decoded through the REAL VFS+decode path --------------
    // Use the real slot-0 name as the texture record name so FindActive(name) ties
    // the decoded record back to the floor slot. The resolver issues
    // LoadByName("*"+name+".BMP", name) (the @0x5bd010/@0x5d97e8 convention).
    const std::string base = names[0];
    std::string tmp = "/tmp";
    const std::string starFile = "*" + base + ".BMP";
    CHECK(WriteSquareBmp(tmp, starFile));         // exactly the path the resolver asks

    // Mount the temp dir as the VFS root (the real LoadByName slurps through it).
    shim::DiskFileSystem tmpFs(tmp);
    CHECK(io::VfsInit(&tmpFs, /*caseInsensitive=*/true));

    TextureAssetCache cache(32);
    // Decode the real BMP into a record under the real slot name (the real path).
    int slot0 = cache.LoadByName(starFile.c_str(), base);
    CHECK(slot0 >= 0);                            // the VFS slurp + BMP decode worked
    const Texture* decoded = cache.record(slot0);
    CHECK(decoded != nullptr);
    CHECK(decoded->name == base);
    CHECK_EQ(decoded->mipWidth, 8);              // 8x8 BMP
    CHECK_EQ((int)decoded->texels.size(), 8 * 8);

    // ---- 3. the resolver maps the real grid bytes -> the decoded record ------
    FloorTextureResolver r;
    r.Bind(names, &cache);
    CHECK(r.bound());

    // type byte 0 -> slot 0 -> the SAME decoded record (FindActive hit, no re-decode).
    const Texture* t0 = r.Resolve(0);
    CHECK(t0 != nullptr);
    CHECK(t0 == decoded);                         // resolved to the real-decoded texture
    CHECK(t0->name == base);

    // The hole bit yields no texture (ComputeTileIllumination's 0x80 gate).
    CHECK(r.Resolve(0x80) == nullptr);

    // Drive the resolver over the REAL per-cell texture grid: every grid byte 0 maps
    // to the decoded record; any byte naming an EMPTY slot maps to null. Count hits.
    int gridHits = 0, gridChecked = 0;
    if (fb.textureGrid.accepted) {
        const std::vector<u8>& grid = fb.textureGrid.data;
        for (std::size_t i = 0; i < grid.size() && gridChecked < 2048; ++i, ++gridChecked) {
            u8 id = (u8)(grid[i] & 7);
            const Texture* tx = r.Resolve(grid[i]);
            if (id == 0 && !(grid[i] & 0x80)) {
                CHECK(tx == decoded);            // slot-0 cells -> the decoded record
                ++gridHits;
            } else if (!names[id][0]) {
                CHECK(tx == nullptr);            // empty slot -> untextured
            }
        }
    }
    CHECK(gridHits > 0);                          // the floor really uses slot 0
    printf("    resolved %d slot-0 cells over the real AUGSBURG grid\n", gridHits);

    // The hook trampoline returns the same record when this resolver is active.
    SetActiveFloorTextureResolver(&r);
    CHECK(FloorTextureResolver::GetTileTextureHook(0) ==
          static_cast<const void*>(decoded));
    SetActiveFloorTextureResolver(nullptr);

    io::VfsShutdown();
}
