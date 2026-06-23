// Golden-vector tests for world_buildingflag_util_recon2 — gilde.exe 0x583990 /
// 0x5b371c. Self-contained; uses tests/framework/test.h.
#include "test.h"
#include "sim/world_buildingflag_util_recon2.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Build a fake stride-65 type-record table and set the +0 kind byte for a type.
struct FakeTypeTable {
    u8 bytes[65 * 64] = {};
    void setKind(int type, u8 kind) { bytes[65 * type] = kind; }
    const u8* base() const { return bytes; }
};

}  // namespace

// The recovered static set: 01 03 04 0B 0D 0E 12 13 15 16 1A 1B 1C 1F 21 00.
TEST(Util2ReconWorldFlag, RecoveredSetBytes) {
    static const u8 expected[16] = {
        0x01, 0x03, 0x04, 0x0B, 0x0D, 0x0E, 0x12, 0x13,
        0x15, 0x16, 0x1A, 0x1B, 0x1C, 0x1F, 0x21, 0x00,
    };
    for (int i = 0; i < 16; ++i)
        CHECK_EQ((int)kBuildingKindSet[i], (int)expected[i]);
}

// A kind code present in the set returns that same code.
TEST(Util2ReconWorldFlag, MatchReturnsKind) {
    FakeTypeTable t;
    i16 type = 5;
    t.setKind(type, 0x12);  // 0x12 is in the set
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0x12);
}

// First and last real elements match.
TEST(Util2ReconWorldFlag, MatchFirstAndLast) {
    FakeTypeTable t;
    i16 type = 0;
    t.setKind(type, 0x01);
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0x01);
    t.setKind(type, 0x21);  // last before terminator
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0x21);
}

// A code NOT in the set returns 0.
TEST(Util2ReconWorldFlag, NoMatchReturnsZero) {
    FakeTypeTable t;
    i16 type = 7;
    t.setKind(type, 0x02);  // not in set
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0);
    t.setKind(type, 0x00);  // 0 never matches (set[0]!=0 but scan ends on term)
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0);
    t.setKind(type, 0x22);  // just past last real code
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0);
}

// set[0]==0 short-circuits to 0 regardless of kind.
TEST(Util2ReconWorldFlag, EmptySetReturnsZero) {
    FakeTypeTable t;
    i16 type = 3;
    t.setKind(type, 0x01);
    static const u8 emptySet[2] = {0x00, 0x00};
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base(), emptySet), 0);
}

// Signed-char comparison: a record byte of 0x80 (==-128 as signed char) does not
// match positive set entries; this also confirms the stride*type indexing.
TEST(Util2ReconWorldFlag, SignedCharAndStride) {
    FakeTypeTable t;
    i16 type = 10;
    t.setKind(type, 0x80);  // -128 signed; no positive set entry equals it
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0);
    // confirm other types untouched (stride correctness)
    t.setKind(type, 0x13);
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&type, t.base()), 0x13);
    i16 other = 11;
    CHECK_EQ((int)World_LookupBuildingTypeFlag(&other, t.base()), 0);
}

// ---------------------------------------------------------------------------
// Universe_ApplyHiddenToggle: control-flow / hook wiring.
namespace {
int g_walkCalls = 0;
u8  g_walkState = 0xFF;
int g_walkMode = -1;
int g_refreshCalls = 0;
unsigned g_refreshArg = 0;

u8 fakeToggle(u8 s) { return s; }
void fakeWalk(void* root, int zero, u8 (*cb)(u8), int mode, u8 s) {
    (void)root; (void)zero; (void)cb;
    ++g_walkCalls; g_walkMode = mode; g_walkState = s;
}
u8 fakeRefresh(unsigned flag) { ++g_refreshCalls; g_refreshArg = flag; return 7; }
}  // namespace

TEST(Util2ReconUniverseToggle, InertByDefault) {
    // With no hooks wired the leaf must be safe and return 0.
    SetUniverseHiddenToggleHooks(nullptr, nullptr, nullptr, nullptr);
    CHECK_EQ((int)Universe_ApplyHiddenToggle(1), 0);
}

TEST(Util2ReconUniverseToggle, WalksThenRefreshes) {
    g_walkCalls = g_refreshCalls = 0;
    SetUniverseHiddenToggleHooks(fakeWalk, nullptr, fakeToggle, fakeRefresh);
    u8 r = Universe_ApplyHiddenToggle(1);
    CHECK_EQ(g_walkCalls, 1);
    CHECK_EQ(g_walkMode, 6);          // original passes mode 6
    CHECK_EQ((int)g_walkState, 1);    // newState forwarded
    CHECK_EQ(g_refreshCalls, 1);
    CHECK_EQ((int)g_refreshArg, 1);   // VIBE_Light_RefreshAllObjects(1)
    CHECK_EQ((int)r, 7);              // returns the refresh result
    // reset to inert for other suites
    SetUniverseHiddenToggleHooks(nullptr, nullptr, nullptr, nullptr);
}
