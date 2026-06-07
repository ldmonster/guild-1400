// E2E: drive a full life-event cutscene sequence (Death -> Bankruptcy -> Salon)
// over a single recording hook surface, asserting the end-to-end ordering of the
// load-bearing presentation calls (scene load, voice/music gate, sky, the
// timed-script chain, fade, teardown, music restore) and that the deterministic
// kernels (seasonal pick, lease decision, duel mapping) feed the flow correctly.
#include "test.h"

#include "sim/cutscene_misc3.h"
#include "sim/cutscene.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct Trace {
    std::vector<std::string> calls;
    int bandIndex = -1;
};
Trace* g_t = nullptr;

void flush() { if (g_t) g_t->calls.push_back("flush"); }
void music(const char* t) { if (g_t) g_t->calls.push_back(std::string("music:") + t); }
void restore() { if (g_t) g_t->calls.push_back("restore"); }
void scene(const char* f) { if (g_t) g_t->calls.push_back(std::string("scene:") + f); }
void sky(const char* s) { if (g_t) g_t->calls.push_back(std::string("sky:") + s); }
void band(int idx) { if (g_t) { g_t->bandIndex = idx; g_t->calls.push_back("band"); } }
void teardown() { if (g_t) g_t->calls.push_back("teardown"); }
void fade(int) { if (g_t) g_t->calls.push_back("fade"); }
int timed(i32 ms) { if (g_t) g_t->calls.push_back("timed:" + std::to_string(ms)); return 0; }
void destroySky() { if (g_t) g_t->calls.push_back("destroysky"); }
void distInherit(void*, int) { if (g_t) g_t->calls.push_back("inherit"); }
void* findP(i32) { static int dummy; return &dummy; }

CutsceneMisc3Hooks fullHooks() {
    CutsceneMisc3Hooks h{};
    h.voiceFlushAll = flush;
    h.musicPlayCutscene = music;
    h.musicRestore = restore;
    h.loadScene = scene;
    h.setupSky = sky;
    h.skyColorBand = band;
    h.teardown = teardown;
    h.fadeIn = fade;
    h.runTimedScript = timed;
    h.destroySky = destroySky;
    h.distributeInheritance = distInherit;
    h.personFind = findP;
    return h;
}

int indexOf(const std::vector<std::string>& v, const std::string& pred, bool prefix=false) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (prefix ? (v[i].rfind(pred, 0) == 0) : (v[i] == pred)) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

TEST(CutsceneMisc3E2E, DeathThenBankruptcyFullFlow) {
    Trace t; g_t = &t;
    Cutscene3() = Cutscene3State{};
    Cutscene3().dayOfMonth = 9;   // season band 1 (8 <= 9 < 10)
    CutsceneMisc3Hooks h = fullHooks();
    SetCutsceneMisc3Hooks(&h);

    CutsceneSlot deathSlot{}; deathSlot.partCount = 1; deathSlot.partIds[0] = 1;
    CutsceneDeath(&deathSlot);

    // ordering assertions for Death
    int iScene = indexOf(t.calls, "scene:Tod.ed3");
    int iSky   = indexOf(t.calls, "sky:Sky_Schoen_02");
    int iBand  = indexOf(t.calls, "band");
    int iFade  = indexOf(t.calls, "fade");
    int iTear  = indexOf(t.calls, "teardown");
    int iRestore = indexOf(t.calls, "restore");
    CHECK(iScene >= 0); CHECK(iSky > iScene); CHECK(iBand > iScene);
    CHECK(iFade > iBand); CHECK(iTear > iFade); CHECK(iRestore > iTear);
    CHECK_EQ(t.bandIndex, 1);   // day 9 -> band 1

    // a timed-script chain ran (3000 first beat present)
    CHECK(indexOf(t.calls, "timed:3000") >= 0);

    // ---- now Bankruptcy reuses the same surface ----
    t.calls.clear(); t.bandIndex = -1;
    Cutscene3().dayOfMonth = 20;   // band 6 (no window)
    CutsceneSlot bk{}; bk.partCount = 1; bk.partIds[0] = 2;
    CutsceneBankruptcy(&bk);

    SetCutsceneMisc3Hooks(nullptr);
    g_t = nullptr;

    int bScene = indexOf(t.calls, "scene:Pleite.ed3");
    int bSky   = indexOf(t.calls, "sky:Sky_Dunkel_01");
    int bInherit = indexOf(t.calls, "inherit");
    int bRestore = indexOf(t.calls, "restore");
    CHECK(bScene >= 0); CHECK(bSky > bScene);
    CHECK(bInherit > bRestore);   // inheritance is the closing step
    CHECK_EQ(t.bandIndex, 6);
    // the long timed chain (16000 / 11000 / 24000) ran
    CHECK(indexOf(t.calls, "timed:16000") >= 0);
}

TEST(CutsceneMisc3E2E, SalonLoadedPathFadesAndTearsDown) {
    Trace t; g_t = &t;
    Cutscene3() = Cutscene3State{};
    CutsceneMisc3Hooks h = fullHooks();
    SetCutsceneMisc3Hooks(&h);

    CutsceneSlot slot{}; slot.partCount = 2; slot.partIds[0] = 1; slot.partIds[1] = 2;
    // entityPresent == false -> the LoadScene("ob_SALON.ed3") path that also fades.
    CutsceneSalon(&slot, /*entityPresent=*/false);

    SetCutsceneMisc3Hooks(nullptr);
    g_t = nullptr;

    int sScene = indexOf(t.calls, "scene:ob_SALON.ed3");
    int sFade  = indexOf(t.calls, "fade");
    int sTear  = indexOf(t.calls, "teardown");
    CHECK(sScene >= 0);
    CHECK(sFade > sScene);
    CHECK(sTear > sFade);
}

TEST(CutsceneMisc3E2E, SalonResolvedPathSkipsLoadAndFade) {
    Trace t; g_t = &t;
    Cutscene3() = Cutscene3State{};
    CutsceneMisc3Hooks h = fullHooks();
    SetCutsceneMisc3Hooks(&h);

    CutsceneSlot slot{}; slot.partCount = 1; slot.partIds[0] = 1;
    // entityPresent == true -> SalonFadeTransition path, no scene load / fade.
    CutsceneSalon(&slot, /*entityPresent=*/true);

    SetCutsceneMisc3Hooks(nullptr);
    g_t = nullptr;

    CHECK(indexOf(t.calls, "scene:ob_SALON.ed3") < 0);  // no load
    CHECK(indexOf(t.calls, "fade") < 0);                 // no fade (loaded==0)
    CHECK(indexOf(t.calls, "restore") >= 0);             // music restored
}

// A small duel-window end-to-end: a window's button click maps to the slot's
// +148 choice byte; an outcome window maps a clicked action to 2/3/4.
TEST(CutsceneMisc3E2E, DuelWindowChoiceAndOutcome) {
    bool quit = false;
    // choice window: clicking "accept" (1210) -> choice 1, quit
    CHECK_EQ(CutsceneDuelChoiceFromButton(kDuelBtnAccept, &quit), 1);
    CHECK(quit);
    // outcome window: clicking action button index 1 -> outcome byte 3
    CHECK_EQ(CutsceneDuelOutcomeFromButton(1), 3);
    // a fresh outcome window with no click keeps its initial 4
    CHECK_EQ(CutsceneDuelOutcomeFromButton(-1), 4);
}
