#pragma once
// =============================================================================
// guild::app — savegame-thumbnail file write/read (gilde.exe, save UI coupling).
//
// The two leaves the save spine calls to persist the 160x120 preview that sits
// alongside a .SAV (a SEPARATE "<name>.tmp"-style sidecar, distinct from the
// thumbnail blob embedded in the .SAV header by io::WriteScenarioBlock):
//
//   0x56D75C  VIBE_Save_WriteThumbnailFile — render the entity list, capture the
//             screen thumbnail (render::CaptureScreenThumbnail), then UnpackColor
//             the 160x120 16bpp buffer (word_13CED78) to a 57600-byte packed RGB
//             blob and VfsWriteStream it to `path`.
//   0x56D870  VIBE_Save_ReadThumbnailFile  — VfsReadStream the 57600-byte RGB blob
//             and PackColor it back into word_13CED78.
//
// Both go through the REAL io VFS siblings (io::VfsOpenFile / VfsWriteStream /
// VfsReadStream / VfsCloseStream) and the REAL render colour (un)pack + capture —
// no mock file/render leaves. The one GUI leaf (Window_RenderEntityList, which
// draws the world into the working surface just before the grab) is routed through
// a hook so a host wires it and a test records it.
//
// FAITHFUL QUIRK: the write emits disk bytes (R, B, G) per pixel (UnpackColor maps
// r->[0], g->[2], b->[1]); the read interprets them as (r=[0], g=[1], b=[2]) when
// re-packing. The round-trip therefore SWAPS the green/blue channels — this is the
// original's behaviour and is preserved verbatim (see thumbnail_io.cpp).
// =============================================================================
#include "guild/common/types.h"
#include "render/colorformat.h"

#include <cstdint>
#include <vector>

namespace guild::app {

// 0xE100 — the on-disk thumbnail blob size (160*120*3 packed RGB bytes).
constexpr std::uint32_t kThumbnailFileBytes = 0xE100;  // 57600

// The capture/draw leaves WriteThumbnailFile invokes before reading word_13CED78.
struct ThumbnailIoHooks {
    // gilde.exe 0x4134F0 — VIBE_Window_RenderEntityList(a2). Draws the world's
    // entity list into the working surface so the grab sees the current scene.
    // Routed through a hook (GUI leaf); a host wires the real draw, a test records.
    void (*windowRenderEntityList)(int arg) = nullptr;
    // gilde.exe 0x56D48C — VIBE_Render_CaptureScreenThumbnail. Resamples the screen
    // into the global thumbnail buffer (word_13CED78). The default (null) wiring
    // leaves the buffer as-is so a test can seed it; a host wires the real capture.
    bool (*captureScreenThumbnail)() = nullptr;
    // The native pixel format used to (un)pack the 16bpp thumbnail words. Defaults
    // to RGB565 (the static-image present format).
    render::ColorFormat fmt{5, 2, 3, 0, 11, 3};
};

// gilde.exe 0x56D75C — VIBE_Save_WriteThumbnailFile (path@edx, renderArg@eax).
//   Window_RenderEntityList(renderArg); CaptureScreenThumbnail(); open(path,"wb");
//   for 120*160 pixels: UnpackColor(word_13CED78[i]) -> blob[3i]=R,[3i+1]=B,[3i+2]=G;
//   VfsWriteStream(blob, 0xE100); close. Returns true if the file was written.
bool WriteThumbnailFile(const char* path, int renderArg, const ThumbnailIoHooks& hooks);

// gilde.exe 0x56D870 — VIBE_Save_ReadThumbnailFile (path@eax).
//   open(path,"rb"); VfsReadStream(blob, 0xE100); for 120*160 pixels:
//   word_13CED78[i] = PackColor(blob[3i], blob[3i+1], blob[3i+2]); close.
//   Returns true if the file was read.
bool ReadThumbnailFile(const char* path, const ThumbnailIoHooks& hooks);

} // namespace guild::app
