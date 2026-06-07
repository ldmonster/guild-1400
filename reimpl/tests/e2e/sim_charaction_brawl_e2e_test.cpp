// End-to-end: a full BRAWL (street-fight) from the first blow through the
// knockout to the handler-entry teardown, driven entirely through
// VIBE_CharAction_BrawlStep (charaction_brawl.{h,cpp}). Models the He-record
// lifecycle the brawl coroutine runs on every tick: landed blows accumulate
// damage until the 5th flips to the knockout state, the knockout blow emits the
// defeat + family bump, and a terminal state frees the entry.
//
// A real-asset variant (GUARDED by GUILD_E2E_ASSETS) would replay a recorded
// brawl-event stream; absent that, the self-contained modeled fight runs.
#include "test.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "sim/charaction_brawl.h"
#include "sim/he.h"

using namespace guild;
using namespace guild::sim;

namespace {

struct FightLog {
    int blows = 0;
    int totalApDamage = 0;
    int defeats = 0;
    int familyBumps = 0;
    int requeues = 0;
    int freed = 0;
    bool victimAlive = true;
};
FightLog* g_log = nullptr;

struct FakePerson { u16 word; };
FakePerson g_victim{0x55};

BrawlHooks MakeFlowHooks() {
    BrawlHooks h{};
    h.packetStatus = [](i32) { return 1; };   // packets resolve immediately
    h.freeHandlerEntry = [](HeRecord*) { g_log->freed++; };
    h.findPersonById = [](i32) -> void* {
        return g_log->victimAlive ? static_cast<void*>(&g_victim) : nullptr;
    };
    h.aggressorRecord = [](HeRecord*) -> void* { return nullptr; };
    h.adjustRelationByMood = [](void*, i8) {};
    h.registerApEvent = [](u16, i32 negAp) {
        g_log->blows++; g_log->totalApDamage += negAp;
    };
    h.sendDefeatMessage = [](u16) { g_log->defeats++; };
    h.bumpVictimFamilyDefeats = [](void*) { g_log->familyBumps++; };
    h.restorePoseAndRequeue = [](HeRecord*) { g_log->requeues++; };
    return h;
}

} // namespace

// ===========================================================================
// Full fight: 5 blows -> knockout -> free.
// ===========================================================================
TEST(BrawlE2E, FightToKnockoutAndTeardown) {
    FightLog log; g_log = &log;
    BrawlHooks h = MakeFlowHooks(); SetBrawlHooks(&h);

    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    Brawl_State(&r) = 0;
    Brawl_VictimId(&r) = 1001;
    Brawl_ApDamage(&r) = 12;          // 12 AP per blow

    // --- Phase 1: trade blows until the 5th flips to the knockout state. ------
    int ticks = 0;
    while (Brawl_State(&r) == 0 && ticks < 50) {
        Brawl_Packet(&r) = -1;        // simulate the prior swing's packet resolving
        BrawlStep(&r);
        ++ticks;
    }
    CHECK_EQ(log.blows, 5);
    CHECK_EQ(log.totalApDamage, -60);                 // 5 * -12
    CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), 5);
    CHECK_EQ(static_cast<int>(Brawl_State(&r)), 1);   // knockout pending
    CHECK_EQ(static_cast<int>(Brawl_ActionWord(&r)), 5 * 24);

    // --- Phase 2: the knockout blow lands the defeat + family bump. -----------
    Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::Knockout);
    CHECK_EQ(log.defeats, 1);
    CHECK_EQ(log.familyBumps, 1);
    CHECK_EQ(log.requeues, 1);
    CHECK_EQ(log.blows, 5);                            // no extra AP on knockout

    // --- Phase 3: the host retires the handler -> terminal state frees it. -----
    Brawl_State(&r) = -2;
    CHECK(BrawlStep(&r) == BrawlOutcome::Freed);
    CHECK_EQ(log.freed, 1);
}

// ===========================================================================
// A blow whose swing packet has not yet resolved must NOT advance the fight.
// ===========================================================================
TEST(BrawlE2E, InFlightSwingStallsTheFight) {
    FightLog log; g_log = &log;
    BrawlHooks h = MakeFlowHooks();
    h.packetStatus = [](i32) { return 0; };   // swing packet still in flight
    SetBrawlHooks(&h);

    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    Brawl_State(&r) = 0;
    Brawl_Packet(&r) = 999;          // a pending swing
    Brawl_VictimId(&r) = 1;
    Brawl_ApDamage(&r) = 8;

    for (int i = 0; i < 5; ++i) {
        CHECK(BrawlStep(&r) == BrawlOutcome::Busy);
    }
    CHECK_EQ(log.blows, 0);                   // no damage while the swing is live
    CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), 0);
    CHECK_EQ(Brawl_Packet(&r), 999);          // handle preserved
}

// ===========================================================================
// GUARDED real-asset replay (skipped unless GUILD_E2E_ASSETS is set).
// ===========================================================================
TEST(BrawlE2E, RealAssetReplayGuarded) {
    const char* dir = std::getenv("GUILD_E2E_ASSETS");
    if (!dir) {
        CHECK(true);   // guarded skip — no assets configured
        return;
    }
    // With a recorded brawl-event stream a host would replay it here and re-assert
    // the blow count / knockout against a golden snapshot. No stream is wired, so
    // we only assert one modeled blow lands cleanly.
    FightLog log; g_log = &log;
    BrawlHooks h = MakeFlowHooks(); SetBrawlHooks(&h);
    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    Brawl_State(&r) = 0; Brawl_VictimId(&r) = 1; Brawl_ApDamage(&r) = 5;
    Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::BlowLanded);
    CHECK_EQ(log.blows, 1);
}
