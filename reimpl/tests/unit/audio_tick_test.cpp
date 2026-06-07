// Unit tests for guild::app::AudioTick + Start/StopMarketLoop (the app-side
// per-frame audio driver translated from VIBE_GameLogic_RunFrameLoop's audio
// block, driving the real audio cores).
#include "test.h"

#include "app/audio_tick.h"
#include "audio/sound.h"
#include "audio/music_world.h"
#include "shim_impl/null_audio.h"

using namespace guild;

namespace {

// A recording music sink so the music block has a real load path.
struct RecMusicSink : audio::IMusicSink {
    int loaded = 0, stopped = 0;
    int loadTrack(const std::string&, int) override { return ++loaded; }
    void stopTrack(int, int) override { ++stopped; }
};

app::AudioListener frontListener() {
    app::AudioListener l{};
    l.forward = audio::Vec3{0.0f, 0.0f, 1.0f};
    return l;
}

audio::MusicDirector outdoorDirector(RecMusicSink& sink) {
    audio::MusicDirector d;
    d.sink = &sink;
    audio::TrackEntry e;
    e.ids = {audio::kOutdoorTrackId};
    d.table.push_back(e);
    return d;
}

} // namespace

// With sound3d disabled, the ambient/3D/voice blocks are skipped (the gate
// dword_63C900 is false).
TEST(AudioTickUnit, GatesOffWhenDisabled) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();
    RecMusicSink sink;
    audio::MusicDirector dir = outdoorDirector(sink);

    app::AudioEnable en;          // all false
    app::AudioTickResult r = app::AudioTick(
        snd, dir, en, /*mask=*/app::mask::kDayCycleMusic, /*tick=*/1,
        frontListener(), /*season=*/audio::kSpring, /*atEnd=*/false, /*loc=*/0);

    CHECK(!r.sound3dRan);
    CHECK(!r.voiceQueueRan);
    CHECK(!r.voicesRan);
    CHECK(r.music == audio::PlaybackAction::kNone); // music gate (musicOn) also off
}

// sound3dOn enables the ambient/3D/voice blocks; musicOn + the day-cycle bit
// enables the music state machine.
TEST(AudioTickUnit, FullTickRunsAllEnabledBlocks) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();
    RecMusicSink sink;
    audio::MusicDirector dir = outdoorDirector(sink);
    // Prime the outdoor track so the playback state machine has an active handle.
    audio::SelectOutdoorSeasonTrack(dir, audio::kSpring);

    app::AudioEnable en;
    en.sound3dOn = true;
    en.weatherOn = true;
    en.musicOn = true;

    app::AudioTickResult r = app::AudioTick(
        snd, dir, en, app::mask::kDayCycleMusic, /*tick=*/2,
        frontListener(), audio::kSpring, /*atEnd=*/false, /*loc=*/0);

    CHECK(r.sound3dRan);
    CHECK(r.voiceQueueRan);
    CHECK(r.voicesRan);
    CHECK(!r.listenerRan);  // terrain-loop listener is a documented stub
}

// The music block is gated by BOTH musicOn AND the kDayCycleMusic mask bit; with
// the bit clear, music does not tick even when musicOn.
TEST(AudioTickUnit, MusicGatedByDayCycleMaskBit) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();
    RecMusicSink sink;
    audio::MusicDirector dir = outdoorDirector(sink);
    audio::SelectOutdoorSeasonTrack(dir, audio::kSpring);

    app::AudioEnable en;
    en.musicOn = true;

    // Mask bit clear -> music block skipped (no season is latched).
    app::AudioTickResult r0 = app::AudioTick(
        snd, dir, en, /*mask=*/0u, /*tick=*/1, frontListener(),
        audio::kSpring, /*atEnd=*/false, /*loc=*/0);
    CHECK(r0.music == audio::PlaybackAction::kNone);
    CHECK(dir.lastSeason == -1);   // music never ticked -> no season latched

    // First music tick (bit set) on a NON-spring season latches lastSeason
    // (the original's lastSeason==-1 init branch returns kNone).
    app::AudioTickResult r1 = app::AudioTick(
        snd, dir, en, app::mask::kDayCycleMusic, /*tick=*/2, frontListener(),
        /*season=*/audio::kSummer, /*atEnd=*/false, /*loc=*/0);
    CHECK(r1.music == audio::PlaybackAction::kNone);
    CHECK(dir.lastSeason == audio::kSummer);

    // Now a real season change (summer -> winter) fires the season-stop.
    app::AudioTickResult r2 = app::AudioTick(
        snd, dir, en, app::mask::kDayCycleMusic, /*tick=*/3, frontListener(),
        /*season=*/audio::kWinter, /*atEnd=*/false, /*loc=*/0);
    CHECK(r2.music == audio::PlaybackAction::kSeasonStop);
}

// Market-loop SFX trigger: starts once (idempotent while playing), then stops.
TEST(AudioTickUnit, MarketLoopStartStop) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();
    // Seed the market sample so playPositioned can resolve it.
    audio::SampleRecord& s = snd.bank().addSample("Athmos\\Markt");
    s.format = 2;
    s.pcm.assign(64, 0);

    audio::Sound3dEntry* handle = nullptr;
    app::AudioListener l = frontListener();
    audio::Vec3 pos{10.0f, 0.0f, 10.0f};

    audio::Sound3dEntry* e = app::StartMarketLoop(snd, handle, pos, l, "Athmos\\Markt");
    CHECK(e != nullptr);
    CHECK(handle == e);
    CHECK(!e->oneShot);          // the market loop is looping, not a one-shot

    // Second start is idempotent: returns the same handle, no new entry.
    audio::Sound3dEntry* again = app::StartMarketLoop(snd, handle, pos, l, "Athmos\\Markt");
    CHECK(again == e);

    // Stop detaches and clears the handle.
    CHECK(app::StopMarketLoop(snd, handle));
    CHECK(handle == nullptr);
    // Stopping again is a no-op (returns false).
    CHECK(!app::StopMarketLoop(snd, handle));
}

// An empty sample name -> the original's "no live scene record" path -> no start.
TEST(AudioTickUnit, MarketLoopNoSampleNoStart) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();
    audio::Sound3dEntry* handle = nullptr;
    app::AudioListener l = frontListener();
    CHECK(app::StartMarketLoop(snd, handle, audio::Vec3{}, l, "") == nullptr);
    CHECK(handle == nullptr);
}
