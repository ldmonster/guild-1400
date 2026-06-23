#include "test.h"

// GUARDED real-asset e2e — wave-3 render fidelity (W3-A): the "magenta polygon"
// root cause + colour-key evidence over the REAL AUGSBURG city.
//
// FINDING (pinned here): the pink/magenta areas in the 3D city frame are NOT
// colour-keyed texels drawn opaque. They are the UNTEXTURED-material fallback
// path: polys whose material has no bound texture were rasterized through the
// 8-BIT shaded triangle path (VIBE_Raster_RasterizeTexturedTriangle @0x5F7D58 +
// FillTexturedSpansShaded @0x5F7960, an 8bpp-surface span) into the 16bpp city
// framebuffer. The ambient shade byte 200 (= FinalizeVertexShadeLuma(200,200,200),
// the BuildObjectCache @0x5c8218 seed) lands as byte pairs 0xC8C8, which read
// back through the RGB565 surface format as RGB(200,24,64) — the pink.
//
// The engine never aims the 8-bit shaded span at a 16-bit surface: in the
// original, EVERY draw-list poly with a texture record goes through the 16bpp
// textured spans (0x5F71AD..0x5F744x), and a poly whose record carries no texels
// binds the 1x1 "white" default (VIBE_Texture_BindActive @0x5db564: a record
// with slot==0 binds white — see render/texture_upload.h).
//
// COLOUR-KEY evidence (the second half of this suite): the original's textured
// span family includes colour-key variants that skip palette index 0
// (VIBE_Raster_FillSpanTexturedMasked @0x5F721A "test dl,dl / jz", BlendMasked
// @0x5F7310, OrMasked @0x5F740A), selected by the SMC patchers
// (PatchSpanConstantsMasked @0x5f753f etc., texraster_recon2_spanpatch). The HW
// path keys every upload by default (texture_upload.cpp: noKey = flags&8 ? 0 :
// noTransparency, dword_64A1FC default 0) and discards keyed texels via the
// BeginScene alpha test (render_recon4_d3dcfg.cpp @0x5e010c: RS25=5, ref 191).
// This suite verifies against the real Textures.BIN which city textures actually
// carry index-0 texels and pins the masked-span entry's behaviour on them.
#include "app/wiring.h"
#include "io/archive_mount.h"
#include "io/save_world_load.h"
#include "play/city_view3d.h"
#include "play/real_texture_source.h"
#include "render/heightmap.h"
#include "render/raster_textured.h"
#include "render/scene_floor.h"
#include "render/surface.h"
#include "render/texture.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/scenes.BIN") &&
           fs.exists("Resources/Objects.BIN") &&
           fs.exists("Resources/Textures.BIN");
}

// Count pixels of the 0xC8C8 artifact colour (the 8-bit shade byte 200 pair
// read as 565 -> RGB(200,24,64)) and true magenta-key colours.
struct PixelTally {
    int pinkC8C8 = 0;      // RGB(200,24,64) — the 8bpp-into-16bpp artifact
    int magentaKey = 0;    // r>200 && b>200 && g<80 — opaque colour-key texels
    int total = 0;
};
PixelTally TallyFrame(render::Surface* s, int w, int h) {
    PixelTally t;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            ++t.total;
            if (px[0] == 200 && px[1] == 24 && px[2] == 64) ++t.pinkC8C8;
            if (px[0] > 200 && px[2] > 200 && px[1] < 80) ++t.magentaKey;
        }
    return t;
}

// P6 dump for visual verification.
void DumpPpm(const char* path, render::Surface* s, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            std::fwrite(px, 1, 3, f);
        }
    std::fclose(f);
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The city frame: pin the magenta/pink root cause + the fixed frame.
// ---------------------------------------------------------------------------
TEST(RenderFidelityW3A, CityFramePinkArtifactAndMaterialBinding) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] RenderFidelityW3A: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));

    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    view.BindWorldObjects();

    play::CityView3D::Options opt;
    opt.fbW = 320; opt.fbH = 240;
    play::CityCamera3D cam = view.OverviewCamera();
    play::CityView3D::Result r = view.RenderFrame(cam, opt);

    PixelTally t = TallyFrame(view.surface(), opt.fbW, opt.fbH);
    std::printf("[w3a] frame: raster=%d texPolys=%d boundMats=%d "
                "pinkC8C8=%d magentaKey=%d / %d px\n",
                r.rasterTris, r.texturedPolys, r.boundMaterials,
                t.pinkC8C8, t.magentaKey, t.total);
    DumpPpm("/tmp/guild_w3a_city.ppm", view.surface(), opt.fbW, opt.fbH);
    std::printf("[w3a] frame dump: /tmp/guild_w3a_city.ppm\n");

    CHECK(r.rasterTris > 1000);
    CHECK(r.texturedPolys > 1000);
    // THE PIN: before the FillTexturedSpansShaded surface-format guard
    // (render/raster.cpp) this overview frame carried 3740 pixels of the
    // 0xC8C8 artifact colour; with the guard the count is ZERO. And no
    // opaque magenta colour-key texels exist in the frame either (the pink
    // hypothesis (a) is disproved — see the file banner).
    CHECK_EQ(t.pinkC8C8, 0);
    CHECK_EQ(t.magentaKey, 0);

    // Diagnostic: which materials of the rendered members do not bind?
    play::RealTextureSource tex;
    CHECK(tex.Mount(&fs));
    play::RealMeshSource mesh;
    CHECK(mesh.MountArchive(&fs, "Resources/Objects.BIN", true));
    std::set<std::string> members;
    for (const auto& inst : view.instances())
        if (!inst.member.empty()) members.insert(inst.member);
    int mats = 0, resolved = 0, noIndices = 0, noName = 0;
    std::map<std::string, int> unresolvedNames;
    for (const auto& m : members) {
        if (!mesh.Resolve(m.c_str())) continue;
        const render::BgfModel* model = mesh.ModelFor(m.c_str());
        if (!model) continue;
        const play::MaterialTextureTable* tbl = tex.BuildTableFor(m.c_str(), *model);
        if (!tbl) continue;
        for (std::size_t mi = 0; mi < tbl->matToTex.size(); ++mi) {
            ++mats;
            int id = tbl->matToTex[mi];
            const render::DecodedBmp* bmp = tbl->TextureFor(id);
            if (!bmp || !bmp->ok) {
                ++noName;
                if (mi < model->materials.size())
                    unresolvedNames[model->materials[mi].name0]++;
            } else if (bmp->indices.empty()) {
                ++noIndices;
                unresolvedNames[std::string("24bpp:") + bmp->member]++;
            } else {
                ++resolved;
            }
        }
    }
    std::printf("[w3a] members=%zu materials=%d resolved=%d "
                "unresolvedName=%d noIndices(24bpp)=%d\n",
                members.size(), mats, resolved, noName, noIndices);
    int shown = 0;
    for (const auto& kv : unresolvedNames) {
        std::printf("[w3a]   unbound material: %-48s x%d\n",
                    kv.first.c_str(), kv.second);
        if (++shown >= 25) break;
    }
    CHECK(mats > 0);

    sim::ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// 2. Colour-key evidence over the real city textures: which carry index-0
//    texels, and what colour palette entry 0 is.
// ---------------------------------------------------------------------------
TEST(RenderFidelityW3A, CityTexturesIndexZeroSurvey) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] RenderFidelityW3A.survey: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::RealTextureSource tex;
    CHECK(tex.Mount(&fs));

    // Survey every BMP member: 8-bit ones with index-0 texels, palette[0] colour.
    int surveyed = 0, with0 = 0, pal0Magenta = 0, bpp24 = 0;
    for (const auto& mem : tex.bin().mount()->members()) {
        const std::string& n = mem.name;
        if (n.size() < 4) continue;
        const render::DecodedBmp* bmp = tex.bin().Decode(n.c_str());
        if (!bmp || !bmp->ok) continue;
        ++surveyed;
        if (bmp->indices.empty()) { ++bpp24; continue; }
        bool has0 = false;
        for (u8 i : bmp->indices) if (i == 0) { has0 = true; break; }
        if (has0) {
            ++with0;
            if (bmp->palette.size() >= 3) {
                u8 r = bmp->palette[0], g = bmp->palette[1], b = bmp->palette[2];
                if (r > 200 && b > 200 && g < 80) ++pal0Magenta;
            }
        }
    }
    std::printf("[w3a] textures surveyed=%d with index-0 texels=%d "
                "pal0=magenta=%d 24bpp=%d\n",
                surveyed, with0, pal0Magenta, bpp24);
    CHECK(surveyed > 100);
    // Pin the survey shape: many shipped textures DO carry index-0 texels
    // (the texels the masked span would key out), and NONE of them stores
    // magenta at palette entry 0 — so opaque-magenta colour-key texels cannot
    // be the pink in the frame.
    CHECK(with0 > 500);
    CHECK_EQ(pal0Magenta, 0);
    CHECK(bpp24 > 100);
}

// ---------------------------------------------------------------------------
// 3. The colour-key masked span over a REAL city texture: the masked variant
//    leaves exactly the index-0 texels untouched (the 0x5F721A skip), the
//    plain variant fills them — pinned pixel-for-pixel on real asset bytes.
// ---------------------------------------------------------------------------
TEST(RenderFidelityW3A, MaskedSpanOverRealCityTexture) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] RenderFidelityW3A.masked: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::RealTextureSource tex;
    CHECK(tex.Mount(&fs));

    // Find a real 8-bit texture with a healthy mix of index-0 and non-0 texels.
    const render::DecodedBmp* pick = nullptr;
    for (const auto& mem : tex.bin().mount()->members()) {
        const render::DecodedBmp* bmp = tex.bin().Decode(mem.name.c_str());
        if (!bmp || !bmp->ok || bmp->indices.empty() || !bmp->square) continue;
        int zeros = 0;
        for (u8 i : bmp->indices) if (i == 0) ++zeros;
        int total = (int)bmp->indices.size();
        if (zeros > total / 10 && zeros < total * 9 / 10) { pick = bmp; break; }
    }
    CHECK(pick != nullptr);
    if (!pick) return;
    std::printf("[w3a] masked-span texture: %s (%dx%d)\n",
                pick->member.c_str(), pick->width, pick->height);

    // Build the render::Texture + 565 LUT exactly as the city binder does.
    render::Texture T;
    render::TextureSetSize(T, pick->width);
    std::copy(pick->indices.begin(),
              pick->indices.begin() +
                  std::min(T.texels.size(), pick->indices.size()),
              T.texels.begin());
    std::vector<u16> pal(256, 0);
    for (int i = 0; i < 256 && pick->palette.size() >= 768; ++i) {
        const u8 r = pick->palette[3 * i + 0];
        const u8 g = pick->palette[3 * i + 1];
        const u8 b = pick->palette[3 * i + 2];
        pal[(std::size_t)i] =
            (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    // Make palette entry 0 a sentinel magenta so a plain-span idx-0 write is
    // unmistakable (this is exactly the "drawn opaque" failure mode).
    pal[0] = 0xF81F;

    auto renderOnce = [&](bool masked) {
        render::Surface* s = render::SurfaceCreate(128, 128, 16);
        std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
        const float w = (float)T.mipWidth;
        render::RgbzVertex v[3] = {
            {4.0f, 4.0f, 0.0f, 0.0f},
            {120.0f, 8.0f, 1.0f * w, 0.0f},
            {8.0f, 120.0f, 0.0f, 1.0f * w},
        };
        int drew = masked
            ? render::RasterizeTexturedTriangleRgbzMasked(s, v, T, pal.data())
            : render::RasterizeTexturedTriangleRgbz(s, v, T, pal.data());
        CHECK(drew != 0);
        return s;
    };
    render::Surface* plain  = renderOnce(false);
    render::Surface* masked = renderOnce(true);

    int magentaPlain = 0, magentaMasked = 0, diff = 0;
    const u16* pp = (const u16*)plain->pixels;
    const u16* mp = (const u16*)masked->pixels;
    for (int i = 0; i < plain->widthPx * plain->height; ++i) {
        if (pp[i] == 0xF81F) ++magentaPlain;
        if (mp[i] == 0xF81F) ++magentaMasked;
        if (pp[i] != mp[i]) {
            ++diff;
            // every difference is an idx-0 pixel: plain wrote the sentinel,
            // masked left the cleared framebuffer untouched.
            CHECK_EQ((int)pp[i], 0xF81F);
            CHECK_EQ((int)mp[i], 0);
        }
    }
    std::printf("[w3a] masked-span: plain magenta=%d masked magenta=%d diff=%d\n",
                magentaPlain, magentaMasked, diff);
    CHECK(magentaPlain > 0);            // the plain span DID draw idx-0 texels
    CHECK_EQ(magentaMasked, 0);         // the colour key removed every one
    CHECK_EQ(diff, magentaPlain);       // and changed nothing else

    render::SurfaceDestroy(plain);
    render::SurfaceDestroy(masked);
}

// ---------------------------------------------------------------------------
// 4. TERRAIN HEIGHTS (wave-3 finding): the city elevation grid is STORED in
//    the scene stream's floor block ("<scene>_height" — LoadFloorRegions
//    @0x5e78a8 head), not derived from a ground mesh. Every shipped city
//    scene parses; AUGSBURG's embedded .cty scene stream carries the same
//    record; the built Heightmap (BuildTerrainMesh @0x5c5610 scales) samples
//    NON-FLAT ground through the 1:1 WorldToTileWithHeight / AverageAreaHeight.
// ---------------------------------------------------------------------------
TEST(RenderFidelityW3A, CityTerrainHeightsFromSceneFloorBlock) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] RenderFidelityW3A.terrain: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());

    // (a) every shipped Staedte scene carries a parsable floor heights record.
    io::ArchiveMount scenes;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    int cityScenes = 0, parsed = 0, varied = 0;
    for (const auto& m : scenes.members()) {
        if (m.name.rfind("Staedte/", 0) != 0) continue;
        if (m.name.size() < 4 ||
            (m.name.substr(m.name.size() - 4) != ".ed3" &&
             m.name.substr(m.name.size() - 4) != ".ED3")) continue;
        ++cityScenes;
        std::vector<u8> bytes;
        if (!scenes.OpenMember(m.name.c_str(), bytes)) continue;
        render::SceneFloorHeights f = render::ParseSceneFloorHeights(bytes);
        if (!f.ok) continue;
        ++parsed;
        u8 mn = 255, mx = 0;
        for (u8 h : f.heights) { if (h < mn) mn = h; if (h > mx) mx = h; }
        if (mx > mn) ++varied;
        std::printf("[w3a] %-40s %ux%u h[%u..%u]\n", m.name.c_str(),
                    f.sizeX, f.sizeY, mn, mx);
    }
    std::printf("[w3a] terrain: cityScenes=%d parsed=%d varied=%d\n",
                cityScenes, parsed, varied);
    CHECK(cityScenes >= 10);
    CHECK_EQ(parsed, cityScenes);   // the skip-walk lands on every floor block
    CHECK(varied >= 7);             // real terrain (only the MASTER seeds are flat)

    // (b) the EMBEDDED .cty scene stream (the blob PostLoadInitScene @0x5a7ef8
    //     feeds to Scene_LoadFromStream) carries the same heights record.
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);
    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));
    render::SceneFloorHeights emb = render::ParseSceneFloorHeights(sceneBlob);
    CHECK(emb.ok);
    CHECK_EQ((int)emb.sizeX, 128);
    CHECK(emb.name == "stadt_AUGSBURG_height");
    u8 emn = 255, emx = 0;
    for (u8 h : emb.heights) { if (h < emn) emn = h; if (h > emx) emx = h; }
    std::printf("[w3a] embedded .cty heights: %s %ux%u h[%u..%u]\n",
                emb.name.c_str(), emb.sizeX, emb.sizeY, emn, emx);
    CHECK(emx > emn);               // NON-FLAT city ground

    // Cross-source: the shipped stadt_AUGSBURG.ed3 grid vs the .cty-embedded one.
    std::vector<u8> shipped;
    CHECK(scenes.OpenMember("Staedte/stadt_AUGSBURG.ed3", shipped));
    render::SceneFloorHeights sf = render::ParseSceneFloorHeights(shipped);
    CHECK(sf.ok);
    int diffCells = 0;
    for (std::size_t i = 0; i < sf.heights.size() && i < emb.heights.size(); ++i)
        if (sf.heights[i] != emb.heights[i]) ++diffCells;
    std::printf("[w3a] stadt_.ed3 vs embedded grid: %d differing cells\n", diffCells);

    // (c) the built city Heightmap samples real, varied ground through the 1:1
    //     leaves the camera terrain-follow uses.
    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    float lo[3], hi[3];
    view.WorldBounds(lo, hi);
    render::Heightmap hm{};
    std::vector<u8> store;
    CHECK(render::BuildCityHeightmapFromFloor(emb, lo, hi, hm, store));

    int sampled = 0;
    float hMin = 1e9f, hMax = -1e9f;
    for (int gz = 8; gz < 120; gz += 16) {
        for (int gx = 8; gx < 120; gx += 16) {
            float w[3];
            if (!render::TileToWorld(&hm, gx, gz, w)) continue;
            int tx = -1, tz = -1;
            float h = 0;
            const float probe[3] = {w[0], 0, w[2]};
            if (!render::WorldToTileWithHeight(&hm, probe, &tx, &tz, &h)) continue;
            ++sampled;
            if (h < hMin) hMin = h;
            if (h > hMax) hMax = h;
        }
    }
    std::printf("[w3a] heightmap samples=%d worldY[%.1f..%.1f] bounds[%.1f..%.1f]\n",
                sampled, hMin, hMax, lo[1], hi[1]);
    CHECK(sampled > 30);
    CHECK(hMax - hMin > 50.0f);     // the ground VARIES in world units
    CHECK(hMin >= lo[1]);           // inside the city's vertical bounds
    CHECK(hMax <= hi[1] + (hi[1] - lo[1]));   // sane scale

    // AverageAreaHeight (0x427468, the camera anchor's ground query) at the
    // city centre returns a height inside the sampled ground band.
    const float centre[3] = {0.5f * (lo[0] + hi[0]), 0, 0.5f * (lo[2] + hi[2])};
    const double avg = render::AverageAreaHeight(&hm, centre);
    std::printf("[w3a] AverageAreaHeight(centre) = %.2f\n", avg);
    CHECK(avg >= hMin - 50.0);
    CHECK(avg <= hMax + 50.0);

    sim::ResetEntityArrays();
}
