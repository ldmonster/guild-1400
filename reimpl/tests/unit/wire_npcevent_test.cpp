// Verifies InstallRealNpcEventWiring() / InstallRealNpcEvent2Wiring() bind the
// reconstructed entity/person/command/reaper leaves into the live NpcEventHooks /
// NpcEventHooks2 bridges (previously partially / fully inert at runtime), and that
// a representative wired step executes over a zeroed He record without crashing,
// returning a defined value.
#include "tests/framework/test.h"

#include "sim/wire_npcevent.h"
#include "sim/npcevent_steps.h"        // NpcEventHooks / Get / Set, NpcEvent_*
#include "sim/npcevent_steps2.h"       // NpcEventHooks2 / Get / Set
#include "sim/npcevent_reaper_full.h"  // Reaper* (reaper fields) + SetReaperFullHooks
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// --- NpcEventHooks: the full table is bound (reaper + entity/person/command) ---
TEST(WireNpcEvent, InstallBindsFullNpcEventHooks) {
    // Baseline: fresh inert table -> everything null.
    SetNpcEventHooks(nullptr);
    CHECK(GetNpcEventHooks().findPerson == nullptr);
    CHECK(GetNpcEventHooks().reaperMove == nullptr);

    InstallRealNpcEventWiring();
    const NpcEventHooks& h = GetNpcEventHooks();

    // entity / person / building resolution.
    CHECK(h.resolveEntity != nullptr);
    CHECK(h.entityField   != nullptr);
    CHECK(h.findPerson    != nullptr);
    CHECK(h.personField   != nullptr);
    CHECK(h.findBuilding  != nullptr);

    // command emit leaves.
    CHECK(h.queueRequestPair33        != nullptr);
    CHECK(h.queueRequestSingle49      != nullptr);
    CHECK(h.queueRequestArgs25        != nullptr);
    CHECK(h.queueRequestQuad52        != nullptr);
    CHECK(h.queueRequestNamedObject53 != nullptr);
    CHECK(h.requestBuildOp87          != nullptr);
    CHECK(h.enqueueObjectInteraction  != nullptr);
    CHECK(h.queueGestureFlag55        != nullptr);

    // packet status / sequence.
    CHECK(h.packetStatus != nullptr);
    CHECK(h.packetSeq    != nullptr);

    // reaper 4 re-bound (SUPERSEDES InstallRealReaperWiring).
    CHECK(h.reaperApproach    == &ReaperApproachTarget);
    CHECK(h.reaperMove        == &ReaperMoveTowardTarget);
    CHECK(h.reaperCachePose   == &ReaperCacheTargetPose);
    CHECK(h.reaperUpdateSound == &ReaperUpdateSoundPos);

    // Genuinely unreconstructed leaves stay inert (null).
    CHECK(h.isNearDoor         == nullptr);
    CHECK(h.requestBuildOp77   == nullptr);
    CHECK(h.loadDemandSnapshot == nullptr);
    CHECK(h.eventPanelCreate   == nullptr);

    SetNpcEventHooks(nullptr);  // restore for later TUs
}

// --- resolve hooks behave: absent id -> token 0 (the "absent" sentinel) --------
TEST(WireNpcEvent, ResolveHooksReturnAbsentTokenForMissingId) {
    InstallRealNpcEventWiring();
    const NpcEventHooks& h = GetNpcEventHooks();

    // No entity arrays populated in this unit TU -> every lookup misses -> token 0.
    CHECK_EQ(h.findPerson(0x7FFFFFF0), 0);
    CHECK_EQ(h.findBuilding(0x7FFFFFF0), 0);
    CHECK_EQ(h.resolveEntity(0x7FFFFFF0), 0);
    // Field read off the absent (0) handle returns 0 (null-record convention).
    CHECK_EQ(h.personField(0, 4), 0);
    CHECK_EQ(h.entityField(0, 1), 0);
    // Out-of-record probe offset (the +1000 reaper "class" probe) -> 0.
    CHECK_EQ(h.entityField(0, 1000), 0);

    SetNpcEventHooks(nullptr);
}

// --- a representative wired step runs over a zeroed He without crashing ---------
TEST(WireNpcEvent, WiredProtectionMoneyInitExecutesOverZeroedRecord) {
    InstallRealNpcEventWiring();

    u8 buf[640];
    std::memset(buf, 0, sizeof(buf));
    HeRecord* h = reinterpret_cast<HeRecord*>(buf);

    // ProtectionMoneyInit: findPerson(+176) misses (no arrays) -> no person ->
    // arms the timer and returns the (defined) advance result. Just must not crash.
    i32 r = NpcEvent_ProtectionMoneyInit(h);
    (void)r;
    CHECK(true);

    SetNpcEventHooks(nullptr);
}

// --- a wired reaper step still executes over inert sub-hooks --------------------
TEST(WireNpcEvent, WiredReaperMoveExecutesOverInertSubhooks) {
    SetReaperFullHooks(nullptr);   // reaper engine sub-callees inert
    InstallRealNpcEventWiring();

    u8 buf[320];
    std::memset(buf, 0, sizeof(buf));
    HeRecord* h = reinterpret_cast<HeRecord*>(buf);

    int code = GetNpcEventHooks().reaperMove(h);
    CHECK(code == 0 || code == 1 || code == 2);

    SetNpcEventHooks(nullptr);
}

// --- NpcEventHooks2: bindable subset bound, unreconstructed leaves inert --------
TEST(WireNpcEvent, InstallBindsNpcEventHooks2Subset) {
    SetNpcEventHooks2(nullptr);
    CHECK(GetNpcEventHooks2().findPerson == nullptr);

    InstallRealNpcEvent2Wiring();
    const NpcEventHooks2& h = GetNpcEventHooks2();

    CHECK(h.queryBegin                != nullptr);
    CHECK(h.findPerson                != nullptr);
    CHECK(h.personField               != nullptr);
    CHECK(h.objectField               != nullptr);
    CHECK(h.queueRequestSingle49      != nullptr);
    CHECK(h.queueRequestNamedObject53 != nullptr);
    CHECK(h.queueRequestArgs25        != nullptr);
    CHECK(h.queueRequest16            != nullptr);
    CHECK(h.packetStatus              != nullptr);
    CHECK(h.packetSeq                 != nullptr);

    // Unreconstructed leaves stay inert.
    CHECK(h.buildingUpgradeLevel != nullptr ? false : true);
    CHECK(h.computeRoomWorth     == nullptr);
    CHECK(h.changePlayerAction   == nullptr);
    CHECK(h.queueRequest39       == nullptr);
    CHECK(h.applyTitleDelta      == nullptr);
    CHECK(h.renderAwardText      == nullptr);

    // Resolve hooks miss -> absent token.
    CHECK_EQ(h.findPerson(0x7FFFFFF0), 0);
    CHECK_EQ(h.queryBegin(0x7FFFFFF0), 0);

    SetNpcEventHooks2(nullptr);
}
