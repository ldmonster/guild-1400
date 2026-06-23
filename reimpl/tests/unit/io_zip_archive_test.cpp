#include "test.h"

#include "io/zip_archive.h"
#include "io/archive_mount.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "io_zip_archive_fixtures.inc"

using namespace guild::io;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// In-memory mock IFileSystem (read-only), backed by byte buffers.
namespace {

class MemFile : public guild::shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* d) : data_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_->size() - pos_;
        if (n > avail) n = avail;
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
    const std::vector<u8>* data_;
    std::size_t pos_ = 0;
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

// --- recovered record sizes ------------------------------------------------
TEST(io_zip_archive, record_layout) {
    CHECK_EQ(sizeof(ZipFileInfo), (size_t)0x50);
    CHECK_EQ(offsetof(ZipFileInfo, compressionMethod), (size_t)0x0C);
    CHECK_EQ(offsetof(ZipFileInfo, dosDate),           (size_t)0x10);
    CHECK_EQ(offsetof(ZipFileInfo, crc),               (size_t)0x14);
    CHECK_EQ(offsetof(ZipFileInfo, compressedSize),    (size_t)0x18);
    CHECK_EQ(offsetof(ZipFileInfo, uncompressedSize),  (size_t)0x1C);
    CHECK_EQ(offsetof(ZipFileInfo, sizeFilename),      (size_t)0x20);
    CHECK_EQ(offsetof(ZipFileInfo, tmSec),             (size_t)0x38);
    CHECK_EQ((unsigned)kZipLocalSig,   0x04034b50u);
    CHECK_EQ((unsigned)kZipCentralSig, 0x02014b50u);
    CHECK_EQ((unsigned)kZipEocdSig,    0x06054b50u);
}

// --- DOS date/time decode (VIBE_Zip_DecodeDosDateTime) ---------------------
TEST(io_zip_archive, dos_date_decode) {
    // 2021-06-04 12:30:20 packed: date=((2021-1980)<<9)|(6<<5)|4, time=(12<<11)|(30<<5)|(20>>1)
    u32 date = ((2021u - 1980u) << 9) | (6u << 5) | 4u;
    u32 time = (12u << 11) | (30u << 5) | (20u >> 1);
    u32 dos = (date << 16) | time;
    u32 tmu[6];
    ZipDecodeDosDateTime(dos, tmu);
    CHECK_EQ(tmu[0], 20u);          // sec (2 * (dos & 0x1F) == 2*10)
    CHECK_EQ(tmu[1], 30u);          // min
    CHECK_EQ(tmu[2], 12u);          // hour
    CHECK_EQ(tmu[3], 4u);           // mday
    CHECK_EQ(tmu[4], 5u);           // mon (0-based: June -> 5)
    CHECK_EQ(tmu[5], 2021u);        // year
}

// --- central directory parse + member list ---------------------------------
TEST(io_zip_archive, central_dir_member_list) {
    MockFs fs;
    fs.add("game/assets.BIN", kUnitZip, kUnitZip_len);

    ZipArchive z;
    CHECK(z.Open(&fs, "game/assets.BIN"));
    // total entries includes the directory entry "empty_dir/"
    CHECK_EQ(z.numberEntry(), 4u);

    // Walk the central directory and collect non-directory member names.
    std::vector<std::string> names;
    CHECK_EQ(z.GoToFirstFile(), (int)kZipOk);
    do {
        char nm[256]; ZipFileInfo info{};
        CHECK_EQ(z.GetCurrentFileInfo(&info, nm, sizeof(nm)), (int)kZipOk);
        if (info.uncompressedSize != 0)
            names.push_back(nm);
    } while (z.GoToNextFile() == (int)kZipOk);

    CHECK_EQ((int)names.size(), kUnitMemberCount);   // 3 real files
    CHECK_EQ(names[0], std::string("hello.txt"));
    CHECK_EQ(names[1], std::string("data/poem.txt"));
    CHECK_EQ(names[2], std::string("big.bin"));
    z.Close();
}

// --- locate + extract: stored member ---------------------------------------
TEST(io_zip_archive, extract_stored_member) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    std::vector<u8> out;
    CHECK(z.ExtractByName("hello.txt", out, /*caseSensitive*/true));
    CHECK(Eq(out, kHelloBytes, kHelloBytes_len));
    z.Close();
}

// --- locate + extract: deflated member -------------------------------------
TEST(io_zip_archive, extract_deflated_member) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    std::vector<u8> out;
    CHECK(z.ExtractByName("data/poem.txt", out, true));
    CHECK(Eq(out, kPoemBytes, kPoemBytes_len));
    z.Close();
}

// --- extract a >64KB deflated member (exercises the inflate window) --------
TEST(io_zip_archive, extract_large_deflated_member) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    std::vector<u8> out;
    CHECK(z.ExtractByName("big.bin", out, true));
    CHECK_EQ(out.size(), (size_t)kBigBytes_len);
    CHECK(Eq(out, kBigBytes, kBigBytes_len));
    z.Close();
}

// --- case-insensitive locate -----------------------------------------------
TEST(io_zip_archive, locate_case_insensitive) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    std::vector<u8> out;
    // wrong case, case-insensitive compare should still find data/poem.txt.
    // (VIBE_Zip_CompareFileName folds case but does NOT translate '\\'->'/'; slash
    // normalization is the mount/tree layer's job — see ArchiveMount.)
    CHECK(z.ExtractByName("DATA/POEM.TXT", out, /*caseSensitive*/false));
    CHECK(Eq(out, kPoemBytes, kPoemBytes_len));
    z.Close();
}

// --- missing member fails and restores position ----------------------------
TEST(io_zip_archive, locate_missing) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    CHECK_EQ(z.LocateFileByName("nope.txt", false), (int)kZipEndOfList);
    // archive still usable
    std::vector<u8> out;
    CHECK(z.ExtractByName("hello.txt", out, true));
    z.Close();
}

// --- ArchiveMount: in-memory member index ----------------------------------
TEST(io_zip_archive, mount_member_index) {
    MockFs fs; fs.add("pack.BIN", kUnitZip, kUnitZip_len);
    ArchiveMount mnt;
    CHECK(mnt.Mount(&fs, "pack.BIN", /*caseInsensitive*/false));
    // directory entry excluded -> 3 indexed members
    CHECK_EQ((int)mnt.memberCount(), kUnitMemberCount);
    // names normalized to uppercase + forward slashes
    for (int i = 0; i < kUnitMemberCount; ++i)
        CHECK(mnt.Find(kUnitMemberNames[i]) != nullptr);

    // extract through the cached central-dir position
    std::vector<u8> out;
    CHECK(mnt.OpenMember("HELLO.TXT", out));
    CHECK(Eq(out, kHelloBytes, kHelloBytes_len));
    CHECK(mnt.OpenMember("BIG.BIN", out));
    CHECK(Eq(out, kBigBytes, kBigBytes_len));

    // the mount normalizes lookups: backslash + mixed case resolve to the same
    // member (VIBE_Path_ConvertBackslashToSlash applied at mount + lookup).
    CHECK(mnt.Find("data\\poem.txt") != nullptr);
    CHECK(mnt.OpenMember("data\\poem.txt", out));
    CHECK(Eq(out, kPoemBytes, kPoemBytes_len));
}

// --- GetCurrentFilePosition round-trips via SetCurrentFilePosition ----------
TEST(io_zip_archive, cached_position_roundtrip) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    // advance to the deflated member, cache its position
    CHECK_EQ(z.LocateFileByName("data/poem.txt", true), (int)kZipOk);
    u32 pos = 0, num = 0;
    CHECK_EQ(z.GetCurrentFilePosition(&pos, &num), (int)kZipOk);
    CHECK(pos != 0);
    // move away, then restore
    CHECK_EQ(z.GoToFirstFile(), (int)kZipOk);
    CHECK_EQ(z.SetCurrentFilePosition(pos, num), (int)kZipOk);
    std::vector<u8> out;
    CHECK(z.ExtractCurrentFile(out));
    CHECK(Eq(out, kPoemBytes, kPoemBytes_len));
    z.Close();
}

// ===========================================================================
// Wave-11 hardening: malformed / truncated / oversized inputs. None of these
// must read or write out of bounds; every one must fail safely (Open()==false
// or extract/locate returns an error). The valid-asset path above is unchanged.
// ===========================================================================

// 0-byte archive: no EOCD, Open must fail (and not touch file_.data()).
TEST(io_zip_archive, malformed_empty_archive) {
    MockFs fs; static const u8 empty[1] = {0};
    fs.add("e.BIN", empty, 0);
    ZipArchive z;
    CHECK(!z.Open(&fs, "e.BIN"));
    CHECK(!z.isOpen());
    std::vector<u8> out;
    CHECK(!z.ExtractCurrentFile(out));        // not open -> false, no OOB
}

// 1-byte and a few-byte buffers: FindEndOfCentralDir's maxBack<=4 guard fires.
TEST(io_zip_archive, malformed_tiny_archives) {
    for (std::size_t n = 1; n <= 8; ++n) {
        std::vector<u8> buf(n, (u8)0x50);     // all 'P' — no real signature
        MockFs fs; fs.add("t.BIN", buf.data(), buf.size());
        ZipArchive z;
        CHECK(!z.Open(&fs, "t.BIN"));
        CHECK(!z.isOpen());
    }
}

// Header-only: just a local-file-header signature, no central dir / EOCD.
TEST(io_zip_archive, malformed_header_only) {
    static const u8 hdr[] = {0x50,0x4B,0x03,0x04, 0x14,0,0,0, 0,0,0,0};
    MockFs fs; fs.add("h.BIN", hdr, sizeof(hdr));
    ZipArchive z;
    CHECK(!z.Open(&fs, "h.BIN"));
}

// EOCD signature present but the record is truncated (fewer than 22 bytes after
// the signature) — the ReadShort/ReadLong run off the buffer and Open fails.
TEST(io_zip_archive, malformed_truncated_eocd) {
    // "PK\5\6" then only 4 of the 18 trailing bytes.
    static const u8 trunc[] = {0x50,0x4B,0x05,0x06, 0,0,0,0};
    MockFs fs; fs.add("te.BIN", trunc, sizeof(trunc));
    ZipArchive z;
    CHECK(!z.Open(&fs, "te.BIN"));
}

// Valid EOCD record but central-dir offset+size point past EOF: Open's sanity
// check (eocd < off+size) rejects it; if overflow slips it, Seek bounds-checks.
TEST(io_zip_archive, malformed_bad_central_dir_offset) {
    // Build a minimal 22-byte EOCD claiming 1 entry, size_cd=0x1000, off_cd=0x7000.
    u8 b[22] = {0};
    b[0]=0x50; b[1]=0x4B; b[2]=0x05; b[3]=0x06;       // signature
    // disk numbers = 0 ; entries this disk / total = 1
    b[8]=1; b[10]=1;
    // size of central dir (LE u32) = 0x1000
    b[12]=0x00; b[13]=0x10; b[14]=0; b[15]=0;
    // offset of central dir (LE u32) = 0x7000 (well past this 22-byte file)
    b[16]=0x00; b[17]=0x70; b[18]=0; b[19]=0;
    MockFs fs; fs.add("bo.BIN", b, sizeof(b));
    ZipArchive z;
    CHECK(!z.Open(&fs, "bo.BIN"));
}

// EOCD declares a central-dir count larger than what the file holds. Walking the
// directory must terminate (Seek fails -> ReadCentralDirEntry errors) without OOB.
TEST(io_zip_archive, malformed_count_too_large) {
    // EOCD with off_cd=0, size_cd=0 but entries=0xFFFF, placed right at start so
    // byteBeforeZip arithmetic stays in range; GoToFirstFile reads at offset 0
    // which is the EOCD sig (not a central sig) -> kZipBadZipFile, not OOB.
    u8 b[22] = {0};
    b[0]=0x50; b[1]=0x4B; b[2]=0x05; b[3]=0x06;
    b[8]=0xFF; b[9]=0xFF; b[10]=0xFF; b[11]=0xFF;      // entries this disk/total
    MockFs fs; fs.add("ct.BIN", b, sizeof(b));
    ZipArchive z;
    // numEntriesTotal==numEntriesThisDisk so that gate passes; off+size==0<=eocd.
    bool opened = z.Open(&fs, "ct.BIN");
    if (opened) {
        // first central-dir read lands on the EOCD signature -> not a member.
        CHECK(!z.currentFileOk());
        z.Close();
    }
    CHECK(true);   // reaching here without an ASAN abort is the assertion
}

// A member whose declared compressedSize exceeds the archive: extract must fail
// at the dataOff+compSize > file size guard, never reading past the buffer.
TEST(io_zip_archive, malformed_member_size_exceeds_archive) {
    // Take the valid zip, then corrupt the FIRST central-dir entry's compressed
    // size to a huge value. Locate the central sig "PK\1\2" and patch +20..+23.
    std::vector<u8> buf(kUnitZip, kUnitZip + kUnitZip_len);
    std::size_t cd = 0;
    for (std::size_t i = 0; i + 4 <= buf.size(); ++i)
        if (buf[i]==0x50 && buf[i+1]==0x4B && buf[i+2]==0x01 && buf[i+3]==0x02) { cd=i; break; }
    CHECK(cd != 0);
    // central header +20 = compressed size (LE u32)
    buf[cd+20]=0xFF; buf[cd+21]=0xFF; buf[cd+22]=0xFF; buf[cd+23]=0x7F;
    MockFs fs; fs.add("ms.BIN", buf.data(), buf.size());
    ZipArchive z;
    if (z.Open(&fs, "ms.BIN")) {
        std::vector<u8> out;
        CHECK_EQ(z.LocateFileByName("hello.txt", false), (int)kZipOk);
        CHECK(!z.ExtractCurrentFile(out));   // size guard rejects, no OOB read
        z.Close();
    }
}

// Truncated deflate payload: corrupt a deflated member's stored compressedSize so
// the inflate input is cut short; extract must fail (size or CRC mismatch).
TEST(io_zip_archive, malformed_truncated_deflate) {
    MockFs fs; fs.add("d.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "d.BIN"));
    CHECK_EQ(z.LocateFileByName("data/poem.txt", true), (int)kZipOk);
    // Re-mount a copy whose archive is physically truncated mid-deflate-stream.
    std::vector<u8> cut(kUnitZip, kUnitZip + (kUnitZip_len / 2));
    MockFs fs2; fs2.add("d2.BIN", cut.data(), cut.size());
    ZipArchive z2;
    // The truncated file likely has no valid EOCD -> Open fails; if it opens,
    // extraction of any deflated member must still fail safely.
    if (z2.Open(&fs2, "d2.BIN")) {
        std::vector<u8> out;
        if (z2.LocateFileByName("data/poem.txt", true) == (int)kZipOk)
            CHECK(!z2.ExtractCurrentFile(out));
        z2.Close();
    }
    z.Close();
}

// LocateFileByName with an over-long (>=256) name returns PARAMERROR, no overflow.
TEST(io_zip_archive, malformed_overlong_locate_name) {
    MockFs fs; fs.add("a.BIN", kUnitZip, kUnitZip_len);
    ZipArchive z; CHECK(z.Open(&fs, "a.BIN"));
    std::string huge(512, 'x');
    CHECK_EQ(z.LocateFileByName(huge.c_str(), false), (int)kZipParamError);
    // null name -> PARAMERROR
    CHECK_EQ(z.LocateFileByName(nullptr, false), (int)kZipParamError);
    z.Close();
}
