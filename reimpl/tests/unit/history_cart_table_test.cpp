// Unit tests for the chronicle cart group-slot table + the real-day scanner.
// Golden vectors derived directly from the gilde.exe reset loops:
//   ResetGroupSlot      0x4fd140 — all 8 slots freed (id=-1, kind=0xFF), flag=1
//   ResetChronicleState 0x4fd090 / FreeChronicleFiles 0x4fd194 — first 4 slots only
//   ParseCommandlineFirstPass 0x4fd6ac — group-index gate (< 4) + "_SET" reset
//   ScanNextEventReal   0x4fe9cc — per-step code classification
#include "test.h"

#include "world/history_cart_table.h"
#include "world/history_scan.h"
#include "world/history_full.h"   // HistoryScanCode

using namespace guild::world;

// ---------------------------------------------------------------------------
// ResetGroupSlot 0x4fd140
// ---------------------------------------------------------------------------
TEST(HistoryCartTable, ResetGroupSlotFreesAllEight) {
    CartTable t{};
    // Dirty the group first so we can see the reset.
    for (int k = 0; k < kCartSlotCount; ++k) {
        t.groups[1].slots[k].id = 100 + k;
        t.groups[1].slots[k].kind = 7;
    }
    t.groups[1].active = 0;

    CHECK(HistoryResetGroupSlot(t, 1));      // returns true (1)
    CHECK_EQ(t.groups[1].active, 1);
    for (int k = 0; k < kCartSlotCount; ++k) {  // ALL 8 slots freed
        CHECK_EQ(t.groups[1].slots[k].id, -1);
        CHECK_EQ((int)t.groups[1].slots[k].kind, 0xFF);
    }
    // Other groups untouched.
    CHECK_EQ(t.groups[0].active, 0);
    CHECK_EQ(t.groups[2].active, 0);
}

TEST(HistoryCartTable, ResetGroupSlotRejectsOutOfRange) {
    CartTable t{};
    CHECK(!HistoryResetGroupSlot(t, 4));     // a1 >= 4u -> 0 (false)
    CHECK(!HistoryResetGroupSlot(t, 99));
    // Nothing written.
    CHECK_EQ(t.groups[0].active, 0);
}

// ---------------------------------------------------------------------------
// ResetChronicleState / FreeChronicleFiles 0x4fd090 / 0x4fd194
// Golden quirk: only the FIRST 4 slots per group are cleared (loop v3=8..32).
// ---------------------------------------------------------------------------
TEST(HistoryCartTable, ResetAllGroupsClearsFirstFourOnly) {
    CartTable t{};
    for (int g = 0; g < kCartGroupCount; ++g) {
        t.groups[g].active = 0;
        for (int k = 0; k < kCartSlotCount; ++k) {
            t.groups[g].slots[k].id = 55;
            t.groups[g].slots[k].kind = 9;
        }
    }

    HistoryResetAllGroups(t);

    for (int g = 0; g < kCartGroupCount; ++g) {
        CHECK_EQ(t.groups[g].active, 1);
        // Slots 0..3 freed.
        for (int k = 0; k < kResetAllGroupsSlots; ++k) {
            CHECK_EQ(t.groups[g].slots[k].id, -1);
            CHECK_EQ((int)t.groups[g].slots[k].kind, 0xFF);
        }
        // Slots 4..7 LEFT UNTOUCHED (the asymmetry vs ResetGroupSlot).
        for (int k = kResetAllGroupsSlots; k < kCartSlotCount; ++k) {
            CHECK_EQ(t.groups[g].slots[k].id, 55);
            CHECK_EQ((int)t.groups[g].slots[k].kind, 9);
        }
    }
}

// ---------------------------------------------------------------------------
// ParseCommandlineFirstPass 0x4fd6ac (group-index core)
// ---------------------------------------------------------------------------
TEST(HistoryCartTable, GroupIndexUseDoesNotReset) {
    CartTable t{};
    t.groups[2].slots[0].id = 123;           // pre-existing slot
    int idx = HistoryParseCommandlineGroupIndex(t, 2, /*isSet=*/false);
    CHECK_EQ(idx, 2);
    CHECK_EQ(t.groups[2].slots[0].id, 123);  // "_USE" keeps the slot
}

TEST(HistoryCartTable, GroupIndexSetResetsGroup) {
    CartTable t{};
    t.groups[3].slots[0].id = 123;
    int idx = HistoryParseCommandlineGroupIndex(t, 3, /*isSet=*/true);
    CHECK_EQ(idx, 3);
    CHECK_EQ(t.groups[3].active, 1);
    CHECK_EQ(t.groups[3].slots[0].id, -1);   // "_SET" resets the whole group
    CHECK_EQ(t.groups[3].slots[7].id, -1);   // all 8 (ResetGroupSlot semantics)
}

TEST(HistoryCartTable, GroupIndexRejectsOutOfRange) {
    CartTable t{};
    CHECK_EQ(HistoryParseCommandlineGroupIndex(t, 4, false), -1);
    CHECK_EQ(HistoryParseCommandlineGroupIndex(t, -1, true), -1); // (unsigned)>=4
    CHECK_EQ(HistoryParseCommandlineGroupIndex(t, 0, false), 0);  // boundary ok
    CHECK_EQ(HistoryParseCommandlineGroupIndex(t, 3, false), 3);  // boundary ok
}

// ---------------------------------------------------------------------------
// ScanNextEventReal 0x4fe9cc — step classifier golden vectors.
// ---------------------------------------------------------------------------
TEST(HistoryScan, RealStepCodes) {
    const int kEmit  = (int)HistoryScanCode::kEmit;       // 1
    const int kStop  = (int)HistoryScanCode::kStop;       // 2
    const int kErr   = (int)HistoryScanCode::kParseError; // 4
    const int kEmpty = (int)HistoryScanCode::kEmitEmpty;  // 6

    // Parse failure dominates regardless of the day diff.
    CHECK_EQ(HistoryScanRealStep(/*parseOk*/false, 1, true, false), kErr);
    CHECK_EQ(HistoryScanRealStep(false, 5, false, true), kErr);

    // diff == 1: gate decides.
    CHECK_EQ(HistoryScanRealStep(true, 1, /*gate*/true,  /*empty*/false), kEmit);
    CHECK_EQ(HistoryScanRealStep(true, 1, /*gate*/true,  /*empty*/true),  kEmpty);
    CHECK_EQ(HistoryScanRealStep(true, 1, /*gate*/false, /*empty*/false),
             kHistoryScanLabelGateFailed);              // 5

    // diff < 1 stops; diff > 1 continues (0).
    CHECK_EQ(HistoryScanRealStep(true, 0, false, false), kStop);
    CHECK_EQ(HistoryScanRealStep(true, -3, false, false), kStop);
    CHECK_EQ(HistoryScanRealStep(true, 2, false, false), 0);
    CHECK_EQ(HistoryScanRealStep(true, 100, false, false), 0);
}

TEST(HistoryScan, RealStepContinuePredicate) {
    // 0 (continue), 5 (gate failed), 6 (empty) keep scanning.
    CHECK(HistoryScanRealStepContinues(0));
    CHECK(HistoryScanRealStepContinues(kHistoryScanLabelGateFailed));
    CHECK(HistoryScanRealStepContinues((int)HistoryScanCode::kEmitEmpty));
    // 1 (emit), 2 (stop), 4 (parse error), 3 (eof) terminate the call.
    CHECK(!HistoryScanRealStepContinues((int)HistoryScanCode::kEmit));
    CHECK(!HistoryScanRealStepContinues((int)HistoryScanCode::kStop));
    CHECK(!HistoryScanRealStepContinues((int)HistoryScanCode::kParseError));
    CHECK(!HistoryScanRealStepContinues((int)HistoryScanCode::kEndOfFile));
}
