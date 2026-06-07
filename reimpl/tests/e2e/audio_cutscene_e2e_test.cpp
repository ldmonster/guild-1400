// End-to-end flows across guild::audio cutscene-music override and worker-comment
// playback: a full enter-cutscene -> duck -> restore sequence, and a select ->
// click -> unload comment lifecycle.
#include "test.h"
#include "audio/music_cutscene.h"
#include "audio/voice_comment.h"
#include "crt/rand.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

struct E2eAudio : shim::IAudioDevice {
    int allocs = 0, stops = 0, frees = 0, plays = 0, masterCalls = 0, muteToZero = 0;
    int lastMaster = -1;
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

struct E2eCommentSink : IVoiceCommentSink {
    std::vector<std::pair<u16,int>> plays; // (id, bank)
    std::vector<int> unloads;
    bool playPositionalSample(u16 id, int bank, const std::string&) override {
        plays.push_back({id, bank}); return true;
    }
    void unloadSampleBank(int bank) override { unloads.push_back(bank); }
};

const char kPcm[32] = {0};

} // namespace

// Full cutscene lifecycle: world music playing -> enter cutscene (override
// loads its own track) -> end cutscene (restore).
TEST(AudioCutsceneE2E, EnterPlayRestoreLifecycle) {
    E2eAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setWorldMusicEnabled(true);
    cs.setMusicSystemOn(true);
    cs.setMusicVolumeSetting(1.0f);

    // Pretend the world director already has a track streaming, then a cutscene
    // begins with its own track.
    cs.playCutsceneTrack("world_theme.mp3", kPcm, sizeof(kPcm), 44100);
    CHECK(cs.inCutscene());
    CHECK(!cs.worldMusicEnabled());
    MusicTrack* firstTrack = cs.activeTrack();
    CHECK(firstTrack != nullptr);

    // Cutscene switches to a dramatic track: previous one fades out (mute), new
    // one loads on a fresh device voice.
    int mutesAfterFirst = dev.muteToZero;
    cs.playCutsceneTrack("battle.mp3", kPcm, sizeof(kPcm), 44100);
    CHECK(dev.muteToZero > mutesAfterFirst);
    CHECK(cs.activeTrack() != firstTrack);
    CHECK_EQ(dev.allocs, 2);

    // Cutscene ends: world music-enable flag restored, fade-up requested.
    cs.restoreAfterCutscene();
    CHECK(!cs.inCutscene());
    CHECK(cs.worldMusicEnabled()); // restored to the snapshot (true)
}

// "No track" cutscene path: simply ducks the running music, then restores.
TEST(AudioCutsceneE2E, DuckOnlyThenRestore) {
    E2eAudio dev;
    MusicPlayer music(&dev);
    CutsceneMusic cs(&music);
    cs.setWorldMusicEnabled(true);

    // Seed an active track.
    cs.playCutsceneTrack("amb.mp3", kPcm, sizeof(kPcm), 44100);
    int allocsAfter = dev.allocs;

    // Duck-only entry (no name): no new voice, music faded toward 0.4.
    cs.playCutsceneTrack();
    CHECK_EQ(dev.allocs, allocsAfter);
    CHECK(music.fadeModifier() > 0.39f && music.fadeModifier() < 0.41f);

    cs.restoreAfterCutscene();
    CHECK(!cs.inCutscene());
}

// Worker-comment lifecycle: select a worker, click-play their comment, then
// unload the comment banks.
TEST(AudioCommentE2E, SelectClickUnloadFlow) {
    crt::Srand(12345); // deterministic RNG for the click roll
    E2eCommentSink sink;
    WorkerCommentPlayer p(&sink);
    p.setCommandBank(0xE8);
    p.setClickBank(0xEC);
    p.setNoiseBank(0xF0);
    p.setGreetingBank(0xF8);

    std::vector<CommentPerson> people = {
        {10, false}, {20, false}, {33, true}, {44, true},
    };

    // Click the selected worker: first selected (id 33) speaks from one of the
    // two click banks.
    int r = p.playSelectedWorkerComment(people);
    CHECK_EQ(sink.plays.size(), static_cast<std::size_t>(1));
    CHECK_EQ(sink.plays[0].first, static_cast<u16>(33));
    CHECK(sink.plays[0].second == 0xEC || sink.plays[0].second == 0xF0);
    (void)r;

    // Context change: tear down all comment banks.
    p.unloadCommentBanks();
    CHECK_EQ(sink.unloads.size(), static_cast<std::size_t>(4));
    CHECK_EQ(p.clickBank(), 0);
    CHECK_EQ(p.commandBank(), 0);

    // A subsequent click no longer plays (banks gone).
    sink.plays.clear();
    p.playSelectedWorkerComment(people);
    CHECK(sink.plays.empty());
}
