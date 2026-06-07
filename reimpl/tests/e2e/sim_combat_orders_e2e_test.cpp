// End-to-end test for the combat ORDER-TICK flow: drive a single unit's order
// slot through a sequence of orders (march -> capture -> attack) using the
// non-attack state machine (combat_orders) and the attack tick (combat_battle),
// recording the emitted OrderCommands, then run a tiny two-unit battle to
// resolution and verify the alive states + outcome + the loot writeback, all
// against a hand-computed reference.
#include "sim/combat_orders.h"
#include "sim/combat_strength.h"
#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/combat_battle.h"
#include "crt/rand.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A world context that lets us script "arrived this tick?" per call.
struct ScriptedWorld {
    bool arrived = false;
    bool freeTileFound = true;
    bool busy = false;

    OrderWorldContext ctx() {
        OrderWorldContext c;
        bool* a = &arrived; bool* f = &freeTileFound; bool* b = &busy;
        c.unitOnTargetTile = [a](const OrderSlot&, float) { return *a; };
        c.findFreeTile = [f](i32 x, i32 z, i32& ox, i32& oz) {
            ox = x; oz = z; return *f;
        };
        c.unitBusy = [b]() { return *b; };
        return c;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Order sequence: march to a tile, then capture an objective, then attack.
// ---------------------------------------------------------------------------
TEST(CombatOrdersE2E, MarchCaptureAttackSequence) {
    std::vector<OrderCommand> log;

    OrderSlot slot{};
    slot.unitId = 5;
    slot.tileX = 8;
    slot.tileZ = 9;
    slot.packetId = -1;
    ScriptedWorld w;

    // --- MARCH: far -> path; mark a command in flight; finish; arrive -> sync ---
    slot.state = kOrderMarch;
    w.arrived = false;
    log.push_back(TickNonAttackOrder(slot, w.ctx()));   // PathToTile
    CHECK_EQ(log.back(), OrderCommand::PathToTile);
    CHECK_EQ(slot.phase, (u8)1);
    // simulate the path command still applying (gated) -> no work
    slot.packetId = 99;
    log.push_back(TickNonAttackOrder(slot, w.ctx()));   // None (gated)
    CHECK_EQ(log.back(), OrderCommand::None);
    // command finished + unit now on the tile -> sync done
    slot.packetId = 1;
    w.arrived = true;
    log.push_back(TickNonAttackOrder(slot, w.ctx()));   // SyncDone
    CHECK_EQ(log.back(), OrderCommand::SyncDone);

    // --- CAPTURE: re-issue, not arrived -> path; arrived -> sync ---
    slot.state = kOrderCapture;
    slot.phase = 0;
    slot.packetId = -1;
    w.arrived = false;
    log.push_back(TickNonAttackOrder(slot, w.ctx()));   // PathToTile
    CHECK_EQ(log.back(), OrderCommand::PathToTile);
    slot.packetId = 1;
    w.arrived = true;
    log.push_back(TickNonAttackOrder(slot, w.ctx()));   // SyncDone (captured)
    CHECK_EQ(log.back(), OrderCommand::SyncDone);

    // On reaching the objective, the capture transfer fires (owner -> attacker).
    CaptureResult cap = CaptureUnitAction(/*attackerSideOwner*/77, /*hasParent*/true);
    CHECK(cap.applied);
    CHECK_EQ(cap.newOwnerId, 77);

    // --- ATTACK: switch the slot to attack and run the attack evaluation ---
    slot.state = kOrderAttack;
    CutsceneRng arng; arng.state = 3;
    AttackEval e = EvaluateAttack(/*dist*/5.0, /*range*/100.0, /*acc*/255,
                                  /*skill*/120, /*mod*/1.0, /*hasTarget*/true,
                                  /*worth*/900.0f, /*min*/10, /*max*/30, arng);
    CHECK(e.inRange);
    CHECK(e.fires);                                     // high accuracy -> fires
    CHECK(e.predictedDamage > 0);

    // The full command log: march path, gated, march sync, capture path, capture sync.
    CHECK_EQ(log.size(), (size_t)5);
    CHECK_EQ(log[0], OrderCommand::PathToTile);
    CHECK_EQ(log[2], OrderCommand::SyncDone);
    CHECK_EQ(log[4], OrderCommand::SyncDone);
}

// ---------------------------------------------------------------------------
// A two-unit battle run to resolution + loot writeback, vs a reference.
// ---------------------------------------------------------------------------
TEST(CombatOrdersE2E, BattleToResolutionWithLoot) {
    crt::Srand(2024);

    // One attacker, one defender. Attacker has high accuracy & damage; defender
    // weak. Run melee rounds: attacker hits the defender until it drops.
    CombatField field;
    CombatUnit* atkU = field.Spawn(/*id*/1, /*hp*/200, /*team*/10);
    CombatUnit* defU = field.Spawn(/*id*/2, /*hp*/120, /*team*/20);
    atkU->worth = 1.0f;
    defU->worth = 1.0f;

    CombatUnitAI atk; atk.unit = atkU; atk.role = kRoleAttack;
    CombatUnitAI def; def.unit = defU; def.role = kRoleAttack;

    std::vector<CombatUnitAI*> attackers = { &atk };
    std::vector<CombatUnitAI*> defenders = { &def };

    CutsceneRng rng; rng.state = 1;
    int rounds = 0;
    BattleWinner winner = BattleWinner::Undecided;
    const int maxMaxHp = 120;
    while (rounds < 50) {
        ++rounds;
        // Attacker fires at defender.
        AttackEval e = EvaluateAttack(10.0, 100.0, /*acc*/255, /*skill*/120, 1.0,
                                      true, defU->worth, 5, 20, rng);
        if (e.fires) {
            int dmg = RollMeleeDamage();
            defU->hp -= dmg;
            // death gate: dead when hp/maxHp < 0.05
            if ((double)defU->hp / (double)maxMaxHp < kDeathRatio) {
                defU->hp = 0;
                defU->alive = 0;
            }
        }
        winner = EvaluateBattleOutcome(attackers, defenders);
        if (winner != BattleWinner::Undecided)
            break;
    }
    CHECK_EQ(winner, BattleWinner::Attacker);
    CHECK_EQ(defU->alive, (u8)0);
    CHECK(atkU->alive != 0);
    CHECK(rounds < 50);

    // Loot writeback for the winning attacker (raid spoils).
    CutsceneRng lootRng; lootRng.state = 11;
    std::vector<LootWare> spoils = { {/*ware*/4, /*qty*/60, /*owner*/0} };
    auto price = [](i32) { return 2.5; };
    // Reference computation.
    CutsceneRng probe; probe.state = 11;
    u32 baseRoll = probe.RandInt(50);
    int expectBase = (int)((baseRoll * kLootBaseCashScale + kLootBaseCashBias) * 800.0);
    u32 qtyRoll = probe.RandInt(20);
    int qty = (int)((double)(qtyRoll + 80) * kLootWareQtyScale * 60.0);
    if (qty < 1) qty = 1;
    int expectTotal = (int)(2.5 * qty + (double)expectBase);

    struct Sink : ILootCommandSink {
        int sells = 0, cash = 0;
        void OnSellWare(i32, i32, int, i32) override { ++sells; }
        void OnCashCredit(int) override { ++cash; }
    } sink;
    LootResult loot = ComputeAttackerStrength(/*commanderCash*/800, spoils,
                                              /*seller*/2, /*buyer*/1,
                                              /*hasFamily*/true, /*before*/1000,
                                              price, lootRng, &sink);
    CHECK_EQ(loot.cashTotal, expectTotal);
    CHECK_EQ(loot.familyCredited, 1000 + expectTotal);
    CHECK_EQ(sink.cash, 1);
    CHECK_EQ(sink.sells, 1);
}
