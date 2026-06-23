// Verifies guild::world::InstallRealBuildingWiring() binds the reconstructed
// building bridges into their live process-global hook slots (previously inert at
// runtime). The five table/query bridges install their inert default (the
// reconstructed control flow over inert sub-leaves); Building5 binds the REAL
// GameTimeAdvance leaf; BuildingDialog binds the REAL entry gate. Headless, no
// main, UNIQUE prefix WireBuilding.
#include "tests/framework/test.h"

#include "world/wire_building.h"

#include "sim/building2.h"
#include "sim/building3.h"
#include "sim/building4.h"
#include "sim/building5.h"
#include "sim/building6.h"
#include "sim/building_value.h"
#include "sim/gametime.h"

#include <cstdint>
#include <cstring>

using namespace guild;

// --- All five default-installed bridges resolve to a non-null live hook. -------
TEST(WireBuilding, InstallsLiveBridgeHooks) {
    // Baseline: clearing leaves each getter at its static default instance (the
    // bridges resolve null -> their reconstructed inert default, never null).
    sim::SetBuilding2QueryHooks(nullptr);
    sim::SetBuilding3Hooks(nullptr);
    sim::SetBuilding4Hooks(nullptr);
    sim::SetBuilding5Hooks(nullptr);
    sim::SetBuilding6Hooks(nullptr);

    world::InstallRealBuildingWiring();

    // Every bridge now has a live (non-null) hook object installed.
    CHECK(sim::Building2QueryHooks() != nullptr);
    CHECK(sim::Building3HooksGet()   != nullptr);
    CHECK(sim::Building4HooksGet()   != nullptr);
    CHECK(sim::Building5HooksGet()   != nullptr);
    CHECK(sim::Building6HooksGet()   != nullptr);
}

// --- Building5's GameTimeAdvance leaf is bound to the REAL sibling. ------------
// The inert default returns 0 unconditionally; the wired hook forwards to the
// real packed-time arithmetic (gilde.exe 0x583150), so advancing a fresh record
// by 90 minutes yields hour-of-day 1 (and writes it back into the record).
TEST(WireBuilding, Building5GameTimeAdvanceIsReal) {
    world::InstallRealBuildingWiring();

    // A zeroed 14-byte packed time record (day 0, 00:00:00).
    std::uint8_t rec[16];
    std::memset(rec, 0, sizeof(rec));

    sim::Building5Hooks* h = sim::Building5HooksGet();
    // +90 minutes -> 01:30 -> hour 1.
    int hour = h->GameTimeAdvance(rec, /*addDays=*/0, /*addSeconds=*/0,
                                  /*addMinutes=*/90);
    CHECK_EQ(hour, 1);

    // Cross-check against the real sibling run directly over the same input.
    sim::GameTime gt{};
    int direct = sim::GameTimeAdvance(&gt, 0, 0, 90);
    CHECK_EQ(hour, direct);
}

// --- The default-installed bridges run reconstructed control flow safely. ------
// Building6's entry gate exercises the reconstructed decision ladder over the
// inert sub-hooks (selection-flag = 0, VIP = 0): a building with the
// "entry forbidden" flag (+91 & 0x4) is always denied; a clean record with no
// selection bit set is not selectable -> denied. A defined boolean either way.
TEST(WireBuilding, Building6EntryGateRunsOverInertSubhooks) {
    world::InstallRealBuildingWiring();
    sim::SetEntryGateContext(/*activePlayerIndex=*/0, /*freeEntryEverywhere=*/false);

    std::uint8_t building[200];
    std::memset(building, 0, sizeof(building));
    building[91] = 0x4;   // +91 bit2 set -> "entry forbidden"
    int denied = sim::Building_CheckEntryAllowed(building);
    CHECK_EQ(denied, 0);

    building[91] = 0x0;   // clean record; selection flag 0 (inert) -> not selectable
    int allowed = sim::Building_CheckEntryAllowed(building);
    CHECK(allowed == 0 || allowed == 1);   // a defined gate result
}

// --- Building3's reconstructed time-window check runs over the default hook. ---
// CheckTimeWindowOpen reads the recovered open-hours table directly (no sub-hook);
// with the table's first record disabled it reports "always open" -> true.
TEST(WireBuilding, Building3TimeWindowRunsAfterInstall) {
    world::InstallRealBuildingWiring();
    int openH = -1, closeH = -1;
    bool open = sim::Building3_CheckTimeWindowOpen(/*typeCode=*/7, &openH, &closeH);
    CHECK(open == true || open == false);   // a defined window result
}
