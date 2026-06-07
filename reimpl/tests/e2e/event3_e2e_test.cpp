// End-to-end flow for src/world/event3.cpp: drive a guard-interaction / sink-to-
// ground event through its full handler lifecycle (init -> phase ticks -> free)
// across the translated VIBE_Event_* bodies, with a scripted hook backend.
#include "world/event3.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event3Hooks;
using guild::world::SetEvent3Hooks;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {

i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u8&       RB(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

struct Backend {
    bool personAlive = true;
    int  scriptHandle = -1;
    int  frees = 0;
    int  actions = 0;
    int  pair33s = 0;
    int  guardQueues = 0;
};
Backend* g_be = nullptr;

i32  E_free(HeRecord*) { g_be->frees++; return 0; }
void* E_findPerson(i32) { return g_be->personAlive ? reinterpret_cast<void*>(1) : nullptr; }
u16  E_charId(void*) { return 11; }
i32  E_scriptHandle(void*) { return g_be->scriptHandle; }
HeRecord* E_findFirst(i32, i32, i32) { return nullptr; }
HeRecord* E_findNext() { return nullptr; }
void E_stampReq(HeRecord*) {}
void* E_findBuilding(i32) { return reinterpret_cast<void*>(0xB); }
void* E_findObject(i32) { return nullptr; }
void E_changeAction(void*, void*, void*, u16) { g_be->actions++; }
i32  E_enqueue(i32, i32, i32, i32, i32, i32, i32, i32) { return 1; }
i32  E_guard61(void*, i32, i32, i32) { g_be->guardQueues++; return 321; }
void E_pair33(i32, i32) { g_be->pair33s++; }
i32  E_req17(i32, i32, i32, i32, i32, i32) { return 1; }
i32  E_packetStatus(i32) { return 0; }
i32  E_packetSeq(i32) { return 0; }
i32  E_variant(i32, i32) { return 0; }
i32  E_guardState(void*) { return 0; }     // -> guard-target path
i32  E_guardSlot(void*, int s) { return s == 0 ? 0x42 : 0; }
i32  E_guardOwner(void*) { return 9; }
i32  E_shuffle(u32 n, i32*) { return (i32)n; }
i32  E_listener(const void*, const float*, i32, const float*, i32) { return 0; }
void E_report(const char*) {}
void E_pd(HeRecord*, i32) {}
void E_pc(HeRecord*, i32, i32) {}
void E_fs(i32, i32) {}
i32  E_rs(const char*, const char*) { return 0; }
i32  E_aw() { return 0; }
i32  E_am() { return 0; }
void E_re(i32* a, i32* b, i32, i32) { if (a) *a = 0; if (b) *b = 0; }
void E_bm(i32, i32, u32) {}
double E_or(void*) { return 0.0; }
void E_as(u16, i32, i32) {}
void E_arr(HeRecord*, void*, u16) {}

Event3Hooks MakeBackend() {
    Event3Hooks hk{};
    hk.freeHandlerEntry = &E_free; hk.findPersonById = &E_findPerson;
    hk.personCharId = &E_charId; hk.personScriptHandle = &E_scriptHandle;
    hk.findFirstHandler = &E_findFirst; hk.findNextHandler = &E_findNext;
    hk.stampTimeAndRequest = &E_stampReq; hk.findBuildingById = &E_findBuilding;
    hk.findObjectById = &E_findObject; hk.changePlayerAction = &E_changeAction;
    hk.enqueueObjectInteraction = &E_enqueue; hk.queueGuardTarget61 = &E_guard61;
    hk.queueRequestPair33 = &E_pair33; hk.queueRequest17 = &E_req17;
    hk.packetStatus = &E_packetStatus; hk.packetSeq = &E_packetSeq;
    hk.buildingVariantIndex = &E_variant; hk.buildingGuardState = &E_guardState;
    hk.buildingGuardSlot = &E_guardSlot; hk.buildingGuardOwner = &E_guardOwner;
    hk.initAndShuffleDwordArray = &E_shuffle; hk.setListener = &E_listener;
    hk.reportMessage = &E_report; hk.eventPanelDestroySlot = &E_pd;
    hk.eventPanelCreateSlot = &E_pc; hk.formSelectWindow = &E_fs;
    hk.textRenderRichString = &E_rs; hk.activeWindowHandle = &E_aw;
    hk.activeWindowMessage = &E_am; hk.resolveEntityById = &E_re;
    hk.objectBuildModelName = &E_bm; hk.buildingOutputRatio = &E_or;
    hk.buildingAdjustStock = &E_as; hk.sendArrivalMessage = &E_arr;
    return hk;
}

void SetClock(int d, int h, int m, int s) {
    GameTime t{}; t.day = d; t.hour = (u16)h; t.minute = m; t.second = s;
    SetNpcClock(t);
}

}  // namespace

// Full sink-to-ground death event: drive every phase to teardown and verify the
// observable side effects (one collapse action, the model stand-up day, the
// pair-33 emit and the final free).
TEST(Event3E2E, SinkToGroundFullLifecycle) {
    Backend be; g_be = &be;
    Event3Hooks hk = MakeBackend();
    SetEvent3Hooks(&hk);
    crt::Srand(1);
    SetClock(10, 6, 0, 0);

    HeRecord h = MakeHe();
    RD(&h, 172) = 500;       // target person id
    RD(&h, 112) = 0;         // start at phase 2 (counter 0)
    RB(&h, 120) = 2;         // armed flag for the final pair-33

    // Phase 2 -> collapse action, counter -> 1.
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 1);

    // Phase 3 -> script handle already -1 -> counter -> 2.
    be.scriptHandle = -1;
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 2);

    // Phase 4 -> action + stand-up + advance(1,0,0), counter -> 3. The first
    // Advance arg lands on the hour-of-day result (clock 06:00 -> 07:00).
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 3);
    CHECK_EQ((int)RT(&h, 82).day, 10);   // day unchanged
    CHECK_EQ((int)RT(&h, 82).hour, 7);   // 6 + 1

    // Phase 5 -> armed -> pair33 + free.
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(be.pair33s, 1);
    CHECK_EQ(be.frees, 1);
    CHECK(be.actions >= 2);              // at least the phase-2 and phase-4 actions

    SetEvent3Hooks(nullptr);
}

// Guard-interaction event: request a guard target then schedule, confirming the
// handle is recorded and the appointment is stamped, and a missing building on a
// re-fire frees the handler.
TEST(Event3E2E, GuardInteractionThenFreeOnMissingBuilding) {
    Backend be; g_be = &be;
    Event3Hooks hk = MakeBackend();
    SetEvent3Hooks(&hk);
    SetClock(4, 14, 30, 0);

    HeRecord h = MakeHe();
    RD(&h, 180) = 77;
    i32 handle = world::RequestGuardInteraction(&h);
    CHECK_EQ(handle, 321);
    CHECK_EQ(be.guardQueues, 1);
    CHECK_EQ(RD(&h, 172), 321);
    CHECK_EQ((int)RT(&h, 82).day, 4);
    CHECK_EQ((int)RT(&h, 82).minute, 30);   // full clock image copied

    // Re-fire with the building gone -> teardown.
    auto missing = [](i32) -> void* { return nullptr; };
    hk.findBuildingById = missing;
    SetEvent3Hooks(&hk);
    HeRecord h2 = MakeHe();
    world::RequestGuardInteraction(&h2);
    CHECK_EQ(be.frees, 1);

    SetEvent3Hooks(nullptr);
}

// Actor action start then transport decay: confirm the schedule advance and a
// stock transfer step, ending in arrival teardown.
TEST(Event3E2E, ActorActionStartThenTransportArrival) {
    Backend be; g_be = &be;
    Event3Hooks hk = MakeBackend();
    SetEvent3Hooks(&hk);
    SetClock(7, 8, 0, 0);

    HeRecord h = MakeHe();
    RD(&h, 172) = 1; RD(&h, 16) = 2; RD(&h, 196) = -1;
    i32 hour = world::StartActorAction(&h);
    CHECK_EQ(hour, 14);                    // 08:00 + 400 min = 14:40
    CHECK_EQ(RD(&h, 176), 200);

    // Transport phase 0: scratch 0 minutes behind clock; remaining decremented.
    RD(&h, 112) = 0;
    RD(&h, 176) = 40;
    RT(&h, 96).day = 7; RT(&h, 96).hour = 7; RT(&h, 96).minute = 0;  // 60 min behind
    world::MoveTowardTargetRun(&h);        // moved = 60*0.1*5 = 30 -> 40-30 = 10
    CHECK_EQ(RD(&h, 176), 10);

    // Arrival phase 1 -> action + free.
    RD(&h, 112) = 1;
    world::MoveTowardTargetRun(&h);
    CHECK_EQ(be.frees, 1);

    SetEvent3Hooks(nullptr);
}
