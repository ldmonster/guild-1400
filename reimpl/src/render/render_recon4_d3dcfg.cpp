// =============================================================================
// guild::render — render_recon4_d3dcfg.cpp
// 1:1 reconstruction of the D3D registry config save/load and the BeginScene
// render-state sequencer of gilde.exe. The Win32 registry primitives and the
// D3D SetRenderState calls go through RenderDeviceHooks (rule 3); the exact key
// names, value layout, struct field packing and state sequence are verbatim.
// =============================================================================
#include "render/render_recon4_d3dcfg.h"
#include "render/render_recon4_ddraw.h"

namespace guild::render {

namespace {
u8 g_lodMode = 0;  // byte_64A098 (1 SWITCH, 2 LOW, 4 HIGH)

// Registry key name literals (recovered via get_bytes; addresses in comments).
const char* kD3ScreenResX   = "d3_screen_res_x";          // 0x629178
const char* kD3ScreenResY   = "d3_screen_res_y";          // 0x629188
const char* kD3ScreenResDep = "d3_screen_res_depth";      // 0x629198
const char* kD3UseHardware  = "d3_use_hardware";          // 0x6291ac
const char* kD3dUseTriple   = "d3d_use_triple";           // 0x6291bc
const char* kD3MaxPolys     = "d3_max_polys";             // 0x6291cc
const char* kD3MaxTextures  = "d3_max_textures";          // 0x6291dc
const char* kD3MaxMipmaps   = "d3_max_mipmaps";           // 0x6291ec / 0x629388 (load: d3d_max_mipmaps)
const char* kD3dMaxMipmaps  = "d3d_max_mipmaps";          // 0x629388 (load side)
const char* kD3sMaxPalettes = "d3s_max_palettes";         // 0x6291fc
const char* kD3dFloortexDiv = "d3d_floortex_divider";     // 0x629210
const char* kD3dZBufferDept = "d3d_z_buffer_depth";       // 0x629228
const char* kD3dStaticTex   = "d3d_static_texture_fourcc"; // 0x62923c
const char* kD3Mode         = "d3_mode";                  // 0x6292a4
const char* kD3ioLodmode    = "d3io_LODMode";             // 0x6292dc
const char* kD3WindowWidth  = "d3_window_width";          // 0x6292ec
const char* kD3WindowHeight = "d3_window_height";         // 0x6292fc
const char* kD3WindowXOff   = "d3_window_x_offset";       // 0x629310
const char* kD3WindowYOff   = "d3_window_y_offset";       // 0x629324
const char* kD3WindowNearZ  = "d3_window_near_z";         // 0x629338
const char* kD3WindowFarZ   = "d3_window_far_z";          // 0x62934c
const char* kD3WindowViewDi = "d3_window_viewdistance";   // 0x62935c
const char* kD3RegistryVers = "d3_registry_version";      // 0x629374

// d3_mode string table (mode index -> name); else "<NULL>".
const char* kModeStr[5] = {
    "DRAWDIB",          // 0  0x629258
    "DIRECTWINDOW",     // 1  0x629260
    "DIRECTWINDOWSOFT", // 2  0x629270
    "FULLSCREEN",       // 3  0x629284
    "FULLSCREENSOFT",   // 4  0x629290
};
const char* kModeNull = "<NULL>";  // unk_6292A0 (the engine writes a "<NULL>"-like sentinel)

// d3io LOD-mode string table.
const char* kLodSwitch = "d3io_LOD_SWITCH"; // 0x6292ac
const char* kLodLow    = "d3io_LOD_LOW";    // 0x6292bc
const char* kLodHigh   = "d3io_LOD_HIGH";   // 0x6292cc

// d3d-settings keys (0x5e0890 / 0x5e0abc).
const char* kD3dAntialias    = "d3d_antialias";              // 0x62b8e4
const char* kD3dBilinear     = "d3d_bilinear_filtering";     // 0x62b8f4
const char* kD3dDither       = "d3d_dither";                 // 0x62b90c
const char* kD3dPerspective  = "d3d_perspective_correction"; // 0x62b918
const char* kD3dSubpixel     = "d3d_subpixel";               // 0x62b934
const char* kD3dMultitexture = "d3d_multitexture";           // 0x62b944
const char* kD3dFillmode     = "d3d_fillmode";               // 0x62b990
const char* kD3dMipfilter    = "d3d_mipfilter";              // 0x62b9fc
const char* kD3Mirrors       = "d3_mirrors";                 // 0x62b9cc
const char* kD3dShadows      = "d3d_shadows";                // 0x62b9d8
const char* kD3dRegistryVer  = "d3d_registry_version";       // 0x62b9e4

const char* kFillPoint    = "D3DFILL_POINT";    // 0x62b958
const char* kFillWireframe = "D3DFILL_WIREFRAME"; // 0x62b968
const char* kFillSolid    = "D3DFILL_SOLID";    // 0x62b97c
const char* kTfpNone      = "D3DTFP_NONE";      // 0x62b9a0
const char* kTfpPoint     = "D3DTFP_POINT";     // 0x62b9ac
const char* kTfpLinear    = "D3DTFP_LINEAR";    // 0x62b9bc

bool strieq(const char* a, const char* b) {
    // gilde.exe VIBE_Util_StrCmpNoCase_Thunk returns 0 when equal (like strcmp).
    // Reproduce a case-insensitive compare; here we return true when EQUAL.
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == *b;
}
} // namespace

u8& LodModeGlobal() { return g_lodMode; }

// ===========================================================================
// gilde.exe 0x5d3938 — VIBE_Render_SaveD3dRegistryConfig
// ===========================================================================
int SaveD3dRegistryConfig(const D3dRegistryConfig& cfg, const char* subkey) {
    const auto& h = GetRenderDeviceHooks();
    int hkey = h.regOpenKey(subkey, 0);
    if (hkey == -1) return -1;

    h.regSetDword(hkey, kD3ScreenResX,   cfg.resX);
    h.regSetDword(hkey, kD3ScreenResY,   cfg.resY);
    h.regSetDword(hkey, kD3ScreenResDep, cfg.resDepth);
    h.regSetDword(hkey, kD3UseHardware,  cfg.useHardware);
    h.regSetDword(hkey, kD3dUseTriple,   cfg.useTriple);
    h.regSetDword(hkey, kD3MaxPolys,     cfg.maxPolys);
    h.regSetDword(hkey, kD3MaxTextures,  cfg.maxTextures);
    h.regSetDword(hkey, kD3MaxMipmaps,   cfg.maxMipmaps);
    h.regSetDword(hkey, kD3sMaxPalettes, cfg.maxPalettes);
    h.regSetDword(hkey, kD3dFloortexDiv, cfg.floortexDiv);
    h.regSetDword(hkey, kD3dZBufferDept, cfg.zBufferDepth);
    h.regSetDword(hkey, kD3dStaticTex,   cfg.staticTexFcc);

    const char* modeStr;
    switch (cfg.mode) {
        case 0: modeStr = kModeStr[0]; break;
        case 1: modeStr = kModeStr[1]; break;
        case 2: modeStr = kModeStr[2]; break;
        case 3: modeStr = kModeStr[3]; break;
        case 4: modeStr = kModeStr[4]; break;
        default: modeStr = kModeNull; break;
    }
    h.regSetString(hkey, kD3Mode, modeStr);

    const char* lodStr;
    if ((u8)g_lodMode >= 2) {
        if ((u8)g_lodMode <= 2)      lodStr = kLodLow;
        else if (g_lodMode == 4)     lodStr = kLodHigh;
        else                         lodStr = kModeNull;
    } else if (g_lodMode == 1) {
        lodStr = kLodSwitch;
    } else {
        lodStr = kModeNull;
    }
    h.regSetString(hkey, kD3ioLodmode, lodStr);

    h.regSetFloat(hkey, cfg.winWidth);
    h.regSetFloat(hkey, cfg.winHeight);
    h.regSetFloat(hkey, cfg.winXOffset);
    h.regSetFloat(hkey, cfg.winYOffset);
    h.regSetFloat(hkey, cfg.winNearZ);
    h.regSetFloat(hkey, cfg.winFarZ);
    h.regSetFloat(hkey, cfg.winViewDist);

    h.regSetDword(hkey, kD3RegistryVers, 3);
    return h.regCloseKey(hkey);
}

// ===========================================================================
// gilde.exe 0x5d3be8 — VIBE_Render_LoadD3dRegistryConfig
// ===========================================================================
int LoadD3dRegistryConfig(D3dRegistryConfig& cfg, int& outModeIndex,
                          const char* subkey) {
    const auto& h = GetRenderDeviceHooks();
    int hkey = h.regOpenKey(subkey, 0);
    if (hkey == -1) return 0;

    if (h.regQueryDword(hkey, kD3RegistryVers) != 3) {
        h.regCloseKey(hkey);
        return 0;
    }

    int tmp = 0;
    h.regQueryDwordOut(hkey, kD3ScreenResX,   &cfg.resX);
    h.regQueryDwordOut(hkey, kD3ScreenResY,   &cfg.resY);
    h.regQueryDwordOut(hkey, kD3ScreenResDep, &cfg.resDepth);
    if (h.regQueryDwordOut(hkey, kD3UseHardware, &tmp)) cfg.useHardware = tmp;
    if (h.regQueryDwordOut(hkey, kD3dUseTriple,  &tmp)) cfg.useTriple   = tmp;
    h.regQueryDwordOut(hkey, kD3MaxPolys,     &cfg.maxPolys);
    h.regQueryDwordOut(hkey, kD3MaxTextures,  &cfg.maxTextures);
    h.regQueryDwordOut(hkey, kD3sMaxPalettes, &cfg.maxPalettes);
    h.regQueryDwordOut(hkey, kD3dMaxMipmaps,  &cfg.maxMipmaps);
    h.regQueryDwordOut(hkey, kD3dFloortexDiv, &cfg.floortexDiv);
    h.regQueryDwordOut(hkey, kD3dZBufferDept, &cfg.zBufferDepth);
    h.regQueryDwordOut(hkey, kD3dStaticTex,   &cfg.staticTexFcc);

    char buf[256];
    if (h.regQueryString(hkey, kD3Mode, buf)) {
        if (strieq(buf, kModeStr[0]))      outModeIndex = 0;
        else if (strieq(buf, kModeStr[1])) outModeIndex = 1;
        else if (strieq(buf, kModeStr[2])) outModeIndex = 2;
        else if (strieq(buf, kModeStr[3])) outModeIndex = 3;
        else if (strieq(buf, kModeStr[4])) outModeIndex = 4;
    }
    if (h.regQueryString(hkey, kD3ioLodmode, buf)) {
        if (strieq(buf, kLodSwitch))    g_lodMode = 1;
        else if (strieq(buf, kLodLow))  g_lodMode = 2;
        else if (strieq(buf, kLodHigh)) g_lodMode = 4;
    }

    h.regQueryFloat(hkey, kD3WindowWidth);
    h.regQueryFloat(hkey, kD3WindowHeight);
    h.regQueryFloat(hkey, kD3WindowXOff);
    h.regQueryFloat(hkey, kD3WindowYOff);
    h.regQueryFloat(hkey, kD3WindowNearZ);
    h.regQueryFloat(hkey, kD3WindowFarZ);
    h.regQueryFloat(hkey, kD3WindowViewDi);

    // gilde.exe 0x5d3df0-0x5d3e16: viewDist (*(float*)(a1+23), byte off 92) is
    // recomputed from resX (a1[0]) as a fild/fmul/fmul:
    //   v38 = *a1;  *((float*)a1+23) = (double)v38 * flt_629398 * flt_62939C
    //   flt_629398 = 0x44000000 = 512.0f, flt_62939C = 0x3acccccd = 0.0015625f
    // i.e. winViewDist = resX * 512.0 * 0.0015625 = resX * 0.8 (computed in
    // double then truncated to float by fstp dword).
    cfg.winViewDist = static_cast<float>(
        static_cast<double>(static_cast<unsigned int>(cfg.resX)) *
        512.0 * 0.0015625);

    cfg.mode = (char)outModeIndex;
    h.regCloseKey(hkey);
    return 1;
}

// ===========================================================================
// gilde.exe 0x5e0890 — VIBE_Render_SaveD3DSettingsToRegistry
// The original packs the bits into a __int16 a2 (feature flags) + a3 fillmode
// + a4 mipfilter. We accept the unpacked struct and write the same keys.
// ===========================================================================
int SaveD3DSettingsToRegistry(const D3DSettings& s, const char* subkey) {
    const auto& h = GetRenderDeviceHooks();
    int hkey = h.regOpenKey(subkey, 0);
    if (hkey == -1) return -1;

    h.regSetDword(hkey, kD3dAntialias,    s.antialias    & 1);
    h.regSetDword(hkey, kD3dBilinear,     s.bilinear     & 1);
    h.regSetDword(hkey, kD3dDither,       s.dither       & 1);
    h.regSetDword(hkey, kD3dPerspective,  s.perspective  & 1);
    h.regSetDword(hkey, kD3dSubpixel,     s.subpixel     & 1);
    h.regSetDword(hkey, kD3dMultitexture, s.multitexture & 1);

    const char* fill;
    if (s.fillmode >= 2) {
        if (s.fillmode <= 2)      fill = kFillWireframe;
        else if (s.fillmode == 3) fill = kFillSolid;
        else                      fill = kModeNull;
    } else if (s.fillmode == 1) {
        fill = kFillPoint;
    } else {
        fill = kModeNull;
    }
    h.regSetString(hkey, kD3dFillmode, fill);

    const char* mip;
    if (s.mipfilter >= 2) {
        if (s.mipfilter <= 2)      mip = kTfpPoint;
        else if (s.mipfilter == 3) mip = kTfpLinear;
        else                       mip = kModeNull;
    } else if (s.mipfilter == 1) {
        mip = kTfpNone;
    } else {
        mip = kModeNull;
    }
    // NOTE: the original writes both fillmode and mipfilter strings to the SAME
    // key name "d3d_fillmode" (a verbatim bug in gilde.exe at 0x5e0a0a — the
    // second SetStringValue reuses aD3dFillmode). Preserved 1:1.
    h.regSetString(hkey, kD3dFillmode, mip);

    h.regSetDword(hkey, kD3Mirrors,      s.mirrors & 1);
    h.regSetDword(hkey, kD3dShadows,     s.shadows & 7);
    h.regSetDword(hkey, kD3dRegistryVer, 3);
    return h.regCloseKey(hkey);
}

// ===========================================================================
// gilde.exe 0x5e0abc — VIBE_Render_LoadD3DSettingsFromRegistry
// ===========================================================================
int LoadD3DSettingsFromRegistry(D3DSettings& s, const char* subkey) {
    const auto& h = GetRenderDeviceHooks();
    int hkey = h.regOpenKey(subkey, 0);
    if (hkey == -1) return 0;

    if (h.regQueryDword(hkey, kD3dRegistryVer) != 3) {
        h.regCloseKey(hkey);
        return 0;
    }

    int v = 0;
    if (h.regQueryDwordOut(hkey, kD3dAntialias,    &v)) s.antialias    = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dBilinear,     &v)) s.bilinear     = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dDither,       &v)) s.dither       = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dPerspective,  &v)) s.perspective  = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dSubpixel,     &v)) s.subpixel     = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dMultitexture, &v)) s.multitexture = (u8)(v & 1);

    char buf[256];
    if (h.regQueryString(hkey, kD3dFillmode, buf)) {
        if (strieq(buf, kFillPoint))         s.fillmode = 1;
        else if (strieq(buf, kFillWireframe)) s.fillmode = 2;
        else                                  s.fillmode = 3;  // SOLID / fallthrough
    }
    if (h.regQueryString(hkey, kD3dMipfilter, buf)) {
        // gilde.exe 0x5e0c98-0x5e0da5 (verified vs disasm):
        //   buf==NONE                       -> 1
        //   buf==POINT                      -> 2   (jnz at 0x5e0d80 not taken)
        //   buf!=POINT && buf!=LINEAR       -> 2   (jnz at 0x5e0d9c taken)
        //   buf!=POINT && buf==LINEAR       -> 3   (fall-through, 0x5e0d9e)
        // i.e. mipfilter==3 ONLY when buf==LINEAR. The "=2" condition is
        // (buf==POINT) || (buf!=LINEAR).
        if (strieq(buf, kTfpNone))                                   s.mipfilter = 1;
        else if (strieq(buf, kTfpPoint) || !strieq(buf, kTfpLinear)) s.mipfilter = 2;
        else                                                         s.mipfilter = 3;
    }
    if (h.regQueryDwordOut(hkey, kD3Mirrors,  &v)) s.mirrors = (u8)(v & 1);
    if (h.regQueryDwordOut(hkey, kD3dShadows, &v)) s.shadows = (u8)(v & 7);

    h.regCloseKey(hkey);
    return 1;
}

// ===========================================================================
// gilde.exe 0x5e010c — VIBE_Render_BeginScene
// Emits the exact render-state sequence; device + SetRenderState via hooks.
//   RS 24 (alpharef-test func/cmp)  = 191
//   RS 25 (alphafunc)               = 5
//   RS 34 (alpha ref)               = alphaRef   (only if alphaTest)
//   RS 28 (alpha-blend enable)      = alphaTest
//   RS 7  (fog table addr)          = zEnable ? fogTable : 0
//   RS 14 (z-enable)                = zEnable && !noZBuffer
// ===========================================================================
void BeginScene(int device, int fogTable, u8 noZBuffer,
                u8 alphaTest, int alphaRef, u8 zEnable,
                BeginSceneState& st) {
    const auto& h = GetRenderDeviceHooks();
    h.deviceBeginScene(device);
    h.deviceSetCurrentTexture(device, 0, 0, 0);
    h.deviceSetRenderState(device, 24, 191);
    h.deviceSetRenderState(device, 25, 5);
    if (alphaTest)
        h.deviceSetRenderState(device, 34, alphaRef);
    h.deviceSetRenderState(device, 28, (u8)alphaTest);
    int v4 = zEnable ? fogTable : 0;
    h.deviceSetRenderState(device, 7, v4);
    int v5 = (zEnable && !noZBuffer) ? 1 : 0;
    h.deviceSetRenderState(device, 14, v5);
    st.lastZEnable = zEnable;
    st.lastAlpha   = alphaTest;
}

} // namespace guild::render
