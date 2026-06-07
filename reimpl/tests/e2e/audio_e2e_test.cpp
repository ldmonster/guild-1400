// End-to-end flow for guild::audio: init the facade with a mock IAudioDevice,
// play several positioned sounds and a music stream, step the voice queue, and
// verify the mock received the expected device calls in order.
#include "test.h"
#include "audio/sound.h"

#include <string>
#include <unordered_map>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

struct MockAudio : shim::IAudioDevice, audio::IAudioStatusDevice {
    struct Call {
        std::string op;
        shim::VoiceHandle h = -1;
        int a = 0, b = 0;
    };
    std::vector<Call> calls;
    std::unordered_map<shim::VoiceHandle, bool> playing;
    shim::VoiceHandle next = 0;

    bool init(int v, int c, int r) override {
        calls.push_back({"init", -1, v, c}); (void)r; return true;
    }
    void shutdown() override { calls.push_back({"shutdown"}); }
    shim::VoiceHandle allocVoice() override {
        shim::VoiceHandle h = next++;
        calls.push_back({"allocVoice", h});
        playing[h] = false;
        return h;
    }
    void freeVoice(shim::VoiceHandle h) override { calls.push_back({"freeVoice", h}); }
    void playSample(shim::VoiceHandle h, const void*, std::size_t bytes, int r, int loops) override {
        calls.push_back({"playSample", h, static_cast<int>(bytes), loops}); (void)r;
        playing[h] = true;
    }
    void stop(shim::VoiceHandle h) override { calls.push_back({"stop", h}); playing[h] = false; }
    void setVolume(shim::VoiceHandle h, int v) override { calls.push_back({"setVolume", h, v}); }
    void setPan(shim::VoiceHandle h, int p) override { calls.push_back({"setPan", h, p}); }
    void setMasterVolume(int v) override { calls.push_back({"setMasterVolume", -1, v}); }

    int sampleStatus(shim::VoiceHandle h) override {
        return playing[h] ? kSampleStatusPlaying : 0;
    }

    int count(const std::string& op) const {
        int n = 0; for (auto& c : calls) if (c.op == op) ++n; return n;
    }
    // Index of the first call matching op at/after `from`; -1 if none.
    int find(const std::string& op, int from = 0) const {
        for (int i = from; i < static_cast<int>(calls.size()); ++i)
            if (calls[i].op == op) return i;
        return -1;
    }
};

} // namespace

TEST(AudioE2E, FullFlow) {
    MockAudio dev;
    SoundSystem sys(&dev);

    // --- init: 48 voices / 2ch / 44100 => 1 init + 48 allocVoice on the device.
    CHECK(sys.init());
    CHECK_EQ(dev.count("init"), 1);
    CHECK_EQ(dev.count("allocVoice"), kDefaultVoices);
    CHECK_EQ(dev.calls.front().op, std::string("init"));
    CHECK_EQ(dev.calls.front().a, kDefaultVoices);
    CHECK_EQ(dev.calls.front().b, kDefaultChannels);

    // --- populate a small sample bank.
    auto& s1 = sys.bank().addSample("explosion");
    s1.pcm.assign(64, 0xAB);
    s1.sampleRate = 22050;
    auto& s2 = sys.bank().addSample("bird");
    s2.pcm.assign(32, 0x10);

    int afterInit = static_cast<int>(dev.calls.size());

    // --- play two positioned sounds: one near/centered, one far/to-the-side.
    Vec3 listener{0, 0, 0};
    Vec3 forward{0, 0, 1};

    Sound3dEntry* near = sys.playPositioned("explosion", Vec3{0, 0, 100}, 127, 5000.0f,
                                            listener, forward, /*oneShot=*/true);
    CHECK(near != nullptr);
    CHECK(near->voice != nullptr);
    // Near + centered: a voice was grabbed (from the pre-allocated pool — no new
    // device allocVoice) and started with a loud, centered mix.
    CHECK(dev.find("playSample", afterInit) >= 0);

    Sound3dEntry* far = sys.playPositioned("bird", Vec3{100000, 0, 0}, 127, 5000.0f,
                                           listener, forward, /*oneShot=*/true);
    CHECK(far != nullptr);

    CHECK(far->voice != nullptr);
    CHECK(far->voice != near->voice); // different pool slots

    // Collect the most-recent volume/pan the device received for each voice over
    // the whole positioned-sound flow (the SetVoiceVolume/Pan guards mean a value
    // is only re-sent when it changes, so the latest write reflects the mix).
    int nearVol = -1, farVol = -1, nearPan = -1, farPan = -1;
    for (int i = afterInit; i < static_cast<int>(dev.calls.size()); ++i) {
        const auto& c = dev.calls[i];
        if (c.op == "setVolume") {
            if (c.h == near->voice->handle) nearVol = c.a;
            if (far->voice && c.h == far->voice->handle) farVol = c.a;
        }
        if (c.op == "setPan") {
            if (c.h == near->voice->handle) nearPan = c.a;
            if (far->voice && c.h == far->voice->handle) farPan = c.a;
        }
    }
    CHECK(nearVol > farVol);    // near is louder than far
    CHECK_EQ(nearPan, 63);      // straight-ahead source => centered
    CHECK(farPan > nearPan);    // side source => panned away from center
    CHECK(farPan >= 120);       // ~fully panned (sin(90)*127*0.5+63)

    // --- start a music stream; loop=true => device loops==0 (infinite).
    char musicPcm[128] = {0};
    int musicMark = static_cast<int>(dev.calls.size());
    MusicTrack* track = sys.music().loadTrack("ambient", musicPcm, sizeof(musicPcm),
                                              44100, /*loop=*/true);
    CHECK(track != nullptr);
    CHECK(track->active);
    int musicPlay = dev.find("playSample", musicMark);
    CHECK(musicPlay >= 0);
    CHECK_EQ(dev.calls[musicPlay].b, 0); // looping stream => loops==0

    // --- step the voice queue: enqueue a speech line, then run the state machine.
    VoiceSlot* speech = sys.voices().allocVoiceChannel();
    CHECK(speech != nullptr);
    char speechPcm[40] = {0};
    CHECK(sys.queue().enqueue(speech, /*delay=*/0, speechPcm, sizeof(speechPcm), 44100));

    int queueMark = static_cast<int>(dev.calls.size());
    // tick beyond the delay window => the line starts (playSample on its voice).
    sys.queue().processNext(/*tick=*/1000);
    int speechPlay = dev.find("playSample", queueMark);
    CHECK(speechPlay >= 0);
    CHECK_EQ(dev.calls[speechPlay].h, speech->handle);
    CHECK(sys.queue().head()->started);

    // device reports the line finished => the queue drains on the next step.
    dev.playing[speech->handle] = false;
    sys.queue().processNext(/*tick=*/2000);
    CHECK(sys.queue().empty());

    // --- ordering sanity: init precedes every playSample; positioned sounds were
    // played before the music stream, which was played before the speech line.
    CHECK_EQ(dev.find("init"), 0);
    CHECK(dev.find("playSample") < musicPlay);
    CHECK(musicPlay < speechPlay);

    // --- shutdown frees the device voices.
    sys.shutdown();
    CHECK(dev.count("shutdown") >= 1 || dev.count("freeVoice") >= kDefaultVoices);
}
