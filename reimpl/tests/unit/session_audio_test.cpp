// Unit tests for play::SessionAudio — real audio in the native city session,
// headless over the capturing NullAudioDevice + an empty MemFileSystem (no real
// install needed; the real-asset path is covered by session_audio_e2e_test).
#include "test.h"

#include "play/session_audio.h"
#include "audio/music_world.h"
#include "crt/rand.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/null_audio.h"

#include <set>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

// The shipped Gilde.INI [Sound] values (master 127 / sfx 114 / msx 50 /
// speech 127 / msx_freq 3).
config::SoundSettings iniSound() {
    config::SoundSettings s;
    s.masterVol = 127;
    s.sfxVol = 114;
    s.msxVol = 50;
    s.speechVol = 127;
    s.msxFreq = 3;
    return s;
}

float v3[3] = {0, 0, 0};
float fwd[3] = {0, 0, 1};

bool inPool(const std::string& name, const std::array<const char*, 4>& pool) {
    for (const char* p : pool)
        if (name == p)
            return true;
    return false;
}

// NullAudioDevice extended with the per-voice status probe (the
// audio::IAudioStatusDevice seam VIBE_Audio_GetSampleStatus models) so the
// outdoor-track end -> reselect rotation can be exercised headless.
struct StatusNullDevice : shim::NullAudioDevice, audio::IAudioStatusDevice {
    std::set<shim::VoiceHandle> playing;
    int sampleStatus(shim::VoiceHandle v) override {
        return playing.count(v) ? audio::kSampleStatusPlaying : 0;
    }
};

// Count Play submissions with a given loops value on the null device.
int playsWithLoops(const shim::NullAudioDevice& dev, int loops) {
    int n = 0;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play && c.loops == loops)
            ++n;
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Init: real cores up, exact volume-settings products (@0x56c148).
// ---------------------------------------------------------------------------
TEST(SessionAudioUnit, InitBringsUpRealCoresAndVolumes) {
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));
    CHECK(sa.initialized());
    CHECK(sa.status().soundInited);
    CHECK(sa.sound() != nullptr);
    CHECK(sa.sound()->voices().initialized());
    CHECK_EQ(sa.sound()->voices().voiceCount(), 48);   // VIBE_Sound_LibInit 48
    CHECK_EQ(sa.sound()->pool3d().capacity(), 16);     // Sound3d_InitPool 16

    // VIBE_Audio_ApplyVolumeSettings @0x56c148 with the shipped INI bytes.
    // CRITICAL: v6 is stored as a 32-bit FLOAT (disasm 56c16b `fstp dword [esp]`
    // = D9 1C 24, reloaded by 56c172 `fmul dword [esp]` = D8 0C 24). The product
    //   127 * flt_62522C(0x3C010204) = 0.99999999627... (double)
    // rounds to 1.0f when stored to float32 (within float epsilon of 1.0). So:
    //   v6 = 1.0
    //   ApplyMasterVolume((int)(msx_vol 50  * v6)) = 50
    //   SetMusicVolume   ((int)(sfx_vol 114 * v6)) = 114
    // The prior golden (113/49) wrongly assumed v6 kept double precision; the
    // float32 round-trip in the binary yields 114/50. Golden corrected to 1:1.
    CHECK_EQ(sa.status().masterVolumeApplied, 114);
    CHECK_EQ(dev.masterVolume(), 114);
    // flt_6422A8 = msx_freq 3 * flt_625230(0.25) = 0.75.
    CHECK(sa.status().musicFreqScale == 0.75f);

    // No install behind the MemFileSystem: nothing indexed, no market PCM.
    CHECK_EQ(sa.status().banksIndexed, (std::size_t)0);
    CHECK(!sa.status().marketSampleLoaded);
    sa.Shutdown();
    CHECK(!sa.initialized());
}

TEST(SessionAudioUnit, InitRejectsNullDeviceOrFs) {
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(!sa.Init(nullptr, &fs, iniSound()));
    CHECK(!sa.Init(&dev, nullptr, iniSound()));
}

// ---------------------------------------------------------------------------
// Frame: the gated audio tick (@0x4c09a0) + outdoor music selection.
// ---------------------------------------------------------------------------
TEST(SessionAudioUnit, FrameRunsGatedTickAndSelectsSeasonTrack) {
    crt::Srand(1);
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));

    // Frame 1: the 3D/voice blocks run (sfx gate on); music latches the season
    // (lastSeason -1 -> set, kNone) — the original first-tick behaviour.
    sa.Frame(v3, fwd, /*loc=*/0, audio::kSummer, /*tick=*/1);
    CHECK(sa.status().lastTick.sound3dRan);
    CHECK(sa.status().lastTick.voiceQueueRan);
    CHECK(sa.status().lastTick.voicesRan);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kNone);

    // Frame 2: no running track -> SelectOutdoorSeasonTrack (id 9876) loads a
    // summer-pool file through the sink.
    sa.Frame(v3, fwd, 0, audio::kSummer, 2);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSelectOutdoor);
    CHECK(sa.director().currentTrackHandle != 0);
    CHECK(inPool(sa.musicSink().currentName(), audio::SummerTracks()));
    CHECK_EQ(sa.musicSink().loadCount(), 1);
    // Null device has no status probe -> the sink loops the stream at the
    // device (the named stream-end gap fallback).
    CHECK_EQ(sa.musicSink().lastLoops(), -1);

    // Frame 3: running, no end signal -> steady state.
    sa.Frame(v3, fwd, 0, audio::kSummer, 3);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kNone);
    CHECK_EQ(sa.musicSink().loadCount(), 1);
    sa.Shutdown();
}

TEST(SessionAudioUnit, MusicGateMsxOff) {
    crt::Srand(1);
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    SessionAudioInit opt;
    opt.msxOn = false; // INI [Sound] msx=0 -> dword_63C8F8 = 0
    CHECK(sa.Init(&dev, &fs, iniSound(), opt));
    for (int t = 1; t <= 3; ++t)
        sa.Frame(v3, fwd, 0, audio::kSpring, (std::uint32_t)t);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kNone);
    CHECK_EQ(sa.musicSink().loadCount(), 0);
    sa.Shutdown();
}

TEST(SessionAudioUnit, MusicGateMsxFreqZero) {
    // VIBE_Music_UpdateOutdoorTrackPlayback @0x5815c4: flt_6422A8 <= 0 -> return.
    crt::Srand(1);
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    config::SoundSettings snd = iniSound();
    snd.msxFreq = 0;
    CHECK(sa.Init(&dev, &fs, snd));
    for (int t = 1; t <= 3; ++t)
        sa.Frame(v3, fwd, 0, audio::kSpring, (std::uint32_t)t);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kNone);
    CHECK_EQ(sa.musicSink().loadCount(), 0);
    sa.Shutdown();
}

TEST(SessionAudioUnit, SfxGateOffSkips3dBlocks) {
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    SessionAudioInit opt;
    opt.sfxOn = false; // INI [Sound] sfx=0 -> dword_63C900 = 0
    CHECK(sa.Init(&dev, &fs, iniSound(), opt));
    sa.Frame(v3, fwd, 0, audio::kSpring, 1);
    CHECK(!sa.status().lastTick.sound3dRan);
    CHECK(!sa.status().lastTick.voiceQueueRan);
    CHECK(!sa.status().lastTick.voicesRan);
    CHECK(!sa.status().marketLoopPlaying); // market trigger also gated
    sa.Shutdown();
}

// Season change: running outdoor track stops (kSeasonStop), the entry NAME is
// cleared (byte_645F28[..]=0), and the next select rerolls a new-season variant
// that differs from the remembered last track.
TEST(SessionAudioUnit, SeasonChangeStopsThenReselects) {
    crt::Srand(7);
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));

    sa.Frame(v3, fwd, 0, audio::kSummer, 1); // latch summer
    sa.Frame(v3, fwd, 0, audio::kSummer, 2); // select summer track
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSelectOutdoor);
    std::string summerTrack = sa.musicSink().currentName();
    CHECK(inPool(summerTrack, audio::SummerTracks()));

    sa.Frame(v3, fwd, 0, audio::kAutumn, 3); // season change -> stop
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSeasonStop);
    CHECK_EQ(sa.director().currentTrackHandle, 0);
    CHECK(sa.director().table[0].name.empty()); // name cleared on season stop
    CHECK_EQ(sa.musicSink().stopCount(), 1);

    sa.Frame(v3, fwd, 0, audio::kAutumn, 4); // reselect in the new season
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSelectOutdoor);
    CHECK(inPool(sa.musicSink().currentName(), audio::AutumnTracks()));
    sa.Shutdown();
}

// Track end (via the per-voice status probe): kTrackEnded -> pause -> reselect,
// with the avoid-repeat reroll picking a DIFFERENT variant than the ended one
// (the !VIBE_Util_StrCmp loop @0x581208, StrCmp == strcmp).
TEST(SessionAudioUnit, TrackEndRotatesVariantViaStatusProbe) {
    crt::Srand(3);
    StatusNullDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));

    sa.Frame(v3, fwd, 0, audio::kWinter, 1); // latch
    sa.Frame(v3, fwd, 0, audio::kWinter, 2); // select
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSelectOutdoor);
    // With a status probe the stream plays ONCE (the MSS loop-count-1 branch).
    CHECK_EQ(sa.musicSink().lastLoops(), 0);
    std::string first = sa.musicSink().currentName();
    CHECK(inPool(first, audio::WinterTracks()));

    dev.playing.insert(sa.musicSink().voice()); // stream running
    sa.Frame(v3, fwd, 0, audio::kWinter, 3);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kNone);

    dev.playing.clear();                        // stream cursor reached the end
    sa.Frame(v3, fwd, 0, audio::kWinter, 4);
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kTrackEnded);
    CHECK_EQ(sa.director().currentTrackHandle, 0);

    sa.Frame(v3, fwd, 0, audio::kWinter, 5);    // reselect
    CHECK(sa.status().lastTick.music == audio::PlaybackAction::kSelectOutdoor);
    CHECK(inPool(sa.musicSink().currentName(), audio::WinterTracks()));
    CHECK(sa.musicSink().currentName() != first); // reroll avoided the repeat
    sa.Shutdown();
}

// ---------------------------------------------------------------------------
// Market-ambience loop (VIBE_Ambient_StartMarketLoop @0x582858 / Stop @0x5828bc,
// fired at scene entry/exit by VIBE_Scene_RunMainFrameLoop @0x50f0c0).
// ---------------------------------------------------------------------------
TEST(SessionAudioUnit, MarketLoopStartsOnFirstFrameStopsOnShutdown) {
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));

    // Stand-in for the faulted bank PCM (no install behind MemFileSystem): seed
    // the market sample through the real SampleBank add path.
    audio::SampleRecord& rec =
        sa.sound()->bank().addSample(kMarketAmbienceSample);
    rec.format = 1;
    rec.sampleRate = 22050;
    rec.pcm.assign(512, 1);

    sa.SetMarketPosition(v3); // market at the listener -> audible at range 0
    sa.Frame(v3, fwd, 0, audio::kSpring, 1);
    CHECK(sa.status().marketLoopPlaying);
    audio::Sound3dEntry* e = sa.marketLoopEntry();
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->inUse);
        CHECK(e->voice != nullptr);          // bound + started by UpdateAll
        CHECK_EQ(e->baseVol, 127);           // AttachToEntity(127, 2200.0)
        CHECK(e->radius == 2200.0f);
    }
    // VIBE_Sound3d_SetLooping(h,1): the device stream was re-armed looping.
    CHECK(playsWithLoops(dev, -1) >= 1);

    // Stays attached across frames (the loop persists for the scene's life).
    sa.Frame(v3, fwd, 0, audio::kSpring, 2);
    CHECK(sa.status().marketLoopPlaying);

    sa.Shutdown(); // scene exit -> StopMarketLoop
    CHECK(!sa.status().marketLoopPlaying);
}

TEST(SessionAudioUnit, MarketLoopNeedsTheRealSample) {
    // The original gates on the live market scene record (*(dword_6477A4+97));
    // here: no resolvable sample (empty install) -> no loop.
    shim::NullAudioDevice dev;
    shim::MemFileSystem fs;
    SessionAudio sa;
    CHECK(sa.Init(&dev, &fs, iniSound()));
    sa.Frame(v3, fwd, 0, audio::kSpring, 1);
    CHECK(!sa.status().marketLoopPlaying);
    CHECK(sa.marketLoopEntry() == nullptr);
    sa.Shutdown();
}

// ---------------------------------------------------------------------------
// IMA ADPCM decoder (WAVE fmt 0x11 — the market-ambience codec).
// ---------------------------------------------------------------------------
TEST(SessionAudioUnit, ImaAdpcmGoldenBlock) {
    // One mono block, blockAlign 12: header {pred=1000, idx=20, rsv} + 8 data
    // bytes (16 nibbles). Expected samples computed against the IMA reference
    // tables (first sample = the header predictor verbatim).
    static const u8 kBlock[12] = {0xE8, 0x03, 0x14, 0x00, 0x71, 0x82,
                                  0xF3, 0x04, 0x55, 0xAA, 0x10, 0x9C};
    static const i16 kExpected[17] = {1000, 1018, 1101, 1161, 1150, 1220,
                                      1084, 1260, 1283, 1520, 1867, 1636,
                                      1426, 1464, 1567, 1283, 1169};
    std::vector<i16> out;
    std::size_t frames =
        DecodeImaAdpcm(kBlock, sizeof(kBlock), /*channels=*/1,
                       /*blockAlign=*/12, /*sampleLimit=*/0, out);
    CHECK_EQ(frames, (std::size_t)17);
    CHECK_EQ(out.size(), (std::size_t)17);
    for (std::size_t i = 0; i < 17 && i < out.size(); ++i)
        CHECK_EQ((int)out[i], (int)kExpected[i]);
}

TEST(SessionAudioUnit, ImaAdpcmHonorsFactSampleLimit) {
    static const u8 kBlock[12] = {0xE8, 0x03, 0x14, 0x00, 0x71, 0x82,
                                  0xF3, 0x04, 0x55, 0xAA, 0x10, 0x9C};
    std::vector<i16> out;
    std::size_t frames = DecodeImaAdpcm(kBlock, sizeof(kBlock), 1, 12,
                                        /*sampleLimit=*/5, out);
    CHECK_EQ(frames, (std::size_t)5);
    CHECK_EQ(out.size(), (std::size_t)5);
    CHECK_EQ((int)out[0], 1000);
    CHECK_EQ((int)out[4], 1150);
}

TEST(SessionAudioUnit, ImaAdpcmRejectsBadArgs) {
    std::vector<i16> out;
    CHECK_EQ(DecodeImaAdpcm(nullptr, 8, 1, 12, 0, out), (std::size_t)0);
    static const u8 b[4] = {0, 0, 0, 0};
    CHECK_EQ(DecodeImaAdpcm(b, 4, 3, 12, 0, out), (std::size_t)0);  // channels
    CHECK_EQ(DecodeImaAdpcm(b, 4, 1, 2, 0, out), (std::size_t)0);   // blockAlign
}

// ---------------------------------------------------------------------------
// Music path mapping (the "msx\" prefix @0x622b3e).
// ---------------------------------------------------------------------------
TEST(SessionAudioUnit, MusicTrackRelPathMapsMsxPrefix) {
    CHECK(MusicTrackRelPath("cd1\\ImFruehling.mp3") == "msx/cd1/ImFruehling.mp3");
    CHECK(MusicTrackRelPath("cd2\\AufDemMarkt.mp3") == "msx/cd2/AufDemMarkt.mp3");
    CHECK(MusicTrackRelPath("") == "msx/");
}
