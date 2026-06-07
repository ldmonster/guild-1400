// E2E flow for guild::sim combat_slots4: a single "a unit takes a fatal hit, the
// damage number floats up and expires, the killer's pursuit decision is rolled, the
// dying unit's escape roll is checked, blood pools spawn, and a follow-up target is
// resolved" — exercising the batch-4 leaves together through the installable hooks.
#include "sim/combat_slots4.h"
#include "sim/combat.h"
#include "crt/rand.h"

#include "test.h"

#include <cmath>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct World {
    std::vector<DmgNumberRecord> dmg;
    int bloodSpawned = 0;
    float lastBloodY = 0.0f;
    int labels = 0, destroys = 0, shows = 0, hides = 0;
    bool onScreen = true;
    std::string lastText;
    int deltaFlag = 0;
    int resetHi = 0;
    World() : dmg(kDmgNum4Count) {}
};
World* g_w = nullptr;

bool E_screen(const void*, int* x, int* y) {
    if (!g_w->onScreen) return false;
    *x = 320; *y = 240; return true;
}
int  E_label(int, int, const char* t) { g_w->labels++; g_w->lastText = t; return 1000 + g_w->labels; }
void E_visible(int, int v) { if (v) g_w->shows++; else g_w->hides++; }
void E_destroy(int) { g_w->destroys++; }
int  E_blood(const void*, float y) { g_w->bloodSpawned++; g_w->lastBloodY = y; return 5000; }
void E_reset(const void*) { g_w->resetHi++; }
void E_delta(const void*, i8 v) { g_w->deltaFlag = v; }

} // namespace

TEST(CombatSlots4_E2E, FatalHitFloatingNumberToExpiry) {
    World w; g_w = &w;
    CombatSlots4Hooks h{};
    h.objectScreenBounds = E_screen;
    h.createTextLabel = E_label;
    h.objectSetVisible = E_visible;
    h.widgetDestroyByType = E_destroy;
    h.spawnBloodPoolMesh = E_blood;
    h.resetObjectHighlights = E_reset;
    h.queueEscapeDelta = E_delta;
    SetCombatSlots4Hooks(&h);

    int victim = 0xBEEF;

    // 1) The killer attacks: a weaponed unit with a live target attacks.
    AttackMoveInput am;
    am.hasObjectDef = true; am.objDefType = 200; am.weaponClass = 1;
    am.hasActiveTarget = true; am.activeTargetHp = 12; am.selfBusy = false;
    CHECK(EvalUnitAttackMove(am) == AttackMoveDecision::kAttack);

    // 2) The hit lands -> spawn a damage number over the victim.
    //    amount 12, unitScale 1.0 -> value = (int)(12/0.01) = 1200.
    int slot = SpawnDamageNumber(w.dmg, &victim, 12, 0, 1.0f);
    CHECK_EQ(slot, 0);
    CHECK_EQ(w.dmg[0].value, 1200);
    CHECK(std::fabs(w.dmg[0].ttl - 64.0f) < 1e-6f);

    // 3) Several frames: the number is created, shown, and decays by 0.5/frame.
    //    From 64.0, 128 frames of -0.5 reach 0 -> expiry on the 128th.
    int totalExpired = 0;
    int frames = 0;
    while (w.dmg[0].value != 0 && frames < 200) {
        totalExpired += UpdateDamageNumbers(w.dmg, -0.5);
        ++frames;
    }
    CHECK_EQ(totalExpired, 1);
    CHECK_EQ(w.labels, 1);            // label created once
    CHECK_EQ(w.destroys, 1);          // destroyed exactly once at expiry
    CHECK_EQ(frames, 128);            // 64.0 / 0.5
    CHECK_EQ(w.lastText, std::string("-1200"));
    CHECK_EQ(w.dmg[0].widget, -1);
    CHECK(w.dmg[0].owner == nullptr);

    // 4) The victim is dead -> spawn a death blood pool. y = pi/180 * roll.
    crt::Srand(7);                      // deterministic roll source
    u32 roll = static_cast<u32>(static_cast<u16>(Math_RandomModulo(360)));
    float y = BloodPoolYOffset(roll, kDeathBloodScaleA, kDeathBloodScaleB);
    int bloodObj = h.spawnBloodPoolMesh(&victim, y);
    CHECK_EQ(bloodObj, 5000);
    CHECK_EQ(w.bloodSpawned, 1);
    CHECK(std::fabs(w.lastBloodY - y) < 1e-6f);

    // 5) Pursuit decision for the killer: low output ratio + a high roll -> flee.
    CHECK(!PursuitPressesAttack(5, 90));
    //    high ratio -> press the attack.
    CHECK(PursuitPressesAttack(40, 90));

    SetCombatSlots4Hooks(nullptr);
}

TEST(CombatSlots4_E2E, CowardFleesThenResolvesNewTarget) {
    World w; g_w = &w;
    CombatSlots4Hooks h{};
    h.resetObjectHighlights = E_reset;
    h.queueEscapeDelta = E_delta;
    SetCombatSlots4Hooks(&h);

    int unit = 1;

    // A cowardly unit (cowardice 9) under fire rolls escape; seed 1 -> roll 8 < 9.
    crt::Srand(1);
    EscapeInput esc;
    esc.canFlee = true; esc.cowardice = 9; esc.hasObjectDef = true;
    CHECK(TriggerEscapeAction(esc, &unit));
    CHECK_EQ(w.resetHi, 1);
    CHECK_EQ(w.deltaFlag, -9);

    // The fleeing unit triggers a replacement spawn at a building entrance.
    ResolveTargetInput rt;
    rt.flags = 0x0 | 0x4;          // spawn person + entrance
    rt.spawnedPersonId = 77;
    rt.hasBuildingCtx = true;
    rt.resolveObjektOk = true;
    auto out = ResolveTargetEntityRef(rt);
    CHECK_EQ(out.entityId, 77);
    CHECK(out.spawnedAtEntrance);

    SetCombatSlots4Hooks(nullptr);
}
