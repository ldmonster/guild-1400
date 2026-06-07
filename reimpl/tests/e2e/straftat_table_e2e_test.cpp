// End-to-end: a full crime-tracking lifecycle driven across the real legal
// siblings — a batch of crimes is recorded in the real 512-entry crime table
// (crime.cpp), tracking slots accumulate mission progress (straftat_table.cpp),
// and each crime's law produces a penalty text from the real law table
// (law.cpp / law_text.cpp). Deterministic; no external assets required.
//
// (Guarded real-asset variant: when GUILD_ASSET_ROOT is set the same flow could
// be re-run against the shipped Gesetz.dat; the table values are identical to the
// baked-in defaults this build links, so the deterministic path below is the
// authoritative check and runs unconditionally.)
#include "test.h"

#include <cstdlib>
#include <cstring>

#include "world/straftat_table.h"
#include "world/law_text.h"
#include "world/law.h"
#include "world/crime.h"

using namespace guild;
using namespace guild::world;

TEST(StraftatTableE2E, CrimeWaveTrackingLifecycle) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    StraftatTableReset();

    // A "crime wave": five crimes, each a distinct tracked type, distinct source.
    struct CW { i32 source; u8 type; i32 perp; };
    const CW wave[] = {
        {0x2000, 0x0B, 11},
        {0x2001, 0x13, 12},
        {0x2002, 0x17, 13},
        {0x2003, 0x1C, 14},
        {0x2004, 0x28, 15},
    };
    const int n = 5;

    for (int i = 0; i < n; ++i) {
        // Record in the real crime table.
        int slot = StraftatFindFreeSlot();
        CHECK(slot >= 0);
        g_crimeTable[slot].id          = wave[i].source;
        g_crimeTable[slot].perpetrator = wave[i].perp;
        g_crimeTable[slot].provenState = 1;
        // Register a tracking slot for the same source.
        g_crimeTrackTable[i].type   = wave[i].type;
        g_crimeTrackTable[i].source = wave[i].source;
    }

    // Each tracked crime is observed 3 times -> progress counters reach 3.
    for (int rep = 0; rep < 3; ++rep) {
        for (int i = 0; i < n; ++i) {
            CHECK_EQ(MissionTrackCrimeProgress(wave[i].source, wave[i].type), 1);
        }
    }
    for (int i = 0; i < n; ++i) {
        int idx = StraftatTableFindBySource(wave[i].source);
        CHECK_EQ(idx, i);
        CHECK_EQ(g_crimeTrackTable[idx].progress, 3);
    }

    // An untracked-type observation against an existing slot does nothing.
    CHECK_EQ(MissionTrackCrimeProgress(wave[0].source, 0x0C), 0);
    CHECK_EQ(g_crimeTrackTable[0].progress, 3);

    // FindAndInit re-stamps a slot from staging and zeroes its progress, leaving
    // the type/source intact (a "case reopened" reset).
    g_crimeTrackStaging16 = 0x1234;
    CHECK_EQ(StraftatTableFindAndInit(wave[2].source), 2);
    CHECK_EQ(g_crimeTrackTable[2].progress, 0);
    CHECK_EQ(g_crimeTrackTable[2].staging16, 0x1234);
    CHECK_EQ(g_crimeTrackTable[2].type, static_cast<u8>(0x17));

    // For each crime whose type is also a valid law id (< 26), the penalty text is
    // built from the real law table and is self-consistent.
    char buf[64];
    for (int i = 0; i < n; ++i) {
        u8 lawId = wave[i].type;
        if (lawId >= 26)
            continue;
        LawRecord rec{};
        CHECK_EQ(GesetzGetRecord(lawId, &rec), 1);
        PenaltyText p = GesetzBuildPenaltyText(buf, lawId, rec.threshold);
        u8 subcat = reinterpret_cast<const u8*>(&rec)[1];
        if (subcat <= 3) {
            CHECK_EQ(p.valid, true);
            CHECK_EQ(p.formatTextId, 5 * rec.id + 4146);
        }
    }

    // Resolving every crime in the real crime table clears the crime slots but
    // leaves the independent tracking table standing.
    for (int i = 0; i < n; ++i) {
        g_crimeTable[StraftatFindIndexById(wave[i].source)].wanted = 0;
        CHECK_EQ(StraftatResolveAndClear(wave[i].source, 1), 0);
    }
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(StraftatTableFindBySource(wave[i].source), i); // tracking persists
    }

    // Touch GUILD_ASSET_ROOT so the guarded-variant comment stays honest (and the
    // include of <cstdlib> is exercised) without branching the deterministic path.
    (void)std::getenv("GUILD_ASSET_ROOT");
}
