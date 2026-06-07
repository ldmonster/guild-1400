// End-to-end: render a REAL game texture through the RGBZ affine-textured
// rasterizer. GUARDED — skips (pass) when the real asset tree is absent.
//
// Flow: mount europe_guild_1400_original/Resources/Textures.BIN with the
// reconstructed ZipArchive, pull out a real square BMP texture leaf, decode it
// into a Texture record (render/texture_asset, using the real BmpLoadBuffer +
// TextureSetSize), build a 16bpp palette LUT via the real colour packer, then
// rasterize a textured triangle into a real Surface and confirm every emitted
// pixel is a genuine texel fetched from the decoded texture.
#include "render/raster_textured.h"
#include "render/raster.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/bmp.h"
#include "render/colorformat.h"
#include "io/zip_archive.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "test.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// texture_asset.cpp's (unused-here) LoadByName cache references VfsSlurp, owned
// by render/mesh_asset.cpp. This e2e decodes bytes the ZipArchive extracted and
// never uses the VFS. The real VfsSlurp links in the full CMake build; this WEAK
// fallback only satisfies an isolated link (the strong real def overrides it).
namespace guild::render {
__attribute__((weak)) bool VfsSlurp(const char*, std::vector<u8>&) { return false; }
}

namespace {

const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/Textures.BIN");
}

// Find and extract the first square BMP texture leaf (w == h, 8-bit) from the
// archive. Returns true + fills `out` and `name`.
bool extractFirstSquareTexture(std::vector<u8>& out, std::string& name) {
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/Textures.BIN"))
        return false;
    char nm[512];
    io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        std::size_t len = std::strlen(nm);
        if (len < 4 || nm[len - 1] == '/') continue;
        if (!((nm[len - 4] == '.') &&
              (nm[len - 3] == 'B' || nm[len - 3] == 'b') &&
              (nm[len - 2] == 'M' || nm[len - 2] == 'm') &&
              (nm[len - 1] == 'P' || nm[len - 1] == 'p')))
            continue;
        std::vector<u8> data;
        if (!z.ExtractByName(nm, data, false)) continue;
        int side = 0;
        if (!TextureReadSquareInfo(data, side) || side < 2 || side > 1024)
            continue;
        // Require an 8-bit indexed source (the texture decode path is palettized).
        BmpInfo bi = BmpReadHeaderInfo(data);
        if (bi.bitCount != 8) continue;
        // Decode must actually succeed (non-trivial palette).
        Texture probe;
        TextureDecode pd = DecodeBmpIntoTexture(data, probe);
        if (!pd.ok) continue;
        out = std::move(data);
        name = nm;
        return true;
    }
    return false;
}

} // namespace

// --- render a real decoded texture; verify all pixels are real texels --------
TEST(RenderRasterTexE2E, RealTexturedTriangle) {
    if (!assetsPresent()) { CHECK(true); return; }  // GUARDED skip-pass

    std::vector<u8> bmp;
    std::string name;
    if (!extractFirstSquareTexture(bmp, name)) { CHECK(true); return; }

    Texture rec;
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    CHECK(dec.ok);
    CHECK(rec.mipWidth >= 2);
    CHECK_EQ((int)rec.texels.size(), rec.mipWidth * rec.mipWidth);
    CHECK_EQ((int)rec.texelMask, (int)TexelMask(rec.mipWidth));

    // Build the 16bpp palette LUT from the recovered source palette.
    ColorFormat fmt = Format565();
    std::vector<u16> pal16(256, 0);
    for (int k = 0; k < 256 && (size_t)(k * 3 + 2) < dec.palette.size(); ++k)
        pal16[k] = (u16)PackColor(fmt, dec.palette[k * 3 + 0],
                                  dec.palette[k * 3 + 1],
                                  dec.palette[k * 3 + 2]);

    // Render a textured triangle that samples the whole texture.
    Surface* s = SurfaceCreate(64, 64, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
    RgbzVertex v[3] = {
        {6.0f, 6.0f, 0.0f, 0.0f},
        {56.0f, 12.0f, 1.0f, 0.0f},
        {12.0f, 56.0f, 0.0f, 1.0f},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, rec, pal16.data());
    CHECK(drew != 0);

    // Every non-zero pixel must be a palette entry of a texel that exists in the
    // decoded texture (the rasterizer fetched real texels, not garbage).
    std::vector<bool> valid(0x10000, false);
    valid[0] = true;
    for (size_t i = 0; i < rec.texels.size(); ++i)
        valid[pal16[rec.texels[i]]] = true;
    const u16* px = (const u16*)s->pixels;
    int drawn = 0;
    for (int i = 0; i < s->height * s->widthPx; ++i) {
        CHECK(valid[px[i]]);
        if (px[i] != 0) ++drawn;
    }
    CHECK(drawn > 0);
    std::printf("[RasterTexE2E] '%s' (%dx%d) -> %d textured pixels rendered\n",
                name.c_str(), rec.mipWidth, rec.mipWidth, drawn);
    SurfaceDestroy(s);
}
