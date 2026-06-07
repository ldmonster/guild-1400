// Unit tests for the portable default shim backends (src/shim_impl/*).
#include "test.h"
#include "shim_impl/null_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"

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
