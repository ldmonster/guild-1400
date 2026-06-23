// Golden tests for the AI needs/score master-table builder (recon, wave 19).
//   gilde.exe 0x4764e8 VIBE_AiNeeds_BuildScoreTable  (+ the DFN read overlay of
//   gilde.exe 0x468a40 VIBE_AiMethod_LoadDataFile).
//
// The fixed 60-method definition table (id/name/scorer/execA/execB/flag) is pinned
// against the binary's code literals; the DFN overlay is pinned against a synthetic
// record. A guarded e2e (tests/e2e/ai_needs_e2e_test.cpp) cross-checks the overlay
// against the real shipped Resources/gamedata/ai/AI_DATA.DFN.
#include "tests/framework/test.h"
#include "sim/ai_needs.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// --------------------------------------------------------------------------
// Definition table shape + golden spot-checks against gilde.exe literals.
// --------------------------------------------------------------------------
TEST(AiNeeds, TableHasSixtyMethods) {
    CHECK_EQ((int)kAiNeedsMethodDefs.size(), 60);
}

TEST(AiNeeds, IdsCoverOneToSixtyExactlyOnce) {
    // The build order is not id order, but the 60 ids must be a permutation of
    // 1..60 (no gaps, no dups), matching the catalog slots used.
    bool seen[kAiNeedsRecordCount] = {false};
    for (const auto& d : kAiNeedsMethodDefs) {
        CHECK(d.id >= 1 && d.id <= 60);
        CHECK(!seen[d.id]);            // no duplicate id
        seen[d.id] = true;
    }
    for (int id = 1; id <= 60; ++id)
        CHECK(seen[id]);               // every id present
}

TEST(AiNeeds, GoldenFirstAndLastRows) {
    // First build block (0x4764fd): id 1 "DummyUnversehrtheit", RelWeighted scorer.
    const auto& first = kAiNeedsMethodDefs.front();
    CHECK_EQ((int)first.id, 1);
    CHECK(std::strcmp(first.name, "DummyUnversehrtheit") == 0);
    CHECK_EQ(first.scorer, 0x4796b0u); // VIBE_AiScore_ComputeRelationWeighted
    CHECK_EQ(first.execA, 0x469de0u);
    CHECK_EQ(first.execB, 0x469df0u);
    CHECK_EQ((int)first.flag, 0x001);

    // Last build block (0x4783b8): id 59 "Studium", flag 0x13.
    const auto& last = kAiNeedsMethodDefs.back();
    CHECK_EQ((int)last.id, 59);
    CHECK(std::strcmp(last.name, "Studium") == 0);
    CHECK_EQ(last.scorer, 0x4796b0u);
    CHECK_EQ(last.execA, 0x4761a0u);
    CHECK_EQ(last.execB, 0x476440u);
    CHECK_EQ((int)last.flag, 0x013);
}

TEST(AiNeeds, GoldenNonSequentialIdsAndFlags) {
    // id 13 "Objekt kaufen (AP)" is built at index 11 (out of id order).
    CHECK_EQ((int)kAiNeedsMethodDefs[11].id, 13);
    CHECK(std::strcmp(kAiNeedsMethodDefs[11].name, "Objekt kaufen (AP)") == 0);
    // id 12 "Objekt Kaufen (Markt)" is built late (index 36).
    CHECK_EQ((int)kAiNeedsMethodDefs[36].id, 12);
    CHECK(std::strcmp(kAiNeedsMethodDefs[36].name, "Objekt Kaufen (Markt)") == 0);
    // id 60 "Buergermeister Kerker" is built mid-list (index 49).
    CHECK_EQ((int)kAiNeedsMethodDefs[49].id, 60);
    CHECK(std::strcmp(kAiNeedsMethodDefs[49].name, "Buergermeister Kerker") == 0);
    // "Repair Building" (id 15, built at index 13) flag 0xff; "Bestechung" (id 9,
    // index 8) flag 0x19.
    CHECK_EQ((int)kAiNeedsMethodDefs[13].flag, 0x0ff);
    CHECK(std::strcmp(kAiNeedsMethodDefs[13].name, "Repair Building") == 0);
    CHECK_EQ((int)kAiNeedsMethodDefs[8].flag, 0x019);
    CHECK(std::strcmp(kAiNeedsMethodDefs[8].name, "Bestechung") == 0);
}

TEST(AiNeeds, BildungUsesRelationWeightedScorer) {
    // id 4 "Bildung" (the headline weight string), built at index 3.
    const auto& b = kAiNeedsMethodDefs[3];
    CHECK_EQ((int)b.id, 4);
    CHECK(std::strcmp(b.name, "Bildung") == 0);
    CHECK_EQ(b.scorer, 0x4796b0u);
}

// --------------------------------------------------------------------------
// BuildScoreTable (no-INI shipped path): fixed fields land at the right catalog
// slot (index == id); slot 0 stays unused; desire slots stay empty.
// --------------------------------------------------------------------------
TEST(AiNeeds, BuildNoIniPlacesFixedFieldsByIdAndReturnsOne) {
    AiNeedsCatalog cat;
    int rc = AiNeeds_BuildScoreTable(cat);
    CHECK_EQ(rc, 1);

    // Slot 0 is the unused/disabled slot (id 0, empty name).
    CHECK_EQ((int)cat[0].id, 0);
    CHECK_EQ(cat[0].name[0], '\0');

    // Every defined method lands at catalog[id] with its fixed fields.
    for (const auto& d : kAiNeedsMethodDefs) {
        const AiNeedsCatalogEntry& e = cat[d.id];
        CHECK_EQ((int)e.id, (int)d.id);
        CHECK(std::strcmp(e.name, d.name) == 0);
        CHECK_EQ(e.scorer, d.scorer);
        CHECK_EQ(e.execA, d.execA);
        CHECK_EQ(e.execB, d.execB);
        CHECK_EQ((int)e.flag, (int)d.flag);
        // No INI -> desire slots empty (attr = -1, change = 0).
        for (int s = 0; s < kAiMethodDesireSlots; ++s) {
            CHECK_EQ((int)e.shortSlots[s].attrIndex, -1);
            CHECK_EQ(e.shortSlots[s].change, 0.0f);
            CHECK_EQ((int)e.longSlots[s].attrIndex, -1);
        }
    }
}

// --------------------------------------------------------------------------
// BuildScoreTable (INI path): a binder that feeds desire keys for one method
// fills its slots and mirrors short->prev.
// --------------------------------------------------------------------------
namespace {
// IniSource callbacks that hand "Bildung"'s slot-0 short desire = BILDUNG/+40.
const char*    TestShortDesire(int slot, void*) { return slot == 0 ? "BILDUNG" : nullptr; }
const char*    TestLongDesire(int, void*)       { return nullptr; }
guild::sim::f32 TestShortChange(int, void*)     { return 40.0f; }
guild::sim::f32 TestLongChange(int, void*)      { return 0.0f; }

bool BindBildungOnly(const AiNeedsMethodDef& def, IniSource& ini, void*) {
    if (std::strcmp(def.name, "Bildung") != 0)
        return false; // other methods: no keys
    ini.getShortDesire = &TestShortDesire;
    ini.getLongDesire  = &TestLongDesire;
    ini.getShortChange = &TestShortChange;
    ini.getLongChange  = &TestLongChange;
    ini.ctx = nullptr;
    return true;
}
} // namespace

TEST(AiNeeds, BuildWithIniBinderFillsAndMirrorsSlots) {
    AiNeedsCatalog cat;
    AiNeedsIniBinder binder;
    binder.bind = &BindBildungOnly;
    binder.ctx = nullptr;
    int rc = AiNeeds_BuildScoreTable(/*haveIni=*/true, binder, cat);
    CHECK_EQ(rc, 1);

    const AiNeedsCatalogEntry& bildung = cat[4]; // id 4
    CHECK(std::strcmp(bildung.name, "Bildung") == 0);
    CHECK_EQ((int)bildung.shortSlots[0].attrIndex, 8); // BILDUNG
    CHECK_EQ(bildung.shortSlots[0].change, 40.0f);
    CHECK_EQ((int)bildung.shortSlots[1].attrIndex, -1);
    // prev vector mirrors the 4 short slots (MemMove +80<-+48).
    CHECK_EQ((int)bildung.prevSlots[0].attrIndex, 8);
    CHECK_EQ(bildung.prevSlots[0].change, 40.0f);

    // A non-bound method (id 1) keeps empty desire slots.
    CHECK_EQ((int)cat[1].shortSlots[0].attrIndex, -1);
}

// --------------------------------------------------------------------------
// DFN overlay: synthetic 61-record buffer. Verifies field schedule (id, name,
// 4 short + 4 long {attr,f32}) + short->prev mirror + short-read stop.
// --------------------------------------------------------------------------
namespace {
void PutF32LE(u8* p, float v) {
    u32 bits; std::memcpy(&bits, &v, 4);
    p[0] = (u8)bits; p[1] = (u8)(bits >> 8); p[2] = (u8)(bits >> 16); p[3] = (u8)(bits >> 24);
}
} // namespace

TEST(AiNeeds, OverlayFromDfnReadsScheduleAndMirrors) {
    std::vector<u8> dfn(kAiNeedsDfnTotalBytes, 0);
    // Build record 4 ("Bildung"-like): id=4, name, short slot0 = {8, 20.0},
    // long slot0 = {3, 15.0}.
    u8* rec = dfn.data() + 4 * kAiNeedsDfnRecordBytes;
    rec[0] = 4;
    std::memcpy(rec + 1, "Bildung", 8); // includes NUL
    u8* q = rec + 33;
    q[0] = 8; PutF32LE(q + 1, 20.0f);                // short slot 0
    q[5] = (u8)(i8)-1; PutF32LE(q + 6, 0.0f);        // short slot 1
    // long slots start at +20 from q
    u8* lq = rec + 33 + 20;
    lq[0] = 3; PutF32LE(lq + 1, 15.0f);              // long slot 0

    AiNeedsCatalog cat;
    AiNeeds_BuildScoreTable(cat); // fixed fields first (the load order)
    int n = AiNeeds_OverlayFromDfn(dfn.data(), dfn.size(), cat);
    CHECK_EQ(n, kAiNeedsRecordCount); // 61 full records

    const AiNeedsCatalogEntry& e = cat[4];
    CHECK_EQ((int)e.id, 4);
    CHECK(std::strcmp(e.name, "Bildung") == 0);
    CHECK_EQ((int)e.shortSlots[0].attrIndex, 8);
    CHECK_EQ(e.shortSlots[0].change, 20.0f);
    CHECK_EQ((int)e.shortSlots[1].attrIndex, -1);
    CHECK_EQ((int)e.longSlots[0].attrIndex, 3);
    CHECK_EQ(e.longSlots[0].change, 15.0f);
    // prev mirrors short.
    CHECK_EQ((int)e.prevSlots[0].attrIndex, 8);
    CHECK_EQ(e.prevSlots[0].change, 20.0f);
    // The DFN overlay preserves the fixed fn-ptr field set by BuildScoreTable.
    CHECK_EQ(e.scorer, 0x4796b0u);
}

TEST(AiNeeds, OverlayStopsOnShortRead) {
    // 2.5 records of bytes -> only 2 full records overlaid.
    std::vector<u8> dfn(kAiNeedsDfnRecordBytes * 2 + 10, 0);
    AiNeedsCatalog cat;
    int n = AiNeeds_OverlayFromDfn(dfn.data(), dfn.size(), cat);
    CHECK_EQ(n, 2);
}

// --------------------------------------------------------------------------
// LoadDataFile (the 0x468a40 shipped read path): full 61-record DFN -> 1; short
// DFN -> 0 (the binary's loop only returns 1 after all 61 records read).
// --------------------------------------------------------------------------
TEST(AiNeeds, LoadDataFileFullReturnsOne) {
    std::vector<u8> dfn(kAiNeedsDfnTotalBytes, 0);
    // Stamp record ids so the overlay is observable.
    for (int r = 0; r < kAiNeedsRecordCount; ++r)
        dfn[(std::size_t)r * kAiNeedsDfnRecordBytes] = (u8)r;
    AiNeedsCatalog cat;
    CHECK_EQ(AiNeeds_LoadDataFile(dfn.data(), dfn.size(), cat), 1);
    // Fixed fn-ptr fields still present (set by the internal BuildScoreTable).
    CHECK_EQ(cat[4].scorer, 0x4796b0u);
}

TEST(AiNeeds, LoadDataFileShortReturnsZero) {
    std::vector<u8> dfn(kAiNeedsDfnTotalBytes - 1, 0); // one byte short of 61 records
    AiNeedsCatalog cat;
    CHECK_EQ(AiNeeds_LoadDataFile(dfn.data(), dfn.size(), cat), 0);
}
