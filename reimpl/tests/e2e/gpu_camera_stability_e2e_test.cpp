// throwaway: measure GPU (Vulkan) motion flicker — render two adjacent flight frames on
// the real device and diff. Compares NEAREST vs bilinear+mips.
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
static std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}
static bool DumpBmp(const std::string& path, render::Surface* s){
    const int W=s->widthPx?s->widthPx:s->width,H=s->height; const int rb=(W*3+3)&~3,is=rb*H,fsz=54+is;
    std::vector<std::uint8_t> o(fsz,0); auto p16=[&](int h,std::uint16_t v){o[h]=v&0xFF;o[h+1]=v>>8;};
    auto p32=[&](int h,std::uint32_t v){o[h]=v&0xFF;o[h+1]=(v>>8)&0xFF;o[h+2]=(v>>16)&0xFF;o[h+3]=(v>>24)&0xFF;};
    o[0]='B';o[1]='M';p32(2,fsz);p32(10,54);p32(14,40);p32(18,W);p32(22,H);p16(26,1);p16(28,24);p32(34,is);
    for(int y=0;y<H;++y){int sy=H-1-y;auto*d=o.data()+54+y*rb; auto*row=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)sy*s->pitch);
        for(int x=0;x<W;++x){std::uint32_t px=row[x];d[x*3]=px&0xFF;d[x*3+1]=(px>>8)&0xFF;d[x*3+2]=(px>>16)&0xFF;}}
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write((const char*)o.data(),o.size()); return f.good(); }
static std::vector<std::uint32_t> grab(render::Surface* s,int W,int H){ std::vector<std::uint32_t> v((std::size_t)W*H);
    for(int y=0;y<H;++y){auto*r=reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(s->pixels)+(std::size_t)y*s->pitch); for(int x=0;x<W;++x) v[(std::size_t)y*W+x]=r[x];} return v; }
static long long bigDiff(const std::vector<std::uint32_t>&A,const std::vector<std::uint32_t>&B,std::vector<std::uint32_t>*d,int thr,long long* lo){ long long n=0; if(lo)*lo=0;
    for(std::size_t i=0;i<A.size();++i){int dr=std::abs((int)((A[i]>>16)&0xFF)-(int)((B[i]>>16)&0xFF));int dg=std::abs((int)((A[i]>>8)&0xFF)-(int)((B[i]>>8)&0xFF));int db=std::abs((int)(A[i]&0xFF)-(int)(B[i]&0xFF));
        int m=std::max({dr,dg,db}); if(m>thr){++n; if(d)(*d)[i]=0xFFFF0000u;} else if(d)(*d)[i]=A[i]; if(lo&&m>15)++*lo;} return n; }
static bool DumpVec(const std::string& path,const std::vector<std::uint32_t>&px,int W,int H){
    const int rb=(W*3+3)&~3,is=rb*H,fsz=54+is; std::vector<std::uint8_t> o(fsz,0);
    auto p16=[&](int h,std::uint16_t v){o[h]=v&0xFF;o[h+1]=v>>8;}; auto p32=[&](int h,std::uint32_t v){o[h]=v&0xFF;o[h+1]=(v>>8)&0xFF;o[h+2]=(v>>16)&0xFF;o[h+3]=(v>>24)&0xFF;};
    o[0]='B';o[1]='M';p32(2,fsz);p32(10,54);p32(14,40);p32(18,W);p32(22,H);p16(26,1);p16(28,24);p32(34,is);
    for(int y=0;y<H;++y){int sy=H-1-y;auto*d=o.data()+54+y*rb; for(int x=0;x<W;++x){std::uint32_t p=px[(std::size_t)sy*W+x];d[x*3]=p&0xFF;d[x*3+1]=(p>>8)&0xFF;d[x*3+2]=(p>>16)&0xFF;}}
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write((const char*)o.data(),o.size()); return f.good(); }
TEST(GpuCameraStability, SupersampleKillsFlicker) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/scenes.BIN")||!fs.exists("Resources/Textures.BIN")) { CHECK(true); return; }
    io::ArchiveMount sc,ob; CHECK(sc.Mount(&fs,"Resources/scenes.BIN",true)); CHECK(ob.Mount(&fs,"Resources/Objects.BIN",true));
    std::vector<u8> ed3; CHECK(sc.OpenMember("Menu/ChooseCity.ed3",ed3));
    auto scene=play::ParseSceneObjects(ed3.data(),ed3.size());
    render::SkyAmbient amb=play::ComputeSceneAmbient(ed3.data(),ed3.size(),0,0.0f,1.0f);
    play::RealTextureSource tex; CHECK(tex.Mount(&fs,"Resources/Textures.BIN"));
    const int W=800,H=600;
    shim::VulkanGraphicsDevice dev; if(!dev.init(W,H,32,false)||!dev.inited()){ std::printf("[skip] no vk\n"); CHECK(true); return; }
    render::Surface* fb=render::SurfaceCreate(W,H,32); CHECK(fb);
    const std::vector<std::string> flight={"dummy_A0","dummy_A1","dummy_A2"};
    auto run=[&](bool bilinear,int ss){
        const int RW=W*ss, RH=H*ss;
        play::PerspRenderOptions opt; opt.backfaceCull=1; opt.bakedLighting=true; opt.shadows=true; opt.bilinear=bilinear;
        opt.ambientRGB[0]=amb.r;opt.ambientRGB[1]=amb.g;opt.ambientRGB[2]=amb.b;
        auto dl=play::BuildSceneDrawList(scene,ob,play::PerspCamera{},RW,RH,opt,&tex);
        auto at=[&](float t){ play::PerspCamera c; play::CameraFlightAt(scene,flight,1500.0f,t,c);
            play::UpdateSceneDrawListCamera(dl,c,RW,RH); dl.geometryId=bilinear*10+ss; CHECK(dev.renderScene3D(dl,fb)); return grab(fb,W,H); };
        auto a=at(800.0f); std::vector<std::uint32_t> d((std::size_t)W*H,0); auto b=at(800.2f);
        long long lo=0; long long n=bigDiff(a,b,&d,60,&lo);
        if(bilinear){ DumpVec("/tmp/gpu_flick_diff.bmp",d,W,H); }
        std::printf("   (>15 = %lld)\n", lo);
        return n; };
    (void)&DumpBmp;
    long long nN=run(false,1), nB=run(true,1), nS=run(true,2);
    std::printf("[gpuflick] NEAREST=%lld bilinear+mips=%lld bilinear+mips+2xSSAA=%lld\n", nN, nB, nS);
    {   // dump a settled 2x-SSAA GPU frame for visual inspection
        play::PerspRenderOptions opt; opt.backfaceCull=1; opt.bakedLighting=true; opt.shadows=true; opt.bilinear=true;
        opt.ambientRGB[0]=amb.r;opt.ambientRGB[1]=amb.g;opt.ambientRGB[2]=amb.b;
        auto dl=play::BuildSceneDrawList(scene,ob,play::PerspCamera{},W*2,H*2,opt,&tex);
        play::PerspCamera c; CHECK(play::BuildSceneCamera(ed3.data(),ed3.size(),scene,"dummy_A2",c));
        play::UpdateSceneDrawListCamera(dl,c,W*2,H*2); dl.geometryId=99;
        if(dev.renderScene3D(dl,fb)) DumpVec("/tmp/cc_ssaa.bmp", grab(fb,W,H), W, H);
    }
    CHECK(nN > 300);            // NEAREST shimmers heavily under motion
    CHECK(nB < nN / 3);         // bilinear+mips cuts it
    CHECK(nS <= 8);             // +2x SSAA: high-amplitude flicker essentially gone
    dev.shutdown(); render::SurfaceDestroy(fb); CHECK(true);
}
#else
TEST(GpuCameraStability, SupersampleKillsFlicker){ CHECK(true); }
#endif
