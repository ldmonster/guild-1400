// e2e: render a Cyrillic + ASCII sample with the CP1251 5x7 font, dump a BMP.
#include "test.h"
#include "render/text_cp1251.h"
#include "render/surface.h"
#include "render/types.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace guild;
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
TEST(CyrillicFont, RenderSample) {
    const int W=420,H=140; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    render::SurfaceColorFill(fb, 12,12,28);
    // CP1251 bytes. "Ганновер" = C3 E0 ED ED EE E2 E5 F0
    const char title[]={(char)0xC3,(char)0xE0,(char)0xED,(char)0xED,(char)0xEE,(char)0xE2,(char)0xE5,(char)0xF0,0};
    // "Сложность: Большая"
    const char row1[]={(char)0xD1,(char)0xEB,(char)0xEE,(char)0xE6,(char)0xED,(char)0xEE,(char)0xF1,(char)0xF2,(char)0xFC,':',' ',
                       (char)0xC1,(char)0xEE,(char)0xEB,(char)0xFC,(char)0xF8,(char)0xE0,(char)0xFF,0};
    render::DrawTextCp1251(fb, 8, 8,  title, 255,235,120);
    render::DrawTextCp1251(fb, 8, 30, "ABCDEFG abc 12345 :", 200,200,200);
    render::DrawTextCp1251(fb, 8, 50, row1, 220,220,220);
    // full uppercase alphabet
    const char abc[]={(char)0xC0,(char)0xC1,(char)0xC2,(char)0xC3,(char)0xC4,(char)0xC5,(char)0xC6,(char)0xC7,(char)0xC8,
                      (char)0xCA,(char)0xCB,(char)0xCC,(char)0xCD,(char)0xCE,(char)0xCF,(char)0xD0,(char)0xD1,(char)0xD2,
                      (char)0xD3,(char)0xD4,(char)0xD5,(char)0xD6,(char)0xD7,(char)0xD8,(char)0xD9,(char)0xDC,(char)0xDD,
                      (char)0xDE,(char)0xDF,0};
    render::DrawTextCp1251(fb, 8, 72, abc, 150,220,255);
    CHECK(DumpBmp("/tmp/cyrillic_font.bmp", fb));
    render::SurfaceDestroy(fb);
}
