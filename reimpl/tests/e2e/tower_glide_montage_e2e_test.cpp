// e2e: render the tower mid-slide between two cities (the 1:1 object-anim glide) at
// several fractions, dumping frames so the smooth move is visually inspectable.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
#include "render/scene_drawlist.h"
#include "render/surface.h"
#include "render/types.h"
#include "shim_impl/disk_filesystem.h"
#include <array>
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
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write(reinterpret_cast<const char*>(o.data()),o.size()); return f.good();
}
} // namespace

TEST(TowerGlideMontage, SlideBetweenCities) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] TowerGlideMontage: game dir absent\n"); CHECK(true); return; }
    io::ArchiveMount scenes,objs; CHECK(scenes.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(objs.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");

    // Marker positions for the two endpoints.
    auto markerPos=[&](const char* city, float out[3])->bool{
        std::string dn="DUMMY_"+std::string(city);
        for (auto& d:scene){ std::string u; for(char c:d.name) u+=(char)std::toupper((unsigned char)c);
            if((d.type==2||d.type==3)&&u==dn){ out[0]=d.pos[0];out[1]=d.pos[1];out[2]=d.pos[2]; return true; } }
        return false;
    };
    float fromP[3], toP[3];
    CHECK(markerPos("HANNOVER", fromP)); CHECK(markerPos("AUGSBURG", toP));

    // One tower, slid between the two endpoints.
    play::SceneObjectInst t; t.name="sp_STADTTURM"; t.type=4; t.mesh="sp_STADTTURM"; t.hasMesh=true; t.noPick=true;
    scene.push_back(std::move(t)); int towerIdx=(int)scene.size()-1;

    const int W=800,H=600; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));
    play::PerspRenderOptions opt; opt.backfaceCull=1; opt.ambient=0.55f;

    const float dirx=toP[0]-fromP[0], diry=toP[1]-fromP[1], dirz=toP[2]-fromP[2];
    const float fracs[5]={0.0f,0.25f,0.5f,0.75f,1.0f};
    float prevProj=-1e9f;
    for (int i=0;i<5;++i){
        float p[3]; play::SampleTowerGlide(fromP, toP, fracs[i], p);
        scene[towerIdx].pos[0]=p[0]; scene[towerIdx].pos[1]=p[1]; scene[towerIdx].pos[2]=p[2];
        auto dl=play::BuildSceneDrawList(scene,objs,cam,W,H,opt,ht?&tex:nullptr);
        render::RasterizeDrawList(dl, fb);
        char path[96]; std::snprintf(path,sizeof(path),"/tmp/tower_glide_%d.bmp",i);
        CHECK(DumpBmp(path, fb));
        // progress along the from->to segment (0..1)
        float proj=((p[0]-fromP[0])*dirx+(p[1]-fromP[1])*diry+(p[2]-fromP[2])*dirz);
        std::printf("[glide] frac=%.2f tower=(%.1f,%.1f,%.1f) proj=%.3f\n", fracs[i], p[0],p[1],p[2], proj);
        CHECK(proj >= prevProj - 1e-3f);          // monotonically advances toward the target
        prevProj=proj;
    }
}
