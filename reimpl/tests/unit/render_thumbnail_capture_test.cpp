// tests/unit/render_thumbnail_capture_test.cpp — unit coverage for the savegame
// thumbnail capture + screenshot grab (render::CaptureScreenThumbnail @0x56d48c,
// render::CaptureScreenshot @0x4ff6d4) and the app-side thumbnail file write/read
// (app::WriteThumbnailFile @0x56d75c / ReadThumbnailFile @0x56d870), exercised in
// isolation with golden colour vectors. No real assets — pure in-memory.
#include "test.h"

#include "app/thumbnail_io.h"
#include "render/colorformat.h"
#include "render/surface_present.h"
#include "render/thumbnail_capture.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// ---- minimal writable in-memory filesystem for the VFS-backed thumbnail io ----
class MemFile : public shim::IFile {
public:
    MemFile(std::vector<u8>* s, bool w) : store_(s), write_(w) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = store_->size() - pos_;
        std::size_t k = n < avail ? n : avail;
        std::memcpy(dst, store_->data() + pos_, k); pos_ += k; return k;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (pos_ + n > store_->size()) store_->resize(pos_ + n);
        std::memcpy(store_->data() + pos_, src, n); pos_ += n; return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        if (whence == SEEK_SET) pos_ = (std::size_t)off;
        else if (whence == SEEK_CUR) pos_ += (std::size_t)off;
        else pos_ = store_->size() + (std::size_t)off;
        return (std::int64_t)pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)store_->size(); }
private:
    std::vector<u8>* store_; std::size_t pos_ = 0; bool write_;
};

class MemFs : public shim::IFileSystem {
public:
    shim::IFile* open(const char* path, const char* mode) override {
        bool w = std::strchr(mode, 'w') != nullptr;
        auto& blob = files_[path];
        if (w) blob.clear();
        else if (files_.find(path) == files_.end()) return nullptr;
        return new MemFile(&blob, w);
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
    std::map<std::string, std::vector<u8>> files_;
};

} // namespace

// CaptureScreenThumbnail: a constant-colour 640x480 16bpp screen must resample to a
// 160x120 thumbnail of that same constant colour (box-average of equal pixels).
TEST(RenderThumbCapture, ConstantColourResample) {
    const int sw = 640, sh = 480;
    std::vector<u16> screen((std::size_t)sw * sh, 0);
    ColorFormat fmt = Format565();
    u16 col = (u16)PackColor(fmt, 0x40, 0x80, 0xC0);   // a 565 mid colour
    for (auto& p : screen) p = col;

    CaptureSource src;
    src.pixels = screen.data();
    src.stridePx = sw;
    src.screenWidth = sw;
    src.screenHeight = sh;
    src.fmt = fmt;
    CHECK(CaptureScreenThumbnail(src));

    const u16* thumb = ThumbnailBuffer();
    // Every thumbnail pixel must be the constant source colour.
    bool allEq = true;
    for (int i = 0; i < kThumbWidth * kThumbHeight; ++i)
        if (thumb[i] != col) { allEq = false; break; }
    CHECK(allEq);
    CHECK_EQ((int)thumb[0], (int)col);
}

// Null source => no capture (the original's DecompressState_Blob surface gate).
TEST(RenderThumbCapture, NullSourceGate) {
    CaptureSource src;     // pixels == nullptr
    CHECK(!CaptureScreenThumbnail(src));
}

// wave-12 boundary: a 1x1 source surface. With screenWidth==1 the sample step is
// tiny (1*0.003125), so every one of the 320x240 nearest-neighbour taps truncates
// to src index 0 — the resample must stay within the single-pixel source (ASAN
// clean) and the whole thumbnail becomes that one colour.
TEST(RenderThumbCapture, OnePixelSourceNoOOB) {
    ColorFormat fmt = Format565();
    u16 only = (u16)PackColor(fmt, 0x10, 0x20, 0x30);
    u16 px[1] = {only};
    CaptureSource src;
    src.pixels = px;
    src.stridePx = 1;
    src.screenWidth = 1;     // step = 1 * 0.003125 -> all taps land on index 0
    src.screenHeight = 1;
    src.fmt = fmt;
    CHECK(CaptureScreenThumbnail(src));
    const u16* thumb = ThumbnailBuffer();
    CHECK_EQ((int)thumb[0], (int)only);
    CHECK_EQ((int)thumb[kThumbWidth * kThumbHeight - 1], (int)only);
}

// wave-12 boundary: a screenshot of a 0-size target. The (w<=0||h<=0) guard must
// short-circuit BEFORE any allocation / present lock — ok==false, empty buffers.
TEST(RenderThumbCapture, ScreenshotZeroSizeGuarded) {
    PresentGlobals g;
    g.mode = PresentBackend::DDrawLockBlt;
    g.ppvBits = 0;
    int serial = 7;
    ScreenshotResult r0 = CaptureScreenshot(g, Format565(), 0, 10, &serial);
    CHECK(!r0.ok);
    CHECK_EQ((int)r0.rgb.size(), 0);
    ScreenshotResult r1 = CaptureScreenshot(g, Format565(), 10, 0, &serial);
    CHECK(!r1.ok);
    // serial still advances each call (post-increment happens before the guard).
    CHECK_EQ(serial, 9);
}

// CaptureScreenshot through the REAL present path: lock a memory surface, draw a
// gradient, grab it as 24-bit RGB, encode a BMP, and check the serial increments.
TEST(RenderThumbCapture, ScreenshotPresentPath) {
    const int w = 8, h = 4;
    std::vector<u16> fb((std::size_t)w * h, 0);
    ColorFormat fmt = Format565();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fb[y * w + x] = (u16)PackColor(fmt, (u8)(x * 30), (u8)(y * 60), 0x10);

    // Drive the present path in Lock-copy mode (mode 2): targetBase = ppvBits.
    PresentGlobals g;
    g.mode = PresentBackend::DDrawLockBlt;
    g.ppvBits = reinterpret_cast<std::uintptr_t>(fb.data());
    g.dibPitch = 2 * w;
    g.dibStride = w;
    g.bytesPerPx = 2;

    int serial = 41;
    ScreenshotResult r = CaptureScreenshot(g, fmt, w, h, &serial);
    CHECK(r.ok);
    CHECK_EQ(serial, 42);                      // post-incremented
    CHECK_EQ(r.path, std::string("gamedata/screenshots/gilde0041.bmp"));
    CHECK_EQ((int)r.rgb.size(), 3 * w * h);
    CHECK(!r.bmp.empty());
    CHECK_EQ((int)r.bmp[0], (int)'B');         // BMP magic
    CHECK_EQ((int)r.bmp[1], (int)'M');
}

// WriteThumbnailFile -> ReadThumbnailFile round-trip through the REAL VFS. The
// original SWAPS green/blue on the round-trip (write emits R,B,G; read packs r,g,b),
// so a captured pixel comes back with its g/b channels exchanged — assert exactly
// that faithful quirk.
TEST(RenderThumbCapture, FileRoundTripChannelSwap) {
    MemFs fs;
    io::VfsInit(&fs, false);

    // Seed the thumbnail buffer with a known colour, no capture hook.
    ColorFormat fmt = Format565();
    u16 col = (u16)PackColor(fmt, 0x40, 0x80, 0xC0);   // r,g,b distinct
    u16* thumb = ThumbnailBuffer();
    for (int i = 0; i < kThumbWidth * kThumbHeight; ++i) thumb[i] = col;

    guild::app::ThumbnailIoHooks hooks;
    hooks.fmt = fmt;
    CHECK(guild::app::WriteThumbnailFile("test.tmp", 0, hooks));

    // Clear, then read back.
    for (int i = 0; i < kThumbWidth * kThumbHeight; ++i) thumb[i] = 0;
    CHECK(guild::app::ReadThumbnailFile("test.tmp", hooks));

    // Decompose the original colour and the round-tripped one.
    u8 r0, g0, b0; UnpackColor(fmt, col, r0, g0, b0);
    u8 r1, g1, b1; UnpackColor(fmt, thumb[0], r1, g1, b1);
    // R survives; G and B are exchanged by the original's byte mapping.
    CHECK_EQ((int)r1, (int)r0);
    CHECK_EQ((int)g1, (int)b0);
    CHECK_EQ((int)b1, (int)g0);

    io::VfsShutdown();
}
