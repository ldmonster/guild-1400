// Wave-12 hardening — boundary / out-of-range tests for the AI-method registry and
// scoring kernels (aimethod_recon3_registry.cpp + aimethod_score_ai_recon2.cpp).
// Focus: the attribute-name index (-1 == "no attribute" gate), the method-id range
// gate that bounds the catalog index 148*id, the field-stream null/empty paths, and
// the relation/candidate selectors over degenerate (empty / equal) inputs. Built
// under ASAN+UBSAN. Goldens for valid input remain unchanged.
#include "test.h"

#include "sim/aimethod_recon3_registry.h"
#include "sim/aimethod_score_ai_recon2.h"

#include <array>
#include <vector>

using namespace guild;
using namespace guild::sim;

// --- Attribute index lookup: bad / unknown names yield -1 -------------------
TEST(AiMethodIndexBoundary, AttributeLookupUnknownAndNull) {
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex(nullptr), -1);
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex(""), -1);
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex("NOPE"), -1);
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex("APSX"), -1);   // prefix, no full match
    // Valid endpoints still resolve (index in [0, kAiAttributeCount)).
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex("APS"), 0);
    CHECK_EQ((int)AiNeeds_LookupAttributeIndex("TRAEGHEIT"),
             kAiAttributeCount - 1);
}

// --- Method-id range gate bounds the catalog index (148*id) -----------------
TEST(AiMethodIndexBoundary, RegisterIdRangeGate) {
    IniSource ini{};                    // all-null callbacks
    auto tryId = [&](int id) {
        AiMethodRecord rec{};
        rec.id = static_cast<u8>(id);
        bool eff = true;
        return AiMethod_RegisterFromIni(rec, /*haveIni*/false, ini, &eff);
    };
    // id <= 0 or id >= 61 -> reject (0): these would index the catalog out of range.
    CHECK_EQ(tryId(0), 0);
    CHECK_EQ(tryId(61), 0);
    CHECK_EQ(tryId(255), 0);            // wraps to signed -1 -> reject
    CHECK_EQ(tryId(128), 0);            // signed-negative byte -> reject
    // In-range ids commit (1).
    CHECK_EQ(tryId(1), 1);
    CHECK_EQ(tryId(60), 1);
}

// --- RegisterFromIni with a present desire key but an UNKNOWN attribute ------
TEST(AiMethodIndexBoundary, RegisterUnknownDesireAttrStoresMinusOne) {
    static const char* kBad = "DOESNOTEXIST";
    IniSource ini{};
    ini.getShortDesire = [](int, void*) -> const char* { return kBad; };
    AiMethodRecord rec{};
    rec.id = 5;
    bool eff = true;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, /*haveIni*/true, ini, &eff), 1);
    // Unknown attribute -> attrIndex == -1 (the "no attribute" slot gate), and a
    // method that affects nothing -> hasEffect false.
    CHECK_EQ((int)rec.shortSlots[0].attrIndex, -1);
    CHECK(!eff);
}

// --- StreamRecord: null stream / null buffer fail cleanly -------------------
TEST(AiMethodIndexBoundary, StreamRecordNullGuards) {
    u8 buf[148] = {0};
    ByteStream s{};                     // null xfer
    CHECK(!AiMethod_StreamRecord(buf, s, /*mirror*/true));
    s.xfer = [](u8*, i32, i32, void*) { return true; };
    CHECK(!AiMethod_StreamRecord(nullptr, s, /*mirror*/false));
}

// --- PickMostDislikedRelation: all-equal / all-at-cap -> none (-1) ----------
TEST(AiMethodIndexBoundary, PickMostDislikedDegenerate) {
    // All at the 100.0 cap: nothing is strictly below -> -1 (no negative index).
    std::array<float, 3> capped{100.0f, 100.0f, 100.0f};
    CHECK_EQ(PickMostDislikedRelation(capped), -1);
    // One below the cap -> its index (in [0,2]).
    std::array<float, 3> one{100.0f, 12.0f, 100.0f};
    CHECK_EQ(PickMostDislikedRelation(one), 1);
}

// --- SelectBestAiMethod: empty candidate set -> id -1 (no OOB scan) ----------
TEST(AiMethodIndexBoundary, SelectBestEmpty) {
    std::vector<MethodCandidate> none;
    MethodChoice c = SelectBestAiMethod(none);
    CHECK_EQ(c.id, -1);                 // both winners stay -1
}

TEST(AiMethodIndexBoundary, SelectBestSingle) {
    std::vector<MethodCandidate> one{ {42, 5.0f, 3.0f} };
    MethodChoice c = SelectBestAiMethod(one);
    CHECK_EQ(c.id, 42);
}
