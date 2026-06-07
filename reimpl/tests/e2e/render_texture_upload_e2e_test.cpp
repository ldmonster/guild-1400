// End-to-end (GUARDED) validation of the texture-record upload/bind layer against
// the REAL shipped game assets:
//   europe_guild_1400_original/Resources/Textures.BIN — PKZIP of ~2370 BMPs.
//
// Flow: mount Textures.BIN -> extract a real square 8-bit texture -> serve it via
// a mock VFS -> TextureAssetCache::LoadByName (VIBE_Texture_LoadByName software
// path) builds a real Texture record -> copy that record into a TextureBank slot
// -> BindActive (the binding addresses the SAME real texels the rasterizer reads)
// -> build a real mip chain from the decoded texels -> UploadToSurface through a
// counting mock GPU (no vendor calls), confirming the release/load orchestration
// drives the real record.
//
// GUARDED: if the asset folder is absent every test passes trivially.
#include "test.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "io/zip_archive.h"
#include "io/vfs.h"
#include "render/bmp.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/texture_upload.h"
#include "render/texture_mip.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/Textures.BIN");
}

static bool extractTexture(const char* member, std::vector<u8>& out) {
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/Textures.BIN"))
        return false;
    return z.ExtractByName(member, out, false);
}

namespace {
struct CountingGpu : ITextureSurface {
    int releases = 0, loads = 0; u32 next = 9000;
    i32 release(u32) override { ++releases; return 0; }
    u8 loadAndUpload(const char*, int, bool, u8, u8 shift, u32* s, u32* d) override {
        ++loads; if (s) *s = next++; if (d) *d = next++; return shift;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Real texture -> record -> bind -> mip -> upload, fully end to end.
// ---------------------------------------------------------------------------
TEST(TexUploadE2E, RealTextureBindMipUpload) {
    if (!assetsPresent()) { CHECK(true); return; }

    // Landkarte2 is a real 256x256 8-bit square texture in the archive.
    std::vector<u8> bmp;
    bool got = extractTexture("_DYNAMIC/2D/Landkarte2.bmp", bmp);
    CHECK(got);
    if (!got) return;

    // Drive the real VFS-backed LoadByName into a record.
    shim::MemFileSystem mem;
    mem.put("textures/LANDKARTE2.BMP", bmp);
    io::VfsInit(&mem, false);

    TextureAssetCache cache(32);
    int slot = cache.LoadByName("textures/LANDKARTE2.BMP", "LANDKARTE2");
    CHECK(slot >= 0);
    if (slot < 0) { io::VfsShutdown(); return; }
    const Texture* loaded = cache.record(slot);
    CHECK(loaded != nullptr);
    if (!loaded) { io::VfsShutdown(); return; }
    CHECK_EQ(loaded->mipWidth, 256);

    // Move the loaded record into a TextureBank slot (the upload-layer bank).
    TextureBank bank(8);
    bank.records[3] = *loaded;
    bank.records[3].slot = 3;                  // bank index as the +76 self-ref
    Texture& rec = bank.records[3];
    CHECK_EQ((int)rec.texels.size(), 256 * 256);

    // BindActive: binding addresses the SAME real texels.
    bank.BindActive(3);
    CHECK_EQ(bank.binding.mipWidth, 256);
    CHECK_EQ((int)bank.binding.widthShift, 8);
    CHECK(bank.binding.texBase == rec.texels.data());

    // Build a real mip chain from the decoded texels; level1 is a 2x2 reduction.
    std::vector<std::vector<u8>> chain = BuildIndexMipChain(rec.texels.data(), 256);
    CHECK_EQ((int)chain.size(), MipLevelCount(256));   // 256..1 -> 9 levels
    CHECK_EQ((int)chain[1].size(), 128 * 128);
    bool mipOk = true;
    for (int v = 0; v < 128 && mipOk; v += 17)
        for (int u = 0; u < 128; u += 23)
            if (chain[1][v * 128 + u] != rec.texels[(2 * v) * 256 + (2 * u)])
                mipOk = false;
    CHECK(mipOk);

    // Upload the real record through a counting mock GPU.
    bank.swEnabled = 1;
    CountingGpu gpu;
    bool ok = bank.UploadToSurface(3, /*force=*/false, "LANDKARTE2.BMP", gpu);
    CHECK(ok);
    CHECK_EQ(gpu.loads, 1);
    CHECK(bank.gpu[3].srcSurface != 0);
    CHECK(bank.gpu[3].dstSurface != 0);

    std::printf("[TexUploadE2E] Landkarte2 256x256: bound shift=%d mip-levels=%d "
                "gpu-loads=%d mipOk=%d\n",
                (int)bank.binding.widthShift, (int)chain.size(), gpu.loads, (int)mipOk);

    io::VfsShutdown();
}

// ---------------------------------------------------------------------------
// UploadAllRecords over several real textures: every active master uploaded.
// ---------------------------------------------------------------------------
namespace {
const char* g_paths[8] = {};
const char* pathFor(const TextureBank&, int i) { return g_paths[i]; }
int g_uploadCb = 0;
void uploadCb(void*) { ++g_uploadCb; }
} // namespace

TEST(TexUploadE2E, UploadAllRealRecords) {
    if (!assetsPresent()) { CHECK(true); return; }

    struct Want { const char* member; const char* name; int side; };
    const Want wants[] = {
        {"_DYNAMIC/2D/Landkarte2.bmp", "LANDKARTE2", 256},
    };

    shim::MemFileSystem mem;
    TextureAssetCache cache(32);
    TextureBank bank(8);
    int n = 0;
    for (const Want& wt : wants) {
        std::vector<u8> bmp;
        if (!extractTexture(wt.member, bmp)) continue;
        std::string vpath = std::string("tex/") + wt.name + ".BMP";
        mem.put(vpath, bmp);
        io::VfsInit(&mem, false);
        int slot = cache.LoadByName(vpath.c_str(), wt.name);
        io::VfsShutdown();
        if (slot < 0) continue;
        bank.records[n] = *cache.record(slot);
        bank.records[n].slot = n + 1;          // non-zero self-ref (slot 0 == white)
        g_paths[n] = "REAL.BMP";
        ++n;
    }
    CHECK(n > 0);

    bank.swEnabled = 1;
    g_uploadCb = 0;
    CountingGpu gpu;
    int uploaded = bank.UploadAllRecords(false, gpu, pathFor, uploadCb, nullptr);
    CHECK_EQ(uploaded, n);
    CHECK_EQ(g_uploadCb, n);
    CHECK_EQ(bank.binding.boundIndex, 0);      // dword_64A1F8 cleared
    std::printf("[TexUploadE2E] UploadAllRecords uploaded %d real records\n", uploaded);
}
