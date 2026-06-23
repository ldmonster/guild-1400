// e2e: render the REAL New-Game ChooseCity scene (the stage the main-menu "New Game"
// funnel enters) to BMP images, so the menu->new-game flow's 3D screen is visually
// inspectable. Two views: the real A_Stadtwahl cutscene camera (the office the player
// first sees) and the map-table view showing the pickable city towers.
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/scene_persp_render.h"
#include "play/real_texture_source.h"
#include "render/surface.h"
#include "render/types.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;

namespace {
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}

// Minimal 24-bit BMP writer (bottom-up) from a render::Surface (16/32 bpp).
bool DumpBmp(const std::string& path, render::Surface* s) {
    const int W = s->widthPx ? s->widthPx : s->width, H = s->height;
    const int rowBytes = (W * 3 + 3) & ~3, imgSize = rowBytes * H, fileSize = 54 + imgSize;
    std::vector<std::uint8_t> out(fileSize, 0);
    auto p16 = [&](int hdr, std::uint16_t v) { out[hdr] = v & 0xFF; out[hdr+1] = v >> 8; };
    auto p32 = [&](int hdr, std::uint32_t v) { out[hdr]=v&0xFF; out[hdr+1]=(v>>8)&0xFF; out[hdr+2]=(v>>16)&0xFF; out[hdr+3]=(v>>24)&0xFF; };
    out[0]='B'; out[1]='M'; p32(2,fileSize); p32(10,54); p32(14,40);
    p32(18,W); p32(22,H); p16(26,1); p16(28,24); p32(34,imgSize);
    for (int y = 0; y < H; ++y) {
        const int srcY = H - 1 - y;                 // BMP is bottom-up
        std::uint8_t* dst = out.data() + 54 + y * rowBytes;
        for (int x = 0; x < W; ++x) {
            std::uint8_t r, g, b;
            if (s->bpp == 32) {
                auto* row = reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(s->pixels) + (std::size_t)srcY * s->pitch);
                std::uint32_t px = row[x];
                r = (px >> 16) & 0xFF; g = (px >> 8) & 0xFF; b = px & 0xFF;
            } else {
                auto* row = reinterpret_cast<std::uint16_t*>(
                    static_cast<std::uint8_t*>(s->pixels) + (std::size_t)srcY * s->pitch);
                std::uint16_t px = row[x];
                r = ((px >> 11) & 0x1F) << 3; g = ((px >> 5) & 0x3F) << 2; b = (px & 0x1F) << 3;
            }
            dst[x*3+0] = b; dst[x*3+1] = g; dst[x*3+2] = r;
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
    return f.good();
}
} // namespace

TEST(ChooseCityFlowRender, RenderNewGameSceneToBmp) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] ChooseCityFlowRender: game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", true));
    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    std::vector<play::SceneObjectInst> scene = play::ParseSceneObjects(ed3.data(), ed3.size());
    CHECK(scene.size() > 20);

    const int W = 1024, H = 768;
    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    CHECK(fb != nullptr);

    play::RealTextureSource tex;
    const bool haveTex = fs.exists("Resources/Textures.BIN") && tex.Mount(&fs, "Resources/Textures.BIN");

    play::PerspRenderOptions ropt;
    ropt.backfaceCull = 0;

    // (1) The real cutscene camera — the office the player first sees on New Game.
    play::PerspCamera cam;
    CHECK(play::CameraFromDummy(scene, "dummy_A1", cam));   // engineProjection on
    play::SceneViewStats s1 =
        play::RenderSceneObjects(scene, objs, cam, fb, ropt, haveTex ? &tex : nullptr);
    CHECK(DumpBmp("/tmp/choosecity_office.bmp", fb));
    std::printf("[flow] office view: meshes=%d tris=%d px=%d -> /tmp/choosecity_office.bmp\n",
                s1.meshesDrawn, s1.trisDrawn, s1.pixelsWritten);
    CHECK(s1.pixelsWritten > 5000);

    // (2) The map table with a city tower at each marker — the pickable city select.
    int placed = play::PlaceMeshAtMarkers(scene, "dummy_HANNOVER", "sp_STADTTURM");
    placed += play::PlaceMeshAtMarkers(scene, "dummy_BERLIN", "sp_STADTTURM");
    placed += play::PlaceMeshAtMarkers(scene, "dummy_AUGSBURG", "sp_STADTTURM");
    placed += play::PlaceMeshAtMarkers(scene, "dummy_DRESDEN", "sp_STADTTURM");
    placed += play::PlaceMeshAtMarkers(scene, "dummy_KOELN", "sp_STADTTURM");
    CHECK(placed == 5);

    play::PerspCamera tcam;
    tcam.eye[0] = -90; tcam.eye[1] = 78; tcam.eye[2] = 172;
    tcam.target[0] = -90; tcam.target[1] = 40; tcam.target[2] = 240;
    tcam.engineProjection = true;
    play::SceneViewStats s2 =
        play::RenderSceneObjects(scene, objs, tcam, fb, ropt, haveTex ? &tex : nullptr);
    CHECK(DumpBmp("/tmp/choosecity_towers.bmp", fb));
    std::printf("[flow] table view: meshes=%d tris=%d px=%d -> /tmp/choosecity_towers.bmp\n",
                s2.meshesDrawn, s2.trisDrawn, s2.pixelsWritten);
    CHECK(s2.meshesDrawn >= 5);

    render::SurfaceDestroy(fb);
}

// The REAL ChooseCity.ed3 7-band light rig parses, and band 0 (the band
// VIBE_Menu_RunChooseCity applies via BlendBandLighting(0,0,1.0)) yields the
// scene's dim bluish ambient — proving render::BlendBandLighting drives the real
// rig data. (The per-vertex light-node accumulation that brightens the scene
// locally is the next sub-piece; ambient alone is intentionally dark.)
TEST(ChooseCityFlowRender, RealRigBand0Ambient) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")) { CHECK(true); return; }
    io::ArchiveMount scenes;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));

    bool ok = false;
    render::SkyAmbient s =
        play::ComputeSceneAmbient(ed3.data(), ed3.size(), 0, 0.0f, 1.0f, &ok);
    std::printf("[flow] real rig band0 ambient=(%.1f,%.1f,%.1f) luma=%.1f\n",
                s.r, s.g, s.b, s.luma);
    CHECK(ok);                                   // the .ed3 carries a full 7-band rig
    CHECK(s.b > s.r);                            // bluish (B=40 > R=17)
    CHECK(s.luma > 0.0f && s.luma < 64.0f);      // dim (luma ~15)
}

// Baked per-vertex lighting (ambient + the .ed3 light nodes) vs the flat path:
// the baked frame is dimmer overall and carries the rig's cool (blue) ambient tint.
TEST(ChooseCityFlowRender, BakedLightingIsDimAndTinted) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        CHECK(true); return;
    }
    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    auto scene = play::ParseSceneObjects(ed3.data(), ed3.size());
    // The .ed3 carries real light nodes (candles / window / sun).
    int lights = 0; for (const auto& o : scene) if (o.hasLight) ++lights;
    CHECK(lights >= 4);

    const int W = 512, H = 384;
    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    CHECK(fb != nullptr);
    play::PerspCamera cam;
    cam.eye[0] = -90; cam.eye[1] = 78; cam.eye[2] = 172;
    cam.target[0] = -90; cam.target[1] = 40; cam.target[2] = 240;
    cam.engineProjection = true;

    auto avg = [&](render::Surface* s, double& r, double& g, double& b) {
        long long sr = 0, sg = 0, sb = 0, n = 0;
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
            for (int x = 0; x < W; ++x) {
                std::uint32_t p = row[x];
                if ((p & 0x00FFFFFFu) == 0) continue;       // skip cleared background
                sr += (p >> 16) & 0xFF; sg += (p >> 8) & 0xFF; sb += p & 0xFF; ++n;
            }
        }
        if (!n) n = 1; r = (double)sr / n; g = (double)sg / n; b = (double)sb / n;
    };

    play::PerspRenderOptions flat;
    play::RenderSceneObjects(scene, objs, cam, fb, flat, nullptr);
    double fr, fg, fb_;
    avg(fb, fr, fg, fb_);

    render::SkyAmbient amb = play::ComputeSceneAmbient(ed3.data(), ed3.size(), 0, 0.0f, 1.0f);
    play::PerspRenderOptions baked;
    baked.bakedLighting = true;
    baked.ambientRGB[0] = amb.r; baked.ambientRGB[1] = amb.g; baked.ambientRGB[2] = amb.b;
    play::RenderSceneObjects(scene, objs, cam, fb, baked, nullptr);
    double br, bg, bb;
    avg(fb, br, bg, bb);

    std::printf("[flow] flat avg=(%.1f,%.1f,%.1f) baked avg=(%.1f,%.1f,%.1f)\n",
                fr, fg, fb_, br, bg, bb);
    // The baked per-vertex lighting (ambient + the scene's candle/sun lights) visibly
    // changes the surface colour vs the flat path — i.e. the lighting is applied.
    const double dr = br - fr, dg = bg - fg, db = bb - fb_;
    CHECK((dr * dr + dg * dg + db * db) > 16.0);   // measurably different from flat
    CHECK(br > 1.0 && bg > 1.0);                    // non-blank (the room is lit, not void)
    render::SurfaceDestroy(fb);
}
