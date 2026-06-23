// =====================================================================================
// audio_recon_engine_test.cpp — golden-vector tests for the 1:1 engine audio recon.
// Exercises slot bookkeeping, oldest-active scan, master-enable, loop-count, the
// crossfade/master-volume math in MixerUpdate, and the bank/voice list walks.
// Self-contained: builds real backing memory for the engine records.
// =====================================================================================
#include "tests/framework/test.h"
#include "audio/audio_recon_engine.h"

#include <cstring>
#include <vector>
#include <string>

using namespace guild::audio::recon;
using Addr = guild::audio::recon::Addr;

// ---- backing storage helpers --------------------------------------------------------
namespace {

// A bank header is 324 bytes (voiceCount@308, voices@312, next@316, mark@320).
struct BankBuf { std::vector<char> mem = std::vector<char>(324, 0); Addr a() { return (Addr)mem.data(); } };
// Voice records are 64 bytes; an array of them.
static std::vector<char> makeVoices(int n) { return std::vector<char>((size_t)n * 64, 0); }
// Channel table: 48-byte entries.
static std::vector<char> makeChannels(int n) { return std::vector<char>((size_t)n * 48, 0); }
// Track-slot table: 10 slots of 296 bytes = 2960.
static std::vector<char> makeSlots() { return std::vector<char>(2960, 0); }

static int& I(std::vector<char>& v, size_t off) { return *reinterpret_cast<int*>(v.data() + off); }
static unsigned char& B(std::vector<char>& v, size_t off) { return *reinterpret_cast<unsigned char*>(v.data() + off); }

} // namespace

// ====================================================================================
TEST(AudioReconEngine, SetMasterEnableStoresAndReturns) {
    AudioEngine e;
    CHECK_EQ(e.setMasterEnable(7), 7);
    CHECK_EQ(e.st.masterEnable, 7);
    CHECK_EQ(e.setMasterEnable(0), 0);
    CHECK_EQ(e.st.masterEnable, 0);
}

// ====================================================================================
TEST(AudioReconEngine, FindBankByNameWalksList) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    BankBuf b0, b1, b2;
    std::strcpy(b0.mem.data(), "alpha");
    std::strcpy(b1.mem.data(), "Beta");      // case-insensitive match target
    std::strcpy(b2.mem.data(), "gamma");
    e.addrAt(b0.a(), 316) = b1.a();
    e.addrAt(b1.a(), 316) = b2.a();
    e.addrAt(b2.a(), 316) = 0;
    e.st.bankListHead = b0.a();

    CHECK_EQ(e.findBankByName("alpha"), b0.a());
    CHECK_EQ(e.findBankByName("beta"), b1.a());   // case-insensitive
    CHECK_EQ(e.findBankByName("gamma"), b2.a());
    CHECK_EQ(e.findBankByName("missing"), (Addr)0);
}

// ====================================================================================
TEST(AudioReconEngine, GetLastBankReturnsTail) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    BankBuf b0, b1;
    e.addrAt(b0.a(), 316) = b1.a();
    e.addrAt(b1.a(), 316) = 0;
    e.st.bankListHead = b0.a();
    CHECK_EQ(e.getLastBank(), b1.a());

    // single bank
    e.st.bankListHead = b1.a();
    CHECK_EQ(e.getLastBank(), b1.a());

    // not live -> sentinel (non-null)
    e.st.soundEnabled = 0;
    CHECK(e.getLastBank() != 0);
}

// ====================================================================================
TEST(AudioReconEngine, FindBankContainingVoiceScansVoiceArray) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    BankBuf b0, b1;
    auto v0 = makeVoices(3);
    auto v1 = makeVoices(4);
    AudioEngine::i32at(b0.a(), 308) = 3;
    e.addrAt(b0.a(), 312) = (Addr)v0.data();
    e.addrAt(b0.a(), 316) = b1.a();
    AudioEngine::i32at(b1.a(), 308) = 4;
    e.addrAt(b1.a(), 312) = (Addr)v1.data();
    e.addrAt(b1.a(), 316) = 0;
    e.st.bankListHead = b0.a();

    Addr voiceInB1 = (Addr)v1.data() + 2 * 64;
    CHECK_EQ(e.findBankContainingVoice(voiceInB1), b1.a());
    Addr voiceInB0 = (Addr)v0.data() + 1 * 64;
    CHECK_EQ(e.findBankContainingVoice(voiceInB0), b0.a());
    CHECK_EQ(e.findBankContainingVoice(0xDEAD), (Addr)0);
}

// ====================================================================================
TEST(AudioReconEngine, FindVoiceBySampleMatchesBoundVoiceField) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto ch = makeChannels(4);
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 4;
    // bind voice 0xABCD at channel index 2 (boundVoice pointer field @+4)
    e.addrAt(e.st.sampleTable + 2 * 48, 4) = (Addr)0xABCD;

    int idx = 0;
    Addr hit = e.findVoiceBySample(0xABCD, &idx);
    CHECK_EQ(hit, e.st.sampleTable + (Addr)(2 * 48));
    CHECK_EQ(idx, 2);

    // not found
    idx = 0;
    CHECK_EQ(e.findVoiceBySample(0x1111, &idx), (Addr)0);

    // not live -> sentinel
    e.st.soundEnabled = 0;
    idx = 0;
    CHECK(e.findVoiceBySample(0xABCD, &idx) != 0);
}

// ====================================================================================
TEST(AudioReconEngine, SetVoiceLoopCountWritesField12AndHooks) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto voice = makeChannels(1);
    Addr v = (Addr)voice.data();
    int hookCalls = 0;
    static int* pCalls = &hookCalls;
    e.hooks.setSampleLoopCount = [](Addr) { ++*pCalls; };
    // 0x4475cb: live+nonzero path returns VIBE_Audio_SetSampleLoopCount() (eax). That
    // leaf is the Miles/MSS boundary; its not-live eax is -1 (the faithful default).
    CHECK_EQ(e.setVoiceLoopCount(v, 5), (Addr)(guild::i32)-1);
    CHECK_EQ(I(voice, 12), 5);
    CHECK_EQ(hookCalls, 1);

    // soundEnabled off -> no write, returns voice unchanged (eax in = voice)
    e.st.soundEnabled = 0;
    I(voice, 12) = 99;
    CHECK_EQ(e.setVoiceLoopCount(v, 1), v);
    CHECK_EQ(I(voice, 12), 99);
}

// ====================================================================================
TEST(AudioReconEngine, CountActiveVoicesGoldenVector) {
    AudioEngine e;
    auto ch = makeChannels(5);
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 5;
    Addr T = e.st.sampleTable;
    // Original branch (status != 4):
    //   if (!field24 || loop==1 || (flag&0xE)) { if (flag&0x10) ++v3; } else ++v3;
    // channel 0: bound + status==4 -> counts (playing branch)
    e.addrAt(T + 0 * 48, 4) = 1;
    // channel 1: bound, status!=4, field24==0 -> enters if-branch; flag 0x10 unset -> NOT counted
    e.addrAt(T + 1 * 48, 4) = 1; I(ch, 1 * 48 + 24) = 0;
    // channel 2: bound, status!=4, field24!=0, loop==1 -> if-branch; flag 0x10 set -> counts
    e.addrAt(T + 2 * 48, 4) = 1; I(ch, 2 * 48 + 24) = 1; I(ch, 2 * 48 + 12) = 1;
    B(ch, 2 * 48 + 20) = 0x10;
    // channel 3: bound, status!=4, field24!=0, loop!=1, busy bits 0 -> else-branch -> counts
    e.addrAt(T + 3 * 48, 4) = 1; I(ch, 3 * 48 + 24) = 1; I(ch, 3 * 48 + 12) = 2;
    B(ch, 3 * 48 + 20) = 0x00;
    // channel 4: not bound -> never counts (no boundVoice set)

    // status hook: channel 0 -> 4, others -> 1
    static Addr base; base = e.st.sampleTable;
    e.hooks.sampleStatus = [](Addr c) -> int { return (c == base) ? 4 : 1; };

    // counts: ch0 (playing) + ch2 (reserved) + ch3 (managed-else) = 3
    CHECK_EQ(e.countActiveVoices(), 3);
}

// ====================================================================================
TEST(AudioReconEngine, FindOldestActiveSamplePicksLowestPriority) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    e.st.sampleCount = 0;            // no channels -> the post-scan "playing?" loop is empty
    BankBuf bank;
    auto voices = makeVoices(3);
    AudioEngine::i32at(bank.a(), 308) = 3;
    e.addrAt(bank.a(), 312) = (Addr)voices.data();
    e.addrAt(bank.a(), 316) = 0;
    e.st.bankListHead = bank.a();
    Addr V = (Addr)voices.data();
    // loaded (+56 = loadedData pointer) voices with priorities at +60
    e.addrAt(V + 0 * 64, 56) = 0x1000; I(voices, 0 * 64 + 60) = 100;
    e.addrAt(V + 1 * 64, 56) = 0x1000; I(voices, 1 * 64 + 60) = 40;   // oldest (lowest)
    e.addrAt(V + 2 * 64, 56) = 0x1000; I(voices, 2 * 64 + 60) = 70;

    Addr oldest = e.findOldestActiveSample();
    CHECK_EQ(oldest, (Addr)voices.data() + 1 * 64);
}

// ====================================================================================
TEST(AudioReconEngine, FreeMemoryForLoadEvictsUntilBudget) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    e.st.sampleCount = 0;
    e.st.memBudget = 1000;
    e.st.memUsed = 900;            // free = 100, need 200 -> must evict
    BankBuf bank;
    auto voices = makeVoices(2);
    AudioEngine::i32at(bank.a(), 308) = 2;
    e.addrAt(bank.a(), 312) = (Addr)voices.data();
    e.addrAt(bank.a(), 316) = 0;
    e.st.bankListHead = bank.a();

    // each voice loaded; loadKind streamed (!=1) -> uses payload at +8.
    // build small data blocks: data[+8] = payload size.
    std::vector<char> d0(16, 0), d1(16, 0);
    *reinterpret_cast<int*>(d0.data() + 8) = 300;  // freeing voice0 reclaims 300+12=312
    *reinterpret_cast<int*>(d1.data() + 8) = 50;
    Addr V = (Addr)voices.data();
    e.addrAt(V + 0 * 64, 56) = (Addr)d0.data(); I(voices, 0 * 64 + 60) = 10; // oldest
    e.addrAt(V + 1 * 64, 56) = (Addr)d1.data(); I(voices, 1 * 64 + 60) = 20;
    B(voices, 0 * 64 + 54) = 0; B(voices, 1 * 64 + 54) = 0;

    int frees = 0; static int* pf; pf = &frees;
    e.hooks.memFree = [](Addr) { ++*pf; };

    int rc = e.freeMemoryForLoad(200);
    CHECK_EQ(rc, 0);                          // succeeded
    CHECK_EQ(frees, 1);                       // evicting oldest (300+12) freed 312, now free=412
    CHECK_EQ(e.st.memUsed, 900 - 312);
}

// ====================================================================================
TEST(AudioReconEngine, UpdatePlaybackArmsAndStamps) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    e.st.sampleClock = 10;                    // 13*10 = 130
    auto ch = makeChannels(2);
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 2;
    e.addrAt(e.st.sampleTable + 0 * 48, 4) = 0x1;   // channel 0 bound
    // channel 1 free (no boundVoice)
    static Addr cbase; cbase = e.st.sampleTable;
    e.hooks.sampleStatus = [](Addr c) -> int { return (c == cbase) ? 4 : 0; };

    int r = e.updatePlayback(42);
    CHECK_EQ(e.st.digitalOutput, 42);
    CHECK_EQ(e.st.playbackActive, 1);
    CHECK_EQ(I(ch, 0 * 48 + 16), 130);        // stamped 13*clock
    CHECK_EQ(I(ch, 1 * 48 + 16), 0);          // free channel untouched
    CHECK_EQ(r, 2);                           // returns sampleCount on the armed path
}

// ====================================================================================
TEST(AudioReconEngine, UpdatePlaybackStopPath) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto ch = makeChannels(1);
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 1;
    e.addrAt(e.st.sampleTable, 4) = 0x1;
    static int stops; stops = 0;
    e.hooks.sampleStatus = [](Addr) -> int { return 4; };
    e.hooks.stopVoice = [](Addr, int) { ++stops; };
    int r = e.updatePlayback(0);
    CHECK_EQ(stops, 1);
    CHECK_EQ(r, 0);
}

// ====================================================================================
// MixerUpdate fade-in math golden vector.
//   slot used-flag (+260) = 1, has stream (+256)=h, music flag (+262)=0, status==4,
//   fadeMode (+276)=1, fadeStart(+272)=0, posMs(+268)=50, target(+280)=100,
//   fadeLen(+288)=0 -> v13 = trackMasterClock. With clock=200, v14=50, v13=200:
//   v13 > v14, not <=, vol = 100*50/200 = 25, |100-25|=75 >= 2, v13 !< v14 -> not done.
//   So fadeMode stays 1 and setStreamVolume(h, 25).
// ====================================================================================
TEST(AudioReconEngine, MixerUpdateFadeInComputesVolume) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    e.st.trackMasterClock = 200;
    auto slots = makeSlots();
    e.st.trackSlots = (Addr)slots.data();
    B(slots, 260) = 1;                     // slot 0 used/active (byte_62DB34 == base+260)
    I(slots, 256) = 0x777;                 // stream handle
    B(slots, 262) = 0;                     // not music
    B(slots, 276) = 1;                     // fade-in
    I(slots, 272) = 0;                     // fadeStart
    I(slots, 268) = 50;                    // posMs
    I(slots, 280) = 100;                   // target vol
    I(slots, 288) = 0;                     // fadeLen (=> v13 = clock)

    static int lastVol; lastVol = -1;
    static int lastStream; lastStream = -1;
    e.hooks.streamStatus = [](int) -> int { return 4; };          // still playing
    e.hooks.getStreamMsPosition = [](int, Addr) {};
    e.hooks.setStreamVolume = [](int s, int v) { lastStream = s; lastVol = v; };

    e.mixerUpdate();
    CHECK_EQ(lastStream, 0x777);
    CHECK_EQ(lastVol, 25);
    CHECK_EQ((int)B(slots, 276), 1);       // fade not complete
}

// ====================================================================================
// MixerUpdate fade-in completion: posMs large enough so v13 <= v14 -> done; volume
// clamped to target and fadeMode cleared.
// ====================================================================================
TEST(AudioReconEngine, MixerUpdateFadeInCompletes) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    e.st.trackMasterClock = 200;
    auto slots = makeSlots();
    e.st.trackSlots = (Addr)slots.data();
    B(slots, 260) = 1;                     // used/active (byte_62DB34 == base+260)
    I(slots, 256) = 0x10;
    B(slots, 276) = 1;
    I(slots, 272) = 0;
    I(slots, 268) = 250;                   // v14 = 250 >= v13(200) -> done
    I(slots, 280) = 100;
    I(slots, 288) = 0;

    static int lastVol; lastVol = -1;
    e.hooks.streamStatus = [](int) -> int { return 4; };
    e.hooks.getStreamMsPosition = [](int, Addr) {};
    e.hooks.setStreamVolume = [](int, int v) { lastVol = v; };
    e.mixerUpdate();
    CHECK_EQ((int)B(slots, 276), 0);       // complete
    CHECK_EQ(I(slots, 284), 100);          // clamped to target
    CHECK_EQ(lastVol, 100);
}

// ====================================================================================
// Master-volume fade tail: linear interpolation base + (target-base)/len * elapsed.
//   base=0, target=100, len=1000, clock => 13*clock - fadeStart = 500 -> cur=50.
// ====================================================================================
TEST(AudioReconEngine, MixerUpdateMasterVolumeFade) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    e.st.trackSlots = 0;                   // no slots: skip slot loop (count is fixed 10,
    // but used-flag read at +4 would deref null) -> give a zeroed slot table instead:
    auto slots = makeSlots();
    e.st.trackSlots = (Addr)slots.data();  // all used-flags 0 -> loop body skipped
    e.st.sampleClock = 100;                // 13*100 = 1300
    e.st.masterFadeStart = 800;            // elapsed = 500
    e.st.masterFadeLen = 1000;
    e.st.masterVolBase = 0.0f;
    e.st.masterVolTarget = 100.0f;
    static int applied; applied = 0;
    e.hooks.applyMasterVolume = [](int, int) { ++applied; };

    e.mixerUpdate();
    CHECK(e.st.masterVolCur > 49.9f && e.st.masterVolCur < 50.1f);
    CHECK_EQ(applied, 1);
    CHECK(e.st.masterVolTarget >= 0.0f);   // still fading

    // elapsed >= len -> snap to target, target reset to -1
    e.st.masterFadeStart = 0;              // elapsed = 1300 >= 1000
    applied = 0;
    e.mixerUpdate();
    CHECK(e.st.masterVolCur > 99.9f && e.st.masterVolCur < 100.1f);
    CHECK(e.st.masterVolTarget < 0.0f);
    CHECK_EQ(applied, 1);
}

// ====================================================================================
TEST(AudioReconEngine, SetVoicePositionAppliesWhenActiveAndNotFading) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    auto slots = makeSlots();
    Addr slot = (Addr)slots.data();
    I(slots, 256) = 0x55;                  // stream handle
    B(slots, 276) = 0;                     // not fading
    B(slots, 260) = 1;                     // active
    static int v; v = -1;
    e.hooks.setStreamVolume = [](int, int vol) { v = vol; };
    e.setVoicePosition(slot, 88);
    CHECK_EQ(I(slots, 280), 88);           // target stored
    CHECK_EQ(I(slots, 284), 88);           // current applied
    CHECK_EQ(v, 88);

    // fading -> only stores target, no apply
    B(slots, 276) = 1; v = -1; I(slots, 284) = 0;
    e.setVoicePosition(slot, 5);
    CHECK_EQ(I(slots, 280), 5);
    CHECK_EQ(v, -1);
}

// ====================================================================================
TEST(AudioReconEngine, Sound3dStopEntryUsesField52) {
    AudioEngine e;
    auto entry = makeChannels(2);          // need >=56 bytes
    Addr en = (Addr)entry.data();
    e.addrAt(en, 52) = (Addr)0x1234;
    static Addr stopped; stopped = 0;
    e.hooks.stopVoice = [](Addr c, int) { stopped = c; };
    Addr r = e.sound3dStopEntry(en, 9);
    CHECK_EQ(stopped, (Addr)0x1234);
    CHECK_EQ(r, (Addr)0x1234);

    // no bound voice -> returns entry, no stop
    e.addrAt(en, 52) = 0; stopped = 0;
    CHECK_EQ(e.sound3dStopEntry(en, 9), en);
    CHECK_EQ(stopped, (Addr)0);
}

// ====================================================================================
TEST(AudioReconEngine, StrCmpNoCaseNGolden) {
    CHECK_EQ(vibeStrCmpNoCaseN("ABC", "abc", 50), 0);
    CHECK_EQ(vibeStrCmpNoCaseN("Hello", "Hello", 50), 0);
    CHECK(vibeStrCmpNoCaseN("abc", "abd", 50) != 0);
    CHECK_EQ(vibeStrCmpNoCaseN("abcXXX", "abcYYY", 3), 0);   // bounded
    CHECK(vibeStrCmpNoCaseN("abcXXX", "abcYYY", 4) != 0);
}

// ====================================================================================
TEST(AudioReconEngine, Abs64LoMatchesTruncatedLLAbs) {
    CHECK_EQ(AudioEngine::abs64lo(5), 5);
    CHECK_EQ(AudioEngine::abs64lo(-5), 5);
    CHECK_EQ(AudioEngine::abs64lo(0), 0);
    CHECK_EQ(AudioEngine::abs64lo(-1), 1);
    CHECK_EQ(AudioEngine::abs64lo((guild::i64)-100), 100);
}

// ====================================================================================
// WAVE-11 hardening: degenerate-size / index-out-of-range edge cases. Each drives
// the real entry with an empty table / index past the count / extreme values; ASAN
// must see every loop stay in bounds and no scan run off a zero-length allocation.
// ====================================================================================

// Zero-channel table: the count/playback scans must not deref the (null/empty)
// table and must return 0 cleanly (the count-> 0 guard).
TEST(AudioReconEngine, ZeroChannelsCountAndUpdate) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    e.st.sampleTable = 0;                 // no table
    e.st.sampleCount = 0;
    CHECK_EQ(e.countActiveVoices(), 0);   // sampleTable null -> v3 stays 0
    CHECK_EQ(e.updatePlayback(0), 0);     // sampleCount 0 -> loop skipped
    // Armed path with 0 channels: returns the passed-in result (loop skipped).
    CHECK_EQ(e.updatePlayback(7), 7);
    CHECK_EQ(e.st.playbackActive, 1);

    // A non-null but zero-length table with count 0 must also stay in bounds.
    auto ch = makeChannels(1);            // 1 entry of backing, count says 0
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 0;
    CHECK_EQ(e.countActiveVoices(), 0);
}

// findVoiceBySample with the starting *idx at or past sampleCount must return 0
// without scanning past the table (the >= sampleCount guard).
TEST(AudioReconEngine, FindVoiceBySampleIdxOutOfRange) {
    AudioEngine e;
    e.st.soundEnabled = 1;
    auto ch = makeChannels(3);
    e.st.sampleTable = (Addr)ch.data();
    e.st.sampleCount = 3;
    e.addrAt(e.st.sampleTable + 2 * 48, 4) = (Addr)0xBEEF;

    int idx = 3;                          // == count -> immediate 0, no read
    CHECK_EQ(e.findVoiceBySample(0xBEEF, &idx), (Addr)0);
    idx = 99;                             // far past count -> still 0, no OOB
    CHECK_EQ(e.findVoiceBySample(0xBEEF, &idx), (Addr)0);
    // Start exactly at the matching index boundary.
    idx = 2;
    CHECK_EQ(e.findVoiceBySample(0xBEEF, &idx), e.st.sampleTable + (Addr)(2 * 48));
    CHECK_EQ(idx, 2);
}

// mixerUpdate with the track system enabled but every slot's used-flag clear
// (0 active voices): the 10-slot loop body is skipped and only the (idle) master
// fade tail runs. Must not touch any slot fields.
TEST(AudioReconEngine, MixerUpdateZeroActiveSlots) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    auto slots = makeSlots();             // all used-flags (+4) == 0
    e.st.trackSlots = (Addr)slots.data();
    e.st.masterVolTarget = -1.0f;         // no master fade in progress
    static int vol; vol = 0;
    static int applied; applied = 0;
    e.hooks.setStreamVolume = [](int, int) { ++vol; };
    e.hooks.applyMasterVolume = [](int, int) { ++applied; };
    e.mixerUpdate();
    CHECK_EQ(vol, 0);                     // no slot processed
    CHECK_EQ(applied, 0);                 // master fade idle
}

// Fade-in with extreme target volume and zero fadeLen (=> v13 = clock), posMs at
// the start so v14 == 0: v13 <= v14 is false but the volume term computes 0; the
// done-test then triggers via |target-0| < 2? no (target huge) -> sets vol and
// continues. Exercises the boundary arithmetic without any division by zero.
TEST(AudioReconEngine, MixerUpdateFadeInExtremeTargetZeroLen) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    e.st.trackMasterClock = 1;           // v13 = clock (fadeLen 0)
    auto slots = makeSlots();
    e.st.trackSlots = (Addr)slots.data();
    B(slots, 260) = 1;                   // used/active (byte_62DB34 == base+260)
    I(slots, 256) = 0x1;                 // stream handle
    B(slots, 262) = 0;                   // not music
    B(slots, 276) = 1;                   // fade-in
    I(slots, 272) = 0;                   // fadeStart
    I(slots, 268) = 0;                   // posMs == fadeStart -> v14 = 0
    I(slots, 280) = 1000000;            // extreme target (slot+280 != 0, safe div)
    I(slots, 288) = 0;                   // fadeLen 0
    static int lastVol; lastVol = -123;
    e.hooks.streamStatus = [](int) -> int { return 4; };
    e.hooks.getStreamMsPosition = [](int, Addr) {};
    e.hooks.setStreamVolume = [](int, int v) { lastVol = v; };
    e.mixerUpdate();
    // v14=0 -> volume term = target*0/v13 = 0; |target-0| = 1000000 >= 2, and
    // v13(1) < (268-272)=0 is false -> not done; setStreamVolume(h, 0).
    CHECK_EQ(lastVol, 0);
}

// Master-volume fade exactly at the boundary (elapsed == len): the unsigned
// compare (v4 < len) is false, so it snaps to target and clears the fade.
TEST(AudioReconEngine, MasterVolumeFadeAtExactBoundary) {
    AudioEngine e;
    e.st.trackSysEnabled = 1;
    auto slots = makeSlots();
    e.st.trackSlots = (Addr)slots.data();
    e.st.sampleClock = 100;              // 13*100 = 1300
    e.st.masterFadeStart = 300;          // elapsed = 1000
    e.st.masterFadeLen = 1000;           // == elapsed -> boundary (not < len)
    e.st.masterVolBase = 10.0f;
    e.st.masterVolTarget = 90.0f;
    static int applied; applied = 0;
    e.hooks.applyMasterVolume = [](int, int) { ++applied; };
    e.mixerUpdate();
    CHECK(e.st.masterVolCur > 89.9f && e.st.masterVolCur < 90.1f); // snapped
    CHECK(e.st.masterVolTarget < 0.0f);  // fade cleared
    CHECK_EQ(applied, 1);
}
