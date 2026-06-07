#include "test.h"

// Integration: drive building5's Bauplatz finders against the REAL reconstructed
// util siblings — NO mocks for the string / vector / transform leaves:
//   util::StrCmpNoCaseN          (string_ops.cpp, 0x5e0db0)  prefix match
//   util::VectorWithinTolerance  (math.cpp,       0x5caa4c)  overlap test
//   util::PointThroughBoneChain  (transform.cpp,  0x5c8b38)  world position
//   sim::GameTimeAdvance         (gametime.cpp,   0x583150)  gate scheduling
//
// The plot finders call StrCmpNoCaseN / VectorWithinTolerance / PointThroughBoneChain
// DIRECTLY (live wiring); only the scene-walk + person iterator are hooked. The gate
// reset's time advance is forwarded into the genuine sim::GameTimeAdvance, exactly as
// the live engine wires it (the 14-byte gate block is a sim::GameTime record).

#include "sim/building5.h"
#include "sim/gametime.h"         // real sim::GameTimeAdvance
#include "sim/types.h"            // sim::GameTime layout
#include "util/string_ops.h"     // real util::StrCmpNoCaseN

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Node {
    std::uint8_t bytes[600];
    Node() { std::memset(bytes, 0, sizeof bytes); }
    void setName(const char* s) { std::strncpy(reinterpret_cast<char*>(bytes), s, 32); }
};

struct SceneHooks : Building5Hooks {
    const std::uint8_t* nodes[8] = {};
    int n = 0;
    const std::uint8_t* SceneNode(int index) override {
        return index < n ? nodes[index] : nullptr;
    }
};

// Forwards the gate time advance into the REAL sim::GameTimeAdvance over the
// building record's embedded 14-byte GameTime block.
struct RealTimeHooks : Building5Hooks {
    int forwarded = 0;
    std::int32_t GameTimeAdvance(std::uint8_t* timeRec, int d, int s, int m) override {
        ++forwarded;
        return sim::GameTimeAdvance(reinterpret_cast<sim::GameTime*>(timeRec), d, s, m);
    }
};

}  // namespace

// The plot filter's "bk_" acceptance is decided by the genuine StrCmpNoCaseN.
TEST(Building5IT, FilterUsesRealStrCmpNoCaseN) {
    SceneHooks h;
    SetBuilding5Hooks(&h);

    // Sanity: the real sibling agrees on the prefix decision the filter makes.
    CHECK_EQ(util::StrCmpNoCaseN("bk_007", kPlotPrefixBk, 3), 0);   // matches
    CHECK(util::StrCmpNoCaseN("house", kPlotPrefixBk, 3) != 0);     // differs

    Node bk, other;
    bk.setName("BK_007");        // case-insensitive match through the real fn
    other.setName("market");
    h.nodes[0] = bk.bytes; h.nodes[1] = other.bytes;
    h.n = 2;

    const std::uint8_t* frame[8] = {};
    std::int32_t count = Building_FilterBlockedBauplatze(0, frame, 8);
    CHECK_EQ(count, 1);                  // only the (case-folded) bk_ plot survives
    CHECK(frame[0] == bk.bytes);
    SetBuilding5Hooks(nullptr);
}

// FindNearestPlotByDistance + the overlap test run through the genuine
// PointThroughBoneChain / VectorWithinTolerance.
TEST(Building5IT, FindNearestUsesRealTransform) {
    SetBuilding5Hooks(nullptr);

    // Two plot frames, parent link (byte 504) zero -> world pos = f[19..21]+f[30..32].
    float a[128]; float b[128];
    std::memset(a, 0, sizeof a);
    std::memset(b, 0, sizeof b);
    a[20] = 5.0f;   a[31] = 5.0f;    // pos (0,10,0)
    b[31] = 80.0f;                   // pos (0,80,0)

    const std::uint8_t* plots[256] = {};
    plots[0] = reinterpret_cast<const std::uint8_t*>(a);
    plots[1] = reinterpret_cast<const std::uint8_t*>(b);

    float p[3] = {0.0f, 9.0f, 0.0f};
    const std::uint8_t* best = Building_FindNearestPlotByDistance(p, plots);
    CHECK(best == reinterpret_cast<const std::uint8_t*>(a));   // |10-9|=1 < |80-9|

    // The (0,80,0) plot is outside the 100-unit cutoff from far away.
    float far_[3] = {0.0f, 300.0f, 0.0f};
    CHECK(Building_FindNearestPlotByDistance(far_, plots) == nullptr);
}

// ResetGateState forwards into the real sim::GameTimeAdvance.
TEST(Building5IT, ResetGateState_RealGameTimeAdvance) {
    RealTimeHooks h;
    SetBuilding5Hooks(&h);

    std::uint8_t rec[256];
    std::memset(rec, 0, sizeof rec);
    // Source GameTime at +68: day 3, hour 23, minute 55, second 0.
    sim::GameTime src{};
    src.day = 3; src.hour = 23; src.minute = 55; src.second = 0;
    std::memcpy(rec + 68, &src, sizeof src);

    std::int32_t hour = Building_ResetGateState(rec);
    CHECK_EQ(h.forwarded, 1);

    // The real advance added 10 minutes to (23:55) -> 00:05 next day -> hour 0.
    sim::GameTime expect{};
    expect.day = 3; expect.hour = 23; expect.minute = 55; expect.second = 0;
    int expectHour = sim::GameTimeAdvance(&expect, 0, 0, 10);
    CHECK_EQ(hour, expectHour);
    CHECK_EQ((int)expect.hour, 0);   // wrapped to the next day's hour 0

    // And the copied block (+82) was the one advanced.
    sim::GameTime got{};
    std::memcpy(&got, rec + 82, sizeof got);
    CHECK_EQ((int)got.hour, (int)expect.hour);
    SetBuilding5Hooks(nullptr);
}
