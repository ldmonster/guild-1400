// Unit tests for the portable default shim backends (src/shim_impl/*).
#include "test.h"
#include "shim_impl/null_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/scripted_platform.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace guild::shim;

// ---------------- NullPlatform / HeadlessPlatform ----------------

TEST(ShimBackends, PlatformClockMonotonicAndSleep) {
    NullPlatform p;
    std::uint32_t t0 = p.timeMs();
    p.sleepMs(15);
    std::uint32_t t1 = p.timeMs();
    CHECK(t1 >= t0);          // monotonic
    CHECK(t1 - t0 >= 10);     // sleep actually elapsed (allow scheduler slack)
}

TEST(ShimBackends, PlatformWindowAndQuit) {
    NullPlatform p;
    CHECK(!p.windowCreated());
    CHECK(p.createMainWindow("t", 640, 480, false)); // no-op success
    CHECK(p.windowCreated());
    CHECK(p.pumpMessages());   // true until quit
    p.postQuit();
    CHECK(!p.pumpMessages());
    p.destroyMainWindow();
    CHECK(!p.windowCreated());
}

TEST(ShimBackends, PlatformScriptedInput) {
    HeadlessPlatform p; // alias check
    MouseState m;
    m.x = 123; m.y = 456; m.left = true; m.right = false; m.middle = true;
    p.setMouse(m);
    MouseState out;
    p.getMouse(out);
    CHECK_EQ(out.x, 123);
    CHECK_EQ(out.y, 456);
    CHECK(out.left);
    CHECK(!out.right);
    CHECK(out.middle);

    CHECK(!p.keyDown(65));
    p.setKey(65, true);
    CHECK(p.keyDown(65));
    p.setKey(65, false);
    CHECK(!p.keyDown(65));
}

// ---------------- MemoryGraphicsDevice ----------------

TEST(ShimBackends, GraphicsFramebufferRoundtrip) {
    MemoryGraphicsDevice g;
    CHECK(g.init(8, 4, 32, false));
    Surface* s = g.backbuffer();
    CHECK(s != nullptr);
    CHECK_EQ(s->width, 8);
    CHECK_EQ(s->height, 4);
    CHECK_EQ(s->pitch, 8 * 4);
    CHECK_EQ(s->bpp, 32);
    CHECK(s->pixels != nullptr);

    // Write a recognizable pattern.
    auto* px = static_cast<std::uint32_t*>(s->pixels);
    for (int i = 0; i < 8 * 4; ++i)
        px[i] = 0xDEAD0000u | static_cast<std::uint32_t>(i);

    CHECK(g.lastPresented().empty()); // nothing presented yet
    g.present();
    CHECK_EQ(g.presentCount(), 1);

    const auto& cap = g.lastPresented();
    CHECK_EQ(cap.size(), static_cast<std::size_t>(8 * 4 * 4));
    CHECK_EQ(std::memcmp(cap.data(), s->pixels, cap.size()), 0);

    // Mutating the backbuffer after present must not change the capture.
    px[0] = 0x12345678u;
    const std::uint32_t* cap32 = reinterpret_cast<const std::uint32_t*>(cap.data());
    CHECK_EQ(cap32[0], 0xDEAD0000u);
}

TEST(ShimBackends, GraphicsPaletteAndBadInit) {
    MemoryGraphicsDevice g;
    CHECK(!g.init(0, 4, 32, false));   // bad width
    CHECK(!g.init(8, 4, 24, false));   // unsupported bpp
    CHECK(g.init(4, 4, 8, false));     // paletted mode OK

    std::uint32_t pal[256];
    for (int i = 0; i < 256; ++i)
        pal[i] = 0xFF000000u | static_cast<std::uint32_t>(i * 0x010101);
    g.setPalette(pal);
    CHECK_EQ(g.palette()[0], 0xFF000000u);
    CHECK_EQ(g.palette()[255], (0xFF000000u | static_cast<std::uint32_t>(255 * 0x010101)));
}

// ---------------- NullAudioDevice ----------------

TEST(ShimBackends, AudioCallRecording) {
    NullAudioDevice a;
    CHECK(a.init(2, 2, 22050));
    VoiceHandle v0 = a.allocVoice();
    VoiceHandle v1 = a.allocVoice();
    CHECK(v0 >= 0);
    CHECK(v1 >= 0);
    CHECK(v0 != v1);
    CHECK_EQ(a.liveVoices(), 2);
    CHECK_EQ(a.allocVoice(), -1); // pool exhausted (2 voices)

    std::uint8_t pcm[100] = {};
    a.playSample(v0, pcm, sizeof(pcm), 22050, 1);
    a.setVolume(v0, 90);
    a.setPan(v0, 64);
    a.stop(v0);
    a.setMasterVolume(50);
    a.freeVoice(v0);

    CHECK_EQ(a.masterVolume(), 50);
    CHECK_EQ(a.liveVoices(), 1);

    const auto& calls = a.calls();
    CHECK_EQ(calls.size(), static_cast<std::size_t>(5)); // play,vol,pan,stop,free
    CHECK(calls[0].kind == NullAudioDevice::CallKind::Play);
    CHECK_EQ(calls[0].voice, v0);
    CHECK_EQ(calls[0].bytes, static_cast<std::size_t>(100));
    CHECK_EQ(calls[0].sampleRate, 22050);
    CHECK_EQ(calls[0].loops, 1);
    CHECK(calls[1].kind == NullAudioDevice::CallKind::SetVolume);
    CHECK_EQ(calls[1].value, 90);
    CHECK(calls[2].kind == NullAudioDevice::CallKind::SetPan);
    CHECK_EQ(calls[2].value, 64);
    CHECK(calls[3].kind == NullAudioDevice::CallKind::Stop);
    CHECK(calls[4].kind == NullAudioDevice::CallKind::Free);
}

// ---------------- LoopbackSocket ----------------

TEST(ShimBackends, LoopbackBidirectional) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;
    CHECK(a.connected());
    CHECK(b.connected());

    const char msg[] = "hello";
    CHECK_EQ(a.send(msg, 5), 5);

    char buf[16] = {};
    CHECK_EQ(b.recv(buf, 16), 5); // gets what's available
    CHECK_EQ(std::memcmp(buf, "hello", 5), 0);
    CHECK_EQ(b.recv(buf, 16), 0); // would-block, no data

    // Other direction.
    CHECK_EQ(b.send("XY", 2), 2);
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(a.recv(buf, 16), 2);
    CHECK_EQ(std::memcmp(buf, "XY", 2), 0);
}

TEST(ShimBackends, LoopbackPartialReads) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;

    const char data[] = "ABCDEFGH";
    CHECK_EQ(a.send(data, 8), 8);

    char buf[4] = {};
    CHECK_EQ(b.recv(buf, 3), 3);            // partial
    CHECK_EQ(std::memcmp(buf, "ABC", 3), 0);
    CHECK_EQ(b.recv(buf, 3), 3);
    CHECK_EQ(std::memcmp(buf, "DEF", 3), 0);
    CHECK_EQ(b.recv(buf, 4), 2);            // only 2 left
    CHECK_EQ(std::memcmp(buf, "GH", 2), 0);
    CHECK_EQ(b.recv(buf, 4), 0);            // drained
}

TEST(ShimBackends, LoopbackCloseDisconnectsPeer) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;
    a.close();
    CHECK(!a.connected());
    CHECK(!b.connected());        // peer's inbox went away
    CHECK_EQ(b.send("x", 1), -1); // error: no peer
}

// ---------------- DiskFileSystem ----------------

TEST(ShimBackends, DiskFileSystemRoundtrip) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "guild_shim_disk_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    DiskFileSystem disk(dir.string());
    CHECK(!disk.exists("a.bin"));

    IFile* w = disk.open("a.bin", "wb");
    CHECK(w != nullptr);
    const char payload[] = "0123456789";
    CHECK_EQ(w->write(payload, 10), static_cast<std::size_t>(10));
    CHECK_EQ(w->size(), static_cast<std::int64_t>(10));
    disk.close(w);

    CHECK(disk.exists("a.bin"));

    IFile* r = disk.open("a.bin", "rb");
    CHECK(r != nullptr);
    CHECK_EQ(r->size(), static_cast<std::int64_t>(10));

    char buf[16] = {};
    CHECK_EQ(r->read(buf, 4), static_cast<std::size_t>(4));
    CHECK_EQ(std::memcmp(buf, "0123", 4), 0);
    CHECK_EQ(r->tell(), static_cast<std::int64_t>(4));

    CHECK_EQ(r->seek(2, SEEK_SET), static_cast<std::int64_t>(2));
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(r->read(buf, 3), static_cast<std::size_t>(3));
    CHECK_EQ(std::memcmp(buf, "234", 3), 0);

    CHECK_EQ(r->seek(-1, SEEK_END), static_cast<std::int64_t>(9));
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(r->read(buf, 4), static_cast<std::size_t>(1)); // only 1 byte left
    CHECK_EQ(buf[0], '9');
    disk.close(r);

    CHECK(disk.open("missing.bin", "rb") == nullptr); // no such file

    fs::remove_all(dir, ec);
}

// ---------------- MemFileSystem ----------------

TEST(ShimBackends, MemFileSystemRoundtrip) {
    MemFileSystem mem;
    CHECK(!mem.exists("note.txt"));
    CHECK(mem.open("note.txt", "rb") == nullptr); // read-missing fails

    IFile* w = mem.open("note.txt", "wb");
    CHECK(w != nullptr);
    CHECK_EQ(w->write("abcdef", 6), static_cast<std::size_t>(6));
    mem.close(w);
    CHECK(mem.exists("note.txt"));

    MemFileSystem::Blob got = mem.get("note.txt");
    CHECK_EQ(got.size(), static_cast<std::size_t>(6));
    CHECK_EQ(got[0], static_cast<std::uint8_t>('a'));

    IFile* r = mem.open("note.txt", "rb");
    CHECK(r != nullptr);
    char buf[8] = {};
    CHECK_EQ(r->read(buf, 2), static_cast<std::size_t>(2));
    CHECK_EQ(std::memcmp(buf, "ab", 2), 0);
    CHECK_EQ(r->seek(4, SEEK_SET), static_cast<std::int64_t>(4));
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(r->read(buf, 8), static_cast<std::size_t>(2)); // "ef"
    CHECK_EQ(std::memcmp(buf, "ef", 2), 0);
    mem.close(r);

    // "w" truncates.
    IFile* w2 = mem.open("note.txt", "wb");
    CHECK_EQ(w2->write("Z", 1), static_cast<std::size_t>(1));
    mem.close(w2);
    CHECK_EQ(mem.get("note.txt").size(), static_cast<std::size_t>(1));

    // Append mode.
    IFile* a = mem.open("note.txt", "ab");
    CHECK_EQ(a->tell(), static_cast<std::int64_t>(1)); // positioned at end
    CHECK_EQ(a->write("Y", 1), static_cast<std::size_t>(1));
    mem.close(a);
    CHECK_EQ(mem.get("note.txt").size(), static_cast<std::size_t>(2));
}

// ====================================================================
// WAVE-11 HARDENING EDGE TESTS (ASAN/UBSAN bounds + leak exercise)
// ====================================================================

// ---------------- MemFileSystem edge cases ----------------

// Reading past EOF, on an empty file, and with a 0-byte request must all stay
// in-bounds (no read off the backing vector) and report 0 / partial.
TEST(ShimBackends, MemFileReadPastEofAndEmpty) {
    MemFileSystem mem;
    mem.put("e.bin", {});                 // empty file
    IFile* r = mem.open("e.bin", "rb");
    CHECK(r != nullptr);
    char buf[8] = {};
    CHECK_EQ(r->read(buf, 8), static_cast<std::size_t>(0)); // empty -> 0, no OOB
    CHECK_EQ(r->read(buf, 0), static_cast<std::size_t>(0)); // 0-byte request
    mem.close(r);

    MemFileSystem::Blob data{1, 2, 3, 4};
    mem.put("d.bin", data);
    IFile* r2 = mem.open("d.bin", "rb");
    CHECK(r2 != nullptr);
    // Seek exactly to EOF then read: avail == 0, must return 0 (no past-end read).
    CHECK_EQ(r2->seek(4, SEEK_SET), static_cast<std::int64_t>(4));
    CHECK_EQ(r2->read(buf, 8), static_cast<std::size_t>(0));
    // Seek beyond EOF then read: pos_ >= sz, must return 0 (the early guard).
    CHECK_EQ(r2->seek(1000, SEEK_SET), static_cast<std::int64_t>(1000));
    CHECK_EQ(r2->read(buf, 8), static_cast<std::size_t>(0));
    // Seek to last byte: a 1-byte partial read, exactly in-bounds.
    CHECK_EQ(r2->seek(3, SEEK_SET), static_cast<std::int64_t>(3));
    CHECK_EQ(r2->read(buf, 8), static_cast<std::size_t>(1));
    CHECK_EQ(buf[0], 4);
    mem.close(r2);
}

// Negative seeks must fail and not move the cursor; a SEEK_CUR underflow stays
// put. Reading a NUL-less / arbitrary-byte blob must not assume termination.
TEST(ShimBackends, MemFileNegativeSeekAndRawBytes) {
    MemFileSystem mem;
    MemFileSystem::Blob raw{0xFF, 0x00, 0xAB, 0xFF}; // no NUL terminator semantics
    mem.put("raw.bin", raw);
    IFile* r = mem.open("raw.bin", "rb");
    CHECK(r != nullptr);
    CHECK_EQ(r->seek(-1, SEEK_SET), static_cast<std::int64_t>(-1)); // invalid
    CHECK_EQ(r->tell(), static_cast<std::int64_t>(0));              // unchanged
    CHECK_EQ(r->seek(-5, SEEK_CUR), static_cast<std::int64_t>(-1)); // underflow
    CHECK_EQ(r->tell(), static_cast<std::int64_t>(0));
    CHECK_EQ(r->seek(-99, SEEK_END), static_cast<std::int64_t>(-1));
    CHECK_EQ(r->seek(5, SEEK_END), static_cast<std::int64_t>(9));   // past-end OK
    // Bad whence value -> -1.
    CHECK_EQ(r->seek(0, 999), static_cast<std::int64_t>(-1));
    std::uint8_t out[4] = {};
    CHECK_EQ(r->seek(0, SEEK_SET), static_cast<std::int64_t>(0));
    CHECK_EQ(r->read(out, 4), static_cast<std::size_t>(4));
    CHECK_EQ(out[0], 0xFFu);
    CHECK_EQ(out[1], 0x00u); // embedded NUL preserved
    CHECK_EQ(out[3], 0xFFu);
    mem.close(r);
}

// Writing into a gap created by seeking past EOF must zero-fill the gap and not
// leave the backing vector under-sized (no OOB memcpy at the write offset).
TEST(ShimBackends, MemFileSparseWriteZeroFillsGap) {
    MemFileSystem mem;
    IFile* w = mem.open("sparse.bin", "wb");
    CHECK(w != nullptr);
    CHECK_EQ(w->write("AB", 2), static_cast<std::size_t>(2));
    CHECK_EQ(w->seek(6, SEEK_SET), static_cast<std::int64_t>(6)); // skip 4 bytes
    CHECK_EQ(w->write("CD", 2), static_cast<std::size_t>(2));
    mem.close(w);
    MemFileSystem::Blob b = mem.get("sparse.bin");
    CHECK_EQ(b.size(), static_cast<std::size_t>(8));
    CHECK_EQ(b[0], static_cast<std::uint8_t>('A'));
    CHECK_EQ(b[1], static_cast<std::uint8_t>('B'));
    CHECK_EQ(b[2], static_cast<std::uint8_t>(0)); // gap zero-filled
    CHECK_EQ(b[5], static_cast<std::uint8_t>(0));
    CHECK_EQ(b[6], static_cast<std::uint8_t>('C'));
    CHECK_EQ(b[7], static_cast<std::uint8_t>('D'));
}

// Bad / null paths and bad modes must fail cleanly (return nullptr, no crash);
// writing to a read-only handle and reading from a write-only handle return 0.
TEST(ShimBackends, MemFileBadPathsModesAndPermissions) {
    MemFileSystem mem;
    CHECK(mem.open(nullptr, "rb") == nullptr);
    CHECK(mem.open("x", nullptr) == nullptr);
    CHECK(mem.open("x", "") == nullptr);     // empty mode -> default switch fails
    CHECK(mem.open("x", "q") == nullptr);    // unknown mode char
    CHECK(!mem.exists(nullptr));

    mem.put("ro.bin", MemFileSystem::Blob{1, 2, 3});
    IFile* r = mem.open("ro.bin", "rb");     // read-only
    CHECK(r != nullptr);
    CHECK_EQ(r->write("z", 1), static_cast<std::size_t>(0)); // not writable
    CHECK_EQ(mem.get("ro.bin").size(), static_cast<std::size_t>(3)); // unchanged
    mem.close(r);

    IFile* w = mem.open("wo.bin", "wb");     // write-only
    CHECK(w != nullptr);
    char buf[4] = {};
    CHECK_EQ(w->read(buf, 4), static_cast<std::size_t>(0)); // not readable
    mem.close(w);

    // close(nullptr) must be safe (delete nullptr is a no-op).
    mem.close(nullptr);
}

// ---------------- LoopbackSocket edge cases ----------------

// recv into a 0-length request, an empty (drained) buffer, and after the buffer
// is fully consumed must stay in-bounds and report 0; send of 0 bytes is a no-op.
TEST(ShimBackends, LoopbackEmptyAndZeroLength) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;

    char buf[8] = {};
    CHECK_EQ(b.recv(buf, 8), 0);   // empty inbox -> would-block
    CHECK_EQ(b.recv(buf, 0), 0);   // 0-length request
    CHECK_EQ(a.send("x", 0), 0);   // 0-length send: no-op
    CHECK_EQ(b.recv(buf, 8), 0);   // still empty
    CHECK_EQ(a.send(nullptr, 0), 0); // null data, zero length: safe no-op
}

// A large send followed by many small recvs must drain exactly without reading
// off the deque (ASAN exercises the per-byte pop loop at the boundary).
TEST(ShimBackends, LoopbackLargeFillDrainBoundary) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;

    std::vector<std::uint8_t> big(4096);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<std::uint8_t>(i & 0xFF);
    CHECK_EQ(a.send(big.data(), big.size()), 4096);

    std::vector<std::uint8_t> got;
    std::uint8_t chunk[7];
    for (;;) {
        int n = b.recv(chunk, sizeof(chunk));
        if (n <= 0) break;
        got.insert(got.end(), chunk, chunk + n);
    }
    CHECK_EQ(got.size(), static_cast<std::size_t>(4096));
    CHECK(got == big);                 // every byte, in order, no corruption
    CHECK_EQ(b.recv(chunk, sizeof(chunk)), 0); // drained -> would-block
}

// recv after close must report the closed/error state and never touch the freed
// inbox; send on a closed socket errors; double close is safe.
TEST(ShimBackends, LoopbackRecvSendAfterClose) {
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& a = *pr.first;
    LoopbackSocket& b = *pr.second;

    CHECK_EQ(a.send("hi", 2), 2);
    a.close();
    CHECK(!a.connected());
    char buf[8] = {};
    CHECK_EQ(a.recv(buf, 8), -1);  // own inbox reset -> error, no UAF
    CHECK_EQ(a.send("x", 1), -1);  // no peer
    a.close();                     // double close: safe
    // b still owns its inbox (the 2 bytes a sent) but its peer is gone.
    CHECK(!b.connected());
    CHECK_EQ(b.recv(buf, 8), 2);   // can still drain what was buffered
    CHECK_EQ(std::memcmp(buf, "hi", 2), 0);
}

// ---------------- DiskFileSystem edge cases ----------------

// Opening a nonexistent file for read fails; null path/mode fail; listing a
// missing dir / making a dir under a fresh root behaves. No leaks on failure.
TEST(ShimBackends, DiskFileNonexistentAndBadArgs) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "guild_shim_disk_edge_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    DiskFileSystem disk(dir.string());
    CHECK(disk.open("nope.bin", "rb") == nullptr); // nonexistent read
    CHECK(disk.open(nullptr, "rb") == nullptr);    // null path
    CHECK(disk.open("a.bin", nullptr) == nullptr); // null mode
    CHECK(!disk.exists(nullptr));
    CHECK(disk.listDir("no_such_dir") == nullptr); // missing dir
    CHECK(disk.listDir(nullptr) == nullptr);

    // size()/seek()/tell() must be well-defined; a fresh empty file has size 0.
    IFile* w = disk.open("z.bin", "wb");
    CHECK(w != nullptr);
    CHECK_EQ(w->size(), static_cast<std::int64_t>(0));
    CHECK_EQ(w->tell(), static_cast<std::int64_t>(0));
    disk.close(w);

    // listDir of the (now non-empty) root finds our file and frees cleanly.
    IDirListing* l = disk.listDir(".");
    CHECK(l != nullptr);
    if (l) {
        bool found = false;
        for (std::size_t i = 0; i < l->count(); ++i)
            if (std::string(l->at(i).name) == "z.bin") found = true;
        CHECK(found);
        delete l;
    }
    fs::remove_all(dir, ec);
}

// ---------------- MemoryGraphicsDevice edge cases ----------------

// backbuffer() before init is null; present() before init is a no-op; setPalette
// with null is ignored; re-init resizes the framebuffer safely.
TEST(ShimBackends, GraphicsUninitAndNullPalette) {
    MemoryGraphicsDevice g;
    CHECK(g.backbuffer() == nullptr);  // not inited
    g.present();                       // no-op, must not touch null framebuffer
    CHECK_EQ(g.presentCount(), 0);
    g.setPalette(nullptr);             // ignored, no deref

    CHECK(g.init(2, 2, 32, false));
    CHECK(g.backbuffer() != nullptr);
    // Re-init to a larger surface; backbuffer must reflect the new size in-bounds.
    CHECK(g.init(64, 32, 32, false));
    Surface* s = g.backbuffer();
    CHECK(s != nullptr);
    CHECK_EQ(s->width, 64);
    CHECK_EQ(s->height, 32);
    // Touch the full new framebuffer (ASAN checks the realloc'd extent).
    auto* px = static_cast<std::uint32_t*>(s->pixels);
    for (int i = 0; i < 64 * 32; ++i) px[i] = 0u;
    g.shutdown();
    CHECK(g.backbuffer() == nullptr);  // null again after shutdown
}

// ---------------- ScriptedPlatform edge cases ----------------

// An empty timeline with no quit set never quits and reports the default input
// state; pollText with no queued text returns "" without touching freed storage.
TEST(ShimBackends, ScriptedPlatformEmptyTimeline) {
    ScriptedPlatform p;
    CHECK(p.pollText().empty());
    for (int i = 0; i < 100; ++i)
        CHECK(p.pumpMessages());       // no quit set -> always true
    CHECK_EQ(p.pumps(), 100);
    MouseState m;
    p.getMouse(m);
    CHECK_EQ(m.x, 0);
    CHECK_EQ(m.y, 0);
    CHECK(!m.left);
    CHECK_EQ(m.wheel, 0);              // no wheel queued
    CHECK(!p.keyDown(65));
    CHECK(p.pollText().empty());
}

// quitAfterPumps(0) quits immediately; quitAfterPumps clamps the pump count.
TEST(ShimBackends, ScriptedPlatformQuitBoundaries) {
    ScriptedPlatform p0;
    p0.quitAfterPumps(0);
    CHECK(!p0.pumpMessages());          // quits on the very first pump
    CHECK_EQ(p0.pumps(), 0);

    ScriptedPlatform p3;
    p3.quitAfterPumps(3);
    CHECK(p3.pumpMessages());
    CHECK(p3.pumpMessages());
    CHECK(p3.pumpMessages());
    CHECK(!p3.pumpMessages());          // 4th returns false
    CHECK(!p3.pumpMessages());          // stays false
    CHECK_EQ(p3.pumps(), 3);
}

// An over-long / dense timeline (more steps than pumps, duplicate + far-future
// pump indices, big held-key sets) must apply in-bounds and never index OOB.
TEST(ShimBackends, ScriptedPlatformOverLongTimeline) {
    ScriptedPlatform p;
    // Many steps, including duplicates at the same pump and far-future indices
    // that never fire (we only pump 5 times). Big key vectors stress the set.
    for (int i = 0; i < 10000; ++i)
        p.scriptAt(i, i, i * 2, (i & 1) != 0,
                   std::vector<int>{i % 256, (i + 1) % 256, (i + 2) % 256},
                   std::string("t"), /*wheelOnce=*/(i % 3));
    // Two steps land on pump 2 (additive text/wheel, last mouse/keys win).
    p.scriptAt(2, 999, 888, true, std::vector<int>{42}, std::string("X"), 5);

    p.quitAfterPumps(5);
    int pumps = 0;
    while (p.pumpMessages()) {
        ++pumps;
        MouseState m;
        p.getMouse(m);                 // read-and-clear wheel each frame
        (void)m;
        (void)p.pollText();
    }
    CHECK_EQ(pumps, 5);
    // No crash / no OOB; the platform survived a timeline far larger than the
    // number of pumps. State at this point reflects the last fired step (pump 4).
    CHECK(p.keyDown(4) || p.keyDown(5) || p.keyDown(6)); // pump-4 key set
}
