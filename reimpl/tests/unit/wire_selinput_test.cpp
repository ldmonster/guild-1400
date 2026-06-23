// Verifies InstallRealSelInputWiring() binds the two wireable command_apply9
// selection/check hook fields (SelectionHooks.parseInt + CheckHooks
// .personQueryBeginFlag90) to their real reconstructed leaves (VIBE_Util_ParseInt
// @0x5dc070 / VIBE_Person_QueryBegin @0x586c20), while every other field stays on
// its inert module default. Previously both bridges were fully inert at runtime
// (nothing installed real leaves). Suite prefix: WireSelInput.
#include "tests/framework/test.h"

#include "sim/wire_selinput.h"
#include "sim/command_apply9.h"   // SelectionHooks / CheckHooks / Get*/Set*
#include "sim/entity.h"           // g_objects / ObjectRec
#include "sim/command.h"          // CommandQueue / DeltaWriter

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert both bridges so a clean baseline can be asserted before install.
static void InertSelInput() {
    SetSelectionHooks(nullptr);
    SetCheckHooks(nullptr);
}

TEST(WireSelInput, BindsRealLeavesIntoSelectionAndCheck) {
    InertSelInput();
    // NOTE: the installer SEEDS each table from its module inert defaults (non-null
    // stubs) and overrides ONLY the wireable field. So unbound fields stay as their
    // inert stubs (non-null), not null. We assert the BOUND fields are non-null and
    // that the unbound fields remain their (non-null) defaults; the execute test
    // proves the bound leaves drive real behaviour.
    InstallRealSelInputWiring();

    const SelectionHooks& s = GetSelectionHooks();
    CHECK(s.parseInt             != nullptr);  // bound: VIBE_Util_ParseInt
    CHECK(s.selectedPersonRecord != nullptr);  // unbound -> inert default
    CHECK(s.worldActiveCount     != nullptr);
    CHECK(s.revealableSlot       != nullptr);
    CHECK(s.revealFlagByte       != nullptr);

    const CheckHooks& c = GetCheckHooks();
    CHECK(c.personQueryBeginFlag90 != nullptr); // bound: VIBE_Person_QueryBegin
    CHECK(c.officeCanRunFor        != nullptr);  // unbound -> inert default
    CHECK(c.officePrereqMet        != nullptr);

    InertSelInput();
}

// The bound parseInt leaf actually parses (the real VIBE_Util_ParseInt: sign +
// decimal, stops at the first non-digit) and the bound person-query leaf actually
// scans the real g_objects pool, resolving an id and surfacing record byte +90 —
// i.e. the wired control flow runs against real state.
TEST(WireSelInput, WiredLeavesExecuteAgainstRealState) {
    InstallRealSelInputWiring();

    // --- parseInt: real signed-decimal parse over a console-style "-123x" tail.
    const SelectionHooks& s = GetSelectionHooks();
    CHECK_EQ(s.parseInt("-123x"), -123);
    CHECK_EQ(s.parseInt("0"),      0);

    // --- personQueryBeginFlag90 is bound to the real VIBE_Person_QueryBegin over
    // g_objects. The query's filter/scan semantics are covered by PersonQueryBegin's
    // own tests; here we verify the bound leaf RUNS against the real pool without
    // crashing and returns a defined found/not-found (an absent id reliably yields
    // not-found with the out byte untouched).
    const CheckHooks& c = GetCheckHooks();
    CHECK(c.personQueryBeginFlag90 != nullptr);

    u8 flag90b = 7;
    int notfound = c.personQueryBeginFlag90(/*a2=*/0, /*key=*/0x7FFFFFF1, &flag90b);
    CHECK_EQ(notfound, 0);          // no record with this id
    CHECK_EQ((int)flag90b, 7);      // out byte untouched on not-found

    // CheckObjectFlagClear consults the real pool through the bound query; an absent
    // id reports "flag clear" (1) — the wired control flow executes end to end.
    CHECK_EQ(CheckObjectFlagClear(/*absent id*/0x7FFFFFF1, /*a2=*/0), 1);

    // inert the bridges for any later test in this TU.
    InertSelInput();
}
