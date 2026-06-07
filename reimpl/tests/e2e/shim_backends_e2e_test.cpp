// E2E: drive several portable shim backends together in one small scenario,
// mimicking how the engine would touch the OS boundary during a frame:
//   - persist a config blob through DiskFileSystem and read it back,
//   - render a deterministic pattern into MemoryGraphicsDevice and capture it,
//   - exchange lockstep command bytes over a LoopbackSocket pair,
//   - tick the HeadlessPlatform clock and pump until a posted quit,
//   - record audio playback calls on NullAudioDevice.
#include "test.h"
#include "shim_impl/null_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace guild::shim;

TEST(ShimBackendsE2E, IntegratedFrameScenario) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "guild_shim_e2e";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 1) Persist a "savegame" header through the disk FS and read it back.
    DiskFileSystem disk(dir.string());
    const std::uint8_t header[] = {'G', 'I', 'L', 'D', 0x01, 0x00, 0x00, 0x00};
    {
        IFile* f = disk.open("save.dat", "wb");
        CHECK(f != nullptr);
        CHECK_EQ(f->write(header, sizeof(header)), sizeof(header));
        disk.close(f);
    }
    CHECK(disk.exists("save.dat"));
    {
        IFile* f = disk.open("save.dat", "rb");
        CHECK(f != nullptr);
        std::uint8_t in[8] = {};
        CHECK_EQ(f->read(in, 8), static_cast<std::size_t>(8));
        CHECK_EQ(std::memcmp(in, header, 8), 0);
        disk.close(f);
    }

    // 2) Render an XOR pattern into the framebuffer and capture via present().
    MemoryGraphicsDevice gfx;
    CHECK(gfx.init(16, 16, 32, false));
    Surface* s = gfx.backbuffer();
    auto* px = static_cast<std::uint32_t*>(s->pixels);
    for (int y = 0; y < s->height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + y * s->pitch);
        for (int x = 0; x < s->width; ++x)
            row[x] = 0xFF000000u | static_cast<std::uint32_t>((x ^ y) & 0xFF);
    }
    gfx.present();
    CHECK_EQ(gfx.presentCount(), 1);
    const auto& frame = gfx.lastPresented();
    CHECK_EQ(frame.size(), static_cast<std::size_t>(16 * 16 * 4));
    // Spot-check the captured corner pixel equals the rendered value.
    const std::uint32_t* cap = reinterpret_cast<const std::uint32_t*>(frame.data());
    CHECK_EQ(cap[0], px[0]);
    CHECK_EQ(cap[16 * 16 - 1], 0xFF000000u | static_cast<std::uint32_t>((15 ^ 15) & 0xFF));

    // 3) Lockstep command exchange over a loopback pair (both directions).
    auto pr = LoopbackSocket::makePair();
    LoopbackSocket& client = *pr.first;
    LoopbackSocket& server = *pr.second;
    const std::uint8_t cmd[] = {0x10, 0x20, 0x30, 0x40};
    CHECK_EQ(client.send(cmd, sizeof(cmd)), static_cast<int>(sizeof(cmd)));
    std::uint8_t got[4] = {};
    CHECK_EQ(server.recv(got, 4), 4);
    CHECK_EQ(std::memcmp(got, cmd, 4), 0);
    // Server acks.
    const std::uint8_t ack[] = {0xAC, 0x00};
    CHECK_EQ(server.send(ack, 2), 2);
    std::uint8_t gotAck[2] = {};
    CHECK_EQ(client.recv(gotAck, 2), 2);
    CHECK_EQ(std::memcmp(gotAck, ack, 2), 0);

    // 4) Tick the platform clock and pump until quit; record an audio call.
    HeadlessPlatform plat;
    NullAudioDevice audio;
    CHECK(audio.init(4, 2, 22050));
    CHECK(plat.createMainWindow("Guild", 16, 16, false));

    std::uint32_t t_start = plat.timeMs();
    VoiceHandle v = audio.allocVoice();
    CHECK(v >= 0);

    int frames = 0;
    while (plat.pumpMessages()) {
        ++frames;
        if (frames == 1) {
            std::uint8_t blip[8] = {};
            audio.playSample(v, blip, sizeof(blip), 22050, 0);
        }
        plat.sleepMs(1);
        if (frames >= 3)
            plat.postQuit(); // quit after a few frames
    }
    CHECK_EQ(frames, 3);
    CHECK(plat.timeMs() >= t_start);

    const auto& calls = audio.calls();
    CHECK_EQ(calls.size(), static_cast<std::size_t>(1));
    CHECK(calls[0].kind == NullAudioDevice::CallKind::Play);
    CHECK_EQ(calls[0].voice, v);

    fs::remove_all(dir, ec);
}
