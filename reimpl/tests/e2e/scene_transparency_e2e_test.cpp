// gilde.exe 0x5e0358 VIBE_Render_SetBlendMode + 0x5F87B8 material byte0/byte1:
// the ChooseCity scene's window light shaft (ub_LICHTKEGEL, gl_Fenster + fx_stau dust)
// and the candle flame (ub_KERZENFLAMME, fx_feua) are MODE 2 = additive. This test
// confirms BuildSceneDrawList tags them as additive batches and that rendering them
// additively BRIGHTENS the image versus forcing the same geometry opaque.
#include "test.h"
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
#include "render/scene_drawlist.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
using namespace guild;
static std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}

static long long SumLuma(render::Surface* s){
    long long sum=0; const int W=s->widthPx?s->widthPx:s->width, H=s->height;
    for(int y=0;y<H;++y){auto*r=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)y*s->pitch);
        for(int x=0;x<W;++x){std::uint32_t p=r[x];sum+=((p>>16)&0xFF)+((p>>8)&0xFF)+(p&0xFF);}}
    return sum;
}

TEST(SceneTransparency, LightShaftIsAdditive) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] SceneTransparency: game dir absent\n"); CHECK(true); return; }
    io::ArchiveMount sc,ob; CHECK(sc.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(ob.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(sc.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN");
    CHECK(ht);
    const int W=320,H=240; render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    play::PerspRenderOptions opt; opt.backfaceCull=1; opt.bakedLighting=true; opt.shadows=true;
    opt.ambientRGB[0]=amb.r;opt.ambientRGB[1]=amb.g;opt.ambientRGB[2]=amb.b;
    auto dl=play::BuildSceneDrawList(scene,ob,play::PerspCamera{},W,H,opt,&tex);

    // The scene MUST contain additive batches (the light shaft + flame).
    int additive=0, alpha=0;
    for (const auto& b : dl.batches) {
        if (b.blend==render::kBlendAdditive) ++additive;
        if (b.blend==render::kBlendAlpha)    ++alpha;
    }
    std::printf("[transp] additive batches=%d alpha batches=%d\n", additive, alpha);
    CHECK(additive > 0);

    // Additive batches must carry a fractional opacity (material byte1/255), never 1.0.
    for (const auto& b : dl.batches)
        if (b.blend==render::kBlendAdditive) { CHECK(b.opacity > 0.0f); CHECK(b.opacity < 1.0f); }

    // Render the A0 establishing view (the window is in frame) with the additive glow,
    // then with the transparent geometry REMOVED. Additive strictly ADDS to whatever
    // opaque pixel was there (dst += src*op, src,op >= 0), so the glow image's total
    // luminance is strictly greater than the no-glow image — and never darker.
    play::PerspCamera c; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A0",c));
    play::UpdateSceneDrawListCamera(dl,c,W,H);
    render::RasterizeDrawList(dl,fb);
    const long long lumaGlow = SumLuma(fb);

    render::Scene3DDrawList dl2=dl;
    for(auto&b:dl2.batches) if(b.blend!=render::kBlendOpaque) b.vertexCount=0;  // drop transparent
    render::RasterizeDrawList(dl2,fb);
    const long long lumaNoGlow = SumLuma(fb);

    std::printf("[transp] A0 luma glow=%lld no-glow=%lld (+%lld)\n",
                lumaGlow, lumaNoGlow, lumaGlow-lumaNoGlow);
    CHECK(lumaGlow >= lumaNoGlow);   // additive never darkens the opaque image
    CHECK(lumaGlow > lumaNoGlow);    // ... and the light shaft / flame add visible light
}
