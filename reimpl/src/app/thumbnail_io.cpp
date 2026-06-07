#include "app/thumbnail_io.h"

#include "io/vfs.h"                       // VfsOpenFile/Read/Write/Close (REUSED)
#include "render/colorformat.h"           // UnpackColor / PackColor (REUSED)
#include "render/thumbnail_capture.h"     // ThumbnailBuffer / kThumb* (REUSED)

namespace guild::app {

// 160*120 = 19200 thumbnail pixels (render::kThumbWidth * kThumbHeight).
static constexpr int kPixels = render::kThumbWidth * render::kThumbHeight;

// gilde.exe 0x56D75C — VIBE_Save_WriteThumbnailFile
//
// Original (Hex-Rays):
//   VIBE_Window_RenderEntityList(a2);
//   VIBE_Render_CaptureScreenThumbnail(...);
//   f = VIBE_Vfs_OpenFile(a1, "wb", dword_62EB78);
//   if (f) {
//     buf = AllocDebug(0xE100, "ls:tmp");
//     for (i=0; i<120; ++i)
//       for (j=0; j<160; ++j) {            // running idx v15
//         VIBE_Render_UnpackColor(word_13CED78[idx], &buf[3*idx], &buf[3*idx+2],
//                                 &buf[3*idx+1]);   // r->[0], g->[2], b->[1]
//       }
//     VIBE_Vfs_WriteStream(buf, 0xE100, f, 1);
//     FreeDebug(buf);
//     VIBE_Vfs_CloseStream(f);
//   }
//   return f;
bool WriteThumbnailFile(const char* path, int renderArg, const ThumbnailIoHooks& hooks) {
    // 0x56d765: VIBE_Window_RenderEntityList(a2) — draw the scene into the surface.
    if (hooks.windowRenderEntityList)
        hooks.windowRenderEntityList(renderArg);
    // 0x56d76a: VIBE_Render_CaptureScreenThumbnail(...) — grab into word_13CED78.
    if (hooks.captureScreenThumbnail)
        hooks.captureScreenThumbnail();

    // 0x56d782: f = VIBE_Vfs_OpenFile(a1, "wb", dword_62EB78).
    io::VfsHandle* f = io::VfsOpenFile(path, "wb");
    if (!f)
        return false;

    // 0x56d79b: buf = AllocDebug(0xE100) — 57600 packed-RGB bytes (RAII vector).
    std::vector<u8> blob(kThumbnailFileBytes, 0);
    const u16* thumb = render::ThumbnailBuffer();

    // 0x56d7a8..0x56d7f6: 120 rows * 160 cols; UnpackColor word -> R,B,G bytes.
    for (int i = 0; i < kPixels; ++i) {
        u8* o = blob.data() + 3 * static_cast<std::size_t>(i);
        // r -> o[0], g -> o[2], b -> o[1]  (the original's exact pointer mapping).
        render::UnpackColor(hooks.fmt, thumb[i], o[0], o[2], o[1]);
    }

    // 0x56d804: VIBE_Vfs_WriteStream(buf, 0xE100, f, 1).
    io::VfsWriteStream(blob.data(), kThumbnailFileBytes, f, 1);
    // 0x56d816: FreeDebug(buf) (RAII).  0x56d822: VIBE_Vfs_CloseStream(f).
    io::VfsCloseStream(f);
    return true;
}

// gilde.exe 0x56D870 — VIBE_Save_ReadThumbnailFile
//
// Original (Hex-Rays):
//   f = VIBE_Vfs_OpenFile(a1, "rb", dword_62EB78);
//   if (f) {
//     buf = AllocDebug(0xE100, "ls:tmp");
//     VIBE_Vfs_ReadStreamBool(buf, 57600, f, 1);
//     for (i=0; i<120; ++i)
//       for (j=0; j<160; ++j)              // running idx v3/v5
//         word_13CED78[idx] = VIBE_Result_Handler_Final(buf[3*idx], buf[3*idx+1],
//                                                        buf[3*idx+2]); // r,g,b
//     FreeDebug(buf);
//     VIBE_Vfs_CloseStream(f);
//   }
//   return f;
bool ReadThumbnailFile(const char* path, const ThumbnailIoHooks& hooks) {
    // 0x56d877: f = VIBE_Vfs_OpenFile(a1, "rb", dword_62EB78).
    io::VfsHandle* f = io::VfsOpenFile(path, "rb");
    if (!f)
        return false;

    // 0x56d8a0: buf = AllocDebug(0xE100); 0x56d8b3: ReadStream(buf, 57600, f, 1).
    std::vector<u8> blob(kThumbnailFileBytes, 0);
    io::VfsReadStream(blob.data(), kThumbnailFileBytes, f, 1);

    u16* thumb = render::ThumbnailBuffer();
    // 0x56d8c8..0x56d8f4: 120 rows * 160 cols; PackColor(blob[3i],[3i+1],[3i+2]).
    // FAITHFUL QUIRK: Result_Handler_Final's params are (r, g, b); the bytes on disk
    // are (R, B, G) from the writer, so this re-packs (r=R, g=B, b=G) — the original
    // green/blue swap on round-trip is preserved exactly.
    for (int i = 0; i < kPixels; ++i) {
        const u8* o = blob.data() + 3 * static_cast<std::size_t>(i);
        thumb[i] = static_cast<u16>(render::PackColor(hooks.fmt, o[0], o[1], o[2]));
    }

    // 0x56d906: FreeDebug(buf) (RAII).  0x56d912: VIBE_Vfs_CloseStream(f).
    io::VfsCloseStream(f);
    return true;
}

} // namespace guild::app
