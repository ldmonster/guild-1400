// Unit tests for guild::audio cutscene-music override (music_cutscene) and the
// worker-comment click playback / bank lifetime (voice_comment).
#include "test.h"
#include "audio/music_cutscene.h"
#include "audio/voice_comment.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// Minimal recording IAudioDevice for MusicPlayer (cutscene routes through it).
struct CsMockAudio : shim::IAudioDevice {
    int allocs = 0;
    int lastMaster = -999;
    int masterCalls = 0;
    int stops = 0;
    int frees = 0;
    int plays = 0;
    int muteToZero = 0; // setVolume(h, 0) calls — the fade-out stop path
    shim::VoiceHandle next = 1;

    bool init(int, int, int) override { return true; }
    void shutdown() override {}
    shim::VoiceHandle allocVoice() override { ++allocs; return next++; }
    void freeVoice(shim::VoiceHandle) override { ++frees; }
    void playSample(shim::VoiceHandle, const void*, std::size_t, int, int) override { ++plays; }
    void stop(shim::VoiceHandle) override { ++stops; }
    void setVolume(shim::VoiceHandle, int v) override { if (v == 0) ++muteToZero; }
    void setPan(shim::VoiceHandle, int) override {}
    void setMasterVolume(int v) override { lastMaster = v; ++masterCalls; }
};

// Recording IVoiceCommentSink.
struct CommentSinkMock : IVoiceCommentSink {
    struct Play { u16 id; int bank; std::string name; };
    std::vector<Play> plays;
    std::vector<int> unloads;
    bool ret = true;

    bool playPositionalSample(u16 id, int bank, const std::string& name) override {
        plays.push_back({id, bank, name});
        return ret;
    }
    void unloadSampleBank(int bank) override { unloads.push_back(bank); }
};

const char kPcm[16] = {0};

} // namespace

// ---- Cutscene music --------------------------------------------------------

TEST(AudioCutscene, EnterSnapshotsAndClearsWorldFlag) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setWorldMusicEnabled(true);

    CHECK(!cs.inCutscene());
    cs.playCutsceneTrack("intro.mp3", kPcm, sizeof(kPcm), 44100);

    // First entry: inCutscene set, world flag cleared (snapshot held internally).
    CHECK(cs.inCutscene());
    CHECK(!cs.worldMusicEnabled());
    // A track was loaded -> a device voice was allocated.
    CHECK_EQ(dev.allocs, 1);
    CHECK(cs.activeTrack() != nullptr);
    CHECK(cs.activeTrack() == cs.cutsceneTrack());
}

TEST(AudioCutscene, GatedOffWhenMusicSystemOff) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setWorldMusicEnabled(true);
    cs.setMusicSystemOn(false); // dword_63C8F8 == 0

    cs.playCutsceneTrack("intro.mp3", kPcm, sizeof(kPcm), 44100);

    // Flag bookkeeping still happens, but no track is loaded.
    CHECK(cs.inCutscene());
    CHECK(!cs.worldMusicEnabled());
    CHECK_EQ(dev.allocs, 0);
    CHECK(cs.activeTrack() == nullptr);
}

TEST(AudioCutscene, GatedOffWhenVolumeSettingZero) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setMusicVolumeSetting(0.0f); // flt_6422A8 == 0

    cs.playCutsceneTrack("intro.mp3", kPcm, sizeof(kPcm), 44100);
    CHECK_EQ(dev.allocs, 0);
    CHECK(cs.activeTrack() == nullptr);
}

TEST(AudioCutscene, NoTrackPathDucksMusic) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);

    // Seed an active track first (so the duck branch has something to fade).
    cs.playCutsceneTrack("a.mp3", kPcm, sizeof(kPcm), 44100);
    CHECK(cs.activeTrack() != nullptr);
    int allocsAfterLoad = dev.allocs;

    // No-name call: ducks current music to 0.4 (no new track loaded).
    cs.playCutsceneTrack();
    CHECK_EQ(dev.allocs, allocsAfterLoad); // no new voice allocated
    // fadeModifier should have moved to the duck volume.
    CHECK(music.fadeModifier() > 0.39f && music.fadeModifier() < 0.41f);
}

TEST(AudioCutscene, SecondTrackStopsPreviousActive) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);

    cs.playCutsceneTrack("a.mp3", kPcm, sizeof(kPcm), 44100);
    int mutesAfterFirst = dev.muteToZero;
    cs.playCutsceneTrack("b.mp3", kPcm, sizeof(kPcm), 44100);
    // The previous active track was stopped (fade-out = setVolume 0) before
    // loading the new one; a new device voice was allocated for "b.mp3".
    CHECK(dev.muteToZero > mutesAfterFirst);
    CHECK_EQ(dev.allocs, 2);
}

TEST(AudioCutscene, RestoreClearsFlagAndRestoresWorldEnable) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setWorldMusicEnabled(true);

    cs.playCutsceneTrack("a.mp3", kPcm, sizeof(kPcm), 44100);
    CHECK(!cs.worldMusicEnabled());

    cs.restoreAfterCutscene();
    CHECK(!cs.inCutscene());
    CHECK(cs.worldMusicEnabled()); // snapshot restored (was true)
}

TEST(AudioCutscene, SetTrackFadeNoopWithoutActiveTrack) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);

    float before = music.fadeModifier();
    cs.setTrackFade(0.5f, 1000); // no active track -> no change
    CHECK(music.fadeModifier() == before);
    CHECK(cs.activeTrack() == nullptr);
}

TEST(AudioCutscene, SetTrackFadeAppliesWithActiveTrack) {
    CsMockAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);

    cs.playCutsceneTrack("a.mp3", kPcm, sizeof(kPcm), 44100);
    cs.setTrackFade(0.5f, 1000);
    CHECK(music.fadeModifier() > 0.49f && music.fadeModifier() < 0.51f);
}

// ---- Worker comment playback ----------------------------------------------

TEST(AudioComment, ClickCommentRoll0UsesClickBank) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setClickBank(11);
    p.setNoiseBank(22);

    CommentPerson person{123, true};
    // RandomModulo is deterministic given the seeded LCG; play many and verify
    // both banks get exercised and only ever the two configured banks are used.
    int clickHits = 0, noiseHits = 0;
    for (int i = 0; i < 200; ++i) {
        sink.plays.clear();
        p.playWorkerClickComment(person);
        CHECK_EQ(sink.plays.size(), static_cast<std::size_t>(1));
        CHECK_EQ(sink.plays[0].id, static_cast<u16>(123));
        CHECK(sink.plays[0].name == std::string(kWorkerClickSampleName));
        if (sink.plays[0].bank == 11) ++clickHits;
        else if (sink.plays[0].bank == 22) ++noiseHits;
        else CHECK(false);
    }
    // RandomModulo(2) yields both outcomes over 200 draws.
    CHECK(clickHits > 0);
    CHECK(noiseHits > 0);
}

TEST(AudioComment, ClickCommentNoBanksReturnsRoll) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    // No banks configured: nothing plays, returns the raw roll (0 or 1).
    CommentPerson person{5, true};
    int r = p.playWorkerClickComment(person);
    CHECK(sink.plays.empty());
    CHECK(r == 0 || r == 1);
}

TEST(AudioComment, ClickCommentNoiseOnlyPlaysOnRoll1) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setNoiseBank(22); // click bank absent

    CommentPerson person{7, true};
    int played = 0;
    for (int i = 0; i < 100; ++i) {
        sink.plays.clear();
        p.playWorkerClickComment(person);
        if (!sink.plays.empty()) {
            CHECK_EQ(sink.plays[0].bank, 22);
            ++played;
        }
    }
    // Only roll==1 plays (roll==0 falls through, click bank absent).
    CHECK(played > 0);
    CHECK(played < 100);
}

TEST(AudioComment, SelectedScanPicksFirstSelected) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setClickBank(11);
    p.setNoiseBank(22);

    std::vector<CommentPerson> people = {
        {100, false}, {200, false}, {300, true}, {400, true},
    };
    p.playSelectedWorkerComment(people);
    CHECK_EQ(sink.plays.size(), static_cast<std::size_t>(1));
    CHECK_EQ(sink.plays[0].id, static_cast<u16>(300)); // first selected
}

TEST(AudioComment, SelectedScanNoneSelectedReturnsZero) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setClickBank(11);

    std::vector<CommentPerson> people = {{1, false}, {2, false}};
    int r = p.playSelectedWorkerComment(people);
    CHECK_EQ(r, 0);
    CHECK(sink.plays.empty());
}

TEST(AudioComment, UnloadReleasesAllBanksInOrderAndClears) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setCommandBank(0xE8);
    p.setClickBank(0xEC);
    p.setNoiseBank(0xF0);
    p.setGreetingBank(0xF8);

    p.unloadCommentBanks();

    // Order matches the original: command, click, noise, greeting.
    CHECK_EQ(sink.unloads.size(), static_cast<std::size_t>(4));
    CHECK_EQ(sink.unloads[0], 0xE8);
    CHECK_EQ(sink.unloads[1], 0xEC);
    CHECK_EQ(sink.unloads[2], 0xF0);
    CHECK_EQ(sink.unloads[3], 0xF8);

    // All handles cleared.
    CHECK_EQ(p.commandBank(), 0);
    CHECK_EQ(p.clickBank(), 0);
    CHECK_EQ(p.noiseBank(), 0);
    CHECK_EQ(p.greetingBank(), 0);

    // Second unload is a no-op (handles already zero).
    sink.unloads.clear();
    p.unloadCommentBanks();
    CHECK(sink.unloads.empty());
}

TEST(AudioComment, UnloadSkipsUnloadedBanks) {
    CommentSinkMock sink;
    WorkerCommentPlayer p(&sink);
    p.setClickBank(0xEC); // only click loaded
    p.unloadCommentBanks();
    CHECK_EQ(sink.unloads.size(), static_cast<std::size_t>(1));
    CHECK_EQ(sink.unloads[0], 0xEC);
}
