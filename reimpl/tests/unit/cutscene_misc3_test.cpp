// Unit tests for the cutscene_misc3 deterministic kernels (per-type cutscene
// mains + salon/lease/duel siblings). Golden vectors computed with python
// against the recovered tables/constants; the flow drivers are exercised over a
// recording hook to prove the call sequence is faithful and gate-respecting.
#include "test.h"

#include "sim/cutscene_misc3.h"
#include "sim/cutscene.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Kernels.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc3, SeasonWindowIndex) {
    // table {8,10,12,15,18,20}; index = first threshold strictly > day.
    CHECK_EQ(CutsceneSeasonWindowIndex(0), 0);   // < 8
    CHECK_EQ(CutsceneSeasonWindowIndex(7), 0);   // < 8
    CHECK_EQ(CutsceneSeasonWindowIndex(8), 1);   // 8 not < 8; < 10
    CHECK_EQ(CutsceneSeasonWindowIndex(9), 1);
    CHECK_EQ(CutsceneSeasonWindowIndex(13), 3);  // 13 < 15
    CHECK_EQ(CutsceneSeasonWindowIndex(19), 5);  // < 20
    CHECK_EQ(CutsceneSeasonWindowIndex(20), 6);  // no window
    CHECK_EQ(CutsceneSeasonWindowIndex(99), 6);
}

TEST(CutsceneMisc3, BirthVoiceBase) {
    CHECK_EQ(CutsceneBirthVoiceBase(false, false), 0);
    CHECK_EQ(CutsceneBirthVoiceBase(true,  false), 2);  // father ill
    CHECK_EQ(CutsceneBirthVoiceBase(false, true),  4);  // mother ill
    CHECK_EQ(CutsceneBirthVoiceBase(true,  true),  6);  // both ill
}

TEST(CutsceneMisc3, BankruptcyMessageId) {
    CHECK_EQ(CutsceneBankruptcyMessageId(0u), 5822);
    CHECK_EQ(CutsceneBankruptcyMessageId(4u), 7343);
    CHECK_EQ(CutsceneBankruptcyMessageId(0xFFu), 7343);  // bit2 set
    CHECK_EQ(CutsceneBankruptcyMessageId(8u), 5822);     // bit2 clear
}

TEST(CutsceneMisc3, ExecutionIntroDuration) {
    CHECK_EQ(CutsceneExecutionIntroDuration(0), 14000);
    CHECK_EQ(CutsceneExecutionIntroDuration(1), 12000);
    CHECK_EQ(CutsceneExecutionIntroDuration(2), 12000);
    // clamps (RandInt(3) only yields 0..2, but guard the table)
    CHECK_EQ(CutsceneExecutionIntroDuration(4), 10000);
    CHECK_EQ(CutsceneExecutionIntroDuration(-1), 14000);
}

TEST(CutsceneMisc3, DuelChoiceFromButton) {
    bool quit = false;
    CHECK_EQ(CutsceneDuelChoiceFromButton(1210, &quit), 1);
    CHECK(quit);
    quit = false;
    CHECK_EQ(CutsceneDuelChoiceFromButton(1155, &quit), 0);
    CHECK(quit);
    quit = true;
    CHECK_EQ(CutsceneDuelChoiceFromButton(99, &quit), 0);
    CHECK(!quit);   // unknown button -> no quit
}

TEST(CutsceneMisc3, DuelOutcomeFromButton) {
    CHECK_EQ(CutsceneDuelOutcomeFromButton(0), 2);
    CHECK_EQ(CutsceneDuelOutcomeFromButton(1), 3);
    CHECK_EQ(CutsceneDuelOutcomeFromButton(2), 4);
    CHECK_EQ(CutsceneDuelOutcomeFromButton(-1), 4);  // initial / no click
}

TEST(CutsceneMisc3, LeaseAcceptWhenRich) {
    // funds * (0.5/6 + 0.3) = 1e6 * 0.38333 >> 32000*1 -> accept at ask.
    LeaseDecision d = CutsceneLeaseAutoResolve(1000000, 5000, 1, 0.5, 0.5, 0.5, 0);
    CHECK_EQ(d.kind, 1);
    CHECK_EQ(d.rent, 5000);
}

TEST(CutsceneMisc3, LeaseRejectWhenStubborn) {
    // counterBudget <= ask but w3 (0.1) <= ratio -> no counter -> reject.
    LeaseDecision d = CutsceneLeaseAutoResolve(20000, 10000, 1, 0.0, 0.5, 0.1, 7);
    CHECK_EQ(d.kind, 0);
    CHECK_EQ(d.rent, 10000);  // unchanged (== ask)
}

TEST(CutsceneMisc3, LeaseCounterOffer) {
    // counterBudget = 20000*0.38333 = 7666 ; ratio = 0.7666 ; w3=0.99 > ratio.
    // cap = min(10000, 2*7666=15332) = 10000 ; step = 1*0.1 ; span = 2334 ;
    // roll = 7%2334 = 7 ; counter = 7*0.1 + 7666 = 7666 (int) ; +7666%32=26 ->
    // python golden = 7684.
    LeaseDecision d = CutsceneLeaseAutoResolve(20000, 10000, 1, 0.0, 0.5, 0.99, 7);
    CHECK_EQ(d.kind, 2);
    CHECK_EQ(d.rent, 7684);
}

// ---------------------------------------------------------------------------
// RNG reuse — the per-type mains snapshot/consume the cutscene LCG (cutscene.h).
// Prove RandInt(3) and the seed snapshot used by Execution are stable.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc3, ExecutionDurationFromRng) {
    CutsceneRng rng;
    rng.SetSeed(12345);
    int roll = static_cast<int>(rng.RandInt(3));
    CHECK(roll >= 0 && roll < 3);
    int dur = CutsceneExecutionIntroDuration(roll);
    CHECK(dur == 14000 || dur == 12000);  // one of the first three entries
}

// ---------------------------------------------------------------------------
// Flow drivers over a recording hook.
// ---------------------------------------------------------------------------
namespace {
struct Rec {
    std::vector<std::string> calls;
    int pumpCount = 0;
    int pumpRemaining = 0;
    int aliveRemaining = 0;
};
Rec* g_rec = nullptr;

int pump(i32, i32, void*) { if (g_rec && g_rec->pumpRemaining > 0) { g_rec->pumpRemaining--; g_rec->pumpCount++; return 1; } return 0; }
void flush() { if (g_rec) g_rec->calls.push_back("flush"); }
void musicPlay(const char* t) { if (g_rec) g_rec->calls.push_back(std::string("music:") + t); }
void musicRestore() { if (g_rec) g_rec->calls.push_back("restore"); }
void loadScene(const char* f) { if (g_rec) g_rec->calls.push_back(std::string("scene:") + f); }
void setupSky(const char* s) { if (g_rec) g_rec->calls.push_back(std::string("sky:") + s); }
void teardown() { if (g_rec) g_rec->calls.push_back("teardown"); }
void fadeIn(int) { if (g_rec) g_rec->calls.push_back("fade"); }
int runTimed(i32 ms) { if (g_rec) g_rec->calls.push_back("timed:" + std::to_string(ms)); return 0; }
int scriptAlive(i32) { if (g_rec && g_rec->aliveRemaining > 0) { g_rec->aliveRemaining--; return 1; } return 0; }

CutsceneMisc3Hooks makeHooks() {
    CutsceneMisc3Hooks h{};
    h.pumpFrame = pump;
    h.voiceFlushAll = flush;
    h.musicPlayCutscene = musicPlay;
    h.musicRestore = musicRestore;
    h.loadScene = loadScene;
    h.setupSky = setupSky;
    h.teardown = teardown;
    h.fadeIn = fadeIn;
    h.runTimedScript = runTimed;
    h.scriptAlive = scriptAlive;
    return h;
}
}  // namespace

TEST(CutsceneMisc3, FormatLodDebug) {
    char buf[256];
    char r = CutsceneFormatLodDebug(buf, sizeof(buf), "MyObj", 0x1F, 3, 7);
    CHECK_EQ(r, (char)1);
    CHECK(std::strstr(buf, "Name: MyObj") != nullptr);
    CHECK(std::strstr(buf, "active_lod: 1f") != nullptr);
    CHECK(std::strstr(buf, "fl.type: 3") != nullptr);
    CHECK(std::strstr(buf, "fl.oldtype: 7") != nullptr);
}

TEST(CutsceneMisc3, PlayTobySceneSequence) {
    Rec rec; g_rec = &rec;
    Cutscene3() = Cutscene3State{};
    CutsceneMisc3Hooks h = makeHooks();
    SetCutsceneMisc3Hooks(&h);

    CutsceneSlot slot{}; slot.partCount = 2;
    CutscenePlayTobyScene(&slot);

    SetCutsceneMisc3Hooks(nullptr);
    g_rec = nullptr;
    // LoadScene("cutscene_toby.ed3") then teardown, with an inert combat pump.
    CHECK(rec.calls.size() >= 2);
    if (rec.calls.size() >= 2) {
        CHECK_EQ(rec.calls.front(), std::string("scene:cutscene_toby.ed3"));
        CHECK_EQ(rec.calls.back(), std::string("teardown"));
    }
}

TEST(CutsceneMisc3, ExecutionRespectsReplayGate) {
    Rec rec; g_rec = &rec;
    Cutscene3() = Cutscene3State{};
    Cutscene3().replayGate = 1;   // gated: skip voice/music/script
    CutsceneMisc3Hooks h = makeHooks();
    SetCutsceneMisc3Hooks(&h);

    CutsceneRng rng; rng.SetSeed(1);
    CutsceneSlot slot{}; slot.partCount = 1; slot.partIds[0] = 5;
    CutsceneExecution(rng, &slot);

    SetCutsceneMisc3Hooks(nullptr);
    g_rec = nullptr;
    // gated: no music track (the intro voice/music are gated), but the timed-
    // script chain + fade + the unconditional teardown flush still run (the
    // original flushes the voice queue at teardown regardless of the gate).
    bool hasMusic = false, hasTeardown = false, hasTimed = false;
    for (auto& c : rec.calls) {
        if (c.rfind("music:", 0) == 0) hasMusic = true;
        if (c == "teardown") hasTeardown = true;
        if (c.rfind("timed:", 0) == 0) hasTimed = true;
    }
    CHECK(!hasMusic);
    CHECK(hasTeardown);
    CHECK(hasTimed);
    Cutscene3() = Cutscene3State{};
}

TEST(CutsceneMisc3, DeathRunsSeasonAndChain) {
    Rec rec; g_rec = &rec;
    Cutscene3() = Cutscene3State{};
    Cutscene3().dayOfMonth = 13;   // season index 3
    CutsceneMisc3Hooks h = makeHooks();
    // count the skyColorBand index
    static int s_band = -1;
    h.skyColorBand = [](int idx) { s_band = idx; };
    SetCutsceneMisc3Hooks(&h);

    CutsceneSlot slot{}; slot.partCount = 1; slot.partIds[0] = 1;
    CutsceneDeath(&slot);

    SetCutsceneMisc3Hooks(nullptr);
    g_rec = nullptr;
    CHECK_EQ(s_band, 3);   // day 13 -> band 3
    // scene load + fade + teardown present
    bool hasScene = false, hasFade = false, hasTeardown = false;
    for (auto& c : rec.calls) {
        if (c == "scene:Tod.ed3") hasScene = true;
        if (c == "fade") hasFade = true;
        if (c == "teardown") hasTeardown = true;
    }
    CHECK(hasScene); CHECK(hasFade); CHECK(hasTeardown);
    Cutscene3() = Cutscene3State{};
}
