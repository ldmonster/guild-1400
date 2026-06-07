#include "test.h"

// Integration: drive the recovered RunHistoryRewardDialog click-decode core
// (world/tutorial_mission.cpp's MissionHistoryRewardHit / *DisableCount) against
// the REAL reconstructed sibling that the live dialog hands off to —
// world/mission_rules.cpp's MissionHistoryRewardMode + MissionHistoryRewardSelection
// (gilde.exe 0x538f8c.. / 0x538ec6..). NOT a mock: the dialog body in the original
// resolves the clicked radio object to an option index (the hit-test recovered
// here) and feeds that index to MissionHistoryRewardMode to produce the byte_63C8F4
// value passed to RunChooseMissionDialog, while the already-completed slot count it
// disabled seeds the radio selection via MissionHistoryRewardSelection. We replay
// that exact cross-module flow over the genuine sibling and assert the mode bytes
// and seed agree with the disable count.
#include "world/tutorial_mission.h"
#include "world/mission_rules.h"   // REAL sibling decode cores

using namespace guild;
using namespace guild::world;

// A click on each reward option (indices 0..4) must drive the SAME mode byte the
// real RunChooseMissionDialog hand-off computes from that option index, and the
// cancel/back buttons must produce the cancel sentinel (-1) that mission_rules maps.
TEST(TutorialMissionItest, HistoryRewardClickToMode) {
    const int opts[5]  = {101, 102, 103, 104, 105};
    const int cancelId = 200;
    const int backId   = 201;

    // Each reward-option click -> recovered byte_63C8F4 mode (0..4). The real
    // RunHistoryRewardDialog wrote that mode DIRECTLY (opt[i] -> i); the radio
    // group's 1-based option index maps through the sibling MissionHistoryRewardMode
    // (1-based opt index -> 0-based mode), so MissionHistoryRewardMode(i+1) must
    // reproduce the hit-test's mode byte.
    for (int i = 0; i < 5; ++i) {
        int hit = MissionHistoryRewardHit(opts, cancelId, backId, opts[i]);
        CHECK_EQ(hit, i);
        CHECK_EQ(MissionHistoryRewardMode(hit + 1), hit);   // REAL sibling agrees
    }

    // Cancel and back both resolve to -1 in the hit-test; the real sibling maps the
    // cancel option index (0 in the choose list / -1 sentinel) consistently: the
    // hit-test's -1 is the explicit cancel mode the follow-up treats as "abort".
    CHECK_EQ(MissionHistoryRewardHit(opts, cancelId, backId, cancelId), -1);
    CHECK_EQ(MissionHistoryRewardHit(opts, cancelId, backId, backId), -1);
    // mission_rules' option-index mapping: index 0 (cancel) and index 6 (back) -> -1.
    CHECK_EQ(MissionHistoryRewardMode(0), -1);
    CHECK_EQ(MissionHistoryRewardMode(6), -1);
}

// The number of options the recovered disable loop turns off must be exactly the
// count the REAL selection-seed sibling consumes, across the full completed range.
TEST(TutorialMissionItest, DisableCountFeedsRealSelectionSeed) {
    for (int completed = 0; completed <= 6; ++completed) {
        int disabled = MissionHistoryRewardDisableCount(completed);
        int seed     = MissionHistoryRewardSelection(completed);   // REAL sibling
        // At least `disabled` options are unavailable; the seeded selection index
        // must point at or past the first still-enabled option (or wrap to 0 when
        // everything is consumed). Concretely the original: completed>4 -> seed 0.
        if (completed > 4) {
            CHECK_EQ(seed, 0);          // all 5 consumed -> reset to top
            CHECK_EQ(disabled, 5);
        } else if (completed > 0) {
            // seed = 2 + (completed-1) for 1..4 (the ++sel accumulation).
            CHECK_EQ(seed, 2 + (completed - 1));
            CHECK_EQ(disabled, completed);
        } else {
            CHECK_EQ(disabled, 0);      // nothing completed -> nothing disabled
        }
    }
}

// End-to-end of the recovered ChooseHistory hit-test feeding the same real mode
// decode (the choose-history dialog hands its slot index straight to the chooser).
TEST(TutorialMissionItest, ChooseHistorySlotToMode) {
    const int opts[6] = {300, 301, 302, 303, 304, 305};
    const int cancel  = 399;
    for (int i = 0; i < 6; ++i) {
        int slot = MissionChooseHistoryHit(opts, cancel, opts[i]);
        CHECK_EQ(slot, i);
    }
    // The first five slots (radio option indices 1..5) agree with the real
    // sibling's 1-based option -> mode table; slot 5 has no reward-option analog.
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(MissionHistoryRewardMode(i + 1), i);
    CHECK_EQ(MissionChooseHistoryHit(opts, cancel, cancel), -1);
}
