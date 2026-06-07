#include "test.h"

// Integration: drive building6's entry gate against the REAL reconstructed
// sibling buildings modules — NO mocks for the two cross-module decisions the
// gate makes internally:
//   sim::Building3_CheckTimeWindowOpen  (building3.cpp, 0x51dc04) — uses the
//       genuine recovered open-hours table kOpenHoursTable + the real clock hour.
//   sim::Building_MapKindToCategory     (building_type.cpp, 0x5878b0) — the real
//       kind->category classifier used to pick the closed-message wording.
//
// CheckEntryAllowed calls both DIRECTLY (live wiring); only the not-yet-
// reconstructed selection-flag query and the GUI message box are hooked. The
// time-window decision flows through the actual building3 table, so the gate
// opens/closes at exactly the bytes the live engine ships.

#include "sim/building6.h"
#include "sim/building3.h"        // real Building3_CheckTimeWindowOpen + table
#include "sim/building_type.h"    // real Building_MapKindToCategory

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct ITHooks : Building6Hooks {
    std::uint8_t types[589 * 8] = {};
    std::int16_t selFlags = 1;
    int lastMsg = -1, lastOpen = -1, lastClose = -1;
    const std::uint8_t* TypeDef(unsigned i) override { return types + 589 * i; }
    std::int16_t ComputeSelectionFlags(unsigned, const std::uint8_t*) override {
        return selFlags;
    }
    void ShowClosedMessage(int m, const std::uint8_t*, int o, int c) override {
        lastMsg = m; lastOpen = o; lastClose = c;
    }
    std::uint8_t* type(int i) { return types + 589 * i; }
};

}  // namespace

// The gate's open/closed decision is taken by the genuine building3 table.
TEST(Building6IT, EntryGateUsesRealTimeWindow) {
    ITHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(/*activePlayer=*/1, false);

    // kOpenHoursTable record {0x06, 5, 9}: a kind-6 building opens [5,9).
    // Confirm the REAL sibling agrees on the boundary the gate will see.
    int o = 0, c = 0;
    SetBuilding3GameHour(7);
    CHECK(Building3_CheckTimeWindowOpen(0x06, &o, &c));   // 7 in [5,9)
    CHECK_EQ(o, 5);
    CHECK_EQ(c, 9);
    SetBuilding3GameHour(4);
    CHECK(!Building3_CheckTimeWindowOpen(0x06, &o, &c));  // 4 < 5

    // Now drive the same decision through the gate.
    h.type(2)[0] = 6;                 // building type 2 -> kind 6
    std::uint8_t b[128] = {};
    b[0] = 2;
    std::uint16_t owner = 99; std::memcpy(b + 39, &owner, 2);

    SetBuilding3GameHour(7);
    CHECK_EQ(Building_CheckEntryAllowed(b), 1);    // open via real table
    CHECK_EQ(h.lastMsg, -1);

    SetBuilding3GameHour(4);
    CHECK_EQ(Building_CheckEntryAllowed(b), 0);    // closed via real table
    CHECK_EQ(h.lastOpen, 5);                       // bounds came from the real table
    CHECK_EQ(h.lastClose, 9);

    SetBuilding6Hooks(nullptr);
}

// The closed-message id is chosen via the REAL kind->category classifier.
TEST(Building6IT, ClosedMessageIdViaRealClassifier) {
    ITHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(1, false);

    // kind 1 -> category 3 (the real classifier) -> the "public" message ids.
    CHECK_EQ((int)Building_MapKindToCategory(1), 3);
    // But kind 1 is not in the open-hours table, so the time-window check returns
    // "open" -> the gate allows entry and never reaches the message branch.
    h.type(2)[0] = 1;
    std::uint8_t b[128] = {};
    b[0] = 2;
    std::uint16_t owner = 99; std::memcpy(b + 39, &owner, 2);
    SetBuilding3GameHour(3);
    CHECK_EQ(Building_CheckEntryAllowed(b), 1);    // kind 1 absent -> always open
    CHECK_EQ(h.lastMsg, -1);

    // A kind that IS scheduled and maps to category 3 exercises the message
    // branch: kind 0x17 (23) is in the table {0x17,23,7} and maps to category 5.
    CHECK_EQ((int)Building_MapKindToCategory(0x17), 5);
    h.type(2)[0] = 0x17;
    SetBuilding3GameHour(10);                      // 10: record {0x17,23,7} closed
    CHECK_EQ(Building_CheckEntryAllowed(b), 0);
    // category 5 -> public wording; nowHour(10) >= openHour(23)? no -> 7219.
    CHECK_EQ(h.lastMsg, 7219);

    SetBuilding6Hooks(nullptr);
}
