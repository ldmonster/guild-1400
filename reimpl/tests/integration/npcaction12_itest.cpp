#include "test.h"

// Integration: drive npcaction12 against a REAL reconstructed sibling — the RNG.
// AssignWorkPlaceStep's NpcAction12Hooks.randomModulo slot is the live
// VIBE_Math_RandomModulo; we forward it straight into the real
// util::RandomModulo (src/util/math_random.cpp), which itself draws from the real
// crt::RandNext LCG (src/crt/rand.cpp) — exactly as the game wires it. We seed the
// CRT RNG (Srand) and assert the product slot the REAL RNG selects is the slot the
// emitted sell command (QueueRequest17) references. No mock RNG model.
#include "sim/npcaction12.h"
#include "sim/npcaction.h"
#include "sim/he.h"

#include "util/math_random.h"   // REAL reconstructed sibling
#include "crt/rand.h"           // REAL LCG behind it

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Rec { u8 b[600]; };
HeRecord* AsHe(Rec& r) { return reinterpret_cast<HeRecord*>(r.b); }

// The RNG hook -> real sibling (Math_RandomModulo over crt::RandNext).
u16 RealRandomModulo(u16 n) { return static_cast<u16>(util::RandomModulo(n)); }

// Synthetic scene: a person -> employer record -> a work object whose product
// iterator yields three distinct product ids. We capture the selected product id
// from the emitted QueueRequest17 'kind' field.
i32 g_personId = 0x101, g_employerId = 0x202;

struct ObjRec { i32 pad0; i32 id; };
ObjRec g_person{0, 0x101};
ObjRec g_employer{0, 0x202};
ObjRec g_workObj{0, 0x303};
ObjRec g_prodRecs[3] = {{0, 7001}, {0, 7002}, {0, 7003}};

int g_iterIdx = 0;

i32 ObjId(void* rec) { return reinterpret_cast<ObjRec*>(rec)->id; }

struct Capture {
    bool emitted = false;
    int kind = -1;   // the product id passed to QueueRequest17
} g_cap;

} // namespace

TEST(NpcAction12Itest, ProductSlotChosenByRealRng) {
    g_cap = Capture{};
    g_iterIdx = 0;

    NpcAction12Hooks h{};
    // person / employer resolution
    h.findPersonById = [](i32 id) -> void* { return id == g_personId ? &g_person : nullptr; };
    h.resolveEntity = [](i32 id, void** out) {
        *out = (id == g_employerId) ? static_cast<void*>(&g_employer) : nullptr;
    };
    // field(+364) on the person record returns the employer link id.
    h.field = [](void* rec, int off) -> i32 {
        if (rec == &g_person && off == 364) return g_employerId;
        return 0;
    };
    h.objId = &ObjId;
    h.markerWord = [](void*) -> u16 { return 0; };
    // relation gate: -100 (<= -26) so the action proceeds.
    h.relationLookup = [](u16, u16) -> i32 { return -100; };
    // work object query (kind 42 hit).
    h.gameObjectQueryFind = [](i32, int, int, int, int d) -> void* {
        return d == 42 ? static_cast<void*>(&g_workObj) : nullptr;
    };
    h.buildingFindWorkProduct = [](void*) -> void* { static ObjRec wp{0, 0x404}; return &wp; };
    // product iterator: three products.
    h.gameObjectIterFirst = [](i32, int, int) -> void* { g_iterIdx = 0; return &g_prodRecs[0]; };
    h.gameObjectIterNext = []() -> void* {
        ++g_iterIdx;
        return g_iterIdx < 3 ? static_cast<void*>(&g_prodRecs[g_iterIdx]) : nullptr;
    };
    h.lookupMarketPrice = [](i16, u8) -> double { return 12.0; };
    h.cityId = [](u16) -> i32 { return 1; };
    h.sendQuickjump = [](i32, int, i32, i32, const char*) {};
    h.freeHandlerEntry = [](HeRecord*) -> i32 { return 0; };
    // THE WIRING: randomModulo -> real util::RandomModulo (over crt::RandNext).
    h.randomModulo = &RealRandomModulo;
    h.request17 = [](i32, i32, int, int kind, u8, int) { g_cap.emitted = true; g_cap.kind = kind; };

    SetNpcAction12Hooks(&h);

    // Seed the REAL CRT RNG. RandomModulo(3) with seed 12345 draws 21468 % 3 == 0,
    // so the FIRST product (id 7001) is selected and reaches the sell command.
    crt::Srand(12345);

    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = 0;     // state 0 -> the work path
    *reinterpret_cast<i32*>(r.b + 172) = g_personId;
    NpcAction12_AssignWorkPlaceStep(AsHe(r));

    CHECK(g_cap.emitted);
    if (g_cap.emitted) {
        // The product id the real RNG selected (slot 0 -> 7001).
        CHECK_EQ(g_cap.kind, (int)(i16)7001);
    }

    // Cross-check the real sibling directly with the same seed.
    crt::Srand(12345);
    CHECK_EQ((int)util::RandomModulo(3), 0);
    crt::Srand(12345);
    CHECK_EQ((int)util::RandomModulo(0x300), 732);  // matches the python LCG oracle

    SetNpcAction12Hooks(nullptr);
}
