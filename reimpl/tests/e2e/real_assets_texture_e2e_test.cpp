// End-to-end validation of the reconstructed BMP / texture loaders against the
// REAL shipped game assets:
//   * europe_guild_1400_original/Resources/Textures.BIN  — a PKZIP archive of
//     ~2370 texture BMPs (8-bit paletted + 24-bit, all bottom-up, square).
//   * europe_guild_1400_original/gfx/BMP/{GroundPlans,Maps}/*.bmp — loose BMP
//     files (non-square 24-bit ground plans + large 1280x1280 24-bit city maps).
//
// Drives: io::ZipArchive (mount/list/extract real members) -> render::BmpReadHeaderInfo
// / render::BmpLoadBuffer (decode real BMP variants) -> render::TextureAssetCache::
// LoadByName (the VIBE_Texture_LoadByName software path) through a mock VFS that
// serves a real extracted square texture. Also exercises the synthetic-only code
// paths the shipped files never hit (BI_RLE8 run-length + top-down rows).
//
// GUARDED: if the asset folder is absent every test passes trivially so the suite
// stays green on machines without the game data.
#include "test.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "io/zip_archive.h"
#include "io/vfs.h"
#include "render/bmp.h"
#include "render/texture.h"
#include "render/texture_asset.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/Textures.BIN");
}

// Extract one member of Textures.BIN by name into `out`. Returns false if absent.
static bool extractTexture(const char* member, std::vector<u8>& out) {
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/Textures.BIN"))
        return false;
    return z.ExtractByName(member, out, false);
}

// ---------------------------------------------------------------------------
// 1. Mount Textures.BIN with the reconstructed ZipArchive; count members and
//    confirm the member inventory (BMP leaves under directory-entry prefixes).
// ---------------------------------------------------------------------------
TEST(RealTexture, TexturesBinArchiveInventory) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Textures.BIN"));
    CHECK_EQ(z.numberEntry(), 2422u);   // PKZIP central-dir total (dir + BMP leaves)

    int total = 0, dirs = 0, bmps = 0, other = 0;
    char nm[512];
    io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        ++total;
        std::size_t len = std::strlen(nm);
        if (len && nm[len - 1] == '/') { ++dirs; continue; }
        // case-insensitive ".BMP" suffix test
        if (len >= 4 &&
            (nm[len - 4] == '.') &&
            (nm[len - 3] == 'B' || nm[len - 3] == 'b') &&
            (nm[len - 2] == 'M' || nm[len - 2] == 'm') &&
            (nm[len - 1] == 'P' || nm[len - 1] == 'p'))
            ++bmps;
        else
            ++other;
    }
    CHECK_EQ(total, 2422);
    CHECK_EQ(dirs, 52);     // 52 directory entries
    CHECK_EQ(bmps, 2370);   // 2370 BMP texture leaves
    CHECK_EQ(other, 0);
    std::printf("[RealTexture] Textures.BIN: %d members (%d dirs, %d BMPs)\n",
                total, dirs, bmps);
}

// ---------------------------------------------------------------------------
// 2. Extract real BMP members and decode them with BmpReadHeaderInfo +
//    BmpLoadBuffer (both 8-bit-index and 24-bit-RGB output paths). Verifies sane
//    width/height/bpp and that 8-bit indices map through the source palette to
//    exactly the 24-bit RGB output.
// ---------------------------------------------------------------------------
namespace {
struct DecodeReport { bool decoded; int w, h, bpp; long nonzero; bool idxMatches; };

DecodeReport decodeAndCheck(const std::vector<u8>& bmp) {
    DecodeReport rep{false, 0, 0, 0, 0, true};
    render::BmpInfo hi = render::BmpReadHeaderInfo(bmp);
    if (!hi.ok) return rep;
    rep.bpp = hi.bitCount;

    int w = 0, h = 0;
    u8 pal[256 * 3];
    std::vector<u8> idx = render::BmpLoadBuffer(bmp, 8, w, h, pal);
    std::vector<u8> rgb = render::BmpLoadBuffer(bmp, 24, w, h, nullptr);
    if (rgb.empty()) return rep;
    rep.decoded = true;
    rep.w = w; rep.h = h;
    if ((std::size_t)(3 * w * h) != rgb.size()) rep.decoded = false;

    for (u8 b : rgb) if (b) ++rep.nonzero;

    // For 8-bit sources: every 24-bit pixel must equal palette[index].
    if (!idx.empty()) {
        for (int k = 0; k < w * h; ++k) {
            u8 ix = idx[k];
            if (rgb[3 * k + 0] != pal[3 * ix + 0] ||
                rgb[3 * k + 1] != pal[3 * ix + 1] ||
                rgb[3 * k + 2] != pal[3 * ix + 2]) {
                rep.idxMatches = false;
                break;
            }
        }
    }
    return rep;
}
} // namespace

TEST(RealTexture, DecodeRealBmpMembers) {
    if (!assetsPresent()) { CHECK(true); return; }

    // A representative spread: 8-bit (full + reduced palette), 24-bit, big square.
    struct Want { const char* member; int w, h, bpp; };
    const Want wants[] = {
        {"_DYNAMIC/2D/Landkarte2.bmp",       256, 256, 8},   // 8-bit, 256-entry palette
        {"_DYNAMIC/2D/BLACK.BMP",            128, 128, 24},  // 24-bit
        {"_DYNAMIC/Boden/Acker.bmp",          64,  64, 24},  // 24-bit terrain texel
        {"_DYNAMIC/2D/Samttuch_512x512.BMP", 512, 512, 24},  // big 24-bit
    };
    for (const Want& wt : wants) {
        std::vector<u8> bmp;
        bool got = extractTexture(wt.member, bmp);
        CHECK(got);
        if (!got) continue;
        DecodeReport r = decodeAndCheck(bmp);
        CHECK(r.decoded);
        CHECK_EQ(r.w, wt.w);
        CHECK_EQ(r.h, wt.h);
        CHECK_EQ(r.bpp, wt.bpp);
        CHECK(r.nonzero > 0);          // real image content, not all-black
        CHECK(r.idxMatches);           // 8-bit index->RGB consistency
        std::printf("[RealTexture] %-34s decoded %dx%d bpp=%d nonzero=%ld idxOK=%d\n",
                    wt.member, r.w, r.h, r.bpp, r.nonzero, (int)r.idxMatches);
    }
}

// Find a real 8-bit member whose palette has FEWER than 256 entries (clrUsed),
// exercising the variable data-offset path, and confirm it decodes.
TEST(RealTexture, DecodeReducedPaletteMember) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Textures.BIN"));

    char nm[512];
    io::ZipFileInfo fi;
    bool found = false;
    for (int r = z.GoToFirstFile(); r == io::kZipOk && !found; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        std::size_t len = std::strlen(nm);
        if (len < 4 || nm[len - 1] == '/') continue;
        std::vector<u8> bmp;
        if (!z.ExtractByName(nm, bmp, false)) continue;
        if (bmp.size() < 0x36) continue;
        // 8-bit + clrUsed in (0,256) -> data offset != 0x36+1024
        u16 bpp = (u16)(bmp[0x1C] | (bmp[0x1D] << 8));
        u32 clrUsed = (u32)(bmp[0x2E] | (bmp[0x2F] << 8) |
                            (bmp[0x30] << 16) | (bmp[0x31] << 24));
        if (bpp != 8 || clrUsed == 0 || clrUsed >= 256) continue;
        DecodeReport rep = decodeAndCheck(bmp);
        CHECK(rep.decoded);
        CHECK(rep.idxMatches);
        CHECK(rep.nonzero > 0);
        std::printf("[RealTexture] reduced-palette %s clrUsed=%u %dx%d decoded\n",
                    nm, clrUsed, rep.w, rep.h);
        found = true;
    }
    CHECK(found);
}

// ---------------------------------------------------------------------------
// 3. Loose gfx/BMP files: a NON-SQUARE 24-bit ground plan (104x110) and a large
//    1280x1280 24-bit city map. Read straight off disk, decode via BmpLoadBuffer.
// ---------------------------------------------------------------------------
namespace {
bool slurpDisk(const char* rel, std::vector<u8>& out) {
    shim::DiskFileSystem fs(kRoot);
    shim::IFile* f = fs.open(rel, "rb");
    if (!f) return false;
    std::int64_t n = f->size();
    out.resize(n > 0 ? (std::size_t)n : 0);
    if (n > 0) f->read(out.data(), (std::size_t)n);
    fs.close(f);
    return true;
}
} // namespace

TEST(RealTexture, DecodeLooseGfxBmps) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    if (!fs.exists("gfx/BMP/GroundPlans/Riss_Haus.BMP")) { CHECK(true); return; }

    struct Want { const char* path; int w, h, bpp; };
    const Want wants[] = {
        {"gfx/BMP/GroundPlans/Riss_Haus.BMP", 104, 110, 24},  // NON-SQUARE 24-bit
        {"gfx/BMP/Maps/Augsburg.bmp",        1280, 1280, 24},  // large 24-bit map
    };
    for (const Want& wt : wants) {
        std::vector<u8> bmp;
        bool got = slurpDisk(wt.path, bmp);
        CHECK(got);
        if (!got) continue;
        DecodeReport r = decodeAndCheck(bmp);
        CHECK(r.decoded);
        CHECK_EQ(r.w, wt.w);
        CHECK_EQ(r.h, wt.h);
        CHECK_EQ(r.bpp, wt.bpp);
        CHECK(r.nonzero > 0);
        std::printf("[RealTexture] loose %-38s decoded %dx%d bpp=%d nonzero=%ld\n",
                    wt.path, r.w, r.h, r.bpp, r.nonzero);
    }
}

// ---------------------------------------------------------------------------
// 4. The TEXTURE path: VIBE_Texture_LoadByName software branch. Extract a real
//    square 8-bit texture from Textures.BIN, mount it in a mock VFS, and drive
//    TextureAssetCache::LoadByName -> a Texture record whose texel buffer holds
//    w*w 8-bit indices addressable by the rasterizer's TexelAt().
// ---------------------------------------------------------------------------
TEST(RealTexture, TextureLoadByNameDecodesRealTexture) {
    if (!assetsPresent()) { CHECK(true); return; }

    // Landkarte2 is a real 256x256 8-bit (square) texture in the archive.
    std::vector<u8> bmp;
    bool got = extractTexture("_DYNAMIC/2D/Landkarte2.bmp", bmp);
    CHECK(got);
    if (!got) return;

    // Square 8-bit -> usable as a texture; confirm via the square-info probe.
    int side = 0;
    CHECK(render::TextureReadSquareInfo(bmp, side));
    CHECK_EQ(side, 256);

    // Mount it under a path and drive the VFS-backed LoadByName.
    shim::MemFileSystem mem;
    mem.put("textures/LANDKARTE2.BMP", bmp);
    io::VfsInit(&mem, false);

    render::TextureAssetCache cache(64);
    int slot = cache.LoadByName("textures/LANDKARTE2.BMP", "LANDKARTE2");
    CHECK(slot >= 0);
    if (slot < 0) { io::VfsShutdown(); return; }

    const render::Texture* rec = cache.record(slot);
    CHECK(rec != nullptr);
    if (rec) {
        CHECK_EQ(rec->name, std::string("LANDKARTE2"));
        CHECK_EQ(rec->mipWidth, 256);
        CHECK_EQ(rec->baseWidth, 256);
        CHECK_EQ((int)rec->texels.size(), 256 * 256);
        CHECK_EQ(rec->texelMask, render::TexelMask(256));
        CHECK_EQ((int)rec->widthShift, 8);            // log2(256)
        CHECK(rec->palette != nullptr);               // source palette persisted
        CHECK_EQ((int)rec->refCount, 1);

        // The decoded texels must equal BmpLoadBuffer's 8-bit indices and address
        // correctly through TexelAt (the rasterizer's inner-loop addressing).
        int w = 0, h = 0;
        u8 pal[256 * 3];
        std::vector<u8> idx = render::BmpLoadBuffer(bmp, 8, w, h, pal);
        CHECK_EQ((int)idx.size(), 256 * 256);
        bool texelsMatch = (idx.size() == rec->texels.size()) &&
                           std::memcmp(idx.data(), rec->texels.data(), idx.size()) == 0;
        CHECK(texelsMatch);

        // TexelAt(u,v) must return the index at row-major (v*w+u) for a 256-wide tex.
        bool addrOk = true;
        for (int vv = 0; vv < 256 && addrOk; vv += 37)
            for (int uu = 0; uu < 256; uu += 53)
                if (render::TexelAt(*rec, uu, vv) != idx[(std::size_t)vv * 256 + uu])
                    addrOk = false;
        CHECK(addrOk);

        // The texture has real (non-constant) content.
        bool varied = false;
        for (std::size_t i = 1; i < rec->texels.size(); ++i)
            if (rec->texels[i] != rec->texels[0]) { varied = true; break; }
        CHECK(varied);

        std::printf("[RealTexture] Texture_LoadByName: slot=%d %dx%d texels=%zu "
                    "mask=0x%X shift=%d texelsMatch=%d addrOk=%d\n",
                    slot, rec->mipWidth, rec->mipWidth, rec->texels.size(),
                    rec->texelMask, (int)rec->widthShift, (int)texelsMatch, (int)addrOk);
    }

    // A second LoadByName of the same name must reuse the slot (refcount bump).
    int slot2 = cache.LoadByName("textures/LANDKARTE2.BMP", "LANDKARTE2");
    CHECK_EQ(slot2, slot);
    CHECK_EQ((int)cache.record(slot)->refCount, 2);

    io::VfsShutdown();
}

// ---------------------------------------------------------------------------
// 5. Synthetic coverage for the codec branches the shipped files never hit:
//    BI_RLE8 run-length decode + a top-down (negative-height) 8-bit BMP. These
//    exercise BmpLoadBuffer paths that the real (all bottom-up BI_RGB) assets do
//    not, proving the loader handles those documented variants too.
// ---------------------------------------------------------------------------
namespace {
void put16(std::vector<u8>& v, std::size_t at, u16 x) { v[at] = (u8)x; v[at+1] = (u8)(x>>8); }
void put32(std::vector<u8>& v, std::size_t at, u32 x) {
    v[at]=(u8)x; v[at+1]=(u8)(x>>8); v[at+2]=(u8)(x>>16); v[at+3]=(u8)(x>>24);
}

// Build a minimal 8-bit BMP header (14+40 + 256-entry palette) into a 0x436-byte
// prefix. `height` may be negative for top-down; `comp` = 0 (BI_RGB) or 1 (RLE8).
std::vector<u8> bmp8Header(int width, int height, u32 comp, std::size_t pixBytes,
                           const u8* palRGB /*256*3, may be null*/) {
    std::vector<u8> f(0x36 + 256 * 4 + pixBytes, 0);
    put16(f, 0, 19778);                       // 'BM'
    put32(f, 2, (u32)f.size());               // file size
    put32(f, 10, 0x36 + 256 * 4);             // data offset
    put32(f, 14, 40);                         // info header size
    put32(f, 18, (u32)width);
    put32(f, 22, (u32)(i32)height);
    put16(f, 26, 1);                          // planes
    put16(f, 28, 8);                          // bpp
    put32(f, 30, comp);
    put32(f, 46, 256);                        // clrUsed = 256
    for (int i = 0; i < 256; ++i) {
        u8 r = palRGB ? palRGB[3*i+0] : (u8)i;
        u8 g = palRGB ? palRGB[3*i+1] : (u8)i;
        u8 b = palRGB ? palRGB[3*i+2] : (u8)i;
        f[0x36 + 4*i + 0] = b;                // BGRA on disk
        f[0x36 + 4*i + 1] = g;
        f[0x36 + 4*i + 2] = r;
        f[0x36 + 4*i + 3] = 0;
    }
    return f;
}
} // namespace

TEST(RealTexture, SyntheticRle8Decode) {
    // 4x4 image, BI_RLE8. Encode 4 rows of "4 * value=row*16" then end-of-bitmap.
    // RLE stream is appended after the palette.
    std::vector<u8> rle;
    for (int row = 0; row < 4; ++row) {
        rle.push_back(4);                 // run length
        rle.push_back((u8)(row * 16));    // value
        rle.push_back(0);                 // end of line
        rle.push_back(0);
    }
    rle.push_back(0); rle.push_back(1);   // end of bitmap

    std::vector<u8> f = bmp8Header(4, 4, /*comp=*/1, rle.size(), nullptr);
    std::memcpy(f.data() + 0x36 + 256 * 4, rle.data(), rle.size());

    int w = 0, h = 0;
    u8 pal[256 * 3];
    std::vector<u8> idx = render::BmpLoadBuffer(f, 8, w, h, pal);
    CHECK_EQ(w, 4);
    CHECK_EQ(h, 4);
    CHECK_EQ((int)idx.size(), 16);
    // RLE rows are emitted bottom-up on disk; the loader flips to top-down. Disk
    // row r had value r*16, placed at output row (3-r). So output row y has value
    // (3-y)*16 across all 4 columns.
    bool ok = !idx.empty();
    for (int y = 0; y < 4 && ok; ++y)
        for (int x = 0; x < 4; ++x)
            if (idx[(std::size_t)y * 4 + x] != (u8)((3 - y) * 16)) ok = false;
    CHECK(ok);
    std::printf("[RealTexture] synthetic RLE8 4x4 decoded ok=%d\n", (int)ok);
}

TEST(RealTexture, SyntheticTopDown8Bit) {
    // 4x4 BI_RGB 8-bit, NEGATIVE height (top-down rows). Row r filled with value r.
    std::size_t pix = 4 * 4;
    std::vector<u8> f = bmp8Header(4, -4, /*comp=*/0, pix, nullptr);
    u8* px = f.data() + 0x36 + 256 * 4;
    for (int r = 0; r < 4; ++r)
        for (int x = 0; x < 4; ++x)
            px[r * 4 + x] = (u8)r;

    int w = 0, h = 0;
    u8 pal[256 * 3];
    std::vector<u8> idx = render::BmpLoadBuffer(f, 8, w, h, pal);
    CHECK_EQ(w, 4);
    CHECK_EQ(h, 4);
    CHECK_EQ((int)idx.size(), 16);
    // Top-down: no flip, so output row y == disk row y == value y.
    bool ok = !idx.empty();
    for (int y = 0; y < 4 && ok; ++y)
        for (int x = 0; x < 4; ++x)
            if (idx[(std::size_t)y * 4 + x] != (u8)y) ok = false;
    CHECK(ok);
    std::printf("[RealTexture] synthetic top-down 8-bit 4x4 decoded ok=%d\n", (int)ok);
}
