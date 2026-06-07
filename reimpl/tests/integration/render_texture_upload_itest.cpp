// Integration test: the reconstructed texture-record upload/bind layer
// (render/texture_upload) driven against the REAL sibling modules it shares the
// 128-byte record with:
//   * render/texture_asset.{cpp}  — TextureAssetCache::LoadByName (VFS+BMP decode)
//   * render/bmp.cpp              — BmpLoadBuffer (8-bit index decode)
//   * render/texture.cpp         — Texture record + TextureSet slot manager
//   * render/texture_mip.cpp     — BuildIndexMipChain / DownsampleIndex2x
//
// Flow: synthesize a real 8-bit BMP -> decode it into a Texture record via the
// real DecodeBmpIntoTexture -> copy that record into a TextureBank slot -> bind
// it (BindActive reads the SAME texel buffer the rasterizer addresses) and verify
// the binding matches the decoded geometry; build a mip chain from the decoded
// texels; then exercise UploadToSurface across a master + its clone through a
// mock GPU and confirm PropagateToInstances copied the decoded texels to the clone.
#include "test.h"

#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/texture_upload.h"
#include "render/texture_mip.h"
#include "render/bmp.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
void put16(std::vector<u8>& v, std::size_t at, u16 x) { v[at]=(u8)x; v[at+1]=(u8)(x>>8); }
void put32(std::vector<u8>& v, std::size_t at, u32 x) {
    v[at]=(u8)x; v[at+1]=(u8)(x>>8); v[at+2]=(u8)(x>>16); v[at+3]=(u8)(x>>24);
}

// Build a real square 8-bit BI_RGB BMP (bottom-up) of side `n`, pixel(x,y)=index.
std::vector<u8> makeSquareBmp(int n, const u8* indices) {
    std::size_t pix = (std::size_t)n * n;
    std::vector<u8> f(0x36 + 256 * 4 + pix, 0);
    put16(f, 0, 19778);                 // 'BM'
    put32(f, 2, (u32)f.size());
    put32(f, 10, 0x36 + 256 * 4);
    put32(f, 14, 40);
    put32(f, 18, (u32)n);
    put32(f, 22, (u32)n);               // positive -> bottom-up
    put16(f, 26, 1);
    put16(f, 28, 8);
    put32(f, 30, 0);                    // BI_RGB
    put32(f, 46, 256);
    for (int i = 0; i < 256; ++i) {     // grayscale palette
        f[0x36 + 4*i + 0] = (u8)i;      // B
        f[0x36 + 4*i + 1] = (u8)i;      // G
        f[0x36 + 4*i + 2] = (u8)i;      // R
    }
    // bottom-up: disk row 0 == image bottom row (n-1).
    u8* px = f.data() + 0x36 + 256 * 4;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            px[(std::size_t)(n - 1 - y) * n + x] = indices[(std::size_t)y * n + x];
    return f;
}

struct MockGpu : ITextureSurface {
    int releases = 0, loads = 0; u32 next = 500;
    i32 release(u32) override { ++releases; return 0; }
    u8 loadAndUpload(const char*, int, bool, u8, u8 shift, u32* s, u32* d) override {
        ++loads; if (s) *s = next++; if (d) *d = next++; return shift;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Decode a real BMP into a record via the real sibling, then bind through the
// upload layer and verify the binding matches the decoded geometry + texels.
// ---------------------------------------------------------------------------
TEST(TexUploadIT, DecodeThenBindMatchesRealRecord) {
    const int N = 16;
    u8 idx[N * N];
    for (int i = 0; i < N * N; ++i) idx[i] = (u8)(i & 0xFF);
    std::vector<u8> bmp = makeSquareBmp(N, idx);

    // REAL sibling: decode the BMP into a Texture record.
    TextureBank bank(4);
    Texture& rec = bank.records[1];
    rec = Texture{};
    rec.refCount = 1;
    rec.slot = 1;
    rec.name = "TILE";
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    CHECK(dec.ok);
    CHECK_EQ(dec.width, N);
    CHECK_EQ((int)rec.texels.size(), N * N);
    CHECK_EQ(rec.mipWidth, N);
    CHECK_EQ((int)rec.widthShift, 4);          // log2(16)
    rec.palette = rec.paletteStore.empty() ? dec.palette.data() : rec.palette;
    rec.paletteStore = dec.palette;            // persist for the binding
    rec.palette = rec.paletteStore.data();

    // The decoded texels must equal BmpLoadBuffer's indices (the rasterizer source).
    int w = 0, h = 0; u8 pal[256 * 3];
    std::vector<u8> ref = BmpLoadBuffer(bmp, 8, w, h, pal);
    CHECK_EQ((int)ref.size(), N * N);
    CHECK(std::memcmp(ref.data(), rec.texels.data(), ref.size()) == 0);

    // Bind it: the binding must point at the SAME texel buffer + geometry.
    bank.BindActive(1);
    CHECK_EQ(bank.binding.mipWidth, N);
    CHECK_EQ((int)bank.binding.widthShift, 4);
    CHECK(bank.binding.texBase == rec.texels.data());
    CHECK(bank.binding.palBase == rec.palette);

    // TexelAt (rasterizer inner loop) and the bound texBase must agree.
    for (int v = 0; v < N; v += 5)
        for (int u = 0; u < N; u += 3)
            CHECK_EQ(TexelAt(rec, u, v), bank.binding.texBase[(v << 4) + u]);
}

// ---------------------------------------------------------------------------
// Build a mip chain from the REAL decoded texels and confirm the first mip is a
// 2x2 box-select reduction of the source indices.
// ---------------------------------------------------------------------------
TEST(TexUploadIT, MipChainFromDecodedTexels) {
    const int N = 8;
    u8 idx[N * N];
    for (int i = 0; i < N * N; ++i) idx[i] = (u8)(i * 3);   // arbitrary content
    std::vector<u8> bmp = makeSquareBmp(N, idx);

    Texture rec;
    rec.refCount = 1; rec.slot = 1;
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    CHECK(dec.ok);

    std::vector<std::vector<u8>> chain = BuildIndexMipChain(rec.texels.data(), N);
    CHECK_EQ((int)chain.size(), MipLevelCount(N));     // 8,4,2,1 -> 4
    // mip level 1 (4x4): each texel == src[2v][2u].
    for (int v = 0; v < 4; ++v)
        for (int u = 0; u < 4; ++u)
            CHECK_EQ(chain[1][v * 4 + u], rec.texels[(2 * v) * N + (2 * u)]);
}

// ---------------------------------------------------------------------------
// Cross-module upload: a master record (real decoded texels) + a clone. Upload
// the master through a mock GPU; PropagateToInstances must hand the clone the
// master's decoded texels + freshly created surfaces.
// ---------------------------------------------------------------------------
TEST(TexUploadIT, UploadPropagatesDecodedTexelsToClone) {
    const int N = 16;
    u8 idx[N * N];
    for (int i = 0; i < N * N; ++i) idx[i] = (u8)(255 - (i & 0xFF));
    std::vector<u8> bmp = makeSquareBmp(N, idx);

    TextureBank bank(8);
    Texture& master = bank.records[0];
    master = Texture{};
    master.refCount = 1; master.slot = 0 /*set below*/; master.name = "ROCK";
    DecodeBmpIntoTexture(bmp, master);
    master.slot = 0;                 // index 0; but slot 0 == white -> use index 2
    // Move the master to a non-zero slot so BindActive/Upload treat it as a real tex.
    bank.records[2] = master;
    bank.records[2].slot = 2;
    bank.records[0] = Texture{};
    Texture& m = bank.records[2];

    // A clone (flags bit1) referencing master index 2.
    Texture& clone = bank.records[5];
    clone = Texture{};
    clone.refCount = 1; clone.flags = 2; clone.slot = 2; clone.name = "ROCK#1";

    bank.swEnabled = 1;
    MockGpu gpu;
    bool ok = bank.UploadToSurface(2, /*force=*/false, "ROCK.BMP", gpu);
    CHECK(ok);
    CHECK_EQ(gpu.loads, 1);          // master uploaded
    CHECK(bank.gpu[2].srcSurface != 0);

    // PropagateToInstances (called inside Upload) handed the clone the master's
    // surfaces + the real decoded texels.
    CHECK_EQ(bank.gpu[5].srcSurface, bank.gpu[2].srcSurface);
    CHECK_EQ(bank.gpu[5].dstSurface, bank.gpu[2].dstSurface);
    CHECK_EQ((int)bank.records[5].texels.size(), N * N);
    CHECK(std::memcmp(bank.records[5].texels.data(),
                      m.texels.data(), (std::size_t)N * N) == 0);
}
