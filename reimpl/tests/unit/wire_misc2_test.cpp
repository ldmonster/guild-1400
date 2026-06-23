// Verifies InstallRealMisc2Wiring() binds the one signature-compatible field of the
// misc2 hook-bridge cluster (PersonnelGuiHooks::randomModulo) to the real
// util::RandomModulo adapter, while seeding-from-defaults so every other field /
// zero-bindable bridge keeps its inert (non-null) module default. Suite prefix:
// WireMisc2. Headless; no main (shared test_main provides it).
#include "tests/framework/test.h"

#include "sim/wire_misc2.h"
#include "gui/personnel_gui.h"        // g_personnelGuiHooks / ResetPersonnelGuiHooks
#include "sim/eventtable_recon.h"     // zero-bindable bridge (default dummy handle)
#include "sim/object_lifecycle7.h"    // ObjLife7Hooks — backs EventTable's CloneOrFreeData
#include "util/math_random.h"

#include <cstdlib>
#include <map>

using namespace guild;

namespace {
// EventTableCreateEvent grows its handle table through ObjectCloneOrFreeData, which
// is backed by the ObjLife7 allocator hook (inert -> null -> crash). Provide a
// malloc-backed allocator so the headless append runs (mirrors eventtable_recon_test).
std::map<const void*, unsigned>& wm2sizes() { static std::map<const void*, unsigned> m; return m; }
void* Wm2Alloc(unsigned n) { void* p = std::malloc(n ? n : 1); wm2sizes()[p] = n; return p; }
unsigned Wm2Hdr(const void* p) { auto it = wm2sizes().find(p); return it == wm2sizes().end() ? 0 : it->second; }
void* Wm2Shrink(const void*) { return nullptr; }
void Wm2Free(const void* p) { wm2sizes().erase(p); std::free(const_cast<void*>(p)); }
void InstallWm2Allocator() {
    sim::ObjLife7Hooks h{};
    h.memAllocFromFreeList = &Wm2Alloc;
    h.memBlockHeaderClear  = &Wm2Hdr;
    h.memShrinkBlock       = &Wm2Shrink;
    h.memReturnToFreeList  = &Wm2Free;
    sim::ObjLife7SetHooks(h);
}
} // namespace

// After install, randomModulo is bound to the misc2 adapter, and every other
// PersonnelGuiHooks field keeps its non-null seeded default (the dialog bodies call
// hooks without null-checks, so a zero-init table would crash).
TEST(WireMisc2, BindsPersonnelRandomModuloAndPreservesDefaults) {
    gui::ResetPersonnelGuiHooks();
    // Capture the pure inert default for randomModulo (the module's DefRandomModulo).
    auto defaultRandomModulo = gui::g_personnelGuiHooks.randomModulo;

    sim::InstallRealMisc2Wiring();

    const gui::PersonnelGuiHooks& h = gui::g_personnelGuiHooks;

    // The bound field is non-null and is the misc2 adapter — distinct from the raw
    // module default (the installer intentionally routes it through wire_misc2).
    CHECK(h.randomModulo != nullptr);
    CHECK(h.randomModulo != defaultRandomModulo);

    // Seed-from-defaults: the un-bound fields still carry their non-null inert stubs.
    CHECK(h.gameTickFinalize       != nullptr);
    CHECK(h.formCenterChildWindows != nullptr);
    CHECK(h.formSelectWindow       != nullptr);
    CHECK(h.formDestroy            != nullptr);
    CHECK(h.formGetChildObjectId   != nullptr);
    CHECK(h.textRenderRichString   != nullptr);
    CHECK(h.gameLogicRunFrameLoop  != nullptr);
    CHECK(h.readLastClickedObject  != nullptr);
    CHECK(h.readCancelEdge         != nullptr);
    CHECK(h.queueHireRequest       != nullptr);
    CHECK(h.commandGetPacketStatus != nullptr);
    CHECK(h.refreshGuildState      != nullptr);
    CHECK(h.computeRecruitmentCost != nullptr);
    CHECK(h.sumCurrencyHeld        != nullptr);

    gui::ResetPersonnelGuiHooks();
}

// The bound randomModulo executes through the real util::RandomModulo: a draw in
// [0, n) for every modulus the bribe-bonus bracket uses (3, 4, 5). Proves the wired
// leaf actually runs and stays in range.
TEST(WireMisc2, BoundRandomModuloDrawsInRange) {
    gui::ResetPersonnelGuiHooks();
    sim::InstallRealMisc2Wiring();

    auto rnd = gui::g_personnelGuiHooks.randomModulo;
    CHECK(rnd != nullptr);
    for (int trial = 0; trial < 64; ++trial) {
        for (int n : {3, 4, 5}) {
            int v = rnd(n);
            CHECK(v >= 0);
            CHECK(v < n);
        }
    }

    gui::ResetPersonnelGuiHooks();
}

// The zero-bindable EventTable bridge is untouched by misc2: its createEvent default
// still hands back a non-null dummy handle, so the table-append bookkeeping runs 1:1
// even though nothing was installed (rule-6 inert CreateEventA).
TEST(WireMisc2, EventTableBridgeRemainsInertDummyHandle) {
    sim::SetEventTableHooks(nullptr);   // restore module inert defaults
    sim::EventTableReset();
    InstallWm2Allocator();              // back CloneOrFreeData so the append can grow

    sim::InstallRealMisc2Wiring();      // must not disturb the EventTable bridge

    const i32 before = sim::EventTableCount();
    sim::EventTableCreateEvent();
    CHECK_EQ(sim::EventTableCount(), before + 1);
    // The inert default createEvent returns a non-null dummy -> appended, not failed.
    CHECK(sim::EventTableHandleAt(before) != 0);

    sim::EventTableReset();
}
