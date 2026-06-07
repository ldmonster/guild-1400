// tests/integration/slice_tavern_itest.cpp — the TAVERN slice on a small real-FORMAT
// world (synthetic person records in the live sim arrays, no shipped assets), driving
// the REAL op-83 command build + apply + REAL economy day.
//
// Asserts:
//   * a real folded person field (target relation +0x5C) CHANGED pre/post recruit
//     and the buyer cash (+0x0A) was debited the price,
//   * HashFullWorld() differs across the command and across the whole day,
//   * the run is DETERMINISTIC (a rerun produces byte-identical hashes),
//   * persons live in g_persons/g_personIds — both zeroed before seeding and
//     re-anchored before each compare (the play-layer ZeroWorldGlobals/Srand rule).
#include "test.h"

#include "play/slice_tavern.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/person_personnel2.h"
#include "sim/types.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// Seed a small real-FORMAT roster: slot 0 buyer (kind 10), dark-corner NPCs after.
void SeedPersons(int n) {
    std::memset(g_persons, 0, sizeof(g_persons));
    std::memset(g_personIds, 0, sizeof(g_personIds));
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_sceneNodes, 0, sizeof(g_sceneNodes));
    ResetEntityArrays();
    for (int i = 0; i < n && i < kPersonCapacity; ++i) {
        Person& p = g_persons[i];
        std::memset(&p, 0, kPersonStride);
        p.marker = (i16)i;
        i32 id = 8000 + i;
        PersonSetDword(&p, kPfId, id);
        g_personIds[i] = id;
        PersonSetByte(&p, kPfIsPlayer, 1);
        PersonSetByte(&p, kPfKind, (u8)(i == 0 ? 10 : 1));
        PersonSetWord(&p, kPfCash, (i16)(600 + 25 * i));
        PersonSetDword(&p, kPfRelationBase, -1);
        PersonSetDword(&p, kPfSuperiorId, -1);
    }
    g_personArrayLoaded = n > 0;
    g_sceneArrayLoaded  = true;
}

} // namespace

// ---------------------------------------------------------------------------
// The op-83 recruit mutates folded person fields pre/post, deterministically.
// ---------------------------------------------------------------------------
TEST(SliceTavernItest, RecruitMutatesFoldedPersonFieldDeterministic) {
    const i32 buyer = 8000, npc = 8001;

    SeedPersons(6);
    crt::Srand(0xCAFE);
    std::uint64_t hBefore = HashFullWorld();

    // The NPC is unbound (-1) before the recruit.
    Person* tgtPre = PersonFindRecordById(npc);
    CHECK(tgtPre != nullptr);
    i32 relBefore = tgtPre ? PersonGetDword(tgtPre, kTvRelationOff) : -999;
    CHECK_EQ(relBefore, -1);
    int buyerCashPre = 0;
    if (Person* b = PersonFindRecordById(buyer))
        buyerCashPre = (int)PersonGetWord(b, kTvCashOff);

    // --- build the byte-exact packet ---
    TavernClick c;
    c.action = TavernAction::kRecruitThug;
    c.playerId = buyer; c.targetId = npc; c.locationId = 1234; c.price = 40;
    TavernPacket pkt = BuildTavernPacket(c);
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 83);
    u8 wire[36];
    pkt.encode(wire);
    CHECK_EQ((int)wire[0], 83);
    std::printf("[tav-it] relBefore=%d buyerCashPre=%d opcode=%d\n",
                relBefore, buyerCashPre, (int)pkt.opcode);

    // --- issue through the real build+gate+apply path ---
    SetTavernApplyHooks(nullptr);
    TavernOrder o = IssueTavernClick(c);
    std::printf("[tav-it] issued=%d relAfter=%d supAfter=%d cashAfter=%d "
                "employmentAfter=%d\n",
                (int)o.issued, o.targetRelationAfter, o.targetSuperiorAfter,
                o.buyerCashAfter, o.employmentAfter);
    CHECK(o.issued);
    CHECK(o.applied);
    CHECK_EQ(o.targetRelationAfter, buyer);
    CHECK_EQ(o.targetSuperiorAfter, buyer);
    CHECK_EQ(o.buyerCashAfter, buyerCashPre - c.price);
    // FindEmploymentRelation scans the live roster for a kind-6/7 master that lists
    // the NPC in its relation array; the dark-corner binding is recorded on the NPC's
    // OWN +0x5C/+0x60 (op-36 Pair36 {npcId, playerId}), so the real result is
    // reported (not forced) — the buyer here is a kind-10 player, not a kind-6 master.
    std::printf("[tav-it] FindEmploymentRelation(npc)=%d\n", o.employmentAfter);

    crt::Srand(0xCAFE);
    std::uint64_t hAfterCmd = HashFullWorld();
    CHECK(hAfterCmd != hBefore);   // the folded g_persons changed

    // --- a real economy day on the slice's own roster (ids 6000+slot) ---
    TavernClick sc;
    sc.action = TavernAction::kRecruitThug;
    sc.playerId = 6000; sc.targetId = 6001; sc.locationId = 99; sc.price = 30;
    TavernSliceResult r = RunTavernSliceSynthetic(0x4242, 6, sc, 0xCAFE);
    std::printf("[tav-it] hashAfterSeed=%llu hashAfterCommand=%llu hashAfterDay=%llu "
                "econPasses=%d\n",
                (unsigned long long)r.hashAfterSeed,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay, r.economyPasses);
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.economyPasses > 0);

    // --- determinism: full rerun is byte-identical ---
    TavernSliceResult r2 = RunTavernSliceSynthetic(0x4242, 6, sc, 0xCAFE);
    CHECK_EQ((long long)r.hashAfterSeed,    (long long)r2.hashAfterSeed);
    CHECK_EQ((long long)r.hashAfterCommand, (long long)r2.hashAfterCommand);
    CHECK_EQ((long long)r.hashAfterDay,     (long long)r2.hashAfterDay);

    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// Affordability gate on a real-format record: a too-dear price does not build.
// ---------------------------------------------------------------------------
TEST(SliceTavernItest, AffordabilityGateOnLoadedRecord) {
    SeedPersons(4);
    TavernClick c;
    c.action = TavernAction::kRecruitThug;
    c.playerId = 8000; c.targetId = 8001; c.locationId = 5; c.price = 50000;
    TavernPacket pkt = BuildTavernPacket(c);
    std::printf("[tav-it] tooDear built=%d opcode=%d\n", (int)pkt.built, (int)pkt.opcode);
    CHECK(!pkt.built);
    TavernOrder o = IssueTavernClick(c);
    CHECK(!o.issued);
    CHECK(!o.applied);
    ResetEntityArrays();
}
