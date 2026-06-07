#pragma once
#include "guild/common/types.h"
#include "render/colorformat.h"
#include "render/surface_stretch.h"

// =============================================================================
// guild::render — backbuffer LOCK / UNLOCK / present-compose (dd_vesa.c / gfx.c).
//
// This is the PORTABLE half of the renderer's present path: the bookkeeping that
// brackets every frame's software draw with a DirectDraw surface Lock and Unlock,
// the RGB-readback used by screenshots, and the surface-to-surface stretch-copy
// used by the screenshot/thumbnail capture. The actual DDraw vtable calls
// (Lock / Unlock / Restore / GetSurfaceDesc) are routed through IDDrawSurface so
// the game logic stays platform-neutral (CONVENTIONS "OS / vendor boundary").
//
// FUNCTIONS RECOVERED (faithful 1:1 of the Hex-Rays pseudocode)
// -----------------------------------------------------------------------------
//   0x4343E4  VIBE_Render_LockSurface       — Lock (vtbl+100), retry-on-lost loop,
//                                              fill present globals (pitch/base/stride)
//   0x434468  VIBE_Render_LockSurfaceWait   — same, Lock flags |= 0x801 (WAIT)
//   0x434508  VIBE_Render_BeginFrameLock    — per-mode acquire of the draw target
//   0x4345D4  VIBE_Render_AcquireBackBuffer — per-mode acquire (LockSurfaceWait path)
//   0x434680  VIBE_Render_UnlockBackBuffer  — per-mode Unlock (vtbl+128)
//   0x423050  VIBE_Surface_CopyRegionRgb    — 16bpp region -> packed 24-bit RGB out
//   0x5DE87C  VIBE_Render_CopySurfacePixels — GetSurfaceDesc+Lock src/dst, stretch,
//                                              Unlock (recursive on the dst desc)
//   0x42E4EC  VIBE_Render_ReportDDrawError  — DDraw HRESULT -> message (portable stub)
//
// THE PRESENT-STATE GLOBAL BLOCK (gilde.exe 0x7626xx; file-scope in dd_vesa.c)
// -----------------------------------------------------------------------------
// These were process-wide globals; gathered into one struct so the lock/unlock is
// re-entrant and testable. One instance (g_present) owns them for the whole engine.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// DirectDraw surface boundary. The original called IDirectDrawSurface methods by
// vtable byte offset; modelled here as named virtuals so a memory mock can stand
// in for the driver. The slot offset each maps to is documented per method.
// ---------------------------------------------------------------------------
struct DDrawLock {
    void* pixels = nullptr;  // DDSURFACEDESC.lpSurface  (+0x24 in desc)
    u32   pitch  = 0;        // DDSURFACEDESC.lPitch     (+0x10 in desc, BYTES/row)
    u32   bitDepth = 0;      // DDSURFACEDESC.ddpfPixelFormat.dwRGBBitCount (+0x54)
};

// DDERR_SURFACELOST as the original compared it (0x887601C2 == -2005532222).
constexpr i32 kDDErrSurfaceLost = -2005532222;

struct IDDrawSurface {
    virtual ~IDDrawSurface() = default;
    // vtbl+0x30 (slot 12) IDirectDrawSurface::GetSurfaceDesc. Fills `out` with the
    // surface's pixel base / pitch / bit-depth. Returns 0 (DD_OK) on success.
    virtual i32 GetSurfaceDesc(DDrawLock& out) = 0;
    // vtbl+0x64 (slot 25) IDirectDrawSurface::Lock. `flags` mirrors the original's
    // dwFlags (LockSurfaceWait passes 0x801 = DDLOCK_WAIT|DDLOCK_SURFACEMEMORYPTR).
    // Returns 0 (DD_OK) on success; kDDErrSurfaceLost requests a Restore+retry.
    virtual i32 Lock(u32 flags, DDrawLock& out) = 0;
    // vtbl+0x80 (slot 32) IDirectDrawSurface::Unlock. Returns 0 (DD_OK) on success.
    virtual i32 Unlock() = 0;
    // vtbl+0x6C (slot 27) IDirectDrawSurface::Restore. Returns 0 on success; the
    // lock loop retries while Restore keeps succeeding (== 0).
    virtual i32 Restore() = 0;
};

// ---------------------------------------------------------------------------
// The five present modes — gilde.exe global byte_762721 (0..4). Same numbering as
// render/present.h PresentMode (kept independent so each module compiles alone).
// ---------------------------------------------------------------------------
enum class PresentBackend : u8 {
    GdiBitBlt    = 0,  // GDI: framebuffer is the DIB (ppvBits / dword_7626B0)
    DDrawBlt     = 1,  // DDraw Blt: draw straight into the Lock'd primary
    DDrawLockBlt = 2,  // Lock+Blt: draw into ppvBits, copy at present
    DDrawFlip    = 3,  // DDraw Flip: draw into the Lock'd back buffer
    DDrawLockFlip= 4,  // Lock+Flip: draw into ppvBits, copy at present
};

// The present-state block (gilde.exe 0x7626xx). Field comments give the original
// global address. A lock fills base/pitchBytes/strideWords; the per-mode acquire
// chooses the DIB (ppvBits) vs. the Lock'd primary (surface).
//
// In the 32-bit original these were all `dword`s — the pixel-base fields held a
// pointer in a dword. On the 64-bit reconstruction those bases are kept as real
// pointers (`uptr` = uintptr_t) so a Lock'd surface address round-trips; the
// integer pitch/stride fields keep their exact `i32` semantics (the strideWords
// can go NEGATIVE for the bottom-up GDI DIB, exactly as the original negated it).
struct PresentGlobals {
    PresentBackend mode = PresentBackend::DDrawLockBlt; // byte_762721 selector

    // Where the renderer is currently drawing (set by the Begin/Acquire).
    std::uintptr_t targetBase   = 0;  // dword_7626F0  (current draw base; 0 = not locked)
    i32   pitchBytes   = 0;  // dword_7626FC  (current row pitch, BYTES; signed)
    i32   strideWords  = 0;  // dword_7626F8  (current row stride, PIXELS; signed)
    i32   pitchExtra   = 0;  // dword_7626F4  (lock: pitch>>3, used by 8bpp paths)
    i32   lockPitch    = 0;  // dword_7626EC  (lock: raw lPitch)
    i32   lockBitDepth = 0;  // dword_762714  (lock: dwRGBBitCount)

    // The DIB / software framebuffer the engine composes into for the GDI / Lock
    // modes (ppvBits 0x7626C0). dibBase != 0 selects the alternate GDI buffer.
    std::uintptr_t ppvBits      = 0;  // dword_7626C0  (software DIB pixels)
    std::uintptr_t dibBase      = 0;  // dword_7626B0  (GDI alternate base; 0 => use ppvBits)
    i32   dibPitch     = 0;  // dword_762710  (DIB pitch, BYTES)
    i32   dibStride    = 0;  // dword_7626E0  (DIB stride, PIXELS == screen width)
    i32   screenHeight = 0;  // dword_7626DC  (cy; framebuffer height, PIXELS)
    i32   bytesPerPx   = 1;  // dword_7626E8  (lock stride divisor; 1/2/3/4)

    i32   lastDDrawStatus = 0; // dword_7626C8 (last Lock/Unlock HRESULT)

    // The DDraw primary/back surface for the Lock/Flip modes (dword_62D57C). May be
    // null in the GDI/Lock-copy modes (which use ppvBits and never touch DDraw).
    IDDrawSurface* primary = nullptr;
};

// gilde.exe 0x4343E4 — VIBE_Render_LockSurface  (eax = surf, ecx = unused)
//   Lock `surf` (vtbl+100, flags=1=DDLOCK_SURFACEMEMORYPTR). On DDERR_SURFACELOST
//   it Restores (vtbl+108) and retries; any other error returns 0. On success it
//   records the lock pitch/depth/stride into `g` and returns the pixel base
//   (lpSurface). 0 on failure. (Original returned the base in a 32-bit dword.)
std::uintptr_t LockSurface(IDDrawSurface& surf, PresentGlobals& g);

// gilde.exe 0x434468 — VIBE_Render_LockSurfaceWait  (eax = surf, edx = flags)
//   As LockSurface but ORs the WAIT bits (flags |= 0x801) into the Lock dwFlags.
std::uintptr_t LockSurfaceWait(IDDrawSurface& surf, u32 flags, PresentGlobals& g);

// gilde.exe 0x434508 — VIBE_Render_BeginFrameLock  (__thiscall)
//   Acquire the current draw target for the active mode:
//     0 (GDI)   : dibBase set => draw into it with NEGATED pitch/stride (bottom-up
//                 DIB); else fall through to the ppvBits path.
//     1,3 (Blt/Flip): LockSurface(primary) -> base; success = base != 0.
//     2,4 (Lock*)   : draw into ppvBits with the DIB pitch/stride.
//   Returns true when a draw target was obtained.
bool BeginFrameLock(PresentGlobals& g);

// gilde.exe 0x4345D4 — VIBE_Render_AcquireBackBuffer
//   Same dispatch as BeginFrameLock but modes 1/3 use LockSurfaceWait(primary,0).
bool AcquireBackBuffer(PresentGlobals& g);

// gilde.exe 0x434680 — VIBE_Render_UnlockBackBuffer  (eax = passthrough status)
//   If a lock is held (targetBase != 0): modes 0/2/4 just clear it; modes 1/3
//   Unlock the primary (vtbl+128) first. `status` is the eax passthrough.
i32 UnlockBackBuffer(PresentGlobals& g, i32 status = 0);

// gilde.exe 0x423050 — VIBE_Surface_CopyRegionRgb
//   (ecx = rows, ebx = cols, src16, srcX0(unused), srcY0(unused), dstRgb)
//   Read a `rows`x`cols` region of a 16bpp surface and write it as packed 24-bit
//   RGB (3 bytes/pixel, top-down) into `dstRgb`. The source row stride in PIXELS
//   is g.strideWords (dword_7626F8, set by the lock). Channel unpack uses `fmt`.
//   Returns the byte width of the last row written (3*cols), as the original did.
u32 CopyRegionRgb(u32 rows, u32 cols, const u16* src16,
                  u8* dstRgb, const ColorFormat& fmt, const PresentGlobals& g);

// gilde.exe 0x5DE87C — VIBE_Render_CopySurfacePixels  (al = ok, eax = dst, edx = srcDesc)
//   Copy/stretch one DDraw surface's pixels into another. The original:
//     - GetSurfaceDesc(dst) (vtbl+48) -> a *sub-surface* handle in `attached`.
//     - if no src desc given, zero-init one, Lock(dst) (vtbl+100) to fill it.
//     - zero a desc, Lock(attached), StretchSurfaceDispatch(attached, src),
//       recurse CopySurfacePixels(attached, attached-desc), Unlock(attached),
//       and Unlock(dst) if we Lock'd it.
//   Here the portable effect is modelled on StretchSurfaceDesc: stretch `srcDesc`
//   into `dstDesc` (both already locked / system-memory). Returns true on success.
bool CopySurfacePixels(StretchSurfaceDesc& dstDesc, const StretchSurfaceDesc& srcDesc);

// gilde.exe 0x42E4EC — VIBE_Render_ReportDDrawError
//   The original is a ~200KB switch mapping every DDERR_* HRESULT to its symbolic
//   name + human description and logging it. That is pure OS-error reporting (the
//   string table is the DirectDraw SDK's); here we return the symbolic name for the
//   handful of codes the lock/present path can produce, "" for DD_OK, and a hex
//   fallback for the rest. Returns a stable C string (no allocation).
const char* ReportDDrawError(i32 hr);

} // namespace guild::render
