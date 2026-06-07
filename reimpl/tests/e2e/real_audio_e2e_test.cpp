// GUARDED real-asset e2e for play::real_audio. Loads a REAL europe_guild_1400
// sample bank (sfx/allgemein.sbf) through the .sbf loader, asserts a real sample
// count > 0, decodes the first PCM sample to interleaved S16 (sane length, real
// rate/channels, non-silent), and submits it to an audio device WITHOUT error.
// With GUILD_HAVE_SDL2 the device is a real SdlAudioDevice under SDL_AUDIODRIVER=
// dummy (proving the SDL2 path accepts the real PCM); otherwise it is the portable
// capturing NullAudioDevice. Skips cleanly when the shipped assets are absent
// (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/real_audio.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/null_audio.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_audio.h"
#include <SDL.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

const char* CodecName(SbCodec c) {
    switch (c) {
        case SbCodec::kPcm: return "PCM";
        case SbCodec::kImaAdpcm: return "IMA-ADPCM";
        case SbCodec::kMp3: return "MP3";
        default: return "unknown";
    }
}

} // namespace

TEST(RealAudioE2E, AllgemeinBankDecodeAndSubmit) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("sfx/allgemein.sbf")) {
        std::printf("[real_audio_e2e] sfx/allgemein.sbf absent under %s -> skip\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    std::vector<u8> fileBytes;
    RealSbBank bank;
    bool loaded = LoadRealSampleBank(fs, "sfx/allgemein.sbf", fileBytes, bank);
    CHECK(loaded);
    if (!loaded) return;

    CHECK(bank.ok);
    std::printf("[real_audio_e2e] bank '%s' : %zu samples (%zu bytes on disk)\n",
                bank.name.c_str(), bank.entries.size(), fileBytes.size());
    CHECK(!bank.entries.empty()); // real sample count > 0

    // Find the first entry we can decode (PCM 16-bit). allgemein's first entry is a
    // variation whose first sub-sample is 44.1kHz stereo 16-bit PCM.
    const RealSbEntry* pcmEntry = nullptr;
    for (const auto& e : bank.entries) {
        std::printf("  - %-28s fmt=%u codec=%-9s %u Hz %u ch %u-bit pcm=%u bytes\n",
                    e.name.c_str(), e.format, CodecName(e.codec),
                    e.sampleRate, e.channels, e.bitsPerSample, e.pcmBytes);
        if (!pcmEntry && e.codec == SbCodec::kPcm && e.bitsPerSample == 16 &&
            e.pcmBytes > 0)
            pcmEntry = &e;
    }
    CHECK(pcmEntry != nullptr);
    if (!pcmEntry) return;

    std::vector<i16> pcm;
    int rate = 0, ch = 0;
    bool decoded = DecodeEntryToPcm(fileBytes, *pcmEntry, pcm, rate, ch);
    CHECK(decoded);
    if (!decoded) return;

    CHECK(rate > 0);
    CHECK(ch >= 1);
    CHECK(!pcm.empty());
    std::size_t frames = pcm.size() / static_cast<std::size_t>(ch);
    bool nonSilent = false;
    for (i16 s : pcm) if (s != 0) { nonSilent = true; break; }
    CHECK(nonSilent);
    std::printf("[real_audio_e2e] decoded '%s': %zu frames @ %d Hz x %d ch "
                "(%zu S16 samples, non-silent=%d)\n",
                pcmEntry->name.c_str(), frames, rate, ch, pcm.size(), (int)nonSilent);

#ifdef GUILD_HAVE_SDL2
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    shim::SdlAudioDevice dev;
    bool inited = dev.init(/*voices*/ 8, /*channels*/ ch, /*rate*/ rate);
    CHECK(inited);
    if (inited && dev.opened()) {
        PlaySfxResult r = PlaySfxByName(dev, fileBytes, bank, pcmEntry->name, /*loops*/ 0);
        CHECK(r.ok);
        CHECK(r.voice >= 0);
        CHECK(r.nonSilent);
        // Pull a block of mixed audio off the device and confirm the real PCM
        // actually mixed (non-zero output) — i.e. the SDL2 path accepted it.
        auto mixed = dev.pullMix(64);
        bool anyOut = false;
        for (auto s : mixed) if (s != 0) { anyOut = true; break; }
        CHECK(anyOut);
        std::printf("[real_audio_e2e] SDL2(dummy) accepted PCM: voice=%d mixed=%zu "
                    "samples anyNonZero=%d\n", r.voice, mixed.size(), (int)anyOut);
        dev.shutdown();
    } else {
        std::printf("[real_audio_e2e] SDL audio device did not open -> "
                    "skipping device submit\n");
    }
#else
    // Portable capturing path: submit to the NullAudioDevice and confirm the PCM
    // bytes reached it via playSample.
    shim::NullAudioDevice dev;
    CHECK(dev.init(/*voices*/ 8, ch, rate));
    PlaySfxResult r = PlaySfxByName(dev, fileBytes, bank, pcmEntry->name, /*loops*/ 0);
    CHECK(r.ok);
    int plays = 0;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play) {
            ++plays;
            CHECK_EQ(c.bytes, pcm.size() * sizeof(i16));
            CHECK_EQ(c.sampleRate, rate);
        }
    CHECK_EQ(plays, 1);
    std::printf("[real_audio_e2e] (no SDL2) NullAudioDevice captured %d play call(s)\n",
                plays);
#endif
}
