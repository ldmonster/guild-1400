// Unit tests for play::real_audio — hand-build a tiny in-memory .sbf (the on-disk
// binary format recovered from VIBE_Sound_LoadSampleBank @0x446b2c) with one
// single-PCM entry and one variation entry, parse it, and assert the entry index,
// codec metadata, and decoded S16 PCM length/values. No filesystem, no device.
#include "test.h"

#include "play/real_audio.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

void PutLe16(std::vector<u8>& b, std::size_t off, u16 v) {
    b[off] = u8(v & 0xFF);
    b[off + 1] = u8((v >> 8) & 0xFF);
}
void PutLe32(std::vector<u8>& b, std::size_t off, u32 v) {
    b[off] = u8(v & 0xFF);
    b[off + 1] = u8((v >> 8) & 0xFF);
    b[off + 2] = u8((v >> 16) & 0xFF);
    b[off + 3] = u8((v >> 24) & 0xFF);
}
void PutStr(std::vector<u8>& b, std::size_t off, const char* s) {
    std::memcpy(b.data() + off, s, std::strlen(s));
}

// Append a complete RIFF/WAVE PCM file (16-bit, `ch` channels, `rate` Hz) carrying
// the given S16 samples; returns the file offset where it was placed.
std::size_t AppendWav(std::vector<u8>& b, int ch, int rate,
                      const std::vector<i16>& samples) {
    std::size_t base = b.size();
    std::size_t dataBytes = samples.size() * 2;
    std::size_t total = 0x2C + dataBytes;
    b.resize(base + total, 0);
    PutStr(b, base + 0x00, "RIFF");
    PutLe32(b, base + 0x04, u32(total - 8));
    PutStr(b, base + 0x08, "WAVE");
    PutStr(b, base + 0x0C, "fmt ");
    PutLe32(b, base + 0x10, 16);
    PutLe16(b, base + 0x14, 1);                 // PCM
    PutLe16(b, base + 0x16, u16(ch));
    PutLe32(b, base + 0x18, u32(rate));
    PutLe32(b, base + 0x1C, u32(rate * ch * 2)); // byteRate
    PutLe16(b, base + 0x20, u16(ch * 2));        // blockAlign
    PutLe16(b, base + 0x22, 16);                 // bits
    PutStr(b, base + 0x24, "data");
    PutLe32(b, base + 0x28, u32(dataBytes));
    for (std::size_t i = 0; i < samples.size(); ++i)
        PutLe16(b, base + 0x2C + i * 2, u16(samples[i]));
    return base;
}

// Build a 2-entry .sbf: entry0 = single PCM block, entry1 = variation (1 sub) PCM.
// Layout per gilde.exe VIBE_Sound_LoadSampleBank @0x446b2c / LoadEntry @0x446830:
// header 0x144 (count u32 @0x134); entries 0x40 each at 0x144; per entry
// +0 u32 data-block offset (the File_Seek target), +4 name(50), +0x36 fmt byte.
std::vector<u8> BuildTinySbf(const std::vector<i16>& s0, const std::vector<i16>& s1,
                             u32& off0, u32& off1) {
    constexpr std::size_t kEntryBase = 0x144;
    constexpr std::size_t kEntrySize = 0x40;
    std::vector<u8> b(kEntryBase + 2 * kEntrySize, 0);
    PutStr(b, 0, "TinyBank");
    PutLe32(b, 0x134, 2); // entryCount

    // ---- entry0: single PCM ----
    std::size_t e0 = kEntryBase;
    PutStr(b, e0 + 4, "ClickSound");
    b[e0 + 0x36] = 1; // format 1
    // data block: 12-byte header {u8 fmt; pad3; u32 size; u32 ptr} then RIFF.
    off0 = u32(b.size());
    std::size_t wavBase0 = 0; // computed after we know the size; lay header then wav
    // reserve 12-byte block header
    std::size_t blk0 = b.size();
    b.resize(b.size() + 12, 0);
    wavBase0 = AppendWav(b, /*ch*/ 1, /*rate*/ 22050, s0);
    b[blk0] = 1;                                   // block fmt
    PutLe32(b, blk0 + 4, u32(b.size() - wavBase0)); // payload size (the whole RIFF)
    PutLe32(b, e0 + 0x00, off0);

    // ---- entry1: variation (subCount 1) PCM ----
    std::size_t e1 = kEntryBase + kEntrySize;
    PutStr(b, e1 + 4, "ExplodeVar");
    b[e1 + 0x36] = 2; // format 2
    off1 = u32(b.size());
    std::size_t blk1 = b.size();
    b.resize(b.size() + 12, 0);     // variation header {subCount; ptr; total}
    PutLe32(b, blk1 + 0, 1);        // subCount = 1
    std::size_t sub = b.size();
    b.resize(b.size() + 12, 0);     // one 12-byte sub-entry {fmt; size; ptr}
    std::size_t wavBase1 = AppendWav(b, /*ch*/ 2, /*rate*/ 44100, s1);
    b[sub] = 1;                                    // sub fmt = wav
    PutLe32(b, sub + 4, u32(b.size() - wavBase1)); // sub payload size
    PutLe32(b, e1 + 0x00, off1);

    return b;
}

} // namespace

TEST(RealAudioUnit, ParseTinyBankIndex) {
    std::vector<i16> s0 = {100, -200, 300, -400, 500, -600};
    std::vector<i16> s1 = {10, 20, -30, -40};
    u32 o0 = 0, o1 = 0;
    auto buf = BuildTinySbf(s0, s1, o0, o1);

    RealSbBank bank;
    CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));
    CHECK(bank.ok);
    CHECK_EQ(bank.name, std::string("TinyBank"));
    CHECK_EQ(bank.entries.size(), (std::size_t)2);

    CHECK_EQ(bank.entries[0].name, std::string("ClickSound"));
    CHECK_EQ(bank.entries[0].format, (u32)1);
    CHECK(bank.entries[0].codec == SbCodec::kPcm);
    CHECK_EQ(bank.entries[0].channels, (u16)1);
    CHECK_EQ(bank.entries[0].sampleRate, (u32)22050);
    CHECK_EQ(bank.entries[0].bitsPerSample, (u16)16);
    CHECK_EQ(bank.entries[0].pcmBytes, (u32)(s0.size() * 2));

    CHECK_EQ(bank.entries[1].name, std::string("ExplodeVar"));
    CHECK_EQ(bank.entries[1].format, (u32)2);
    CHECK_EQ(bank.entries[1].subCount, (u32)1);
    CHECK(bank.entries[1].codec == SbCodec::kPcm);
    CHECK_EQ(bank.entries[1].channels, (u16)2);
    CHECK_EQ(bank.entries[1].sampleRate, (u32)44100);
}

TEST(RealAudioUnit, DecodeSinglePcm) {
    std::vector<i16> s0 = {100, -200, 300, -400, 500, -600};
    std::vector<i16> s1 = {10, 20, -30, -40};
    u32 o0 = 0, o1 = 0;
    auto buf = BuildTinySbf(s0, s1, o0, o1);
    RealSbBank bank;
    CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));

    std::vector<i16> pcm;
    int rate = 0, ch = 0;
    CHECK(DecodeEntryToPcm(buf, bank.entries[0], pcm, rate, ch));
    CHECK_EQ(rate, 22050);
    CHECK_EQ(ch, 1);
    CHECK_EQ(pcm.size(), s0.size());
    bool same = (pcm.size() == s0.size());
    for (std::size_t i = 0; same && i < s0.size(); ++i)
        if (pcm[i] != s0[i]) same = false;
    CHECK(same);
}

TEST(RealAudioUnit, DecodeVariationPcm) {
    std::vector<i16> s0 = {1, 2, 3, 4};
    std::vector<i16> s1 = {10, 20, -30, -40, 50, 60}; // stereo: 3 frames
    u32 o0 = 0, o1 = 0;
    auto buf = BuildTinySbf(s0, s1, o0, o1);
    RealSbBank bank;
    CHECK(ParseRealSampleBank(buf.data(), buf.size(), bank));

    const RealSbEntry* e = FindEntry(bank, "explodevar"); // case-insensitive
    CHECK(e != nullptr);
    if (!e) return;
    std::vector<i16> pcm;
    int rate = 0, ch = 0;
    CHECK(DecodeEntryToPcm(buf, *e, pcm, rate, ch));
    CHECK_EQ(ch, 2);
    CHECK_EQ(rate, 44100);
    CHECK_EQ(pcm.size(), s1.size());
    bool same = (pcm.size() == s1.size());
    for (std::size_t i = 0; same && i < s1.size(); ++i)
        if (pcm[i] != s1[i]) same = false;
    CHECK(same);
}

TEST(RealAudioUnit, TruncatedHeaderRejected) {
    std::vector<u8> tiny(0x100, 0); // shorter than the 0x144 header
    RealSbBank bank;
    CHECK(!ParseRealSampleBank(tiny.data(), tiny.size(), bank));
    CHECK(!bank.ok);
}
