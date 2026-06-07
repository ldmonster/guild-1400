#include "shim_impl/filedump_graphics.h"
#include "render/raster.h"
#include "render/types.h"
#include <cstdint>
#include <cstdio>
using namespace guild;
int main() {
    shim::FileDumpGraphicsDevice gfx;
    gfx.configureDump("/tmp", "guild_demo", shim::FileDumpGraphicsDevice::kBmp);
    gfx.init(128, 128, 8, false);
    std::uint32_t pal[256] = {0};
    pal[0]=0x000020; pal[1]=0xE03030; pal[2]=0x30C040; pal[3]=0x3050E0; pal[4]=0xE0C040;
    gfx.setPalette(pal);
    shim::Surface* sh = gfx.backbuffer();
    for (int i=0;i<sh->height*sh->pitch;i++) ((std::uint8_t*)sh->pixels)[i]=0;
    // bridge to render::Surface over the same buffer
    render::Surface fb{};
    fb.width=sh->width; fb.height=sh->height; fb.pitch=sh->pitch;
    fb.widthPx=sh->pitch; fb.bpp=8; fb.pixels=(u8*)sh->pixels;
    fb.clipX0=0; fb.clipY0=0; fb.clipX1=sh->width; fb.clipY1=sh->height;
    render::RasterVertex q1[3]={{10,10,200},{116,20,200},{20,116,200}};
    render::RasterVertex q2[3]={{116,20,200},{110,116,200},{20,116,200}};
    render::RasterVertex t1[3]={{58,28,255},{98,98,255},{34,86,255}};
    render::RasterVertex t2[3]={{60,12,255},{84,42,255},{46,46,255}};
    render::RasterizeFlatTriangle(&fb,q1,3);
    render::RasterizeFlatTriangle(&fb,q2,3);
    render::RasterizeFlatTriangle(&fb,t1,1);
    render::RasterizeFlatTriangle(&fb,t2,4);
    gfx.present();
    std::printf("wrote %s\n", gfx.framePath(0, shim::FileDumpGraphicsDevice::kBmp).c_str());
    return 0;
}
