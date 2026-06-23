// e2e: the backend-neutral draw-list path reproduces the engine RenderSceneObjects
// image. We render the real ChooseCity scene both ways — RenderSceneObjects (the
// oracle) vs render::RasterizeDrawList(play::BuildSceneDrawList(...)) — and assert
// the two framebuffers are (near) bit-identical. This proves the GPU path's input
// (the draw list) carries the engine's exact T&L/projection, so the Vulkan backend
// drawing the same list is 1:1. GUARDED on the game dir; honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
#include "render/scene_drawlist.h"
#include "render/surface.h"
#include "render/types.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
namespace {
std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}
// Max-abs and mismatch fraction between two 32bpp surfaces.
void Compare(render::Surface* a, render::Surface* b, int W, int H,
             int& maxDiff, double& mismatchFrac) {
    long long mism = 0; maxDiff = 0;
    for (int y = 0; y < H; ++y) {
        auto* ra = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(a->pixels)+(std::size_t)y*a->pitch);
        auto* rb = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(b->pixels)+(std::size_t)y*b->pitch);
        for (int x = 0; x < W; ++x) {
            const std::uint32_t pa = ra[x], pb = rb[x];
            int dr = std::abs((int)((pa>>16)&0xFF)-(int)((pb>>16)&0xFF));
            int dg = std::abs((int)((pa>>8)&0xFF)-(int)((pb>>8)&0xFF));
            int db = std::abs((int)(pa&0xFF)-(int)(pb&0xFF));
            int d = std::max({dr,dg,db});
            if (d > maxDiff) maxDiff = d;
            if (d > 0) ++mism;
        }
    }
    mismatchFrac = (double)mism / ((double)W*H);
}
} // namespace

TEST(SceneDrawListEquiv, MatchesRenderSceneObjects) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] SceneDrawListEquiv: game dir absent (%s)\n", GameDir().c_str()); CHECK(true); return; }
    io::ArchiveMount scenes,objs; CHECK(scenes.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(objs.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");
    CHECK(ht);

    const int W=800,H=600;
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));

    // Test both shading modes (flat textured + baked lit), both with culling on.
    for (int mode = 0; mode < 2; ++mode) {
        play::PerspRenderOptions opt; opt.backfaceCull=1; opt.ambient=0.55f;
        if (mode==1) { opt.bakedLighting=true; opt.ambientRGB[0]=17; opt.ambientRGB[1]=10; opt.ambientRGB[2]=40; }

        render::Surface* fa=render::SurfaceCreate(W,H,32); CHECK(fa);
        render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
        auto so = play::RenderSceneObjects(scene,objs,cam,fa,opt,&tex);
        auto dl = play::BuildSceneDrawList(scene,objs,cam,W,H,opt,&tex);
        auto sr = render::RasterizeDrawList(dl, fb);

        int maxDiff; double frac; Compare(fa,fb,W,H,maxDiff,frac);
        std::printf("[equiv] mode=%d objects: oracle tris=%d px=%d | drawlist batches=%zu verts=%zu tex=%zu tris=%d px=%d | maxDiff=%d mismatch=%.4f%%\n",
                    mode, so.trisDrawn, so.pixelsWritten, dl.batches.size(), dl.verts.size(), dl.textures.size(),
                    sr.trianglesDrawn, sr.pixelsWritten, maxDiff, frac*100.0);
        // The draw-list path reproduces the oracle: identical geometry (same triangle
        // and pixel counts) and the same image. Flat shading (mode 0, what the live
        // ChooseCity uses) is BIT-EXACT (maxDiff 0). Baked lighting (mode 1) differs by
        // at most 1 LSB on <1% of pixels — purely float summation association (the
        // oracle divides the interpolated Gouraud sum by 255; the draw list pre-divides
        // per vertex), below the 8-bit quantization floor, not a behavioral difference.
        CHECK(sr.trianglesDrawn == so.trisDrawn);
        CHECK(sr.pixelsWritten == so.pixelsWritten);
        CHECK(maxDiff <= 1);
        if (mode == 0) { CHECK(maxDiff == 0); CHECK(frac == 0.0); }   // flat: bit-exact
        else           CHECK(frac < 0.02);                            // baked: <1 LSB, <2% px
        render::SurfaceDestroy(fa); render::SurfaceDestroy(fb);
    }
}
