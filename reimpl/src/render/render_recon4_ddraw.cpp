// =============================================================================
// guild::render — render_recon4_ddraw.cpp
// 1:1 reconstruction of the DDraw/D3D enumeration / caps / mode-table cluster
// of gilde.exe. GPU/DDraw and registry calls are routed through RenderDeviceHooks
// (inert defaults below); the control logic is verbatim.
//
// OMITTED pure DDraw/D3D boundary wrappers (rule 8 — never faked, listed with
// reason; they are pure 1-line vtable calls with no reconstructable logic):
//   0x4346c8 LockSurfaceRegion   — IDirectDrawSurface::Lock wrapper + ptr math
//                                  bound entirely to a locked DDSURFACEDESC.
//   0x434fcc CreateSurface       — pure IDirectDraw::CreateSurface wrapper.
//   0x435094 ReleaseSurface      — pure surface->Release().
//   0x4350ac UnlockSurface       — pure surface->Unlock().
//   0x4350c4 BlitSurface         — pure surface->Blt()/BltFast().
//   0x435ab0 LogSurfaceCaps      — pure GetCaps() + log print (no logic).
//   0x435cb4 SetSurfacePrivateData — pure surface->SetPrivateData().
//   0x435d30 GetSurfacePrivateData — pure surface->GetPrivateData().
//   0x5d8fb0 WithSurfaceContext  — pure Lock/callback/Unlock trampoline.
//   0x5dd5a4 PrecacheTexture     — pure SetTexture + DrawPrimitive priming.
//   0x5dd464 ClearViewport       — pure IDirect3DViewport::Clear2() wrapper.
//   0x5dd9dc DrawTriangleList    — pure DrawPrimitive(TRIANGLELIST) wrapper.
//   0x5de764 ReleaseDeviceObjects— pure device/viewport/zbuf Release sequence.
//   0x5e0324 EndScene            — pure device->EndScene().
//   0x431f44 ReleaseSurfaces     — pure surface/DDraw Release teardown sequence.
//   0x43343c SelectDDrawDevice   — DirectDrawEnumerateEx/Create + registry GUID
//                                  match; the GUID-pick loop is reconstructable
//                                  but is inseparable from live DDraw COM
//                                  creation; deferred (ask-first if needed).
//   0x43371c EnumDisplayModes    — IDDraw::EnumDisplayModes wrapper around
//                                  SelectDDrawDevice + EnumModeCallback (logic
//                                  is in the callback, here reconstructed).
//   0x4337d4 InitDisplayMode     — large DDraw surface-creation state machine;
//                                  the channel-bit-extraction inner loops are
//                                  reconstructable but the function is dominated
//                                  by live surface creation; deferred.
//   0x5dd61c CreateDeviceAndViewport — pure D3D device/viewport/zbuf creation.
//   0x5dd1c4 EnumDevices         — IDDraw7 enum + registry GUID match; bound to
//                                  live COM; the mode-pick loop reconstructable
//                                  but inseparable; deferred.
//   0x434728 ClearRect           — software memset/Lock clear paths; bound to
//                                  the live framebuffer pointers; deferred.
//   0x5b11c8 DrawLinesD3D        — vertex-build + DrawPrimitive; deferred (huge,
//                                  GPU-bound transform/clip pipeline).
//   0x5b1958 DrawObjectBoundingBox / 0x5b6128 DrawDebugOverlay /
//   0x5b5474 DrawTexturedQuad / 0x5eef90 DrawSkyFlareSprite — GPU draw paths.
//   0x56be58 ApplyGfxSettings    — graphics-options apply; reaches ~15 cross-
//                                  module callees (scene traverse, universe slot
//                                  switch, camera, gamma); belongs to the gfx-
//                                  options cluster, not this DDraw leaf set.
// =============================================================================
#include "render/render_recon4_ddraw.h"
#include <cstring>
#include <cstdlib>

namespace guild::render {

// 32-bit branchless abs matching the original's `cdq; xor; sub` sequence
// (well-defined for INT_MIN: returns INT_MIN, exactly like the binary).
static inline i32 abs32(i32 x) {
    i32 t = x >> 31;
    return (x ^ t) - t;
}

// ---------------------------------------------------------------------------
// Inert default hooks. No-op / failure-returning so the headless build links
// and tests run without a GPU or registry.
// ---------------------------------------------------------------------------
namespace {

int   inert_regOpen(const char*, std::uintptr_t)          { return -1; }
int   inert_regClose(int)                                 { return 0; }
void  inert_regSetDword(int, const char*, int)            {}
void  inert_regSetString(int, const char*, const char*)   {}
void  inert_regSetFloat(int, float)                       {}
int   inert_regQueryDword(int, const char*)               { return 0; }
int   inert_regQueryDwordOut(int, const char*, int*)      { return 0; }
int   inert_regQueryString(int, const char*, char*)       { return 0; }
void  inert_regQueryFloat(int, const char*)               {}
void  inert_devBeginScene(int)                            {}
void  inert_devSetCurrentTexture(int, int, int, int)      {}
void  inert_devSetRenderState(int, int, int)              {}

RenderDeviceHooks g_hooks = {
    inert_regOpen, inert_regClose, inert_regSetDword, inert_regSetString,
    inert_regSetFloat, inert_regQueryDword, inert_regQueryDwordOut,
    inert_regQueryString, inert_regQueryFloat,
    inert_devBeginScene, inert_devSetCurrentTexture, inert_devSetRenderState,
};

} // namespace

void InstallRenderDeviceHooks(const RenderDeviceHooks& h) { g_hooks = h; }
const RenderDeviceHooks& GetRenderDeviceHooks() { return g_hooks; }
void ResetRenderDeviceHooks() {
    g_hooks = RenderDeviceHooks{
        inert_regOpen, inert_regClose, inert_regSetDword, inert_regSetString,
        inert_regSetFloat, inert_regQueryDword, inert_regQueryDwordOut,
        inert_regQueryString, inert_regQueryFloat,
        inert_devBeginScene, inert_devSetCurrentTexture, inert_devSetRenderState,
    };
}

// ===========================================================================
// gilde.exe 0x432260 — VIBE_Render_ConfigureSurfaceCaps
// Translates the D3D device-desc (`dev`, base = a1) capability bits into the
// per-mode caps sub-block (`mode`, base = a2). All accesses by raw byte offset
// to match the original exactly. Caps bytes live at mode+780/+781; the
// derived dwords at mode+784..+820. Returns the last byte written (the
// original returns (char)v13, the trailing scratch value).
// ===========================================================================
void ConfigureSurfaceCaps(const u8* dev, u8* mode) {
    // gilde.exe 0x432269: `mov al, [edx+30Dh]` (mode+781) is read BEFORE the
    // memset (rep stosb at 0x432294 over mode+780..823). v52 captures the
    // PRE-memset mode[781]: v52 = (u8)(32 * mode[781]) >> 7 == (mode[781]>>2)&1.
    u8 v52 = (u8)((u8)(32 * mode[781]) >> 7);

    // memset(mode+780, 0, 44)  (0x432285..0x4322a4)
    std::memset(mode + 780, 0, 44);

    // *(mode+781) = (4*(v52&1)) | (*(mode+781) & 0xFB)
    mode[781] = (u8)((4 * (v52 & 1)) | (mode[781] & 0xFB));

    bool v12;  // "has stencil/extra-zbuffer" gate for the second block
    u8 v9 = dev[5];
    if (((v9 & 2) != 0 || (v9 & 1) != 0) && (dev[4] & 0x40) != 0) {
        u8 v10 = dev[140];
        v12 = ((i8)v10 < 0) || (v10 & 2) != 0;
    } else {
        v12 = false;
    }

    // bit3 @ mode+780 = (dev+4 & 1) && *(u32)(dev+24)==1
    {
        bool v14 = (dev[4] & 1) != 0 &&
                   *reinterpret_cast<const u32*>(dev + 24) == 1u;
        u8 v15 = mode[780] & 0xF7;
        mode[780] = (u8)((8 * v14) | v15);
    }

    if ((dev[4] & 0x40) != 0) {
        // bit5 @ mode+780 = (i8)dev[109] >= 0
        u8 v16 = mode[780] & 0xDF;
        u8 v17 = (u8)(32 * ((i8)dev[109] >= 0));
        mode[780] = (u8)(v17 | v16);

        if ((mode[780] & 0x20) != 0) {
            *reinterpret_cast<u32*>(mode + 800) = 1;
            u8 v18 = dev[110];
            u8 v19 = mode[781] & 0xEF;
            mode[781] = (u8)((16 * ((v18 & 4) != 0)) | v19);
        } else {
            *reinterpret_cast<u32*>(mode + 800) = 0;
        }

        // bit7 @ mode+780 = dev[108] & 1
        u8 v20 = mode[780] & 0x7F;
        u8 v21 = (u8)(((dev[108] & 1) != 0) << 7);
        mode[780] = (u8)(v21 | v20);

        // bit6 @ mode+780 = dev[109] & 8
        u8 v22 = dev[109];
        u8 v23 = mode[780] & 0xBF;
        mode[780] = (u8)((((v22 & 8) != 0) << 6) | v23);

        // bit0 @ mode+780 = (dev[129] & 0x40) || (dev[129] & 0x10)
        u8 v24 = dev[129];
        bool v26 = (v24 & 0x40) != 0 || (v24 & 0x10) != 0;
        u8 v27 = mode[780] & 0xFE;
        mode[780] = (u8)(v26 | v27);

        if ((mode[780] & 1) == 0) {
            // bit1 @ mode+780 = (dev[129]&0x20)||(dev[109]&2)
            bool v29 = (dev[129] & 0x20) != 0 || (dev[109] & 2) != 0;
            u8 v30 = mode[780] & 0xFD;
            mode[780] = (u8)((2 * v29) | v30);
        }

        // bit3 @ mode+781 = (mode+780 bit0 clear) && (mode+780 bit1 set)
        u8 v31 = mode[780];
        bool v32 = (v31 & 1) == 0 && (v31 & 2) != 0;
        u8 v33 = mode[781] & 0xF7;
        mode[781] = (u8)((8 * v32) | v33);

        // bit5 @ mode+781 = (i8)dev[108] < 0
        u8 v34 = mode[781] & 0xDF;
        u8 v35 = (u8)(32 * ((i8)dev[108] < 0));
        mode[781] = (u8)(v35 | v34);
    }

    if (v12) {
        // bit2 @ mode+780 = dev[132] & 1
        u8 v36 = mode[780] & 0xFB;
        u8 v37 = (u8)(4 * ((dev[132] & 1) != 0));
        mode[780] = (u8)(v37 | v36);

        // bit4 @ mode+780 = dev[136] & 2
        u8 v38 = mode[780] & 0xEF;
        u8 v39 = (u8)(16 * ((dev[136] & 2) != 0));
        mode[780] = (u8)(v39 | v38);

        // depth-buffer bpp select @ mode+820
        u8 v40 = mode[780];
        *reinterpret_cast<u32*>(mode + 820) = 1;
        if ((v40 & 0x10) != 0) {
            u8 v41 = dev[136];
            if ((v41 & 0x20) == 0) {
                if ((v41 & 8) != 0) *reinterpret_cast<u32*>(mode + 820) = 2;
                goto LABEL_27;
            }
        } else {
            u8 v48 = dev[136];
            if ((v48 & 0x10) == 0) {
                if ((v48 & 4) != 0) *reinterpret_cast<u32*>(mode + 820) = 2;
                goto LABEL_27;
            }
        }
        *reinterpret_cast<u32*>(mode + 820) = 3;
    LABEL_27:
        // bit0 @ mode+781 = dev[108] & 0x20
        u8 v42 = dev[108];
        u8 v43 = mode[781] & 0xFE;
        mode[781] = (u8)(((v42 & 0x20) != 0) | v43);

        // bit1 @ mode+781 = (u16)(dev+250) >= 2 && (dev[244]&8) && (mode+781 bit2 clear)
        bool v45 = *reinterpret_cast<const u16*>(dev + 250) >= 2u &&
                   (dev[244] & 8) != 0 && (mode[781] & 4) == 0;
        u8 v46 = mode[781] & 0xFD;
        mode[781] = (u8)((2 * v45) | v46);

        // dword copies: mode+784 <- dev+172, mode+788 <- dev+176
        *reinterpret_cast<u32*>(mode + 784) = *reinterpret_cast<const u32*>(dev + 172);
        *reinterpret_cast<u32*>(mode + 788) = *reinterpret_cast<const u32*>(dev + 176);

        // mode+792 <- dev+180 (default 1024 if 0)
        u32 v47 = *reinterpret_cast<const u32*>(dev + 180);
        *reinterpret_cast<u32*>(mode + 792) = v47;
        if (!v47) *reinterpret_cast<u32*>(mode + 792) = 1024;

        // mode+796 <- dev+184 (default 1024 if 0)
        u32 v13 = *reinterpret_cast<const u32*>(dev + 184);
        *reinterpret_cast<u32*>(mode + 796) = v13;
        if (!v13) *reinterpret_cast<u32*>(mode + 796) = 1024;

        // src/dest blend caps -> mode+804 / mode+808
        if ((dev[120] & 0x20) != 0 && (dev[116] & 0x10) != 0) {
            *reinterpret_cast<u32*>(mode + 804) = 6;
            *reinterpret_cast<u32*>(mode + 808) = 5;
        } else {
            u8 v49 = dev[120];
            if ((v49 & 0x20) != 0) {
                *reinterpret_cast<u32*>(mode + 804) = 6;
                *reinterpret_cast<u32*>(mode + 808) = 2;
            } else if ((v49 & 2) != 0 && (dev[116] & 2) != 0) {
                *reinterpret_cast<u32*>(mode + 804) = 2;
                *reinterpret_cast<u32*>(mode + 808) = 2;
            } else if ((dev[120] & 4) != 0) {
                *reinterpret_cast<u32*>(mode + 804) = 3;
                *reinterpret_cast<u32*>(mode + 808) = 2;
            } else if ((dev[121] & 1) != 0) {
                *reinterpret_cast<u32*>(mode + 804) = 9;
                *reinterpret_cast<u32*>(mode + 808) = 2;
            } else {
                *reinterpret_cast<u32*>(mode + 804) = 1;
                u8 v50 = mode[780];
                *reinterpret_cast<u32*>(mode + 808) = 2;
                mode[780] = (u8)(v50 & 0xFE);  // clear bit0
            }
        }

        if ((dev[120] & 2) != 0 && (dev[116] & 2) != 0) {
            *reinterpret_cast<u32*>(mode + 812) = 2;
            *reinterpret_cast<u32*>(mode + 816) = 2;
        } else {
            *reinterpret_cast<u32*>(mode + 812) = *reinterpret_cast<u32*>(mode + 804);
            *reinterpret_cast<u32*>(mode + 816) = *reinterpret_cast<u32*>(mode + 808);
        }
    }
}

// ===========================================================================
// gilde.exe 0x4336ac — VIBE_Render_EnumModeCallback
// `mode` raw record: caps flag @+76 (bit 0x40), RGB bit-count @+84,
// width @+12, height @+8.
// ===========================================================================
int EnumModeCallback(const u8* mode, int wantDepth, StdResFlags& out) {
    if ((mode[76] & 0x40) == 0 ||
        wantDepth != *reinterpret_cast<const i32*>(mode + 84))
        return 1;
    i32 w = *reinterpret_cast<const i32*>(mode + 12);
    i32 h = *reinterpret_cast<const i32*>(mode + 8);
    if (w == 800 && h == 600)   out.have_800x600  = 1;
    if (w == 1024 && h == 768)  out.have_1024x768 = 1;
    if (w != 1152 || h != 864)  return 1;
    out.have_1152x864 = 1;
    return 1;
}

// ===========================================================================
// gilde.exe 0x5dce20 — VIBE_Render_EnumTextureFormatsCallback
// `fmt` is a DDPIXELFORMAT-like 32-byte record:
//   +4(=fmt[1]*4) ... here indexed as dwords: d[0]=size, d[1]=flags(fmt[4]),
//   d[2]=fourcc, d[3]=rgbBitCount, d[4]=rMask, d[5]=gMask, d[6]=bMask, d[7]=aMask
//   (byte view: fmt[4] is the low byte of dword[1] flags).
// `acc` accumulator layout (byte offsets):
//   +0   best.rgbBitCount         +8..+39  best DDPIXELFORMAT (opaque)
//   +20  alt.rgbBitCount          +44..+75 alt DDPIXELFORMAT (alpha)
//   +40  alpha.rgbBitCount        +56  alpha-target depth
//   +78  flag: opaque slot filled
// ===========================================================================
int EnumTextureFormatsCallback(const u8* fmt, u8* acc, SurfaceChannelBits& surf) {
    const u32* d  = reinterpret_cast<const u32*>(fmt);
    u32 v33 = d[3];  // rgbBitCount
    u8  v35 = 0, v34 = 0, v36 = 0;

    // Capture the first format whose fourcc matches the requested one (acc+4).
    if (!acc[78] && (fmt[4] & 4) != 0 &&
        *reinterpret_cast<u32*>(acc + 4) &&
        *reinterpret_cast<u32*>(acc + 4) == d[2]) {
        acc[78] = 1;
        *reinterpret_cast<u32*>(acc) = d[3];
        std::memcpy(acc + 8, fmt, 32);
    }

    u8 v9 = fmt[4];  // flags low byte
    if ((v9 & 3) != 0 || v33 < 8 ||
        (v33 == 8 && (v9 & 0x20) == 0) ||
        (v33 > 8 && (fmt[4] & 0x40) == 0))
        return 1;

    if (!acc[78]) {
        u32 accDepth = *reinterpret_cast<u32*>(acc);
        // pick closest-but-not-greater depth, or richer channels at equal depth.
        bool take = false;
        if (!*reinterpret_cast<u32*>(acc + 20)) {
            take = true;
        } else if (v33 >= accDepth &&
                   // gilde.exe 0x5dcfa6/0x5dcfb2: both are 32-bit cdq-abs, signed `jl`.
                   abs32(static_cast<i32>(v33 - accDepth)) <
                   abs32(static_cast<i32>(*reinterpret_cast<u32*>(acc + 20) - accDepth))) {
            take = true;
        } else if (v33 == *reinterpret_cast<u32*>(acc + 20) &&
                   (u32)(d[5] + d[4] + d[6]) >
                   (*reinterpret_cast<u32*>(acc + 32) +
                    *reinterpret_cast<u32*>(acc + 28) +
                    *reinterpret_cast<u32*>(acc + 24))) {
            take = true;
        }
        if (take) std::memcpy(acc + 8, fmt, 32);
    }

    // count set bits in r/g/b masks (32 iterations each) -> v35/v34/v36
    { u32 m = d[4]; for (int i = 0; i < 32; ++i) { if (m & 1) ++v35; m >>= 1; } }
    { u32 m = d[5]; for (int i = 0; i < 32; ++i) { if (m & 1) ++v34; m >>= 1; } }
    { u32 m = d[6]; for (int i = 0; i < 32; ++i) { if (m & 1) ++v36; m >>= 1; } }

    if (!*reinterpret_cast<u32*>(acc + 56))
        goto LABEL_30;
    {
        u32 v32 = *reinterpret_cast<u32*>(acc + 40);
        if (v33 >= v32 &&
            // gilde.exe 0x5dd07d/0x5dd089: both 32-bit cdq-abs, signed `jl`.
            abs32(static_cast<i32>(v33 - v32)) <
            abs32(static_cast<i32>(*reinterpret_cast<u32*>(acc + 56) - *reinterpret_cast<u32*>(acc + 40))))
            goto LABEL_30;
        if (v33 != *reinterpret_cast<u32*>(acc + 56) ||
            (u32)(d[5] + d[4] + d[6]) <=
            (*reinterpret_cast<u32*>(acc + 68) +
             *reinterpret_cast<u32*>(acc + 64) +
             *reinterpret_cast<u32*>(acc + 60))) {
            return 1;
        }
        if (surf.matchedBackbuffer) return 1;
    }
LABEL_30:
    if (v35 == surf.alphaBits && v34 == surf.greenBits && v36 == surf.blueBits)
        surf.matchedBackbuffer = 1;
    std::memcpy(acc + 44, fmt, 32);
    return 1;
}

// ===========================================================================
// gilde.exe 0x5dd534 — VIBE_Render_EnumZBufferFormatsCallback
// `fmt`: dword[1] must == 1024 (DDPF_ZBUFFER), dword[3] = z-depth bits.
// `acc`: dword[0] = wanted depth, dword[5] = current best depth.
// Picks the format with z-depth >= wanted that is closest, copying the
// 32-byte DDPIXELFORMAT into acc+8.
// ===========================================================================
int EnumZBufferFormatsCallback(const u8* fmt, u8* acc) {
    const u32* d = reinterpret_cast<const u32*>(fmt);
    u32* a = reinterpret_cast<u32*>(acc);
    u32 v3 = d[3];
    if (d[1] != 1024) return 1;
    u32 v4 = a[5];
    if (v4) {
        if (v3 < a[0] || v3 - a[0] > v4 - a[0])
            return 1;
    }
    std::memcpy(acc + 8, fmt, 32);
    return 1;
}

} // namespace guild::render
