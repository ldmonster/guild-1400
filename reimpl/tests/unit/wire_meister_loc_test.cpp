// Verifies InstallRealMeisterLocWiring() binds the MeisterAi command-emitter bridge
// (ai::Meister3Hooks), the LocationDialog4 dungeon-jailer gate
// (world::LocationDialog4Hooks), and the mission-name building-type lookup
// (gui::MissionNameHooks) to their real reconstructed leaves — previously all three
// were inert at runtime (nothing installed them). Suite prefix: WireMeisterLoc.
#include "tests/framework/test.h"

#include "world/wire_meister_loc.h"

#include "ai/meisterai3.h"          // Meister3Hooks / Set/GetMeister3Hooks, ApplyDrinkAction
#include "world/location4.h"        // LocationDialog4Hooks / Set/GetLocationDialog4Hooks
#include "world/office.h"           // OfficeHolder / g_officeHolders / OfficeHolderTableReset
#include "gui/mission_load_run.h"   // MissionNameHooks / Menu_SetMissionNameHooks, formatter

#include "sim/real_hooks.h"         // RealCommandQueue()
#include "sim/building2.h"          // Building_LookupTypeRecordA / TypeRecord (golden reference)

#include <cstring>

using namespace guild;

// ---------------------------------------------------------------------------
// (1) Binding contract: the wireable fields point at real adapters; unbound fields
//     keep their module inert (non-null for the func-ptr bridges) stubs.
// ---------------------------------------------------------------------------
TEST(WireMeisterLoc, BindsRealLeavesIntoAllThreeBridges) {
    // Re-seed the func-ptr bridges to their inert defaults so a clean baseline holds.
    ai::SetMeister3Hooks(ai::Meister3Hooks{});           // default-ctor restores inert stubs
    world::SetLocationDialog4Hooks(nullptr);             // null => module re-inerts on Get

    world::InstallRealMeisterLocWiring();

    // --- ai::Meister3Hooks: the two command emitters are now real -------------
    const ai::Meister3Hooks m3 = ai::GetMeister3Hooks();
    CHECK(m3.request_build_op90 != nullptr);
    CHECK(m3.queue_coord27      != nullptr);
    // unbound fields keep their inert (non-null) stubs:
    CHECK(m3.queue_slot_reset28      != nullptr);
    CHECK(m3.emit_group_state        != nullptr);
    CHECK(m3.update_handler_worldpos != nullptr);
    CHECK(m3.dispatch_handler        != nullptr);

    // --- world::LocationDialog4Hooks: the dungeon-jailer gate is real ---------
    const world::LocationDialog4Hooks& l4 = world::GetLocationDialog4Hooks();
    CHECK(l4.dungeonHasJailer != nullptr);
    // unbound (no clean target) stay inert (null is the module default here).

    // --- gui::MissionNameHooks installed (subclass) ---------------------------
    // Menu_SetMissionNameHooks returns the PREVIOUS pointer. After install the live
    // pointer must NOT be the module's default-constructed inert MissionNameHooks
    // (whose LookupBuildingTypeRecord returns 0); re-installing nullptr (a no-op the
    // setter still records) lets us read back the currently-bound pointer.
    gui::MissionNameHooks* live = gui::Menu_SetMissionNameHooks(nullptr);
    CHECK(live != nullptr);                       // a real subclass instance is bound
    CHECK(live->LookupBuildingTypeRecord(5, nullptr) == 6u);  // real builder (6-byte record)
    gui::Menu_SetMissionNameHooks(live);          // restore the wired hook
}

// ---------------------------------------------------------------------------
// (2) Execute: the wired MeisterAi emitter actually stages a packet onto the SHARED
//     real command queue.  ApplyDrinkAction emits RequestBuildOp90 (+ a slot-reset).
// ---------------------------------------------------------------------------
TEST(WireMeisterLoc, MeisterEmitStagesOntoRealQueue) {
    world::InstallRealMeisterLocWiring();
    sim::CommandQueue* q = sim::RealCommandQueue();
    CHECK(q != nullptr);

    const u32 before = q->send_count();
    // ApplyDrinkAction(actorBuildId, cost, slotId, slotKind) -> request_build_op90 fires.
    char code = ai::ApplyDrinkAction(/*actorBuildId*/ 7, /*cost*/ 10,
                                     /*slotId*/ 3, /*slotKind*/ 2);
    CHECK_EQ(code, 4);  // drink action code (faithful return)
    const u32 after = q->send_count();
    // At least the op90 packet must have been enqueued (slot_reset28 is inert).
    CHECK(after >= before + 1);
}

// ---------------------------------------------------------------------------
// (3) LocationDialog4 dungeon-jailer gate reads the real office table: false on an
//     empty table, true once a type-17 (jailer) holder is present.
// ---------------------------------------------------------------------------
TEST(WireMeisterLoc, DungeonJailerGateReadsRealOfficeTable) {
    world::InstallRealMeisterLocWiring();
    const world::LocationDialog4Hooks& l4 = world::GetLocationDialog4Hooks();
    CHECK(l4.dungeonHasJailer != nullptr);

    world::OfficeHolderTableReset();              // clear -> no jailer
    CHECK(l4.dungeonHasJailer() == false);

    // Install a jailer (office type 17) in slot 0 and re-probe.
    world::g_officeHolders[0].type = 17;
    CHECK(l4.dungeonHasJailer() == true);

    world::OfficeHolderTableReset();              // restore for later tests
}

// ---------------------------------------------------------------------------
// (4) MissionName lookup returns the real 6-byte type record (golden vs the direct
//     reconstructed Building_LookupTypeRecordA), driven through the live formatter.
// ---------------------------------------------------------------------------
TEST(WireMeisterLoc, MissionNameLookupCopiesRealTypeRecord) {
    world::InstallRealMeisterLocWiring();

    // Golden reference: the reconstructed builder for a known code.
    constexpr std::uint8_t kCode = 5;
    sim::TypeRecord golden{};
    sim::Building_LookupTypeRecordA(kCode, &golden);

    // Drive the formatter, which routes through the installed hook and writes the
    // 6-byte record into out.record / out.recordLen.
    gui::MissionNameScratch out{};
    // src strings are 2-byte-stride; a single-char source is enough to exercise it.
    const char src0[] = {'A', 0, 0};
    const char src1[] = {'B', 0, 0};
    unsigned len = gui::Menu_FormatMissionBuildingName(src0, src1, out);

    // The formatter looks up SHIBYTE(dword_122F4A0); dword_122F4A0 is seeded to
    // 856692811 (0x33113B0B) -> SHIBYTE == 0x33 == 51. Verify the bound lookup ran
    // and returned the real record length (6), and the bytes match the builder.
    CHECK_EQ(len, 6u);
    CHECK_EQ(out.recordLen, 6u);

    sim::TypeRecord ref{};
    sim::Building_LookupTypeRecordA(/*SHIBYTE(856692811)=*/ 0x33, &ref);
    // out.record holds the 6 little-endian bytes of `ref`.
    const auto* rb = reinterpret_cast<const unsigned char*>(out.record);
    CHECK_EQ(rb[0], static_cast<unsigned char>(ref.dword0 & 0xFF));
    CHECK_EQ(rb[1], static_cast<unsigned char>((ref.dword0 >> 8) & 0xFF));
    CHECK_EQ(rb[2], static_cast<unsigned char>((ref.dword0 >> 16) & 0xFF));
    CHECK_EQ(rb[3], static_cast<unsigned char>((ref.dword0 >> 24) & 0xFF));
    CHECK_EQ(rb[4], static_cast<unsigned char>(ref.word4 & 0xFF));
    CHECK_EQ(rb[5], static_cast<unsigned char>((ref.word4 >> 8) & 0xFF));
    (void)golden;
}
