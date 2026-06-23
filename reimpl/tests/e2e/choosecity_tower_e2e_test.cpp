// e2e: the 1:1 ChooseCity tower model (VIBE_Menu_RunChooseCity @0x52e6d8) — exactly
// ONE sp_STADTTURM tower is drawn (not one per city), seated on the default city, and
// it MOVES to a different city's marker position (the per-city "stadt_<city>" markers
// are invisible pick targets only). GUARDED on the game dir; honors GUILD_GAME_DIR.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
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
// Mirror RunCityScreen3D's object model: invisible per-city markers + one moving tower.
struct Setup { std::vector<play::SceneObjectInst> scene; int towerIdx=-1; std::vector<std::array<float,3>> cityPos; };
} // namespace

TEST(ChooseCityTower, OneTowerMovesOnPick) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] ChooseCityTower: game dir absent (%s)\n", GameDir().c_str()); CHECK(true); return; }
    io::ArchiveMount scenes,objs; CHECK(scenes.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(objs.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto base=play::ParseSceneObjects(ed3.data(),ed3.size());
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");

    const char* cities[]={"HANNOVER","BERLIN","AUGSBURG","DRESDEN","KOELN"};
    auto scene=base;
    std::vector<std::array<float,3>> cpos; int firstCity=-1;
    for (int ci=0; ci<5; ++ci) {
        std::string dn="DUMMY_"+std::string(cities[ci]);
        for (auto& d:scene) {
            std::string u; for(char c:d.name) u+=(char)std::toupper((unsigned char)c);
            if ((d.type==2||d.type==3)&&u==dn) {
                play::SceneObjectInst m; m.name="stadt_"+std::string(cities[ci]);
                m.type=4; m.mesh="sp_STADTTURM"; m.hasMesh=true; m.noRender=true;
                m.pos[0]=d.pos[0]; m.pos[1]=d.pos[1]; m.pos[2]=d.pos[2];
                cpos.push_back({d.pos[0],d.pos[1],d.pos[2]});
                scene.push_back(std::move(m)); if(firstCity<0) firstCity=ci; break;
            }
        }
    }
    CHECK(firstCity==0); CHECK(cpos.size()==5);
    // The single tower, on the default city.
    play::SceneObjectInst t; t.name="sp_STADTTURM"; t.type=4; t.mesh="sp_STADTTURM"; t.hasMesh=true; t.noPick=true;
    t.pos[0]=cpos[0][0]; t.pos[1]=cpos[0][1]; t.pos[2]=cpos[0][2];
    scene.push_back(std::move(t)); int towerIdx=(int)scene.size()-1;

    const int W=800,H=600; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    play::PerspRenderOptions ropt; ropt.ambient=0.55f; ropt.backfaceCull=1;
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));

    // Baseline: the raw room (no markers/tower) — count its drawn meshes.
    auto sBase=play::RenderSceneObjects(base,objs,cam,fb,ropt,ht?&tex:nullptr);
    // With markers + one tower: exactly ONE extra mesh draws (the 5 markers are noRender).
    auto sTower=play::RenderSceneObjects(scene,objs,cam,fb,ropt,ht?&tex:nullptr);
    CHECK(DumpBmp("/tmp/cc_tower_default.bmp",fb));
    std::printf("[tower] base meshes=%d, with 5 markers+1 tower meshes=%d (expect base+1)\n",
                sBase.meshesDrawn, sTower.meshesDrawn);
    CHECK(sTower.meshesDrawn == sBase.meshesDrawn + 1);   // only the tower is drawn, not 5

    // Move the tower to a different city (AUGSBURG, index 2) — as a click would.
    scene[towerIdx].pos[0]=cpos[2][0]; scene[towerIdx].pos[1]=cpos[2][1]; scene[towerIdx].pos[2]=cpos[2][2];
    auto sMoved=play::RenderSceneObjects(scene,objs,cam,fb,ropt,ht?&tex:nullptr);
    CHECK(DumpBmp("/tmp/cc_tower_moved.bmp",fb));
    CHECK(sMoved.meshesDrawn == sBase.meshesDrawn + 1);   // still exactly one tower
    render::SurfaceDestroy(fb);
}
