// Verifies InstallRealCharRender2Wiring() binds the one reconstructed pure-logic leaf
// (VIBE_Path_ConvertBackslashToSlash @0x44eb54) into CharRender3Hooks, seeding the rest of
// the table from the module's inert defaults (the call sites invoke the hook without a
// null-check, so every unbound field must stay a non-null safe stub). The sibling
// CharRender2 / CharMesh / CharQuery bridges have no bindable pure-logic field and are left
// inert (rule 3) — not exercised here. Suite prefix: WireCharRender2.
#include "tests/framework/test.h"

#include "sim/wire_charrender2.h"
#include "sim/character_render3.h"  // CharRender3Hooks / Set|GetCharRender3Hooks

#include <cstring>

using namespace guild;
using namespace guild::sim;

TEST(WireCharRender2, BindsConvertBackslashAndSeedsRemainingFields) {
    // Re-inert so a clean baseline holds before install (GetCharRender3Hooks() then returns
    // the module's inert default table).
    SetCharRender3Hooks(nullptr);

    InstallRealCharRender2Wiring();

    const CharRender3Hooks& h = GetCharRender3Hooks();
    // The one reconstructed leaf is bound.
    CHECK(h.convertBackslashToSlash != nullptr);
    // SEED-FROM-DEFAULTS: every other field is kept as its non-null inert default (the call
    // sites dereference these unconditionally — a zero-init table would crash). Spot-check a
    // representative spread across the table.
    CHECK(h.findFreeMeshSlot     != nullptr);
    CHECK(h.setGrayColorThunk    != nullptr);
    CHECK(h.loadStreamToStock    != nullptr);
    CHECK(h.attachToBone         != nullptr);
    CHECK(h.createMorphAnim      != nullptr);
    CHECK(h.attachToUniverseNode != nullptr);
    CHECK(h.buildLightCache      != nullptr);
    CHECK(h.switchUniverse       != nullptr);
    CHECK(h.soundPlaySample      != nullptr);
    CHECK(h.reportError          != nullptr);

    SetCharRender3Hooks(nullptr);  // restore for any later test in this TU
}

// The bound convertBackslashToSlash actually performs the real 1:1 transform (in-place
// '\\'(92) -> '/'(47), byte-faithful) over the installed hook — i.e. the wiring routes to
// the genuine reconstructed VIBE_Path_ConvertBackslashToSlash, not an inert no-op.
TEST(WireCharRender2, ConvertBackslashRunsRealTransform) {
    SetCharRender3Hooks(nullptr);
    InstallRealCharRender2Wiring();

    const CharRender3Hooks& h = GetCharRender3Hooks();

    char buf[] = "character\\bauer\\bauer_idle.baf";
    h.convertBackslashToSlash(buf);
    CHECK(std::strcmp(buf, "character/bauer/bauer_idle.baf") == 0);

    // No backslashes -> unchanged.
    char buf2[] = "already/forward/slashed";
    h.convertBackslashToSlash(buf2);
    CHECK(std::strcmp(buf2, "already/forward/slashed") == 0);

    // Empty string -> no-op, no crash.
    char buf3[] = "";
    h.convertBackslashToSlash(buf3);
    CHECK(buf3[0] == '\0');

    SetCharRender3Hooks(nullptr);
}
