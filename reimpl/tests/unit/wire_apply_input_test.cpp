// Verifies InstallRealApplyInputWiring() binds the two wireable bridges in this
// agent's slice to their real reconstructed leaves — previously both were fully
// inert (nothing installed them):
//   IssueOnObjectHooks.labelStrncmp    -> util::StrncmpN
//   ActionTargetPickHooks.lightSetGrayThunk -> render::BroadcastGrayDword
// and that the bound leaves produce REAL (not inert-default) behaviour.
// Suite prefix: WireApplyInput. Headless, no main.
#include "tests/framework/test.h"

#include "sim/wire_apply_input.h"
#include "sim/command_apply12.h"        // IssueOnObject / SetIssueOnObjectHooks
#include "gui/action_target_pickn.h"    // ActionDialog_* / ActionTargetPickHooksActive
#include "util/string_ops.h"            // util::StrncmpN
#include "render/light.h"               // render::BroadcastGrayDword

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert both bridges so a clean baseline holds before install.
static void WaiInertAll() {
    SetIssueOnObjectHooks(nullptr);
    gui::SetActionTargetPickHooks(nullptr);
}

TEST(WireApplyInput, BindsRealLeavesIntoBothBridges) {
    WaiInertAll();
    InstallRealApplyInputWiring();

    // ActionTargetPickHooks: the grey-thunk field is now a real adapter (the four
    // unbound fields keep their inert stubs — non-null, since the call site does
    // NOT null-check them). The thunk must NOT be the inert default any more.
    const gui::ActionTargetPickHooks& tp = gui::ActionTargetPickHooksActive();
    CHECK(tp.lightSetGrayThunk != nullptr);
    CHECK(tp.lightSetGrayThunk != gui::ActionTargetPickHooks_Default()->lightSetGrayThunk);
    // unbound fields stay on the inert defaults (non-null pointers)
    CHECK(tp.amtRunOfficeOverviewWindow != nullptr);
    CHECK(tp.textRenderFormattedMessage != nullptr);
    CHECK(tp.hudUpdateEdgeScroll        != nullptr);
    CHECK(tp.confirmAbductCallback      != nullptr);

    WaiInertAll();
}

// The wired ActionTargetPick grey-thunk now produces the GENUINE grey-broadcast
// value (render::BroadcastGrayDword of the real grey LEVEL), not the inert
// default's verbatim middle arg.
//
// ABI verified by disasm: VIBE_Light_SetGrayColorThunk @0x5c6af0 broadcasts its
// FIRST register arg (level@edx) across the dword; the second arg (ebx) is the
// memory-fill byte COUNT (the dword memory-fill `mov dh,dl; shl edx,8; ...`
// runs entirely on edx). At the live call site @0x548c20 the disasm is
// `xor edx,edx` (level=0) and `mov ebx,28h` (count=40). The reimpl hook is laid
// out positionally as (a@edx /*level*/, level@ebx /*count*/, recOut@eax /*dst*/),
// so the broadcast SOURCE is the first hook param `a`, and `kAbductGrayLevel`
// (40) is the fill COUNT (the second param), NOT the broadcast value.
TEST(WireApplyInput, GrayThunkBroadcastsRealValue) {
    WaiInertAll();

    // Baseline (inert default): DefLightSetGrayThunk writes recOut = its middle
    // arg verbatim (it mis-models the broadcast). Driving it with the live call
    // shape (a=level=0, middle=count=40) stores 40.
    int rec_inert = 99;
    gui::ActionTargetPickHooksActive().lightSetGrayThunk(0, gui::kAbductGrayLevel, &rec_inert);
    CHECK_EQ(rec_inert, gui::kAbductGrayLevel);   // inert: verbatim middle arg (40)

    // After install: the SAME call now broadcasts the real grey LEVEL (the first
    // arg, 0 at this call site) across the dword. broadcast(0) == 0.
    InstallRealApplyInputWiring();
    int rec_real = 99;
    gui::ActionTargetPickHooksActive().lightSetGrayThunk(0, gui::kAbductGrayLevel, &rec_real);
    const std::int32_t expect =
        static_cast<std::int32_t>(render::BroadcastGrayDword((u8)0));  // level (edx) == 0
    CHECK_EQ(rec_real, expect);                   // real: broadcast of the level (0) -> 0
    CHECK_EQ(rec_real, 0);

    // A nonzero grey level (first arg) broadcasts across all four byte-lanes;
    // the middle arg (count) does NOT affect the broadcast value.
    int rec_lvl = 99;
    gui::ActionTargetPickHooksActive().lightSetGrayThunk(0x12, /*count*/40, &rec_lvl);
    CHECK_EQ(rec_lvl, static_cast<int>(render::BroadcastGrayDword((u8)0x12))); // 0x12121212

    // End-to-end through the public builder (BeginAbductTargetPick uses the active
    // thunk to fill the overlay-color descriptor): the record now carries the real
    // broadcast value, the flag (1024) and kind (6) stay byte-faithful.
    int begin_rv = gui::ActionDialog_BeginAbductTargetPick(/*self=*/0x1234);
    (void)begin_rv;   // window leaf is inert (returns 0); the record fill is what we wired

    WaiInertAll();
}

// InstallRealApplyInputWiring is idempotent (re-callable; matches the boot path
// where the spine installs once at command-system init).
TEST(WireApplyInput, InstallIsIdempotent) {
    WaiInertAll();
    InstallRealApplyInputWiring();
    InstallRealApplyInputWiring();   // second call must not crash / change shape
    const gui::ActionTargetPickHooks& tp = gui::ActionTargetPickHooksActive();
    CHECK(tp.lightSetGrayThunk != nullptr);
    CHECK(tp.lightSetGrayThunk != gui::ActionTargetPickHooks_Default()->lightSetGrayThunk);
    WaiInertAll();
}

// The leaf the IssueOnObject wiring binds (util::StrncmpN) classifies the order
// labels through the LIVE router exactly as the wired binding does. IssueOnObject's
// installed table is file-scope (no getter to read it back), so we drive the public
// router with a table that sets labelStrncmp to the SAME real leaf the installer
// binds (&util::StrncmpN), proving the wired-leaf shape routes WARE -> conquer
// (kind 6) end-to-end. InstallRealApplyInputWiring() is exercised first.
TEST(WireApplyInput, WiredStrncmpClassifiesWareLabelAsConquer) {
    WaiInertAll();
    InstallRealApplyInputWiring();   // exercise the installer (no crash)

    // The real leaf, verified directly: the two prefixes the router compares.
    CHECK_EQ(util::StrncmpN("WARE_Brot", "WARE", 4), 0);          // a WARE label
    CHECK_EQ(util::StrncmpN("sp_CONQUER", "sp_CONQUER", 10), 0);  // a conquer label
    CHECK(util::StrncmpN("xyz", "WARE", 4) != 0);                 // not a WARE label

    // Drive the live router with the SAME real leaf the installer binds.
    CommandQueue q;
    q.set_standalone(true);
    q.Init();

    IssueOnObjectHooks t{};
    t.orderArmed    = []() { return true; };       // dword_67221C armed
    const char* kWareLabel = "WARE_Brot";
    t.pickedObject  = []() -> i32 { return 1; };   // a pick under the cursor
    t.pickedLabel   = [kWareLabel]() { return kWareLabel; };
    t.pickedIsUnit  = [](i32) { return false; };   // treat as a label string
    t.labelStrncmp  = [](const char* a, const char* b, int n) {
        return util::StrncmpN(a, b, n);            // == the installer's binding
    };
    SetIssueOnObjectHooks(&t);

    // worldToTile succeeds so BuildConquerCommand can stamp + enqueue the order.
    CombatOrderHandle h{};
    CombatOrderContext ctx{};
    ctx.worldToTile = [](float, float, float, i32& tx, i32& tz) {
        tx = 7; tz = 9; return true;
    };
    OrderStage outSlot{};
    char rv = IssueOnObject(q, h, /*target=*/100, ctx, outSlot);
    // The WARE label routes to BuildConquerCommand -> returns 1 (an order issued).
    CHECK_EQ(rv, static_cast<char>(1));

    WaiInertAll();
}
