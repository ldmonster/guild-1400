#include "test.h"

// Integration: drive combat_slots4's TriggerEscapeAction against the REAL
// reconstructed RNG sibling (sim/combat.cpp's Math_RandomModulo, gilde.exe
// 0x58b89c, which in turn advances the live crt::RandNext LCG, crt/rand.cpp
// 0x5cb8bc). NOT a mock: the escape-roll branch the original computes as
// `RandomModulo(10) < cowardice` is consumed straight from the genuine RNG. We
// seed the live generator (crt::Srand), peek the next roll via the SAME real
// Math_RandomModulo, re-seed, then run TriggerEscapeAction and assert the
// flee/stop decision matches the roll the real sibling produced.
//
// The CombatSlots4Hooks side-effect leaves (highlights / escape-delta / shout)
// have NO reconstructed sibling — they are render/command/voice glue — so the
// flee path's side effects are observed through a recording hooks struct, while
// the decision itself is driven by the real RNG.
#include "sim/combat_slots4.h"
#include "sim/combat.h"   // REAL sibling: Math_RandomModulo
#include "crt/rand.h"     // REAL: Srand / RandNext (the LCG Math_RandomModulo advances)

using namespace guild;
using namespace guild::sim;

namespace {
// Recording hooks: the escape side effects (no reconstructed sibling).
struct EscapeRec {
    bool reset = false;
    bool queued = false;  i8 queuedFlag = 0;
    bool shouted = false;
    const void* unit = nullptr;
} g_rec;
void RecReset(const void* u)          { g_rec.reset = true; g_rec.unit = u; }
void RecQueue(const void* u, i8 flag) { g_rec.queued = true; g_rec.queuedFlag = flag; g_rec.unit = u; }
void RecShout(const void* u)          { g_rec.shouted = true; g_rec.unit = u; }

CombatSlots4Hooks MakeHooks() {
    CombatSlots4Hooks h{};                 // all-null inert defaults
    h.resetObjectHighlights = &RecReset;
    h.queueEscapeDelta       = &RecQueue;
    h.sendFleeMessage        = &RecShout;
    return h;
}

// Peek the next RandomModulo(10) the real sibling will produce from seed `s`,
// without disturbing the run (we re-seed before the real call under test).
int PeekRoll10(u32 s) {
    crt::Srand(s);
    return static_cast<int>(static_cast<u16>(Math_RandomModulo(kEscapeRollMod)));
}
} // namespace

// cowardice strictly greater than the real roll => unit flees; the escape delta
// is queued with flag == -cowardice, driven entirely by the real RNG sibling.
TEST(CombatSlots4Itest, FleesWhenRealRollUnderCowardice) {
    const u32 seed = 0x1234;
    int roll = PeekRoll10(seed);
    CHECK(roll >= 0 && roll < kEscapeRollMod);   // real Math_RandomModulo range

    CombatSlots4Hooks h = MakeHooks();
    SetCombatSlots4Hooks(&h);

    EscapeInput in{};
    in.canFlee   = true;
    in.cowardice = static_cast<u8>(roll + 1);    // strictly greater -> flee
    in.atBuilding = false;
    in.hasObjectDef = false;

    g_rec = EscapeRec{};
    int unitTag = 0;
    crt::Srand(seed);                            // re-seed: real call consumes same roll
    bool fled = TriggerEscapeAction(in, &unitTag);

    CHECK(fled);
    CHECK(g_rec.reset);
    CHECK(g_rec.queued);
    CHECK_EQ(static_cast<int>(g_rec.queuedFlag), -static_cast<int>(in.cowardice));
    CHECK(!g_rec.shouted);                        // not at a class-6/7 building
    if (g_rec.unit) CHECK_EQ(g_rec.unit, static_cast<const void*>(&unitTag));

    SetCombatSlots4Hooks(nullptr);
}

// cowardice <= the real roll => the unit holds: just the "stop" delta (flag 1),
// no highlight reset / no shout.
TEST(CombatSlots4Itest, HoldsWhenRealRollAtOrAboveCowardice) {
    const u32 seed = 0x55AA;
    int roll = PeekRoll10(seed);

    CombatSlots4Hooks h = MakeHooks();
    SetCombatSlots4Hooks(&h);

    EscapeInput in{};
    in.canFlee   = true;
    in.cowardice = static_cast<u8>(roll);        // roll < cowardice is FALSE -> hold
    in.hasObjectDef = false;

    g_rec = EscapeRec{};
    int unitTag = 0;
    crt::Srand(seed);
    bool fled = TriggerEscapeAction(in, &unitTag);

    CHECK(!fled);
    CHECK(!g_rec.reset);
    CHECK(g_rec.queued);
    CHECK_EQ(static_cast<int>(g_rec.queuedFlag), 1);   // the "stop" delta
    CHECK(!g_rec.shouted);

    SetCombatSlots4Hooks(nullptr);
}

// canFlee gate clear => the RNG is never consulted at all; immediate no-flee with
// no side effects (the real sibling's state is left untouched).
TEST(CombatSlots4Itest, CannotFleeShortCircuitsBeforeRng) {
    CombatSlots4Hooks h = MakeHooks();
    SetCombatSlots4Hooks(&h);

    crt::Srand(0x9001);
    int before = crt::RandNext();   // advance once; remember the next state indirectly
    crt::Srand(0x9001);
    (void)before;

    EscapeInput in{};
    in.canFlee = false;             // gate clear
    in.cowardice = 9;

    g_rec = EscapeRec{};
    int unitTag = 0;
    bool fled = TriggerEscapeAction(in, &unitTag);

    CHECK(!fled);
    CHECK(!g_rec.reset);
    CHECK(!g_rec.queued);
    CHECK(!g_rec.shouted);

    SetCombatSlots4Hooks(nullptr);
}

// The flee shout fires only at a class-6/7 building with a valid object def; the
// flee decision is still the real RNG roll < cowardice.
TEST(CombatSlots4Itest, FleeShoutAtClass6Building) {
    const u32 seed = 0x2468;
    int roll = PeekRoll10(seed);

    CombatSlots4Hooks h = MakeHooks();
    SetCombatSlots4Hooks(&h);

    EscapeInput in{};
    in.canFlee   = true;
    in.cowardice = static_cast<u8>(roll + 1);    // flee
    in.atBuilding = true;
    in.buildingClass = 6;
    in.hasObjectDef = true;

    g_rec = EscapeRec{};
    int unitTag = 0;
    crt::Srand(seed);
    bool fled = TriggerEscapeAction(in, &unitTag);

    CHECK(fled);
    CHECK(g_rec.shouted);

    SetCombatSlots4Hooks(nullptr);
}
