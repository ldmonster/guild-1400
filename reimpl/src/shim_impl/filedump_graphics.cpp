#include "shim_impl/filedump_graphics.h"

#include <cstdio>
#include <cstring>

namespace guild::shim {

namespace {

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 24));
}
std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                      (static_cast<std::uint32_t>(p[3]) << 24));
}

} // namespace

void FileDumpGraphicsDevice::configureDump(const std::string& dir,
                                           const std::string& prefix, int formats) {
    dir_ = dir;
    prefix_ = prefix;
    formats_ = formats;
    configured_ = true;
}

bool FileDumpGraphicsDevice::init(int width, int height, int bpp, bool fullscreen) {
    return mem_.init(width, height, bpp, fullscreen);
}

void FileDumpGraphicsDevice::shutdown() {
    mem_.shutdown();
}

Surface* FileDumpGraphicsDevice::backbuffer() {
    return mem_.backbuffer();
}

void FileDumpGraphicsDevice::setPalette(const std::uint32_t* argb256) {
    mem_.setPalette(argb256);
}

// Build the 24-bit RGB (top-down) view of the current snapshot. The
// MemoryGraphicsDevice has not yet copied the live framebuffer into its
// "presented" buffer when we want to read it during present(); to be robust
// either before or after the inner present(), we read whichever is populated.
RgbImage FileDumpGraphicsDevice::snapshotRgb() const {
    RgbImage img;
    // We need the surface geometry; backbuffer() exposes it. mem_ is const here,
    // so reconstruct geometry from the (non-const-safe) snapshot + a const cast
    // is avoided by using the public accessors via a local copy of the surface.
    const Surface* s = const_cast<MemoryGraphicsDevice&>(mem_).backbuffer();
    if (!s || !s->pixels)
        return img;

    const std::vector<std::uint8_t>& snap = mem_.lastPresented();
    const std::uint8_t* px = nullptr;
    if (!snap.empty())
        px = snap.data();
    else
        px = static_cast<const std::uint8_t*>(s->pixels);

    const int w = s->width, h = s->height, bpp = s->bpp, pitch = s->pitch;
    img.width = w;
    img.height = h;
    img.rgb.assign(static_cast<std::size_t>(w) * h * 3, 0);

    const std::uint32_t* pal = mem_.palette();
    // If 8bpp and no palette was ever set (all entries zero), expand indices as a
    // grayscale ramp so frames remain visible; otherwise honour the palette.
    bool paletteSet = false;
    if (bpp == 8) {
        for (int i = 0; i < 256; ++i)
            if (pal[i] != 0) { paletteSet = true; break; }
    }
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = px + static_cast<std::size_t>(y) * pitch;
        std::uint8_t* dst = &img.rgb[(static_cast<std::size_t>(y) * w) * 3];
        for (int x = 0; x < w; ++x) {
            std::uint8_t r = 0, g = 0, b = 0;
            if (bpp == 8) {
                std::uint8_t idx = row[x];
                if (!paletteSet) {
                    r = g = b = idx; // grayscale ramp
                } else {
                    std::uint32_t argb = pal[idx]; // 0xAARRGGBB
                    r = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
                    g = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
                    b = static_cast<std::uint8_t>(argb & 0xFF);
                }
            } else if (bpp == 16) {
                std::uint16_t v = rd16(row + x * 2);
                std::uint8_t r5 = (v >> 11) & 0x1F;
                std::uint8_t g6 = (v >> 5) & 0x3F;
                std::uint8_t b5 = v & 0x1F;
                r = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
                g = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
                b = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
            } else { // 32bpp 0xAARRGGBB
                const std::uint8_t* p = row + x * 4;
                b = p[0];
                g = p[1];
                r = p[2];
            }
            dst[x * 3 + 0] = r;
            dst[x * 3 + 1] = g;
            dst[x * 3 + 2] = b;
        }
    }
    img.ok = true;
    return img;
}

std::string FileDumpGraphicsDevice::framePath(int frame, Format fmt) const {
    char num[16];
    std::snprintf(num, sizeof(num), "%04d", frame);
    std::string path = dir_;
    if (!path.empty() && path.back() != '/')
        path += '/';
    path += prefix_;
    path += num;
    path += (fmt == kBmp) ? ".bmp" : ".ppm";
    return path;
}

bool FileDumpGraphicsDevice::dumpToFile(const std::string& path,
                                        const std::vector<std::uint8_t>& bytes) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = bytes.empty() ||
              std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

void FileDumpGraphicsDevice::present() {
    // Index of the frame being written (presentCount before the inner present()).
    const int frame = mem_.presentCount();
    // Snapshot the live framebuffer into mem_.lastPresented() and bump the count.
    mem_.present();

    if (!configured_)
        return;

    RgbImage img = snapshotRgb();
    if (!img.ok)
        return;

    if (formats_ & kBmp)
        dumpToFile(framePath(frame, kBmp),
                   EncodeBmp24(img.width, img.height, img.rgb.data()));
    if (formats_ & kPpm)
        dumpToFile(framePath(frame, kPpm),
                   EncodePpm(img.width, img.height, img.rgb.data()));
}

// ---- standard 24-bit BMP (BI_RGB, bottom-up, DWORD-padded rows) ----
std::vector<std::uint8_t> FileDumpGraphicsDevice::EncodeBmp24(int w, int h,
                                                              const std::uint8_t* rgb) {
    std::vector<std::uint8_t> out;
    const int rowBytes = w * 3;
    const int padded = (rowBytes + 3) & ~3;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(padded) * h;
    const std::uint32_t fileSize = 54 + dataSize;

    put16(out, 0x4D42);     // 'BM'
    put32(out, fileSize);
    put16(out, 0);
    put16(out, 0);
    put32(out, 54);         // pixel data offset

    put32(out, 40);         // info header size
    put32(out, static_cast<std::uint32_t>(w));
    put32(out, static_cast<std::uint32_t>(h));
    put16(out, 1);          // planes
    put16(out, 24);         // bpp
    put32(out, 0);          // BI_RGB
    put32(out, dataSize);
    put32(out, 2835);       // ~72 DPI
    put32(out, 2835);
    put32(out, 0);
    put32(out, 0);

    // bottom-up rows; on-disk B,G,R; pad each row to 4 bytes.
    const std::uint8_t zero[3] = {0, 0, 0};
    for (int y = h - 1; y >= 0; --y) {
        const std::uint8_t* src = rgb + static_cast<std::size_t>(y) * rowBytes;
        for (int x = 0; x < w; ++x) {
            out.push_back(src[x * 3 + 2]); // B
            out.push_back(src[x * 3 + 1]); // G
            out.push_back(src[x * 3 + 0]); // R
        }
        out.insert(out.end(), zero, zero + (padded - rowBytes));
    }
    return out;
}

// ---- binary P6 PPM (top-down, R,G,B) ----
std::vector<std::uint8_t> FileDumpGraphicsDevice::EncodePpm(int w, int h,
                                                            const std::uint8_t* rgb) {
    char hdr[64];
    int n = std::snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", w, h);
    std::vector<std::uint8_t> out(hdr, hdr + n);
    out.insert(out.end(), rgb, rgb + static_cast<std::size_t>(w) * h * 3);
    return out;
}

RgbImage FileDumpGraphicsDevice::DecodeBmp24(const std::vector<std::uint8_t>& file) {
    RgbImage img;
    if (file.size() < 54 || file[0] != 'B' || file[1] != 'M')
        return img;
    std::uint32_t dataOff = rd32(file.data() + 10);
    std::int32_t w = static_cast<std::int32_t>(rd32(file.data() + 18));
    std::int32_t h = static_cast<std::int32_t>(rd32(file.data() + 22));
    std::uint16_t bpp = rd16(file.data() + 28);
    std::uint32_t comp = rd32(file.data() + 30);
    if (bpp != 24 || comp != 0 || w <= 0)
        return img;
    bool bottomUp = h > 0;
    int ah = h < 0 ? -h : h;
    const int rowBytes = w * 3;
    const int padded = (rowBytes + 3) & ~3;
    if (file.size() < dataOff + static_cast<std::size_t>(padded) * ah)
        return img;

    img.width = w;
    img.height = ah;
    img.rgb.assign(static_cast<std::size_t>(w) * ah * 3, 0);
    for (int r = 0; r < ah; ++r) {
        const std::uint8_t* src = file.data() + dataOff + static_cast<std::size_t>(r) * padded;
        int outRow = bottomUp ? (ah - 1 - r) : r;
        std::uint8_t* dst = &img.rgb[(static_cast<std::size_t>(outRow) * w) * 3];
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 3 + 2]; // R
            dst[x * 3 + 1] = src[x * 3 + 1]; // G
            dst[x * 3 + 2] = src[x * 3 + 0]; // B
        }
    }
    img.ok = true;
    return img;
}

RgbImage FileDumpGraphicsDevice::DecodePpm(const std::vector<std::uint8_t>& file) {
    RgbImage img;
    if (file.size() < 2 || file[0] != 'P' || file[1] != '6')
        return img;
    // Parse three whitespace-separated integers (width, height, maxval) after P6,
    // honouring '#' comment lines.
    std::size_t i = 2;
    auto skipws = [&]() {
        while (i < file.size()) {
            if (file[i] == '#') {
                while (i < file.size() && file[i] != '\n') ++i;
            } else if (file[i] == ' ' || file[i] == '\t' ||
                       file[i] == '\n' || file[i] == '\r') {
                ++i;
            } else {
                break;
            }
        }
    };
    auto readInt = [&](int& out) -> bool {
        skipws();
        if (i >= file.size() || file[i] < '0' || file[i] > '9') return false;
        long v = 0;
        while (i < file.size() && file[i] >= '0' && file[i] <= '9')
            v = v * 10 + (file[i++] - '0');
        out = static_cast<int>(v);
        return true;
    };
    int w = 0, h = 0, maxv = 0;
    if (!readInt(w) || !readInt(h) || !readInt(maxv)) return img;
    if (w <= 0 || h <= 0 || maxv != 255) return img;
    // Exactly one whitespace byte separates the header from binary data.
    ++i;
    if (file.size() < i + static_cast<std::size_t>(w) * h * 3) return img;
    img.width = w;
    img.height = h;
    img.rgb.assign(file.begin() + i, file.begin() + i + static_cast<std::size_t>(w) * h * 3);
    img.ok = true;
    return img;
}

} // namespace guild::shim
