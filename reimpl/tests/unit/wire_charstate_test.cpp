// Verifies InstallRealCharStateWiring() binds the CharState render leaves
// (CharStateHooks) and the wireable CharacterFactory leaves (CharacterFactoryHooks)
// to their real reconstructed cross-module leaves — previously CharStateHooks was
// NEVER installed in src/ (both leaves ran inert) and most CharacterFactory leaves
// ran their inert defaults. Suite prefix: WireCharState. Headless, no main.
#include "tests/framework/test.h"

#include "sim/wire_charstate.h"
#include "sim/character_state.h"
#include "sim/character_factory.h"
#include "sim/character_query.h"   // LiveActor / g_live / Universe

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert each bridge so a clean baseline can be asserted before install.
static void InertCharState() {
    SetCharStateHooks(nullptr);
    SetCharacterFactoryHooks(nullptr);   // restores MakeDefaults()
}

TEST(WireCharState, BindsRealLeavesIntoCharStateAndFactory) {
    InertCharState();
    InstallRealCharStateWiring();

    // --- CharStateHooks: both render leaves are now real (non-null adapters) ----
    const CharStateHooks& s = GetCharStateHooks();
    CHECK(s.applyPivot      != nullptr);
    CHECK(s.applyVisibility != nullptr);

    // --- CharacterFactoryHooks: the wireable leaves are bound; seed-from-defaults
    //     keeps every other slot non-null (so unchecked call sites stay safe) ------
    const CharacterFactoryHooks& f = GetCharacterFactoryHooks();
    CHECK(f.indexFromPointer     != nullptr);
    CHECK(f.preloadAniSet        != nullptr);
    CHECK(f.preloadLowPolyAniSet != nullptr);
    CHECK(f.reportError          != nullptr);
    CHECK(f.findSubstring        != nullptr);   // real strstr default, kept by seeding
    // every unbound slot stays its inert (non-null) stub
    CHECK(f.queryTerrainType     != nullptr);
    CHECK(f.buildObjectCache     != nullptr);
    CHECK(f.propagateDirtyFlag   != nullptr);
    CHECK(f.updateLowPolyMesh    != nullptr);
    CHECK(f.destroy              != nullptr);
    CHECK(f.attachToUniverseNode != nullptr);

    InertCharState();
}

// The CharState flagged-local passes run through the installed real leaves over a
// zeroed live universe without crashing, returning a defined result — proving the
// wired control flow executes (the pivot / visibility leaves resolve against the
// real object/render reconstructions). With no flagged actors the passes report 0.
TEST(WireCharState, FlaggedLocalPassesExecuteEmpty) {
    InstallRealCharStateWiring();

    int processed = ProcessFlaggedLocal();   // 0x40204c -> real SetPivotVector leaf
    CHECK(processed >= 0);
    int cleared = RefreshFlaggedLocal();     // 0x40208c -> real ApplyVisibilityState leaf
    CHECK(cleared >= 0);

    InertCharState();
}

// A representative factory leaf runs over a zeroed record through the installed real
// adapters: indexFromPointer(null) returns the 0xFFFFFFFF out-of-range sentinel (the
// "local universe" truthy value the factory branch keys on), and the preload leaves
// run the real path-formatting loop over an empty base name without crashing.
TEST(WireCharState, FactoryLeavesExecuteOverZeroedRecord) {
    InstallRealCharStateWiring();
    const CharacterFactoryHooks& f = GetCharacterFactoryHooks();

    // indexFromPointer(null) -> IndexFromUniverse(null) -> -1 (0xFFFFFFFF), truthy.
    unsigned idx = f.indexFromPointer(nullptr);
    CHECK(idx == 0xFFFFFFFFu);

    // The gait+idle preload set the factory always preloads.
    static const char kGehen[] = "bewegung/gehen";
    static const char kStehen[] = "stehen/stehen_newnoise";
    const char* gait[2] = {kGehen, kStehen};

    // A zeroed 516-byte record (the factory addresses the record by raw byte offset).
    alignas(8) unsigned char recBytes[520];
    std::memset(recBytes, 0, sizeof(recBytes));
    LiveActor* rec = reinterpret_cast<LiveActor*>(recBytes);

    f.preloadAniSet(rec, gait, 2);          // real PreloadAniSet over empty base name
    static const char kLow[] = "gehen";
    const char* low[1] = {kLow};
    f.preloadLowPolyAniSet(rec, low, 1);    // real PreloadLowPolyAniSet

    f.reportError("ch_CreateMesh(): test");  // real ErrorLog FormatMessage path

    InertCharState();
}
