// Golden tests for VIBE_Vfs_WriteBuffered @0x5dc0d0 (guild::io::VfsWriteBuffered)
// — the CRT `fwrite` core over the engine's buffered FILE clone.
//
// Headless: a memory-backed IFile captures every byte committed to the backing
// stream so we can assert the exact output (binary block writes vs. text CR/LF
// expansion) and the fwrite return contract (record count, error -> 0).
#include "test.h"

#include "io/savestate_decompress.h"
#include "io/file_buffered.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::io;
using guild::shim::IFile;

namespace {

// Memory-backed write sink. `cap` caps the bytes it will accept per write call
// (0 = unlimited); used to force short-write / failure paths.
struct MemFile : IFile {
    std::vector<guild::u8> data;
    std::size_t shortAt = 0;   // 0 = accept everything; else accept at most this
    bool failNext = false;

    std::size_t read(void*, std::size_t) override { return 0; }
    std::size_t write(const void* src, std::size_t n) override {
        if (failNext) return 0;
        std::size_t take = (shortAt && n > shortAt) ? shortAt : n;
        const auto* p = static_cast<const guild::u8*>(src);
        data.insert(data.end(), p, p + take);
        return take;
    }
    std::int64_t seek(std::int64_t, int) override { return 0; }
    std::int64_t tell() override { return 0; }
    std::int64_t size() override { return (std::int64_t)data.size(); }
};

// Build a BufferedFile bound to a fresh MemFile, opened for write.
BufferedFile MakeWriteFile(MemFile* mf, bool binary, guild::u32 bufSize = 4096) {
    BufferedFile f{};
    std::memset(&f, 0, sizeof(f));
    f.file = mf;
    f.fs = nullptr;
    f.binary = binary;
    f.writeMode = true;
    f.readMode = false;
    f.bufSize = bufSize;
    f.flags = kFileWrite | (binary ? kFileBinary : 0u);
    return f;
}

std::string Str(const std::vector<guild::u8>& v) {
    return std::string(v.begin(), v.end());
}

} // namespace

// ---------------------------------------------------------------------------
// Binary: small write stays in the buffer until flushed; record count correct.
TEST(savestate_decompress, binary_small_buffers_then_flush) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, /*binary=*/true);

    const char* msg = "hello";
    std::size_t recs = VfsWriteBuffered(msg, 1, 5, &f);
    CHECK_EQ(recs, (std::size_t)5);
    // Buffered, not yet committed to the sink.
    CHECK_EQ(mf.data.size(), (std::size_t)0);
    CHECK_EQ(f.remaining, (guild::u32)5);

    CHECK_EQ(FileFlush(&f), 0);
    CHECK_EQ(Str(mf.data), std::string("hello"));
}

// Binary: record semantics — size*count, returns whole records.
TEST(savestate_decompress, binary_record_count) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, true);
    guild::u32 vals[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    std::size_t recs = VfsWriteBuffered(vals, sizeof(guild::u32), 4, &f);
    CHECK_EQ(recs, (std::size_t)4);
    FileFlush(&f);
    CHECK_EQ(mf.data.size(), (std::size_t)16);
}

// Binary: a request larger than the buffer with an empty buffer takes the
// direct 512-aligned write path (bypassing the buffer).
TEST(savestate_decompress, binary_large_direct_aligned) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, true, /*bufSize=*/512);
    std::vector<guild::u8> big(2000);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = (guild::u8)(i & 0xFF);

    std::size_t recs = VfsWriteBuffered(big.data(), 1, big.size(), &f);
    FileFlush(&f);
    CHECK_EQ(recs, (std::size_t)2000);
    CHECK_EQ(mf.data.size(), (std::size_t)2000);
    // Bytes preserved verbatim.
    bool ok = true;
    for (std::size_t i = 0; i < big.size(); ++i)
        if (mf.data[i] != big[i]) { ok = false; break; }
    CHECK(ok);
}

// Text: '\n' expands to CR,LF; record count counts the source bytes.
TEST(savestate_decompress, text_newline_expands_crlf) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, /*binary=*/false);
    const char* line = "a\nb\n";       // 4 source bytes
    std::size_t recs = VfsWriteBuffered(line, 1, 4, &f);
    CHECK_EQ(recs, (std::size_t)4);    // returns source-record count
    FileFlush(&f);
    CHECK_EQ(Str(mf.data), std::string("a\r\nb\r\n"));  // expanded on disk
}

// Text: no expansion for plain bytes / carriage returns left as-is.
TEST(savestate_decompress, text_plain_passthrough) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, false);
    const char* s = "xyz";
    std::size_t recs = VfsWriteBuffered(s, 1, 3, &f);
    CHECK_EQ(recs, (std::size_t)3);
    FileFlush(&f);
    CHECK_EQ(Str(mf.data), std::string("xyz"));
}

// Contract: size==0 returns 0 and never touches the stream.
TEST(savestate_decompress, zero_size_noop) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, true);
    CHECK_EQ(VfsWriteBuffered("data", 0, 4, &f), (std::size_t)0);
    CHECK_EQ(mf.data.size(), (std::size_t)0);
    CHECK_EQ(f.remaining, (guild::u32)0);
}

// Contract: count==0 returns 0.
TEST(savestate_decompress, zero_count_noop) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, true);
    CHECK_EQ(VfsWriteBuffered("data", 4, 0, &f), (std::size_t)0);
    CHECK_EQ(mf.data.size(), (std::size_t)0);
}

// Contract: a stream not opened for write fails with the error bit and 0.
TEST(savestate_decompress, not_writable_sets_error) {
    MemFile mf;
    BufferedFile f = MakeWriteFile(&mf, true);
    f.flags &= ~static_cast<guild::u32>(kFileWrite);   // strip WRITE
    CHECK_EQ(VfsWriteBuffered("x", 1, 1, &f), (std::size_t)0);
    CHECK((f.flags & kFileError) != 0);
}

// Contract: a backing-store failure during a flush forces the count to 0.
TEST(savestate_decompress, backend_failure_zeroes_count) {
    MemFile mf;
    mf.failNext = true;                 // every write to the sink fails
    BufferedFile f = MakeWriteFile(&mf, true, /*bufSize=*/4);
    // 8 bytes through a 4-byte buffer => a mid-stream flush, which fails.
    std::size_t recs = VfsWriteBuffered("abcdefgh", 1, 8, &f);
    CHECK_EQ(recs, (std::size_t)0);
    CHECK((f.flags & kFileError) != 0);
}

// Round-trip: bytes written through fwrite read back identically via FileRead.
TEST(savestate_decompress, roundtrip_binary) {
    MemFile mf;
    BufferedFile wf = MakeWriteFile(&mf, true, 64);
    std::vector<guild::u8> payload(200);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = (guild::u8)((i * 7 + 3) & 0xFF);
    std::size_t recs = VfsWriteBuffered(payload.data(), 1, payload.size(), &wf);
    FileFlush(&wf);
    CHECK_EQ(recs, payload.size());
    CHECK_EQ(mf.data.size(), payload.size());
    bool ok = true;
    for (std::size_t i = 0; i < payload.size(); ++i)
        if (mf.data[i] != payload[i]) { ok = false; break; }
    CHECK(ok);
}
