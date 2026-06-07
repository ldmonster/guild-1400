#include "test.h"
#include "render/terrain_render3.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// E2E: build a full sky dome with several layers, configure their scroll/fade,
// refresh the dome + vertex colour tables, swap a texture, then tear the whole
// thing down via SkyDestroyGlobal — exercising the create/insert/configure/
// colour/free flow across the module the way the scene-init path drives it.

namespace {
int  g_loads = 0, g_releases = 0, g_uploads = 0, g_frees = 0;
void* E2EAlloc(u32 n, const char*) { void* p = ::operator new(n); std::memset(p, 0, n); return p; }
void  E2EFree(void* p) { ++g_frees; ::operator delete(p); }
void* E2ETexLoad(const u8* nm) { ++g_loads; return nm ? (void*)0x100 : nullptr; }
void  E2ETexRelease(void*) { ++g_releases; }
int   E2ETexUpload(void*) { ++g_uploads; return 1; }
} // namespace

TEST(TR3_E2E, FullDomeLifecycle) {
    g_loads = g_releases = g_uploads = g_frees = 0;
    TerrainRender3Hooks h{};
    h.allocDebug = &E2EAlloc; h.freeDebug = &E2EFree;
    h.textureLoad = &E2ETexLoad; h.textureRelease = &E2ETexRelease;
    h.textureUpload = &E2ETexUpload;
    InstallTerrainRender3Hooks(h);

    SetRenderGameTick(0x2024);

    // 1) seed default colours
    SkyDefaultColors defc{};
    InitDefaultColors(&defc);
    CHECK_EQ(defc.count, 64);

    // 2) create the dome and register it as the global one
    SkyDome* dome = SkyCreate(/*owner*/1, /*p1*/2, /*p2*/3);
    CHECK(dome != nullptr);
    if (!dome) { ResetTerrainRender3Hooks(); return; }
    CHECK_EQ(dome->createTick, 0x2024);
    SkySetGlobalDome(dome);
    CHECK(SkyGetGlobalDome() == dome);

    // 3) build 3 layers (2 tail, 1 prepend)
    const u8* nm = (const u8*)"sky_layer";
    SkyLayer* base  = CreateLayer(dome, false, 1, nm, 0, 0, 256, 256);
    SkyLayer* mid   = CreateLayer(dome, false, 0, nm, 8, 8, 256, 256);
    SkyLayer* front = CreateLayer(dome, true, 0, nm, -4, -4, 512, 512);
    CHECK(base && mid && front);
    CHECK(dome->headLayer == front);          // prepend put it first
    if (front && base) CHECK(front->next == base);
    if (base) CHECK(base->next == mid);
    CHECK_EQ(g_loads, 3);
    CHECK_EQ(g_uploads, 3);

    // 4) configure scroll + fade on the base layer
    if (base) {
        SetLayerScrollSpeed(dome, base, 3.0f);
        CHECK(std::fabs(base->scroll - (-3.0f * 9.999999974752427e-07f)) < 1e-12f);
        base->fadeFlag = 5;
        u8 r = SetLayerFade(dome, base, 2, 8.0f);
        CHECK_EQ((int)r, 5);
        CHECK(std::fabs(base->fadeRate - 0.125f) < 1e-6f);
        CHECK_EQ((int)base->fadeTgt, 2);
    }

    // 5) refresh dome + vertex colours (night) and verify a known entry
    float a[21], b[21], c[21], last[3];
    RefreshDomeColors(true, a, b, c, last);
    CHECK(a[0] == 17.0f);    // night row 0 R
    u8 vbuf[6 * 56];
    std::memset(vbuf, 0, sizeof(vbuf));
    ApplyVertexColors(vbuf, true, false);   // night/exterior
    float* v0 = reinterpret_cast<float*>(vbuf + 24);
    CHECK(v0[0] == 116.0f); // night/ext row 0 R

    // 6) swap the mid layer's texture
    if (mid) {
        int relBefore = g_releases;
        u8 ok = LayerLoadTexture(mid, (const u8*)"replacement");
        CHECK_EQ((int)ok, 1);
        CHECK_EQ(g_releases, relBefore + 1);
        CHECK(mid->texture == (void*)0x100);
    }

    // 7) destroy the global dome -> all 3 layers released + 4 frees (3 layers+dome)
    SkyDome* res = SkyDestroyGlobal();
    CHECK(res == nullptr);
    CHECK(SkyGetGlobalDome() == nullptr);
    CHECK_EQ(g_releases, 4);   // 3 create + 1 swap... actually 3 layer releases at destroy + 1 swap = 4
    CHECK_EQ(g_frees, 4);      // 3 layers + dome

    SetRenderGameTick(0);
    ResetTerrainRender3Hooks();
}
