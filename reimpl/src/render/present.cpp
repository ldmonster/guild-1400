#include "render/present.h"

#include "shim/IGraphicsDevice.h"

#include <cstring>

namespace guild::render {

// gilde.exe 0x4349e4 — VIBE_Render_PresentFrame
//
// The original's five cases differ only in how the framebuffer reaches the screen.
// Mapped onto IGraphicsDevice:
//   - GDI BitBlt (0) and DDraw Blt (1) blit the existing offscreen surface, so they
//     are a single present() (the backend already owns the blit-from-DIB / Blt).
//   - Flip (3) is a page flip: present() again (no framebuffer copy needed).
//   - Lock modes (2,4) Lock the offscreen DD surface, qmemcpy ppvBits into it,
//     Unlock, then Blt (2) / Flip (4). Here: backbuffer() returns the locked
//     surface; we memcpy the software framebuffer into its pixels, then present().
//
// `copyBytes` mirrors dword_7626B8 (the exact byte count the original qmemcpy'd:
// the aligned head + 4*words + tail decomposition collapses to a plain memcpy).
bool PresentFrame(shim::IGraphicsDevice& dev, PresentMode mode,
                  const PresentState& st) {
    switch (mode) {
        case PresentMode::GdiBitBlt:   // case 0
        case PresentMode::DDrawBlt:    // case 1
        case PresentMode::DDrawFlip:   // case 3
            dev.present();
            return true;

        case PresentMode::DDrawLockBlt:   // case 2
        case PresentMode::DDrawLockFlip:  // case 4
        {
            shim::Surface* bb = dev.backbuffer(); // VIBE_Render_LockSurface
            if (!bb || !bb->pixels)
                return false;                     // Lock failed
            if (st.framebuffer && st.copyBytes > 0) {
                std::memcpy(bb->pixels, st.framebuffer,
                            static_cast<std::size_t>(st.copyBytes));
            }
            dev.present();                        // Unlock + Blt/Flip
            return true;
        }
    }
    return true; // default: status passthrough
}

} // namespace guild::render
