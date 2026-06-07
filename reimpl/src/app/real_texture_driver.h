#pragma once
// gilde.exe — REAL-asset texture -> render driver (guild::app).
//
// INTEGRATION GLUE, not a translation. This module wires the already-
// reconstructed pieces of the texture/BMP/render stack into one end-to-end
// path that proves REAL shipped texture bytes flow through the renderer to an
// inspectable image:
//
//   1. (optionally) MountRealGameAssets over a shim::IFileSystem rooted at a
//      real "Die Gilde" install (real_boot.h);
//   2. mount Resources/Textures.BIN with the reconstructed io::ZipArchive and
//      decode its REAL BMP members through render::TextureAssetCache::LoadByName
//      (the VIBE_Texture_LoadByName software path) -> render::Texture records
//      whose +68 texel buffers hold the real 8-bit palette indices;
//   3. build a 16bpp index->RGB565 palette LUT from each decoded record's
//      source palette (render::PackColor) and rasterize a textured quad of one
//      chosen texture into a 16-bit render::Surface using the REAL affine
//      textured-triangle rasterizer (render::RasterizeTexturedTriangleRgbz);
//   4. copy that 16-bit framebuffer into a shim::FileDumpGraphicsDevice
//      backbuffer and present() -> a standard 24-bit BMP / P6 PPM on disk,
//      so the rendered frame is viewable with any image tool.
//
// Everything reached here is real reconstructed code over real bytes; the only
// OS boundary is shim::IFileSystem (asset reads) and the optional file dump.
// No DDraw/GDI: the present layer is the inert FileDumpGraphicsDevice shim.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"

#include <string>
#include <vector>

namespace guild::app {

// Per-texture decode record returned by the driver.
struct DecodedTextureInfo {
    std::string member;     // archive member name (or VFS path)
    int  width = 0;         // square texture side (w == h)
    int  bpp = 0;           // source BMP bit count (8 or 24)
    bool square = false;    // passed the texture square-check (loadable)
    bool decoded = false;   // LoadByName produced a record
    long nonzeroTexels = 0; // count of non-zero palette indices in the texels
};

// The result of a render-driver run.
struct TextureRenderResult {
    // ---- decode tier ----
    int  archiveMembers = 0;   // total members listed in Textures.BIN
    int  bmpMembers = 0;       // members with a .BMP/.bmp suffix
    int  texturesDecoded = 0;  // members LoadByName successfully decoded
    int  squareTextures = 0;   // decoded textures that were square (renderable)
    std::vector<DecodedTextureInfo> samples;  // a small spread of decoded textures

    // ---- render tier ----
    bool rendered = false;     // a frame was rasterized + presented
    std::string renderedTexture;  // which texture was rasterized
    int  renderWidth = 0;      // framebuffer dimensions
    int  renderHeight = 0;
    long nonBlankPixels = 0;   // non-zero 16bpp framebuffer pixels after raster
    std::string framePath;     // dumped image path ("" if no dump configured)
};

// ---------------------------------------------------------------------------
// Drive ONE texture from a BMP byte buffer all the way to a presented frame.
// Decodes the BMP through render::DecodeBmpIntoTexture, builds the 16bpp LUT,
// rasterizes a full-surface textured quad with the real RGBZ rasterizer, copies
// the 16bpp framebuffer into a FileDumpGraphicsDevice and presents one frame.
//
//   * `fbW`/`fbH` are the output framebuffer size.
//   * `dumpDir` empty -> no file written (frame still rasterized + counted).
//   * Returns a result with rendered=true / nonBlankPixels>0 on success.
// ---------------------------------------------------------------------------
TextureRenderResult RenderTextureBmpToImage(const std::vector<u8>& bmp,
                                            const std::string& name,
                                            int fbW, int fbH,
                                            const std::string& dumpDir = "",
                                            const std::string& dumpPrefix = "tex");

// ---------------------------------------------------------------------------
// The full real-asset path: open Textures.BIN through `fs`, list + decode every
// BMP member through the by-name texture cache, count decodes / squares, collect
// a sample spread, then rasterize ONE chosen square texture to a frame (and
// optionally dump it). `archiveRel` defaults to "Resources/Textures.BIN".
//
// `renderMember` selects which decoded texture is rasterized; if empty (or not
// found square) the first square texture decoded is used. `maxDecode` caps the
// number of members decoded (<=0 == all) to keep test runs bounded.
// ---------------------------------------------------------------------------
TextureRenderResult RenderRealTexturesFromArchive(
    shim::IFileSystem* fs,
    const std::string& archiveRel = "Resources/Textures.BIN",
    const std::string& renderMember = "",
    int fbW = 256, int fbH = 256,
    const std::string& dumpDir = "",
    int maxDecode = 0);

} // namespace guild::app
