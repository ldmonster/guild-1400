// Integration tests: the crime-tracking table + penalty-text path exercised
// against the REAL sibling modules — world/law.cpp (g_lawTable, GesetzGetRecord),
// world/crime.cpp (the 512-entry crime table + Beweis), and world/law_text.cpp.
// No mocks for the legal data: the actual recovered tables drive the flow.
#include "test.h"

#include <cstring>

#include "world/straftat_table.h"
#include "world/law_text.h"
#include "world/law.h"
#include "world/crime.h"

using namespace guild;
using namespace guild::world;

// A "crime gets recorded, mission progress is tracked, and the matching law's
// penalty text is built" mini-flow, end to end across the real legal tables.
TEST(StraftatTableI, CrimeTrackThenPenaltyText) {
    LawTableResetDefaults();      // real law.cpp default table
    CrimeAndEvidenceReset();      // real crime.cpp 512-entry table + evidence
    StraftatTableReset();         // the tracking table

    const i32 source = 0x1000;
    const u8  crimeType = 0x13;   // a tracked code (and a real law id < 26)

    // Record a crime in the real crime table and register a tracking slot.
    int slot = StraftatFindFreeSlot();
    CHECK(slot >= 0);
    g_crimeTable[slot].id          = source;
    g_crimeTable[slot].perpetrator = 77;
    g_crimeTable[slot].provenState = 1;       // proven
    g_crimeTable[slot].location    = crimeType; // law key

    g_crimeTrackTable[0].type   = crimeType;
    g_crimeTrackTable[0].source = source;

    // Mission progress bumps for the matching tracked type.
    CHECK_EQ(MissionTrackCrimeProgress(source, crimeType), 1);
    CHECK_EQ(g_crimeTrackTable[0].progress, 1);

    // The crime's law key (location byte) selects a real law record; fetch it.
    LawRecord law{};
    u8 lawKey = g_crimeTable[slot].location;
    CHECK_EQ(GesetzGetRecord(lawKey, &law), 1);
    CHECK_EQ(law.id, lawKey); // record byte+0 round-trips

    // Build the penalty text from the SAME real law table.
    char buf[64];
    PenaltyText p = GesetzBuildPenaltyText(buf, lawKey, law.threshold);
    CHECK_EQ(p.valid, true);
    // category == law id; format/prompt derive from it.
    CHECK_EQ(p.formatTextId, 5 * lawKey + 4146);
    CHECK_EQ(p.promptTextId, 5 * lawKey + 4145);

    // The crime is still proven & countable in the real crime table.
    CHECK_EQ(StraftatCountActiveByTarget(77), 1);
}

// FindAndInit drives a tracking slot from the staging globals, then the crime is
// resolved through the real ResolveAndClear; the tracking slot persists (it is a
// separate table) — verifies the two crime tables are genuinely independent.
TEST(StraftatTableI, TrackingTableIndependentOfCrimeTable) {
    CrimeAndEvidenceReset();
    StraftatTableReset();

    const i32 source = 555;
    int slot = StraftatFindFreeSlot();
    g_crimeTable[slot].id          = source;
    g_crimeTable[slot].perpetrator = 9;
    g_crimeTable[slot].wanted      = 0;
    g_crimeTable[slot].provenState = 1;

    g_crimeTrackTable[1].type   = 0x17;
    g_crimeTrackTable[1].source = source;
    g_crimeTrackStaging16 = 0xABCD;
    CHECK_EQ(StraftatTableFindAndInit(source), 1);
    CHECK_EQ(g_crimeTrackTable[1].staging16, 0xABCD);

    // Force-clear the crime in the REAL crime table.
    CHECK_EQ(StraftatResolveAndClear(source, 1), 0);
    CHECK_EQ(g_crimeTable[slot].id, -1);          // crime slot cleared
    // Tracking slot untouched (different table).
    CHECK_EQ(StraftatTableFindBySource(source), 1);
    CHECK_EQ(g_crimeTrackTable[1].type, static_cast<u8>(0x17));
}

// Every in-range law id produces a deterministic, self-consistent penalty text
// derived from the real law table (or an invalid result for the bad subcat case).
TEST(StraftatTableI, PenaltyTextOverWholeLawTable) {
    LawTableResetDefaults();
    char buf[64];
    for (u8 id = 0; id < 26; ++id) {
        LawRecord rec{};
        CHECK_EQ(GesetzGetRecord(id, &rec), 1);
        PenaltyText p = GesetzBuildPenaltyText(buf, id, 0);
        u8 subcat = reinterpret_cast<const u8*>(&rec)[1]; // record byte +1
        if (subcat <= 3) {
            CHECK_EQ(p.valid, true);
            CHECK_EQ(p.formatTextId, 5 * rec.id + 4146);
            CHECK_EQ(p.promptTextId, 5 * rec.id + 4145);
        } else {
            CHECK_EQ(p.valid, false);
        }
    }
}
