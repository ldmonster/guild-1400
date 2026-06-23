#pragma once
#include "guild/common/types.h"
#include <cstdint>

// =============================================================================
// guild::render — DirectDraw/Direct3D control-logic layer, recon batch 4
// (render_recon4). 1:1 translations from the Hex-Rays reference of gilde.exe
// (imagebase 0x400000) of the VIBE_Render_* DDraw/D3D enumeration / caps /
// registry / scene-state-sequencing cluster.
//
// SCOPE / RULE-3 BOUNDARY
//   The original cluster is half pure engine logic (mode-enum heuristics,
//   surface-caps bit evaluation, texture/z-buffer format scoring, the registry
//   save/load of the D3D settings struct, render-state sequencing) and half
//   pure DirectDraw/Direct3D vtable calls. Per rule 3 (3D tech -> Vulkan) and
//   rule 6, the GPU/DDraw/D3D API CALLS are the boundary: they are routed
//   through an installable RenderDeviceHooks function-pointer table with INERT
//   defaults (defined in render_recon4_ddraw.cpp). A real IGraphicsDevice
//   backend (or a test recorder) binds the hooks. The CONTROL LOGIC around the
//   calls is reconstructed verbatim.
//
//   Likewise the Win32 registry primitives (RegOpenKey/RegQuery/RegSet/Close)
//   are routed through hooks; the EXACT key names, value layout and the D3D
//   settings struct field offsets are real logic and are reconstructed here.
//
// Functions reconstructed here (all verified ABSENT at write time):
//   0x432260  VIBE_Render_ConfigureSurfaceCaps         (D3D-device caps -> mode caps)
//   0x4327a0  VIBE_Render_RegisterVideoMode            (mode-table population)
//   0x4336ac  VIBE_Render_EnumModeCallback             (std-resolution detection)
//   0x5dce20  VIBE_Render_EnumTextureFormatsCallback   (texture-format scoring)
//   0x5dd534  VIBE_Render_EnumZBufferFormatsCallback   (z-buffer-format filter)
//   (0x5af268..0x5af288 GetViewParamB..F are already in render_leaves3 — reused.)
//   0x5d3938  VIBE_Render_SaveD3dRegistryConfig        (mode/LOD config save)
//   0x5d3be8  VIBE_Render_LoadD3dRegistryConfig        (mode/LOD config load)
//   0x5e0890  VIBE_Render_SaveD3DSettingsToRegistry    (render-flag config save)
//   0x5e0abc  VIBE_Render_LoadD3DSettingsFromRegistry  (render-flag config load)
//   0x5e010c  VIBE_Render_BeginScene                   (render-state sequencing)
//   0x5af268..0x5af288  VIBE_Render_GetViewParamB..F    (viewport-param getters)
//
// Pure 1-line GPU/DDraw wrappers are OMITTED (see .cpp header comment for the
// address + reason list, per rule 8 — never faked).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// RenderDeviceHooks — the rule-3 GPU/DDraw/D3D + registry boundary. Inert
// defaults in render_recon4_ddraw.cpp; a backend / test recorder installs real
// implementations. Each maps to a specific gilde.exe call site.
// ---------------------------------------------------------------------------
struct RenderDeviceHooks {
    // --- Win32 registry primitives (the keys/values are reconstructed; only
    //     the syscalls are routed) ---
    // 0x40c420 VIBE_Registry_OpenKey(name, arg) -> HKEY (-1 == fail)
    int  (*regOpenKey)(const char* subkey, std::uintptr_t arg);
    // 0x40c4c8 VIBE_Registry_CloseKey(hkey)
    int  (*regCloseKey)(int hkey);
    // 0x40c670 VIBE_Registry_SetDwordValue(hkey, name, _, value)
    void (*regSetDword)(int hkey, const char* name, int value);
    // 0x40c690 VIBE_Registry_SetStringValue(hkey, name, str)  (wide/2-byte str)
    void (*regSetString)(int hkey, const char* name, const char* value);
    // 0x40c6b8 VIBE_Registry_SetFloatValue(hkey, value)
    void (*regSetFloat)(int hkey, float value);
    // 0x40c4d8 VIBE_Registry_QueryDwordValue(hkey, name) -> dword
    int  (*regQueryDword)(int hkey, const char* name);
    // 0x40c510 VIBE_Registry_QueryDwordOut(hkey, name, out) -> nonzero if present
    int  (*regQueryDwordOut)(int hkey, const char* name, int* out);
    // 0x40c550 VIBE_Registry_QueryStringValue(hkey, name, outBuf) -> nonzero
    int  (*regQueryString)(int hkey, const char* name, char* outBuf);
    // 0x40c608 VIBE_Registry_QueryFloatOut(hkey, name)  (writes engine globals)
    void (*regQueryFloat)(int hkey, const char* name);

    // --- D3D device render-state (BeginScene 0x5e010c) ---
    // (*(device+36))(device)            BeginScene
    void (*deviceBeginScene)(int device);
    // (*(device+152))(device,a,b,c)     SetTexture / current-texture
    void (*deviceSetCurrentTexture)(int device, int a, int b, int c);
    // (*(device+88))(device, state, value)   SetRenderState
    void (*deviceSetRenderState)(int device, int state, int value);
};

void InstallRenderDeviceHooks(const RenderDeviceHooks& hooks);
const RenderDeviceHooks& GetRenderDeviceHooks();
void ResetRenderDeviceHooks();  // restore inert defaults (tests)

// NOTE: 0x432770 VIBE_Render_DepthToModeFlag is already reconstructed in
// render_leaves.{h,cpp} as guild::render::DepthToModeFlag(i32); reuse that
// (do not redefine — ODR).

// ===========================================================================
// Surface-caps evaluation (ConfigureSurfaceCaps 0x432260).
//
// The original takes two raw byte blobs: `dev` is a D3DDEVICEDESC-style record
// (the half-pitched DDraw device-capabilities struct passed to the device
// callback) and `mode` is the per-mode record entry (an 824-byte slot in the
// global mode table, here addressed from its +528 caps sub-block). All the
// reads/writes are by raw byte offset to match the original exactly; we keep
// the same offsets so golden bytes can be asserted.
// ===========================================================================
// `dev`  base = a1 (D3D device-desc record).
// `mode` base = a2 (mode record; caps live at mode+780/+781, dwords at +784..+820).
void ConfigureSurfaceCaps(const u8* dev, u8* mode);

// ===========================================================================
// Standard-resolution detection (EnumModeCallback 0x4336ac).
//
// The DDraw EnumDisplayModes callback record `mode` (raw bytes) is examined;
// when caps flag bit 0x40 @+76 is set and the RGB bit-count @+84 matches the
// requested depth, the 800x600 / 1024x768 / 1152x864 availability flags are
// set. Returns the DDENUMRET_OK (==1) continue value.
// ===========================================================================
struct StdResFlags {
    u8 have_800x600  = 0;  // byte_62D599
    u8 have_1024x768 = 0;  // byte_62D59A
    u8 have_1152x864 = 0;  // byte_62D59B
};
int EnumModeCallback(const u8* mode, int wantDepth, StdResFlags& out);

// ===========================================================================
// Texture-format scoring (EnumTextureFormatsCallback 0x5dce20 +
// EnumTextureFormats final-select 0x5dd0a0).
//
// The original walks a DDPIXELFORMAT (raw bytes, base a1) and a stateful
// accumulator record (raw bytes, base a2) selecting the best opaque + best
// alpha texture format, and (separately) flags whether the chosen format
// matches the back-buffer RGB layout. We reconstruct the full scoring logic,
// keeping the field offsets verbatim. `surf` holds the back-buffer channel bit
// counts (byte_762718/76271F/762720) consulted by the "matches back-buffer"
// test (byte_14080E4).
// ===========================================================================
struct SurfaceChannelBits {
    u8 alphaBits = 0;  // byte_762718
    u8 greenBits = 0;  // byte_76271F
    u8 blueBits  = 0;  // byte_762720
    u8 matchedBackbuffer = 0;  // byte_14080E4 (set when an enumerated fmt matches)
};
// Returns DDENUMRET_OK (==1). `acc` (>=80 bytes) is the accumulator struct.
int EnumTextureFormatsCallback(const u8* fmt, u8* acc, SurfaceChannelBits& surf);
// Returns DDENUMRET_OK (==1). `acc` (>=32 bytes) is the z-buffer accumulator.
int EnumZBufferFormatsCallback(const u8* fmt, u8* acc);

// NOTE: 0x5af268..0x5af288 VIBE_Render_GetViewParamB..F are already
// reconstructed in render_leaves3.{h,cpp} (guild::render::GetViewParamB..F over
// the shared ViewParams / ViewParamState()); reuse those — do not redefine.

} // namespace guild::render
