// Verifies InstallRealReaperWiring() binds the reconstructed full Reaper functions
// into the live NpcEventHooks bridge (previously fully inert at runtime).
#include "tests/framework/test.h"

#include "sim/real_reaper_wiring.h"
#include "sim/npcevent_steps.h"        // NpcEventHooks / GetNpcEventHooks / SetNpcEventHooks
#include "sim/npcevent_reaper_full.h"  // ReaperMoveTowardTarget / ...
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

TEST(RealReaperWiring, BindsReaperLeavesIntoNpcEventHooks) {
    // Baseline: a fresh inert table leaves the reaper leaves null.
    SetNpcEventHooks(nullptr);
    CHECK(GetNpcEventHooks().reaperMove == nullptr);

    // After install, all four reaper leaves point at the 1:1 reconstructions.
    InstallRealReaperWiring();
    const NpcEventHooks& h = GetNpcEventHooks();
    CHECK(h.reaperApproach    == &ReaperApproachTarget);
    CHECK(h.reaperMove        == &ReaperMoveTowardTarget);
    CHECK(h.reaperCachePose   == &ReaperCacheTargetPose);
    CHECK(h.reaperUpdateSound == &ReaperUpdateSoundPos);

    // The other (unbound) fields stay null -> inert default preserved.
    CHECK(h.findPerson == nullptr);

    SetNpcEventHooks(nullptr);  // restore for any later test in this TU
}

// The wired reaper functions run over a (zeroed) He with inert sub-hooks without
// crashing, returning a defined progress code — i.e. real control flow executes.
TEST(RealReaperWiring, WiredReaperMoveExecutesOverInertSubhooks) {
    SetReaperFullHooks(nullptr);   // all reaper sub-callees inert
    InstallRealReaperWiring();

    u8 buf[320];
    std::memset(buf, 0, sizeof(buf));
    HeRecord* h = reinterpret_cast<HeRecord*>(buf);

    int code = GetNpcEventHooks().reaperMove(h);  // routes to ReaperMoveTowardTarget
    CHECK(code == 0 || code == 1 || code == 2);   // a defined reaper progress code

    SetNpcEventHooks(nullptr);
}
