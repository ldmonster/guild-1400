// =====================================================================================
// audio_recon_dispatch_test.cpp — golden tests for the engine audio glue layer:
// command dispatchers, ambient-track control, 3d bind, sample-bank create.
// =====================================================================================
#include "tests/framework/test.h"
#include "audio/audio_recon_engine.h"

#include <cstring>
#include <vector>

using namespace guild::audio::recon;
using Addr = guild::audio::recon::Addr;

namespace {
static int& I(std::vector<char>& v, size_t off) { return *reinterpret_cast<int*>(v.data() + off); }
static unsigned char& B(std::vector<char>& v, size_t off) { return *reinterpret_cast<unsigned char*>(v.data() + off); }

// shared capture targets (function-pointer hooks can't capture)
static int g_loopSet;        static Addr g_voicePlayed;
static Addr g_allocReturn;   static int g_libInitArgs[4];
}

// ====================================================================================
TEST(AudioReconDispatch, CmdPlaySampleResolvesSetsLoopAndPlays) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto voice = std::vector<char>(64, 0);
    g_allocReturn = (Addr)voice.data();
    g_loopSet = -1; g_voicePlayed = 0;

    e.hooks.playSample = [](int, Addr) -> Addr { return g_allocReturn; };
    e.hooks.setSampleLoopCount = [](Addr v) { g_loopSet = (int)*reinterpret_cast<int*>(v + 12); };
    e.hooks.playVoiceSample = [](Addr v, int) { g_voicePlayed = v; };

    int loop = 3;
    Addr r = e.cmdPlaySample(2, 0x1000, &loop);
    CHECK_EQ(r, (Addr)voice.data());
    CHECK_EQ(I(voice, 12), 3);           // loop count written
    CHECK_EQ(g_loopSet, 3);              // MSS loop-count hook saw it
    CHECK_EQ(g_voicePlayed, (Addr)voice.data());

    // null sample -> early 0, no loop/play
    g_allocReturn = 0; g_voicePlayed = 0;
    CHECK_EQ(e.cmdPlaySample(2, 0x1000, &loop), (Addr)0);
    CHECK_EQ(g_voicePlayed, (Addr)0);
}

// ====================================================================================
TEST(AudioReconDispatch, CmdPlaySample3DOneShotBranch) {
    AudioEngine e;
    auto voice = std::vector<char>(64, 0);
    static Addr oneShotVoice; oneShotVoice = 0;
    static int oneShotBaseVol; oneShotBaseVol = 0;
    g_allocReturn = (Addr)voice.data();
    e.hooks.playSample = [](int, Addr) -> Addr { return g_allocReturn; };
    e.hooks.sound3dPlayOneShot = [](Addr v, Addr, int baseVol, float) {
        oneShotVoice = v; oneShotBaseVol = baseVol;
    };
    int pos = 0, vol = 7, mode = 1;
    Addr r = e.cmdPlaySample3D(&pos, &vol, &mode, 0);
    CHECK_EQ(r, (Addr)voice.data());
    CHECK_EQ(oneShotVoice, (Addr)voice.data());
    CHECK_EQ(oneShotBaseVol, 60);        // baseVol constant 60
}

// ====================================================================================
TEST(AudioReconDispatch, CmdStopSampleStopsBoundVoice) {
    AudioEngine e;
    auto ref = std::vector<char>(8, 0);
    e.addrAt((Addr)ref.data(), 0) = (Addr)0xCAFE;   // arg block: voice pointer @+0
    static Addr stopped; stopped = 0;
    e.hooks.stopVoice = [](Addr v, int) { stopped = v; };
    CHECK_EQ(e.cmdStopSample((Addr)ref.data()), 0);
    CHECK_EQ(stopped, (Addr)0xCAFE);
}

// ====================================================================================
TEST(AudioReconDispatch, Sound3dBindHandleStartsWhenNotPlayed) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto entry = std::vector<char>(96, 0);
    auto bound = std::vector<char>(64, 0);
    e.addrAt((Addr)bound.data(), 4) = (Addr)0x1;  // *(boundVoice+4) != 0 (has sample)
    e.addrAt((Addr)entry.data(), 52) = (Addr)bound.data();  // entry[13] boundVoice
    I(entry, 80) = 0;                             // v4[20] hasPlayed == 0 -> play path
    static Addr playedVoice; playedVoice = 0;
    e.hooks.playVoiceSample = [](Addr v, int) { playedVoice = v; };
    e.hooks.setSampleLoopCount = [](Addr) {};

    Addr r = e.sound3dBindHandle((Addr)entry.data(), 4, 0);
    CHECK_EQ(I(bound, 12), 4);                    // loop count pushed to voice
    CHECK_EQ(I(entry, 68), 4);                    // entry[17] loop mirror
    CHECK_EQ(playedVoice, (Addr)bound.data());
    CHECK_EQ(r, (Addr)entry.data());
}

// ====================================================================================
TEST(AudioReconDispatch, PlayAmbientTrackDisabledReturnsSentinel) {
    AudioEngine e;
    e.st.ambientEnabled = 0;
    CHECK(e.playAmbientTrack("music", 100) != 0);  // returns &unk_764890 sentinel
}

// ====================================================================================
TEST(AudioReconDispatch, PlayAmbientTrackUsesFreeSlotAndStarts) {
    AudioEngine e;
    e.st.ambientEnabled = 1;
    e.st.trackSysEnabled = 1;
    auto slot = std::vector<char>(296, 0xAB);     // dirty memory to verify zero-init
    static Addr g_slot; g_slot = (Addr)slot.data();
    e.hooks.findActiveTrackSlot = []() -> Addr { return 0; };       // none active
    e.hooks.findFreeVoiceSlot   = []() -> Addr { return g_slot; };
    static int started; started = 0;
    e.hooks.startTrack = [](Addr, int) -> int { started = 1; return 0; };  // success

    Addr r = e.playAmbientTrack("ambient.wav", 77);
    CHECK_EQ(r, g_slot);
    CHECK_EQ((int)B(slot, 262), 1);               // marked music
    CHECK_EQ(std::strcmp(slot.data(), "ambient.wav"), 0);
    CHECK_EQ(started, 1);
    CHECK_EQ(I(slot, 280), 77);                   // volume stored via setVoicePosition
}

// ====================================================================================
TEST(AudioReconDispatch, InitDefaultMixerForwardsLibInitArgs) {
    AudioEngine e;
    e.hooks.soundLibInit = [](int a, int b, int c, int d) -> int {
        g_libInitArgs[0] = a; g_libInitArgs[1] = b;
        g_libInitArgs[2] = c; g_libInitArgs[3] = d;
        return 0;
    };
    e.initDefaultMixer();
    CHECK_EQ(g_libInitArgs[0], 0);
    CHECK_EQ(g_libInitArgs[1], 3);
    CHECK_EQ(g_libInitArgs[2], 2);
    CHECK_EQ(g_libInitArgs[3], 44100);
}

// ====================================================================================
TEST(AudioReconDispatch, SampleBankCreateAllocatesAndCounts) {
    AudioEngine e;
    auto blk = std::vector<char>(64, 0xCD);
    static Addr g_blk; g_blk = (Addr)blk.data();
    static int g_destroy; g_destroy = 0;
    static char g_name[64]; g_name[0] = 0;
    e.hooks.sampleBankDestroy = []() { ++g_destroy; };
    e.hooks.memAlloc = [](int, const char*) -> Addr { return g_blk; };
    e.hooks.strNCopyPad = [](Addr d, const char* s, int) {
        std::strncpy(reinterpret_cast<char*>(d), s, 50);
        std::strncpy(g_name, s, 50);
    };

    int rc = e.sampleBankCreate("Bank1");
    CHECK_EQ(rc, 0);
    CHECK_EQ(g_destroy, 1);
    CHECK_EQ(e.st.pendingBank, g_blk);
    CHECK_EQ(e.st.bankSeq, 1);
    CHECK_EQ(std::strcmp(g_name, "Bank1"), 0);

    // a second create with a pending bank -> -1 (after destroy hook fires again)
    int rc2 = e.sampleBankCreate("Bank2");
    CHECK_EQ(rc2, -1);
    CHECK_EQ(g_destroy, 2);
}
