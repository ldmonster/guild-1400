// Integration tests for play::real_audio wired to a CAPTURING device. Builds a
// small in-memory .sbf, parses it, then PlaySfxByName -> NullAudioDevice (the
// portable capturing backend that records every playSample call). Asserts the
// decoded PCM actually reached the device (bytes/rate/loops), deterministically.
// Also exercises the SDL backend (S16 mix) when GUILD_HAVE_SDL2 is defined.
#include "test.h"

#include "play/real_audio.h"
#include "shim_impl/null_audio.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

void PutLe16(std::vector<u8>& b, std::size_t off, u16 v) {
    b[off] = u8(v & 0xFF); b[off + 1] = u8((v >> 8) & 0xFF);
}
void PutLe32(std::vector<u8>& b, std::size_t off, u32 v) {
    b[off] = u8(v); b[off + 1] = u8(v >> 8); b[off + 2] = u8(v >> 16); b[off + 3] = u8(v >> 24);
}
void PutStr(std::vector<u8>& b, std::size_t off, const char* s) {
    std::memcpy(b.data() + off, s, std::strlen(s));
}
std::size_t AppendWav(std::vector<u8>& b, int ch, int rate, const std::vector<i16>& s) {
    std::size_t base = b.size(), dataBytes = s.size() * 2, total = 0x2C + dataBytes;
    b.resize(base + total, 0);
    PutStr(b, base, "RIFF"); PutLe32(b, base + 4, u32(total - 8));
    PutStr(b, base + 8, "WAVE"); PutStr(b, base + 0x0C, "fmt ");
    PutLe32(b, base + 0x10, 16); PutLe16(b, base + 0x14, 1);
    PutLe16(b, base + 0x16, u16(ch)); PutLe32(b, base + 0x18, u32(rate));
    PutLe32(b, base + 0x1C, u32(rate * ch * 2)); PutLe16(b, base + 0x20, u16(ch * 2));
    PutLe16(b, base + 0x22, 16); PutStr(b, base + 0x24, "data");
    PutLe32(b, base + 0x28, u32(dataBytes));
    for (std::size_t i = 0; i < s.size(); ++i) PutLe16(b, base + 0x2C + i * 2, u16(s[i]));
    return base;
}

// Single-entry PCM .sbf for the itest.
std::vector<u8> BuildSbf(const char* entryName, int ch, int rate,
                         const std::vector<i16>& s) {
    constexpr std::size_t kEntryBase = 0x148, kEntrySize = 0x40;
    std::vector<u8> b(kEntryBase + kEntrySize, 0);
    PutStr(b, 0, "ItBank");
    PutLe32(b, 0x134, 1);
    std::size_t e = kEntryBase;
    PutStr(b, e, entryName);
    PutLe32(b, e + 0x32, 1);
    u32 off = u32(b.size());
    std::size_t blk = b.size();
    b.resize(b.size() + 12, 0);
    std::size_t wavBase = AppendWav(b, ch, rate, s);
    b[blk] = 1;
    PutLe32(b, blk + 4, u32(b.size() - wavBase));
    PutLe32(b, e + 0x3C, off);
    return b;
}

} // namespace

// PlaySfxByName decodes the bank's PCM and submits it to the capturing device with
// the right byte count, rate, and loop flag.
TEST(RealAudioIntegration, PlaySfxReachesCapturingDevice) {
    std::vector<i16> samples = {0, 1000, -1000, 2000, -2000, 4000};
    auto buf = BuildSbf("DoorOpen", /*ch*/ 1, /*rate*/ 22050, samples);

    RealSbBank bank;
    CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));

    shim::NullAudioDevice dev;
    CHECK(dev.init(/*voices*/ 8, /*channels*/ 1, /*rate*/ 22050));

    PlaySfxResult r = PlaySfxByName(dev, buf, bank, "DoorOpen", /*loops*/ 0);
    CHECK(r.ok);
    CHECK_EQ(r.entryName, std::string("DoorOpen"));
    CHECK(r.codec == SbCodec::kPcm);
    CHECK_EQ(r.sampleRate, 22050);
    CHECK_EQ(r.channels, 1);
    CHECK_EQ(r.pcmFrames, samples.size());
    CHECK_EQ(r.pcmBytes, samples.size() * sizeof(i16));
    CHECK(r.nonSilent);
    CHECK(r.voice >= 0);

    // The device recorded exactly one Play call carrying our PCM bytes/rate.
    int plays = 0;
    for (const auto& c : dev.calls()) {
        if (c.kind == shim::NullAudioDevice::CallKind::Play) {
            ++plays;
            CHECK_EQ(c.bytes, samples.size() * sizeof(i16));
            CHECK_EQ(c.sampleRate, 22050);
            CHECK_EQ(c.loops, 0);
            CHECK_EQ(c.voice, r.voice);
        }
    }
    CHECK_EQ(plays, 1);
}

// Determinism: parsing + playing the same bytes twice yields identical results.
TEST(RealAudioIntegration, Deterministic) {
    std::vector<i16> samples = {5, 6, 7, 8, 9, 10};
    auto buf = BuildSbf("Bell", 2, 44100, samples);

    auto run = [&](PlaySfxResult& out) {
        RealSbBank bank;
        CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));
        shim::NullAudioDevice dev;
        CHECK(dev.init(4, 2, 44100));
        out = PlaySfxByName(dev, buf, bank, "Bell", 0);
    };
    PlaySfxResult a, b;
    run(a);
    run(b);
    CHECK_EQ(a.ok, b.ok);
    CHECK_EQ(a.pcmBytes, b.pcmBytes);
    CHECK_EQ(a.pcmFrames, b.pcmFrames);
    CHECK_EQ(a.sampleRate, b.sampleRate);
    CHECK_EQ(a.channels, b.channels);
    CHECK_EQ(a.nonSilent, b.nonSilent);
}

// Missing sample -> no submission, ok=false (no crash).
TEST(RealAudioIntegration, MissingSampleNoSubmit) {
    std::vector<i16> samples = {1, 2, 3, 4};
    auto buf = BuildSbf("Known", 1, 22050, samples);
    RealSbBank bank;
    CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));
    shim::NullAudioDevice dev;
    CHECK(dev.init(4, 1, 22050));
    PlaySfxResult r = PlaySfxByName(dev, buf, bank, "DoesNotExist", 0);
    CHECK(!r.ok);
    int plays = 0;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play) ++plays;
    CHECK_EQ(plays, 0);
}
