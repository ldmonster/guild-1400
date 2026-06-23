// Golden-vector unit tests for the render_recon4 DDraw/D3D control-logic cluster
// (guild::render). All GPU/DDraw and registry edges are routed through recording
// RenderDeviceHooks; tests are headless and assert:
//   - DepthToModeFlag mapping (0x432770)
//   - EnumModeCallback std-resolution detection (0x4336ac)
//   - EnumZBufferFormatsCallback closest-depth filter (0x5dd534)
//   - EnumTextureFormatsCallback opaque/alpha scoring + backbuffer match (0x5dce20)
//   - ConfigureSurfaceCaps bit/dword evaluation (0x432260)
//   - D3d registry config save/load round-trip (0x5d3938 / 0x5d3be8)
//   - D3D settings save/load round-trip + verbatim fillmode-key reuse (0x5e0890/0x5e0abc)
//   - BeginScene render-state call ordering & values (0x5e010c)
#include "test.h"

#include "render/render_recon4_ddraw.h"
#include "render/render_recon4_d3dcfg.h"
#include "render/render_leaves.h"  // DepthToModeFlag (0x432770, already present)

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A recording / in-memory registry + D3D-state hook block.
struct RecRegistry {
    std::map<std::string, int>         dwords;
    std::map<std::string, std::string> strings;
    std::vector<float>                 floats;
    int  version = 3;
    bool openOk  = true;
    bool closed  = false;

    // D3D render-state recorder for BeginScene.
    std::vector<std::pair<int,int>> rsCalls;  // (state, value)
    std::vector<int>                seq;       // call-order: 0 begin,1 settex,2 rs
};

RecRegistry* g_rec = nullptr;

int rOpen(const char*, std::uintptr_t) { return g_rec->openOk ? 1 : -1; }
int rClose(int) { g_rec->closed = true; return 0; }
void rSetD(int, const char* n, int v) { g_rec->dwords[n] = v; }
void rSetS(int, const char* n, const char* v) { g_rec->strings[n] = v; }
void rSetF(int, float v) { g_rec->floats.push_back(v); }
int rQD(int, const char* n) {
    if (std::string(n).find("registry_version") != std::string::npos)
        return g_rec->version;
    auto it = g_rec->dwords.find(n);
    return it == g_rec->dwords.end() ? 0 : it->second;
}
int rQDOut(int, const char* n, int* out) {
    auto it = g_rec->dwords.find(n);
    if (it == g_rec->dwords.end()) return 0;
    *out = it->second;
    return 1;
}
int rQS(int, const char* n, char* out) {
    auto it = g_rec->strings.find(n);
    if (it == g_rec->strings.end()) return 0;
    std::strcpy(out, it->second.c_str());
    return 1;
}
void rQF(int, const char*) {}

void dBegin(int) { g_rec->seq.push_back(0); }
void dSetTex(int, int, int, int) { g_rec->seq.push_back(1); }
void dRS(int, int s, int v) { g_rec->seq.push_back(2); g_rec->rsCalls.push_back({s, v}); }

RenderDeviceHooks makeHooks() {
    return RenderDeviceHooks{
        rOpen, rClose, rSetD, rSetS, rSetF, rQD, rQDOut, rQS, rQF,
        dBegin, dSetTex, dRS,
    };
}

struct HookGuard {
    RecRegistry rec;
    HookGuard() { g_rec = &rec; InstallRenderDeviceHooks(makeHooks()); }
    ~HookGuard() { ResetRenderDeviceHooks(); g_rec = nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
TEST(RenderRecon4, DepthToModeFlag) {
    CHECK_EQ(DepthToModeFlag(8),  2048);
    CHECK_EQ(DepthToModeFlag(16), 1024);
    CHECK_EQ(DepthToModeFlag(24), 512);
    CHECK_EQ(DepthToModeFlag(32), 256);
    CHECK_EQ(DepthToModeFlag(0),  0);
    CHECK_EQ(DepthToModeFlag(15), 0);
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, EnumModeCallbackStdResolutions) {
    StdResFlags f;
    u8 rec[128];

    auto mk = [&](int w, int h, int depth, u8 caps) {
        std::memset(rec, 0, sizeof rec);
        rec[76] = caps;
        *reinterpret_cast<i32*>(rec + 84) = depth;  // RGB bit-count
        *reinterpret_cast<i32*>(rec + 12) = w;
        *reinterpret_cast<i32*>(rec + 8)  = h;
    };

    // caps bit 0x40 missing -> no flag set, returns 1
    mk(800, 600, 16, 0x00);
    CHECK_EQ(EnumModeCallback(rec, 16, f), 1);
    CHECK_EQ((int)f.have_800x600, 0);

    // depth mismatch -> no flag
    mk(800, 600, 32, 0x40);
    EnumModeCallback(rec, 16, f);
    CHECK_EQ((int)f.have_800x600, 0);

    // 800x600 @16 match
    mk(800, 600, 16, 0x40);
    EnumModeCallback(rec, 16, f);
    CHECK_EQ((int)f.have_800x600, 1);

    // 1024x768
    mk(1024, 768, 16, 0x40);
    EnumModeCallback(rec, 16, f);
    CHECK_EQ((int)f.have_1024x768, 1);

    // 1152x864
    mk(1152, 864, 16, 0x40);
    EnumModeCallback(rec, 16, f);
    CHECK_EQ((int)f.have_1152x864, 1);

    // unrelated resolution leaves flags untouched
    StdResFlags g;
    mk(640, 480, 16, 0x40);
    EnumModeCallback(rec, 16, g);
    CHECK_EQ((int)g.have_800x600, 0);
    CHECK_EQ((int)g.have_1024x768, 0);
    CHECK_EQ((int)g.have_1152x864, 0);
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, EnumZBufferFormatsCallback) {
    // acc must hold at least 40 bytes: the function reads dword[5] (offset 20)
    // and writes memcpy(acc+8, fmt, 32) -> offsets 8..40. The original indexed a
    // full accumulator struct; size the buffer to that real contract (>=40).
    u8 acc[40];
    u8 fmt[32];
    auto mkFmt = [&](u32 flags, u32 zdepth) {
        std::memset(fmt, 0, sizeof fmt);
        reinterpret_cast<u32*>(fmt)[1] = flags;
        reinterpret_cast<u32*>(fmt)[3] = zdepth;
    };

    // wanted depth 16, no current best (acc[5]==0): any DDPF_ZBUFFER format copied.
    std::memset(acc, 0, sizeof acc);
    reinterpret_cast<u32*>(acc)[0] = 16;
    reinterpret_cast<u32*>(acc)[5] = 0;
    mkFmt(1024, 32);
    CHECK_EQ(EnumZBufferFormatsCallback(fmt, acc), 1);
    CHECK_EQ(reinterpret_cast<u32*>(acc + 8)[3], 32u);  // copied (dword[3] of fmt)

    // non-zbuffer (flags != 1024) ignored.
    std::memset(acc, 0, sizeof acc);
    reinterpret_cast<u32*>(acc)[0] = 16;
    mkFmt(64, 16);
    EnumZBufferFormatsCallback(fmt, acc);
    CHECK_EQ(reinterpret_cast<u32*>(acc + 8)[3], 0u);   // untouched

    // with a current best of 32, a closer (24) candidate >= wanted(16) wins.
    std::memset(acc, 0, sizeof acc);
    reinterpret_cast<u32*>(acc)[0] = 16;  // wanted
    reinterpret_cast<u32*>(acc)[5] = 32;  // best depth so far
    mkFmt(1024, 24);
    EnumZBufferFormatsCallback(fmt, acc);
    CHECK_EQ(reinterpret_cast<u32*>(acc + 8)[3], 24u);  // copied (24-16 <= 32-16)
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, EnumTextureFormatsCallbackBackbufferMatch) {
    // Build a 32-bit-RGBA format whose channel bit-counts match the back-buffer
    // and assert matchedBackbuffer flips.
    u8 acc[80];
    u8 fmt[32];
    std::memset(acc, 0, sizeof acc);
    std::memset(fmt, 0, sizeof fmt);
    auto* d = reinterpret_cast<u32*>(fmt);
    d[0] = 32;                 // size
    fmt[4] = 0x40;             // flags low byte: RGB (bit 0x40), not 1/2/0x20
    d[3] = 16;                 // rgbBitCount > 8
    d[4] = 0xF800;             // R mask -> 5 bits
    d[5] = 0x07E0;             // G mask -> 6 bits
    d[6] = 0x001F;             // B mask -> 5 bits

    SurfaceChannelBits surf;
    surf.alphaBits = 5;   // counts compared: r->alphaBits slot, g, b
    surf.greenBits = 6;
    surf.blueBits  = 5;
    surf.matchedBackbuffer = 0;

    int r = EnumTextureFormatsCallback(fmt, acc, surf);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)surf.matchedBackbuffer, 1);   // 5/6/5 matched
    // fmt copied into the alpha slot acc+44.
    CHECK_EQ(reinterpret_cast<u32*>(acc + 44)[3], 16u);

    // A format rejected by the early flag gate (DDPF_ALPHA bits set) does not match.
    SurfaceChannelBits surf2;
    surf2.alphaBits = 5; surf2.greenBits = 6; surf2.blueBits = 5;
    u8 acc2[80]; std::memset(acc2, 0, sizeof acc2);
    fmt[4] = 0x01;  // flags bit gate (v9 & 3) != 0 -> early return
    EnumTextureFormatsCallback(fmt, acc2, surf2);
    CHECK_EQ((int)surf2.matchedBackbuffer, 0);
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, ConfigureSurfaceCapsDepthBufferSelect) {
    // dev record big enough; mode record >= 824 bytes.
    u8 dev[256];
    u8 mode[824];
    std::memset(dev, 0, sizeof dev);
    std::memset(mode, 0, sizeof mode);

    // Drive the v12 gate: dev[5] bit1/0 + dev[4]&0x40 + dev[140] high/bit1.
    dev[5]   = 0x01;  // (v9 & 1)
    dev[4]   = 0x40;  // texture caps present
    dev[140] = 0x02;  // (v10 & 2) -> v11 true -> v12 true
    // depth-buffer select: mode+780 bit4 path. Want mode+820 == 3.
    dev[136] = 0x02;  // sets mode+780 bit4 (v39) so (v40 & 0x10) != 0
    // dev[136] bit5 (0x20) clear and bit3 (8) clear -> falls to bit-set branch:
    // since (v41 & 0x20)==0 and (v41 & 8)==0 -> mode+820 stays at the LABEL_27
    // path with value 1 (goto LABEL_27 leaves it at 1). Verify that path.
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 820), 1u);

    // Now force the "+820 == 3" branch: mode+780 bit4 set AND dev[136] bit5 set.
    std::memset(mode, 0, sizeof mode);
    dev[136] = 0x22;  // bit5 (0x20) set + bit1 (0x02) set => (v40&0x10)!=0 && (v41&0x20)!=0 -> 3
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 820), 3u);

    // Default blend caps fallback (no blend caps set) -> src=1, dst=2.
    std::memset(mode, 0, sizeof mode);
    dev[120] = 0; dev[121] = 0; dev[116] = 0;
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 804), 1u);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 808), 2u);
    // floortex defaults (dev+180/+184 == 0) -> 1024.
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 792), 1024u);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 796), 1024u);

    // Premium blend caps: dev[120]&0x20 && dev[116]&0x10 -> src=6,dst=5.
    std::memset(mode, 0, sizeof mode);
    dev[120] = 0x20; dev[116] = 0x10;
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 804), 6u);
    CHECK_EQ(*reinterpret_cast<u32*>(mode + 808), 5u);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x432269: `mov al,[edx+30Dh]` reads mode[781] BEFORE the memset at
// 0x432294. v52 = (u8)(32*mode[781])>>7 = bit2 of the PRE-memset mode[781],
// then mode[781] = 4*(v52&1) | (mode[781]&0xFB) after the clear. So presetting
// mode[781] bit2 (0x04) must round-trip to mode[781] bit2 set (== 0x04).
TEST(RenderRecon4, ConfigureSurfaceCapsV52PreMemsetBit) {
    u8 dev[256];
    u8 mode[824];
    std::memset(dev, 0, sizeof dev);
    std::memset(mode, 0, sizeof mode);
    dev[4] = 0;  // no texture caps -> skip the big block, isolate the v52 write.

    mode[781] = 0x04;  // pre-memset bit2 -> v52 = (32*4)>>7 = 1
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ((int)(mode[781] & 0x04), 0x04);  // 4*(v52&1) survived the memset

    // Without the bit set, v52 == 0 and mode[781] bit2 stays clear.
    std::memset(mode, 0, sizeof mode);
    ConfigureSurfaceCaps(dev, mode);
    CHECK_EQ((int)(mode[781] & 0x04), 0x00);
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, D3dRegistryConfigRoundTrip) {
    HookGuard hg;
    LodModeGlobal() = 4;  // LOD_HIGH

    D3dRegistryConfig in;
    in.resX = 1024; in.resY = 768; in.resDepth = 32; in.mode = 3 /*FULLSCREEN*/;
    in.useHardware = 1; in.useTriple = 0; in.maxPolys = 50000; in.maxTextures = 256;
    in.maxPalettes = 8; in.maxMipmaps = 4; in.zBufferDepth = 16; in.staticTexFcc = 1;
    in.floortexDiv = 2;

    int rc = SaveD3dRegistryConfig(in, "Settings");
    CHECK_EQ(rc, 0);  // regCloseKey returns 0
    CHECK(hg.rec.closed);
    // Exact key/value assertions.
    CHECK_EQ(hg.rec.dwords["d3_screen_res_x"], 1024);
    CHECK_EQ(hg.rec.dwords["d3_screen_res_y"], 768);
    CHECK_EQ(hg.rec.dwords["d3_screen_res_depth"], 32);
    CHECK_EQ(hg.rec.dwords["d3_max_polys"], 50000);
    CHECK_EQ(hg.rec.dwords["d3_registry_version"], 3);
    CHECK(hg.rec.strings["d3_mode"] == "FULLSCREEN");
    CHECK(hg.rec.strings["d3io_LODMode"] == "d3io_LOD_HIGH");
    CHECK_EQ((int)hg.rec.floats.size(), 7);

    // Read back.
    D3dRegistryConfig out;
    int modeIdx = -1;
    LodModeGlobal() = 0;
    int ok = LoadD3dRegistryConfig(out, modeIdx, "Settings");
    CHECK_EQ(ok, 1);
    CHECK_EQ(out.resX, 1024);
    CHECK_EQ(out.resY, 768);
    CHECK_EQ(out.maxTextures, 256);
    CHECK_EQ(modeIdx, 3);
    CHECK_EQ((int)LodModeGlobal(), 4);  // LOD_HIGH parsed back

    // Version mismatch -> load fails.
    hg.rec.version = 2;
    D3dRegistryConfig bad;
    int mi = -1;
    CHECK_EQ(LoadD3dRegistryConfig(bad, mi, "Settings"), 0);
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, D3dRegistryConfigModeNull) {
    HookGuard hg;
    LodModeGlobal() = 0;  // -> "<NULL>"
    D3dRegistryConfig in;
    in.mode = 9;  // out of range -> "<NULL>"
    SaveD3dRegistryConfig(in, "Settings");
    CHECK(hg.rec.strings["d3_mode"] == "<NULL>");
    CHECK(hg.rec.strings["d3io_LODMode"] == "<NULL>");
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, D3DSettingsRoundTrip) {
    HookGuard hg;
    D3DSettings in;
    in.antialias = 1; in.bilinear = 0; in.dither = 1; in.perspective = 1;
    in.subpixel = 0; in.multitexture = 1; in.mirrors = 1; in.shadows = 5;
    in.fillmode = 3 /*SOLID*/; in.mipfilter = 3 /*LINEAR*/;

    int rc = SaveD3DSettingsToRegistry(in, "Settings");
    CHECK_EQ(rc, 0);
    CHECK_EQ(hg.rec.dwords["d3d_antialias"], 1);
    CHECK_EQ(hg.rec.dwords["d3d_dither"], 1);
    CHECK_EQ(hg.rec.dwords["d3d_multitexture"], 1);
    CHECK_EQ(hg.rec.dwords["d3_mirrors"], 1);
    CHECK_EQ(hg.rec.dwords["d3d_shadows"], 5);
    CHECK_EQ(hg.rec.dwords["d3d_registry_version"], 3);
    // Verbatim gilde.exe bug: both fillmode + mipfilter strings land on the
    // SAME key "d3d_fillmode"; mipfilter wins (written last).
    CHECK(hg.rec.strings["d3d_fillmode"] == "D3DTFP_LINEAR");

    // For the load path to recover fillmode + mipfilter, seed BOTH keys (the
    // load reads d3d_fillmode and d3d_mipfilter separately).
    hg.rec.strings["d3d_fillmode"]  = "D3DFILL_SOLID";
    hg.rec.strings["d3d_mipfilter"] = "D3DTFP_LINEAR";

    D3DSettings out;
    int ok = LoadD3DSettingsFromRegistry(out, "Settings");
    CHECK_EQ(ok, 1);
    CHECK_EQ((int)out.antialias, 1);
    CHECK_EQ((int)out.dither, 1);
    CHECK_EQ((int)out.multitexture, 1);
    CHECK_EQ((int)out.mirrors, 1);
    CHECK_EQ((int)out.shadows, 5);
    CHECK_EQ(out.fillmode, 3);   // SOLID
    // gilde.exe 0x5e0d8e-0x5e0da5: mipfilter==3 ONLY when buf==LINEAR (the
    // jnz at 0x5e0d9c skips the =2 store and falls through to =3). The prior
    // golden (==2) inverted the POINT/LINEAR test polarity; corrected to 3.
    CHECK_EQ(out.mipfilter, 3);  // LINEAR -> 3
}

// ---------------------------------------------------------------------------
TEST(RenderRecon4, BeginSceneRenderStateSequence) {
    HookGuard hg;
    BeginSceneState st;
    // alphaTest=1, alphaRef=200, zEnable=1, noZBuffer=0, fogTable=0xABCD
    BeginScene(/*device*/7, /*fogTable*/0xABCD, /*noZBuffer*/0,
               /*alphaTest*/1, /*alphaRef*/200, /*zEnable*/1, st);

    // Expected render-state (state,value) sequence:
    //   24,191 | 25,5 | 34,200 | 28,1 | 7,0xABCD | 14,1
    std::vector<std::pair<int,int>> expect = {
        {24, 191}, {25, 5}, {34, 200}, {28, 1}, {7, 0xABCD}, {14, 1},
    };
    CHECK_EQ((int)hg.rec.rsCalls.size(), (int)expect.size());
    for (size_t i = 0; i < expect.size() && i < hg.rec.rsCalls.size(); ++i) {
        CHECK_EQ(hg.rec.rsCalls[i].first,  expect[i].first);
        CHECK_EQ(hg.rec.rsCalls[i].second, expect[i].second);
    }
    // Call ordering: BeginScene (0) then SetTexture (1) before any render-state.
    CHECK(hg.rec.seq.size() >= 2);
    CHECK_EQ(hg.rec.seq[0], 0);
    CHECK_EQ(hg.rec.seq[1], 1);
    CHECK_EQ((int)st.lastZEnable, 1);
    CHECK_EQ((int)st.lastAlpha, 1);

    // No alpha test: RS 34 omitted; RS 28 = 0.
    hg.rec.rsCalls.clear(); hg.rec.seq.clear();
    BeginScene(7, 0xABCD, /*noZBuffer*/1, /*alphaTest*/0, /*alphaRef*/200,
               /*zEnable*/1, st);
    bool has34 = false; int rs28 = -1, rs14 = -1;
    for (auto& c : hg.rec.rsCalls) {
        if (c.first == 34) has34 = true;
        if (c.first == 28) rs28 = c.second;
        if (c.first == 14) rs14 = c.second;
    }
    CHECK(!has34);
    CHECK_EQ(rs28, 0);
    CHECK_EQ(rs14, 0);  // zEnable && !noZBuffer == 1 && !1 == 0
}
