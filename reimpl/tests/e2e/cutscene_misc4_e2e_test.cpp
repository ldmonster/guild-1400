// e2e: drive a full RunParticipants master-driven run and a salon transition
// across the cutscene_misc4 functions over a recording hook surface, asserting
// the binary-faithful cross-module call sequence and final state.
#include "test.h"

#include "sim/cutscene_misc4.h"
#include "sim/cutscene.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct Capture {
    std::vector<std::string> events;
    int pumpReturnsRemaining = 0;  // how many more times pumpFrame returns "keep"
    int dialogCalls = 0;
};
Capture* g_cap = nullptr;

int CapPump(i32, i32, void*) {
    if (!g_cap) return 0;
    g_cap->events.push_back("pump");
    if (g_cap->pumpReturnsRemaining > 0) { --g_cap->pumpReturnsRemaining; return 1; }
    return 0;
}
void CapInit(const CutsceneSlot*) { if (g_cap) g_cap->events.push_back("init"); }
void CapDialog(const CutsceneSlot*) { if (g_cap) { g_cap->events.push_back("dialog"); ++g_cap->dialogCalls; } }
void CapBanner(const char*) { if (g_cap) g_cap->events.push_back("banner"); }
void CapLight(int, int) { if (g_cap) g_cap->events.push_back("light"); }
void CapQueue(int, void*) { if (g_cap) g_cap->events.push_back("queue"); }
void CapNet(void*) { if (g_cap) g_cap->events.push_back("net"); }

int g_allDoneCalls = 0;
int AllDoneAfterTwo(const CutsceneSlot*) {
    // not done for the first two checks, then done.
    return (++g_allDoneCalls > 2) ? 1 : 0;
}

void CapFadeOut() { if (g_cap) g_cap->events.push_back("fadeout"); }
void CapLoadScene(int kind, i32, i16) {
    if (g_cap) g_cap->events.push_back("load" + std::to_string(kind));
}

CutsceneSlot MakeSlot(const i32* ids, int n, i32 master) {
    CutsceneSlot s{};
    std::memset(&s, 0, sizeof(s));
    s.id = 42; s.master = master; s.partCount = static_cast<u8>(n);
    for (int i = 0; i < kMaxParticipants; ++i) s.partIds[i] = -1;
    for (int i = 0; i < n; ++i) s.partIds[i] = ids[i];
    return s;
}
bool Has(const std::vector<std::string>& v, const std::string& e) {
    for (auto& s : v) if (s == e) return true;
    return false;
}
}  // namespace

TEST(CutsceneMisc4E2E, MasterRunProducesBannerInitDialogPumpSequence) {
    Capture cap; g_cap = &cap;
    g_allDoneCalls = 0;
    Cutscene3() = Cutscene3State{};

    CutsceneMisc4Hooks h{};
    h.pumpFrame = CapPump;
    h.initParticipantTable = CapInit;
    h.showParticipantDialog = CapDialog;
    h.setStatusBanner = CapBanner;
    h.setLightGray = CapLight;
    h.queueFlagBlob = CapQueue;
    h.netRunWaitLoop = CapNet;
    h.allParticipantsDone = AllDoneAfterTwo;  // loops twice then completes
    cap.pumpReturnsRemaining = 2;             // keep the choice loop pumping
    SetCutsceneMisc4Hooks(&h);

    CutsceneRng rng; rng.SetSeed(0xABCD);
    i32 ids[] = {100, 200, 300};
    CutsceneSlot s = MakeSlot(ids, 3, /*master=*/200);

    // master-report path (flag 0x4) + master mode (byte 1).
    i32 ret = CutsceneRunParticipants(rng, &s, kWorldFlagMasterReport,
                                      /*runModeByte=*/1, /*cutType=*/2);

    // Seed snapshot/restore is exact.
    CHECK_EQ(ret, static_cast<i32>(0xABCD));
    CHECK_EQ(rng.GetSeed(), static_cast<i32>(0xABCD));
    // ++dword_6315A4 on exit.
    CHECK_EQ(Cutscene3().duelMode, 1);
    // The master preamble ran: light + queue + net wait + banner + init.
    CHECK(Has(cap.events, "init"));
    CHECK(Has(cap.events, "light"));
    CHECK(Has(cap.events, "queue"));
    CHECK(Has(cap.events, "net"));
    CHECK(Has(cap.events, "banner"));
    // The choice loop pumped and showed the dialog at least twice.
    CHECK(cap.dialogCalls >= 2);
    CHECK(Has(cap.events, "pump"));
    SetCutsceneMisc4Hooks(nullptr);
}

TEST(CutsceneMisc4E2E, SalonTransitionLoadThenFade) {
    Capture cap; g_cap = &cap;
    Cutscene3() = Cutscene3State{};

    CutsceneMisc4Hooks h{};
    h.salonFadeOut = CapFadeOut;
    h.salonLoadScene = CapLoadScene;
    SetCutsceneMisc4Hooks(&h);

    // No cached scene matches -> load. Production type -> Gebaeude (kind 0).
    int cls = CutsceneSalonFadeTransition(/*entity=*/500, /*season=*/3,
                                          /*c1=*/100, 2, /*c2=*/200, 1,
                                          /*production=*/true, /*packed=*/9);
    CHECK_EQ(cls, 2);
    CHECK(Has(cap.events, "load0"));   // Gebaeude scene loaded
    CHECK(Has(cap.events, "fadeout")); // then the cross-fade

    // Now a cached match -> fade only, no new load.
    cap.events.clear();
    int cls2 = CutsceneSalonFadeTransition(100, 2, 100, 2, 200, 1, true, 9);
    CHECK_EQ(cls2, 1);
    CHECK(!Has(cap.events, "load0"));
    CHECK(Has(cap.events, "fadeout"));

    // Objekt with packed dispatch byte == -2 -> skip the scene load, still fade.
    cap.events.clear();
    int cls3 = CutsceneSalonFadeTransition(700, 4, 100, 2, 200, 1,
                                           /*production=*/false, /*packed=*/-2);
    CHECK_EQ(cls3, 2);
    CHECK(!Has(cap.events, "load0"));
    CHECK(!Has(cap.events, "load1"));
    CHECK(Has(cap.events, "fadeout"));
    SetCutsceneMisc4Hooks(nullptr);
}
