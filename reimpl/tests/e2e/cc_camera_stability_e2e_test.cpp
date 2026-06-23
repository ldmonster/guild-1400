// e2e: ChooseCity camera-motion stability. A sub-pixel camera move must not flip many
// pixels (no flicker). Bilinear filtering removes the NEAREST texel shimmer on the
// detailed walls/floor. GUARDED on the game dir; honors GUILD_GAME_DIR.
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
std::vector<std::uint32_t> grab(render::Surface* s,int W,int H){ std::vector<std::uint32_t> v((std::size_t)W*H);
    for(int y=0;y<H;++y){auto*r=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)y*s->pitch);
        for(int x=0;x<W;++x) v[(std::size_t)y*W+x]=r[x];} return v; }
long long bigDiff(const std::vector<std::uint32_t>&A,const std::vector<std::uint32_t>&B){ long long n=0;
    for(std::size_t i=0;i<A.size();++i){
        int dr=std::abs((int)((A[i]>>16)&0xFF)-(int)((B[i]>>16)&0xFF));
        int dg=std::abs((int)((A[i]>>8)&0xFF)-(int)((B[i]>>8)&0xFF));
        int db=std::abs((int)(A[i]&0xFF)-(int)(B[i]&0xFF));
        if(std::max({dr,dg,db})>60)++n; } return n; }
} // namespace

TEST(CcCameraStability, BilinearKillsShimmer) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")||!fs.exists("Resources/Textures.BIN")) {
        std::printf("[skip]\n"); CHECK(true); return; }
    io::ArchiveMount sc,ob; CHECK(sc.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(ob.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(sc.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    play::RealTextureSource tex; CHECK(tex.Mount(&fs,"Resources/Textures.BIN"));
    const int W=800,H=600; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    const std::vector<std::string> flight={"dummy_A0","dummy_A1","dummy_A2"};

    auto runFlicker=[&](bool bilinear){
        play::PerspRenderOptions opt; opt.backfaceCull=1; opt.bakedLighting=true; opt.shadows=true; opt.bilinear=bilinear;
        opt.ambientRGB[0]=amb.r; opt.ambientRGB[1]=amb.g; opt.ambientRGB[2]=amb.b;
        auto dl=play::BuildSceneDrawList(scene,ob,play::PerspCamera{},W,H,opt,&tex);
        auto at=[&](float t){ play::PerspCamera c; play::CameraFlightAt(scene,flight,1500.0f,t,c);
            play::UpdateSceneDrawListCamera(dl,c,W,H); render::RasterizeDrawList(dl,fb); return grab(fb,W,H); };
        auto s1=at(800.0f); auto s2=at(800.0f); CHECK(bigDiff(s1,s2)==0);   // deterministic
        return bigDiff(at(800.0f), at(800.2f));                            // sub-pixel move
    };
    long long nearest = runFlicker(false);
    long long bilin   = runFlicker(true);
    std::printf("[stability] sub-pixel flip pixels: NEAREST=%lld bilinear=%lld\n", nearest, bilin);
    CHECK(nearest > 300);                       // NEAREST shimmers a lot under motion
    CHECK(bilin < nearest / 3);                 // bilinear cuts it dramatically
    CHECK(bilin < 200);
    render::SurfaceDestroy(fb);
}
