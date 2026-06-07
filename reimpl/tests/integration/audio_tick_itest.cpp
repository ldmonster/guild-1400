// Integration test for the app-side audio tick driving the REAL audio cores
// (SoundSystem voice pool + 3D pool + speech queue + music director) end-to-end,
// crossing app::AudioTick / Start/StopMarketLoop against the real audio siblings.
// Only the OS leaf (IAudioDevice) is a shim backend (NullAudioDevice); every
// audio decision runs through reconstructed code.
#include "test.h"

#include "app/audio_tick.h"
#include "audio/sound.h"
#include "audio/music_world.h"
#include "shim_impl/null_audio.h"

#include <string>

using namespace guild;

namespace {

struct ItestMusicSink : audio::IMusicSink {
    int loaded = 0, stopped = 0;
    int loadTrack(const std::string&, int) override { return ++loaded; }
    void stopTrack(int, int) override { ++stopped; }
};

app::AudioListener Listener(float fx, float fy, float fz) {
    app::AudioListener l{};
    l.forward = audio::Vec3{fx, fy, fz};
    return l;
}

} // namespace

// A full frame-of-audio: bring up the real sound system, seed the music table and
// a market sample, fire the market SFX, then run the per-frame AudioTick. Assert
// that music was selected, an SFX (3D market loop) is live, the 3D pool updated a
// real voice, and the device actually saw playback calls.
TEST(AudioTickItest, SelectsMusicAndFiresSfx) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    CHECK(snd.init(audio::kDefaultVoices, audio::kDefaultChannels,
                   audio::kDefaultSampleRate));

    // Seed the market-ambience sample through the REAL sample bank (the loader's
    // in-memory index, VIBE_SampleBank_FindSampleByName resolves it).
    audio::SampleRecord& s = snd.bank().addSample("Athmos\\Markt");
    s.format = 2;
    s.pcm.assign(128, 0x7f);
    s.sampleRate = audio::kDefaultSampleRate;

    // Seed the outdoor music director (the real music_world table director).
    ItestMusicSink sink;
    audio::MusicDirector dir;
    dir.sink = &sink;
    audio::TrackEntry e;
    e.ids = {audio::kOutdoorTrackId};
    dir.table.push_back(e);

    app::AudioEnable en;
    en.sound3dOn = true;
    en.weatherOn = true;
    en.musicOn = true;

    // --- music selection (per game state): select the season track once -------
    int idx = audio::SelectOutdoorSeasonTrack(dir, audio::kSummer);
    CHECK(idx >= 0);
    CHECK(dir.currentTrackHandle != 0);   // a real stream handle was loaded
    CHECK(sink.loaded == 1);

    // --- SFX trigger: start the looping market ambience (real 3D attach) -------
    app::AudioListener listener = Listener(0.0f, 0.0f, 1.0f);
    audio::Sound3dEntry* market = nullptr;
    audio::Vec3 marketPos{20.0f, 0.0f, 5.0f};  // off to the listener's right
    audio::Sound3dEntry* started =
        app::StartMarketLoop(snd, market, marketPos, listener, "Athmos\\Markt");
    CHECK(started != nullptr);
    CHECK(market == started);
    CHECK(market->voice != nullptr);          // a real voice was bound
    CHECK(market->radius == 2200.0f);         // the original's attach range
    CHECK(!market->oneShot);                  // looping

    // The attach already ran one 3D update -> the device saw at least one Play.
    bool sawPlay = false;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play) sawPlay = true;
    CHECK(sawPlay);

    // --- run one full per-frame audio tick across the real cores --------------
    app::AudioTickResult r = app::AudioTick(
        snd, dir, en, app::mask::kDayCycleMusic, /*tick=*/13, listener,
        audio::kSummer, /*atEnd=*/false, /*loc=*/0);
    CHECK(r.sound3dRan);
    CHECK(r.voiceQueueRan);
    CHECK(r.voicesRan);
    // The season did not change -> the music state machine reports a non-stop
    // action (running -> none, or it re-affirmed playback).
    CHECK(r.music != audio::PlaybackAction::kSeasonStop);

    // The market loop is still live after the tick (it loops).
    CHECK(market != nullptr);

    // --- stop the SFX (real detach) -------------------------------------------
    CHECK(app::StopMarketLoop(snd, market));
    CHECK(market == nullptr);

    snd.shutdown();
}

// A season change while music plays stops the running outdoor track (the
// per-state music selection) — driven through the real music director.
TEST(AudioTickItest, SeasonChangeStopsTrack) {
    shim::NullAudioDevice dev;
    audio::SoundSystem snd(&dev);
    snd.init();

    ItestMusicSink sink;
    audio::MusicDirector dir;
    dir.sink = &sink;
    audio::TrackEntry e;
    e.ids = {audio::kOutdoorTrackId};
    dir.table.push_back(e);
    audio::SelectOutdoorSeasonTrack(dir, audio::kSummer);

    app::AudioEnable en;
    en.sound3dOn = true;
    en.musicOn = true;

    app::AudioListener l = Listener(0, 0, 1);

    // Latch lastSeason = summer.
    app::AudioTick(snd, dir, en, app::mask::kDayCycleMusic, 1, l,
                   audio::kSummer, false, 0);
    CHECK(dir.lastSeason == audio::kSummer);

    // Change to autumn -> the running outdoor track is stopped.
    app::AudioTickResult r = app::AudioTick(
        snd, dir, en, app::mask::kDayCycleMusic, 2, l, audio::kAutumn, false, 0);
    CHECK(r.music == audio::PlaybackAction::kSeasonStop);
    CHECK(dir.currentTrackHandle == 0);
    CHECK(sink.stopped >= 1);

    snd.shutdown();
}
