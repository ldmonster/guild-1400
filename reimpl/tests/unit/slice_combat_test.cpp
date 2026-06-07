// tests/unit/slice_combat_test.cpp — the COMBAT / INTRIGUE (attack, sabotage, theft,
// crime) slice on a SYNTHETIC world (no assets). Proves:
//   * BuildCombatPacket drives the REAL sim::BuildAttackPacket (0x488a4c) and emits
//     an opcode-80 packet with order-KIND byte = 2 (kOrderPacketAttack),
//   * ApplyCombatPacket writes the order into the target's folded record pad,
//   * ApplyCrimeCommand ports VIBE_Command_ExAddStraftat (0x498f44): a free
//     g_crimeTable slot gets the 45-byte crime record (id/perp/victim/location) and
//     the folded g_relationMatrix cell sours,
//   * the step sequence kSeed -> kCommand -> kDay: seed baseline, command mutates the
//     world (folded field), the day mutates it further,
//   * the whole sequence is DETERMINISTIC (identical per-step hashes across reruns).
#include "test.h"

#include "play/slice_combat.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "world/crime.h"
#include "world/law_types.h"
#include "world/relation.h"

#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::world;
using namespace guild::sim;

namespace {

CombatInteraction AttackInteraction() {
    CombatInteraction it;
    it.action      = HostileAction::kAttack;
    it.attackerId  = 500;
    it.targetId    = 777;
    it.attackTileX = 42;
    it.attackParam = 3;
    return it;
}

CombatInteraction CrimeInteraction() {
    CombatInteraction it;
    it.action        = HostileAction::kCrime;
    it.perpetratorId = 10;
    it.victimId      = 20;
    it.crimeLocation = 7;
    it.crimeType     = CrimeType::kSabotage;
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// ATTACK packet: the REAL BuildAttackPacket emits opcode 80 / order-kind 2.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, BuildAttackPacketEmitsOpcode80Kind2) {
    CombatPacket pkt = BuildCombatPacket(AttackInteraction());
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 80);                  // RequestBuildOp80
    CHECK_EQ((int)pkt.orderKind, 2);                // kOrderPacketAttack
    CHECK_EQ(pkt.target, 777);
    CHECK_EQ(pkt.attacker, 500);
    CHECK(pkt.ringSlot >= 0);                        // enqueued into the ring
}

TEST(SliceCombatUnit, BuildAttackPacketRejectsNonAttackAndNullArgs) {
    CombatInteraction crime = CrimeInteraction();
    CHECK(!BuildCombatPacket(crime).built);
    CHECK_EQ((int)BuildCombatPacket(crime).opcode, 0);

    CombatInteraction noTarget = AttackInteraction();
    noTarget.targetId = 0;
    CHECK(!BuildCombatPacket(noTarget).built);       // BuildAttackPacket early-out

    CombatInteraction noAttacker = AttackInteraction();
    noAttacker.attackerId = 0;
    CHECK(!BuildCombatPacket(noAttacker).built);
}

// ---------------------------------------------------------------------------
// ATTACK apply: writes the order kind into the target's folded record pad.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, ApplyAttackWritesOrderPad) {
    ResetEntityArrays();
    std::memset(g_objects, 0, sizeof(g_objects));
    g_objects[0].alive = 1;
    g_objects[0].id    = 777;
    SetCombatApplyHooks(nullptr);   // inert default -> REAL BuildingFindById

    u8* base = reinterpret_cast<u8*>(&g_objects[0]);
    CHECK_EQ((int)base[0x60], 0);

    CombatInteraction it = AttackInteraction();
    CombatPacket pkt = BuildCombatPacket(it);
    int applied = ApplyCombatPacket(pkt, it);

    CHECK_EQ(applied, 1);
    CHECK_EQ((int)base[0x60], 2);                    // order-kind = attack
    i32 destX = 0;
    std::memcpy(&destX, base + 0x64, sizeof destX);
    CHECK_EQ(destX, 42);                             // attackTileX

    ResetEntityArrays();
}

TEST(SliceCombatUnit, ApplyAttackNoTargetRecordIsInert) {
    ResetEntityArrays();
    std::memset(g_objects, 0, sizeof(g_objects));
    SetCombatApplyHooks(nullptr);
    CombatInteraction it = AttackInteraction();
    CombatPacket pkt = BuildCombatPacket(it);
    CHECK_EQ(ApplyCombatPacket(pkt, it), 0);         // no live target id -> no mutation
    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// CRIME apply: the REAL ExAddStraftat record-write core + relation hit.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, ApplyCrimeWritesCrimeRecordAndSoursRelation) {
    std::memset(g_crimeTable, 0, sizeof(g_crimeTable));
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));

    CombatInteraction it = CrimeInteraction();
    int relBefore = RelationGet(it.victimId, it.perpetratorId);
    CHECK_EQ(relBefore, 0);

    int slot = ApplyCrimeCommand(it, /*crimeId=*/1234);
    CHECK(slot >= 0);

    // The folded crime record carries the staged fields.
    const CrimeRecord& cr = g_crimeTable[slot];
    CHECK_EQ(cr.id, 1234);
    CHECK_EQ(cr.perpetrator, 10);                    // +22 taeter
    CHECK_EQ((int)cr.location, 7);                   // +28
    CHECK_EQ(cr.target, 20);                         // +33 victim
    CHECK_EQ(cr.provenState, 1);                     // +37 active (occupied)
    i32 opfer = 0;
    std::memcpy(&opfer, reinterpret_cast<const u8*>(&cr) + 18, sizeof opfer);
    CHECK_EQ(opfer, 20);                             // +18 opfer

    // The relation soured.
    int relAfter = RelationGet(it.victimId, it.perpetratorId);
    CHECK_EQ(relAfter, relBefore - 10);

    std::memset(g_crimeTable, 0, sizeof(g_crimeTable));
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
}

TEST(SliceCombatUnit, ApplyCrimeRejectsNonCrime) {
    CHECK_EQ(ApplyCrimeCommand(AttackInteraction(), 1), -1);
}

// ---------------------------------------------------------------------------
// Step sequencing (ATTACK): seed -> command (folded pad mutates) -> day.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, AttackStepSequencingMutatesThenDay) {
    CombatStepHash h[3];
    i32 before = -1, after = -1;
    int n = RunCombatStepsSynthetic(/*seed=*/0x2468, AttackInteraction(),
                                    h, 3, &before, &after);
    CHECK_EQ(n, 3);
    CHECK_EQ((int)before, 0);
    CHECK_EQ((int)after, 2);                         // pad-kind 0 -> 2 (attack)

    CHECK(!h[0].mutated);                            // seed baseline
    CHECK(h[1].mutated);                             // attack command moved the hash
    CHECK(h[2].mutated);                             // the day evolved further
    CHECK(h[2].hashAfter != h[0].hashAfter);
}

// ---------------------------------------------------------------------------
// Step sequencing (CRIME): seed -> command (relation/crime table mutates) -> day.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, CrimeStepSequencingMutatesThenDay) {
    CombatStepHash h[3];
    i32 before = -99, after = -99;
    int n = RunCombatStepsSynthetic(/*seed=*/0x1357, CrimeInteraction(),
                                    h, 3, &before, &after);
    CHECK_EQ(n, 3);
    CHECK_EQ((int)before, 0);
    CHECK_EQ((int)after, -10);                       // relation soured

    CHECK(!h[0].mutated);
    CHECK(h[1].mutated);                             // crime command moved the hash
    CHECK(h[2].mutated);
}

// ---------------------------------------------------------------------------
// Determinism: identical per-step hashes across two independent runs.
// ---------------------------------------------------------------------------
TEST(SliceCombatUnit, StepSequenceIsDeterministic) {
    CombatStepHash a[3], b[3];
    RunCombatStepsSynthetic(0x1111, AttackInteraction(), a, 3, nullptr, nullptr);
    RunCombatStepsSynthetic(0x1111, AttackInteraction(), b, 3, nullptr, nullptr);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)a[i].hashAfter, (long long)b[i].hashAfter);

    CombatStepHash c[3];
    RunCombatStepsSynthetic(0x2222, AttackInteraction(), c, 3, nullptr, nullptr);
    CHECK(c[2].hashAfter != a[2].hashAfter);         // different seed -> diverged day

    // The crime path is deterministic too.
    CombatStepHash d[3], e[3];
    RunCombatStepsSynthetic(0x3333, CrimeInteraction(), d, 3, nullptr, nullptr);
    RunCombatStepsSynthetic(0x3333, CrimeInteraction(), e, 3, nullptr, nullptr);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)d[i].hashAfter, (long long)e[i].hashAfter);
}
