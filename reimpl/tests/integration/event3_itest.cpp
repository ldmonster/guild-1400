// Integration test: VIBE_Event_RequestGuardInteraction (0x4ef7e8) wired against a
// REAL reconstructed sibling. When the resolved building's guard-state is 2, the
// event body computes a guard-target variant index by calling
// VIBE_BuildingType_ComputeVariantIndex(0x589cb0) with a RandomModulo(0x0C)+1 group
// argument. We forward event3's `buildingVariantIndex` hook into the genuine
// guild::sim::BuildingType_ComputeVariantIndex (NOT a mock) — exactly the live
// wiring — and assert the cross-module variant the binary would produce.
//
// The RandomModulo draws this body makes consume the REAL CRT LCG (guild::crt
// RandNext/Srand) via the module's direct guild::util::RandomModulo call, so the
// (group -> variant) result is fully determined by the seed and the real sibling.
#include "test.h"

#include "world/event3.h"
#include "sim/building_type.h"
#include "sim/npcaction.h"      // NpcClock / SetNpcClock (shared game clock)
#include "util/math_random.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
// A building record is opaque to event3 (resolved + queried via hooks); we only
// need identity + a guard-state. Size generously past the engine's 589-byte stride.
struct BuildingRec { unsigned char bytes[700] = {0}; };
BuildingRec g_building;

void* findBuilding(i32 id) { return id == 7 ? &g_building : nullptr; }
i32   guardStateSpawn(void*) { return 2; }   // selects the variant/enqueue branch

// The REAL sibling, bound exactly as the binary wires the hook: the event body
// passes (group = RandomModulo(0x0C)+1, mode = 1); ComputeVariantIndex takes
// (group, rank). We forward straight into the reconstructed function.
struct Capture {
    int variantArg = -1;   // the group the body computed and passed in
    int variantOut = -1;   // what the real sibling returned
    int enqueueKind = 0;
};
Capture* g_cap = nullptr;

i32 realBuildingVariantIndex(i32 group, i32 mode) {
    if (g_cap) g_cap->variantArg = group;
    int out = static_cast<int>(
        sim::BuildingType_ComputeVariantIndex(static_cast<u8>(group),
                                              static_cast<u8>(mode)));
    if (g_cap) g_cap->variantOut = out;
    return out;
}

i32 captureEnqueue(i32 a, i32 b, i32 kind, i32 d, i32 e, i32 variant,
                   i32 g, i32 h) {
    (void)a; (void)b; (void)d; (void)e; (void)g; (void)h;
    if (g_cap) g_cap->enqueueKind = kind;
    return variant;   // return the variant so RequestGuardInteraction stores it
}
}  // namespace

TEST(Event3Itest, GuardInteractionVariantFromRealBuildingTypeSibling) {
    Capture cap; g_cap = &cap;

    Event3Hooks h{};
    std::memset(&h, 0, sizeof(h));
    SetEvent3Hooks(nullptr);                 // start from inert defaults
    h = GetEvent3Hooks();                    // copy the inert table...
    h.findBuildingById = findBuilding;       // ...then override the few we need
    h.buildingGuardState = guardStateSpawn;
    h.buildingVariantIndex = realBuildingVariantIndex;   // REAL sibling
    h.enqueueObjectInteraction = captureEnqueue;
    SetEvent3Hooks(&h);

    // Deterministic clock + REAL CRT LCG seed (as a fresh session would set up).
    sim::GameTime clk{}; clk.day = 3; clk.hour = 9; clk.minute = 0; clk.second = 0;
    sim::SetNpcClock(clk);
    crt::Srand(12345);

    // He record: +180 holds the building id the body resolves.
    sim::HeRecord he{};
    std::memset(&he, 0, sizeof(he));
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&he) + 180) = 7;

    i32 stored = RequestGuardInteraction(&he);

    SetEvent3Hooks(nullptr);
    g_cap = nullptr;

    // The body drew RandomModulo(0x0C)+1 for the group; recompute against the same
    // REAL LCG sequence to know the exact group/variant the binary produced.
    crt::Srand(12345);
    int expectGroup = util::RandomModulo(0x0C) + 1;
    int expectVariant = static_cast<int>(
        sim::BuildingType_ComputeVariantIndex(static_cast<u8>(expectGroup), 1));

    // Cross-module flow assertions.
    CHECK(cap.variantArg == expectGroup);           // group fed to real sibling
    CHECK(cap.variantArg >= 1 && cap.variantArg <= 12);
    CHECK_EQ(cap.variantOut, expectVariant);        // real sibling output
    CHECK_EQ(cap.enqueueKind == 0, false);          // a kind in [22,26] was chosen
    CHECK(cap.enqueueKind >= 22 && cap.enqueueKind <= 26);
    // RequestGuardInteraction stores the enqueue result (== variant) into He+172.
    i32 he172 = *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&he) + 172);
    CHECK_EQ(stored, expectVariant);
    CHECK_EQ(he172, expectVariant);

    // The appointment block (+82) was stamped from the shared clock (day 3).
    i32 apptDay = *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&he) + 82);
    CHECK_EQ(apptDay, 3);
}

// Second cross-module assertion: when the building is missing the body never
// touches the variant sibling — it frees the handler instead (inert default free
// returns the record base as int). Proves the real sibling is only reached on the
// guard-state-2 path.
TEST(Event3Itest, MissingBuildingSkipsVariantSibling) {
    Capture cap; g_cap = &cap;

    SetEvent3Hooks(nullptr);
    Event3Hooks h = GetEvent3Hooks();
    h.findBuildingById = [](i32) -> void* { return nullptr; };  // no building
    h.buildingVariantIndex = realBuildingVariantIndex;
    SetEvent3Hooks(&h);

    sim::HeRecord he{};
    std::memset(&he, 0, sizeof(he));
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&he) + 180) = 999;

    i32 r = RequestGuardInteraction(&he);

    SetEvent3Hooks(nullptr);
    g_cap = nullptr;

    // The real sibling was never invoked (variantArg stays at its sentinel).
    CHECK_EQ(cap.variantArg, -1);
    // Inert freeHandlerEntry returns the record base as int -> non-zero, != stored.
    CHECK_EQ(r, static_cast<i32>(reinterpret_cast<std::intptr_t>(&he)));
}
