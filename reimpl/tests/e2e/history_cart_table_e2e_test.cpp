// End-to-end: a whole chronicle commandline / scan flow across the recovered
// cart-table lifecycle (ResetAllGroups -> ParseCommandlineGroupIndex(_SET/_USE) ->
// per-group slot fill) and the real-day scanner classifier (ScanNextEventReal).
//
// This drives the recovered RULES the way the engine would: initialise the table,
// open a "_SET" group (which clears it), populate its slots, then walk a list of
// dated chronicle entries with HistoryScanRealStep, accumulating the emitted ids
// exactly as the original loop (continue on 0/5/6, stop/emit on 1/2) would.
#include "test.h"

#include <vector>

#include "world/history_cart_table.h"
#include "world/history_scan.h"
#include "world/history_full.h"

using namespace guild::world;

namespace {

// A parsed chronicle entry (what VIBE_History_ParseDate would yield per label).
struct Entry {
    bool parseOk;
    int  day;        // entry day
    bool gatePassed; // VIBE_History_ParseLabelPasses outcome (diff==1 only)
    bool textEmpty;  // resolved text empty (diff==1 & gate passed)
    int  textId;     // the chronicle id this entry would emit
};

// Drive the REAL scanner over a list, mirroring the original's loop: advance
// while the step code keeps scanning (0/5/6), stop the walk on emit (1), stop (2)
// or parse error (4). Returns the emitted ids (one per emit step).
std::vector<int> DriveRealScan(int currentDay, const std::vector<Entry>& entries) {
    std::vector<int> emitted;
    for (const auto& e : entries) {
        int diff = currentDay - e.day;
        int code = HistoryScanRealStep(e.parseOk, diff, e.gatePassed, e.textEmpty);

        if (code == (int)HistoryScanCode::kEmit)
            emitted.push_back(e.textId);

        // The original returns out of the call on 1/2/4 (stops walking now);
        // codes 0/5/6 continue to the next label.
        if (!HistoryScanRealStepContinues(code))
            break;
    }
    return emitted;
}

} // namespace

TEST(HistoryCartTableE2E, SetGroupResetsThenScanEmitsYesterday) {
    CartTable t{};
    // 1) engine init: reset all groups (only first 4 slots; flag=1).
    HistoryResetAllGroups(t);
    for (int g = 0; g < kCartGroupCount; ++g)
        CHECK_EQ(t.groups[g].active, 1);

    // 2) a "_SET 1" label opens group 1 (full 8-slot reset) and we fill 3 slots.
    int gi = HistoryParseCommandlineGroupIndex(t, 1, /*isSet=*/true);
    CHECK_EQ(gi, 1);
    CHECK_EQ(t.groups[1].slots[7].id, -1); // _SET reset cleared slot 7 too
    t.groups[1].slots[0] = CartSlot{200, 6};
    t.groups[1].slots[1] = CartSlot{201, 7};
    t.groups[1].slots[2] = CartSlot{202, 6};

    // 3) a later "_USE 1" reuses the group without clearing it.
    int gj = HistoryParseCommandlineGroupIndex(t, 1, /*isSet=*/false);
    CHECK_EQ(gj, 1);
    CHECK_EQ(t.groups[1].slots[0].id, 200); // still populated

    // 4) walk a chronicle list for game day 10. Only the entry dated day 9
    //    (diff==1) with a passing gate and non-empty text emits; older entries
    //    (diff>1) are skipped, and the first entry on/after day 10 stops the scan.
    std::vector<Entry> entries = {
        { true, 5, false, false, 7001 }, // diff 5 -> continue
        { true, 7, false, false, 7002 }, // diff 3 -> continue
        { true, 9, true,  false, 7003 }, // diff 1, gate ok, non-empty -> EMIT
        { true, 9, true,  true,  7004 }, // diff 1, gate ok, EMPTY -> code 6, continue
        { true, 9, false, false, 7005 }, // diff 1, gate FAILED -> code 5, continue
        { true, 10, false, false, 7006 }, // diff 0 -> STOP
        { true, 11, false, false, 7007 }, // never reached
    };
    auto emitted = DriveRealScan(/*currentDay=*/10, entries);
    CHECK_EQ(emitted.size(), (size_t)1);
    CHECK_EQ(emitted[0], 7003);
}

TEST(HistoryCartTableE2E, ParseFailureAbortsScan) {
    std::vector<Entry> entries = {
        { true,  3, false, false, 8001 }, // continue
        { false, 0, false, false, 8002 }, // parse fail -> code 4, abort
        { true,  9, true,  false, 8003 }, // never reached
    };
    auto emitted = DriveRealScan(/*currentDay=*/10, entries);
    CHECK_EQ(emitted.size(), (size_t)0);   // aborted before any emit
}

TEST(HistoryCartTableE2E, MultipleEmptyAndGateFailsKeepScanning) {
    // A run of code-6 (empty) and code-5 (gate failed) entries must not stop the
    // scan; the eventual non-empty match emits.
    std::vector<Entry> entries = {
        { true, 9, true,  true,  9001 }, // code 6 -> continue
        { true, 9, false, false, 9002 }, // code 5 -> continue
        { true, 9, true,  true,  9003 }, // code 6 -> continue
        { true, 9, true,  false, 9004 }, // EMIT
    };
    auto emitted = DriveRealScan(/*currentDay=*/10, entries);
    CHECK_EQ(emitted.size(), (size_t)1);
    CHECK_EQ(emitted[0], 9004);
}
