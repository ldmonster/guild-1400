// e2e: load the REAL per-city description (_STADTAUSWAHL_<city>_BESCHR) and render
// the ChooseCity info window card to a BMP. GUARDED on the game dir; GUILD_GAME_DIR.
#include "test.h"
#include "play/city_info.h"
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
TEST(CityInfoCard, RenderRealDescriptions) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/textbin_deutsch.BIN")) { std::printf("  [skip] no textbin\n"); CHECK(true); return; }
    play::CityInfoText txt;
    CHECK(txt.Load(&fs));
    std::printf("[card] text entries=%d\n", txt.entryCount());

    const char* cities[]={"HANNOVER","BERLIN","AUGSBURG","DRESDEN","KOELN"};
    const int W=300, H=150;
    int rendered=0;
    for (int i=0;i<5;++i){
        std::string b = txt.Beschr(cities[i]);
        std::printf("[card] %-10s beschr.len=%zu\n", cities[i], b.size());
        if (b.empty()) continue;
        render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
        render::SurfaceColorFill(fb, 40, 60, 90);   // a backing colour to see the panel frame
        play::RenderCityInfoCard(fb, 10, 10, W-20, H-20, cities[i], b);
        char path[96]; std::snprintf(path,sizeof(path),"/tmp/city_info_%s.bmp",cities[i]);
        CHECK(DumpBmp(path, fb));
        render::SurfaceDestroy(fb);
        ++rendered;
    }
    CHECK(rendered == 5);            // all five shipped cities have a real description
}
