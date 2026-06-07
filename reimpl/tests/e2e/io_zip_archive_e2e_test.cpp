#include "test.h"

#include "io/zip_archive.h"
#include "io/archive_mount.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "io_zip_archive_e2e_fixtures.inc"

using namespace guild::io;
using guild::u8;

// In-memory mock IFileSystem (read-only).
namespace {

class MemFile : public guild::shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* d) : data_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t a = data_->size() - pos_;
        if (n > a) n = a;
        if (n) std::memcpy(dst, data_->data() + pos_, n);
        pos_ += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_->size() : 0;
        std::int64_t t = base + off;
        if (t < 0 || (std::size_t)t > data_->size()) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_->size(); }
private:
    const std::vector<u8>* data_; std::size_t pos_ = 0;
};

class MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& p, const u8* d, std::size_t n) {
        files_[p] = std::vector<u8>(d, d + n);
    }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : new MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

bool Eq(const std::vector<u8>& a, const unsigned char* p, unsigned long n) {
    return a.size() == n && (n == 0 || std::memcmp(a.data(), p, n) == 0);
}

} // namespace

// Mount a multi-member .BIN over the mock FS, open several members by path
// through the archive index, and verify decompressed bytes (incl. a >64KB member).
TEST(io_zip_archive_e2e, mount_open_and_verify) {
    MockFs fs;
    fs.add("gamedata/pack.BIN", kE2eBin, kE2eBin_len);

    ArchiveMount mnt;
    CHECK(mnt.Mount(&fs, "gamedata/pack.BIN", /*caseInsensitive*/false));
    CHECK_EQ((int)mnt.memberCount(), 4);

    // open several members by path (normalized: uppercase + forward slashes)
    std::vector<u8> wall, script, mesh, groups;
    CHECK(mnt.OpenMember("TEXTURES/WALL.RAW", wall));
    CHECK(Eq(wall, kWall, kWall_len));

    CHECK(mnt.OpenMember("SCRIPTS/MAIN.TXT", script));
    CHECK(Eq(script, kScript, kScript_len));

    // >64KB incompressible member -> exercises the inflate sliding window
    CHECK(mnt.OpenMember("OBJECTS/TREE.MESH", mesh));
    CHECK_EQ(mesh.size(), (size_t)kMesh_len);
    CHECK(Eq(mesh, kMesh, kMesh_len));

    CHECK(mnt.OpenMember("GROUPS/LIST", groups));
    CHECK(Eq(groups, kGroups, kGroups_len));

    // re-open a member to confirm the cached position is reusable
    std::vector<u8> wall2;
    CHECK(mnt.OpenMember("TEXTURES/WALL.RAW", wall2));
    CHECK(Eq(wall2, kWall, kWall_len));
}

// Direct ZipArchive path: open archive, locate each member by name, extract.
TEST(io_zip_archive_e2e, direct_locate_extract) {
    MockFs fs;
    fs.add("x.BIN", kE2eBin, kE2eBin_len);

    ZipArchive z;
    CHECK(z.Open(&fs, "x.BIN"));
    CHECK_EQ(z.numberEntry(), 4u);

    std::vector<u8> mesh;
    CHECK(z.ExtractByName("objects/tree.mesh", mesh, /*caseSensitive*/true));
    CHECK(Eq(mesh, kMesh, kMesh_len));

    std::vector<u8> script;
    CHECK(z.ExtractByName("scripts/main.txt", script, true));
    CHECK(Eq(script, kScript, kScript_len));
    z.Close();
}

// Case-insensitive mount: names kept as stored, lookups fold case.
TEST(io_zip_archive_e2e, case_insensitive_mount) {
    MockFs fs;
    fs.add("y.BIN", kE2eBin, kE2eBin_len);

    ArchiveMount mnt;
    CHECK(mnt.Mount(&fs, "y.BIN", /*caseInsensitive*/true));
    std::vector<u8> wall;
    CHECK(mnt.OpenMember("textures/wall.raw", wall));   // lowercase lookup
    CHECK(Eq(wall, kWall, kWall_len));
}
