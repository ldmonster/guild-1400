#include "test.h"

// Integration: drive combat_slots5 against TWO real reconstructed siblings (NOT
// mocks), exactly as the live wiring would:
//
//  (A) The scene-graph string-match collector callbacks (EscapeTileCallback /
//      ConquerObjectCallback / WareObjectCallback) compare a scene object's name
//      against a fixed prefix using the engine's own string primitives. We feed
//      the SAME object names through the REAL util siblings (util::StrCmpNoCase,
//      gilde.exe 0x5cb8f0; util::StrncmpN, 0x5e9ee0) and assert the collector's
//      module-level predicate agrees with the real comparator on the exact match,
//      case-fold, and length boundaries — i.e. the cross-module decision the game
//      makes when walking the battle scene graph.
//
//  (B) FindNearestEnemyTarget's no-primary fallback picks a roster entry uniformly
//      via Math_RandomModulo. We wire the randomModulo hook straight into the REAL
//      sim::Math_RandomModulo (combat.cpp, 0x58b89c) which advances the live CRT
//      LCG (crt::RandNext / crt::Srand). We seed the generator, peek the index the
//      real sibling will produce, re-seed, and assert FindNearestEnemyTarget
//      returns the fallback entry the genuine RNG selects.
#include "sim/combat_slots5.h"
#include "sim/combat.h"          // REAL: Math_RandomModulo
#include "util/string_ops.h"     // REAL: StrCmpNoCase / StrncmpN
#include "crt/rand.h"            // REAL: Srand / RandNext (the LCG)

#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// (A) Scene-callback predicates vs the real util comparators.
// ---------------------------------------------------------------------------
TEST(CombatSlots5Itest, EscapePredicateAgreesWithRealStrCmpNoCase) {
    const char* names[] = {"sp_ESCAPE", "SP_escape", "sp_ESCAPEX", "sp_CONQUER", "wall"};
    for (const char* n : names) {
        bool real = (util::StrCmpNoCase(n, "sp_ESCAPE") == 0);
        CHECK_EQ(EscapeTileMatches(n), real);
    }
}

TEST(CombatSlots5Itest, ConquerPredicateAgreesWithRealStrncmpN) {
    const char* names[] = {"sp_CONQUER", "sp_CONQUER_42", "sp_CONQUE", "sp_conquer"};
    for (const char* n : names) {
        bool real = (util::StrncmpN(n, "sp_CONQUER", 10) == 0);
        CHECK_EQ(ConquerObjectMatches(n), real);
    }
}

TEST(CombatSlots5Itest, WarePredicateAgreesWithRealStrncmpN) {
    // The ware predicate is: prefix match (real StrncmpN over 5) AND distinct id.
    struct Case { const char* name; int cand; int held; };
    Case cs[] = {
        {"WARE_07", 7, 3},   // prefix ok, ids differ -> match
        {"WARE_07", 7, 7},   // prefix ok, same id     -> skip
        {"ITEM_07", 7, 3},   // wrong prefix           -> skip
    };
    for (const Case& c : cs) {
        bool prefixReal = (util::StrncmpN(c.name, "WARE_", 5) == 0);
        bool expect = prefixReal && (c.cand != c.held);
        CHECK_EQ(WareObjectMatches(c.name, c.cand, c.held), expect);
    }
}

// ---------------------------------------------------------------------------
// (B) FindNearestEnemyTarget fallback driven by the REAL Math_RandomModulo.
// ---------------------------------------------------------------------------
namespace {
// Forward the hook straight into the real sibling — same shape the live code uses.
int RealRandomModulo(int n) {
    return static_cast<int>(static_cast<u16>(Math_RandomModulo(static_cast<u16>(n))));
}
} // namespace

TEST(CombatSlots5Itest, FallbackPickUsesRealRng) {
    const u32 seed = 0xC0FFEE;
    std::vector<int> fallback = {11, 22, 33, 44, 55, 66, 77};
    const int n = static_cast<int>(fallback.size());

    // Peek the index the real RNG will produce from `seed`.
    crt::Srand(seed);
    int peekIdx = RealRandomModulo(n);
    CHECK(peekIdx >= 0 && peekIdx < n);
    int expected = fallback[static_cast<std::size_t>(peekIdx)];

    // Re-seed and run FindNearestEnemyTarget with no primary candidate so the
    // fallback path consumes the SAME real RNG draw.
    crt::Srand(seed);
    CombatSlots5Hooks h{};
    h.randomModulo = &RealRandomModulo;
    SetCombatSlots5Hooks(&h);

    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;       // forces the fallback branch
    int got = FindNearestEnemyTarget(none, true, self, fallback);

    CHECK_EQ(got, expected);
    SetCombatSlots5Hooks(nullptr);
}

// Sanity: the real generator is deterministic for a fixed seed (re-seeding gives
// the identical fallback pick), confirming the wiring is reproducible.
TEST(CombatSlots5Itest, FallbackIsDeterministicAcrossReseed) {
    const u32 seed = 0x1357;
    std::vector<int> fallback = {1, 2, 3, 4};
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;

    CombatSlots5Hooks h{};
    h.randomModulo = &RealRandomModulo;
    SetCombatSlots5Hooks(&h);

    crt::Srand(seed);
    int a = FindNearestEnemyTarget(none, true, self, fallback);
    crt::Srand(seed);
    int b = FindNearestEnemyTarget(none, true, self, fallback);
    CHECK_EQ(a, b);
    SetCombatSlots5Hooks(nullptr);
}
