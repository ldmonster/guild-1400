// Unit tests for VIBE_CharAction_BrawlStep @0x4d201c (charaction_brawl.{h,cpp}).
// The brawl combat damage-resolution state machine: per-blow mood + AP damage,
// the 5-hit knockout gate, the 24-tick swing cadence, terminal-free + busy gates.
// Golden values hand-derived from the Hex-Rays pseudocode.
#include "sim/charaction_brawl.h"
#include "sim/he.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Recording hook backend.
struct Rec {
    std::vector<i8>  moods;
    std::vector<i32> apEvents;
    int  freed = 0, defeatMsgs = 0, familyBumps = 0, requeues = 0;
    int  victimReturns = 1;   // 1 == victim present; 0 == gone
    int  packetStatus  = 1;   // resolved by default
    void* aggressor = reinterpret_cast<void*>(0x1);
    u16  victimWord = 0x33;
};
Rec* g_rec = nullptr;

// A small "person" with a u16 id/alive word at offset 0 (what the hooks read).
struct FakePerson { u16 word; };
FakePerson g_victim{0x33};

BrawlHooks MakeHooks() {
    BrawlHooks h{};
    h.packetStatus = [](i32) { return g_rec->packetStatus; };
    h.freeHandlerEntry = [](HeRecord*) { g_rec->freed++; };
    h.findPersonById = [](i32) -> void* {
        return g_rec->victimReturns ? static_cast<void*>(&g_victim) : nullptr;
    };
    h.aggressorRecord = [](HeRecord*) { return g_rec->aggressor; };
    h.adjustRelationByMood = [](void*, i8 kind) { g_rec->moods.push_back(kind); };
    h.registerApEvent = [](u16, i32 negAp) { g_rec->apEvents.push_back(negAp); };
    h.sendDefeatMessage = [](u16) { g_rec->defeatMsgs++; };
    h.bumpVictimFamilyDefeats = [](void*) { g_rec->familyBumps++; };
    h.restorePoseAndRequeue = [](HeRecord*) { g_rec->requeues++; };
    return h;
}

HeRecord MakeRecord() {
    HeRecord r{};
    std::memset(&r, 0, sizeof(r));
    return r;
}

} // namespace

TEST(SimBrawl, KnockoutGate) {
    // The brawl ends (state -> 1) on the 5th blow, not before.
    CHECK_EQ(static_cast<int>(kBrawlKnockoutHits), 5);
    CHECK_EQ(BrawlNextState(1), 0);
    CHECK_EQ(BrawlNextState(4), 0);
    CHECK_EQ(BrawlNextState(5), 1);
    CHECK_EQ(BrawlNextState(6), 1);
    CHECK_EQ(static_cast<int>(kBrawlActionTicks), 24);
}

TEST(SimBrawl, FieldOffsets) {
    HeRecord r = MakeRecord();
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_State(&r)) - reinterpret_cast<u8*>(&r), 112);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_Flags(&r)) - reinterpret_cast<u8*>(&r), 120);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_Packet(&r)) - reinterpret_cast<u8*>(&r), 132);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_VictimId(&r)) - reinterpret_cast<u8*>(&r), 172);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_ApDamage(&r)) - reinterpret_cast<u8*>(&r), 180);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_HitCount(&r)) - reinterpret_cast<u8*>(&r), 186);
    CHECK_EQ(reinterpret_cast<u8*>(&Brawl_ActionWord(&r)) - reinterpret_cast<u8*>(&r), 86);
}

TEST(SimBrawl, TerminalStateFrees) {
    Rec rec; g_rec = &rec;
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();

    Brawl_State(&r) = -2;
    CHECK(BrawlStep(&r) == BrawlOutcome::Freed);
    CHECK_EQ(rec.freed, 1);

    Brawl_State(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::Freed);
    CHECK_EQ(rec.freed, 2);
}

TEST(SimBrawl, BusyFlagGatesOut) {
    Rec rec; g_rec = &rec;
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();
    Brawl_State(&r) = 0;
    Brawl_Flags(&r) = 4;       // busy bit set
    CHECK(BrawlStep(&r) == BrawlOutcome::Busy);
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 0);   // no damage applied
}

TEST(SimBrawl, InFlightPacketGatesOut) {
    Rec rec; g_rec = &rec;
    rec.packetStatus = 0;       // packet NOT yet resolved
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();
    Brawl_State(&r) = 0;
    Brawl_Packet(&r) = 77;      // an in-flight packet
    CHECK(BrawlStep(&r) == BrawlOutcome::Busy);
    CHECK_EQ(Brawl_Packet(&r), 77);          // handle NOT cleared while in flight
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 0);
}

TEST(SimBrawl, LandedBlowAppliesMoodAndDamage) {
    Rec rec; g_rec = &rec;
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();
    Brawl_State(&r) = 0;
    Brawl_Packet(&r) = 5;             // resolved (status 1) -> cleared
    Brawl_VictimId(&r) = 42;
    Brawl_ApDamage(&r) = 30;          // AP damage magnitude -> event gets -30
    Brawl_ActionWord(&r) = 100;
    Brawl_HitCount(&r) = 0;
    // mood bytes: HIBYTE of dword at +181 / +182 == bytes at +184 / +185.
    HeBytes(&r)[184] = static_cast<u8>(7);
    HeBytes(&r)[185] = static_cast<u8>(9);

    CHECK(BrawlStep(&r) == BrawlOutcome::BlowLanded);
    CHECK_EQ(Brawl_Packet(&r), -1);                  // cleared
    CHECK_EQ(static_cast<int>(rec.moods.size()), 2);
    CHECK_EQ(static_cast<int>(rec.moods[0]), 7);
    CHECK_EQ(static_cast<int>(rec.moods[1]), 9);
    CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), 1);    // bumped
    CHECK_EQ(static_cast<int>(Brawl_State(&r)), 0);       // < 5 -> still 0
    CHECK_EQ(static_cast<int>(Brawl_ActionWord(&r)), 124); // 100 + 24
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 1);
    CHECK_EQ(rec.apEvents[0], -30);                  // negated AP damage
}

TEST(SimBrawl, FiveBlowsTriggerKnockoutState) {
    Rec rec; g_rec = &rec;
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();
    Brawl_State(&r) = 0;
    Brawl_VictimId(&r) = 42;
    Brawl_ApDamage(&r) = 10;

    // Four blows keep state 0; the fifth flips it to 1 (knockout pending).
    for (int i = 1; i <= 4; ++i) {
        Brawl_Packet(&r) = -1;
        CHECK(BrawlStep(&r) == BrawlOutcome::BlowLanded);
        CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), i);
        CHECK_EQ(static_cast<int>(Brawl_State(&r)), 0);
    }
    Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::BlowLanded);
    CHECK_EQ(static_cast<int>(Brawl_HitCount(&r)), 5);
    CHECK_EQ(static_cast<int>(Brawl_State(&r)), 1);   // KNOCKOUT pending

    // Total AP damage dealt over 5 blows: 5 * -10.
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 5);
    int sum = 0; for (int v : rec.apEvents) sum += v;
    CHECK_EQ(sum, -50);
    // Action word advanced 5 * 24.
    CHECK_EQ(static_cast<int>(Brawl_ActionWord(&r)), 120);
}

TEST(SimBrawl, KnockoutBlowEmitsDefeat) {
    Rec rec; g_rec = &rec;
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();
    Brawl_State(&r) = 1;              // knockout blow
    Brawl_Packet(&r) = -1;
    Brawl_VictimId(&r) = 42;

    CHECK(BrawlStep(&r) == BrawlOutcome::Knockout);
    CHECK_EQ(rec.defeatMsgs, 1);
    CHECK_EQ(rec.familyBumps, 1);
    CHECK_EQ(rec.requeues, 1);
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 0);  // no further AP on knockout
}

TEST(SimBrawl, VictimGonePathRequeues) {
    Rec rec; g_rec = &rec;
    rec.victimReturns = 0;            // victim record gone
    BrawlHooks h = MakeHooks(); SetBrawlHooks(&h);
    HeRecord r = MakeRecord();

    // state 0, victim gone -> pose restore + requeue, NO damage.
    Brawl_State(&r) = 0; Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::BlowMissedVictimGone);
    CHECK_EQ(rec.requeues, 1);
    CHECK_EQ(static_cast<int>(rec.apEvents.size()), 0);

    // state 1, victim gone -> knockout-victim-gone (still requeues).
    Brawl_State(&r) = 1; Brawl_Packet(&r) = -1;
    CHECK(BrawlStep(&r) == BrawlOutcome::KnockoutVictimGone);
    CHECK_EQ(rec.requeues, 2);
    CHECK_EQ(rec.defeatMsgs, 0);     // no defeat message when victim gone
}
