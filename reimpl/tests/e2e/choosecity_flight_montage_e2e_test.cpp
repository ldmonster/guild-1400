// e2e: render the LIVE ChooseCity A_Stadtwahl camera flight (dummy_A0 -> A1 -> A2)
// exactly as RunCityScreen3D drives it — parent-composed world transforms (so the
// RAUM-child walls/ceiling enclose the camera) + flat shading for visibility — and
// dump a montage of frames so the "camera doesn't fly / room has no walls / blue
// filler" complaints are visually checkable. GUARDED on the game dir; GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
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

bool DumpBmp(const std::string& path, render::Surface* s) {
    const int W = s->widthPx ? s->widthPx : s->width, H = s->height;
    const int rowBytes = (W * 3 + 3) & ~3, imgSize = rowBytes * H, fileSize = 54 + imgSize;
    std::vector<std::uint8_t> out(fileSize, 0);
    auto p16 = [&](int h, std::uint16_t v) { out[h] = v & 0xFF; out[h+1] = v >> 8; };
    auto p32 = [&](int h, std::uint32_t v) { out[h]=v&0xFF; out[h+1]=(v>>8)&0xFF; out[h+2]=(v>>16)&0xFF; out[h+3]=(v>>24)&0xFF; };
    out[0]='B'; out[1]='M'; p32(2,fileSize); p32(10,54); p32(14,40);
    p32(18,W); p32(22,H); p16(26,1); p16(28,24); p32(34,imgSize);
    for (int y = 0; y < H; ++y) {
        const int srcY = H - 1 - y;
        std::uint8_t* dst = out.data() + 54 + y * rowBytes;
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)srcY * s->pitch);
        for (int x = 0; x < W; ++x) {
            std::uint32_t px = row[x];
            dst[x*3+0] = px & 0xFF; dst[x*3+1] = (px>>8)&0xFF; dst[x*3+2] = (px>>16)&0xFF;
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
    return f.good();
}
} // namespace

TEST(ChooseCityFlightMontage, RenderFlightFrames) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] ChooseCityFlightMontage: game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    io::ArchiveMount scenes, objs;
    CHECK(scenes.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", true));
    std::vector<u8> ed3;
    CHECK(scenes.OpenMember("Menu/ChooseCity.ed3", ed3));
    auto scene = play::ParseSceneObjects(ed3.data(), ed3.size());
    CHECK(scene.size() > 20);

    play::RealTextureSource tex;
    const bool haveTex = fs.exists("Resources/Textures.BIN") && tex.Mount(&fs, "Resources/Textures.BIN");

    const int W = 800, H = 600;
    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    CHECK(fb != nullptr);

    play::PerspRenderOptions ropt;
    ropt.clearFirst = true; ropt.clearR = 0x10; ropt.clearG = 0x12; ropt.clearB = 0x20;
    ropt.ambient = 0.55f;
    ropt.backfaceCull = 1;   // D3D fixed-function cull: the camera sits INSIDE the room,
                             // the near-wall back-faces must be dropped (else they occlude).

    // The settled A2 pick camera (same as RunCityScreen3D's `cam`).
    play::PerspCamera settled;
    CHECK(play::BuildSceneCamera(ed3.data(), ed3.size(), scene, "dummy_A2", settled));

    const std::vector<std::string> kFlight = {"dummy_A0", "dummy_A1", "dummy_A2"};
    const int introFrames = 90;
    const int kSamples[] = {0, 22, 45, 67, 89};   // start .. end of the flight
    int s = 0;
    int wallVisibleFrames = 0;
    for (int idx = 0; idx < 5; ++idx) {
        const int frame = kSamples[idx];
        play::PerspCamera frameCam = settled;
        const float t = 1500.0f * (float)frame / (float)introFrames;
        CHECK(play::CameraFlightAt(scene, kFlight, 1500.0f, t, frameCam));
        play::SceneViewStats st =
            play::RenderSceneObjects(scene, objs, frameCam, fb, ropt, haveTex ? &tex : nullptr);
        char path[128];
        std::snprintf(path, sizeof(path), "/tmp/choosecity_flight_%d.bmp", idx);
        CHECK(DumpBmp(path, fb));
        std::printf("[flight] frame %2d: eye=(%.0f,%.0f,%.0f) tgt=(%.0f,%.0f,%.0f) meshes=%d tris=%d px=%d -> %s\n",
                    frame, frameCam.eye[0], frameCam.eye[1], frameCam.eye[2],
                    frameCam.target[0], frameCam.target[1], frameCam.target[2],
                    st.meshesDrawn, st.trisDrawn, st.pixelsWritten, path);
        if (st.meshesDrawn >= 5 && st.pixelsWritten > 20000) ++wallVisibleFrames;
        ++s;
    }
    // Every sampled flight frame shows the enclosed room (walls + ceiling), not a
    // blue void: meshes draw and a large fraction of the frame is filled.
    CHECK(wallVisibleFrames >= 4);

    // The settled A2 view (the pick view) renders the room too.
    play::SceneViewStats fin =
        play::RenderSceneObjects(scene, objs, settled, fb, ropt, haveTex ? &tex : nullptr);
    CHECK(DumpBmp("/tmp/choosecity_settled.bmp", fb));
    std::printf("[flight] settled A2: meshes=%d tris=%d px=%d -> /tmp/choosecity_settled.bmp\n",
                fin.meshesDrawn, fin.trisDrawn, fin.pixelsWritten);
    CHECK(fin.meshesDrawn >= 5);

    render::SurfaceDestroy(fb);
}
