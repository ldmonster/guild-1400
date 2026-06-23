// Golden-vector unit tests for src/render/render_recon2.{h,cpp}.
// Verifies the 1:1 control flow / state transitions translated from gilde.exe:
//   0x5af200 ResetEngineState, 0x5af8e4 SetEngineEnabled,
//   0x5b04a8 ApplyFogAndLightFlags, 0x5b499c PresentSceneAndClearFlags,
//   0x5d9014 WithSurfaceContext2.
//
// Cross-module callees are stubbed through RenderRecon2Hooks and recorded so
// each branch's observable side effects can be asserted.

#include "tests/framework/test.h"
#include "src/render/render_recon2.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// ---- shared trace of hook invocations ----
struct Trace {
    std::vector<std::string> log;
    int radixMode = -1;
    int radixHead = -1;
    bool drewTextured = false;
    bool rasterized = false;
    int  uploadCalls = 0;
    int  cacheResetCalls = 0;
    int  skyBuildCalls = 0;
    int  refreshCalls = 0;
    u8   gammaSet = 0;
    u8   mipReq = 0;
    int  traverseKind = 0;
    int  traverseCalls = 0;
    int  presentBeginFlag = -1;
    int  presentEndRet = 0xABCD;
    int  floorRenders = 0;
    // palette remap capture
    u16* remapPixels = nullptr;
    int  remapTable = 0;
    int  remapCount = 0;
};
Trace* g_t = nullptr;

void h_radix(int head, int mode) { g_t->radixHead = head; g_t->radixMode = mode; }
void h_drawTex() { g_t->drewTextured = true; }
void h_raster() { g_t->rasterized = true; }
void h_traverse(int, int, void (*)(), int kind) {
    g_t->traverseKind = kind; ++g_t->traverseCalls;
}
int  h_cacheReset() { ++g_t->cacheResetCalls; return 0; }
void h_sky(int, int) { ++g_t->skyBuildCalls; }
void h_invalidate(int) {}
int  h_upload(int, int (*)(void)) { ++g_t->uploadCalls; return 77; }
u8   h_refresh(unsigned int) { ++g_t->refreshCalls; return 0x42; }
void h_gamma(u8 g) { g_t->gammaSet = g; }
u8   h_mip(u8 lvl) { g_t->mipReq = lvl; return static_cast<u8>(lvl + 1); }
void h_floor(int) { ++g_t->floorRenders; }
void h_remap(u16* px, int tbl, int n) {
    g_t->remapPixels = px; g_t->remapTable = tbl; g_t->remapCount = n;
}
void h_presentBegin(int, int flag) { g_t->presentBeginFlag = flag; }
int  h_presentEnd(int) { return g_t->presentEndRet; }

RenderRecon2Hooks MakeHooks() {
    RenderRecon2Hooks h{};
    h.radixSortDrawList = h_radix;
    h.drawTexturedTriangles = h_drawTex;
    h.rasterizeMeshList = h_raster;
    h.sceneGraphTraverseTree = h_traverse;
    h.shadowClearAllCasters = nullptr;
    h.shadowResetCasterTransforms = nullptr;
    h.meshMarkAllFramesDirty = nullptr;
    h.textureCacheReset = h_cacheReset;
    h.skyBuildDomeMesh = h_sky;
    h.objectInvalidateCurrent = h_invalidate;
    h.textureUploadAllRecords = h_upload;
    h.lightRefreshAllObjects = h_refresh;
    h.setGammaTable = h_gamma;
    h.setMipFilterLevel = h_mip;
    h.floorRenderMinimap = h_floor;
    h.remapSurfacePalette = h_remap;
    h.devicePresentBegin = h_presentBegin;
    h.devicePresentEnd = h_presentEnd;
    return h;
}

void Setup(Trace& t) {
    g_t = &t;
    ResetRenderRecon2State();
    InstallRenderRecon2Hooks(MakeHooks());
}

} // namespace

// ===========================================================================
// 0x5af200 — ResetEngineState
// ===========================================================================
TEST(Render2ReconResetEngine, EnabledTakesTexturedPath) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.engineEnabled = 1;
    s.drawListHead = 0x1234;
    s.drawListCount = 9;
    s.viewParamA = 5;
    s.field13FC570 = 0;

    int ret = ResetEngineState();

    CHECK_EQ(ret, 9);                 // returns dword_13FC584
    CHECK_EQ(t.radixMode, 1);         // engine on -> mode 1
    CHECK_EQ(t.radixHead, 0x1234);
    CHECK(t.drewTextured);
    CHECK(!t.rasterized);
    CHECK_EQ(s.viewParamA, 0);        // dword_649DA0 cleared
    CHECK_EQ(s.field13FC54C, 0);
    CHECK_EQ(s.field13FC574, 0);
    CHECK_EQ(s.field13FC4E0, 0);
    CHECK_EQ(s.field13FC570, 9);      // <- 13FC584
    CHECK_EQ(s.drawListHead, 0);      // dword_13FC770 = 0
}

TEST(Render2ReconResetEngine, DisabledTakesSoftwarePath) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.engineEnabled = 0;
    s.drawListHead = 0x77;
    s.drawListCount = 3;

    int ret = ResetEngineState();

    CHECK_EQ(ret, 3);
    CHECK_EQ(t.radixMode, 0);         // engine off -> mode 0
    CHECK_EQ(t.radixHead, 0x77);
    CHECK(!t.drewTextured);
    CHECK(t.rasterized);
}

// ===========================================================================
// 0x5af8e4 — SetEngineEnabled
// ===========================================================================
TEST(Render2ReconSetEnabled, EnableWhenAllowedAndChanged) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.enableAllowed = 1;
    s.sceneInit = 1;
    s.sky = 0x900;
    s.engineEnabled = 0;            // prev=0

    u8 ret = SetEngineEnabled(1);

    CHECK_EQ((int)s.engineEnabled, 1);
    CHECK_EQ(t.cacheResetCalls, 1);
    CHECK_EQ(t.skyBuildCalls, 1);   // sky != 0
    // state changed (0->1) AND sceneInit: re-upload + refresh; returns refresh
    CHECK_EQ(t.uploadCalls, 1);
    CHECK_EQ(t.refreshCalls, 1);
    CHECK_EQ((int)ret, 0x42);       // Light_RefreshAllObjects result
}

TEST(Render2ReconSetEnabled, NoSkyNoRebuild) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.enableAllowed = 1; s.sceneInit = 1; s.sky = 0; s.engineEnabled = 0;
    SetEngineEnabled(1);
    CHECK_EQ(t.skyBuildCalls, 0);   // dword_64A7C8 == 0 -> no dome build
}

TEST(Render2ReconSetEnabled, NotAllowedDisables) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.enableAllowed = 0; s.sceneInit = 1; s.engineEnabled = 1;  // prev=1

    u8 ret = SetEngineEnabled(1);   // not allowed -> goes to else (0)

    CHECK_EQ((int)s.engineEnabled, 0);
    // changed (1->0) AND sceneInit -> refresh path
    CHECK_EQ(t.refreshCalls, 1);
    CHECK_EQ((int)ret, 0x42);
    CHECK_EQ(t.cacheResetCalls, 0); // enable body not taken
}

TEST(Render2ReconSetEnabled, NoChangeReturnsState) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.enableAllowed = 1; s.sceneInit = 1; s.engineEnabled = 1; // prev=1, stays 1
    u8 ret = SetEngineEnabled(1);
    CHECK_EQ((int)s.engineEnabled, 1);
    CHECK_EQ(t.refreshCalls, 0);    // no change -> no refresh
    CHECK_EQ((int)ret, 1);          // returns engineEnabled
}

TEST(Render2ReconSetEnabled, ChangeButNoSceneInitSkipsRefresh) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.enableAllowed = 1; s.sceneInit = 0; s.engineEnabled = 0;
    u8 ret = SetEngineEnabled(1);   // 0->1 change, but sceneInit==0
    CHECK_EQ((int)s.engineEnabled, 1);
    CHECK_EQ(t.refreshCalls, 0);
    CHECK_EQ(t.uploadCalls, 0);
    CHECK_EQ((int)ret, 1);          // returns result (engineEnabled)
}

// ===========================================================================
// 0x5b04a8 — ApplyFogAndLightFlags
// ===========================================================================
TEST(Render2ReconFogFlags, FogModeChangeRebuildsCasters) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.fogFlags = 0xF0;       // low3 = 0
    s.shadowQuality = 2;

    u8 ret = ApplyFogAndLightFlags(/*mip*/4, /*fog*/3, /*shadow*/2,
                                   /*gamma*/7, nullptr);

    // fog low3 (0)!=3 -> rebuild + write bits; shadow unchanged
    CHECK_EQ((int)(s.fogFlags & 7), 3);
    CHECK_EQ((int)(s.fogFlags & 0xF8), 0xF0);  // high bits preserved
    CHECK_EQ(t.traverseCalls, 1);
    CHECK_EQ(t.traverseKind, 192);
    CHECK_EQ((int)t.gammaSet, 7);
    CHECK_EQ((int)t.mipReq, 4);
    CHECK_EQ(t.cacheResetCalls, 0);  // shadow unchanged
    CHECK_EQ((int)ret, 5);           // SetMipFilterLevel returned 4+1
}

TEST(Render2ReconFogFlags, ShadowChangeUploadsAndReturnsUpload) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.fogFlags = 0x05;       // low3 = 5
    s.shadowQuality = 1;

    int (*cb)(void) = nullptr;
    u8 ret = ApplyFogAndLightFlags(/*mip*/2, /*fog*/5, /*shadow*/3,
                                   /*gamma*/0, cb);

    // fog low3 (5)==5 but shadow 1!=3 -> condition true (rebuild), bits rewritten
    CHECK_EQ((int)(s.fogFlags & 7), 5);
    CHECK_EQ(t.traverseCalls, 1);
    // shadow changed -> cache reset, store, return upload (77)
    CHECK_EQ(t.cacheResetCalls, 1);
    CHECK_EQ((int)s.shadowQuality, 3);
    CHECK_EQ(t.uploadCalls, 1);
    CHECK_EQ((int)ret, 77);
}

TEST(Render2ReconFogFlags, NoChangeNoRebuildReturnsMip) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.fogFlags = 0x02;       // low3 = 2
    s.shadowQuality = 4;

    u8 ret = ApplyFogAndLightFlags(/*mip*/9, /*fog*/2, /*shadow*/4,
                                   /*gamma*/3, nullptr);

    CHECK_EQ(t.traverseCalls, 0);    // nothing changed
    CHECK_EQ(t.cacheResetCalls, 0);
    CHECK_EQ(t.uploadCalls, 0);
    CHECK_EQ((int)t.gammaSet, 3);
    CHECK_EQ((int)ret, 10);          // mip 9+1
}

// ===========================================================================
// 0x5b499c — PresentSceneAndClearFlags
// ===========================================================================
TEST(Render2ReconPresent, ClearsDirtyBitsAndPresents) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    // 3 records, stride 128, flag at +104; set 0x80|0x01 to verify only 0x80 clears
    const int N = 3;
    std::vector<u8> table(128 * N, 0);
    for (int i = 0; i < N; ++i) table[128 * i + 104] = 0x81;
    s.objectCount = N;
    s.objectTable = table.data();
    s.floor = 0x55;
    s.deviceCtx = 0xDEAD;
    t.presentEndRet = 0x1234;

    int ret = PresentSceneAndClearFlags(/*presentFlag*/1, /*minimap*/1);

    for (int i = 0; i < N; ++i)
        CHECK_EQ((int)table[128 * i + 104], 0x01);  // 0x80 cleared, 0x01 kept
    CHECK_EQ(t.presentBeginFlag, 1);
    CHECK_EQ(t.traverseKind, 64);
    CHECK_EQ(t.floorRenders, 1);     // floor!=0 && minimap
    CHECK_EQ(ret, 0x1234);           // returns devicePresentEnd
}

TEST(Render2ReconPresent, NoMinimapWhenFlagZero) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.objectCount = 0;
    s.floor = 0x55;
    PresentSceneAndClearFlags(0, /*minimap*/0);
    CHECK_EQ(t.floorRenders, 0);
    CHECK_EQ(t.presentBeginFlag, 0);
}

TEST(Render2ReconPresent, NoMinimapWhenFloorZero) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.floor = 0;
    PresentSceneAndClearFlags(1, /*minimap*/1);
    CHECK_EQ(t.floorRenders, 0);     // floor==0
}

// ===========================================================================
// 0x5d9014 — WithSurfaceContext2
// ===========================================================================
TEST(Render2ReconSurfaceCtx, NoClipMidScreen) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.clipMinY = 0;
    s.clipMaxY = 100;
    s.surfaceStride = 999;            // prev stride to be restored

    std::vector<u16> px(64 * 64, 0);
    Recon2SurfaceRec rec{};
    rec.stride = 64;
    rec.pixels = px.data();

    WithSurfaceContext2(/*x*/5, /*y*/10, /*width*/8, /*table*/0x33, &rec);

    // y=10 within [0,100): no clip. pixel = base + stride*y + x
    CHECK_EQ(t.remapPixels, px.data() + (64 * 10 + 5));
    CHECK_EQ(t.remapTable, 0x33);
    CHECK_EQ(t.remapCount, 8);
    CHECK_EQ(s.surfaceStride, 999);   // restored
}

TEST(Render2ReconSurfaceCtx, ClipTopReducesWidthAndZeroesY) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.clipMinY = 20;                  // y(5) < 20
    s.clipMaxY = 1000;
    s.surfaceStride = 7;

    std::vector<u16> px(32 * 32, 0);
    Recon2SurfaceRec rec{};
    rec.stride = 32;
    rec.pixels = px.data();

    WithSurfaceContext2(/*x*/3, /*y*/5, /*width*/40, /*table*/1, &rec);

    // a3 -= (20-5)=15 -> 25 ; a2 -> 0 ; then a2+a3=25 <= 1000 (no further clip)
    // pixel = base + stride*0 + 3
    CHECK_EQ(t.remapPixels, px.data() + 3);
    CHECK_EQ(t.remapCount, 25);
    CHECK_EQ(s.surfaceStride, 7);     // restored
}

TEST(Render2ReconSurfaceCtx, ClipBottomClampsWidth) {
    Trace t; Setup(t);
    auto& s = Recon2State();
    s.clipMinY = 0;
    s.clipMaxY = 50;                  // y=40, width=30 -> 40+30=70 > 50
    s.surfaceStride = 1;

    std::vector<u16> px(64 * 64, 0);
    Recon2SurfaceRec rec{};
    rec.stride = 64;
    rec.pixels = px.data();

    WithSurfaceContext2(/*x*/0, /*y*/40, /*width*/30, /*table*/9, &rec);

    // a3 = clipMaxY - a2 - 1 = 50 - 40 - 1 = 9
    CHECK_EQ(t.remapCount, 9);
    CHECK_EQ(t.remapPixels, px.data() + (64 * 40));
}
