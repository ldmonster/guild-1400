// Integration: wire the text-DB driver (LoadRealTextDb) against the REAL
// reconstructed sibling loaders — guild::io::ArchiveMount (the PKZIP mount the
// real boot builds) + gui::text::BuildTextArray — over a small synthetic PKZIP
// fixture served through an in-memory shim::IFileSystem. Asserts the cross-module
// flow: mount -> per-member extract -> parse -> populated TextDb -> resolve ids.
//
// A guarded second test runs the same driver against the REAL textbin_deutsch.BIN
// when the asset dir is present.
#include "tests/framework/test.h"

#include "app/real_boot.h"
#include "app/real_text_driver.h"

#include "compress/crc.h"
#include "gui/text/textdb.h"
#include "gui/text_load.h"
#include "io/archive_mount.h"
#include "io/vfs.h"

#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include "guild/common/types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using guild::gui::text::TextDb;
using guild::gui::text::kResNameStride;

// ---------------------------------------------------------------------------
// .res blob builder (same recovered layout as the unit test).
namespace {

void PutU32(std::vector<u8>& b, u32 v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
    b.push_back(static_cast<u8>((v >> 16) & 0xFF));
    b.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

struct Entry { std::string name; std::string text; u8 tag; };

std::vector<u8> BuildRes(u32 baseIndex, const std::vector<Entry>& es) {
    std::vector<u8> b;
    u32 ec = static_cast<u32>(es.size());
    PutU32(b, ec);
    PutU32(b, baseIndex);
    PutU32(b, baseIndex + ec - 1);
    std::vector<u8> blob;
    std::vector<u32> offs;
    for (const auto& e : es) {
        offs.push_back(static_cast<u32>(blob.size()));
        blob.insert(blob.end(), e.text.begin(), e.text.end());
        blob.push_back(0);
    }
    for (u32 o : offs) PutU32(b, o);
    for (const auto& e : es) {
        u8 field[kResNameStride] = {0};
        std::size_t n = e.name.size() < (kResNameStride - 1u) ? e.name.size()
                                                              : (kResNameStride - 1u);
        std::memcpy(field, e.name.data(), n);
        b.insert(b.end(), field, field + kResNameStride);
    }
    for (const auto& e : es) b.push_back(e.tag);
    PutU32(b, static_cast<u32>(blob.size()));
    b.insert(b.end(), blob.begin(), blob.end());
    return b;
}

// ---------------------------------------------------------------------------
// Minimal STORED PKZIP writer (no compression) the reconstructed ZipArchive
// reader accepts: local headers, central directory, end-of-central-dir, with a
// real CRC-32 computed via the reconstructed compress::CrcCompute.
struct ZipEntry { std::string name; std::vector<u8> data; };

void Put16(std::vector<u8>& b, u32 v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
}
void Put32(std::vector<u8>& b, u32 v) { PutU32(b, v); }

std::vector<u8> BuildStoredZip(const std::vector<ZipEntry>& entries) {
    std::vector<u8> out;
    struct CD { std::string name; u32 crc, size, localOff; };
    std::vector<CD> cds;

    for (const auto& e : entries) {
        u32 localOff = static_cast<u32>(out.size());
        u32 crc = compress::CrcCompute(0, e.data.data(),
                                       static_cast<u32>(e.data.size()));
        u32 sz = static_cast<u32>(e.data.size());
        // local file header
        Put32(out, 0x04034b50);
        Put16(out, 20);            // version needed
        Put16(out, 0);             // flag
        Put16(out, 0);             // method = stored
        Put16(out, 0); Put16(out, 0); // dos time/date
        Put32(out, crc);
        Put32(out, sz);            // compressed
        Put32(out, sz);            // uncompressed
        Put16(out, static_cast<u32>(e.name.size())); // name len
        Put16(out, 0);             // extra len
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.data.begin(), e.data.end());
        cds.push_back({e.name, crc, sz, localOff});
    }

    u32 cdStart = static_cast<u32>(out.size());
    for (const auto& c : cds) {
        Put32(out, 0x02014b50);
        Put16(out, 20);            // version made by
        Put16(out, 20);            // version needed
        Put16(out, 0);             // flag
        Put16(out, 0);             // method
        Put16(out, 0); Put16(out, 0); // dos time/date
        Put32(out, c.crc);
        Put32(out, c.size);        // compressed
        Put32(out, c.size);        // uncompressed
        Put16(out, static_cast<u32>(c.name.size())); // name len
        Put16(out, 0);             // extra len
        Put16(out, 0);             // comment len
        Put16(out, 0);             // disk number start
        Put16(out, 0);             // internal attrs
        Put32(out, 0);             // external attrs
        Put32(out, c.localOff);    // local header offset
        out.insert(out.end(), c.name.begin(), c.name.end());
    }
    u32 cdSize = static_cast<u32>(out.size()) - cdStart;

    // EOCD
    Put32(out, 0x06054b50);
    Put16(out, 0);             // disk number
    Put16(out, 0);             // cd start disk
    Put16(out, static_cast<u32>(cds.size())); // entries this disk
    Put16(out, static_cast<u32>(cds.size())); // total entries
    Put32(out, cdSize);
    Put32(out, cdStart);
    Put16(out, 0);             // comment len
    return out;
}

// ---------------------------------------------------------------------------
// In-memory single-file IFileSystem serving the fixture zip under a fixed name.
class MemFile : public shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* d) : d_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = d_->size() - pos_;
        std::size_t take = n < avail ? n : avail;
        std::memcpy(dst, d_->data() + pos_, take);
        pos_ += take;
        return take;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == SEEK_CUR ? static_cast<std::int64_t>(pos_)
                          : whence == SEEK_END ? static_cast<std::int64_t>(d_->size())
                                               : 0;
        std::int64_t np = base + off;
        if (np < 0) np = 0;
        if (np > static_cast<std::int64_t>(d_->size())) np = static_cast<std::int64_t>(d_->size());
        pos_ = static_cast<std::size_t>(np);
        return pos_;
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }
    std::int64_t size() override { return static_cast<std::int64_t>(d_->size()); }
private:
    const std::vector<u8>* d_;
    std::size_t pos_ = 0;
};

class MemFs : public shim::IFileSystem {
public:
    MemFs(std::string name, std::vector<u8> bytes)
        : name_(std::move(name)), bytes_(std::move(bytes)) {}
    shim::IFile* open(const char* path, const char* /*mode*/) override {
        if (path && name_ == path) return new MemFile(&bytes_);
        return nullptr;
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return path && name_ == path; }
private:
    std::string name_;
    std::vector<u8> bytes_;
};

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

} // namespace

// ---------------------------------------------------------------------------
TEST(RealTextDriverIntegration, MountFixtureThenDriveLoaderAndResolve) {
    // Two .res members tiling the array: A at base 0 (2 entries), B at base 2.
    std::vector<u8> resA = BuildRes(0, {{"K_A0", "Alpha", 0xFF},
                                        {"K_A1", "Beta",  0xFF}});
    std::vector<u8> resB = BuildRes(2, {{"K_B0", "Gamma", 0xFF},
                                        {"K_B1", "Delta", 0x0A}}); // random-tag marker

    std::vector<u8> zip = BuildStoredZip({
        {"Text_A.res", resA},
        {"Text_B.res", resB},
    });

    const std::string member = "Resources/textbin_fixture.BIN";
    MemFs fs(member, std::move(zip));

    // Wire the driver against a real ArchiveMount, packaged as the RealGameAssets
    // the driver consumes (so we exercise the real boot's data shape).
    guild::app::RealGameAssets assets;
    guild::app::RealGameAssets::MountedArchive ma;
    ma.name = member;
    ma.mount = std::make_unique<io::ArchiveMount>();
    ma.mounted = ma.mount->Mount(&fs, member.c_str(), /*caseInsensitive=*/false);
    CHECK(ma.mounted);
    ma.memberCount = ma.mount->memberCount();
    assets.archives.push_back(std::move(ma));

    TextDb db;
    auto res = guild::app::LoadRealTextDb(assets, member, db);

    CHECK(res.archiveMounted);
    CHECK_EQ(res.resMembers, 2u);
    CHECK_EQ(res.resLoaded, 2u);
    CHECK_EQ(res.entryCount, 4);     // tiled [0..3]
    CHECK_EQ(db.Count(), 4);

    // Resolve a mix of present + absent keys end to end through the driver helper.
    auto resolved = guild::app::ResolveStringKeys(
        db, {"K_A0", "k_b1", "K_NOPE"});
    CHECK_EQ(resolved.size(), 3u);
    CHECK(resolved[0].found);
    CHECK(resolved[0].text == "Alpha");
    CHECK(resolved[1].found);                 // case-folded
    CHECK(resolved[1].text == "Delta");
    CHECK_EQ(resolved[1].tag, 0x0Au);         // the {rN} tag survived the round trip
    CHECK(!resolved[2].found);
}

// ---------------------------------------------------------------------------
// GUARDED real-asset wiring: drive LoadRealTextDb over the REAL textbin_deutsch.BIN
// mounted by MountRealGameAssets. Skips cleanly when the install is absent.
TEST(RealTextDriverIntegration, RealDeutschArchiveOverMountRealGameAssets) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/textbin_deutsch.BIN")) { CHECK(true); return; }

    guild::app::RealGameAssets assets = guild::app::MountRealGameAssets(&fs, GameDir());
    CHECK(assets.vfsBound);

    TextDb db;
    auto res = guild::app::LoadRealTextDb(assets, "Resources/textbin_deutsch.BIN", db);
    CHECK(res.archiveMounted);
    CHECK(res.resMembers >= 100u);          // ~101 compiled .res members
    CHECK_EQ(res.resLoaded, res.resMembers);
    CHECK(res.entryCount > 10000);          // the tiled global text array

    // A known key from Text_N_Nachrichten.res resolves to a non-empty string.
    auto resolved = guild::app::ResolveStringKeys(db, {"_NEV_DORTHIN+0"});
    CHECK(resolved[0].found);
    CHECK(!resolved[0].text.empty());

    io::VfsShutdown();
}
