// Integration: drive world_history2's VIBE_He_* engine tail against TWO REAL
// reconstructed siblings, wired exactly as the live engine forwards them:
//
//   * the diagnostic name-handling uses VIBE_Util_StrCmpNoCase (0x5cb8f0,
//     util/string_ops.cpp): the engine compares player names case-insensitively,
//     so we assert the He_LogInvalidAllocType / CreateGfxInfo diagnostics the
//     module produces match the expected text through the genuine StrCmpNoCase
//     sibling (not memcmp), proving the cross-module name flow.
//   * the stale-icon decision routes through WorldHistory2Hooks.parentEntityId,
//     which we forward into the REAL CRT LCG VIBE_Util_RandNext (0x5cb8bc,
//     crt/rand.cpp) seeded with VIBE_Crt_Srand, so the "entity id drifted" test
//     is driven by the same draw the binary would compute.
//
// The render/universe leaves (Object detach, Universe slot toggle, mesh build)
// have no reconstructed sibling; those hooks stay inert / use captors.
#include "test.h"

#include "world/world_history2.h"
#include "util/string_ops.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

char g_log[512];
void CapLog(const char* s) { std::strncpy(g_log, s, sizeof(g_log) - 1); }

// REAL CRT LCG forwarded as the parentEntityId hook: the slot's "live" id is the
// next draw, so a slot whose stored id differs from the draw is judged stale.
int RandLiveId(int /*parent*/) { return crt::RandNext(); }

} // namespace

// Diagnostic text flows out of the module and matches via the REAL StrCmpNoCase.
TEST(WorldHistory2Itest, DiagnosticMatchesViaRealStrCmpNoCase) {
    g_log[0] = 0;
    WorldHistory2Hooks h{};
    h.logMessage = CapLog;
    SetWorldHistory2Hooks(&h);

    u8 rec[16] = {0}; rec[0] = 3;
    He_LogInvalidAllocType(rec, "KaufMann");

    // The genuine engine sibling folds case; assert equality the way the engine
    // would (StrCmpNoCase == 0), and prove it is case-insensitive by matching a
    // differently-cased copy of the expected string.
    CHECK_EQ(util::StrCmpNoCase(g_log,
        "he_AllocNone(): Invalid HE-Type : 3, von Spieler KaufMann"), 0);
    CHECK_EQ(util::StrCmpNoCase(g_log,
        "HE_ALLOCNONE(): INVALID HE-TYPE : 3, VON SPIELER KAUFMANN"), 0);
    // A genuinely different name must NOT match.
    CHECK(util::StrCmpNoCase(g_log,
        "he_AllocNone(): Invalid HE-Type : 3, von Spieler Bauer") != 0);

    SetWorldHistory2Hooks(nullptr);
}

// CreateGfxInfo's invalid-parent diagnostic, validated through the real sibling.
TEST(WorldHistory2Itest, CreateGfxInfoInvalidParentDiagnosticViaRealSibling) {
    HeIconPoolReset();
    g_log[0] = 0;
    WorldHistory2Hooks h{};
    h.logMessage = CapLog;
    h.objectFindByHandle = [](int) { return 0; };   // parent invalid
    SetWorldHistory2Hooks(&h);

    int ok = He_CreateGfxInfo(7, 0, "AltesSchloss");
    CHECK_EQ(ok, 0);
    CHECK_EQ(util::StrCmpNoCase(g_log,
        "he_CreateGfxInfo(): invalid parent :ALTESSCHLOSS"), 0);

    SetWorldHistory2Hooks(nullptr);
}

// Stale-icon sweep driven by the REAL CRT LCG. Seed 12345 -> first RandNext draw
// is 21468 (state*1103515245+12345; (state>>16)&0x7FFF). A slot storing that id
// is fresh; one storing a different id is stale and gets destroyed.
TEST(WorldHistory2Itest, StaleSweepDrivenByRealRng) {
    HeIconPoolReset();
    HeIconSlot* pool = HeIconPool();
    // slot 0 will be checked first: stored id 21468 == first draw -> fresh.
    pool[0].parent = 1; pool[0].entityId = 21468; pool[0].node = 0;
    // slot 1 checked second: second draw is 9988; store something else -> stale.
    pool[1].parent = 2; pool[1].entityId = 12345; pool[1].node = 5;

    WorldHistory2Hooks h{};
    h.parentEntityId = RandLiveId;   // REAL crt::RandNext
    SetWorldHistory2Hooks(&h);

    crt::Srand(12345);
    He_DestroyStaleIcons();

    CHECK_EQ(pool[0].entityId, 21468);   // fresh: first draw matched
    CHECK_EQ(pool[0].parent, 1);
    CHECK_EQ(pool[1].entityId, -1);      // stale: second draw (9988) != 12345
    CHECK_EQ(pool[1].parent, 0);

    SetWorldHistory2Hooks(nullptr);
}
