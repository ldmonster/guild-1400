// End-to-end flow for a location interaction FSM (gilde.exe VIBE_Location_*):
// open the contact menu, run the modal frame loop while the user clicks items,
// dispatch each click to its dialog, then close. We drive the exact register/
// dispatch FSM the originals use (world/location.cpp) with a mock frame-loop and
// a scripted click source standing in for VIBE_GameLogic_RunFrameLoop /
// dword_631720. The real Form/Dialog/Command helpers live in other modules and
// are represented here only by the dialog-target id that gets dispatched.
#include "test.h"
#include "world/location2.h"

using namespace guild::world;

namespace {

// Scripted modal session: a sequence of "frames". Each frame is either a click
// on a menu item (by index) or "no click"; the session ends when the script is
// exhausted (RunFrameLoop returns false). Records every dialog dispatched.
struct ModalSession {
    std::vector<int> dispatched;   // dialog targets opened, in order
    int frames = 0;

    // Run the loop body once per scripted frame. `clicks[i] < 0` => no click.
    void run(const std::vector<ContactMenuItem>& menu, const std::vector<int>& clicks) {
        // VIBE_StatusText_ResetEntries() — start fresh.
        for (int clickIndex : clicks) {
            ++frames;  // VIBE_GameLogic_RunFrameLoop returned non-zero
            // (Re)register the gated menu items this frame.
            ContactRegistration reg = ContactRegisterMenu(menu);
            // dword_631720 = slot id of the clicked item (0 == nothing).
            int slot = 0;
            if (clickIndex >= 0 && (std::size_t)clickIndex < reg.slotIds.size())
                slot = reg.slotIds[clickIndex];
            int t = ContactDispatch(menu, reg, slot);
            if (t != -1)
                dispatched.push_back(t);
        }
        // RunFrameLoop returns 0 -> Form_Destroy() -> close.
    }
};

int target(LocationDialog d) { return static_cast<int>(d); }

} // namespace

// Full guard-station visit: open with a plain customs box, the player clicks
// Patrol, then Master Certificate, then the customs box (detain), then closes.
TEST(LocationFsmE2E, GuardStationVisit) {
    auto menu = GuardContactMenu(/*shopOpen=*/true, /*customsBox=*/327);
    ModalSession s;
    // frames: idle, click patrol(0), idle, click master-cert(3), click detain(4), close
    s.run(menu, {-1, 0, -1, 3, 4});
    CHECK_EQ(s.frames, 5);
    CHECK_EQ(s.dispatched.size(), (std::size_t)3);
    CHECK_EQ(s.dispatched[0], target(LocationDialog::GuardPatrolStart));
    CHECK_EQ(s.dispatched[1], target(LocationDialog::MasterCertificate));
    CHECK_EQ(s.dispatched[2], target(LocationDialog::GuardDetainStart));
}

// A church visit by a foreign priest with a confession room: confess, donate, leave.
TEST(LocationFsmE2E, ChurchForeignPriestVisit) {
    auto menu = ChurchContactMenu(/*shopOpen=*/false, /*otherCity=*/true, /*confRoom=*/true);
    ModalSession s;
    s.run(menu, {0 /*confess*/, 1 /*donate*/});
    CHECK_EQ(s.dispatched.size(), (std::size_t)2);
    CHECK_EQ(s.dispatched[0], target(LocationDialog::ChurchConfession));
    CHECK_EQ(s.dispatched[1], target(LocationDialog::ChurchDonation));
}

// Thief-prison visit + guard-start gate interaction: the player kidnaps, then
// the breakout option only appears for their own prisoner with the flag set.
TEST(LocationFsmE2E, PrisonAndGuardStartGate) {
    auto menu = ThiefPrisonContactMenu(/*shop=*/true, /*ours=*/true, /*breakout=*/true);
    ModalSession s;
    s.run(menu, {0 /*kidnap*/, 2 /*breakout*/});
    CHECK_EQ(s.dispatched.size(), (std::size_t)2);
    CHECK_EQ(s.dispatched[0], target(LocationDialog::ThiefKidnapStart));
    CHECK_EQ(s.dispatched[1], target(LocationDialog::ThiefBreakout));

    // The guard-start dialog only opens when no active-char action is in flight.
    GuardStartResult blocked = GuardCustomsStart(/*activeCharFlag=*/true);
    CHECK(!blocked.opened);
    GuardStartResult ok = GuardCustomsStart(/*activeCharFlag=*/false);
    CHECK(ok.opened);
    CHECK_EQ(ok.action.dispatchMode, 4);
}

// Idle location: open, spin a few frames with no menu, close — nothing dispatched.
TEST(LocationFsmE2E, IdleLocationNoInteraction) {
    auto menu = IdleContactMenu();
    ModalSession s;
    s.run(menu, {-1, -1, 0, -1});
    CHECK_EQ(s.frames, 4);
    CHECK_EQ(s.dispatched.size(), (std::size_t)0);
}
