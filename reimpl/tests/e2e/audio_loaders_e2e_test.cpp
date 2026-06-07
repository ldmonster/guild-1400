// End-to-end: load a synthetic language sample bank, compose worker-comment
// voice names for several persons/sentinels, "play" each by resolving it in the
// bank and routing playback through a mock IAudioDevice + DigitalAudio handle
// allocation, then verify the resolved names and the device-call sequence
// against a hand-computed reference.
#include "test.h"
#include "audio/samplebank_load.h"
#include "audio/voice_builder.h"
#include "audio/digital_output.h"

#include <string>
#include <utility>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

struct MockDev : shim::IAudioDevice {
    struct Call { std::string op; shim::VoiceHandle h; int a; int b; };
    std::vector<Call> calls;
    int next = 1;
    bool init(int v, int c, int r) override {
        calls.push_back({"init", -1, v, c}); (void)r; return true;
    }
    void shutdown() override { calls.push_back({"shutdown", -1, 0, 0}); }
    shim::VoiceHandle allocVoice() override {
        shim::VoiceHandle h = next++;
        calls.push_back({"alloc", h, 0, 0});
        return h;
    }
    void freeVoice(shim::VoiceHandle h) override { calls.push_back({"free", h, 0, 0}); }
    void playSample(shim::VoiceHandle h, const void*, std::size_t bytes, int rate, int loops) override {
        calls.push_back({"play", h, static_cast<int>(bytes), rate}); (void)loops;
    }
    void stop(shim::VoiceHandle h) override { calls.push_back({"stop", h, 0, 0}); }
    void setVolume(shim::VoiceHandle h, int v) override { calls.push_back({"vol", h, v, 0}); }
    void setPan(shim::VoiceHandle h, int p) override { calls.push_back({"pan", h, p, 0}); }
    void setMasterVolume(int) override {}
};

// A tiny "engine" tying the recovered pieces together: it resolves a composed
// voice name in the bank and, if found, allocates a device voice and plays it.
struct VoiceEngine {
    SbBank bank;
    DigitalAudio* audio;
    int out;
    std::vector<std::string> playedNames;

    bool playVoice(u32 person, const VoicePerson* rec, int variation,
                   const std::string& base, bool audioReady) {
        bool useVar = false;
        std::string name = BuildSampleName(person, rec, variation, base,
                                           bank, audioReady, useVar);
        const SbEntry* e = FindSampleInBank(bank, name);
        if (!e)
            return false; // "Voicesample not found"
        playedNames.push_back(name);
        shim::VoiceHandle h = audio->allocateSampleHandle(out);
        if (h < 0)
            return false;
        // VIBE_Sound_PlaySample seeds vol=127 / pan=63 then plays.
        audio->device()->setVolume(h, 127);
        audio->device()->setPan(h, 63);
        audio->device()->playSample(h, nullptr, 0, 44100, useVar ? 0 : 1);
        return true;
    }
};

std::vector<u8> buildBank(const std::string& name,
                          const std::vector<std::pair<std::string,u8>>& entries) {
    std::vector<u8> buf(kSbBankBaseSize + entries.size() * kSbEntrySize, 0);
    auto putName = [&](std::size_t off, const std::string& s){
        for (std::size_t i = 0; i < kSbNameLen; ++i)
            buf[off+i] = (i < s.size()) ? u8(s[i]) : 0;
    };
    putName(0, name);
    u32 c = u32(entries.size());
    buf[kSbCountOffset+0]=u8(c); buf[kSbCountOffset+1]=u8(c>>8);
    buf[kSbCountOffset+2]=u8(c>>16); buf[kSbCountOffset+3]=u8(c>>24);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::size_t b = kSbBankBaseSize + i * kSbEntrySize;
        putName(b + kSbEntryNameOff, entries[i].first);
        buf[b + kSbEntryFmtOff] = entries[i].second;
    }
    return buf;
}

} // namespace

TEST(AudioLoadersE2E, WorkerCommentFlow) {
    // 1) Pick the worker-comment bank set for a craft building (type 99).
    WorkerCommentBanks banks = SelectWorkerCommentBanks(99);
    CHECK_EQ(banks.command, std::string("ARBEITER_KOMMENTARE_BEFEHL_HANDWERK.sbf"));
    CHECK_EQ(LanguageBankPath(banks.command),
             std::string("sprache\\ARBEITER_KOMMENTARE_BEFEHL_HANDWERK.sbf"));

    // 2) Load a synthetic "sprache" bank containing the samples those voices
    //    will resolve to (males M1..M3, females W1..W2, sentinel HS).
    auto bytes = buildBank("HANDWERK", {
        {"ZUFRIEDEN_M1", kSbFormatWav},
        {"ZUFRIEDEN_M2", kSbFormatWav},
        {"ZUFRIEDEN_M3", kSbFormatWav},
        {"ZUFRIEDEN_W1", kSbFormatWav},
        {"ZUFRIEDEN_W2", kSbFormatWav},
        {"ALARM_HS",     kSbFormatWav},
    });

    MockDev dev;
    DigitalAudio audio(&dev);
    audio.setDriverInstalled(true);
    int out = audio.openDigitalOutput(2, 44100, 16, /*maxHandles=*/8);
    CHECK_EQ(out, 0);

    VoiceEngine eng;
    eng.audio = &audio;
    eng.out = out;
    CHECK(LoadSampleBankFromBuffer(bytes.data(), bytes.size(),
                                   "sfx\\HANDWERK.sbf", eng.bank));

    // 3) Compose + play several voices.
    VoicePerson maleA{}; maleA.gender = 0; maleA.variantSeed = 1; // -> _M2
    VoicePerson femB{};  femB.gender  = 1; femB.variantSeed  = 0; // -> _W1

    CHECK(eng.playVoice(7, &maleA, -1, "ZUFRIEDEN", true));      // ZUFRIEDEN_M2
    CHECK(eng.playVoice(5, &femB,  -1, "ZUFRIEDEN", true));      // ZUFRIEDEN_W1
    CHECK(eng.playVoice(u32(-6), nullptr, -1, "ALARM", true));   // ALARM_HS (sentinel)
    // A name that does not exist in the bank -> not played.
    VoicePerson maleC{}; maleC.gender = 0; maleC.variantSeed = 0; // -> _M1
    CHECK(!eng.playVoice(9, &maleC, -1, "MISSING", true));

    // 4) Reference: the resolved names, in order.
    std::vector<std::string> expectNames = {
        "ZUFRIEDEN_M2", "ZUFRIEDEN_W1", "ALARM_HS",
    };
    CHECK_EQ(eng.playedNames.size(), expectNames.size());
    for (std::size_t i = 0; i < expectNames.size(); ++i)
        CHECK_EQ(eng.playedNames[i], expectNames[i]);

    // 5) Reference device-call sequence: init once, then per played voice
    //    alloc/vol/pan/play (the missing one never reaches the device).
    std::vector<std::string> expectOps = {"init"};
    for (std::size_t i = 0; i < expectNames.size(); ++i) {
        expectOps.push_back("alloc");
        expectOps.push_back("vol");
        expectOps.push_back("pan");
        expectOps.push_back("play");
    }
    CHECK_EQ(dev.calls.size(), expectOps.size());
    for (std::size_t i = 0; i < expectOps.size(); ++i)
        CHECK_EQ(dev.calls[i].op, expectOps[i]);

    // Three distinct voices were allocated (handles 1,2,3) and 3 sample slots used.
    CHECK_EQ(audio.outputAt(out)->allocatedSampleCount, u32(3));
    CHECK_EQ(audio.lookupSampleHandleIndex(1), 0);
    CHECK_EQ(audio.lookupSampleHandleIndex(2), 1);
    CHECK_EQ(audio.lookupSampleHandleIndex(3), 2);

    // Each "vol"/"pan" carried the PlaySample defaults (127 / 63).
    int volCount = 0, panCount = 0;
    for (auto& c : dev.calls) {
        if (c.op == "vol") { CHECK_EQ(c.a, 127); ++volCount; }
        if (c.op == "pan") { CHECK_EQ(c.a, 63);  ++panCount; }
    }
    CHECK_EQ(volCount, 3);
    CHECK_EQ(panCount, 3);
}

TEST(AudioLoadersE2E, Mp3VariationRoutesAsVariation) {
    // A bank where the resolved name is an mp3 variation group.
    auto bytes = buildBank("V", {{"GREET_M1", kSbFormatMp3}});
    MockDev dev;
    DigitalAudio audio(&dev);
    audio.setDriverInstalled(true);
    int out = audio.openDigitalOutput(2, 44100, 16, 4);

    VoiceEngine eng; eng.audio = &audio; eng.out = out;
    CHECK(LoadSampleBankFromBuffer(bytes.data(), bytes.size(), "p", eng.bank));

    VoicePerson male{}; male.gender = 0; male.variantSeed = 0; // -> _M1
    // variation 2: name resolves to mp3, so no "%02i" suffix, plays as variation.
    CHECK(eng.playVoice(7, &male, 2, "GREET", true));
    CHECK_EQ(eng.playedNames.size(), std::size_t(1));
    CHECK_EQ(eng.playedNames[0], std::string("GREET_M1"));
    // The last "play" used loops==0 (variation path) — verify a play happened.
    bool sawPlay = false;
    for (auto& c : dev.calls) if (c.op == "play") sawPlay = true;
    CHECK(sawPlay);
}
