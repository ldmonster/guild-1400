#include "render/surface_present.h"

#include <cstdint>
#include <cstdio>

namespace guild::render {

// gilde.exe 0x4343E4 — VIBE_Render_LockSurface
//
// Original (Hex-Rays): builds a DDSURFACEDESC (dwSize=124 at v5[0]), then loops:
//   v3 = surf->Lock(0, &desc, 1, 0);            // vtbl+100, flags=1
//   if (!v3) { record lock globals; return desc.lpSurface; }
//   if (v3 != DDERR_SURFACELOST) return 0;
//   while (!surf->Restore());                    // vtbl+108
// The recorded globals (faithful):
//   dword_7626EC = lPitch                (desc+0x10)   -> lockPitch
//   dword_7626FC = lPitch                              -> pitchBytes (current)
//   dword_762714 = dwRGBBitCount         (desc+0x54)   -> lockBitDepth
//   dword_7626F4 = dwRGBBitCount >> 3                  -> pitchExtra
//   dword_7626F8 = lPitch / dword_7626E8              -> strideWords (pixels/row)
// Returns desc.lpSurface (the +0x24 field, here DDrawLock.pixels).
std::uintptr_t LockSurface(IDDrawSurface& surf, PresentGlobals& g) {
    DDrawLock desc{};
    for (;;) {
        i32 hr = surf.Lock(/*flags*/ 1, desc);
        if (hr == 0) {
            g.lockPitch    = static_cast<i32>(desc.pitch);
            g.pitchBytes   = static_cast<i32>(desc.pitch);
            g.lockBitDepth = static_cast<i32>(desc.bitDepth);
            g.pitchExtra   = static_cast<i32>(desc.bitDepth) >> 3;
            i32 div = g.bytesPerPx ? g.bytesPerPx : 1;   // dword_7626E8
            g.strideWords  = static_cast<i32>(desc.pitch) / div;
            return reinterpret_cast<std::uintptr_t>(desc.pixels);
        }
        if (hr != kDDErrSurfaceLost)
            return 0;
        if (surf.Restore() != 0)   // loop continues only while Restore succeeds (==0)
            return 0;
    }
}

// gilde.exe 0x434468 — VIBE_Render_LockSurfaceWait
// Identical to LockSurface except the Lock dwFlags is (flags | 0x801) — the WAIT
// path (DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR). The LOWORD-OR in the original means
// only the low 16 bits of flags carry the lock bits.
std::uintptr_t LockSurfaceWait(IDDrawSurface& surf, u32 flags, PresentGlobals& g) {
    u32 lockFlags = (flags & 0xFFFF0000u) | ((flags | 0x801u) & 0xFFFFu);
    DDrawLock desc{};
    for (;;) {
        i32 hr = surf.Lock(lockFlags, desc);
        if (hr == 0) {
            g.lockPitch    = static_cast<i32>(desc.pitch);
            g.pitchBytes   = static_cast<i32>(desc.pitch);
            g.lockBitDepth = static_cast<i32>(desc.bitDepth);
            g.pitchExtra   = static_cast<i32>(desc.bitDepth) >> 3;
            i32 div = g.bytesPerPx ? g.bytesPerPx : 1;
            g.strideWords  = static_cast<i32>(desc.pitch) / div;
            return reinterpret_cast<std::uintptr_t>(desc.pixels);
        }
        if (hr != kDDErrSurfaceLost)
            return 0;
        if (surf.Restore() != 0)
            return 0;
    }
}

// gilde.exe 0x434508 — VIBE_Render_BeginFrameLock
// switch(byte_762721):
//   case 0: if (dword_7626B0) { dword_7626F0=dword_7626B0; pitch=-dword_762710;
//                               stride=-dword_7626E0; } else fall to ppvBits; ret 1
//   case 1,3: dword_7626F0 = LockSurface(dword_62D57C); ret base!=0
//   case 2,4: dword_7626F0=ppvBits; pitch=dword_762710; stride=dword_7626E0; ret 1
//   default:  ret 0
bool BeginFrameLock(PresentGlobals& g) {
    switch (g.mode) {
        case PresentBackend::GdiBitBlt:               // case 0
            if (g.dibBase) {
                g.targetBase  = g.dibBase;
                g.pitchBytes  = -g.dibPitch;          // bottom-up DIB
                g.strideWords = -g.dibStride;
                return true;
            }
            // fall through to the ppvBits path (LABEL_4 in the original)
            g.targetBase  = g.ppvBits;
            g.pitchBytes  = g.dibPitch;
            g.strideWords = g.dibStride;
            return true;

        case PresentBackend::DDrawBlt:                // case 1
        case PresentBackend::DDrawFlip:               // case 3
            g.targetBase = g.primary ? LockSurface(*g.primary, g) : 0;
            return g.targetBase != 0;

        case PresentBackend::DDrawLockBlt:            // case 2
        case PresentBackend::DDrawLockFlip:           // case 4
            g.targetBase  = g.ppvBits;
            g.pitchBytes  = g.dibPitch;
            g.strideWords = g.dibStride;
            return true;
    }
    return false;                                     // default
}

// gilde.exe 0x4345D4 — VIBE_Render_AcquireBackBuffer
// Same shape as BeginFrameLock but the DDraw path is LockSurfaceWait(primary,0).
bool AcquireBackBuffer(PresentGlobals& g) {
    switch (g.mode) {
        case PresentBackend::GdiBitBlt:               // case 0
            if (g.dibBase) {
                g.targetBase  = g.dibBase;
                g.pitchBytes  = -g.dibPitch;
                g.strideWords = -g.dibStride;
                return true;
            }
            g.targetBase  = g.ppvBits;                // LABEL_4
            g.pitchBytes  = g.dibPitch;
            g.strideWords = g.dibStride;
            return true;

        case PresentBackend::DDrawBlt:                // case 1
        case PresentBackend::DDrawFlip:               // case 3
            g.targetBase = g.primary ? LockSurfaceWait(*g.primary, 0, g) : 0;
            return g.targetBase != 0;

        case PresentBackend::DDrawLockBlt:            // case 2
        case PresentBackend::DDrawLockFlip:           // case 4
            g.targetBase  = g.ppvBits;
            g.pitchBytes  = g.dibPitch;
            g.strideWords = g.dibStride;
            return true;
    }
    return false;
}

// gilde.exe 0x434680 — VIBE_Render_UnlockBackBuffer
// if (dword_7626F0) switch(byte_762721):
//   case 0,2,4: dword_7626F0 = 0;
//   case 1,3:   result = primary->Unlock(0); dword_7626F0 = 0;   // vtbl+128
//   default:    result = passthrough;
// returns the status byte.
i32 UnlockBackBuffer(PresentGlobals& g, i32 status) {
    if (g.targetBase == 0)
        return status;
    i32 result = status;
    switch (g.mode) {
        case PresentBackend::GdiBitBlt:               // case 0
        case PresentBackend::DDrawLockBlt:            // case 2
        case PresentBackend::DDrawLockFlip:           // case 4
            g.targetBase = 0;
            break;
        case PresentBackend::DDrawBlt:                // case 1
        case PresentBackend::DDrawFlip:               // case 3
            result = g.primary ? g.primary->Unlock() : 0;
            g.targetBase = 0;
            break;
    }
    return result;
}

// gilde.exe 0x423050 — VIBE_Surface_CopyRegionRgb
//
// Original loops v12 over [0,a1=rows), v6 over [0,a2=cols):
//   UnpackColor(src16[v6 + dword_7626F8*v12], &out[3*(v6 + a2*v12) + 0/1/2]);
// i.e. a `rows`x`cols` block, source stride dword_7626F8 PIXELS, dest packed RGB
// (3 bytes/pixel, row-contiguous). The src args a4/a5 (X0/Y0) are unused by the
// body — the original already advanced the locked base before calling. Returns the
// last `result = 3*a2` written.
//
// IMPORTANT — destination channel order. The original call is
//   UnpackColor(px, /*r=*/out+0, /*g=*/v8=out+2, /*b=*/v7=out+1)
// (UnpackColor's params are pixel, r, g, b). So the three bytes land as
//   out[0] = R,  out[1] = B,  out[2] = G
// i.e. an R,B,G byte order on disk — faithfully preserved here (it matches the
// BGR-ish layout VIBE_Bmp_Save24Bit then consumes in CaptureScreenshot).
u32 CopyRegionRgb(u32 rows, u32 cols, const u16* src16,
                  u8* dstRgb, const ColorFormat& fmt, const PresentGlobals& g) {
    u32 result = 0;
    const i32 srcStride = g.strideWords;             // dword_7626F8
    for (u32 row = 0; row < rows; ++row) {
        for (u32 col = 0; col < cols; ++col) {
            u16 px = src16[static_cast<std::size_t>(col) +
                           static_cast<std::size_t>(srcStride) * row];
            u8* o = dstRgb + 3u * (static_cast<std::size_t>(col) +
                                   static_cast<std::size_t>(cols) * row);
            // r -> o[0], g -> o[2], b -> o[1] (the call's exact pointer mapping)
            UnpackColor(fmt, px, o[0], o[2], o[1]);
        }
        result = 3u * cols;
    }
    return result;
}

// gilde.exe 0x5DE87C — VIBE_Render_CopySurfacePixels
//
// The original drives DDraw surfaces: GetSurfaceDesc(dst) to obtain an attached
// sub-surface, Lock both, StretchSurfaceDispatch the (already-locked) descs, then
// Unlock. The whole DDraw dance exists only to obtain two system-memory pixel
// blocks and hand them to the stretch dispatch; once both surfaces are locked (as
// they are here — `dstDesc`/`srcDesc` are system-memory StretchSurfaceDesc records)
// the portable effect is exactly one StretchSurfaceDispatch. We model that, which
// also covers the original's self-recursion (the recursive CopySurfacePixels call
// just re-applies the same stretch to the attached desc, a no-op once depths/sizes
// match). Returns true on success (the original's `al`).
bool CopySurfacePixels(StretchSurfaceDesc& dstDesc, const StretchSurfaceDesc& srcDesc) {
    if (!dstDesc.pixels || !srcDesc.pixels)
        return false;
    StretchSurfaceDispatch(dstDesc, srcDesc);
    return true;
}

// gilde.exe 0x42E4EC — VIBE_Render_ReportDDrawError  (portable subset)
// The original is a ~200KB switch over every DDERR_* HRESULT producing the SDK's
// "DDERR_NAME: description" string and logging it. Only DD_OK and the handful of
// codes the lock/present path raises matter for the reconstruction; the rest fall
// back to a hex rendering. No allocation — returns a pointer into static storage.
const char* ReportDDrawError(i32 hr) {
    switch (static_cast<u32>(hr)) {
        case 0x00000000u: return "";                                  // DD_OK
        case 0x887601C2u: return "DDERR_SURFACELOST";                 // -2005532222
        case 0x887601C0u: return "DDERR_GENERIC";
        case 0x88760046u: return "DDERR_NOTLOCKED";
        case 0x88760036u: return "DDERR_SURFACEBUSY";
        case 0x88760084u: return "DDERR_WASSTILLDRAWING";
        case 0x8876017Cu: return "DDERR_OUTOFVIDEOMEMORY";
        case 0x88760097u: return "DDERR_INVALIDRECT";
        case 0x88760057u: return "DDERR_INVALIDPARAMS";
        default: {
            static thread_local char buf[24];
            std::snprintf(buf, sizeof buf, "DDERR_0x%08X",
                          static_cast<unsigned>(hr));
            return buf;
        }
    }
}

} // namespace guild::render
