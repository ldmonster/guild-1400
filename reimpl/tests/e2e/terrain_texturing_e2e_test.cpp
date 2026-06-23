#include "test.h"

// =============================================================================
// GUARDED real-asset e2e — wave-5 W5-TX: the REAL AUGSBURG ground rendered
// TEXTURED through the frame spine. The wave-4 ground drew a flat white-default
// LEVEL-SHADE; this asserts the ground now samples the REAL floor slot textures
// (WIESE/SAND/...) decoded from Textures.BIN via the W5-TILE FloorTextureResolver
// + the TextureAssetCache mounted by CityView3D.
//
//   1. CityView3D Init + LoadCity("AUGSBURG"): floor slots resolve (slot[0]=WIESE).
//   2. The textureCache() resolves the slot BMPs (24-bit, palettized) from the
//      archive (the resolved WIESE record decodes to a 64x64 texture).
//   3. A textured frame shows REAL COLOUR on the ground (not the grey white-
//      default): pixels carrying the resolved slot textures' exact texel colours
//      appear, and the ground band has many distinct colours (texture variation),
//      including the slot[0]="WIESE" texture's pixels.
//   4. Determinism: two textured renders are byte-identical.
//   5. The visual artifact /tmp/guild_tx_ground.ppm.
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
// =============================================================================
#include "play/city_view3d.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <set>
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
           fs.exists("Resources/Objects.BIN") && fs.exists("Resources/Textures.BIN");
}

std::vector<u8> Snap(render::Surface* s, int w, int h) {
    std::vector<u8> out; out.reserve((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 p[3]; render::SurfaceGetPixelRgb(s, x, y, p);
            out.push_back(p[0]); out.push_back(p[1]); out.push_back(p[2]);
        }
    return out;
}

bool DumpPpm(const char* path, const std::vector<u8>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, (std::size_t)w * h * 3, f);
    std::fclose(f);
    return true;
}

// Collect the set of 565-roundtripped RGB colours a record's texels use (the
// exact colours the textured span can write at full light — light row 62, which
// is the source colour). Keyed (r<<16|g<<8|b) after the 565 round-trip the
// surface stores.
void CollectTexColors(const render::Texture* rec, std::set<unsigned>& out) {
    if (!rec || rec->paletteStore.size() < 768) return;
    std::set<int> used;
    for (u8 t : rec->texels) used.insert((int)t);
    for (int idx : used) {
        u8 R = rec->paletteStore[3 * idx + 0];
        u8 G = rec->paletteStore[3 * idx + 1];
        u8 B = rec->paletteStore[3 * idx + 2];
        u16 v = (u16)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
        u8 rr = (u8)(((v >> 11) & 0x1F) << 3);
        u8 gg = (u8)(((v >> 5) & 0x3F) << 2);
        u8 bb = (u8)((v & 0x1F) << 3);
        out.insert(((unsigned)rr << 16) | ((unsigned)gg << 8) | bb);
    }
}

} // namespace

TEST(TerrainTexturingE2E, AugsburgGroundTextured) {
    if (!RealAssetsPresent()) {
        printf("    [skip] real game assets not present\n");
        return;
    }
    shim::DiskFileSystem fs(GameDir());

    CityView3D view;
    CHECK(view.Init(&fs));
    if (!view.mounted()) return;
    CHECK(view.LoadCity("AUGSBURG"));
    CHECK(view.hasGround());

    // ---- 1. the floor slots resolve (slot[0] = WIESE) -----------------------
    const FloorGround& g = view.ground();
    CHECK(std::string(g.typeNames[0].name) == "WIESE");
    printf("    [info] floor slots: %s %s %s %s ...\n",
           g.typeNames[0].name, g.typeNames[1].name, g.typeNames[2].name,
           g.typeNames[3].name);

    // ---- 2. the cache resolves the slot BMPs from the archive ---------------
    render::TextureAssetCache* cache = view.textureCache();
    int wieseSlot = cache->LoadByName("*WIESE.BMP", "WIESE");
    CHECK(wieseSlot >= 0);
    const render::Texture* wiese = cache->record(wieseSlot);
    CHECK(wiese != nullptr);
    CHECK_EQ(wiese->mipWidth, 64);             // the real 64x64 floor texture
    CHECK(!wiese->texels.empty());
    CHECK(wiese->paletteStore.size() >= 768);  // 24-bit BMP -> palettized
    std::set<unsigned> wieseColors;
    CollectTexColors(wiese, wieseColors);
    CHECK(wieseColors.size() > 4);             // a real (non-trivial) texture

    // all 8 slots resolve a real texture through the active resolver/cache.
    std::set<unsigned> allSlotColors = wieseColors;
    int slotsResolved = 0;
    for (int i = 0; i < 8; ++i) {
        if (!g.typeNames[i].name[0]) continue;
        std::string path = std::string("*") + g.typeNames[i].name + ".BMP";
        int s = cache->LoadByName(path.c_str(), g.typeNames[i].name);
        const render::Texture* rec = cache->record(s);
        if (rec && rec->mipWidth > 0) { ++slotsResolved; CollectTexColors(rec, allSlotColors); }
    }
    CHECK(slotsResolved >= 5);
    printf("    [info] slots resolved=%d, WIESE colors=%zu, all-slot colors=%zu\n",
           slotsResolved, wieseColors.size(), allSlotColors.size());

    // ---- 3. a textured frame: REAL COLOUR on the ground ---------------------
    CityView3D::Options opt;
    opt.fbW = 320; opt.fbH = 240;
    opt.textured = true;
    opt.terrain  = true;
    const CityCamera3D cam = view.OverviewCamera();

    CityView3D::Result r = view.RenderFrame(cam, opt);
    CHECK(r.terrainDrawn);
    CHECK(r.terrainRasterTris > 0);
    std::vector<u8> snap = Snap(view.surface(), opt.fbW, opt.fbH);

    // Count: distinct colours, coloured (NON-grey) pixels, pixels matching the
    // resolved slot textures' texel colours, and pixels matching WIESE.
    std::set<unsigned> distinct;
    int nonClear = 0, colour = 0, slotMatch = 0, wieseMatch = 0;
    for (int i = 0; i < opt.fbW * opt.fbH; ++i) {
        u8 R = snap[3 * i], G = snap[3 * i + 1], B = snap[3 * i + 2];
        if (R == 0 && G == 0 && B == 0) continue;     // clear (black sky)
        ++nonClear;
        unsigned k = ((unsigned)R << 16) | ((unsigned)G << 8) | B;
        distinct.insert(k);
        if (!(R == G && G == B)) ++colour;            // not a grey level-shade
        if (allSlotColors.count(k)) ++slotMatch;
        if (wieseColors.count(k))   ++wieseMatch;
    }
    printf("    [info] frame nonClear=%d distinct=%zu colour=%d slotTexMatch=%d "
           "wieseMatch=%d\n", nonClear, distinct.size(), colour, slotMatch, wieseMatch);

    // The ground is now genuinely TEXTURED, not a flat white-default level-shade:
    //  * many distinct colours (texture detail),
    //  * a large number of COLOURED (non-grey) pixels carrying real texel colour,
    //  * thousands of pixels matching the resolved slot textures' exact texels,
    //  * the slot[0]="WIESE" grass texture's pixels appear.
    CHECK(distinct.size() > 200);
    CHECK(colour > 2000);
    CHECK(slotMatch > 2000);
    CHECK(wieseMatch > 50);

    // ---- 4. determinism: a second textured render is byte-identical ---------
    CityView3D::Result r2 = view.RenderFrame(cam, opt);
    CHECK_EQ(r2.terrainRasterTris, r.terrainRasterTris);
    std::vector<u8> snap2 = Snap(view.surface(), opt.fbW, opt.fbH);
    CHECK(snap2 == snap);

    // ---- 5. the visual artifact --------------------------------------------
    CHECK(DumpPpm("/tmp/guild_tx_ground.ppm", snap, opt.fbW, opt.fbH));
    printf("    [info] dumped /tmp/guild_tx_ground.ppm\n");
}
