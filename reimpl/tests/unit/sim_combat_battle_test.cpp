// Unit tests for the combat BATTLE module (projectile.{h,cpp} +
// combat_battle.{h,cpp}). Golden vectors are python-computed; the RNGs are
// reproduced bit-exact (CRT LCG and the cutscene LCG).
#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/combat_battle.h"
#include "sim/projectile.h"
#include "crt/rand.h"
#include "test.h"

#include <cmath>
#include <cstddef>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// Struct layouts (byte-for-byte against recovered strides/offsets).
// ===========================================================================
TEST(SimCombatBattle, OrderSlotLayout) {
    CHECK_EQ(sizeof(OrderSlot), static_cast<size_t>(44));
    CHECK_EQ(kOrderSlotStride, 44);
    CHECK_EQ(offsetof(OrderSlot, unitId), static_cast<size_t>(0));
    CHECK_EQ(offsetof(OrderSlot, state), static_cast<size_t>(4));
    CHECK_EQ(offsetof(OrderSlot, packetId), static_cast<size_t>(8));
    CHECK_EQ(offsetof(OrderSlot, phase), static_cast<size_t>(12));
    CHECK_EQ(offsetof(OrderSlot, tileX), static_cast<size_t>(16));
    CHECK_EQ(offsetof(OrderSlot, tileZ), static_cast<size_t>(20));
    CHECK_EQ(offsetof(OrderSlot, tileAux), static_cast<size_t>(24));
    CHECK_EQ(offsetof(OrderSlot, moved), static_cast<size_t>(28));
    CHECK_EQ(offsetof(OrderSlot, firing), static_cast<size_t>(29));
    CHECK_EQ(offsetof(OrderSlot, hitFlag), static_cast<size_t>(32));
    CHECK_EQ(offsetof(OrderSlot, predictedDamage), static_cast<size_t>(36));
    // warePhase (+34) aliases hitFlag byte 2; check the accessor.
    OrderSlot s{}; s.hitFlag = 0x00AB0000; CHECK_EQ(static_cast<int>(s.WarePhase()), 0xAB);
}

TEST(SimCombatBattle, BattleDescriptorDefaults) {
    BattleDescriptor b;
    CHECK_EQ(b.attackerRoster[0], -1);
    CHECK_EQ(b.defenderRoster[15], -1);
    CHECK_EQ(b.productionObjects[0], -1);
    CHECK_EQ(kRosterCapacity, 16);
}

// ===========================================================================
// Unit strength — GetSoundRangeScale / WeaponWeight
// ===========================================================================
TEST(SimCombatBattle, WeaponWeightTable) {
    // The weights are FLOAT-precision in the binary (v8 is a float register slot):
    // 0.80000001==float(0.8), 0.69999999==float(0.7), 0.89999998==float(0.9),
    // 0.30000001==float(0.3). Compare against the float-rounded constants exactly.
    CHECK_EQ(WeaponWeight(340), static_cast<double>(static_cast<float>(0.5)));
    CHECK_EQ(WeaponWeight(342), static_cast<double>(static_cast<float>(0.8)));
    CHECK_EQ(WeaponWeight(344), static_cast<double>(static_cast<float>(1.0)));
    CHECK_EQ(WeaponWeight(350), static_cast<double>(static_cast<float>(0.7)));
    CHECK_EQ(WeaponWeight(352), static_cast<double>(static_cast<float>(0.7)));
    CHECK_EQ(WeaponWeight(366), static_cast<double>(static_cast<float>(0.8)));
    CHECK_EQ(WeaponWeight(374), static_cast<double>(static_cast<float>(0.9)));
    CHECK_EQ(WeaponWeight(1234), static_cast<double>(static_cast<float>(0.3)));  // default
}

TEST(SimCombatBattle, GetSoundRangeScale) {
    CombatUnitAI u;
    u.weaponType = 342;          // weight 0.8
    u.productionRating = 2.0f;
    u.outputRatio = 0.5f;
    // 0.5 (output) * (2.0 * 0.8) = 0.8
    CHECK(std::fabs(GetSoundRangeScale(u) - 0.8) < 1e-6);
}

// ===========================================================================
// Role scoring — ScoreUnitForRole
// ===========================================================================
TEST(SimCombatBattle, ScoreUnitForRole) {
    CombatUnitAI u;
    u.weaponType = 344;          // weight 1.0
    u.productionRating = 1.0f;
    u.outputRatio = 1.0f;        // strength = 1.0
    u.weaponClass = 0;
    u.role = 0;
    // role 0, unassigned -> strength * 1.0 = 1.0
    CHECK(std::fabs(ScoreUnitForRole(u, 0) - 1.0) < 1e-6);
    u.role = 3;                  // already assigned -> * 0.5
    CHECK(std::fabs(ScoreUnitForRole(u, 0) - 0.5) < 1e-6);
    // role 3 always 1.0
    CHECK(std::fabs(ScoreUnitForRole(u, 3) - 1.0) < 1e-6);
    // role 2: assigned-to-2 -> 1.0, else 0.5
    u.role = 2;
    CHECK(std::fabs(ScoreUnitForRole(u, 2) - 1.0) < 1e-6);
    u.role = 0;
    CHECK(std::fabs(ScoreUnitForRole(u, 2) - 0.5) < 1e-6);
    // role 4: 1/outputRatio (role != 2)
    u.outputRatio = 0.25f;
    CHECK(std::fabs(ScoreUnitForRole(u, 4) - 4.0) < 1e-5);
    // dead ranged target on role 0 -> 0.2 * strength
    u.outputRatio = 1.0f; u.weaponClass = 2; u.activeTargetHp = 0; u.role = 0;
    CHECK(std::fabs(ScoreUnitForRole(u, 0) - 0.2) < 1e-6);
}

// ===========================================================================
// Balance index — ComputeBalanceIndex
// ===========================================================================
TEST(SimCombatBattle, ComputeBalanceIndex) {
    BattleDescriptor b;
    b.modeFlags = kBattleAttack;          // attack flag (bit1)
    // attacker -> mode 2; strength ratio bucketed.
    BalanceIndex hi = ComputeBalanceIndex(b, true, 2.0, 1.0, 0);   // ratio 2.0 >= 1.75
    CHECK_EQ(hi.mode, 2);
    CHECK_EQ(hi.bucket, 4);
    BalanceIndex mid = ComputeBalanceIndex(b, true, 1.5, 1.0, 0);  // 1.25<=1.5<1.75
    CHECK_EQ(mid.bucket, 3);
    BalanceIndex low = ComputeBalanceIndex(b, true, 1.0, 1.0, 0);  // 0.75<=1.0<1.25
    CHECK_EQ(low.bucket, 2);
    BalanceIndex vl = ComputeBalanceIndex(b, true, 0.6, 1.0, 0);   // 0.5<=0.6<0.75
    CHECK_EQ(vl.bucket, 1);
    BalanceIndex z = ComputeBalanceIndex(b, true, 0.1, 1.0, 0);    // <0.5
    CHECK_EQ(z.bucket, 0);
    // defender mode 3; flag-count override.
    BalanceIndex d1 = ComputeBalanceIndex(b, false, 2.0, 1.0, 1);  // enemyCount 1 -> bucket1
    CHECK_EQ(d1.mode, 3);
    CHECK_EQ(d1.bucket, 1);
    BalanceIndex d2 = ComputeBalanceIndex(b, false, 2.0, 1.0, 3);  // 3 -> bucket0
    CHECK_EQ(d2.bucket, 0);
}

// ===========================================================================
// EvaluateAttack — range gate + hit-chance + predicted damage (golden)
// ===========================================================================
TEST(SimCombatBattle, EvaluateAttackOutOfRange) {
    CutsceneRng rng; rng.state = 77;
    AttackEval e = EvaluateAttack(/*dist*/200.0, /*range*/100.0, /*acc*/200,
                                  /*skill*/3, /*mod*/1.0, /*hasTarget*/true,
                                  /*worth*/120.0f, /*min*/8, /*max*/30, rng);
    CHECK(!e.inRange);
    CHECK(!e.fires);
    CHECK_EQ(e.predictedDamage, 0);
}

TEST(SimCombatBattle, EvaluateAttackHitChanceGolden) {
    // acc=200, skill=3 -> chance = (int)(200*0.01*3) = 6.
    // seed 1: RandomModulo(255) == 16838 % 255 == 8; fires = 8 > (255-6=249) -> false.
    crt::Srand(1);
    CutsceneRng rng; rng.state = 77;
    AttackEval e = EvaluateAttack(50.0, 100.0, 200, 3, 1.0, true, 120.0f, 8, 30, rng);
    CHECK(e.inRange);
    CHECK(!e.fires);              // 8 is not > 249
}

TEST(SimCombatBattle, EvaluateAttackFiresGolden) {
    // High accuracy so the roll always fires: acc=255, skill=100 ->
    // chance = (int)(255*0.01*100) = 255; fires = roll > (255-255=0) -> roll>0.
    // seed 1: roll = 16838 % 255 = 8 > 0 -> fires.
    // Then cutseed 77: RandInt(30-8=22) -> ((s*..)>>16 %0x7FFF)%22.
    crt::Srand(1);
    CutsceneRng rng; rng.state = 77;
    AttackEval e = EvaluateAttack(50.0, 100.0, 255, 100, 1.0, true, 120.0f, 8, 30, rng);
    CHECK(e.inRange);
    CHECK(e.fires);
    // python: roll=8, rollSum=8+8=16, predicted=(int)(16*120.0*0.01)=19
    CHECK_EQ(e.predictedDamage, 19);
}

TEST(SimCombatBattle, EvaluateAttackNoTargetWorth) {
    // No unit target -> worth = 900.0; chance 255 fires.
    crt::Srand(1);
    CutsceneRng rng; rng.state = 77;
    AttackEval e = EvaluateAttack(10.0, 100.0, 255, 100, 1.0, false, 0.0f, 8, 30, rng);
    CHECK(e.fires);
    // rollSum=16, predicted = (int)(16*900.0*0.01) = (int)144.0 = 144
    CHECK_EQ(e.predictedDamage, 144);
}

// ===========================================================================
// Projectile / bomb physics — golden
// ===========================================================================
TEST(SimCombatBattle, ThrownBombBlastGolden) {
    CombatField field;
    CombatUnit* u = field.Spawn(/*id*/5, /*hp*/1000, /*team*/2);
    u->worth = 900.0f;
    Bomb bomb; bomb.active = true; bomb.x = 0; bomb.y = 0; bomb.z = 0;
    bomb.minDamage = 10; bomb.maxDamage = 40;
    BlastTarget t; t.unit = u; t.x = 20.0f; t.y = 0.0f; t.z = 0.0f; // dist 20

    CutsceneRng rng; rng.state = 12345;
    BombHit h = ResolveBlastOnTarget(bomb, t, kThrownBombRadius, kThrownBombFalloff,
                                     /*friendlySide*/1, rng);
    // python: roll=19 rollSum=29 baseI=261 dmg=208
    CHECK_EQ(h.unitId, 5);
    CHECK_EQ(h.damage, 208);
    CHECK_EQ(u->hp, 1000 - 208);
    CHECK_EQ(h.teamColor, 2);     // friendlySide 1 != team 2
    CHECK(!h.killed);             // 792/1000 ratio >> 0.05
}

TEST(SimCombatBattle, DroppedBombBlastGolden) {
    CombatField field;
    CombatUnit* u = field.Spawn(7, 800, 1);
    u->worth = 500.0f;
    Bomb bomb; bomb.active = true; bomb.x = 0; bomb.z = 0; bomb.y = 0;
    bomb.minDamage = 5; bomb.maxDamage = 25;
    BlastTarget t; t.unit = u; t.x = 85.0f; t.z = 0.0f; t.y = 0.0f;  // dist 85
    CutsceneRng rng; rng.state = 999;
    BombHit h = ResolveBlastOnTarget(bomb, t, kDroppedBombRadius, kDroppedBombFalloff,
                                     1, rng);
    // python (with the binary float falloff 1/170 == 0.0058823530562222):
    //   roll=5 rollSum=10 baseI=50; 1-85*falloff = 0.49999999 -> dmg = (int)24.99 = 24.
    CHECK_EQ(h.damage, 24);
    CHECK_EQ(h.teamColor, 1);     // friendlySide 1 == team 1
}

TEST(SimCombatBattle, BombOutOfRangeNoDamage) {
    CombatField field;
    CombatUnit* u = field.Spawn(9, 500, 2);
    u->worth = 900.0f;
    Bomb bomb; bomb.active = true;
    bomb.minDamage = 10; bomb.maxDamage = 40;
    BlastTarget t; t.unit = u; t.x = 150.0f;   // > 100 radius
    CutsceneRng rng; rng.state = 1;
    BombHit h = ResolveBlastOnTarget(bomb, t, kThrownBombRadius, kThrownBombFalloff, 1, rng);
    CHECK_EQ(h.unitId, -1);       // out of range
    CHECK_EQ(u->hp, 500);         // untouched
}

TEST(SimCombatBattle, UpdateThrownBombsDeactivates) {
    CombatField field;
    CombatUnit* u = field.Spawn(1, 1000, 2);
    u->worth = 900.0f;
    std::vector<Bomb> bombs(1);
    bombs[0].active = true; bombs[0].minDamage = 10; bombs[0].maxDamage = 40;
    std::vector<BlastTarget> targets;
    BlastTarget t; t.unit = u; t.x = 20.0f; targets.push_back(t);
    CutsceneRng rng; rng.state = 12345;
    std::vector<BombHit> hits;
    int active = UpdateThrownBombs(bombs, targets, 1, rng, &hits);
    CHECK_EQ(active, 1);          // one bomb was active this tick
    CHECK(!bombs[0].active);      // deactivated after detonation
    CHECK_EQ(static_cast<int>(hits.size()), 1);
    CHECK_EQ(hits[0].damage, 208);
}

TEST(SimCombatBattle, UpdateBombExplosionsFuse) {
    CombatField field;
    CombatUnit* u = field.Spawn(1, 1000, 2);
    u->worth = 500.0f;
    std::vector<Bomb> bombs(1);
    bombs[0].active = true; bombs[0].minDamage = 5; bombs[0].maxDamage = 25;
    bombs[0].spawnTick = 100;     // fuse expires at 100 + 350 = 450
    std::vector<BlastTarget> targets;
    BlastTarget t; t.unit = u; t.x = 85.0f; targets.push_back(t);
    CutsceneRng rng; rng.state = 999;
    // now = 400 -> not yet (400 < 450 means fuse not expired: 100+350=450; 450<400 false)
    int a1 = UpdateBombExplosions(bombs, targets, 400, 1, rng, nullptr);
    CHECK_EQ(a1, 1);
    CHECK(bombs[0].active);       // still active, fuse not expired
    CHECK_EQ(u->hp, 1000);
    // now = 500 -> 450 < 500 -> detonate.
    std::vector<BombHit> hits;
    UpdateBombExplosions(bombs, targets, 500, 1, rng, &hits);
    CHECK(!bombs[0].active);
    CHECK_EQ(hits.size(), static_cast<size_t>(1));
    CHECK_EQ(hits[0].damage, 24);
}

TEST(SimCombatBattle, RollProjectileDamageGolden) {
    // arrow: min=10,max=40,worth=900 -> base = (10 + RandInt(30)) * 900 * 0.01
    CutsceneRng rng; rng.state = 12345;
    int dmg = RollProjectileDamage(10, 40, 900.0f, rng);
    // python: roll=19 rollSum=29 base=int(29*900*0.01)=int(261.0)=261
    CHECK_EQ(dmg, 261);
}

// ===========================================================================
// BuildOrderForUnit — role -> action mapping
// ===========================================================================
TEST(SimCombatBattle, BuildOrderForUnit) {
    CombatField field;
    CombatUnit* cu = field.Spawn(1, 100, 1);
    CombatUnitAI u; u.unit = cu;

    u.role = kRoleAttack; u.weaponClass = 0;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::AttackNearest);
    u.weaponClass = 2;            // ranged -> threatened tile
    u.activeTargetHp = 5;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::AttackThreatTile);
    u.activeTargetHp = 0;        // ranged target dead -> wait
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::None);

    u.weaponClass = 0;
    u.role = kRoleConquerWare;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::ConquerWare);
    u.role = kRoleMoveConquer;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::MoveToConquer);
    u.role = kRoleHold;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::Hold);
    u.role = kRoleTile;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::TileOrder);
    u.role = kRoleEscape;
    CHECK(BuildOrderForUnit(u, true, false) == BattleAction::Escape);

    // captured / no actor -> None
    CHECK(BuildOrderForUnit(u, true, true) == BattleAction::None);
    CombatUnitAI empty;
    CHECK(BuildOrderForUnit(empty, true, false) == BattleAction::None);
}

// ===========================================================================
// EvaluateBattleOutcome
// ===========================================================================
TEST(SimCombatBattle, EvaluateBattleOutcome) {
    CombatField field;
    CombatUnit* a = field.Spawn(1, 100, 1);
    CombatUnit* d = field.Spawn(2, 100, 2);
    CombatUnitAI ua; ua.unit = a;
    CombatUnitAI ud; ud.unit = d;
    std::vector<CombatUnitAI*> atk{&ua};
    std::vector<CombatUnitAI*> def{&ud};

    CHECK(EvaluateBattleOutcome(atk, def) == BattleWinner::Undecided);
    d->alive = 0;                // defender dead
    CHECK(EvaluateBattleOutcome(atk, def) == BattleWinner::Attacker);
    d->alive = 1; a->alive = 4;  // attacker fled (marker 4 == not effective)
    CHECK(EvaluateBattleOutcome(atk, def) == BattleWinner::Defender);
    a->alive = 1;
    // flag-capture special case -> defender wins regardless.
    CHECK(EvaluateBattleOutcome(atk, def, /*allFlagsAttacker*/true) == BattleWinner::Defender);
}

// ===========================================================================
// AutoResolveBattle — abstract strength comparison (golden)
// ===========================================================================
TEST(SimCombatBattle, AutoResolveGolden) {
    CombatField field;
    // Attacker: 1 unit, strength contribution.
    CombatUnit* a = field.Spawn(1, 100, 1);
    CombatUnitAI ua; ua.unit = a;
    ua.weaponType = 344;  ua.productionRating = 10.0f; ua.outputRatio = 1.0f; // str 10
    // Defender: 1 unit, weaker.
    CombatUnit* d = field.Spawn(2, 100, 2);
    CombatUnitAI ud; ud.unit = d;
    ud.weaponType = 340;  ud.productionRating = 1.0f; ud.outputRatio = 1.0f;  // str 0.5
    std::vector<CombatUnitAI*> atk{&ua};
    std::vector<CombatUnitAI*> def{&ud};

    // SumStrength(atk)=10.0 (int 10), SumStrength(def)=0.5 (int 0).
    // Order of CRT rolls: defScore = RandomModulo(30) + 0; atkScore = (u16)(10 + RandomModulo(30)).
    // seed 1: first RandomModulo(30) = 16838 % 30 = 8 -> defScore = 8.
    //         second RandomModulo(30) = 5758 % 30 = 28 -> atkScore = 10 + 28 = 38.
    // defScore(8) <= atkScore(38) -> per the original, DEFENDER wins.
    crt::Srand(1);
    AutoResolveResult r = AutoResolveBattle(atk, def);
    CHECK_EQ(r.defenderScore, 8);
    CHECK_EQ(r.attackerScore, 38);
    CHECK(r.winner == BattleWinner::Defender);
}

// ===========================================================================
// AssignUnitsToRoles — RNG-driven planner (golden, hand-traced)
// ===========================================================================
TEST(SimCombatBattle, AssignUnitsToRolesDeterministic) {
    CombatField field;
    // Two units; balance weights heavily favour role 0 (attack).
    CombatUnit* c0 = field.Spawn(1, 100, 1);
    CombatUnit* c1 = field.Spawn(2, 100, 1);
    CombatUnitAI u0; u0.unit = c0; u0.weaponType = 344; u0.productionRating = 2.0f;
                    u0.outputRatio = 1.0f; u0.role = 0;
    CombatUnitAI u1; u1.unit = c1; u1.weaponType = 342; u1.productionRating = 1.0f;
                    u1.outputRatio = 1.0f; u1.role = 0;
    std::vector<CombatUnitAI*> units{&u0, &u1};

    // Cumulative role weights: all roll values <= weights[0] -> always role 0.
    RoleWeights w{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    crt::Srand(1);                // RandomFloatScaled draws
    CutsceneRng rng; rng.state = 7;
    AssignUnitsToRoles(units, w, rng);
    // Both unassigned (role 0) so they take the chosen role 0 without a reassign
    // roll; both end at role 0.
    CHECK_EQ(static_cast<int>(u0.role), 0);
    CHECK_EQ(static_cast<int>(u1.role), 0);
}

TEST(SimCombatBattle, AssignUnitsToRolesPicksByWeight) {
    CombatField field;
    CombatUnit* c0 = field.Spawn(1, 100, 1);
    CombatUnitAI u0; u0.unit = c0; u0.weaponType = 344; u0.productionRating = 1.0f;
                    u0.outputRatio = 1.0f; u0.role = 0;
    std::vector<CombatUnitAI*> units{&u0};
    // Weights such that any roll in (0,1) lands on role 3 (hold): weights[0..2]=0,
    // weights[3..5]=1. The first RandomFloatScaled (seed 1) = 0.5138 -> first r
    // with 0.5138 <= weights[r] is r=3.
    RoleWeights w{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    crt::Srand(1);
    CutsceneRng rng; rng.state = 7;
    AssignUnitsToRoles(units, w, rng);
    CHECK_EQ(static_cast<int>(u0.role), 3);
}

// --- Wave-12 hardening: boundary indices / empty collections ---------------

// ComputeBalanceIndex with no battle flags returns the default mode 6 (one past
// the 6-row table). This is a VALUE the caller uses to index dword_B59C40
// (30*mode); the index bound is the caller's contract — pin the boundary value
// so the table-read owner sees mode==6 explicitly (documented for that owner).
TEST(SimCombatBattle, ComputeBalanceIndexDefaultModeBoundary) {
    BattleDescriptor b;
    b.modeFlags = 0;                          // no raid/attack/defend bit
    BalanceIndex r = ComputeBalanceIndex(b, true, 1.0, 1.0, 0);
    CHECK_EQ(r.mode, 6);                       // default sentinel (out-of-table)
    // bucket still derived from the ratio (1.0 -> bucket 2).
    CHECK_EQ(r.bucket, 2);
    // Zero "other" strength avoids div-by-zero (ratio forced to 0 -> bucket 0).
    BalanceIndex z = ComputeBalanceIndex(b, true, 5.0, 0.0, 0);
    CHECK_EQ(z.bucket, 0);
}

// ScoreUnitForRole with a role byte past the handled 0..5 set falls into the
// default (0.0) — no table/array access, no UB on the out-of-range role.
TEST(SimCombatBattle, ScoreUnitForRoleOutOfRangeRole) {
    CombatField field;
    CombatUnit* c0 = field.Spawn(1, 100, 1);
    CombatUnitAI u; u.unit = c0; u.weaponType = 344; u.outputRatio = 1.0f;
    CHECK_EQ(ScoreUnitForRole(u, /*role*/ 200), 0.0);   // default branch
}

// Assigning roles over an EMPTY unit list is a no-op (the for-loop count is 0)
// and must not touch the empty pool vector.
TEST(SimCombatBattle, AssignUnitsToRolesEmpty) {
    std::vector<CombatUnitAI*> units;          // 0 units
    RoleWeights w{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    crt::Srand(1);
    CutsceneRng rng; rng.state = 7;
    AssignUnitsToRoles(units, w, rng);         // no crash, nothing assigned
    CHECK(units.empty());
}

// All weights zero: every cumulative threshold fails the `roll <= weights[r]`
// test, so chosenRole falls through to the last index (5) without reading past
// the 6-element RoleWeights array.
TEST(SimCombatBattle, AssignUnitsToRolesAllWeightsZero) {
    CombatField field;
    CombatUnit* c0 = field.Spawn(1, 100, 1);
    CombatUnitAI u0; u0.unit = c0; u0.weaponType = 344; u0.outputRatio = 1.0f;
                    u0.role = 0;
    std::vector<CombatUnitAI*> units{&u0};
    RoleWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    crt::Srand(1);
    CutsceneRng rng; rng.state = 7;
    AssignUnitsToRoles(units, w, rng);
    CHECK_EQ(static_cast<int>(u0.role), 5);    // last-index fallthrough
}
