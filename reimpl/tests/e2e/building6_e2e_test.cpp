#include "test.h"

// End-to-end: drive the building entry gate through its full decision ladder for
// a single building as the day progresses and as ownership / VIP status change,
// asserting the gate flips exactly at the recovered boundaries.

#include "sim/building6.h"
#include "sim/building3.h"
#include "sim/building_type.h"

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct WorldHooks : Building6Hooks {
    std::uint8_t types[589 * 8] = {};
    std::int16_t selFlags = 1;          // selectable
    std::uint8_t vip = 0;
    int messages = 0;
    const std::uint8_t* TypeDef(unsigned i) override { return types + 589 * i; }
    std::int16_t ComputeSelectionFlags(unsigned, const std::uint8_t*) override {
        return selFlags;
    }
    std::uint8_t ActivePlayerVipByte(unsigned) override { return vip; }
    void ShowClosedMessage(int, const std::uint8_t*, int, int) override { ++messages; }
    std::uint8_t* type(int i) { return types + 589 * i; }
};

}  // namespace

TEST(Building6E2E, EntryGateAcrossTheDay) {
    WorldHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(/*activePlayer=*/1, /*freeEntry=*/false);

    // A public craftsman shop: type 2 -> kind 6, open-hours record {0x06,5,9}.
    h.type(2)[0] = 6;
    std::uint8_t b[128] = {};
    b[0] = 2;
    std::uint16_t owner = 99;            // owned by someone else
    std::memcpy(b + 39, &owner, 2);

    // Before opening (hour 3): closed -> denied + a message.
    SetBuilding3GameHour(3);
    CHECK_EQ(Building_CheckEntryAllowed(b), 0);
    CHECK_EQ(h.messages, 1);

    // During open hours (hour 6, in [5,9)): allowed, no new message.
    SetBuilding3GameHour(6);
    CHECK_EQ(Building_CheckEntryAllowed(b), 1);
    CHECK_EQ(h.messages, 1);

    // After closing (hour 12): closed again -> denied + another message.
    SetBuilding3GameHour(12);
    CHECK_EQ(Building_CheckEntryAllowed(b), 0);
    CHECK_EQ(h.messages, 2);

    // Ownership overrides the closed schedule entirely (no message added).
    std::uint16_t me = 1; std::memcpy(b + 39, &me, 2);
    CHECK_EQ(Building_CheckEntryAllowed(b), 1);
    CHECK_EQ(h.messages, 2);

    SetBuilding6Hooks(nullptr);
}

TEST(Building6E2E, VipAndForbiddenInteraction) {
    WorldHooks h;
    SetBuilding6Hooks(&h);
    SetEntryGateContext(2, false);
    h.selFlags = 0;                       // not selectable on its own

    // VIP gets into a kind-4 building even when closed / not selectable.
    h.vip = 1;
    h.type(7)[0] = 4;                     // kind 4
    std::uint8_t b[128] = {};
    b[0] = 7;
    std::uint16_t owner = 99; std::memcpy(b + 39, &owner, 2);
    SetBuilding3GameHour(2);
    CHECK_EQ(Building_CheckEntryAllowed(b), 1);

    // But the +91 forbidden bit beats even VIP status.
    b[91] = 0x4;
    CHECK_EQ(Building_CheckEntryAllowed(b), 0);

    SetBuilding6Hooks(nullptr);
}
