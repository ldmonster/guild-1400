#pragma once
#include "guild/common/types.h"

namespace guild::shim { class IGraphicsDevice; }

// Frame-present dispatch for the guild::render present layer (dd_vesa.c / gfx.c).
//
//   VIBE_Render_PresentFrame  @0x4349e4
//
// The original switches on the global present-mode selector byte_762721 (0..4) and
// blits the software framebuffer ppvBits (0x7626C0) to the window via one of five
// back-ends: GDI BitBlt, DirectDraw Blt, Lock+memcpy+Blt, fullscreen Flip, or
// Lock+memcpy+Flip. All five OS paths are routed here through shim::IGraphicsDevice
// so the game code stays platform-neutral (see CONVENTIONS "OS / vendor boundary").
namespace guild::render {

// The five present modes (byte_762721 == 0..4), preserved verbatim.
enum class PresentMode : u8 {
    GdiBitBlt    = 0,  // case 0: GetDC -> CreateCompatibleDC -> BitBlt(SRCCOPY)
    DDrawBlt     = 1,  // case 1: primary->Blt(clipRect, back, ...)  (vtbl+20)
    DDrawLockBlt = 2,  // case 2: Lock back -> memcpy(ppvBits) -> Unlock -> Blt
    DDrawFlip    = 3,  // case 3: primary->Flip(0, flags)            (vtbl+44)
    DDrawLockFlip= 4,  // case 4: Lock back -> memcpy(ppvBits) -> Unlock -> Flip
};

// The host-side present operation an IGraphicsDevice must implement. Each enum maps
// 1:1 to one DDraw/GDI step the original performed, so a backend (or a test mock)
// can record the exact sequence emitted per mode.
//
//   present()          == one frame presented (the whole blit/flip happened)
//   backbuffer()       == Lock of the offscreen surface to get its pixel ptr
//
// The dispatcher copies the software framebuffer into the locked backbuffer for the
// Lock modes (2/4), mirroring the qmemcpy of ppvBits into the Lock'd surface.

// Present state — the dd_vesa globals the dispatcher reads. In the original these
// were the file-scope 0x7626xx block; gathered here so the dispatch is re-entrant.
struct PresentState {
    const void* framebuffer = nullptr; // ppvBits  (0x7626C0) software pixels
    i32  copyBytes = 0;                // dword_7626B8: bytes to copy into the lock
    i32  width = 0;                    // dword_7626E0
    i32  height = 0;                   // cy (0x7626DC)
};

// gilde.exe 0x4349e4 — VIBE_Render_PresentFrame
//   (__thiscall, returns the last DDraw/GDI HRESULT-ish status as a byte; here a
//   simple success bool). Dispatches on `mode` and drives `dev` accordingly:
//     mode 0 (GDI)        -> dev.present()
//     mode 1 (Blt)        -> dev.present()
//     mode 2 (Lock+Blt)   -> dev.backbuffer(); copy fb; dev.present()
//     mode 3 (Flip)       -> dev.present()
//     mode 4 (Lock+Flip)  -> dev.backbuffer(); copy fb; dev.present()
// Returns false only when a Lock mode could not obtain a backbuffer (the original
// falls through to the Flip path on a failed Lock in case 2; we report failure).
bool PresentFrame(shim::IGraphicsDevice& dev, PresentMode mode,
                  const PresentState& st);

} // namespace guild::render
