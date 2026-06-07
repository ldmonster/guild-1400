// Unit tests for guild::sim combat_slots4 (gilde.exe Combat batch-4 leaves).
// Golden vectors computed independently in python3 (see the brief). The RNG goldens
// use the CRT LCG (crt::Srand + Math_RandomModulo) seeded deterministically.
#include "sim/combat_slots4.h"
#include "sim/combat.h"
#include "crt/rand.h"

#include "test.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A small recording mock for the hooks.
struct Recorder {
    int   resetHi = 0;
    int   deltaCalls = 0;
    i8    lastDelta = 0;
    int   fleeMsgs = 0;
    int   bloodCalls = 0;
    float lastBloodY = 0.0f;
    // damage-number widget side
    int   labelsCreated = 0;
    int   layouts = 0;
    int   shows = 0, hides = 0;
    int   destroys = 0;
    bool  forceOnScreen = true;
    int   fakeX = 100, fakeY = 200;
    std::string lastText;
};
Recorder* g_rec = nullptr;

void H_resetHi(const void*) { g_rec->resetHi++; }
void H_delta(const void*, i8 v) { g_rec->deltaCalls++; g_rec->lastDelta = v; }
void H_flee(const void*) { g_rec->fleeMsgs++; }
int  H_blood(const void*, float y) { g_rec->bloodCalls++; g_rec->lastBloodY = y; return 777; }
bool H_screen(const void*, int* x, int* y) {
    if (!g_rec->forceOnScreen) return false;
    *x = g_rec->fakeX; *y = g_rec->fakeY; return true;
}
int  H_label(int, int, const char* t) { g_rec->labelsCreated++; g_rec->lastText = t; return 4242; }
void H_layout(int, int, int) { g_rec->layouts++; }
void H_visible(int, int v) { if (v) g_rec->shows++; else g_rec->hides++; }
void H_destroy(int) { g_rec->destroys++; }

CombatSlots4Hooks MakeHooks() {
    CombatSlots4Hooks h{};
    h.resetObjectHighlights = H_resetHi;
    h.queueEscapeDelta = H_delta;
    h.sendFleeMessage = H_flee;
    h.spawnBloodPoolMesh = H_blood;
    h.objectScreenBounds = H_screen;
    h.createTextLabel = H_label;
    h.widgetLayoutBounds = H_layout;
    h.objectSetVisible = H_visible;
    h.widgetDestroyByType = H_destroy;
    return h;
}

float feq(float a, float b) { return std::fabs(a - b) <= 1e-6f * (1.0f + std::fabs(b)); }

} // namespace

// ===========================================================================
// DistanceToTargetXZ  (0x4864a0)
// ===========================================================================
TEST(CombatSlots4_Distance, ClassicTriples) {
    CHECK(feq(DistanceToTargetXZ(0, 0, 3, 4), 5.0f));        // 3-4-5
    CHECK(feq(DistanceToTargetXZ(1.5f, 2.5f, 4.5f, 6.5f), 5.0f)); // dx=3 dz=4
    CHECK(feq(DistanceToTargetXZ(10, 10, 10, 10), 0.0f));    // coincident
    CHECK(feq(DistanceToTargetXZ(0, 0, 5, 0), 5.0f));        // pure x
    CHECK(feq(DistanceToTargetXZ(0, 0, 0, -7), 7.0f));       // pure -z
}

// ===========================================================================
// TriggerEscapeAction  (0x485c0c)
// ===========================================================================
TEST(CombatSlots4_Escape, CannotFleeNoop) {
    Recorder rec; g_rec = &rec;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    EscapeInput in; in.canFlee = false; in.cowardice = 9;
    CHECK(!TriggerEscapeAction(in, nullptr));
    CHECK_EQ(rec.deltaCalls, 0);
    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_Escape, RollGatedFlee) {
    // seed 1 -> Math_RandomModulo(10) sequence: 8,8,3,5,...
    // cowardice 9 -> roll(8) < 9 -> flee (first call).
    Recorder rec; g_rec = &rec;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    crt::Srand(1);
    EscapeInput in; in.canFlee = true; in.cowardice = 9;
    in.atBuilding = false; in.hasObjectDef = true;
    CHECK(TriggerEscapeAction(in, nullptr));   // roll 8 < 9
    CHECK_EQ(rec.resetHi, 1);
    CHECK_EQ(rec.deltaCalls, 1);
    CHECK_EQ((int)rec.lastDelta, -9);          // flag = -cowardice
    CHECK_EQ(rec.fleeMsgs, 0);                 // not at a 6/7 building
    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_Escape, RollGatedStay) {
    // seed 1, but cowardice 3: roll 8 is NOT < 3 -> stay; delta flag = 1.
    Recorder rec; g_rec = &rec;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    crt::Srand(1);
    EscapeInput in; in.canFlee = true; in.cowardice = 3;
    CHECK(!TriggerEscapeAction(in, nullptr));
    CHECK_EQ(rec.resetHi, 0);
    CHECK_EQ(rec.deltaCalls, 1);
    CHECK_EQ((int)rec.lastDelta, 1);           // "stop" flag
    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_Escape, FleeShoutAtBuilding) {
    Recorder rec; g_rec = &rec;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    crt::Srand(1);                              // roll 8
    EscapeInput in; in.canFlee = true; in.cowardice = 10;
    in.atBuilding = true; in.buildingClass = 7; in.hasObjectDef = true;
    CHECK(TriggerEscapeAction(in, nullptr));
    CHECK_EQ(rec.fleeMsgs, 1);
    SetCombatSlots4Hooks(nullptr);
}

// ===========================================================================
// SpawnDamageNumber  (0x487300)
// ===========================================================================
TEST(CombatSlots4_DmgSpawn, ValueAndSlotAlloc) {
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    int dummy = 0;
    // golden: (int)(50 / (1.0 * 0.01)) = 5000
    int slot = SpawnDamageNumber(tbl, &dummy, 50, 0, 1.0f);
    CHECK_EQ(slot, 0);
    CHECK_EQ(tbl[0].value, 5000);
    CHECK(feq(tbl[0].ttl, 64.0f));
    CHECK_EQ(tbl[0].kind, 0);
    CHECK_EQ(tbl[0].widget, -1);
    // next spawn lands in slot 1 (slot 0 now live).
    int s2 = SpawnDamageNumber(tbl, &dummy, 7, 1, 2.0f);  // (int)(7/0.02)=350
    CHECK_EQ(s2, 1);
    CHECK_EQ(tbl[1].value, 350);
    CHECK_EQ(tbl[1].kind, 1);
}

TEST(CombatSlots4_DmgSpawn, TableFullReturnsNeg1) {
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    for (auto& r : tbl) r.value = 1;   // all live
    int dummy = 0;
    CHECK_EQ(SpawnDamageNumber(tbl, &dummy, 10, 0, 0.5f), -1);
}

TEST(CombatSlots4_DmgSpawn, ScaleGolden) {
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    int dummy = 0;
    SpawnDamageNumber(tbl, &dummy, 100, 2, 0.5f);  // (int)(100/(0.5*0.01))=20000
    CHECK_EQ(tbl[0].value, 20000);
}

// ===========================================================================
// UpdateDamageNumbers  (0x48736c)
// ===========================================================================
TEST(CombatSlots4_DmgUpdate, OffScreenHides) {
    Recorder rec; g_rec = &rec; rec.forceOnScreen = false;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    tbl[0].value = 5; tbl[0].widget = 99; tbl[0].ttl = 10.0f;
    int expired = UpdateDamageNumbers(tbl, -0.5);
    CHECK_EQ(expired, 0);
    CHECK_EQ(rec.hides, 1);
    CHECK(feq(tbl[0].ttl, 10.0f));   // no decay while off screen
    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_DmgUpdate, OnScreenCreatesLabelAndDecays) {
    Recorder rec; g_rec = &rec; rec.forceOnScreen = true;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    tbl[0].value = 5; tbl[0].widget = -1; tbl[0].ttl = 1.0f; tbl[0].kind = 0;
    int expired = UpdateDamageNumbers(tbl, -0.5);   // ttl 1.0 -> 0.5
    CHECK_EQ(expired, 0);
    CHECK_EQ(rec.labelsCreated, 1);
    CHECK_EQ(rec.shows, 1);
    CHECK_EQ(tbl[0].widget, 4242);
    CHECK(feq(tbl[0].ttl, 0.5f));
    CHECK_EQ(rec.lastText, std::string("-5"));
}

TEST(CombatSlots4_DmgUpdate, ExpiryDestroysAndFrees) {
    Recorder rec; g_rec = &rec; rec.forceOnScreen = true;
    auto hooks = MakeHooks(); SetCombatSlots4Hooks(&hooks);
    std::vector<DmgNumberRecord> tbl(kDmgNum4Count);
    tbl[0].value = 5; tbl[0].widget = 50; tbl[0].ttl = 0.5f;  // -0.5 -> 0.0 -> expire
    int expired = UpdateDamageNumbers(tbl, -0.5);
    CHECK_EQ(expired, 1);
    CHECK_EQ(rec.destroys, 1);
    CHECK_EQ(tbl[0].value, 0);
    CHECK_EQ(tbl[0].widget, -1);
    CHECK(tbl[0].owner == nullptr);
    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_DmgUpdate, KindFormatting) {
    char buf[64];
    FormatDamageText(buf, sizeof(buf), 12, 0);
    CHECK_EQ(std::string(buf), std::string("-12"));
    FormatDamageText(buf, sizeof(buf), 7, 1);   // [-7] (chars 92 '\\' 93 ']')
    CHECK_EQ(std::string(buf), std::string("\\-7]"));
    FormatDamageText(buf, sizeof(buf), 3, 2);   // ^-3_ (chars 94 '^' 95 '_')
    CHECK_EQ(std::string(buf), std::string("^-3_"));
}

// ===========================================================================
// EvalUnitAttackMove  (0x491324)
// ===========================================================================
TEST(CombatSlots4_AttackMove, NoWeaponTypeSet) {
    CHECK(IsNoWeaponDefType(0));
    CHECK(IsNoWeaponDefType(340));
    CHECK(IsNoWeaponDefType(374));
    CHECK(!IsNoWeaponDefType(341));
    CHECK(!IsNoWeaponDefType(160));
}

TEST(CombatSlots4_AttackMove, RangedDeadTargetHolds) {
    AttackMoveInput in;
    in.weaponClass = 1; in.hasActiveTarget = true; in.activeTargetHp = 0;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kHoldNoTarget);
    in.weaponClass = 2; in.activeTargetHp = -3;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kHoldNoTarget);
}

TEST(CombatSlots4_AttackMove, NoWeaponMovesToTargetWhenFar) {
    AttackMoveInput in;
    in.hasObjectDef = true; in.objDefType = 340;   // no-weapon set
    in.hasTarget = true; in.distanceToTarget = 30.0f; in.weaponRange = 10.0f;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kMoveToTarget);
    in.hasTarget = false;                           // no target -> also move
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kMoveToTarget);
}

TEST(CombatSlots4_AttackMove, NoWeaponInRangeFallsToAttack) {
    AttackMoveInput in;
    in.hasObjectDef = true; in.objDefType = 340;
    in.hasTarget = true; in.distanceToTarget = 4.0f; in.weaponRange = 10.0f;
    in.selfBusy = false;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kAttack);
    in.selfBusy = true;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kHoldBusy);
}

TEST(CombatSlots4_AttackMove, WeaponedAttacks) {
    AttackMoveInput in;
    in.hasObjectDef = true; in.objDefType = 200;   // usable weapon
    in.weaponClass = 1; in.hasActiveTarget = true; in.activeTargetHp = 50;
    CHECK(EvalUnitAttackMove(in) == AttackMoveDecision::kAttack);
}

// ===========================================================================
// ProcessShotAndBomb  (0x4bfb68)
// ===========================================================================
TEST(CombatSlots4_ShotBomb, Disarmed) {
    ShotBombInput in; in.armed = false; in.shotRequested = true; in.bombRequested = true;
    auto o = ProcessShotAndBomb(in);
    CHECK(o.path == ShotBombPath::kDisarmed);
    CHECK_EQ(o.statusWord, 0);
    CHECK(!o.playedShotSound);
}

TEST(CombatSlots4_ShotBomb, ArmedNothing) {
    ShotBombInput in; in.armed = true;
    auto o = ProcessShotAndBomb(in);
    CHECK(o.path == ShotBombPath::kNone);
    CHECK_EQ(o.statusWord, 33);
}

TEST(CombatSlots4_ShotBomb, ShotOnlyWithMelee) {
    ShotBombInput in; in.armed = true; in.shotRequested = true; in.hasPendingHit = true;
    auto o = ProcessShotAndBomb(in);
    CHECK(o.path == ShotBombPath::kShotOnly);
    CHECK(o.playedShotSound);
    CHECK(o.appliedMeleeHit);
    CHECK(!o.placedBomb);
}

TEST(CombatSlots4_ShotBomb, ShotThenBomb) {
    ShotBombInput in; in.armed = true; in.shotRequested = true; in.bombRequested = true;
    auto o = ProcessShotAndBomb(in);
    CHECK(o.path == ShotBombPath::kShotThenBomb);
    CHECK(o.playedShotSound);
    CHECK(o.placedBomb);
    CHECK(o.clearedMouseMask);
}

TEST(CombatSlots4_ShotBomb, BombOnly) {
    ShotBombInput in; in.armed = true; in.bombRequested = true;
    auto o = ProcessShotAndBomb(in);
    CHECK(o.path == ShotBombPath::kBombOnly);
    CHECK(!o.playedShotSound);
    CHECK(o.placedBomb);
    CHECK(o.clearedMouseMask);
}

// ===========================================================================
// UpdatePursuitTargets  (0x48c400) — decisions
// ===========================================================================
TEST(CombatSlots4_Pursuit, AttackThreshold) {
    CHECK(PursuitPressesAttack(20, 99));   // >= 20 -> attack regardless of roll
    CHECK(PursuitPressesAttack(100, 100));
    CHECK(PursuitPressesAttack(0, 30));    // roll 30 <= 30 -> attack
    CHECK(PursuitPressesAttack(19, 0));    // low ratio but roll 0 <= 30
    CHECK(!PursuitPressesAttack(19, 31));  // below both -> flee
    CHECK(!PursuitPressesAttack(0, 100));
}

TEST(CombatSlots4_Pursuit, RecordClear) {
    // duelMode <= 1 -> always clear.
    CHECK(PursuitRecordShouldClear(1, 2, true));
    CHECK(PursuitRecordShouldClear(0, 3, true));
    // duelMode > 1: states 3/4 kept; state 2 with live target kept; else cleared.
    CHECK(!PursuitRecordShouldClear(2, 3, false));
    CHECK(!PursuitRecordShouldClear(2, 4, false));
    CHECK(!PursuitRecordShouldClear(2, 2, true));
    CHECK(PursuitRecordShouldClear(2, 2, false));   // state 2, target dead
    CHECK(PursuitRecordShouldClear(2, 0, true));    // some other state
}

// ===========================================================================
// ResolveTargetEntityRef  (0x57ea8c)
// ===========================================================================
TEST(CombatSlots4_Resolve, EventPickStampsOwner) {
    ResolveTargetInput in;
    in.flags = 0x1 | 0x2;        // bit0 event-pick, bit1 stamp
    in.pickedEventId = 42;
    auto o = ResolveTargetEntityRef(in);
    CHECK_EQ(o.entityId, 42);
    CHECK(o.stampedOwnerBytes);
    CHECK(!o.spawnedAtEntrance);
}

TEST(CombatSlots4_Resolve, EventPickNoStampWithoutBit1) {
    ResolveTargetInput in;
    in.flags = 0x1;              // bit0 only
    in.pickedEventId = 5;
    auto o = ResolveTargetEntityRef(in);
    CHECK_EQ(o.entityId, 5);
    CHECK(!o.stampedOwnerBytes);
}

TEST(CombatSlots4_Resolve, EventPickFailsReturnsNull) {
    ResolveTargetInput in;
    in.flags = 0x1 | 0x2;
    in.pickedEventId = -1;       // 0xFFFF
    auto o = ResolveTargetEntityRef(in);
    CHECK_EQ(o.entityId, -1);
    CHECK(!o.stampedOwnerBytes);
}

TEST(CombatSlots4_Resolve, PersonSpawnPath) {
    ResolveTargetInput in;
    in.flags = 0x0;              // bit0 clear -> spawn person
    in.spawnedPersonId = 9;
    auto o = ResolveTargetEntityRef(in);
    CHECK_EQ(o.entityId, 9);
}

TEST(CombatSlots4_Resolve, EntranceSpawnGate) {
    ResolveTargetInput in;
    in.flags = 0x0 | 0x4;        // spawn + bit2 entrance
    in.spawnedPersonId = 3;
    in.hasBuildingCtx = true;
    in.resolveObjektOk = true;
    auto o = ResolveTargetEntityRef(in);
    CHECK_EQ(o.entityId, 3);
    CHECK(o.spawnedAtEntrance);
    // without a building context -> no entrance spawn.
    in.hasBuildingCtx = false;
    auto o2 = ResolveTargetEntityRef(in);
    CHECK(!o2.spawnedAtEntrance);
    // with ctx but ResolveTargetObjekt fails -> no entrance spawn.
    in.hasBuildingCtx = true; in.resolveObjektOk = false;
    auto o3 = ResolveTargetEntityRef(in);
    CHECK(!o3.spawnedAtEntrance);
}

// ===========================================================================
// BloodPoolYOffset  (0x48c708 / 0x4a4944)
// ===========================================================================
TEST(CombatSlots4_Blood, AngleRadiansGolden) {
    // y = roll * pi/180 (deg->rad). golden: blood(0)=0, blood(180)=pi, blood(360)=2pi.
    CHECK(feq(BloodPoolYOffset(0, kDeathBloodScaleA, kDeathBloodScaleB), 0.0f));
    CHECK(feq(BloodPoolYOffset(180, kDeathBloodScaleA, kDeathBloodScaleB), 3.1415927410125732f));
    CHECK(feq(BloodPoolYOffset(360, kDeathBloodScaleA, kDeathBloodScaleB), 6.2831854820251465f));
    CHECK(feq(BloodPoolYOffset(90, kDeathBloodScaleA, kDeathBloodScaleB), 1.5707963705062866f));
    // SpawnBloodPool uses identical constants -> identical result.
    CHECK(feq(BloodPoolYOffset(359, kBloodScaleA, kBloodScaleB), 6.265732288360596f));
}
