#include "render/terrain_render3.h"

#include <cmath>
#include <cstring>
#include <new>

// =============================================================================
// guild::render — sky-dome layer / colour-table / terrain grid-line / weather
// thunder leaves. See terrain_render3.h for the reconstructed function map and
// the original addresses. The render-resource side effects route through the
// TerrainRender3Hooks struct (inert defaults below).
// =============================================================================

namespace guild::render {

// g_gameTick (dword_62EB38) is the 32-bit game-frame counter SkyCreate stamps
// into the dome's createTick. The owning definition is guild::sim::g_gameTick
// (sim/actionqueue.cpp); to keep this render module self-contained (no sim link
// dependency) the current tick is supplied through a render-local mirror that
// the app/scene layer keeps in sync. SetRenderGameTick lets tests/integration
// drive it deterministically (default 0, matching a fresh frame counter).
static u32 g_renderGameTick = 0;
void SetRenderGameTick(u32 tick) { g_renderGameTick = tick; }
u32  GetRenderGameTick() { return g_renderGameTick; }

// ---------------------------------------------------------------------------
// Recovered colour tables (get_bytes, decoded to float).
//
// ApplyVertexColors picks one of four 6-vertex tables (each 24 floats = 6 verts
// x 4 floats) by {night, interior}:
//   day  / exterior -> unk_631AF8
//   day  / interior -> unk_631B68
//   night/ exterior -> unk_631C48
//   night/ interior -> unk_631CB8
// ---------------------------------------------------------------------------
static const float kAvcDayExt[24] = {
    133, 131, 255, 307,  119, 143, 218, 350,  213, 180, 160, 375,
    255, 206, 123, 511,  255, 185,  86, 511,  162, 129, 197, 326};
static const float kAvcDayInt[24] = {
    133, 112, 255, 472,  111, 141, 181, 453,  187, 211, 154, 370,
    255, 194, 117, 511,  255, 175,  58, 414,  204, 192, 181, 419};
static const float kAvcNightExt[24] = {
    116, 131, 255, 273,  108, 143, 200, 302,  125, 177, 160, 321,
    142, 199, 221, 341,  167, 175, 172, 346,  142, 138, 200, 355};
static const float kAvcNightInt[24] = {
    133, 112, 255, 321,  119, 148, 144, 438,  122, 146, 144, 375,
    147, 182, 197, 355,  136, 129, 144, 336,  154, 192, 206, 311};

// RefreshDomeColors picks one of two 7-entry dome tables (each 28 floats = 7
// rows x 4 floats; the 4th column is the 0.0 alpha the original skips):
//   day   -> unk_631BD8
//   night -> unk_631D28
static const float kDomeDay[28] = {
    26, 15, 71, 0,  23, 34, 55, 0,  68, 51, 43, 0,  99, 78, 40, 0,
    71, 56, 37, 0,  34, 22, 52, 0,  26, 15, 71, 0};
static const float kDomeNight[28] = {
    17, 15, 71, 0,  14, 27, 58, 0,  14, 22, 58, 0,  20, 32, 55, 0,
    20, 24, 61, 0,  20, 12, 46, 0,  17, 15, 71, 0};

// ---------------------------------------------------------------------------
// Hooks — inert defaults.
// ---------------------------------------------------------------------------
static void* DefaultAlloc(u32 size, const char* /*tag*/) {
    void* p = ::operator new(size, std::nothrow);
    if (p) std::memset(p, 0, size);
    return p;
}
static void  DefaultFree(void* ptr) { ::operator delete(ptr); }
static void* DefaultTexLoad(const u8* /*name*/) { return nullptr; }
static void  DefaultTexRelease(void* /*h*/) {}
static int   DefaultTexUpload(void* /*h*/) { return 0; }
static void  DefaultDrawLines(const void*, u32, u8, u8, u8, int) {}
static int   DefaultRandomMod(u16 /*n*/) { return 0; }

static TerrainRender3Hooks g_hooks = {
    &DefaultAlloc, &DefaultFree, &DefaultTexLoad, &DefaultTexRelease,
    &DefaultTexUpload, &DefaultDrawLines, &DefaultRandomMod};

void InstallTerrainRender3Hooks(const TerrainRender3Hooks& hooks) {
    g_hooks.allocDebug     = hooks.allocDebug     ? hooks.allocDebug     : &DefaultAlloc;
    g_hooks.freeDebug      = hooks.freeDebug      ? hooks.freeDebug      : &DefaultFree;
    g_hooks.textureLoad    = hooks.textureLoad    ? hooks.textureLoad    : &DefaultTexLoad;
    g_hooks.textureRelease = hooks.textureRelease ? hooks.textureRelease : &DefaultTexRelease;
    g_hooks.textureUpload  = hooks.textureUpload  ? hooks.textureUpload  : &DefaultTexUpload;
    g_hooks.drawLines      = hooks.drawLines      ? hooks.drawLines      : &DefaultDrawLines;
    g_hooks.randomMod      = hooks.randomMod      ? hooks.randomMod      : &DefaultRandomMod;
}
void ResetTerrainRender3Hooks() {
    g_hooks = {&DefaultAlloc, &DefaultFree, &DefaultTexLoad, &DefaultTexRelease,
               &DefaultTexUpload, &DefaultDrawLines, &DefaultRandomMod};
}

// The global dome (dword_64A7C8) DestroyGlobal drains.
static SkyDome* g_globalDome = nullptr;

// ---------------------------------------------------------------------------
// gilde.exe 0x5efdc8 — VIBE_Sky_InitDefaultColors
// ---------------------------------------------------------------------------
void InitDefaultColors(SkyDefaultColors* out) {
    if (!out) return;
    out->horizon  = 0xE08080;  // dword_1408774
    out->lower    = 0xE0E0E0;  // dword_1408778
    out->zenith   = 0xFFFFFF;  // dword_140877C
    out->upper    = 0xE0E0E0;  // dword_1408780
    out->horizon2 = 0xE08080;  // dword_1408784
    out->count    = 64;        // dword_1408770[0]
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efc78 — VIBE_Sky_SetLayerScrollSpeed
// (LODWORD(a3) & 0x7FFFFFFF) != 0  <=>  speed is not +/-0.0.
// ---------------------------------------------------------------------------
// flt_62C164 = 1e-6 (decoded 9.999999974752427e-07f).
static const float kScrollScale = 9.999999974752427e-07f;

SkyDome* SetLayerScrollSpeed(SkyDome* dome, SkyLayer* layer, float speed) {
    if (dome && layer) {
        u32 bits;
        std::memcpy(&bits, &speed, 4);
        if ((bits & 0x7FFFFFFFu) != 0)
            layer->scroll = -speed * kScrollScale;
        else
            layer->scroll = 0.0f;
    }
    return dome;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efca8 — VIBE_Sky_SetLayerFade
// Original returns `a1` (clobbered to old fadeFlag byte on the dur!=0 path).
// ---------------------------------------------------------------------------
u8 SetLayerFade(SkyDome* dome, SkyLayer* layer, u8 target, float dur) {
    u8 ret = 1;  // a1 nonzero on entry (dome ptr); LOBYTE updates track it.
    if (dome && layer) {
        u32 bits;
        std::memcpy(&bits, &dur, 4);
        if ((bits & 0x7FFFFFFFu) != 0) {
            layer->fadeCur  = 0.0f;              // *(a2+12) = 0
            u8 oldFlag = layer->fadeFlag;        // LOBYTE(a1) = *(a2+38)
            layer->fadeTgt   = target;           // *(a2+37) = a3
            layer->fadeState = oldFlag;          // *(a2+36) = a1(=oldFlag)
            layer->fadeRate  = 1.0f / dur;       // *(a2+16) = 1.0/a4
            ret = oldFlag;
        } else {
            layer->fadeCur  = 1.0f;              // *(a2+12) = 1.0f bits
            u8 oldFlag = layer->fadeFlag;        // LOBYTE(a1) = *(a2+38)
            layer->fadeRate = 0.0f;              // *(a2+16) = 0
            layer->fadeState = oldFlag;          // *(a2+36)
            layer->fadeTgt   = oldFlag;          // *(a2+37)
            ret = oldFlag;
        }
    }
    return ret;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efb78 — VIBE_Sky_CreateLayer
// ---------------------------------------------------------------------------
SkyLayer* CreateLayer(SkyDome* dome, bool prepend, u8 fadeFlag,
                      const u8* textureName, i32 posX, i32 posY,
                      i32 sizeX, i32 sizeY) {
    if (!dome) return nullptr;
    if (!textureName) return nullptr;
    SkyLayer* layer = static_cast<SkyLayer*>(
        g_hooks.allocDebug(sizeof(SkyLayer), "d3_sky:CreateLayer"));
    if (!layer) return nullptr;

    layer->scroll   = 0.0f;            // *((_DWORD*)v11+2) = 0
    layer->fadeCur  = 1.0f;            // *((_DWORD*)v11+3) = 1065353216
    layer->posX     = posX;            // *(_DWORD*)v11 = a5
    layer->posY     = posY;            // +1 = a6
    layer->sizeX    = sizeX;           // +6 = a7
    layer->sizeY    = sizeY;           // +5 = a8
    layer->fadeState= fadeFlag;        // v11[36] = a3
    layer->fadeTgt  = fadeFlag;        // v11[37] = a3
    layer->fadeFlag = fadeFlag;        // (mirror; original +38 is read later)
    layer->fadeRate = 0.0f;            // *((_DWORD*)v11+4) = 0

    layer->texture = g_hooks.textureLoad(textureName);  // VIBE_Texture_LoadByName
    if (layer->texture)
        g_hooks.textureUpload(layer->texture);

    layer->next = nullptr;
    if (prepend) {
        layer->next = dome->headLayer;   // *(v9+40) = head
        dome->headLayer = layer;         // head = v9
    } else {
        layer->next = nullptr;           // *(v9+40) = 0
        SkyLayer* head = dome->headLayer;
        if (head) {
            SkyLayer* it = head;
            if (it->next) {
                while (it->next) it = it->next;
            }
            it->next = layer;
        } else {
            dome->headLayer = layer;
        }
    }
    return layer;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efb14 — VIBE_Sky_RemoveLayer
// ---------------------------------------------------------------------------
SkyDome* RemoveLayer(SkyDome* dome, SkyLayer* layer) {
    if (dome && layer) {
        SkyLayer* head = dome->headLayer;
        if (layer == head) {
            dome->headLayer = head->next;          // head = *(v4+40)
            g_hooks.textureRelease(layer->texture);
            g_hooks.freeDebug(layer);
        } else {
            // Find the node whose ->next == layer (the original walks via +40).
            SkyLayer* it = head;
            if (head) {
                while (it && it->next != layer)
                    it = it->next;
            }
            if (it) {
                SkyLayer* nxt = it->next;           // a4 = *(v5+40)
                if (layer == nxt)
                    it->next = nxt->next;           // *(v5+40) = *(a4+40)
            }
            g_hooks.textureRelease(layer->texture);
            g_hooks.freeDebug(layer);
        }
    }
    return dome;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efcf0 — VIBE_Sky_LayerLoadTexture
// ---------------------------------------------------------------------------
u8 LayerLoadTexture(SkyLayer* layer, const u8* textureName) {
    if (!layer) return 0;
    g_hooks.textureRelease(layer->texture);            // release *(a1+28)
    layer->texture = g_hooks.textureLoad(textureName); // VIBE_Texture_LoadByName
    if (layer->texture)
        return static_cast<u8>(g_hooks.textureUpload(layer->texture));
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efd28 — VIBE_Sky_Create  (mesh build is the inert default)
// ---------------------------------------------------------------------------
SkyDome* SkyCreate(i32 owner, i32 param1, i32 param2) {
    SkyDome* dome = static_cast<SkyDome*>(
        g_hooks.allocDebug(0xACC, "d3_sky:Create"));
    if (!dome) return nullptr;
    dome->hourIndex  = -1;                  // +681 = -1
    dome->flags      = 0;                   // +688 (model field) = 0
    dome->owner      = owner;               // +685 = a1
    dome->createTick = (i32)g_renderGameTick; // +689 = dword_62EB38
    dome->param1     = param1;              // +686 = a2
    dome->param2     = param2;              // +687 = a3
    dome->headLayer  = nullptr;             // +690 / list head = 0
    // VIBE_Sky_BuildDomeMesh(dome, 1): inert here (no scene graph in isolation).
    return dome;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efda0 — VIBE_Sky_Destroy
// ---------------------------------------------------------------------------
SkyDome* SkyDestroy(SkyDome* dome) {
    // 0x5efda0: drain layers via RemoveLayer, then FreeDebug. The original does
    // NOT touch the global-dome slot here (dword_64A7C8); only DestroyGlobal does.
    if (dome) {
        while (dome->headLayer)
            RemoveLayer(dome, dome->headLayer);
        SkyDome* saved = dome;
        g_hooks.freeDebug(dome);
        return saved;
    }
    return dome;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efe0c — VIBE_Sky_DestroyGlobal
// ---------------------------------------------------------------------------
SkyDome* SkyDestroyGlobal() {
    SkyDome* dome = g_globalDome;
    if (dome) {
        while (dome->headLayer)
            RemoveLayer(dome, dome->headLayer);
        g_hooks.freeDebug(dome);
        g_globalDome = nullptr;
    }
    return nullptr;
}

// Test/integration helpers for the global-dome slot (dword_64A7C8).
void SkySetGlobalDome(SkyDome* dome) { g_globalDome = dome; }
SkyDome* SkyGetGlobalDome() { return g_globalDome; }

// ---------------------------------------------------------------------------
// gilde.exe 0x4b1e10 — VIBE_Sky_ApplyVertexColors
// Copies a 6-vertex colour block (4 floats/vertex) into the vertex array. The
// original writes at *(a1+488)+i-32 .. +i-20 for i in {56,112,...,336}; that is
// a 4-float colour quad at byte offset (i-32) i.e. vertex k at +56*k+24.
// ---------------------------------------------------------------------------
void ApplyVertexColors(u8* dst, bool night, bool interior) {
    if (!dst) return;
    const float* tbl;
    if (night)
        tbl = interior ? kAvcNightInt : kAvcNightExt;
    else
        tbl = interior ? kAvcDayInt : kAvcDayExt;
    // 6 vertices, stride 56, colour quad at +24.
    for (int k = 0; k < 6; ++k) {
        float* c = reinterpret_cast<float*>(dst + 56 * k + 24);
        c[0] = tbl[4 * k + 0];
        c[1] = tbl[4 * k + 1];
        c[2] = tbl[4 * k + 2];
        c[3] = tbl[4 * k + 3];
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b236c — VIBE_Sky_RefreshDomeColors
// 7 rows, each row's first three channels fan into three parallel arrays
// (flt_13FD158/15C/160 in the original) and lastRGB = the final row's triple.
// ---------------------------------------------------------------------------
void RefreshDomeColors(bool night, float dstA[21], float dstB[21],
                       float dstC[21], float lastRGB[3]) {
    const float* tbl = night ? kDomeNight : kDomeDay;
    float r = 0, g = 0, b = 0;
    for (int k = 0; k < 7; ++k) {
        r = tbl[4 * k + 0];
        g = tbl[4 * k + 1];
        b = tbl[4 * k + 2];
        if (dstA) { dstA[3 * k + 0] = r; dstA[3 * k + 1] = g; dstA[3 * k + 2] = b; }
        if (dstB) { dstB[3 * k + 0] = r; dstB[3 * k + 1] = g; dstB[3 * k + 2] = b; }
        if (dstC) { dstC[3 * k + 0] = r; dstC[3 * k + 1] = g; dstC[3 * k + 2] = b; }
    }
    if (lastRGB) { lastRGB[0] = r; lastRGB[1] = g; lastRGB[2] = b; }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c3a48 — VIBE_Heightmap_DrawGridLines (geometry core)
// GridDimWeight: 1.0, halved-toward-0.7 on the two alternation masks; matches
// the v56/v57 = 1.0 ; *=0.7 if (mask&i)||(mask&j) ladder.
// ---------------------------------------------------------------------------
float GridDimWeight(int i, int j, int mask1, int mask2) {
    float w = 1.0f;
    if ((mask1 & i) != 0 || (mask1 & j) != 0)
        w = 1.0f * kGridDim;
    if ((mask2 & i) != 0 || (mask2 & j) != 0)
        w = w * kGridDim;
    return w;
}

float EmitGridQuad(const float centre[3], const float basisA[3],
                   const float basisB[3], float dimWeight, float out[16]) {
    if (!centre || !basisA || !basisB || !out) return dimWeight;
    // v0 = centre - A ; v1 = centre + A ; v2 = centre - B ; v3 = centre + B
    out[0]  = centre[0] - basisA[0];
    out[1]  = centre[1] - basisA[1];
    out[2]  = centre[2] - basisA[2];
    out[3]  = dimWeight;                 // dim weight in slot[3] of vertex 0
    out[4]  = centre[0] + basisA[0];
    out[5]  = centre[1] + basisA[1];
    out[6]  = centre[2] + basisA[2];
    out[7]  = 0.0f;
    out[8]  = centre[0] - basisB[0];
    out[9]  = centre[1] - basisB[1];
    out[10] = centre[2] - basisB[2];
    out[11] = dimWeight;                 // dim weight in slot[3] of vertex 2
    out[12] = centre[0] + basisB[0];
    out[13] = centre[1] + basisB[1];
    out[14] = centre[2] + basisB[2];
    out[15] = 0.0f;
    return dimWeight;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4c05ac — VIBE_Weather_RenderAndThunder (thunder-trigger phase)
//   !RandomModulo(300) || (type==3 && !RandomModulo(100))
// The original's RandomModulo is short-circuiting: when RandomModulo(300)==0 it
// fires WITHOUT calling RandomModulo(100); only when (300)!=0 AND type==3 does
// it call RandomModulo(100). We preserve that ordering.
// ---------------------------------------------------------------------------
bool ThunderShouldTrigger(int weatherType) {
    u16 r300 = static_cast<u16>(g_hooks.randomMod(300));
    if (r300 == 0)
        return true;
    if (weatherType == 3) {
        u16 r100 = static_cast<u16>(g_hooks.randomMod(100));
        if (r100 == 0)
            return true;
    }
    return false;
}

} // namespace guild::render
