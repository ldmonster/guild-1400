#include "app/real_texture_driver.h"

#include "io/zip_archive.h"
#include "io/vfs.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/filedump_graphics.h"
#include "render/bmp.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "render/raster_textured.h"

#include <cstring>

// =============================================================================
// guild::app — real-asset texture -> render driver (implementation).
//
// Wiring only. Every load-bearing call below is into an already-reconstructed
// reconstructed function over real bytes:
//   io::ZipArchive            (mount/list/extract Textures.BIN members)
//   render::DecodeBmpIntoTexture / render::TextureAssetCache::LoadByName
//                             (the VIBE_Texture_LoadByName software decode path)
//   render::SurfaceCreate     (the gfx.c system-memory surface)
//   render::PackColor         (channel pack for the 16bpp palette LUT)
//   render::RasterizeTexturedTriangleRgbz (the affine textured-triangle path)
//   shim::FileDumpGraphicsDevice (the inert present/dump shim — no DDraw/GDI)
// =============================================================================
namespace guild::app {

namespace {

// Build a 256-entry index->RGB565 LUT from a record's source palette (256*3 RGB
// triples). This is the dword_1406A78 "index->16bpp" table the rasterizer's
// patched palBase immediate pointed at; we synthesise it from the decoded
// palette via the same channel-pack the surface uses (render::PackColor).
void BuildPalette565(const render::Texture& rec, const render::ColorFormat& fmt,
                     u16 lut[256]) {
    const u8* pal = static_cast<const u8*>(rec.palette);
    for (int i = 0; i < 256; ++i) {
        u8 r = 0, g = 0, b = 0;
        if (pal) { r = pal[3 * i + 0]; g = pal[3 * i + 1]; b = pal[3 * i + 2]; }
        lut[i] = static_cast<u16>(render::PackColor(fmt, r, g, b));
    }
}

// Count non-zero palette indices in a record's texel buffer.
long CountNonzeroTexels(const render::Texture& rec) {
    long n = 0;
    for (u8 t : rec.texels) if (t) ++n;
    return n;
}

// Rasterize the whole `tex` as a screen-filling textured quad (two triangles)
// into the 16-bit surface `fb`, using the real RGBZ rasterizer. UVs span [0,1)
// across the surface so the entire texture is sampled. Returns spans-drawn flag.
int RasterFullQuad(render::Surface* fb, const render::Texture& tex,
                   const u16* pal565) {
    const float W = static_cast<float>(fb->width);
    const float H = static_cast<float>(fb->height);
    // Two triangles covering [0,W)x[0,H). The RGBZ rasterizer (like the original
    // mirror-poly path) draws the winding whose long edge is on the LEFT; the
    // screen-down/clockwise winding below is the one that yields positive spans.
    // UVs are in TEXEL units [0,M) (M = tex.mipWidth); RasterizeTexturedTriangleRgbz
    // multiplies them by mipWidth (texScale) exactly as the engine did, and the
    // (V*W+U)&texelMask addressing wraps the result back into the square.
    const float M = static_cast<float>(tex.mipWidth);
    const render::RgbzVertex tri0[3] = {
        {0.f, 0.f, 0.f, 0.f},
        {W,   H,   M,   M},
        {0.f, H,   0.f, M},
    };
    const render::RgbzVertex tri1[3] = {
        {0.f, 0.f, 0.f, 0.f},
        {W,   0.f, M,   0.f},
        {W,   H,   M,   M},
    };
    int drew = render::RasterizeTexturedTriangleRgbz(fb, tri0, tex, pal565);
    drew |= render::RasterizeTexturedTriangleRgbz(fb, tri1, tex, pal565);
    return drew;
}

// Present one 16bpp surface through a FileDumpGraphicsDevice. Copies the
// software framebuffer (RGB565, what the rasterizer wrote) into the device's
// 16bpp backbuffer and presents -> a 24-bit BMP on disk if `dumpDir` set.
// Returns the dumped frame path ("" if no dump or failure).
std::string PresentSurface(const render::Surface* fb, const std::string& dumpDir,
                           const std::string& dumpPrefix) {
    shim::FileDumpGraphicsDevice dev;
    if (!dumpDir.empty())
        dev.configureDump(dumpDir, dumpPrefix, shim::FileDumpGraphicsDevice::kBmp);
    if (!dev.init(fb->width, fb->height, 16, false))
        return "";
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels)
        return "";
    // Copy the rasterizer's RGB565 rows into the device backbuffer (both 16bpp).
    const int bytesPerRow = fb->width * 2;
    for (int y = 0; y < fb->height; ++y) {
        std::memcpy(static_cast<u8*>(bb->pixels) + (std::size_t)y * bb->pitch,
                    fb->pixels + (std::size_t)y * fb->pitch,
                    (std::size_t)bytesPerRow);
    }
    dev.present();
    if (dumpDir.empty())
        return "";
    return dev.framePath(0, shim::FileDumpGraphicsDevice::kBmp);
}

} // namespace

// ---------------------------------------------------------------------------
TextureRenderResult RenderTextureBmpToImage(const std::vector<u8>& bmp,
                                            const std::string& name,
                                            int fbW, int fbH,
                                            const std::string& dumpDir,
                                            const std::string& dumpPrefix) {
    TextureRenderResult res;

    // Decode through the reconstructed BMP/texture loader into a record.
    render::Texture rec;
    rec.name = name;
    render::TextureDecode dec = render::DecodeBmpIntoTexture(bmp, rec);
    if (!dec.ok)
        return res;
    // DecodeBmpIntoTexture points rec.palette at the (by-value) returned dec's
    // palette storage, which would dangle once `dec` dies; persist a record-owned
    // copy (exactly what TextureAssetCache::LoadByName does on its slot record).
    rec.paletteStore = dec.palette;
    rec.palette = rec.paletteStore.data();
    res.texturesDecoded = 1;
    res.squareTextures = 1;

    DecodedTextureInfo info;
    info.member = name;
    info.width = dec.width;
    info.square = true;
    info.decoded = true;
    info.nonzeroTexels = CountNonzeroTexels(rec);
    render::BmpInfo hi = render::BmpReadHeaderInfo(bmp);
    info.bpp = hi.ok ? hi.bitCount : 0;
    res.samples.push_back(info);

    // Build the 16bpp surface + index->RGB565 LUT, rasterize, present.
    render::ColorFormat fmt = render::Format565();
    render::Surface* fb = render::SurfaceCreate(fbW, fbH, 16, fmt);
    if (!fb)
        return res;
    std::memset(fb->pixels, 0, (std::size_t)fb->pitch * fb->height);

    u16 pal565[256];
    BuildPalette565(rec, fmt, pal565);

    int drew = RasterFullQuad(fb, rec, pal565);

    // Count non-blank 16bpp pixels.
    const u16* px = reinterpret_cast<const u16*>(fb->pixels);
    long nonblank = 0;
    for (int i = 0; i < fb->widthPx * fb->height; ++i)
        if (px[i]) ++nonblank;

    res.rendered = (drew != 0);
    res.renderedTexture = name;
    res.renderWidth = fbW;
    res.renderHeight = fbH;
    res.nonBlankPixels = nonblank;
    res.framePath = PresentSurface(fb, dumpDir, dumpPrefix);

    render::SurfaceDestroy(fb);
    return res;
}

// ---------------------------------------------------------------------------
namespace {
// Case-insensitive ".bmp" suffix test.
bool HasBmpSuffix(const char* nm, std::size_t len) {
    if (len < 4) return false;
    return nm[len - 4] == '.' &&
           (nm[len - 3] == 'B' || nm[len - 3] == 'b') &&
           (nm[len - 2] == 'M' || nm[len - 2] == 'm') &&
           (nm[len - 1] == 'P' || nm[len - 1] == 'p');
}
} // namespace

TextureRenderResult RenderRealTexturesFromArchive(
    shim::IFileSystem* fs,
    const std::string& archiveRel,
    const std::string& renderMember,
    int fbW, int fbH,
    const std::string& dumpDir,
    int maxDecode) {
    TextureRenderResult res;
    if (!fs)
        return res;

    io::ZipArchive z;
    if (!z.Open(fs, archiveRel.c_str()))
        return res;

    // First pass: inventory + decode counting (drive the reconstructed decoder
    // over every real BMP member). Keep a chosen record for rendering.
    render::Texture renderRec;
    bool haveRenderRec = false;
    std::string chosenName;

    // A small spread of sample dimensions to report (first few of each kind).
    int sampleCap = 8;

    char nm[512];
    io::ZipFileInfo fi;
    int decodedCount = 0;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        ++res.archiveMembers;
        std::size_t len = std::strlen(nm);
        if (!HasBmpSuffix(nm, len))
            continue;
        ++res.bmpMembers;

        if (maxDecode > 0 && decodedCount >= maxDecode) {
            // Stop decoding once the cap is hit, but keep counting members.
            continue;
        }

        std::vector<u8> bmp;
        if (!z.ExtractByName(nm, bmp, false))
            continue;

        render::Texture rec;
        rec.name = nm;
        render::TextureDecode dec = render::DecodeBmpIntoTexture(bmp, rec);
        if (!dec.ok)
            continue;
        // Persist the decoded palette on the record (DecodeBmpIntoTexture points
        // rec.palette at the returned dec's storage, which would dangle).
        rec.paletteStore = dec.palette;
        rec.palette = rec.paletteStore.data();
        ++res.texturesDecoded;
        ++decodedCount;
        bool square = true;            // DecodeBmpIntoTexture only succeeds square
        if (square) ++res.squareTextures;

        if ((int)res.samples.size() < sampleCap) {
            DecodedTextureInfo info;
            info.member = nm;
            info.width = dec.width;
            info.square = square;
            info.decoded = true;
            info.nonzeroTexels = CountNonzeroTexels(rec);
            render::BmpInfo hi = render::BmpReadHeaderInfo(bmp);
            info.bpp = hi.ok ? hi.bitCount : 0;
            res.samples.push_back(info);
        }

        // Pick the render target: the requested member if it matches, else the
        // first square texture decoded.
        bool wantThis = (!renderMember.empty() && renderMember == nm);
        if (wantThis || (!haveRenderRec && renderMember.empty())) {
            renderRec = rec;            // own a copy (texels + paletteStore)
            renderRec.palette = renderRec.paletteStore.data();
            chosenName = nm;
            haveRenderRec = true;
            if (wantThis) {
                // keep scanning to finish member/decode counts but no need to
                // replace the render record again.
            }
        }
    }

    if (!haveRenderRec)
        return res;   // decoded counts populated; nothing to render

    // Render tier: drive the real RGBZ rasterizer over the chosen real texture.
    render::ColorFormat fmt = render::Format565();
    render::Surface* fb = render::SurfaceCreate(fbW, fbH, 16, fmt);
    if (!fb)
        return res;
    std::memset(fb->pixels, 0, (std::size_t)fb->pitch * fb->height);

    u16 pal565[256];
    BuildPalette565(renderRec, fmt, pal565);
    int drew = RasterFullQuad(fb, renderRec, pal565);

    const u16* px = reinterpret_cast<const u16*>(fb->pixels);
    long nonblank = 0;
    for (int i = 0; i < fb->widthPx * fb->height; ++i)
        if (px[i]) ++nonblank;

    res.rendered = (drew != 0);
    res.renderedTexture = chosenName;
    res.renderWidth = fbW;
    res.renderHeight = fbH;
    res.nonBlankPixels = nonblank;
    res.framePath = PresentSurface(fb, dumpDir, "real_texture");

    render::SurfaceDestroy(fb);
    return res;
}

} // namespace guild::app
