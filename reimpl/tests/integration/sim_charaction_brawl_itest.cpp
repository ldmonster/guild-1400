#include "test.h"

#include <cstring>
#include <vector>

#include "sim/charaction_brawl.h"
#include "sim/he.h"
#include "sim/npcaction.h"        // REAL VIBE_Npc_AdjustRelationByMood + NpcLeafHooks
#include "ai/meister_events.h"    // REAL RegisterApEvent + ApEventPool
#include "crt/rand.h"             // REAL ANSI LCG (the mood increment draw)

using namespace guild;
using namespace guild::sim;

// Cross-module integration: drive the brawl combat step against REAL sibling
// damage-resolution leaves — the actual VIBE_Npc_AdjustRelationByMood (which
// mutates the aggressor's per-mood relation bytes via the real CRT-RNG increment)
// and the actual MeisterAi RegisterApEvent pool (which records the negated AP
// damage as a live event slot). A full 5-blow brawl must move BOTH real subsystems
// and end in the knockout state.

namespace {

// Shared test wiring (BrawlStep hooks bridge to the real siblings).
guild::ai::ApEventPool* g_pool = nullptr;
HeRecord*        g_aggressor = nullptr;
std::vector<i32> g_apAmounts;
int              g_op93Calls = 0;

// A fake victim person whose id/alive word the AP-event reads.
struct FakePerson { u16 word; };
FakePerson g_victim{0x44};

BrawlHooks WireRealSiblings() {
    BrawlHooks h{};
    h.packetStatus = [](i32) { return 1; };                 // always resolved
    h.freeHandlerEntry = [](HeRecord*) {};
    h.findPersonById = [](i32) -> void* { return &g_victim; };
    h.aggressorRecord = [](HeRecord*) -> void* { return g_aggressor; };
    // REAL relation/mood damage: mutate the aggressor's relation bytes.
    h.adjustRelationByMood = [](void* agg, i8 kind) {
        NpcAdjustRelationByMood(static_cast<HeRecord*>(agg), kind);
    };
    // REAL AP-event registration: record the negated-AP damage as a live slot.
    h.registerApEvent = [](u16 victimWord, i32 negAp) {
        g_apAmounts.push_back(negAp);
        guild::ai::RegisterApEvent(*g_pool, /*owner*/victimWord, /*secondary*/0,
                        /*primary*/negAp, "pruegel");
    };
    h.sendDefeatMessage = [](u16) {};
    h.bumpVictimFamilyDefeats = [](void*) {};
    h.restorePoseAndRequeue = [](HeRecord*) {};
    return h;
}

} // namespace

// ===========================================================================
// A full 5-blow brawl drives the REAL relation-mood subsystem AND the REAL
// AP-event pool, then flips to the knockout state.
// ===========================================================================
TEST(BrawlItest, FullBrawlMovesRealSubsystems) {
    crt::Srand(2024);

    guild::ai::ApEventPool pool{};  // default member inits zero every slot
    g_pool = &pool;

    HeRecord aggressor{};
    std::memset(&aggressor, 0, sizeof(aggressor));
    g_aggressor = &aggressor;
    g_apAmounts.clear();

    // Install a no-op Npc leaf hook backend (so RequestBuildOp93 emits don't
    // fault) and count the relation-delta commands the REAL function emits.
    g_op93Calls = 0;
    NpcLeafHooks npc{};
    npc.requestBuildOp93 = [](i32, int, i32, u8) { g_op93Calls++; };
    SetNpcLeafHooks(&npc);

    BrawlHooks bh = WireRealSiblings();
    SetBrawlHooks(&bh);

    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    Brawl_State(&r) = 0;
    Brawl_VictimId(&r) = 42;
    Brawl_ApDamage(&r) = 15;        // each blow -> -15 AP
    // Aggressor mood kinds 0 and 1 (valid: < 5 so the real fn applies a delta).
    HeBytes(&r)[184] = 0;           // mood kind 1
    HeBytes(&r)[185] = 1;           // mood kind 2

    for (int i = 1; i <= 5; ++i) {
        Brawl_Packet(&r) = -1;
        BrawlOutcome o = BrawlStep(&r);
        CHECK(o == BrawlOutcome::BlowLanded);
        CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), i);
    }
    CHECK_EQ(static_cast<int>(Brawl_State(&r)), 1);   // knockout pending

    // The REAL relation function (NpcAdjustRelationByMood) computes a nonzero
    // mood delta for each uncapped mood byte and emits it as an op93 command. Two
    // valid mood kinds * 5 blows -> 10 emitted relation-delta commands. (The real
    // function mutates relation state via the lockstep command, not in place.)
    CHECK_EQ(g_op93Calls, 10);

    // The REAL AP-event pool now carries five live slots, each the -15 damage.
    int live = 0, sum = 0;
    for (const auto& s : pool.slots) {
        if (s.ttl != 0) { ++live; sum += s.primary; }
    }
    CHECK_EQ(live, 5);
    CHECK_EQ(sum, -75);                       // 5 * -15
    CHECK_EQ(static_cast<int>(g_apAmounts.size()), 5);

    // The knockout blow now resolves through the same wiring without re-damaging.
    Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::Knockout);
    CHECK_EQ(static_cast<int>(g_apAmounts.size()), 5);   // no further AP damage
}

// ===========================================================================
// The real relation function caps each mood byte; once capped, AdjustRelation
// returns 0 (no command), but the brawl AP damage still lands every blow.
// ===========================================================================
TEST(BrawlItest, ApDamageLandsEvenWhenRelationCapped) {
    crt::Srand(7);
    guild::ai::ApEventPool pool{};  // default member inits zero every slot
    g_pool = &pool;

    HeRecord aggressor{};
    std::memset(&aggressor, 0, sizeof(aggressor));
    // Pre-saturate the mood byte so NpcAdjustRelationByMood early-returns 0.
    HeBytes(&aggressor)[128 + 0] = 0xFF;
    g_aggressor = &aggressor;
    g_apAmounts.clear();

    NpcLeafHooks npc{};
    npc.requestBuildOp93 = [](i32, int, i32, u8) {};
    SetNpcLeafHooks(&npc);
    BrawlHooks bh = WireRealSiblings();
    SetBrawlHooks(&bh);

    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    Brawl_State(&r) = 0;
    Brawl_VictimId(&r) = 42;
    Brawl_ApDamage(&r) = 20;
    HeBytes(&r)[184] = 0;     // mood kind 0 (already capped)
    HeBytes(&r)[185] = 0;

    Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::BlowLanded);

    int live = 0;
    for (const auto& s : pool.slots) if (s.ttl != 0) ++live;
    CHECK_EQ(live, 1);                         // AP damage landed regardless
    CHECK_EQ(static_cast<int>(g_apAmounts[0]), -20);
}
