#pragma once
#include "guild/common/types.h"
#include "render/render_recon4_ddraw.h"  // RenderDeviceHooks (registry boundary)
#include <cstdint>

// =============================================================================
// guild::render — D3D registry config save/load + scene-state sequencing.
// 1:1 from gilde.exe. The exact registry key names and the in-memory config
// struct field offsets are reconstructed verbatim (rule 8); only the
// RegOpenKey/Query/Set/Close syscalls and the D3D SetRenderState calls are
// routed through RenderDeviceHooks (rule 3).
//
//   0x5d3938  VIBE_Render_SaveD3dRegistryConfig
//   0x5d3be8  VIBE_Render_LoadD3dRegistryConfig
//   0x5e0890  VIBE_Render_SaveD3DSettingsToRegistry
//   0x5e0abc  VIBE_Render_LoadD3DSettingsFromRegistry
//   0x5e010c  VIBE_Render_BeginScene
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// LOD-mode global (byte_64A098) is shared between SaveD3dRegistryConfig /
// LoadD3dRegistryConfig and ApplyGfxSettings. Owned here (single definition).
//   1 == LOD_SWITCH, 2 == LOD_LOW, 4 == LOD_HIGH, else "<NULL>"
// ---------------------------------------------------------------------------
u8&  LodModeGlobal();

// ---------------------------------------------------------------------------
// 0x5d3938 — VIBE_Render_SaveD3dRegistryConfig.
// Writes the mode/LOD config. `mode` selects the d3_mode string:
//   0 DRAWDIB, 1 DIRECTWINDOW, 2 DIRECTWINDOWSOFT, 3 FULLSCREEN,
//   4 FULLSCREENSOFT, else "<NULL>" (unk_6292A0).
// Returns the regCloseKey result, or -1 if the key could not be opened.
// Argument order matches the original __thiscall (a2..a25, a26=subkey).
// ---------------------------------------------------------------------------
struct D3dRegistryConfig {
    int   resX        = 0;   // a2  -> d3_screen_res_x
    int   resY        = 0;   // a3  -> d3_screen_res_y
    int   resDepth    = 0;   // a4  -> d3_screen_res_depth
    char  mode        = 0;   // a5  -> d3_mode (string)
    int   useHardware = 0;   // a6  -> d3_use_hardware
    int   useTriple   = 0;   // a7  -> d3d_use_triple
    int   maxPolys    = 0;   // a8  -> d3_max_polys
    int   maxTextures = 0;   // a9  -> d3_max_textures
    int   maxPalettes = 0;   // a10 -> d3s_max_palettes
    int   maxMipmaps  = 0;   // a11 -> d3_max_mipmaps
    int   zBufferDepth= 0;   // a12 -> d3d_z_buffer_depth
    int   staticTexFcc= 0;   // a13 -> d3d_static_texture_fourcc
    int   floortexDiv = 0;   // a15 -> d3d_floortex_divider
    float winWidth    = 0;   // a19 -> d3_window_width
    float winHeight   = 0;   // a20 -> d3_window_height
    float winXOffset  = 0;   // a21 -> d3_window_x_offset
    float winYOffset  = 0;   // a22 -> d3_window_y_offset
    float winNearZ    = 0;   // a23 -> d3_window_near_z
    float winFarZ     = 0;   // a24 -> d3_window_far_z
    float winViewDist = 0;   // a25 -> d3_window_viewdistance
};
int SaveD3dRegistryConfig(const D3dRegistryConfig& cfg, const char* subkey);

// 0x5d3be8 — VIBE_Render_LoadD3dRegistryConfig.
// Reads back the same keys (requires d3_registry_version == 3). Returns 1 on
// success, 0 on missing/version-mismatch key. Fills the int[] view of the
// engine config block (offsets match the original a1[] indices); see .cpp.
// Returns the parsed mode index in `outModeIndex` (a1+12 byte), LOD into the
// shared LodModeGlobal(). Floats go through regQueryFloat (engine globals).
int LoadD3dRegistryConfig(D3dRegistryConfig& cfg, int& outModeIndex,
                          const char* subkey);

// ---------------------------------------------------------------------------
// 0x5e0890 / 0x5e0abc — render-flag settings. The original packs the flags
// into a 12-byte struct: byte[0] feature bits, dword[1] fillmode, dword[2]
// mipfilter, plus mirrors/shadows in byte[0]/byte[1]. We reconstruct the exact
// bit packing.
// ---------------------------------------------------------------------------
struct D3DSettings {
    // byte 0 bit layout (from SaveD3DSettingsToRegistry shifts):
    u8 antialias    = 0;  // bit0  d3d_antialias            (a2 & 1)
    u8 bilinear     = 0;  // bit1  d3d_bilinear_filtering   ((a2<<6)>>7)
    u8 dither       = 0;  // bit2  d3d_dither               ((32*a2)>>7)
    u8 perspective  = 0;  // bit3  d3d_perspective_correction ((16*a2)>>7)
    u8 subpixel     = 0;  // bit4  d3d_subpixel             ((8*a2)>>7)
    u8 multitexture = 0;  // bit5  d3d_multitexture         ((4*a2)>>7)
    u8 mirrors      = 0;  // bit6  d3_mirrors               ((2*a2)>>7)
    u8 shadows      = 0;  // byte1 low3  d3d_shadows        (HIBYTE(a2)&7)
    int fillmode    = 0;  // dword1  d3d_fillmode  (1 POINT,2 WIREFRAME,3 SOLID)
    int mipfilter   = 0;  // dword2  d3d_mipfilter (1 NONE,2 POINT,3 LINEAR)
};
// Returns regCloseKey result, or -1 if key open failed.
int SaveD3DSettingsToRegistry(const D3DSettings& s, const char* subkey);
// Returns 1 on success (version==3), 0 otherwise.
int LoadD3DSettingsFromRegistry(D3DSettings& s, const char* subkey);

// ---------------------------------------------------------------------------
// 0x5e010c — VIBE_Render_BeginScene. Emits the exact D3D render-state
// sequence the engine sets at scene start. The device handle + SetRenderState
// go through hooks; the state IDs / values / branch logic are reconstructed.
//   alphaTest: a1 (also drives RS 34 ref + RS 28 enable)
//   alphaRef : a2 (RS 34, only when alphaTest)
//   zEnable  : a3 (RS 7 fog-table when set; RS 14 z-enable = a3 && !noZBuffer)
// `device` is the D3D device handle; `fogTable` == dword_1408080;
// `noZBuffer` == byte_64A352. Updates beginSceneZ (byte_64A357) and
// beginSceneAlpha (byte_64A356) which are returned via the state struct.
// ---------------------------------------------------------------------------
struct BeginSceneState {
    u8 lastZEnable    = 0;  // byte_64A357
    u8 lastAlpha      = 0;  // byte_64A356
};
void BeginScene(int device, int fogTable, u8 noZBuffer,
                u8 alphaTest, int alphaRef, u8 zEnable,
                BeginSceneState& st);

} // namespace guild::render
