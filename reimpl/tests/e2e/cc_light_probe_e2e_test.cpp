// e2e: ChooseCity lighting + shadows. The scene has real light nodes (candles/window/
// sun); baked per-vertex lighting differs from flat, and the projected-shadow pass adds
// a darkening batch that dims receivers. GUARDED on the game dir; GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
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
namespace {
std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}
bool DumpBmp(const std::string& path, render::Surface* s){
    const int W=s->widthPx?s->widthPx:s->width,H=s->height;
    const int rb=(W*3+3)&~3,is=rb*H,fsz=54+is; std::vector<std::uint8_t> o(fsz,0);
    auto p16=[&](int h,std::uint16_t v){o[h]=v&0xFF;o[h+1]=v>>8;};
    auto p32=[&](int h,std::uint32_t v){o[h]=v&0xFF;o[h+1]=(v>>8)&0xFF;o[h+2]=(v>>16)&0xFF;o[h+3]=(v>>24)&0xFF;};
    o[0]='B';o[1]='M';p32(2,fsz);p32(10,54);p32(14,40);p32(18,W);p32(22,H);p16(26,1);p16(28,24);p32(34,is);
    for(int y=0;y<H;++y){int sy=H-1-y;auto*d=o.data()+54+y*rb;
        auto*row=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)sy*s->pitch);
        for(int x=0;x<W;++x){std::uint32_t px=row[x];d[x*3]=px&0xFF;d[x*3+1]=(px>>8)&0xFF;d[x*3+2]=(px>>16)&0xFF;}}
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write((const char*)o.data(),o.size()); return f.good();
}
long long luma(render::Surface* s,int W,int H){ long long t=0;
    for(int y=0;y<H;++y){auto*r=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)y*s->pitch);
        for(int x=0;x<W;++x){std::uint32_t p=r[x]; t+=((p>>16)&0xFF)+((p>>8)&0xFF)+(p&0xFF);}} return t; }
} // namespace

TEST(CcLightingShadow, BakedLitWithShadows) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) { std::printf("[skip]\n"); CHECK(true); return; }
    io::ArchiveMount sc,ob; CHECK(sc.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(ob.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(sc.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    int lights=0; for(auto&o:scene) if(o.hasLight) ++lights;
    CHECK(lights>=4);                                   // candles + window + sun
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");
    const int W=800,H=600; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));

    auto mk=[&](bool baked,bool shadows){ play::PerspRenderOptions o; o.backfaceCull=1;
        if(baked){o.bakedLighting=true;o.ambientRGB[0]=amb.r;o.ambientRGB[1]=amb.g;o.ambientRGB[2]=amb.b;} else o.ambient=0.55f;
        o.shadows=shadows; return o; };

    // baked vs flat: the baked light is non-uniform (candle-lit) -> different total luma.
    play::RenderSceneObjects(scene,ob,cam,fb,mk(false,false),ht?&tex:nullptr); long long Lflat=luma(fb,W,H);
    play::RenderSceneObjects(scene,ob,cam,fb,mk(true,false), ht?&tex:nullptr); long long Lbaked=luma(fb,W,H);
    std::printf("[lit] flat=%lld baked=%lld\n",Lflat,Lbaked);
    CHECK(Lbaked != Lflat);
    CHECK(Lbaked < Lflat);                              // candle-lit night scene is darker than flat 0.55

    // shadows: the draw-list shadow pass adds a darkening batch and lowers total luma.
    auto dlNo=play::BuildSceneDrawList(scene,ob,cam,W,H,mk(true,false),ht?&tex:nullptr);
    render::RasterizeDrawList(dlNo,fb); long long LnoSh=luma(fb,W,H);
    auto dlSh=play::BuildSceneDrawList(scene,ob,cam,W,H,mk(true,true), ht?&tex:nullptr);
    int shBatches=0,shVerts=0; for(auto&b:dlSh.batches) if(b.texId==render::kSceneShadowBatch){++shBatches;shVerts+=b.vertexCount;}
    render::RasterizeDrawList(dlSh,fb); long long Lsh=luma(fb,W,H);
    DumpBmp("/tmp/cc_baked_shadow.bmp",fb);
    std::printf("[shadow] batches=%d verts=%d luma no-shadow=%lld shadow=%lld\n",shBatches,shVerts,LnoSh,Lsh);
    CHECK(shBatches==1);
    CHECK(shVerts>0);
    CHECK(Lsh < LnoSh);                                 // shadows darken receivers
    render::SurfaceDestroy(fb);
}
