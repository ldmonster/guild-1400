// Unit tests for guild::audio (voice pool, sample bank, 3D math, queue, music).
// Uses a mock shim::IAudioDevice that records every call and (via the audio
// module's IAudioStatusDevice extension) reports voice playback status.
#include "test.h"
#include "audio/sound.h"
#include "audio/voice.h"
#include "audio/voicequeue.h"
#include "audio/sound3d.h"
#include "audio/samplebank.h"
#include "audio/music.h"

#include <climits>
#include <string>
#include <unordered_map>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// Records device calls and lets the test mark voices playing/finished.
struct MockAudio : shim::IAudioDevice, audio::IAudioStatusDevice {
    struct Call {
        std::string op;
        shim::VoiceHandle h = -1;
        int a = 0, b = 0;
    };
    std::vector<Call> calls;
    std::unordered_map<shim::VoiceHandle, bool> playing;
    shim::VoiceHandle next = 0;
    bool initialized = false;

    bool init(int v, int c, int r) override {
        calls.push_back({"init", -1, v, c}); (void)r; initialized = true; return true;
    }
    void shutdown() override { calls.push_back({"shutdown"}); initialized = false; }
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
    const Call* lastOf(const std::string& op) const {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->op == op) return &*it;
        return nullptr;
    }
};

} // namespace

// ---- Voice pool: allocation, recycling, exhaustion --------------------------

TEST(AudioVoice, InitAllocatesOneDeviceVoicePerSlot) {
    MockAudio dev;
    VoicePool pool(&dev);
    CHECK(pool.init(4, 2, 44100));
    CHECK(pool.initialized());
    CHECK_EQ(pool.voiceCount(), 4);
    CHECK_EQ(dev.count("allocVoice"), 4);
    CHECK_EQ(dev.count("init"), 1);
}

TEST(AudioVoice, AllocChannelRecyclesFreeSlots) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(2, 2, 44100);
    // Nothing playing yet: first alloc returns the first slot.
    VoiceSlot* a = pool.allocVoiceChannel();
    CHECK(a != nullptr);
    CHECK_EQ(a, pool.slotAt(0));
    // Mark it playing on the device; next alloc must pick a different slot.
    dev.playing[a->handle] = true;
    VoiceSlot* b = pool.allocVoiceChannel();
    CHECK(b != nullptr);
    CHECK(b != a);
}

TEST(AudioVoice, ExhaustionReturnsInvalidThenFreeingRecycles) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(2, 2, 44100);
    // allocVoiceChannel does not itself reserve a slot (the original relies on the
    // device status to mark a slot busy). Grab slot 0, mark it playing so the next
    // alloc moves on; grab slot 1 and make it looping+playing too.
    VoiceSlot* a = pool.allocVoiceChannel();
    dev.playing[a->handle] = true;
    VoiceSlot* b = pool.allocVoiceChannel();
    CHECK(b != a);
    VoicePool::setLoopFlag(b, true);
    dev.playing[b->handle] = true;
    // Both busy => exhaustion => null (the original's invalid voice).
    CHECK(pool.allocVoiceChannel() == nullptr);
    // Free one (stop + clear loop) and it recycles.
    pool.stopVoice(b, false);
    dev.playing[b->handle] = false;
    VoicePool::setLoopFlag(b, false);
    VoiceSlot* c = pool.allocVoiceChannel();
    CHECK_EQ(c, b);
}

TEST(AudioVoice, AllocStealsLowestPriority) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(3, 2, 44100);
    // All three slots "playing" so pass-1 finds none free... but recycler needs a
    // first non-busy slot to enter pass 2. Make slot 0 free as the seed, then give
    // slots different priorities and verify the lowest-priority free wins.
    pool.slotAt(0)->priority = 50;
    pool.slotAt(1)->priority = 5;
    pool.slotAt(2)->priority = 99;
    VoiceSlot* v = pool.allocVoiceChannel();
    // None playing/looping: lowest priority (slot 1, prio 5) is stolen.
    CHECK_EQ(v, pool.slotAt(1));
}

// ---- Volume / pan clamping (0..127) -----------------------------------------

TEST(AudioVoice, VolumePanClampedToRange) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(1, 2, 44100);
    VoiceSlot* v = pool.allocVoiceChannel();

    // In-range volume is stored (offset +0x28) and pushed to device.
    pool.setVoiceVolume(v, 100);
    CHECK_EQ(v->pan, 100); // +0x28 carries the volume value (crossed offsets)
    CHECK(dev.lastOf("setVolume") != nullptr);
    CHECK_EQ(dev.lastOf("setVolume")->a, 100);

    // 127 is the max accepted.
    pool.setVoiceVolume(v, 127);
    CHECK_EQ(v->pan, 127);

    // 128 and negatives are rejected (a2 < 0x80 gate) — value unchanged.
    int before = v->pan;
    pool.setVoiceVolume(v, 128);
    CHECK_EQ(v->pan, before);
    pool.setVoiceVolume(v, -1);
    CHECK_EQ(v->pan, before);

    // Pan range likewise.
    pool.setVoicePan(v, 64);
    CHECK_EQ(v->volume, 64); // +0x24 carries the pan value
    pool.setVoicePan(v, 200);
    CHECK_EQ(v->volume, 64); // rejected, unchanged
}

// ---- WAVE-11 hardening: index-out-of-range / exhaustion / degenerate buffers -

// slotAt must reject negative and past-the-end indices (no OOB vector access).
TEST(AudioVoice, SlotAtIndexOutOfRange) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(3, 2, 44100);
    CHECK(pool.slotAt(0) != nullptr);
    CHECK(pool.slotAt(2) != nullptr);
    CHECK(pool.slotAt(3) == nullptr);     // == count, past the end
    CHECK(pool.slotAt(99) == nullptr);    // far past the end
    CHECK(pool.slotAt(-1) == nullptr);    // negative
    CHECK(pool.slotAt(-1000) == nullptr);
}

// A 0-voice pool: init with zero (and a negative) voice count must produce an
// empty pool whose allocator returns null without touching an empty vector.
TEST(AudioVoice, ZeroVoicePoolIsSafe) {
    MockAudio dev;
    VoicePool pool(&dev);
    CHECK(pool.init(0, 2, 44100));
    CHECK_EQ(pool.voiceCount(), 0);
    CHECK(pool.allocVoiceChannel() == nullptr); // slots_.empty() guard
    CHECK(pool.slotAt(0) == nullptr);

    MockAudio dev2;
    VoicePool pool2(&dev2);
    CHECK(pool2.init(-5, 2, 44100));            // negative -> clamped to 0 slots
    CHECK_EQ(pool2.voiceCount(), 0);
    CHECK(pool2.allocVoiceChannel() == nullptr);
}

// More concurrent "sounds" than channels: with every slot busy, repeated
// allocation must keep returning null (channel-pool exhaustion) and never index
// past the slot array.
TEST(AudioVoice, ChannelPoolExhaustionRepeated) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(2, 2, 44100);
    VoiceSlot* a = pool.allocVoiceChannel();
    dev.playing[a->handle] = true;
    VoiceSlot* b = pool.allocVoiceChannel();
    dev.playing[b->handle] = true;
    VoicePool::setLoopFlag(b, true);
    // Ask for many more voices than exist: every call sees all busy -> null.
    for (int i = 0; i < 50; ++i)
        CHECK(pool.allocVoiceChannel() == nullptr);
}

// Volume / pan at integer extremes go through the unsigned (<0x80) gate; INT_MIN
// / INT_MAX / 0 / 127 must be handled without UB and the in-range edges stored.
TEST(AudioVoice, VolumePanIntegerExtremes) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(1, 2, 44100);
    VoiceSlot* v = pool.allocVoiceChannel();

    pool.setVoiceVolume(v, 0);    CHECK_EQ(v->pan, 0);    // min accepted
    pool.setVoiceVolume(v, 127);  CHECK_EQ(v->pan, 127);  // max accepted
    int kept = v->pan;
    pool.setVoiceVolume(v, INT_MAX); CHECK_EQ(v->pan, kept); // rejected, unchanged
    pool.setVoiceVolume(v, INT_MIN); CHECK_EQ(v->pan, kept); // rejected (wraps high)
    pool.setVoiceVolume(v, 0x80);    CHECK_EQ(v->pan, kept); // exactly 128 rejected

    pool.setVoicePan(v, 0);     CHECK_EQ(v->volume, 0);
    pool.setVoicePan(v, 127);   CHECK_EQ(v->volume, 127);
    int keptP = v->volume;
    pool.setVoicePan(v, INT_MIN); CHECK_EQ(v->volume, keptP);
    pool.setVoicePan(v, INT_MAX); CHECK_EQ(v->volume, keptP);
}

// startVoice with a 0-length / null PCM buffer must not read the buffer; it just
// forwards (bytes==0) to the device. NULL-device-status voices report not-playing.
TEST(AudioVoice, StartVoiceZeroLengthBuffer) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(1, 2, 44100);
    VoiceSlot* v = pool.allocVoiceChannel();
    // 0-length buffer (malformed/empty sample): no read of pcm, no crash.
    pool.startVoice(v, nullptr, 0, 44100, 0);
    // A non-null pointer with 0 bytes: still must not deref the data.
    static const unsigned char one = 0;
    pool.startVoice(v, &one, 0, 44100, 0);
    // Calling on a null voice is a no-op (guarded).
    pool.startVoice(nullptr, &one, 1, 44100, 0);
    CHECK(true); // reaching here without ASAN abort is the assertion
}

// ---- 3D position -> pan / volume math ---------------------------------------

TEST(Audio3dMath, NearIsLoudAndCentered) {
    Vec3 listener{0, 0, 0};
    Vec3 ahead{0, 0, 100}; // close, straight ahead
    int vol = Compute3dVolume(listener, ahead, 5000.0f, 127);
    CHECK_EQ(vol, 126); // near => loud (golden)
    int pan = Compute3dPan(/*forward*/ Vec3{0, 0, 1}, /*dir*/ Vec3{0, 0, 100});
    CHECK_EQ(pan, 63); // straight ahead => center (golden)
}

TEST(Audio3dMath, FarIsQuieterAndPanned) {
    Vec3 listener{0, 0, 0};
    // Monotonic attenuation golden vector (computed with python oracle).
    CHECK_EQ(Compute3dVolume(listener, Vec3{0, 0, 50000}, 5000.0f, 127), 106);
    CHECK_EQ(Compute3dVolume(listener, Vec3{0, 0, 100000}, 5000.0f, 127), 78);
    CHECK_EQ(Compute3dVolume(listener, Vec3{0, 0, 150000}, 5000.0f, 127), 41);
    // Beyond the radius => silent.
    CHECK_EQ(Compute3dVolume(listener, Vec3{0, 0, 10000000}, 5000.0f, 127), 0);

    // Source to the side => fully panned (sin(90deg)*127*0.5+63 = 126).
    CHECK_EQ(Compute3dPan(Vec3{0, 0, 1}, Vec3{100, 0, 0}), 126);
    // 45 degrees => golden 107.
    CHECK_EQ(Compute3dPan(Vec3{0, 0, 1}, Vec3{100, 0, 100}), 107);
    // Behind => sin(180)=0 => center again.
    CHECK_EQ(Compute3dPan(Vec3{0, 0, 1}, Vec3{0, 0, -100}), 63);
}

// VIBE_Math_VectorAngleBetween @0x5ca334 is a *signed* XZ-plane angle, so the +x and
// -x sides of the listener pan to opposite extremes. Golden values from the binary
// oracle (acos(0)-2*PI => sin=+1 => 126 on the right; -acos(0) => sin=-1 => -0.5
// truncates to 0 on the left). A magnitude-only acos analogue would wrongly give 126
// for both sides — this pins the faithful left/right asymmetry.
TEST(Audio3dMath, PanIsSignedLeftRight) {
    const Vec3 fwd{0, 0, 1};
    CHECK_EQ(Compute3dPan(fwd, Vec3{100, 0, 0}), 126);  // right  => hard right
    CHECK_EQ(Compute3dPan(fwd, Vec3{-100, 0, 0}), 0);   // left   => hard left
    CHECK_EQ(Compute3dPan(fwd, Vec3{100, 0, 100}), 107);  // right-45
    CHECK_EQ(Compute3dPan(fwd, Vec3{-100, 0, 100}), 18);  // left-45
    // Y is ignored (vectors flattened to XZ before the angle).
    CHECK_EQ(Compute3dPan(fwd, Vec3{100, 9999, 0}), 126);
    CHECK_EQ(Compute3dPan(fwd, Vec3{-100, -9999, 0}), 0);
}

// ---- Sample bank indexing ---------------------------------------------------

TEST(AudioSampleBank, FindByNameAndVariation) {
    SampleBank bank;
    bank.addSample("door_open");
    bank.addSample("door_close");
    auto& var = bank.addVariation("footstep");
    var.samples.push_back(SampleRecord{}); var.samples.back().name = "step1";
    var.samples.push_back(SampleRecord{}); var.samples.back().name = "step2";

    CHECK(bank.findSampleByName("door_open") != nullptr);
    CHECK(bank.findSampleByName("door_close") != nullptr);
    CHECK(bank.findSampleByName("missing") == nullptr);

    CHECK(bank.findVariationByName("footstep") != nullptr);
    CHECK(bank.findVariationByName("nope") == nullptr);

    CHECK(bank.findSampleInVariation("footstep", "step2") != nullptr);
    CHECK(bank.findSampleInVariation("footstep", "step9") == nullptr);

    SampleRecord* last = bank.getLastSample("footstep");
    CHECK(last != nullptr);
    CHECK_EQ(last->name, std::string("step2"));
}

TEST(AudioSampleBank, SizeFormulas) {
    SampleBank bank;
    bank.addSample("a");
    bank.addSample("b");
    auto& v = bank.addVariation("v");
    v.samples.push_back(SampleRecord{});
    v.samples.push_back(SampleRecord{});
    v.samples.push_back(SampleRecord{});
    // computeVariationSize = 12*3 + base
    CHECK_EQ(bank.computeVariationSize("v", 100), 12 * 3 + 100);
    CHECK_EQ(bank.computeVariationSize("missing", 100), 0);
    // gilde.exe 0x447614 — disasm-exact formula (S=topSamples, V=variations,
    // C=subSampleCount): 12*(S+C) + 12*V + (S+V)<<6 + 324 + base.  The earlier
    // golden dropped the `+12*V` term (lea/sub/shl @0x44767c..0x447685); fixed to
    // match the binary.  S=2, V=1, C=5 => nodes=S+V=3.
    int expected = 12 * (5 + 2) + 12 * 1 + (3 << 6) + 324 + 1000;
    CHECK_EQ(bank.computeTotalSize(5, 1000), expected);
}

// ---- Voice queue ------------------------------------------------------------

TEST(AudioVoiceQueue, EnqueueProcessAndFinish) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(2, 2, 44100);
    VoiceQueue q(&pool);

    VoiceSlot* v = pool.allocVoiceChannel();
    char pcm[8] = {0};
    CHECK(q.enqueue(v, /*delay=*/10, pcm, sizeof(pcm), 44100));
    CHECK(q.enqueue(nullptr, 0, pcm, 0, 44100) == false); // null voice rejected
    CHECK_EQ(q.size(), 1u);
    CHECK(v->flags & kVoiceFlag_Looping); // enqueue marks looping

    // Before the delay elapses (tick within window) the line does not start.
    q.processNext(/*tick=*/5);
    CHECK(!q.head()->started);
    CHECK_EQ(dev.count("playSample"), 0);

    // After the delay window the line plays.
    q.processNext(/*tick=*/100);
    CHECK(q.head()->started);
    CHECK_EQ(dev.count("playSample"), 1);

    // While the device reports it playing, it stays queued.
    q.processNext(/*tick=*/200);
    CHECK_EQ(q.size(), 1u);

    // When the device says it finished, it is freed and the queue empties.
    dev.playing[v->handle] = false;
    q.processNext(/*tick=*/300);
    CHECK(q.empty());
    CHECK(!(v->flags & kVoiceFlag_Looping)); // loop flag cleared on retire
}

TEST(AudioVoiceQueue, PauseFlagBlocksProcessing) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(1, 2, 44100);
    VoiceQueue q(&pool);
    VoiceSlot* v = pool.allocVoiceChannel();
    char pcm[4] = {0};
    q.enqueue(v, 0, pcm, sizeof(pcm), 44100);
    q.setPaused(true);
    q.processNext(1000);
    CHECK(!q.head()->started);
    CHECK_EQ(dev.count("playSample"), 0);
}

TEST(AudioVoiceQueue, FlushClearsEverything) {
    MockAudio dev;
    VoicePool pool(&dev);
    pool.init(2, 2, 44100);
    VoiceQueue q(&pool);
    VoiceSlot* a = pool.allocVoiceChannel();
    VoiceSlot* b = pool.allocVoiceChannel();
    char pcm[4] = {0};
    q.enqueue(a, 0, pcm, 4, 44100);
    q.enqueue(b, 5, pcm, 4, 44100);
    CHECK_EQ(q.size(), 2u);
    q.flushAll();
    CHECK(q.empty());
    CHECK(!(a->flags & kVoiceFlag_Looping));
    CHECK(!(b->flags & kVoiceFlag_Looping));
}

// ---- Music player -----------------------------------------------------------

TEST(AudioMusic, LoadStartStopTrack) {
    MockAudio dev;
    MusicPlayer music(&dev);
    char pcm[16] = {0};
    MusicTrack* t = music.loadTrack("theme", pcm, sizeof(pcm), 44100, /*loop=*/true);
    CHECK(t != nullptr);
    CHECK(t->active);
    CHECK_EQ(t->volume, kDefaultTrackVolume);
    CHECK(music.isTrackPlaying(t));
    CHECK_EQ(dev.count("playSample"), 1);
    // Loop => loops==0 in the device call.
    CHECK_EQ(dev.lastOf("playSample")->b, 0);

    // Loading the same active track again is a no-op (find returns it).
    int plays = dev.count("playSample");
    MusicTrack* t2 = music.loadTrack("theme", pcm, sizeof(pcm), 44100, true);
    CHECK_EQ(t2, t);
    CHECK_EQ(dev.count("playSample"), plays);

    music.stopTrack(t, /*fade=*/false);
    CHECK(!t->active);
    CHECK_EQ(dev.count("stop"), 1);
}

TEST(AudioMusic, FadeModifierScalesMasterVolume) {
    MockAudio dev;
    MusicPlayer music(&dev);
    music.applyMasterVolume(100); // modifier 1.0 => 100
    CHECK_EQ(dev.lastOf("setMasterVolume")->a, 100);

    music.setFadeVolume(0.5f, 2000); // modifier 0.5 => 100*0.5 = 50
    CHECK_EQ(dev.lastOf("setMasterVolume")->a, 50);

    // Out-of-range modifiers are rejected (guard 0..1); master stays as last.
    music.setFadeVolume(2.0f, 0);
    CHECK_EQ(dev.lastOf("setMasterVolume")->a, 50);

    // Clamp: a negative modifier set directly is clamped to 0 on apply.
    music.setFadeVolume(0.0f, 0);
    CHECK_EQ(dev.lastOf("setMasterVolume")->a, 0);
}
