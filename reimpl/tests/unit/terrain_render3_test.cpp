#include "test.h"
#include "render/terrain_render3.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

bool fclose(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Test hooks: an in-process texture/alloc tracker.
int  g_texLoadCalls = 0;
int  g_texReleaseCalls = 0;
int  g_texUploadCalls = 0;
void* FakeAlloc(u32 size, const char*) { void* p = ::operator new(size); std::memset(p, 0, size); return p; }
void  FakeFree(void* p) { ::operator delete(p); }
void* FakeTexLoad(const u8* name) { ++g_texLoadCalls; return name ? (void*)0xBEEF : nullptr; }
void  FakeTexRelease(void*) { ++g_texReleaseCalls; }
int   FakeTexUpload(void*) { ++g_texUploadCalls; return 1; }

void InstallFakes() {
    g_texLoadCalls = g_texReleaseCalls = g_texUploadCalls = 0;
    TerrainRender3Hooks h{};
    h.allocDebug = &FakeAlloc; h.freeDebug = &FakeFree;
    h.textureLoad = &FakeTexLoad; h.textureRelease = &FakeTexRelease;
    h.textureUpload = &FakeTexUpload;
    InstallTerrainRender3Hooks(h);
}

} // namespace

TEST(TR3_Sky, InitDefaultColors) {
    SkyDefaultColors c{};
    InitDefaultColors(&c);
    CHECK_EQ(c.horizon, 0xE08080);
    CHECK_EQ(c.lower, 0xE0E0E0);
    CHECK_EQ(c.zenith, 0xFFFFFF);
    CHECK_EQ(c.upper, 0xE0E0E0);
    CHECK_EQ(c.horizon2, 0xE08080);
    CHECK_EQ(c.count, 64);
    InitDefaultColors(nullptr);  // null guard: no crash
}

TEST(TR3_Sky, ScrollSpeed) {
    SkyDome dome{}; SkyLayer layer{};
    layer.scroll = 123.0f;
    // zero magnitude -> 0
    CHECK(SetLayerScrollSpeed(&dome, &layer, 0.0f) == &dome);
    CHECK(layer.scroll == 0.0f);
    // negative zero -> still 0
    SetLayerScrollSpeed(&dome, &layer, -0.0f);
    CHECK(layer.scroll == 0.0f);
    // nonzero -> -speed * 1e-6
    SetLayerScrollSpeed(&dome, &layer, 2.0f);
    CHECK(fclose(layer.scroll, -1.9999999949504854e-06f));
    SetLayerScrollSpeed(&dome, &layer, -5.0f);
    CHECK(fclose(layer.scroll, 5.0f * 9.999999974752427e-07f));
    // null guards
    SetLayerScrollSpeed(nullptr, &layer, 1.0f);
    SetLayerScrollSpeed(&dome, nullptr, 1.0f);
}

TEST(TR3_Sky, SetLayerFade) {
    SkyDome dome{}; SkyLayer layer{};
    layer.fadeFlag = 9;
    // dur != 0: fadeCur=0, rate=1/dur, state/tgt from old flag/target
    u8 r = SetLayerFade(&dome, &layer, 3, 4.0f);
    CHECK(layer.fadeCur == 0.0f);
    CHECK(fclose(layer.fadeRate, 0.25f));
    CHECK_EQ((int)layer.fadeTgt, 3);
    CHECK_EQ((int)layer.fadeState, 9);   // old fadeFlag
    CHECK_EQ((int)r, 9);
    // dur == 0: fadeCur=1, rate=0, state=tgt=old flag
    layer.fadeFlag = 7;
    r = SetLayerFade(&dome, &layer, 2, 0.0f);
    CHECK(layer.fadeCur == 1.0f);
    CHECK(layer.fadeRate == 0.0f);
    CHECK_EQ((int)layer.fadeState, 7);
    CHECK_EQ((int)layer.fadeTgt, 7);
    CHECK_EQ((int)r, 7);
}

TEST(TR3_Sky, CreateLayerPrependAndTail) {
    InstallFakes();
    SkyDome dome{};
    const u8* nm = (const u8*)"clouds";
    // tail insert into empty -> becomes head
    SkyLayer* a = CreateLayer(&dome, false, 1, nm, 10, 20, 30, 40);
    CHECK(a != nullptr);
    if (a) {
        CHECK_EQ(a->posX, 10); CHECK_EQ(a->posY, 20);
        CHECK_EQ(a->sizeX, 30); CHECK_EQ(a->sizeY, 40);
        CHECK(a->fadeCur == 1.0f);
        CHECK_EQ((int)a->fadeState, 1);
        CHECK(a->texture == (void*)0xBEEF);
        CHECK(dome.headLayer == a);
    }
    // tail insert second -> a->next == b
    SkyLayer* b = CreateLayer(&dome, false, 0, nm, 1, 2, 3, 4);
    CHECK(b != nullptr);
    if (a && b) {
        CHECK(a->next == b);
        CHECK(b->next == nullptr);
    }
    // prepend third -> new head, ->next == old head
    SkyLayer* c = CreateLayer(&dome, true, 0, nm, 0, 0, 0, 0);
    CHECK(c != nullptr);
    if (c && a) {
        CHECK(dome.headLayer == c);
        CHECK(c->next == a);
    }
    CHECK_EQ(g_texLoadCalls, 3);
    CHECK_EQ(g_texUploadCalls, 3);
    // null guards
    CHECK(CreateLayer(nullptr, false, 0, nm, 0, 0, 0, 0) == nullptr);
    CHECK(CreateLayer(&dome, false, 0, nullptr, 0, 0, 0, 0) == nullptr);
    // cleanup: dome is stack-allocated here, so free only the heap layers.
    while (dome.headLayer) RemoveLayer(&dome, dome.headLayer);
    ResetTerrainRender3Hooks();
}

TEST(TR3_Sky, RemoveLayerMiddleAndHead) {
    InstallFakes();
    // Build a 3-node list via CreateLayer (tail).
    SkyDome* dome = SkyCreate(0, 0, 0);
    CHECK(dome != nullptr);
    if (dome) {
        const u8* nm = (const u8*)"x";
        SkyLayer* l0 = CreateLayer(dome, false, 0, nm, 0, 0, 0, 0);
        SkyLayer* l1 = CreateLayer(dome, false, 0, nm, 1, 0, 0, 0);
        SkyLayer* l2 = CreateLayer(dome, false, 0, nm, 2, 0, 0, 0);
        CHECK(l0 && l1 && l2);
        // remove middle
        RemoveLayer(dome, l1);
        if (l0) CHECK(l0->next == l2);
        CHECK_EQ(g_texReleaseCalls, 1);
        // remove head
        RemoveLayer(dome, l0);
        CHECK(dome->headLayer == l2);
        CHECK_EQ(g_texReleaseCalls, 2);
        SkyDestroy(dome);
        CHECK_EQ(g_texReleaseCalls, 3);  // last layer released on destroy
    }
    ResetTerrainRender3Hooks();
}

TEST(TR3_Sky, LayerLoadTexture) {
    InstallFakes();
    SkyLayer layer{};
    layer.texture = (void*)0x1234;
    u8 ok = LayerLoadTexture(&layer, (const u8*)"newtex");
    CHECK_EQ((int)ok, 1);                // upload returned 1
    CHECK_EQ(g_texReleaseCalls, 1);      // old released
    CHECK_EQ(g_texLoadCalls, 1);
    CHECK(layer.texture == (void*)0xBEEF);
    // null name -> load returns null -> upload skipped -> 0
    g_texUploadCalls = 0;
    u8 ok2 = LayerLoadTexture(&layer, nullptr);
    CHECK_EQ((int)ok2, 0);
    CHECK_EQ(g_texUploadCalls, 0);
    CHECK(LayerLoadTexture(nullptr, (const u8*)"z") == 0);
    ResetTerrainRender3Hooks();
}

TEST(TR3_Sky, CreateStampsTick) {
    InstallFakes();
    SetRenderGameTick(0x1357);
    SkyDome* dome = SkyCreate(42, 7, 9);
    CHECK(dome != nullptr);
    if (dome) {
        CHECK_EQ(dome->hourIndex, -1);
        CHECK_EQ(dome->owner, 42);
        CHECK_EQ(dome->param1, 7);
        CHECK_EQ(dome->param2, 9);
        CHECK_EQ(dome->createTick, 0x1357);
        CHECK(dome->headLayer == nullptr);
        SkyDestroy(dome);
    }
    SetRenderGameTick(0);
    ResetTerrainRender3Hooks();
}

TEST(TR3_SkyColor, ApplyVertexColors) {
    // Vertex array: 6 vertices, stride 56, colour at +24.
    u8 buf[6 * 56];
    std::memset(buf, 0, sizeof(buf));
    ApplyVertexColors(buf, /*night*/false, /*interior*/false);  // day/exterior table
    // First vertex colour quad = {133,131,255,307}
    float* c0 = reinterpret_cast<float*>(buf + 0 * 56 + 24);
    CHECK(c0[0] == 133.0f); CHECK(c0[1] == 131.0f);
    CHECK(c0[2] == 255.0f); CHECK(c0[3] == 307.0f);
    // Last vertex (k=5) = {162,129,197,326}
    float* c5 = reinterpret_cast<float*>(buf + 5 * 56 + 24);
    CHECK(c5[0] == 162.0f); CHECK(c5[1] == 129.0f);
    CHECK(c5[2] == 197.0f); CHECK(c5[3] == 326.0f);
    // night/interior table first vertex = {133,112,255,321}
    std::memset(buf, 0, sizeof(buf));
    ApplyVertexColors(buf, true, true);
    float* n0 = reinterpret_cast<float*>(buf + 0 * 56 + 24);
    CHECK(n0[0] == 133.0f); CHECK(n0[1] == 112.0f);
    CHECK(n0[2] == 255.0f); CHECK(n0[3] == 321.0f);
    ApplyVertexColors(nullptr, false, false);  // null guard
}

TEST(TR3_SkyColor, RefreshDomeColors) {
    float a[21], b[21], cc[21], last[3];
    RefreshDomeColors(/*night*/false, a, b, cc, last);  // day table
    // Row 0 = {26,15,71}
    CHECK(a[0] == 26.0f); CHECK(a[1] == 15.0f); CHECK(a[2] == 71.0f);
    CHECK(b[0] == 26.0f); CHECK(cc[2] == 71.0f);
    // Row 2 = {68,51,43}
    CHECK(a[6] == 68.0f); CHECK(a[7] == 51.0f); CHECK(a[8] == 43.0f);
    // last row (k=6) = {26,15,71}
    CHECK(last[0] == 26.0f); CHECK(last[1] == 15.0f); CHECK(last[2] == 71.0f);
    // night table row 0 = {17,15,71}
    RefreshDomeColors(true, a, b, cc, last);
    CHECK(a[0] == 17.0f); CHECK(a[1] == 15.0f); CHECK(a[2] == 71.0f);
    CHECK(last[0] == 17.0f);
    // tolerate null output arrays
    RefreshDomeColors(false, nullptr, nullptr, nullptr, nullptr);
}

TEST(TR3_Grid, DimWeight) {
    CHECK(GridDimWeight(0, 0, 1, 2) == 1.0f);              // no mask hit
    CHECK(GridDimWeight(1, 0, 1, 2) == 0.699999988079071f); // mask1 hits
    CHECK(GridDimWeight(0, 2, 1, 2) == 0.699999988079071f); // mask2 hits
    // both masks hit -> 0.7 * 0.7
    CHECK(fclose(GridDimWeight(3, 3, 1, 2), 0.4899999797344208f));
}

TEST(TR3_Grid, EmitQuad) {
    float centre[3] = {10.0f, 20.0f, 30.0f};
    float A[3] = {1.0f, 0.0f, 0.0f};
    float B[3] = {0.0f, 0.0f, 2.0f};
    float out[16];
    float w = EmitGridQuad(centre, A, B, 0.7f, out);
    CHECK(w == 0.7f);
    // v0 = centre - A
    CHECK(out[0] == 9.0f); CHECK(out[1] == 20.0f); CHECK(out[2] == 30.0f); CHECK(out[3] == 0.7f);
    // v1 = centre + A
    CHECK(out[4] == 11.0f); CHECK(out[5] == 20.0f); CHECK(out[6] == 30.0f); CHECK(out[7] == 0.0f);
    // v2 = centre - B
    CHECK(out[8] == 10.0f); CHECK(out[10] == 28.0f); CHECK(out[11] == 0.7f);
    // v3 = centre + B
    CHECK(out[12] == 10.0f); CHECK(out[14] == 32.0f); CHECK(out[15] == 0.0f);
    EmitGridQuad(nullptr, A, B, 1.0f, out);  // null guard
}

TEST(TR3_Weather, ThunderTriggerInert) {
    // default hook randomMod -> 0, so RandomModulo(300)==0 => always fires.
    ResetTerrainRender3Hooks();
    CHECK(ThunderShouldTrigger(0) == true);
    CHECK(ThunderShouldTrigger(3) == true);
}

// Custom rng to exercise both branches deterministically.
namespace {
int g_rngSeq[4]; int g_rngIdx = 0;
int SeqRng(u16) { int v = g_rngSeq[g_rngIdx % 4]; ++g_rngIdx; return v; }
}

TEST(TR3_Weather, ThunderTriggerBranches) {
    TerrainRender3Hooks h{};
    h.randomMod = &SeqRng;
    InstallTerrainRender3Hooks(h);
    // r300 != 0, type != 3 -> false (no second call)
    g_rngSeq[0] = 5; g_rngSeq[1] = 0; g_rngIdx = 0;
    CHECK(ThunderShouldTrigger(0) == false);
    CHECK_EQ(g_rngIdx, 1);  // only one rng call
    // r300 != 0, type == 3, r100 == 0 -> true
    g_rngSeq[0] = 5; g_rngSeq[1] = 0; g_rngIdx = 0;
    CHECK(ThunderShouldTrigger(3) == true);
    CHECK_EQ(g_rngIdx, 2);
    // r300 != 0, type == 3, r100 != 0 -> false
    g_rngSeq[0] = 5; g_rngSeq[1] = 7; g_rngIdx = 0;
    CHECK(ThunderShouldTrigger(3) == false);
    ResetTerrainRender3Hooks();
}
