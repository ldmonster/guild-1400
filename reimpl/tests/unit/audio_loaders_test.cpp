// Unit tests for the deferred guild::audio loaders / voice builders:
//   - sample-bank (.sbf) buffer parse + find/mp3 (samplebank_load)
//   - RIFF/WAVE header parse + sine tables (soundwave)
//   - worker-voice sample-name composition + bank selection (voice_builder)
//   - digital output / sample-handle alloc + recycle (digital_output)
#include "test.h"
#include "audio/samplebank_load.h"
#include "audio/soundwave.h"
#include "audio/voice_builder.h"
#include "audio/digital_output.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// --- synthetic .sbf builder -------------------------------------------------
// Header is 0x144 bytes (name @0, entryCount @0x134); then count x 0x40 entries
// (name @+4, format @+0x36). Matches LoadSampleBankFromBuffer.
struct SbBuilder {
    std::vector<u8> buf;
    void putName(std::size_t off, const std::string& s, std::size_t max) {
        for (std::size_t i = 0; i < max; ++i)
            buf[off + i] = (i < s.size()) ? static_cast<u8>(s[i]) : 0;
    }
    void putLe32(std::size_t off, u32 v) {
        buf[off+0] = u8(v); buf[off+1] = u8(v>>8);
        buf[off+2] = u8(v>>16); buf[off+3] = u8(v>>24);
    }
    std::vector<u8> build(const std::string& bankName,
                          const std::vector<std::pair<std::string,u8>>& entries) {
        buf.assign(kSbBankBaseSize + entries.size() * kSbEntrySize, 0);
        putName(0, bankName, kSbNameLen);
        putLe32(kSbCountOffset, static_cast<u32>(entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::size_t base = kSbBankBaseSize + i * kSbEntrySize;
            putName(base + kSbEntryNameOff, entries[i].first, kSbNameLen);
            buf[base + kSbEntryFmtOff] = entries[i].second;
        }
        return buf;
    }
};

struct MockDev : shim::IAudioDevice {
    int next = 100;
    int alloced = 0;
    int freed = 0;
    bool init(int, int, int) override { return true; }
    void shutdown() override {}
    shim::VoiceHandle allocVoice() override { ++alloced; return next++; }
    void freeVoice(shim::VoiceHandle) override { ++freed; }
    void playSample(shim::VoiceHandle, const void*, std::size_t, int, int) override {}
    void stop(shim::VoiceHandle) override {}
    void setVolume(shim::VoiceHandle, int) override {}
    void setPan(shim::VoiceHandle, int) override {}
    void setMasterVolume(int) override {}
};

} // namespace

TEST(AudioLoaders, SampleBankParseEntries) {
    SbBuilder b;
    auto bytes = b.build("SYSTEM", {
        {"DOOR_OPEN", kSbFormatWav},
        {"FOOTSTEP",  kSbFormatMp3},
        {"BELL_W1",   kSbFormatWav},
    });
    SbBank bank;
    CHECK(LoadSampleBankFromBuffer(bytes.data(), bytes.size(), "sfx\\SYSTEM.sbf", bank));
    CHECK_EQ(bank.name, std::string("SYSTEM"));
    CHECK_EQ(bank.path, std::string("sfx\\SYSTEM.sbf"));
    CHECK_EQ(bank.entries.size(), std::size_t(3));
    CHECK_EQ(bank.entries[0].name, std::string("DOOR_OPEN"));
    CHECK_EQ(bank.entries[0].format, u8(kSbFormatWav));
    CHECK_EQ(bank.entries[1].name, std::string("FOOTSTEP"));
    CHECK_EQ(bank.entries[1].format, u8(kSbFormatMp3));
    CHECK_EQ(bank.entries[2].name, std::string("BELL_W1"));
    CHECK(bank.loaded);
}

TEST(AudioLoaders, SampleBankTruncatedFails) {
    SbBuilder b;
    auto bytes = b.build("X", {{"A", kSbFormatWav}, {"B", kSbFormatWav}});
    SbBank bank;
    // Chop the last entry: declared count=2 but only 1 entry of bytes present.
    bytes.resize(kSbBankBaseSize + kSbEntrySize);
    CHECK(!LoadSampleBankFromBuffer(bytes.data(), bytes.size(), "x.sbf", bank));
    CHECK_EQ(bank.entries.size(), std::size_t(0));
    // Header shorter than 0x144 also fails.
    std::vector<u8> tiny(10, 0);
    SbBank b2;
    CHECK(!LoadSampleBankFromBuffer(tiny.data(), tiny.size(), "t", b2));
}

TEST(AudioLoaders, SampleBankFindAndMp3) {
    SbBuilder b;
    auto bytes = b.build("BANK", {
        {"GREET_M1", kSbFormatWav},
        {"GREET_M2", kSbFormatMp3},
    });
    SbBank bank;
    CHECK(LoadSampleBankFromBuffer(bytes.data(), bytes.size(), "p", bank));
    // Case-insensitive match (StrCmpNoCaseN).
    CHECK(FindSampleInBank(bank, "greet_m1") != nullptr);
    CHECK(FindSampleInBank(bank, "GREET_M1") != nullptr);
    CHECK(FindSampleInBank(bank, "missing") == nullptr);
    // mp3 detection on the format byte.
    CHECK(!SampleIsMp3(bank, "GREET_M1"));
    CHECK(SampleIsMp3(bank, "GREET_M2"));
    CHECK(!SampleIsMp3(bank, "missing"));
    // audio uninitialized => original returns 1.
    CHECK(SampleIsMp3(bank, "missing", /*audioReady=*/false));
}

TEST(AudioLoaders, WavHeaderParse) {
    // Build a minimal 16-bit stereo 44100 PCM WAV with 8 data bytes.
    std::vector<u8> w;
    auto put32 = [&](u32 v){ w.push_back(u8(v)); w.push_back(u8(v>>8));
                             w.push_back(u8(v>>16)); w.push_back(u8(v>>24)); };
    auto put16 = [&](u16 v){ w.push_back(u8(v)); w.push_back(u8(v>>8)); };
    auto tag = [&](const char* t){ for (int i=0;i<4;++i) w.push_back(u8(t[i])); };
    tag("RIFF"); put32(36 + 8); tag("WAVE");
    tag("fmt "); put32(16);
    put16(1);            // PCM
    put16(2);            // channels
    put32(44100);        // sampleRate
    put32(44100*2*2);    // byteRate
    put16(4);            // blockAlign
    put16(16);           // bits
    tag("data"); put32(8);
    for (int i = 0; i < 8; ++i) w.push_back(u8(i));

    WavHeader h = ParseWavHeader(w.data(), w.size());
    CHECK(h.ok);
    CHECK_EQ(h.audioFormat, u16(1));
    CHECK_EQ(h.numChannels, u16(2));
    CHECK_EQ(h.sampleRate, u32(44100));
    CHECK_EQ(h.byteRate, u32(44100*2*2));
    CHECK_EQ(h.blockAlign, u16(4));
    CHECK_EQ(h.bitsPerSample, u16(16));
    CHECK_EQ(h.dataSize, u32(8));
    CHECK_EQ(h.dataOffset, std::size_t(0x2C));
}

TEST(AudioLoaders, WavHeaderSkipsExtraChunk) {
    std::vector<u8> w;
    auto put32 = [&](u32 v){ w.push_back(u8(v)); w.push_back(u8(v>>8));
                             w.push_back(u8(v>>16)); w.push_back(u8(v>>24)); };
    auto put16 = [&](u16 v){ w.push_back(u8(v)); w.push_back(u8(v>>8)); };
    auto tag = [&](const char* t){ for (int i=0;i<4;++i) w.push_back(u8(t[i])); };
    tag("RIFF"); put32(100); tag("WAVE");
    tag("fmt "); put32(16);
    put16(1); put16(1); put32(22050); put32(22050); put16(1); put16(8);
    // A "fact" chunk before "data" (4 bytes) — must be skipped.
    tag("fact"); put32(4); put32(0xDEADBEEF);
    tag("data"); put32(3); w.push_back(1); w.push_back(2); w.push_back(3);

    WavHeader h = ParseWavHeader(w.data(), w.size());
    CHECK(h.ok);
    CHECK_EQ(h.numChannels, u16(1));
    CHECK_EQ(h.sampleRate, u32(22050));
    CHECK_EQ(h.bitsPerSample, u16(8));
    CHECK_EQ(h.dataSize, u32(3));
}

TEST(AudioLoaders, WavHeaderRejectsBad) {
    std::vector<u8> junk(64, 0);
    CHECK(!ParseWavHeader(junk.data(), junk.size()).ok);
    CHECK(!ParseWavHeader(nullptr, 0).ok);
}

TEST(AudioLoaders, SineTables) {
    SineTables t = InitSineTables(8);
    CHECK(t.valid());
    CHECK_EQ(t.count, u16(8));
    CHECK_EQ(t.sine.size(), std::size_t(8));
    CHECK_EQ(t.aux1.size(), std::size_t(8));
    // table[k] == sin(k * 2pi/8); aux tables zeroed.
    float step = 6.2831855f / 8.0f;
    for (int k = 0; k < 8; ++k) {
        CHECK(std::fabs(t.sine[k] - std::sin(step * k)) < 1e-5f);
        CHECK_EQ(t.aux1[k], 0.0f);
        CHECK_EQ(t.aux2[k], 0.0f);
    }
    // n <= 4 => invalid.
    CHECK(!InitSineTables(4).valid());
    CHECK(!InitSineTables(0).valid());
}

TEST(AudioLoaders, VoiceSuffixSentinels) {
    bool only = false;
    CHECK_EQ(BuildVoiceSuffix(u32(-2), nullptr, only), std::string("_W1"));
    CHECK_EQ(BuildVoiceSuffix(u32(-1), nullptr, only), std::string("_W2"));
    CHECK_EQ(BuildVoiceSuffix(u32(-5), nullptr, only), std::string("_M1"));
    CHECK_EQ(BuildVoiceSuffix(u32(-4), nullptr, only), std::string("_M2"));
    CHECK_EQ(BuildVoiceSuffix(u32(-3), nullptr, only), std::string("_M3"));
    CHECK_EQ(BuildVoiceSuffix(u32(-6), nullptr, only), std::string("_HS"));
    CHECK_EQ(BuildVoiceSuffix(u32(-8), nullptr, only), std::string(""));
    CHECK(!only);
    // -7 => only base, empty suffix.
    CHECK_EQ(BuildVoiceSuffix(u32(-7), nullptr, only), std::string(""));
    CHECK(only);
}

TEST(AudioLoaders, VoiceSuffixPerson) {
    bool only = false;
    VoicePerson female{}; female.gender = 1; female.variantSeed = 0;
    CHECK_EQ(BuildVoiceSuffix(5, &female, only), std::string("_W1"));
    female.variantSeed = 1;
    CHECK_EQ(BuildVoiceSuffix(5, &female, only), std::string("_W2"));
    female.variantSeed = 2; // &1 -> 0 -> _W1
    CHECK_EQ(BuildVoiceSuffix(5, &female, only), std::string("_W1"));

    VoicePerson male{}; male.gender = 0;
    male.variantSeed = 0; CHECK_EQ(BuildVoiceSuffix(7, &male, only), std::string("_M1"));
    male.variantSeed = 1; CHECK_EQ(BuildVoiceSuffix(7, &male, only), std::string("_M2"));
    male.variantSeed = 2; CHECK_EQ(BuildVoiceSuffix(7, &male, only), std::string("_M3"));
    male.variantSeed = 3; CHECK_EQ(BuildVoiceSuffix(7, &male, only), std::string("_M1"));
}

TEST(AudioLoaders, ComposeSampleName) {
    SbBank empty; // no entries => SampleIsMp3 always false
    bool useVar = false;
    VoicePerson male{}; male.gender = 0; male.variantSeed = 0;
    // person index, male variant 0, no variation -> base + "_M1"
    CHECK_EQ(BuildSampleName(7, &male, -1, "GREET", empty, true, useVar),
             std::string("GREET_M1"));
    CHECK(!useVar);
    // with variation 3, not mp3 -> append "03"
    CHECK_EQ(BuildSampleName(7, &male, 3, "GREET", empty, true, useVar),
             std::string("GREET_M103"));
    // sentinel -7 -> base only, variation ignored
    CHECK_EQ(BuildSampleName(u32(-7), nullptr, 5, "RAW", empty, true, useVar),
             std::string("RAW"));
    // sentinel _HS with variation 12 -> "BASE_HS12"
    CHECK_EQ(BuildSampleName(u32(-6), nullptr, 12, "BASE", empty, true, useVar),
             std::string("BASE_HS12"));
}

TEST(AudioLoaders, ComposeSampleNameMp3Variation) {
    // A bank where "VAR_M1" is an mp3 variation group.
    SbBuilder b;
    auto bytes = b.build("V", {{"VAR_M1", kSbFormatMp3}});
    SbBank bank;
    CHECK(LoadSampleBankFromBuffer(bytes.data(), bytes.size(), "p", bank));
    bool useVar = false;
    VoicePerson male{}; male.gender = 0; male.variantSeed = 0;
    // variation 2, name resolves to mp3 -> no "%02i" append, useVariation set.
    std::string n = BuildSampleName(7, &male, 2, "VAR", bank, true, useVar);
    CHECK_EQ(n, std::string("VAR_M1"));
    CHECK(useVar);
}

TEST(AudioLoaders, WorkerCommentBankSelection) {
    auto thieves = SelectWorkerCommentBanks(4);
    CHECK_EQ(thieves.command, std::string("ARBEITER_KOMMENTARE_BEFEHL_DIEBE.sbf"));
    CHECK_EQ(thieves.click,   std::string("ARBEITER_KOMMENTARE_KLICK_DIEBE.sbf"));
    CHECK_EQ(thieves.noise,   std::string("ARBEITER_KOMMENTARE_KLICK_GERAEUSCHE.sbf"));
    CHECK_EQ(thieves.greeting, std::string("")); // type 4 has no greeting

    auto robbers = SelectWorkerCommentBanks(16);
    CHECK_EQ(robbers.command, std::string("ARBEITER_KOMMENTARE_BEFEHL_RAEUBER.sbf"));

    auto soldiers = SelectWorkerCommentBanks(19);
    CHECK_EQ(soldiers.click, std::string("ARBEITER_KOMMENTARE_KLICK_SOELDNER.sbf"));

    auto craft = SelectWorkerCommentBanks(99);
    CHECK_EQ(craft.command, std::string("ARBEITER_KOMMENTARE_BEFEHL_HANDWERK.sbf"));

    // greetings by type
    CHECK_EQ(SelectWorkerCommentBanks(7).greeting, std::string("BEGRUESSUNG_KIRCHE.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(5).greeting, std::string("BEGRUESSUNG_GELDLEIHE.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(14).greeting,
             std::string("BEGRUESSUNG_KRAEUTERLADEN_PARFUEMERIE.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(9).greeting, std::string("BEGRUESSUNG_LAGERHAUS.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(20).greeting,
             std::string("BEGRUESSUNG_SCHMIEDE_STEIMETZ_TISCHLER.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(12).greeting,
             std::string("BEGRUESSUNG_WALDSTUECK_STEINBRUCH_MINE.sbf"));
    CHECK_EQ(SelectWorkerCommentBanks(22).greeting, std::string("BEGRUESSUNG_WIRTSHAUS.sbf"));
}

TEST(AudioLoaders, LanguageBankPath) {
    CHECK_EQ(LanguageBankPath("FOO.sbf"), std::string("sprache\\FOO.sbf"));
}

TEST(AudioLoaders, DigitalOutputOpenAndAlloc) {
    MockDev dev;
    DigitalAudio audio(&dev);
    // No driver => open fails.
    CHECK_EQ(audio.openDigitalOutput(2, 44100, 16), -1);

    audio.setDriverInstalled(true);
    int out = audio.openDigitalOutput(2, 44100, 16, /*maxHandles=*/4);
    CHECK_EQ(out, 0);
    DigitalOutput* o = audio.outputAt(out);
    CHECK(o != nullptr);
    CHECK_EQ(o->channels, u16(2));
    CHECK_EQ(o->sampleRate, u32(44100));
    CHECK_EQ(o->bitsPerSample, u16(16));
    CHECK_EQ(o->blockAlign, u16(4));            // 2 * (16>>3)
    CHECK_EQ(o->bytesPerSec, u32(2 * 2 * 44100)); // ch * bytes * rate
    CHECK_EQ(o->maxSampleHandles, u32(4));
    CHECK_EQ(o->allocatedSampleCount, u32(0));

    // Allocate up to capacity.
    shim::VoiceHandle h0 = audio.allocateSampleHandle(out);
    shim::VoiceHandle h1 = audio.allocateSampleHandle(out);
    CHECK(h0 >= 0);
    CHECK(h1 >= 0);
    CHECK(h0 != h1);
    CHECK_EQ(audio.outputAt(out)->allocatedSampleCount, u32(2));
    CHECK_EQ(audio.lookupSampleHandleIndex(h0), 0);
    CHECK_EQ(audio.lookupSampleHandleIndex(h1), 1);
    CHECK_EQ(audio.lookupSampleHandleIndex(9999), -1);
}

TEST(AudioLoaders, DigitalOutputRecycle) {
    MockDev dev;
    DigitalAudio audio(&dev);
    audio.setDriverInstalled(true);
    int out = audio.openDigitalOutput(1, 22050, 8, 2);
    CHECK_EQ(out, 0);
    shim::VoiceHandle a = audio.allocateSampleHandle(out);
    shim::VoiceHandle b = audio.allocateSampleHandle(out);
    CHECK(a >= 0 && b >= 0);
    // Capacity 2 reached -> next alloc fails.
    CHECK_EQ(audio.allocateSampleHandle(out), -1);
    // Release one, slot recycles.
    CHECK_EQ(audio.releaseSampleHandle(a), 0);
    CHECK_EQ(dev.freed, 1);
    CHECK_EQ(audio.outputAt(out)->allocatedSampleCount, u32(1));
    shim::VoiceHandle c = audio.allocateSampleHandle(out);
    CHECK(c >= 0);
    CHECK_EQ(audio.lookupSampleHandleIndex(c), 0); // reuses freed slot 0
    // Releasing an unknown handle fails.
    CHECK_EQ(audio.releaseSampleHandle(424242), -1);
}

TEST(AudioLoaders, DigitalStreamOpenStart) {
    MockDev dev;
    DigitalAudio audio(&dev);
    audio.setDriverInstalled(true);
    int out = audio.openDigitalOutput(2, 44100, 16, 4);
    CHECK_EQ(out, 0);
    shim::VoiceHandle s = audio.openStream(out);
    CHECK(s >= 0);
    CHECK_EQ(audio.startStream(s), 0);
    CHECK_EQ(audio.startStream(987654), -1); // unknown stream
}
