// Integration: drive charaction_steps6's InitSpionage espionage coroutine against
// a REAL reconstructed sibling — VIBE_Math_RandomModulo (util/math_random.cpp
// 0x58b89c), which itself draws from the real CRT LCG VIBE_Util_RandNext
// (crt/rand.cpp 0x5cb8bc). This is the live wiring: the coroutines never embed
// their own RNG; they seed the spy scan cursor (He +188) and the coprime stride
// (He +192) through the `randomModulo` hook, which in the running game is
// VIBE_Math_RandomModulo. Here we forward that hook straight into the genuine
// util::RandomModulo, seed the real generator deterministically with crt::Srand,
// and assert that the values InitSpionage stamps into the He record match the EXACT
// sequence the real RNG produces — i.e. the cross-module RNG flow is byte-faithful.
//
// findPersonById is a local spy stub (the spy-record resolve is a genuine
// cross-cluster leaf with no reconstructed sibling); queueRequestEntity29 is
// installed non-inert because InitSpionage calls it unconditionally on the
// fall-through. The load-bearing thing under test — the RNG draws — is the real one.
#include "test.h"

#include "sim/charaction_steps6.h"
#include "sim/npcaction.h"        // NpcLeafHooks / SetNpcLeafHooks
#include "util/math_random.h"     // REAL util::RandomModulo (0x58b89c)
#include "crt/rand.h"             // REAL crt::Srand / crt::RandNext (the LCG)

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A full He record buffer (the coroutine reaches past +232).
struct HeBuf {
    alignas(8) std::uint8_t bytes[1024];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
};

// A spy record the findPersonById leaf resolves (its +2 category byte is read).
HeBuf g_spy;
HeRecord* SpyById(i32 /*id*/) { return g_spy.rec(); }

// InitSpionage calls GetNpcLeafHooks().queueRequestEntity29 unconditionally on the
// LABEL_19 fall-through; install a recording non-inert stub so it does not deref a
// null inert default.
i32 g_lastQueueArg = -999;
i32 RecQueue29(int arg, HeRecord*) { g_lastQueueArg = arg; return 0xABCD; }

// Forward charaction_steps6's randomModulo hook into the REAL sibling.
int RealRandomModulo(int n) { return util::RandomModulo(static_cast<u16>(n)); }

// The stride table InitSpionage indexes (mirrors the module's private kSpyStrideTable).
const i32 kStride[16] = { 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 };

} // namespace

// InitSpionage seeds He+188 = randomModulo(256) and He+192 = stride[randomModulo(16)]
// from the REAL RNG. Seed the generator, predict the exact draws, then run the
// coroutine and confirm the stamped values match the real sequence.
TEST(CharactionSteps6Itest, InitSpionageSeedsFromRealRng) {
    // 1) Predict the exact draws the real RNG will produce from a known seed.
    crt::Srand(0x1357);
    int expectCursor = util::RandomModulo(0x100);          // first draw: % 256
    int expectStride = kStride[util::RandomModulo(0x10) & 0x0F]; // second: % 16
    // Pin that the real RNG is doing modular reduction (in range).
    CHECK(expectCursor >= 0 && expectCursor < 256);
    int strideIdxLow = -1;
    for (int i = 0; i < 16; ++i) if (kStride[i] == expectStride) strideIdxLow = i;
    CHECK(strideIdxLow >= 0);

    // 2) Re-seed identically and run the coroutine through the real hook.
    crt::Srand(0x1357);

    // Base on the library's complete inert defaults (the struct has no null-fill in
    // SetCharActionStep6Hooks; every member must be valid), then override two.
    SetCharActionStep6Hooks(nullptr);
    CharActionStep6Hooks h = GetCharActionStep6Hooks();
    h.findPersonById = SpyById;        // spy resolves (so the seed block runs)
    h.randomModulo   = RealRandomModulo;  // <-- real sibling wiring
    SetCharActionStep6Hooks(&h);

    NpcLeafHooks leaf{};
    leaf.queueRequestEntity29 = RecQueue29;
    SetNpcLeafHooks(&leaf);

    g_lastQueueArg = -999;
    HeBuf he;
    // flags+121 bit2 clear (not already spawned); spy id at +196.
    He_F196(he.rec()) = 4242;

    i32 ret = InitSpionage(he.rec());

    // The RNG-seeded fields match the real generator's exact draws.
    CHECK_EQ(He_F188(he.rec()), expectCursor);
    CHECK_EQ(He_F192(he.rec()), expectStride);
    // resolveEntityById/findNearestEntity are inert (no object) -> LABEL_19 arms a
    // -1 entity request through the real queue hook; ret echoes its handle.
    CHECK_EQ(g_lastQueueArg, -1);
    CHECK_EQ(ret, 0xABCD);

    SetCharActionStep6Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
}

// The "already spawned" guard (flags +121 bit 2 set) returns the existing request
// handle WITHOUT drawing from the RNG at all — confirm the generator is untouched.
TEST(CharactionSteps6Itest, InitSpionageAlreadySpawnedSkipsRng) {
    crt::Srand(0x99);
    u32 stateBefore = *crt::RandStatePtr();

    SetCharActionStep6Hooks(nullptr);
    CharActionStep6Hooks h = GetCharActionStep6Hooks();
    h.findPersonById = SpyById;
    h.randomModulo   = RealRandomModulo;
    SetCharActionStep6Hooks(&h);

    HeBuf he;
    he.bytes[121] = 0x04;              // (flags & 0x400) set -> already spawned
    He_ReqHandle(he.rec()) = 0x5151;  // +132 existing handle

    i32 ret = InitSpionage(he.rec());
    CHECK_EQ(ret, 0x5151);            // returns the existing handle
    CHECK_EQ(*crt::RandStatePtr(), stateBefore);  // RNG never advanced

    SetCharActionStep6Hooks(nullptr);
}
