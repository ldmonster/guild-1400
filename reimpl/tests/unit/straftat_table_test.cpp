// Unit tests (golden vectors) for the secondary crime-record / mission-tracking
// table (world/straftat_table.{h,cpp}: VIBE_StraftatTable_* / VIBE_Mission_*) and
// for VIBE_Gesetz_BuildPenaltyText (world/law_text.cpp). Deterministic record /
// text maths, computed against the recovered gilde.exe globals.
#include "test.h"

#include <cstring>

#include "world/straftat_table.h"
#include "world/law_text.h"
#include "world/law.h"

using namespace guild;
using namespace guild::world;

namespace {

// Helper: install one occupied record at index `i` with type `t` and source `s`.
void Put(int i, u8 t, i32 s) {
    StraftatTableReset();
    g_crimeTrackTable[i].type   = t;
    g_crimeTrackTable[i].source = s;
}

} // namespace

// ---------------------------------------------------------------------------
// VIBE_StraftatTable_FindBySource / _ContainsSource (0x53846c / 0x538524).
// ---------------------------------------------------------------------------
TEST(StraftatTable, FindBySourceEmptyTableMisses) {
    StraftatTableReset();
    CHECK_EQ(StraftatTableFindBySource(1234), -1);
    CHECK_EQ(StraftatTableContainsSource(1234), 0);
}

TEST(StraftatTable, FindBySourceHitReturnsIndex) {
    Put(5, 0x0B, 7777);
    CHECK_EQ(StraftatTableFindBySource(7777), 5);
    CHECK_EQ(StraftatTableContainsSource(7777), 1);
    // Key match but the slot is free (type==0) => miss.
    g_crimeTrackTable[5].type = 0;
    CHECK_EQ(StraftatTableFindBySource(7777), -1);
    CHECK_EQ(StraftatTableContainsSource(7777), 0);
}

TEST(StraftatTable, FindBySourceReturnsFirstOccupiedMatch) {
    StraftatTableReset();
    g_crimeTrackTable[3].type = 0x13; g_crimeTrackTable[3].source = 42;
    g_crimeTrackTable[9].type = 0x17; g_crimeTrackTable[9].source = 42;
    CHECK_EQ(StraftatTableFindBySource(42), 3); // lowest index wins
}

TEST(StraftatTable, FindBySourceLastSlot) {
    Put(kStraftatTableCount - 1, 0x28, -55);
    CHECK_EQ(StraftatTableFindBySource(-55), kStraftatTableCount - 1);
}

// ---------------------------------------------------------------------------
// VIBE_StraftatTable_FindAndInit (0x5384a0).
// ---------------------------------------------------------------------------
TEST(StraftatTable, FindAndInitCopiesStagingAndZeroes) {
    Put(2, 0x0B, 900);
    // Pre-dirty the fields FindAndInit must overwrite/zero.
    g_crimeTrackTable[2].field18  = 0x11111111;
    g_crimeTrackTable[2].progress = 0x22222222;
    g_crimeTrackTable[2].field20  = 0x33;
    // Staging source.
    for (int k = 0; k < 8; ++k) g_crimeTrackStaging8[k] = static_cast<u8>(0xA0 + k);
    g_crimeTrackStaging16 = 0x44556677;
    g_crimeTrackStaging20 = 0x8899;

    CHECK_EQ(StraftatTableFindAndInit(900), 2);
    const StraftatTableRecord& r = g_crimeTrackTable[2];
    for (int k = 0; k < 8; ++k) CHECK_EQ(r.staging8[k], static_cast<u8>(0xA0 + k));
    CHECK_EQ(r.staging16, 0x44556677);
    CHECK_EQ(r.staging20, static_cast<u16>(0x8899));
    CHECK_EQ(r.field18, 0);
    CHECK_EQ(r.progress, 0);
    CHECK_EQ(r.field20, 0u);
    // The type/source key are left intact.
    CHECK_EQ(r.type, static_cast<u8>(0x0B));
    CHECK_EQ(r.source, 900);
}

TEST(StraftatTable, FindAndInitMissReturnsMinusOne) {
    StraftatTableReset();
    CHECK_EQ(StraftatTableFindAndInit(123), -1);
}

// ---------------------------------------------------------------------------
// VIBE_Mission_TrackCrimeProgress (0x539054).
// ---------------------------------------------------------------------------
TEST(StraftatTable, TrackProgressMissReturnsZero) {
    StraftatTableReset();
    CHECK_EQ(MissionTrackCrimeProgress(5, 0x0B), 0);
}

TEST(StraftatTable, TrackProgressTrackedTypesBumpWhenMatching) {
    const u8 tracked[] = {0x0B, 0x13, 0x17, 0x1C, 0x28};
    for (u8 t : tracked) {
        Put(0, t, 500);
        // type byte == crimeType -> progress increments.
        CHECK_EQ(MissionTrackCrimeProgress(500, t), 1);
        CHECK_EQ(g_crimeTrackTable[0].progress, 1);
        // Second call bumps again.
        CHECK_EQ(MissionTrackCrimeProgress(500, t), 1);
        CHECK_EQ(g_crimeTrackTable[0].progress, 2);
    }
}

TEST(StraftatTable, TrackProgressTrackedTypeButRecordTypeMismatch) {
    // Found, tracked type, but record's type byte differs -> no bump, returns 1.
    Put(0, 0x0B, 600);
    CHECK_EQ(MissionTrackCrimeProgress(600, 0x13), 1);
    CHECK_EQ(g_crimeTrackTable[0].progress, 0);
}

TEST(StraftatTable, TrackProgressUntrackedTypesReturnZero) {
    // Sample untracked codes across the compare chain boundaries.
    const u8 untracked[] = {0x00, 0x0A, 0x0C, 0x12, 0x14, 0x16, 0x18, 0x1B,
                            0x1D, 0x27, 0x29, 0xFF};
    for (u8 t : untracked) {
        Put(0, t, 700);
        CHECK_EQ(MissionTrackCrimeProgress(700, t), 0);
        CHECK_EQ(g_crimeTrackTable[0].progress, 0);
    }
}

// ---------------------------------------------------------------------------
// VIBE_Gesetz_BuildPenaltyText (0x4c2dc4) — golden vectors vs the recovered
// law table (law.cpp default).
// ---------------------------------------------------------------------------
TEST(GesetzPenaltyText, OutOfRangeLawId) {
    char buf[64];
    PenaltyText p = GesetzBuildPenaltyText(buf, 26, 100); // a2 >= 26 -> 0
    CHECK_EQ(p.valid, false);
    p = GesetzBuildPenaltyText(buf, 255, 100);
    CHECK_EQ(p.valid, false);
}

TEST(GesetzPenaltyText, GoldenVectors) {
    LawTableResetDefaults();
    char buf[64];
    struct V { u8 lawId; bool valid; int fmt; int prompt; int arg; };
    // Computed from the recovered law table (byte+1 subcat, bytes24..27 = arg base,
    // byte+0 == lawId == category). The threshold parameter does NOT affect output.
    const V vecs[] = {
        {0, true, 4146, 4145, 4115}, // subcat 1: v7(=2)   + 4113
        {1, true, 4151, 4150, 4658}, // subcat 2: v7(=536) + 4122
        {2, true, 4156, 4155, 4128}, // subcat 3: v7(=0)   + 4128
        {8, true, 4186, 4185, 10},   // subcat 0: v7(=10)
    };
    for (const V& v : vecs) {
        PenaltyText p = GesetzBuildPenaltyText(buf, v.lawId, 99999 /*ignored*/);
        CHECK_EQ(p.valid, v.valid);
        CHECK_EQ(p.formatTextId, v.fmt);
        CHECK_EQ(p.promptTextId, v.prompt);
        CHECK_EQ(p.argTextId, v.arg);
    }
}

TEST(GesetzPenaltyText, ThresholdParamIsInert) {
    LawTableResetDefaults();
    char buf[64];
    PenaltyText a = GesetzBuildPenaltyText(buf, 8, 0);
    PenaltyText b = GesetzBuildPenaltyText(buf, 8, 1234567);
    CHECK_EQ(a.argTextId, b.argTextId);   // the a3 threshold never reaches the output
    CHECK_EQ(a.formatTextId, b.formatTextId);
}

TEST(GesetzPenaltyText, RenderHookReceivesComputedIds) {
    LawTableResetDefaults();
    static int seenFmt = -1, seenPrompt = -1, seenArg = -1, calls = 0;
    seenFmt = seenPrompt = seenArg = -1; calls = 0;
    GesetzSetPenaltyRenderFn(
        [](char*, int f, int pr, int ar, void*) {
            seenFmt = f; seenPrompt = pr; seenArg = ar; ++calls;
        },
        nullptr);
    char buf[64];
    PenaltyText p = GesetzBuildPenaltyText(buf, 1, 0);
    CHECK_EQ(calls, 1);
    CHECK_EQ(seenFmt, p.formatTextId);
    CHECK_EQ(seenPrompt, p.promptTextId);
    CHECK_EQ(seenArg, p.argTextId);
    // The invalid path must NOT call the render hook.
    calls = 0;
    GesetzBuildPenaltyText(buf, 26, 0);
    CHECK_EQ(calls, 0);
    GesetzSetPenaltyRenderFn(nullptr, nullptr); // restore default
}
