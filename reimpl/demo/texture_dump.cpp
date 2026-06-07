// texture_dump — load REAL textures from the shipped Textures.BIN archive through
// the reconstructed PKZIP reader + BMP decoder, then re-emit each as a standard
// 24-bit Windows BMP for visual inspection. Also dumps a loose gfx/BMP file.
//
// Pipeline exercised end-to-end on real bytes:
//   io::ZipArchive (mount + extract member) ->
//   render::BmpReadHeaderInfo / render::BmpLoadBuffer (decode 8/24-bit) ->
//   render::PictureSaveBmp24 (write a clean top-down 24-bit BMP)
//
// Build (CMake globs tests/, not demo/ — compile explicitly):
//   g++ -std=c++17 -I . -I include -I src
//     demo/texture_dump.cpp
//     src/render/bmp.cpp src/render/picture_io.cpp
//     src/io/zip_archive.cpp src/compress/inflate.cpp src/compress/crc.cpp
//     src/compress/zlib.cpp src/shim_impl/disk_filesystem.cpp
//     -o /tmp/texture_dump && /tmp/texture_dump
#include "shim_impl/disk_filesystem.h"
#include "io/zip_archive.h"
#include "render/bmp.h"
#include "render/picture_io.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace guild;

static const char* ROOT =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

// Decode a BMP byte buffer to width*height*3 RGB and write it out as a clean
// 24-bit BMP (top-down) for visual inspection.
static bool dumpBmp(const std::vector<u8>& bmp, const char* label, const char* outPath) {
    render::BmpInfo hi = render::BmpReadHeaderInfo(bmp);
    if (!hi.ok) { std::printf("  %-30s: header parse FAILED\n", label); return false; }

    int w = 0, h = 0;
    std::vector<u8> rgb = render::BmpLoadBuffer(bmp, 24, w, h, nullptr);
    if (rgb.empty() || (std::size_t)(3 * w * h) != rgb.size()) {
        std::printf("  %-30s: decode FAILED\n", label);
        return false;
    }
    std::vector<u8> out = render::PictureSaveBmp24(w, h, rgb.data());
    FILE* f = std::fopen(outPath, "wb");
    if (!f) { std::printf("  %-30s: cannot write %s\n", label, outPath); return false; }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    long nz = 0; for (u8 b : rgb) if (b) ++nz;
    std::printf("  %-30s: %dx%d bpp=%d nonzero=%ld -> %s (%zu bytes)\n",
                label, w, h, hi.bitCount, nz, outPath, out.size());
    return true;
}

int main() {
    shim::DiskFileSystem fs(ROOT);
    if (!fs.exists("Resources/Textures.BIN")) {
        std::printf("Textures.BIN not present under %s — nothing to dump.\n", ROOT);
        return 0;
    }

    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/Textures.BIN")) {
        std::printf("FAIL: ZipArchive.Open(Textures.BIN)\n");
        return 1;
    }
    std::printf("Textures.BIN mounted: %u members\n", z.numberEntry());

    struct Want { const char* member; const char* out; };
    const Want wants[] = {
        {"_DYNAMIC/2D/Landkarte2.bmp",       "/tmp/texdump_landkarte2_8bit.bmp"},
        {"_DYNAMIC/Boden/Acker.bmp",         "/tmp/texdump_acker_24bit.bmp"},
        {"_DYNAMIC/2D/Samttuch_512x512.BMP", "/tmp/texdump_samttuch_512.bmp"},
    };
    int fails = 0;
    for (const Want& wt : wants) {
        std::vector<u8> bmp;
        if (!z.ExtractByName(wt.member, bmp, false)) {
            std::printf("  %-30s: extract FAILED\n", wt.member);
            ++fails;
            continue;
        }
        if (!dumpBmp(bmp, wt.member, wt.out)) ++fails;
    }

    // A loose ground-plan BMP (non-square 24-bit) straight off disk.
    if (fs.exists("gfx/BMP/GroundPlans/Riss_Haus.BMP")) {
        shim::IFile* lf = fs.open("gfx/BMP/GroundPlans/Riss_Haus.BMP", "rb");
        if (lf) {
            std::vector<u8> bmp((std::size_t)lf->size());
            if (!bmp.empty()) lf->read(bmp.data(), bmp.size());
            fs.close(lf);
            if (!dumpBmp(bmp, "gfx/.../Riss_Haus.BMP", "/tmp/texdump_riss_haus.bmp")) ++fails;
        }
    }

    std::printf("texture_dump done (%d failures)\n", fails);
    return fails ? 1 : 0;
}
