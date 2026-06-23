// GUARDED real-asset e2e for play::SessionAudio — the native city session's
// audio stack over the shipped europe_guild_1400 install:
//   * Init indexes the real include_sfx.ini sample banks (sfx/*.sbf) through
//     the reconstructed loader and faults the REAL market-ambience PCM in
//     (sfx/Lebewesen/athmos.sbf, "Athmo_Marktplatz_Mono_4Bit", IMA ADPCM mono
//     22050 -> decoded S16, capped by the RIFF `fact` frame count),
//   * N session Frames start the market loop (3D positional, radius 2200) and
//     select a real outdoor season track (msx/cd1/*.mp3) through the music
//     director; with GUILD_HAVE_MP3 the track is decoded and audibly streamed.
// Skips cleanly when the install is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/session_audio.h"
#include "audio/music_world.h"
#include "audio/sound3d.h"
#include "crt/rand.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/null_audio.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_audio.h"
#include <SDL.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "europe_guild_1400_original";
}

config::SoundSettings iniSound() {
    // The shipped Gilde.INI [Sound] block.
    config::SoundSettings s;
    s.masterVol = 127;
    s.sfxVol = 114;
    s.msxVol = 50;
    s.speechVol = 127;
    s.msxFreq = 3;
    return s;
}

bool inPool(const std::string& name, const std::array<const char*, 4>& pool) {
    for (const char* p : pool)
        if (name == p)
            return true;
    return false;
}

// The real athmos sample: `fact` = 2578256 frames, mono -> stereo S16 expand.
constexpr std::size_t kMarketFactFrames = 2578256;

} // namespace

TEST(SessionAudioE2E, InitLoadsRealBanksAndMarketAdpcm) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("sfx/Lebewesen/athmos.sbf") || !fs.exists("include_sfx.ini")) {
        std::printf("[session_audio_e2e] install absent under %s -> skip\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::NullAudioDevice dev;
    SessionAudio sa;
    SessionAudioInit opt;
    opt.gameDir = GameDir();
    CHECK(sa.Init(&dev, &fs, iniSound(), opt));
    CHECK(sa.status().soundInited);

    // The include_sfx.ini preload (VIBE_Sound_PreloadFromIncludeFile @0x52f154):
    // the shipped listing names ~80 banks; expect a healthy real load.
    std::printf("[session_audio_e2e] banks=%zu samples=%zu\n",
                sa.status().banksIndexed, sa.status().samplesIndexed);
    CHECK(sa.status().banksIndexed >= 50);
    CHECK(sa.status().samplesIndexed >= 200);

    // The market-ambience PCM (VIBE_Sound_LoadEntry for athmos.sbf): real IMA
    // ADPCM mono 22050 Hz decoded to interleaved stereo S16, `fact`-capped.
    CHECK(sa.status().marketSampleLoaded);
    CHECK_EQ(sa.status().marketSampleRate, 22050);
    CHECK_EQ(sa.status().marketPcmBytes,
             kMarketFactFrames * 2 /*ch*/ * sizeof(i16));

    // The decoded ambience is real audio, not silence.
    audio::SampleRecord* rec =
        sa.sound()->bank().findSampleByName(kMarketAmbienceSample);
    CHECK(rec != nullptr);
    if (rec) {
        bool nonSilent = false;
        for (std::size_t i = 0; i < rec->pcm.size(); ++i) {
            if (rec->pcm[i] != 0) { nonSilent = true; break; }
        }
        CHECK(nonSilent);
    }
    sa.Shutdown();
}

TEST(SessionAudioE2E, SessionFramesPlayMarketLoopAndSeasonTrack) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("sfx/Lebewesen/athmos.sbf") || !fs.exists("msx/CD1") ||
        !fs.exists("include_sfx.ini")) {
        std::printf("[session_audio_e2e] install absent under %s -> skip\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    crt::Srand(1);
    shim::NullAudioDevice dev;
    SessionAudio sa;
    SessionAudioInit opt;
    opt.gameDir = GameDir();
    CHECK(sa.Init(&dev, &fs, iniSound(), opt));

    // Market stall a little off to the listener's right, well inside the 2200
    // radius; listener walks forward a few steps over the ticks.
    const float market[3] = {300.0f, 0.0f, 400.0f};
    sa.SetMarketPosition(market);
    const float fwd[3] = {0.0f, 0.0f, 1.0f};
    for (int t = 1; t <= 5; ++t) {
        float pos[3] = {0.0f, 0.0f, 10.0f * (float)t};
        sa.Frame(pos, fwd, /*locationId=*/0, audio::kSummer, (std::uint32_t)t);
        CHECK(sa.status().lastTick.sound3dRan);
        CHECK(sa.status().lastTick.voicesRan);
    }

    // Market loop: attached, 3D-mixed, audible at this range, device-looped.
    CHECK(sa.status().marketLoopPlaying);
    audio::Sound3dEntry* e = sa.marketLoopEntry();
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->inUse);
        CHECK(e->voice != nullptr);
        const audio::Vec3 lis{0.0f, 0.0f, 50.0f};
        int vol = audio::Compute3dVolume(lis, e->sourcePos, e->radius, e->baseVol);
        std::printf("[session_audio_e2e] market 3D vol @50z = %d\n", vol);
        CHECK(vol > 0);
    }

    // Outdoor music: a real summer-pool track was selected and loaded through
    // the sink (the real msx/cd1 file, resolved case-insensitively).
    CHECK(sa.director().currentTrackHandle != 0);
    CHECK(inPool(sa.musicSink().currentName(), audio::SummerTracks()));
    CHECK(sa.musicSink().loadCount() >= 1);
    std::printf("[session_audio_e2e] outdoor track: %s%s\n",
                sa.musicSink().currentName().c_str(),
                sa.musicSink().streaming() ? " (PCM streaming)" : " (no mp3 decode in this build)");
#ifdef GUILD_HAVE_MP3
    // The real backend decodes the shipped mp3 -> the stream voice carries PCM.
    CHECK(sa.musicSink().streaming());
#endif

    sa.Shutdown();
    CHECK(!sa.status().marketLoopPlaying);
    CHECK(!sa.initialized());
}

#ifdef GUILD_HAVE_SDL2
// AUDIBLE-OUTPUT proof on the REAL SDL device (dummy driver, headless): bring
// the session up over a real SdlAudioDevice, tick frames, then pull-mix the
// device's own mixer and assert the produced PCM is NON-SILENT — the exact
// samples the SDL callback would feed the hardware. The market-ambience loop
// (real decoded ADPCM) alone guarantees signal; with GUILD_HAVE_MP3 the
// outdoor season track streams real music on top.
TEST(SessionAudioE2E, SdlDeviceMixesAudibleSessionAudio) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("sfx/Lebewesen/athmos.sbf") || !fs.exists("include_sfx.ini")) {
        std::printf("[session_audio_e2e] install absent under %s -> skip\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    crt::Srand(1);
    shim::SdlAudioDevice dev;
    SessionAudio sa;
    SessionAudioInit opt;
    opt.gameDir = GameDir();
    CHECK(sa.Init(&dev, &fs, iniSound(), opt));
    CHECK(dev.opened());
    CHECK_EQ(dev.deviceChannels(), 2);
    CHECK_EQ(dev.deviceRate(), 44100);

    const float market[3] = {100.0f, 0.0f, 100.0f};
    sa.SetMarketPosition(market);
    const float fwd[3] = {0.0f, 0.0f, 1.0f};
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    for (int t = 1; t <= 4; ++t)
        sa.Frame(pos, fwd, /*locationId=*/0, audio::kSpring, (std::uint32_t)t);

    CHECK(sa.status().marketLoopPlaying);
    CHECK(sa.director().currentTrackHandle != 0);
    CHECK(dev.activeVoices() >= 1); // market loop (+ music stream with MP3)

    // Deterministic pull of the device mixer: real per-voice volume/pan/master
    // applied to the real decoded PCM. Skip the resampled lead-in (22050->44100
    // ambience starts with a quiet ramp) by pulling a few buffers.
    bool nonSilent = false;
    int peak = 0;
    for (int pull = 0; pull < 200 && !nonSilent; ++pull) {
        std::vector<std::int16_t> mix = dev.pullMix(4096);
        for (std::int16_t s : mix) {
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
            if (a > 16) { nonSilent = true; }
        }
    }
    std::printf("[session_audio_e2e] SDL mix peak=%d %s\n", peak,
                sa.musicSink().streaming() ? "(music streaming)" : "");
    CHECK(nonSilent);

    sa.Shutdown();
    dev.shutdown();
    CHECK(!dev.opened());
}
#endif // GUILD_HAVE_SDL2
