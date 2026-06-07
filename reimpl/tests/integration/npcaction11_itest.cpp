// Integration: drive npcaction11's BeginUnequipObject launcher against a REAL
// reconstructed sibling — VIBE_Math_RandomModulo (util/math_random.cpp 0x58b89c)
// layered over the genuine CRT LCG VIBE_Util_RandNext (crt/rand.cpp 0x5cb8bc).
//
// This is the live wiring: in gilde.exe BeginUnequipObject schedules its action by
// Advancing the +82 appointment GameTime by `VIBE_Math_RandomModulo(10) + 10`
// minutes. Here the module's `randomModulo` hook forwards straight into the REAL
// util::RandomModulo (which itself pulls the next value from the REAL crt::RandNext
// generator), exactly as the running game does — no mock RNG. We seed the generator
// with crt::Srand and then assert the resulting appointment minute is the byte-exact
// product of the real LCG (state*1103515245+12345, bits 16..30), proving the
// cross-module RNG -> appointment-math flow end to end.
//
// Golden (computed by python over the real LCG with seed 12345):
//   RandNext() #1 = 21468 ; 21468 % 10 = 8 ; appointment = 0 + (8 + 10) = 18 min.
#include "test.h"

#include "sim/npcaction11.h"
#include "sim/npcaction.h"          // SetNpcClock
#include "sim/gametime.h"
#include "util/math_random.h"       // REAL VIBE_Math_RandomModulo
#include "crt/rand.h"               // REAL crt::RandNext / Srand

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct HeBuf {
    alignas(8) std::uint8_t bytes[512];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    void put32(int off, i32 v) { std::memcpy(bytes + off, &v, 4); }
};

// A trivial opaque building record (only objId is read).
struct Bldg { i32 id = 0x77; } g_bldg;

// --- the load-bearing hook: forward into the REAL RNG sibling ----------------
// BeginUnequipObject's randomModulo(10) draw goes through the genuine
// util::RandomModulo, which advances the real crt LCG.
u16 RealRandomModulo(u16 n) {
    return static_cast<u16>(guild::util::RandomModulo(n));
}

void* BuildingFindById(i32) { return &g_bldg; }
i32   ObjId(void* r)        { return r ? static_cast<Bldg*>(r)->id : 0; }

int g_args25 = 0; i32 g_a25id = -1; int g_a25b = -1;
void Args25(i32 id, int /*a*/, int b, int /*c*/, int /*d*/) {
    ++g_args25; g_a25id = id; g_a25b = b;
}

NpcAction11Hooks MakeHooks() {
    NpcAction11Hooks h{};
    h.randomModulo     = RealRandomModulo;   // <-- real sibling wiring
    h.buildingFindById = BuildingFindById;
    h.objId            = ObjId;
    h.requestArgs25    = Args25;
    return h;
}

} // namespace

TEST(NpcAction11Itest, BeginUnequipAdvancesByRealLcgModulo) {
    // Seed the REAL generator; first RandNext() == 21468 -> %10 == 8 -> +18 min.
    crt::Srand(12345);
    CHECK_EQ(static_cast<int>(guild::util::RandomModulo(10)), 8);  // pin the sibling
    // Re-seed so the launcher sees the same first draw.
    crt::Srand(12345);

    GameTime clk{}; clk.day = 5; clk.hour = 9; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);

    g_args25 = 0;
    NpcAction11Hooks h = MakeHooks();
    SetNpcAction11Hooks(&h);

    HeBuf he; he.put32(172, 0x77);
    NpcAction11_BeginUnequipObject(he.rec());

    // +82 advanced by (real-LCG 8) + 10 = 18 minutes from 09:00 -> 09:18.
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(st.day, 5);
    CHECK_EQ(static_cast<int>(st.hour), 9);
    CHECK_EQ(st.minute, 18);          // byte-exact product of the real generator

    // The unequip command still fires against the resolved building.
    CHECK_EQ(g_args25, 1);
    CHECK_EQ(g_a25id, 0x77);
    CHECK_EQ(g_a25b, 512);

    SetNpcAction11Hooks(nullptr);     // restore inert defaults
}

// A second draw advances the generator deterministically: the next RandNext()
// after the first is 9988 -> %10 == 8 again (9988 % 10 == 8). Drive two launches
// off ONE seeding and confirm the real LCG state carries across module calls.
TEST(NpcAction11Itest, ConsecutiveLaunchesConsumeRealGeneratorState) {
    crt::Srand(12345);                // RandNext stream: 21468, 9988, 22117, ...
    GameTime clk{}; clk.day = 0; clk.hour = 0; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);

    NpcAction11Hooks h = MakeHooks();
    SetNpcAction11Hooks(&h);

    HeBuf he1; he1.put32(172, 0x77);
    NpcAction11_BeginUnequipObject(he1.rec());
    GameTime s1; std::memcpy(&s1, he1.bytes + 82, sizeof s1);
    CHECK_EQ(s1.minute, 18);          // 21468 % 10 (=8) + 10

    HeBuf he2; he2.put32(172, 0x77);
    NpcAction11_BeginUnequipObject(he2.rec());
    GameTime s2; std::memcpy(&s2, he2.bytes + 82, sizeof s2);
    CHECK_EQ(s2.minute, 18);          // 9988 % 10 (=8) + 10  (generator advanced)

    SetNpcAction11Hooks(nullptr);
}
