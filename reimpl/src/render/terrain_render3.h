#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — sky-dome layer cluster + dome/vertex colour tables + terrain
// grid-line geometry + weather thunder-trigger phase (gilde.exe d3_sky.c /
// d3_floor.c / d3_weather.c leaves).
//
// Faithful 1:1 reconstruction of the untranslated VIBE_Sky_* layer/colour
// leaves plus the deterministic terrain grid-line builder and the weather
// thunder/lightning RNG phase. The render-resource side-effects (heap alloc,
// texture load/release/upload, D3D line draw) are routed through an installable
// hooks struct with inert defaults defined in this library .cpp, so the
// pointer-arithmetic / colour-table / RNG logic is testable in isolation.
//
//   0x5efdc8  VIBE_Sky_InitDefaultColors    (seed the 5 dome colour ptrs + count)
//   0x5efc78  VIBE_Sky_SetLayerScrollSpeed  (scroll = -|speed| * 1e-6)
//   0x5efca8  VIBE_Sky_SetLayerFade         (fade reciprocal + state bytes)
//   0x5efb78  VIBE_Sky_CreateLayer          (alloc 0x2C layer + list insert)
//   0x5efb14  VIBE_Sky_RemoveLayer          (unlink layer + release texture/free)
//   0x5efcf0  VIBE_Sky_LayerLoadTexture     (release+reload+upload a layer tex)
//   0x5efd28  VIBE_Sky_Create               (alloc dome + build mesh)
//   0x5efda0  VIBE_Sky_Destroy              (drain layers + free dome)
//   0x5efe0c  VIBE_Sky_DestroyGlobal        (drain + free the global dome)
//   0x4b1e10  VIBE_Sky_ApplyVertexColors    (copy 6-vertex colour block)
//   0x4b236c  VIBE_Sky_RefreshDomeColors    (copy 7-entry dome colour rows)
//   0x5c3a48  VIBE_Heightmap_DrawGridLines  (build grid-line vertex buffer)
//   0x4c05ac  VIBE_Weather_ThunderTrigger   (deterministic RNG lightning phase)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Sky layer record — the 0x2C (44) byte block VIBE_Sky_CreateLayer allocates,
// indexed as _DWORD* in the original (v11[k] == +4*k). Modelled at original
// byte offsets; `next` is the +0x28 (40) list link.
//   +0x00 ( 0)  posX      (i32)   a5  (layer origin x)
//   +0x04 ( 4)  posY      (i32)   a6
//   +0x08 ( 8)  scroll    (f32)   set by SetLayerScrollSpeed
//   +0x0C (12)  fadeCur   (f32)   1.0 default; fade alpha accumulator
//   +0x10 (16)  fadeRate  (f32)   1/duration (SetLayerFade)
//   +0x14 (20)  sizeY     (i32)   a8
//   +0x18 (24)  sizeX     (i32)   a7
//   +0x1C (28)  texture   (void*) loaded texture handle (+0x1C = v13[7])
//   +0x24 (36)  fadeState (u8)    a3 / mirrored
//   +0x25 (37)  fadeTgt   (u8)    a3
//   +0x26 (38)  fadeFlag  (u8)    read by SetLayerFade
//   +0x28 (40)  next      (SkyLayer*)
// ---------------------------------------------------------------------------
struct SkyLayer {
    i32       posX;      // +0x00
    i32       posY;      // +0x04
    float     scroll;    // +0x08
    float     fadeCur;   // +0x0C
    float     fadeRate;  // +0x10
    i32       sizeY;     // +0x14
    i32       sizeX;     // +0x18
    void*     texture;   // +0x1C
    u8        fadeState; // +0x24
    u8        fadeTgt;   // +0x25
    u8        fadeFlag;  // +0x26
    SkyLayer* next;      // +0x28
};

// Sky dome object (original 0xACC bytes). Only the fields the layer cluster
// touches are modelled. The layer list head lives at +0x2752 (2752) in the
// original (v[688]); we expose it as `headLayer`.
//   +2720 (681*4) hourIndex (i32 = -1 default)
//   +2740 (685*4) owner     (i32 = a1)
//   +2744 (686*4) param1    (i32 = a2)
//   +2748 (687*4) param2    (i32 = a3)
//   +2752 (688*4) headLayer (SkyLayer*) (= 0)
//   +2756 (689*4) createTick(i32 = g_gameTick)
//   +2760 (690*4) flags     (i32 = 0)
struct SkyDome {
    i32       hourIndex;  // +2720
    i32       owner;      // +2740
    i32       param1;     // +2744
    i32       param2;     // +2748
    SkyLayer* headLayer;  // +2752
    i32       createTick; // +2756
    i32       flags;      // +2760
};

// One RGBA-ish colour quad as the runtime tables store it (4 floats).
struct SkyColor4 { float c[4]; };

// ---------------------------------------------------------------------------
// Installable hooks for the render-resource side-effects (alloc / texture /
// D3D). Inert defaults are defined in terrain_render3.cpp. Tests install their
// own; integration forwards them into real reconstructed siblings.
// ---------------------------------------------------------------------------
struct TerrainRender3Hooks {
    // VIBE_Memory_AllocDebug(size, tag) — heap allocate `size` bytes (default:
    // operator new, zero-filled to match the original BSS-cleared blocks).
    void* (*allocDebug)(u32 size, const char* tag);
    // VIBE_Memory_FreeDebug(ptr) — free (default: operator delete[]).
    void  (*freeDebug)(void* ptr);
    // VIBE_Texture_LoadByName(name) -> handle (default: 0 / not found).
    void* (*textureLoad)(const u8* name);
    // VIBE_Texture_ReleaseEntry(handle) (default: no-op).
    void  (*textureRelease)(void* handle);
    // VIBE_Texture_UploadToSurface(handle) -> ok (default: 0).
    int   (*textureUpload)(void* handle);
    // VIBE_Render_DrawLinesD3D(buf, lineCount, r, g, b, width) (default: no-op).
    void  (*drawLines)(const void* buf, u32 lineCount, u8 r, u8 g, u8 b, int w);
    // VIBE_Math_RandomModulo(n) -> [0,n) (default: 0 -> deterministic).
    int   (*randomMod)(u16 n);
};

// Install custom hooks (nullptr members fall back to the inert default).
void InstallTerrainRender3Hooks(const TerrainRender3Hooks& hooks);
// Restore all-inert defaults.
void ResetTerrainRender3Hooks();

// ---------------------------------------------------------------------------
// Sky dome colour state (VIBE_Sky_InitDefaultColors writes 5 global colour ids
// + a count). In the original these are .rdata addresses; we store them as the
// 32-bit values the original stored (the literal addresses 0xE08080 etc.).
// ---------------------------------------------------------------------------
struct SkyDefaultColors {
    i32 horizon;    // dword_1408774 = 0xE08080
    i32 lower;      // dword_1408778 = 0xE0E0E0
    i32 zenith;     // dword_140877C = 0xFFFFFF
    i32 upper;      // dword_1408780 = 0xE0E0E0
    i32 horizon2;   // dword_1408784 = 0xE08080
    i32 count;      // dword_1408770 = 64
};

// gilde.exe 0x5efdc8 — seed the default dome colour ids + count(64).
void InitDefaultColors(SkyDefaultColors* out);

// gilde.exe 0x5efc78 — layer scroll speed: 0 if |speed|==0 else -speed*1e-6.
// (LODWORD&0x7FFFFFFF != 0 tests "non-zero magnitude" i.e. speed != +/-0.0.)
SkyDome* SetLayerScrollSpeed(SkyDome* dome, SkyLayer* layer, float speed);

// gilde.exe 0x5efca8 — set a layer's fade. When |dur| != 0: fadeCur=0,
// fadeRate=1/dur, fadeState/fadeTgt updated from `target`/old fadeFlag.
// When dur==0: fadeCur=1.0, fadeRate=0, both state bytes = old fadeFlag.
u8 SetLayerFade(SkyDome* dome, SkyLayer* layer, u8 target, float dur);

// gilde.exe 0x5efb78 — allocate a layer (via hooks), seed it, load its texture,
// and insert it at head (prepend!=0) or tail of the dome's layer list.
// Returns the new layer (or nullptr on alloc/arg failure).
SkyLayer* CreateLayer(SkyDome* dome, bool prepend, u8 fadeFlag,
                      const u8* textureName, i32 posX, i32 posY,
                      i32 sizeX, i32 sizeY);

// gilde.exe 0x5efb14 — unlink `layer` from the dome list, release its texture,
// and free it. Returns the dome (unchanged on arg failure).
SkyDome* RemoveLayer(SkyDome* dome, SkyLayer* layer);

// gilde.exe 0x5efcf0 — release a layer's current texture, load+upload a new one.
u8 LayerLoadTexture(SkyLayer* layer, const u8* textureName);

// gilde.exe 0x5efd28 — allocate + initialise a dome (createTick from g_gameTick).
// Returns nullptr on alloc failure. (Mesh build is the inert no-op default.)
SkyDome* SkyCreate(i32 owner, i32 param1, i32 param2);

// gilde.exe 0x5efda0 — drain all layers then free the dome. Returns input dome
// pointer value (the original returns the saved register, here the freed ptr).
SkyDome* SkyDestroy(SkyDome* dome);

// gilde.exe 0x5efe0c — drain + free the global dome (dword_64A7C8); returns 0.
SkyDome* SkyDestroyGlobal();

// Test/integration accessors for the global-dome slot (dword_64A7C8) and the
// render-local game-tick mirror that SkyCreate stamps into createTick.
void     SkySetGlobalDome(SkyDome* dome);
SkyDome* SkyGetGlobalDome();
void     SetRenderGameTick(u32 tick);
u32      GetRenderGameTick();

// gilde.exe 0x4b1e10 — copy a 6-vertex colour block (4 floats/vertex) into the
// caller's vertex array (stride 56 bytes, colour quad at +24). `night` picks the
// dword_11BC1C0 branch; `interior` picks the (+529 & 0x10) branch. `dst` points
// at the vertex array base (the original's *(a1+488)); `dstStride` is 56.
void ApplyVertexColors(u8* dst, bool night, bool interior);

// gilde.exe 0x4b236c — copy the 7-entry dome colour rows into the dome node's
// colour arrays. `night` picks dword_11BC1C0; writes `dstA`/`dstB`/`dstC` each
// as 7 RGB triples (the three flt_13FD158/15C/160 arrays in the original) and
// returns the last RGB triple in `lastRGB` (the flt_64A074/78/7C ambient).
void RefreshDomeColors(bool night, float dstA[21], float dstB[21],
                       float dstC[21], float lastRGB[3]);

// ---------------------------------------------------------------------------
// Terrain grid-line geometry. The original walks an 8x8 macro-tile grid, and
// for each LOD'd sub-tile emits 4 line-endpoint vertices (16 bytes: xyz +
// dim-weight) per crossing edge into a scratch buffer, then draws them. We
// expose the per-quad vertex emission as a pure function for golden testing.
//
// For a tile centre P (3 floats) and the two grid basis vectors A,B (each 3
// floats, = the view-matrix-rotated half-extents), the emitter writes 4
// vertices: P-A, P+A, P-B, P+B, with the dim weight `w` in slot [3] of the
// first and third. `dimMask`/`dimSub` select the 0.7x dimming the original
// applies on alternating sub-rows (flt_628B80 = 0.7).
// ---------------------------------------------------------------------------
constexpr float kGridDim = 0.699999988079071f;  // flt_628B80

// Compute the dim weight for a sub-cell (i,j) given the two alternation masks.
// 1.0, *0.7 if (mask1 & i)||(mask1 & j), *0.7 again if (mask2 & i)||(mask2 & j).
float GridDimWeight(int i, int j, int mask1, int mask2);

// Emit the 4 grid-line endpoint vertices for a centre/basis into `out` (16
// floats = 4 vertices x 4 floats). Returns the dim weight written.
float EmitGridQuad(const float centre[3], const float basisA[3],
                   const float basisB[3], float dimWeight, float out[16]);

// ---------------------------------------------------------------------------
// Weather thunder/lightning trigger phase (deterministic RNG core of
// VIBE_Weather_RenderAndThunder @0x4c05ac). Given the active weather type and
// whether the scene allows lightning, decide whether a thunder sound fires
// this frame. Uses the installable randomMod hook (= VIBE_Math_RandomModulo).
//   - fires if RandomModulo(300)==0, OR
//   - (weatherType==3 && RandomModulo(100)==0).
// Returns true when a thunder sound should be triggered.
// ---------------------------------------------------------------------------
bool ThunderShouldTrigger(int weatherType);

} // namespace guild::render
