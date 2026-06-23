// e2e: drive a complete cutscene presentation flow across the cutscene_misc2
// bodies — load+run a scene script, fade in, build the sky, run a timed
// presentation segment, then tear the cutscene down.
#include "sim/cutscene_misc2.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A single recording harness standing in for the engine frame loop + the
// script / fade / sky / scene leaves the cutscene driver calls.
struct Engine {
    int   frameNo       = 0;
    int   maxFrames     = 0;       // pump stops after this many
    u32   tickPerFrame  = 0;
    int   loadCalls     = 0;
    int   runMainCalls  = 0;
    bool  scriptAlive   = true;
    int   fadeFrames    = 0;       // frames before the fade reports done
    int   skyCreate     = 0, skyLayer = 0, skyConfig = 0, skyRemove = 0, skyDestroy = 0;
    int   finishCalls   = 0, sceneTeardown = 0;
    int   damage        = 0, dialog = 0;
    std::vector<i32> log;          // ordered phase markers
};
Engine g_e;

int EngPump(i32, i32, void*) {
    g_e.frameNo++;
    Cutscene2().gameTick += g_e.tickPerFrame;
    return (g_e.frameNo >= g_e.maxFrames) ? 0 : 1;
}
void* EngLoad(const char*) { g_e.loadCalls++; return reinterpret_cast<void*>(0xABCD); }
void EngRunMain(void*) { g_e.runMainCalls++; }
void* EngFind(i32) { return g_e.scriptAlive ? reinterpret_cast<void*>(1) : nullptr; }
void EngFinish(void*) { g_e.finishCalls++; }
u8 EngHandleFlags(i32) { return 0; }   // never busy -> waits exit immediately

void* EngFadeReg() { g_e.log.push_back(1); return reinterpret_cast<void*>(0xFADE); }
u8 EngFadeDone(void*) { return (g_e.frameNo >= g_e.fadeFrames) ? 0x04 : 0x00; }

void* EngSkyCreate() { g_e.skyCreate++; return reinterpret_cast<void*>(0x5C); }
void* EngSkyLayer(void*) { g_e.skyLayer++; return reinterpret_cast<void*>(0x1A); }
void EngSkyConfig(void*, void*) { g_e.skyConfig++; }
void EngSkyRemove(void*, void*) { g_e.skyRemove++; }
void EngSkyDestroy(void*) { g_e.skyDestroy++; }

void EngDamage() { g_e.damage++; }
void EngDialog(i32) { g_e.dialog++; }
void EngSceneTeardown() { g_e.sceneTeardown++; }

Cutscene2Hooks FullHooks() {
    Cutscene2Hooks h{};
    h.pumpFrame = &EngPump;
    h.scriptLoadFromDir = &EngLoad;
    h.scriptRunMain = &EngRunMain;
    h.scriptFindByHandle = &EngFind;
    h.scriptFinish = &EngFinish;
    h.scriptHandleFlags = &EngHandleFlags;
    h.fadeRegister = &EngFadeReg;
    h.fadeDoneByte = &EngFadeDone;
    h.skyCreate = &EngSkyCreate;
    h.skyCreateLayer = &EngSkyLayer;
    h.skyConfigLayer = &EngSkyConfig;
    h.skyRemoveLayer = &EngSkyRemove;
    h.skyDestroy = &EngSkyDestroy;
    h.combatUpdateDamageNumbers = &EngDamage;
    h.showParticipantDialog = &EngDialog;
    h.sceneTeardown = &EngSceneTeardown;
    return h;
}

}  // namespace

TEST(CutsceneMisc2E2E, FullPresentationFlow) {
    g_e = Engine{};
    Cutscene2() = Cutscene2State{};
    Cutscene2Hooks h = FullHooks();
    SetCutscene2Hooks(&h);

    // -- phase 1: enter cutscene; load + run the scene script ----------------
    Cutscene2().nestDepth = 1;          // one cutscene open
    i32 handle = 0;
    void* script = CutsceneLoadAndRunScript("duell.ed3", &handle);
    CHECK(script != nullptr);
    CHECK_EQ(g_e.loadCalls, 1);
    CHECK_EQ(g_e.runMainCalls, 1);
    CHECK_EQ(handle, 1);                 // started -> handle exists
    Cutscene2().scriptHandleA = handle;  // track it for teardown

    // -- phase 2: fade to black over 4 frames --------------------------------
    g_e.maxFrames = 100;
    g_e.fadeFrames = 4;
    CutsceneFadeIn(1, nullptr);
    CHECK_EQ(g_e.frameNo, 4);            // spun exactly until done bit set

    // -- phase 3: build the sky ----------------------------------------------
    CutsceneSetupSky(nullptr);
    CHECK_EQ(g_e.skyCreate, 1);
    CHECK_EQ(g_e.skyLayer, 1);
    CHECK_EQ(g_e.skyConfig, 1);
    CHECK(CutsceneSkyState().sky != nullptr);

    // -- phase 4: run a timed presentation segment (deadline elapses) --------
    g_e.frameNo = 0;
    g_e.maxFrames = 3;                   // stop after a couple frames
    g_e.tickPerFrame = 1000;             // jump the ms clock past the deadline
    Cutscene2().gameTick = 0;
    CutsceneRunTimedScript(14, 0, nullptr);   // 14 frames -> ~1ms deadline
    CHECK_EQ(Cutscene2().forceQuit, 1);  // deadline latched the quit flag
    CHECK(g_e.damage >= 1);
    CHECK(g_e.dialog >= 1);

    // -- phase 5: tear the sky + cutscene down -------------------------------
    CutsceneDestroySky();
    CHECK_EQ(g_e.skyRemove, 1);
    CHECK_EQ(g_e.skyDestroy, 1);
    // gilde.exe 0x4aa740: DestroySky clears ONLY dword_64A7C8 (the mirror); the sky
    // handle dword_6315F0 is left intact. Verified vs disasm.
    CHECK_EQ(CutsceneSkyState().mirror, (void*)nullptr);
    CHECK(CutsceneSkyState().sky != nullptr);

    CutsceneTeardown();
    CHECK_EQ(g_e.finishCalls, 1);        // primary script finished
    CHECK_EQ(g_e.sceneTeardown, 1);
    CHECK_EQ(Cutscene2().nestDepth, 0);  // nesting back to 0
}

TEST(CutsceneMisc2E2E, RemoteReplayShortCircuitsEverything) {
    g_e = Engine{};
    Cutscene2() = Cutscene2State{};
    Cutscene2Hooks h = FullHooks();
    SetCutscene2Hooks(&h);

    Cutscene2().replayGate = 1;          // dword_6315BC -> remote replay
    Cutscene2().nestDepth = 1;

    // every gated body becomes a no-op
    CHECK_EQ(CutsceneLoadAndRunScript("x", nullptr), (void*)nullptr);
    CutsceneFadeIn(0, nullptr);
    CutsceneSetupSky(nullptr);
    CutsceneRunTimedScript(100, 0, nullptr);
    CutsceneRunCombatScript(50, nullptr, 0);
    CutsceneTeardown();

    CHECK_EQ(g_e.loadCalls, 0);
    CHECK_EQ(g_e.frameNo, 0);            // no frames pumped at all
    CHECK_EQ(g_e.skyCreate, 0);
    CHECK_EQ(g_e.finishCalls, 0);
    CHECK_EQ(Cutscene2().nestDepth, 1);  // teardown skipped
}

TEST(CutsceneMisc2E2E, DuelWindowFlowAcrossModes) {
    g_e = Engine{};
    Cutscene2() = Cutscene2State{};

    static int outcome = 0, choice = 0;
    Cutscene2Hooks h{};
    h.duelOutcomeWindow = [](i32, i32, i32) -> int { outcome++; return 4; };
    h.duelChoiceWindow = [](i32, i32) -> int { choice++; return 1; };
    SetCutscene2Hooks(&h);

    Cutscene2().duelMode = 0;            // choice phase
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 1);
    Cutscene2().duelMode = 1;            // outcome phase
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 4);
    CHECK_EQ(choice, 1);
    CHECK_EQ(outcome, 1);
}
