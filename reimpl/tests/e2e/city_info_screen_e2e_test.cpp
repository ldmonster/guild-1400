// e2e: the full ChooseCity screen with the real info window composited (scene +
// the selected city's description card), as RunCityScreen3D presents it.
#include "test.h"
#include "io/archive_mount.h"
#include "play/city_info.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
#include "render/scene_drawlist.h"
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
static std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}
static bool DumpBmp(const std::string& path, render::Surface* s){
    const int W=s->widthPx?s->widthPx:s->width,H=s->height;
    const int rb=(W*3+3)&~3,is=rb*H,fsz=54+is; std::vector<std::uint8_t> o(fsz,0);
    auto p16=[&](int h,std::uint16_t v){o[h]=v&0xFF;o[h+1]=v>>8;};
    auto p32=[&](int h,std::uint32_t v){o[h]=v&0xFF;o[h+1]=(v>>8)&0xFF;o[h+2]=(v>>16)&0xFF;o[h+3]=(v>>24)&0xFF;};
    o[0]='B';o[1]='M';p32(2,fsz);p32(10,54);p32(14,40);p32(18,W);p32(22,H);p16(26,1);p16(28,24);p32(34,is);
    for(int y=0;y<H;++y){int sy=H-1-y;auto*d=o.data()+54+y*rb;
        auto*row=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)sy*s->pitch);
        for(int x=0;x<W;++x){std::uint32_t px=row[x];d[x*3]=px&0xFF;d[x*3+1]=(px>>8)&0xFF;d[x*3+2]=(px>>16)&0xFF;}}
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write(reinterpret_cast<const char*>(o.data()),o.size()); return f.good();
}
TEST(CityInfoScreen, SceneWithInfoWindow) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")||!fs.exists("Resources/textbin_deutsch.BIN")) {
        std::printf("  [skip] assets absent\n"); CHECK(true); return; }
    io::ArchiveMount scenes,objs; CHECK(scenes.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(objs.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");
    play::CityInfoText cityText; CHECK(cityText.Load(&fs));
    play::CityInfoGfx cityGfx; CHECK(cityGfx.Load(&fs));

    const int W=800,H=600; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));
    // Live screen settings: baked candle lighting + projected shadows.
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    play::PerspRenderOptions opt; opt.backfaceCull=1; opt.bakedLighting=true; opt.shadows=true; opt.bilinear=true;
    opt.ambientRGB[0]=amb.r; opt.ambientRGB[1]=amb.g; opt.ambientRGB[2]=amb.b;
    auto dl=play::BuildSceneDrawList(scene,objs,cam,W,H,opt,ht?&tex:nullptr);
    render::RasterizeDrawList(dl, fb);

    // The real info window (parchment panel + crest + AUSWAHL button), bottom-center.
    auto L = play::RenderCityInfoWindow(fb, W, H, cityText, cityGfx, "HANNOVER");
    std::printf("[infownd] panel=(%d,%d %dx%d) button=(%d,%d %dx%d)\n",
                L.panelX,L.panelY,L.panelW,L.panelH, L.btnX,L.btnY,L.btnW,L.btnH);
    CHECK(L.valid);
    CHECK(L.btnW > 0 && L.btnH > 0);

    CHECK(DumpBmp("/tmp/city_info_screen.bmp", fb));
    CHECK(true);
}
