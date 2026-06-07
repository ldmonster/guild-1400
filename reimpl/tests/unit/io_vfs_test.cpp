#include "test.h"

#include "io/vfs.h"
#include "io/file.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "io_vfs_fixtures.inc"

using namespace guild::io;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// MOCK IFileSystem backed by in-memory byte buffers.
namespace {

class MemFile : public guild::shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* data) : data_(data) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_->size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_->data() + pos_, n);
        pos_ += n;
        return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        (void)src; (void)n; return 0;  // read-only mock
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_->size() : 0;
        std::int64_t t = base + off;
        if (t < 0 || (std::size_t)t > data_->size()) return -1;
        pos_ = (std::size_t)t;
        return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_->size(); }
private:
    const std::vector<u8>* data_;
    std::size_t pos_ = 0;
};

class MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& path, const u8* p, std::size_t n) {
        files_[path] = std::vector<u8>(p, p + n);
    }
    guild::shim::IFile* open(const char* path, const char* mode) override {
        (void)mode;
        auto it = files_.find(path);
        if (it == files_.end()) return nullptr;
        return new MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

std::vector<u8> V(const unsigned char* p, unsigned long n) {
    return std::vector<u8>(p, p + n);
}
bool Eq(const std::vector<u8>& a, const unsigned char* p, unsigned long n) {
    return a.size() == n && (n == 0 || std::memcmp(a.data(), p, n) == 0);
}

} // namespace

// --- handle layout (recovered offsets) -------------------------------------
TEST(io_vfs, handle_layout) {
    CHECK_EQ(sizeof(VfsHandle), (size_t)320);
    CHECK_EQ(offsetof(VfsHandle, name),           (size_t)0x000);
    CHECK_EQ(offsetof(VfsHandle, backendStream),  (size_t)0x100);
    CHECK_EQ(offsetof(VfsHandle, memRemaining),   (size_t)0x104);
    CHECK_EQ(offsetof(VfsHandle, memReadTotal),   (size_t)0x108);
    CHECK_EQ(offsetof(VfsHandle, writeDst),       (size_t)0x10C);
    CHECK_EQ(offsetof(VfsHandle, writeRemaining), (size_t)0x110);
    CHECK_EQ(offsetof(VfsHandle, writeTotal),     (size_t)0x114);
    CHECK_EQ(offsetof(VfsHandle, crc),            (size_t)0x138);
    CHECK_EQ(offsetof(VfsHandle, flags),          (size_t)0x13C);
}

// --- loose file: transparent read --------------------------------------------
TEST(io_vfs, loose_read_full) {
    MockFs fs;
    fs.add("plain.dat", kLooseBytes, kLooseBytes_len);
    CHECK(VfsInit(&fs, true));

    VfsHandle* h = VfsOpenFile("plain.dat", "rb");
    CHECK(h != nullptr);
    CHECK((h->flags & kVfsRead) != 0);
    CHECK((h->flags & (kVfsGzip | kVfsZipMember)) == 0);

    std::vector<u8> got(kLooseBytes_len);
    u32 n = VfsReadStream(got.data(), 1, h, kLooseBytes_len);
    CHECK_EQ(n, (u32)kLooseBytes_len);
    CHECK(Eq(got, kLooseBytes, kLooseBytes_len));

    // at EOF a further read returns 0
    u8 tmp;
    CHECK_EQ(VfsReadStream(&tmp, 1, h, 1), (u32)0);
    CHECK_EQ(VfsCloseStream(h), 0);
    VfsShutdown();
}

// --- loose file: seek / tell / partial reads ---------------------------------
TEST(io_vfs, loose_seek_tell_partial) {
    MockFs fs;
    fs.add("plain.dat", kLooseBytes, kLooseBytes_len);
    VfsInit(&fs, true);
    VfsHandle* h = VfsOpenFile("plain.dat", "rb");
    CHECK(h != nullptr);

    CHECK_EQ(VfsTell(h), (long)0);

    // read 100 bytes
    std::vector<u8> a(100);
    CHECK_EQ(VfsReadStream(a.data(), 1, h, 100), (u32)100);
    CHECK_EQ(VfsTell(h), (long)100);
    CHECK(std::memcmp(a.data(), kLooseBytes, 100) == 0);

    // SEEK_SET to 50
    CHECK_EQ(VfsSeek(h, 50, 0), 0);
    CHECK_EQ(VfsTell(h), (long)50);
    u8 b50;
    CHECK_EQ(VfsReadStream(&b50, 1, h, 1), (u32)1);
    CHECK_EQ(b50, kLooseBytes[50]);

    // SEEK_CUR +9
    CHECK_EQ(VfsSeek(h, 9, 1), 0);
    CHECK_EQ(VfsTell(h), (long)60);

    // SEEK_END -7
    CHECK_EQ(VfsSeek(h, -7, 2), 0);
    CHECK_EQ(VfsTell(h), (long)(kLooseBytes_len - 7));
    std::vector<u8> tail(7);
    CHECK_EQ(VfsReadStream(tail.data(), 1, h, 7), (u32)7);
    CHECK(std::memcmp(tail.data(), kLooseBytes + (kLooseBytes_len - 7), 7) == 0);

    // out-of-range seeks fail
    CHECK_EQ(VfsSeek(h, -1, 0), -1);
    CHECK_EQ(VfsSeek(h, 1, 2), -1);

    // read past EOF clamps to remaining
    CHECK_EQ(VfsSeek(h, (long)kLooseBytes_len - 3, 0), 0);
    std::vector<u8> over(100);
    CHECK_EQ(VfsReadStream(over.data(), 1, h, 100), (u32)3);

    VfsCloseStream(h);
    VfsShutdown();
}

// --- gzip: transparent decompression -----------------------------------------
TEST(io_vfs, gzip_transparent) {
    MockFs fs;
    fs.add("packed.gz", kGzBytes, kGzBytes_len);
    VfsInit(&fs, true);

    VfsHandle* h = VfsOpenFile("packed.gz", "rb");
    CHECK(h != nullptr);
    CHECK((h->flags & kVfsGzip) != 0);

    std::vector<u8> got(kGzPlain_len);
    u32 n = VfsReadStream(got.data(), 1, h, kGzPlain_len);
    CHECK_EQ(n, (u32)kGzPlain_len);
    CHECK(Eq(got, kGzPlain, kGzPlain_len));

    // tell reports decompressed position
    CHECK_EQ(VfsTell(h), (long)kGzPlain_len);
    VfsCloseStream(h);
    VfsShutdown();
}

// --- .BIN zip archive: open first member transparently -----------------------
TEST(io_vfs, bin_zip_member) {
    MockFs fs;
    fs.add("levels.BIN", kSoloZip, kSoloZip_len);
    VfsInit(&fs, true);

    VfsHandle* h = VfsOpenFile("levels.BIN", "rb");
    CHECK(h != nullptr);
    CHECK((h->flags & kVfsZipMember) != 0);

    std::vector<u8> got(kLooseBytes_len);
    u32 n = VfsReadStream(got.data(), 1, h, kLooseBytes_len);
    CHECK_EQ(n, (u32)kLooseBytes_len);
    CHECK(Eq(got, kLooseBytes, kLooseBytes_len));
    VfsCloseStream(h);
    VfsShutdown();
}

// --- memory stream: raw read -------------------------------------------------
TEST(io_vfs, memory_stream_raw) {
    MockFs fs;
    VfsInit(&fs, true);
    std::vector<u8> buf = V(kMemC, kMemC_len);
    VfsHandle* h = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "rb");
    CHECK(h != nullptr);
    CHECK((h->flags & kVfsMemory) != 0);

    std::vector<u8> got(kMemC_len);
    CHECK_EQ(VfsReadStream(got.data(), 1, h, kMemC_len), (u32)kMemC_len);
    CHECK(Eq(got, kMemC, kMemC_len));
    VfsCloseStream(h);
    VfsShutdown();
}

// --- memory stream: gzip framed read -----------------------------------------
TEST(io_vfs, memory_stream_gzip) {
    MockFs fs;
    VfsInit(&fs, true);
    std::vector<u8> buf = V(kGzBytes, kGzBytes_len);
    VfsHandle* h = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "rb");
    CHECK(h != nullptr);

    std::vector<u8> got(kGzPlain_len);
    CHECK_EQ(VfsReadStream(got.data(), 1, h, kGzPlain_len), (u32)kGzPlain_len);
    CHECK(Eq(got, kGzPlain, kGzPlain_len));
    VfsCloseStream(h);
    VfsShutdown();
}

// --- memory stream: write into a caller buffer -------------------------------
TEST(io_vfs, memory_stream_write) {
    MockFs fs;
    VfsInit(&fs, true);
    std::vector<u8> dst(64, 0);
    VfsHandle* h = VfsOpenMemoryStream(dst.data(), (u32)dst.size(), "wb");
    CHECK(h != nullptr);
    CHECK((h->flags & kVfsMemory) != 0);
    CHECK((h->flags & kVfsRead) == 0);

    const char* msg = "guildvfs";
    CHECK_EQ(VfsWriteStream(msg, 1, h, 8), (u32)8);
    CHECK_EQ(h->writeTotal, (u32)8);
    CHECK_EQ(VfsTell(h), (long)8);
    CHECK(std::memcmp(dst.data(), msg, 8) == 0);

    // writing past the remaining room clamps
    std::vector<u8> big(100, 0xAB);
    CHECK_EQ(VfsWriteStream(big.data(), 1, h, 100), (u32)(64 - 8));
    CHECK_EQ(h->writeTotal, (u32)64);
    CHECK_EQ(VfsCloseStream(h), 64);
    VfsShutdown();
}

// --- error / guard paths -----------------------------------------------------
TEST(io_vfs, error_guards) {
    MockFs fs;
    fs.add("plain.dat", kLooseBytes, kLooseBytes_len);
    VfsInit(&fs, true);

    // missing file
    CHECK(VfsOpenFile("nope.dat", "rb") == nullptr);
    // null/empty args
    CHECK(VfsOpenFile(nullptr, "rb") == nullptr);
    CHECK(VfsOpenFile("", "rb") == nullptr);

    VfsHandle* h = VfsOpenFile("plain.dat", "rb");
    CHECK(h != nullptr);
    // read with null handle -> -1
    CHECK_EQ(VfsReadStream(nullptr, 1, nullptr, 4), (u32)0xFFFFFFFFu);
    // write to a read-only stream -> -1
    u8 x = 0;
    CHECK_EQ(VfsWriteStream(&x, 1, h, 1), (u32)0xFFFFFFFFu);
    // zero-length read -> 0
    CHECK_EQ(VfsReadStream(&x, 0, h, 0), (u32)0);
    VfsCloseStream(h);
    VfsShutdown();
}

// --- open-count bookkeeping --------------------------------------------------
TEST(io_vfs, open_count) {
    MockFs fs;
    fs.add("a.dat", kMemA, kMemA_len);
    fs.add("b.dat", kMemB, kMemB_len);
    VfsInit(&fs, true);
    CHECK_EQ(VfsOpenCount(), 0);
    VfsHandle* h1 = VfsOpenFile("a.dat", "rb");
    VfsHandle* h2 = VfsOpenFile("b.dat", "rb");
    CHECK_EQ(VfsOpenCount(), 2);
    VfsCloseStream(h1);
    CHECK_EQ(VfsOpenCount(), 1);
    VfsCloseStream(h2);
    CHECK_EQ(VfsOpenCount(), 0);
    VfsShutdown();
}
