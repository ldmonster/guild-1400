#include "test.h"

#include "sim/building6.h"
#include "sim/building3.h"       // SetBuilding3GameHour for the time-window step
#include "sim/building_type.h"   // Building_MapKindToCategory oracle

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A building record: the gate reads +0 (type code), +39 (owner), +91 (flag).
struct BRec {
    std::uint8_t b[128] = {};
    BRec(std::int8_t typeCode, std::uint16_t owner, std::uint8_t flag91) {
        std::memset(b, 0, sizeof b);
        b[0] = static_cast<std::uint8_t>(typeCode);
        std::memcpy(b + 39, &owner, 2);
        b[91] = flag91;
    }
};

// Test hooks: a controllable type table + selection flags + VIP byte + a record
// of any "closed" message shown.
struct GateHooks : Building6Hooks {
    std::uint8_t types[589 * 8] = {};
    std::int16_t selFlags = 0;
    std::uint8_t vip = 0;
    int shownMsg = -1, shownOpen = -1, shownClose = -1;

    const std::uint8_t* TypeDef(unsigned i) override { return types + 589 * i; }
    std::int16_t ComputeSelectionFlags(unsigned, const std::uint8_t*) override {
        return selFlags;
    }
    std::uint8_t ActivePlayerVipByte(unsigned) override { return vip; }
    void ShowClosedMessage(int msg, const std::uint8_t*, int o, int c) override {
        shownMsg = msg; shownOpen = o; shownClose = c;
    }
    std::uint8_t* type(int i) { return types + 589 * i; }
};

}  // namespace

// +91 bit 2 set -> always forbidden, even when everything else would allow.
TEST(Building6, EntryForbiddenFlag) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(/*activePlayer=*/1, /*freeEntry=*/true);
    h.selFlags = 0x201;
    BRec b(/*type=*/2, /*owner=*/1, /*flag91=*/0x4);   // forbidden bit + owner
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 0);
    SetBuilding6Hooks(nullptr);
}

// Global free-entry-everywhere allows entry regardless of selection/time.
TEST(Building6, EntryFreeEverywhere) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(3, /*freeEntry=*/true);
    h.selFlags = 0;                 // not selectable, but free entry wins
    BRec b(2, /*owner=*/99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 1);
    SetBuilding6Hooks(nullptr);
}

// VIP byte allows entry for kinds 6 and 4 only.
TEST(Building6, EntryVipForKind6And4) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.vip = 1;
    h.selFlags = 0;
    h.type(5)[0] = 6;               // type 5 -> kind 6
    BRec b6(5, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b6.b), 1);

    h.type(7)[0] = 4;               // type 7 -> kind 4
    BRec b4(7, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b4.b), 1);

    // A VIP at a kind that is neither 6 nor 4 does NOT get the VIP pass.
    h.type(9)[0] = 2;               // kind 2
    BRec b2(9, 99, 0);
    SetBuilding3GameHour(0);
    CHECK_EQ(Building_CheckEntryAllowed(b2.b), 0);   // not selectable -> denied
    SetBuilding6Hooks(nullptr);
}

// Selection bit 0x200 forces allow.
TEST(Building6, EntryForceAllowFlag) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.selFlags = 0x200;
    BRec b(2, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 1);
    SetBuilding6Hooks(nullptr);
}

// Owning the building (+39 == active player) allows entry.
TEST(Building6, EntryOwnerAllowed) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(42, false);
    h.selFlags = 0;
    BRec b(2, /*owner=*/42, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 1);
    SetBuilding6Hooks(nullptr);
}

// Not selectable (bit 0x1 clear) and none of the allow conditions -> denied.
TEST(Building6, EntryNotSelectable) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.selFlags = 0x200 - 0x200;     // 0
    BRec b(2, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 0);
    CHECK_EQ(h.shownMsg, -1);       // denied before the time-window/UI stage
    SetBuilding6Hooks(nullptr);
}

// Selectable + within open hours -> allowed (no closed message).
TEST(Building6, EntrySelectableAndOpen) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.selFlags = 1;                 // selectable, not force-allow
    // Building kind 6 -> open-hours record {0x06, 5, 9}; hour 7 is in [5,9).
    h.type(2)[0] = 6;
    SetBuilding3GameHour(7);
    BRec b(2, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 1);
    CHECK_EQ(h.shownMsg, -1);
    SetBuilding6Hooks(nullptr);
}

// Selectable but closed -> denied + the closed message is shown. The message id
// is chosen from the current hour vs the open hour and the category.
TEST(Building6, EntryClosedShowsMessage) {
    GateHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.selFlags = 1;
    h.type(2)[0] = 6;               // kind 6 -> open [5,9); category = ?
    // hour 3 < open 5 -> "before opening" branch. category from kind 6:
    u8 cat = Building_MapKindToCategory(6);
    SetBuilding3GameHour(3);
    BRec b(2, 99, 0);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 0);
    int expect = (cat == 3 || cat == 5) ? 7219 : 7216;   // nowHour < openHour
    CHECK_EQ(h.shownMsg, expect);
    CHECK_EQ(h.shownOpen, 5);
    CHECK_EQ(h.shownClose, 9);

    // hour 20 >= open 5 -> "after closing" branch (different message id).
    h.shownMsg = -1;
    SetBuilding3GameHour(20);
    CHECK_EQ(Building_CheckEntryAllowed(b.b), 0);
    int expect2 = (cat == 3 || cat == 5) ? 7220 : 7217;  // nowHour >= openHour
    CHECK_EQ(h.shownMsg, expect2);
    SetBuilding6Hooks(nullptr);
}

// Null building -> 0 (defensive guard).
TEST(Building6, EntryNullBuilding) {
    SetBuilding6Hooks(nullptr);
    CHECK_EQ(Building_CheckEntryAllowed(nullptr), 0);
}
