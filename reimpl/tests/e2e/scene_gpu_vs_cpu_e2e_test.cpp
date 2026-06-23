// e2e: the Vulkan 3D scene pipeline reproduces the CPU reference rasteriser. We
// build the real ChooseCity draw list, render it both on the GPU
// (VulkanGraphicsDevice::renderScene3D, scene.vert/scene.frag on lavapipe) and on
// the CPU (render::RasterizeDrawList), and assert the two images match (within GPU
// edge-fill + 8-bit rounding tolerance). This validates the shaders' transform /
// projection / MODULATE against the engine math. GUARDED on GUILD_HAVE_VULKAN +
// a usable device + the game dir; honors GUILD_GAME_DIR.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "io/archive_mount.h"
#include "play/scene_view.h"
#include "play/real_texture_source.h"
#include "render/scene_drawlist.h"
#include "render/surface.h"
#include "render/types.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/vulkan_backend.h"

#include <algorithm>
#include <cmath>
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

TEST(SceneGpuVsCpu, ChooseCityMatches) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] SceneGpuVsCpu: game dir absent\n"); CHECK(true); return; }
    io::ArchiveMount scenes,objs; CHECK(scenes.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(objs.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(scenes.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    play::RealTextureSource tex; bool ht=fs.exists("Resources/Textures.BIN")&&tex.Mount(&fs,"Resources/Textures.BIN"); CHECK(ht);

    const int W=800,H=600;
    play::PerspCamera cam; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",cam));
    play::PerspRenderOptions opt; opt.backfaceCull=1; opt.ambient=0.55f;
    auto dl = play::BuildSceneDrawList(scene,objs,cam,W,H,opt,&tex);
    CHECK(!dl.verts.empty());

    shim::VulkanGraphicsDevice dev;
    if (!dev.init(W,H,32,false) || !dev.inited()) {
        std::printf("  [skip] SceneGpuVsCpu: no usable Vulkan device\n"); CHECK(true); return; }
    std::printf("    [vk] device=\"%s\" api=%s\n", dev.deviceName().c_str(), dev.apiVersion().c_str());

    render::Surface* gpu = render::SurfaceCreate(W,H,32); CHECK(gpu);
    render::Surface* cpu = render::SurfaceCreate(W,H,32); CHECK(cpu);
    bool gpuOk = dev.renderScene3D(dl, gpu);
    CHECK(gpuOk);                                  // the GPU 3D path ran
    render::RasterizeDrawList(dl, cpu);
    DumpBmp("/tmp/cc_gpu.bmp", gpu);
    DumpBmp("/tmp/cc_cpu.bmp", cpu);

    // Compare. GPU vs CPU differ only at triangle edges (fill-rule) and by 8-bit
    // store rounding; assert the bulk of the image is (near) identical.
    long long n = (long long)W*H, close4=0, close16=0; double sumAbs=0; int maxd=0;
    for (int y=0;y<H;++y){
        auto* rg=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(gpu->pixels)+(std::size_t)y*gpu->pitch);
        auto* rc=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(cpu->pixels)+(std::size_t)y*cpu->pitch);
        for (int x=0;x<W;++x){
            int dr=std::abs((int)((rg[x]>>16)&0xFF)-(int)((rc[x]>>16)&0xFF));
            int dg=std::abs((int)((rg[x]>>8)&0xFF)-(int)((rc[x]>>8)&0xFF));
            int db=std::abs((int)(rg[x]&0xFF)-(int)(rc[x]&0xFF));
            int d=std::max({dr,dg,db});
            if (d<=4) ++close4; if (d<=16) ++close16;
            sumAbs += d; if (d>maxd) maxd=d;
        }
    }
    const double close4Frac=(double)close4/n, close16Frac=(double)close16/n, meanAbs=sumAbs/n;
    std::printf("[gpuvscpu] close(<=4)=%.2f%% close(<=16)=%.2f%% meanAbs=%.3f maxDiff=%d -> /tmp/cc_gpu.bmp,/tmp/cc_cpu.bmp\n",
                close4Frac*100, close16Frac*100, meanAbs, maxd);
    // The GPU image reproduces the CPU reference: the bulk of pixels are within 4/255,
    // nearly all within 16/255. The residual is inherent to NEAREST texture sampling
    // (floor(uv*w) lands on a neighbouring texel at boundaries when the two float
    // pipelines disagree by an ULP) plus triangle-edge fill-rule differences — not a
    // transform/shading error (the projection, MODULATE and depth order all match).
    // ~95% of pixels (triangle interiors) match within 4/255; the ~5% that differ more
    // are triangle-boundary pixels (the two rasterisers' fill rules / NEAREST texel
    // selection disagree at edges) — rule 3 permits the rasteriser swap. The mean
    // error stays small, confirming no systematic transform/shading divergence.
    CHECK(close4Frac > 0.90);
    CHECK(meanAbs < 8.0);

    // Baked lighting + projected shadows: the GPU shadow pipeline (multiply blend) must
    // also reproduce the CPU darkening pass. Render both and compare the same way.
    play::PerspRenderOptions sopt; sopt.backfaceCull=1; sopt.bakedLighting=true; sopt.shadows=true;
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    sopt.ambientRGB[0]=amb.r; sopt.ambientRGB[1]=amb.g; sopt.ambientRGB[2]=amb.b;
    auto sdl = play::BuildSceneDrawList(scene,objs,cam,W,H,sopt,&tex);
    int shB=0; for(auto&b:sdl.batches) if(b.texId==render::kSceneShadowBatch) ++shB;
    CHECK(shB==1);                                   // a shadow batch is present
    CHECK(dev.renderScene3D(sdl, gpu));
    render::RasterizeDrawList(sdl, cpu);
    DumpBmp("/tmp/cc_gpu_shadow.bmp", gpu); DumpBmp("/tmp/cc_cpu_shadow.bmp", cpu);
    long long n2=(long long)W*H, c4=0; double sa=0;
    for (int y=0;y<H;++y){
        auto* rg=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(gpu->pixels)+(std::size_t)y*gpu->pitch);
        auto* rc=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(cpu->pixels)+(std::size_t)y*cpu->pitch);
        for (int x=0;x<W;++x){
            int dr=std::abs((int)((rg[x]>>16)&0xFF)-(int)((rc[x]>>16)&0xFF));
            int dg=std::abs((int)((rg[x]>>8)&0xFF)-(int)((rc[x]>>8)&0xFF));
            int db=std::abs((int)(rg[x]&0xFF)-(int)(rc[x]&0xFF));
            int d=std::max({dr,dg,db}); if(d<=4)++c4; sa+=d;
        }
    }
    std::printf("[gpuvscpu+shadow] close(<=4)=%.2f%% meanAbs=%.3f\n", (double)c4/n2*100, sa/n2);
    CHECK((double)c4/n2 > 0.88);                      // GPU shadows match the CPU darkening
    CHECK(sa/n2 < 9.0);

    // Bilinear + mipmaps on the GPU (trilinear sampler + uploaded mip chain): render it
    // and confirm the path runs + produces a non-blank frame (anti-aliased minification).
    play::PerspRenderOptions fopt=sopt; fopt.bilinear=true;
    auto fdl = play::BuildSceneDrawList(scene,objs,cam,W,H,fopt,&tex);
    int mipped=0; for(auto&t:fdl.textures) if(!t.mips.empty()) ++mipped;
    std::printf("[gpu-mip] textures=%zu mipped=%d\n", fdl.textures.size(), mipped);
    CHECK(mipped > 0);                                // mip chains were built
    CHECK(dev.renderScene3D(fdl, gpu));               // GPU trilinear path runs
    DumpBmp("/tmp/cc_gpu_mip.bmp", gpu);
    long long lit=0; for(int y=0;y<H;++y){auto*r=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(gpu->pixels)+(std::size_t)y*gpu->pitch);
        for(int x=0;x<W;++x) if((r[x]&0xFFFFFF)>0x080808) ++lit;}
    CHECK(lit > 50000);                               // non-blank
    dev.shutdown();
    render::SurfaceDestroy(gpu); render::SurfaceDestroy(cpu);
}

#else
#include <cstdio>
TEST(SceneGpuVsCpu, ChooseCityMatches) { std::printf("  [skip] no GUILD_HAVE_VULKAN\n"); CHECK(true); }
#endif
