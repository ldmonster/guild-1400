// Unit tests for the buildings module (guild::sim) — gilde.exe.
// Golden values were computed independently in python from the recovered
// constants (see the implementer report / scratch script).
#include "test.h"

#include "sim/building.h"
#include "sim/building_type.h"
#include "sim/building_value.h"
#include "sim/entity.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {

bool feq(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

// Builds a clean type table with one production type at index 1 and one storage
// type at index 2, then marks the table loaded.
void setupTypeTable() {
    ResetBuildings();
    g_buildingTypes[1] = BuildingTypeDef{};
    g_buildingTypes[1].kind = 11;          // production kind
    g_buildingTypes[1].security = 3;       // security mode/level
    g_buildingTypes[1].outputFactor[0] = 3;
    g_buildingTypes[1].outputFactor[1] = 5;
    g_buildingTypes[1].inputFactor[0] = 7;
    g_buildingTypes[2] = BuildingTypeDef{};
    g_buildingTypes[2].kind = 10;          // storage kind
    g_buildingTypesLoaded = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Building-type classification (pure integer mappers).
// ---------------------------------------------------------------------------
TEST(SimBuilding, TypeGroupFromCode) {
    CHECK_EQ(BuildingType_GroupFromCode(1), 11);
    CHECK_EQ(BuildingType_GroupFromCode(6), 11);
    CHECK_EQ(BuildingType_GroupFromCode(7), 9);
    CHECK_EQ(BuildingType_GroupFromCode(13), 7);
    CHECK_EQ(BuildingType_GroupFromCode(31), 13);
    CHECK_EQ(BuildingType_GroupFromCode(58), 2);
    CHECK_EQ(BuildingType_GroupFromCode(75), 4);
    CHECK_EQ(BuildingType_GroupFromCode(76), 0);
    CHECK_EQ(BuildingType_GroupFromCode(0), 0);
}

TEST(SimBuilding, TypeGroupFromPairCode) {
    CHECK_EQ(BuildingType_GroupFromPairCode(1), 11);
    CHECK_EQ(BuildingType_GroupFromPairCode(8), 8);
    CHECK_EQ(BuildingType_GroupFromPairCode(9), 0);   // 9..12 unmapped
    CHECK_EQ(BuildingType_GroupFromPairCode(26), 4);
}

TEST(SimBuilding, TypeRankWithinGroup) {
    CHECK_EQ(BuildingType_ComputeRankWithinGroup(1), 6);  // highest=rank1 -> code1=rank6
    CHECK_EQ(BuildingType_ComputeRankWithinGroup(6), 1);
    CHECK_EQ(BuildingType_ComputeRankWithinGroup(7), 6);
    CHECK_EQ(BuildingType_ComputeRankWithinGroup(75), 1);
    CHECK_EQ(BuildingType_ComputeRankWithinGroup(100), 0);
}

TEST(SimBuilding, TypeMappers) {
    // MapToActionCode: code1->group11->action(case 11)=4 ; code13->group7->action7
    CHECK_EQ(BuildingType_MapToActionCode(1), 4);
    CHECK_EQ(BuildingType_MapToActionCode(13), 7);
    CHECK_EQ(BuildingType_MapToActionCode(46), 18);  // code46->group1->action18
    CHECK_EQ(BuildingType_MapToActionCode(76), 0);
    // MapToCategoryCode (group -> category)
    CHECK_EQ(BuildingType_MapToCategoryCode(1), 18);
    CHECK_EQ(BuildingType_MapToCategoryCode(5), 14);
    CHECK_EQ(BuildingType_MapToCategoryCode(11), 4);
    // MapToProfessionCode: code1->group11->prof11
    CHECK_EQ(BuildingType_MapToProfessionCode(1), 11);
    CHECK_EQ(BuildingType_MapToProfessionCode(13), 20);  // group7 -> 20
    // MapActionToCategory
    CHECK_EQ(BuildingType_MapActionToCategory(4), 11);
    CHECK_EQ(BuildingType_MapActionToCategory(18), 1);
    CHECK_EQ(BuildingType_MapActionToCategory(99), 0);
    // ComputeVariantIndex (group, rank)
    CHECK_EQ(BuildingType_ComputeVariantIndex(1, 1), 51);   // 51-(1-1)
    CHECK_EQ(BuildingType_ComputeVariantIndex(1, 6), 46);   // 51-(6-1)
    CHECK_EQ(BuildingType_ComputeVariantIndex(0, 3), 0);
}

TEST(SimBuilding, ClassifyHelpers) {
    CHECK_EQ(BuildingType_ClassifyByRange(3), 7);
    CHECK_EQ(BuildingType_ClassifyByRange(36), 2);
    CHECK_EQ(BuildingType_ClassifyByRange(48), 3);
    CHECK_EQ(BuildingType_ClassifyByRange(60), 4);
    CHECK_EQ(BuildingType_ClassifyByRange(66), 5);
    CHECK_EQ(BuildingType_ClassifyByRange(100), 0);
    // Building_ClassifyTypeFlag
    CHECK_EQ(Building_ClassifyTypeFlag(0), 2);    // a1<1 && != ... -> returns 2
    CHECK_EQ(Building_ClassifyTypeFlag(1), 0);
    CHECK_EQ(Building_ClassifyTypeFlag(7), 2);    // 7..0x27 -> 2
    CHECK_EQ(Building_ClassifyTypeFlag(0x2A), 0);
    CHECK_EQ(Building_ClassifyTypeFlag(52), 0);
    CHECK_EQ(Building_ClassifyTypeFlag(0x3A), 2);
    // Building_IsTypeInGroup
    CHECK_EQ(Building_IsTypeInGroup(1), 0);
    CHECK_EQ(Building_IsTypeInGroup(5), 2);   // 5 not in listed set -> 2
    CHECK_EQ(Building_IsTypeInGroup(24), 0);
}

TEST(SimBuilding, KindPredicates) {
    CHECK(Building_IsStorageKind(10));
    CHECK(!Building_IsStorageKind(11));
    CHECK(Building_IsProductionKind(11));
    CHECK(Building_IsProductionKind(28));
    CHECK(!Building_IsProductionKind(10));
    CHECK_EQ(Building_MapKindToCategory(1), 3);
    CHECK_EQ(Building_MapKindToCategory(2), 6);
    CHECK_EQ(Building_MapKindToCategory(7), 4);
    CHECK_EQ(Building_MapKindToCategory(0x13), 7);
    CHECK_EQ(Building_MapKindToCategory(0x17), 5);
    CHECK_EQ(Building_MapKindToCategory(0), 0);
}

// ---------------------------------------------------------------------------
// Type-table lookup + record accessors.
// ---------------------------------------------------------------------------
TEST(SimBuilding, FindById) {
    ResetEntityArrays();
    // The object/building array keys alive @+0 and id @+1 (the shared 169B record).
    g_objects[3].alive = 1;
    g_objects[3].id = 4242;
    g_objects[7].alive = 1;
    g_objects[7].id = 5;

    CHECK(BuildingFindRecordById(4242) == &g_objects[3]);
    CHECK(BuildingFindRecordById(5) == &g_objects[7]);
    CHECK(BuildingFindRecordById(9999) == nullptr);
    // dead slot is skipped
    g_objects[3].alive = 0;
    CHECK(BuildingFindRecordById(4242) == nullptr);
}

TEST(SimBuilding, TypeTableAccessors) {
    setupTypeTable();
    // The value-math BuildingRec is a standalone overlay (typeIndex @+0).
    BuildingRec b{};
    b.typeIndex = 1;

    CHECK(Building_IsProductionType(&b));
    CHECK(!Building_IsStorageType(&b));
    CHECK_EQ(Building_GetSecurityLevel(&b), 3);
    // security mode 3 -> reqLevel forced to 7 ; 7 >= securityLevel(3) -> true
    CHECK(Building_CheckSecurityThreshold(&b, 0));
    CHECK_EQ(Building_MapTypeToCategory(&b), 2);   // kind 11 -> category 2

    // storage type
    BuildingRec s{}; s.typeIndex = 2;
    CHECK(Building_IsStorageType(&s));
    CHECK(!Building_IsProductionType(&s));

    // upgrade level packed in high byte (arithmetic shift)
    CHECK_EQ(Building_GetUpgradeLevel(0x07000000), 7);
    CHECK_EQ(Building_GetUpgradeLevel(static_cast<i32>(0xFF000000)), -1);

    // table unloaded -> accessors treat as absent
    g_buildingTypesLoaded = false;
    CHECK_EQ(Building_GetSecurityLevel(&b), 0);
    CHECK(!Building_IsProductionType(&b));
}

// ---------------------------------------------------------------------------
// Production-rating / value math (golden vectors).
// ---------------------------------------------------------------------------
TEST(SimBuilding, ProductionRating) {
    setupTypeTable();
    BuildingRec b{};
    b.typeIndex = 1;
    b.statLevel[0] = 200;
    b.statLevel[1] = 150;
    b.statLevel[2] = 100;
    b.statLevel[3] = 250;
    b.statLevel[4] = 128;
    b.staffBits = 0;

    SetBuildingRatingHooks(nullptr);  // inert hooks

    CHECK(feq(Building_EvalProductionRating(&b, 0), 0.7936508394777775));
    CHECK(feq(Building_EvalProductionRating(&b, 1), 0.5952381296083331));
    // staff bitfield affects stat0: bit20 set -> (v>>20)&7 == 1
    b.staffBits = 0x00100000;
    CHECK(feq(Building_EvalProductionRating(&b, 0), 0.7381508371084928));
    b.staffBits = 0x3000;  // bits12-13 affect stat4
    CHECK(feq(Building_EvalProductionRating(&b, 4), 0.007936522364616394));

    // out of range / null
    CHECK(feq(Building_EvalProductionRating(&b, 5), -1.0));
    CHECK(feq(Building_EvalProductionRating(nullptr, 0), -1.0));

    // clamps
    BuildingRec hi{}; hi.typeIndex = 1; hi.statLevel[0] = 252;
    CHECK(feq(Building_EvalProductionRating(&hi, 0), 1.0));
    BuildingRec lo{}; lo.typeIndex = 1; lo.statLevel[0] = 0; lo.staffBits = 0x000000F0;
    CHECK(feq(Building_EvalProductionRating(&lo, 0), 0.0));

    // pixels = trunc(rating*252)
    BuildingRec p{}; p.typeIndex = 1; p.statLevel[0] = 200;
    CHECK_EQ(Building_ComputeProductionPixels(0, &p), 200);
}

TEST(SimBuilding, RatingCurveAndItemValue) {
    setupTypeTable();
    BuildingRec a{}; a.typeIndex = 1; a.statLevel[0] = 200;
    BuildingRec b{}; b.typeIndex = 1; b.statLevel[0] = 100;
    SetBuildingRatingHooks(nullptr);
    CHECK(feq(Building_ComputeRatingCurveA(0, &a, &b), 0.737465408340034));

    // CurveB on raw ratings: f(0.5,0.25)
    double rb = Building_ComputeRatingCurveB(0.5f, 0.25f);
    // ((0.25-0.25)*0.4 + 0.6 + (0.5-0.0625)*0.6 + 0.4)*0.5
    double expect = ((0.25 - 0.25) * 0.4f + 0.6f + (0.5 - 0.0625) * 0.6f + 0.4f) * 0.5f;
    CHECK(feq(rb, expect));

    // item base value: factor 7, kind 0 -> 6272 ; kind 6 (sale) with priceMode 0 -> 3136
    SetBuildingPriceMode(0);
    BuildingRec normal{}; normal.objectKind = 0;
    CHECK(feq(Building_ComputeItemBaseValue(&normal, 1, -1, 0), 6272.0));  // input factor[0]=7
    BuildingRec sale{}; sale.objectKind = 6;
    CHECK(feq(Building_ComputeItemBaseValue(&sale, 1, -1, 0), 3136.0));
    SetBuildingPriceMode(2);
    CHECK(feq(Building_ComputeItemBaseValue(&sale, 1, -1, 0), 6272.0));
    SetBuildingPriceMode(0);
}

// ---------------------------------------------------------------------------
// WAVE-16 1:1: ComputeItemBaseValue @0x58f328. inIdx (a4) reads the 6-wide +553
// PRODUCTION factor array (inputFactor[6], indexed 0..5 by the worth aggregator's
// output loop); outIdx (a3) reads the 2-wide +563 INPUT factor array
// (outputFactor[2], indexed 0..1). The original performs no bounds check; the
// arrays are sized to cover the live indices, so an outIdx of 2..5 reads a 0
// padding byte / out-of-array slot -> 0. statLevel has 5 entries;
// EvalProductionRating must reject negative/over-large stats. ASAN+UBSAN clean.
// ---------------------------------------------------------------------------
TEST(SimBuildingHarden, ItemBaseValueIndexBounds) {
    setupTypeTable();
    SetBuildingPriceMode(0);
    BuildingRec b{}; b.objectKind = 0;

    // valid output slots 0,1 (factors 3,5) -> 896*factor, byte-identical.
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, 0, -1), 2688.0));   // 896*3
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, 1, -1), 4480.0));   // 896*5
    // out-of-range output slots 2..5 (the aggregator's 0..5 sweep) -> 0, no OOB.
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, 2, -1), 0.0));
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, 5, -1), 0.0));
    // valid input slots 0,1 (factors 7,0).
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, -1, 0), 6272.0));   // 896*7
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, -1, 1), 0.0));      // 896*0
    // out-of-range input slot -> 0, no OOB.
    CHECK(feq(Building_ComputeItemBaseValue(&b, 1, -1, 5), 0.0));
}

TEST(SimBuildingHarden, EvalProductionRatingStatBounds) {
    setupTypeTable();
    SetBuildingRatingHooks(nullptr);
    BuildingRec b{}; b.typeIndex = 1;
    for (int i = 0; i < 5; ++i) b.statLevel[i] = 100;
    // valid stats 0..4 return a real (>= 0) rating.
    CHECK(Building_EvalProductionRating(&b, 0) >= 0.0f);
    CHECK(Building_EvalProductionRating(&b, 4) >= 0.0f);
    // stat >= 5 (original guard) and negative stat (added guard) -> -1, no OOB.
    CHECK(feq(Building_EvalProductionRating(&b, 5), -1.0));
    CHECK(feq(Building_EvalProductionRating(&b, -1), -1.0));
    CHECK(feq(Building_EvalProductionRating(&b, 1000), -1.0));
    // null building -> -1.
    CHECK(feq(Building_EvalProductionRating(nullptr, 0), -1.0));
}

TEST(SimBuildingHarden, ItemBaseValueUnloadedType) {
    // A type index with no loaded table entry must resolve to factor 0 (td null
    // path) and not dereference garbage.
    ResetBuildings();                         // table unloaded
    BuildingRec b{}; b.objectKind = 0;
    CHECK(feq(Building_ComputeItemBaseValue(&b, 200, 0, -1), 0.0));
    CHECK(feq(Building_ComputeItemBaseValue(&b, 200, -1, 0), 0.0));
}

TEST(SimBuilding, ProductionRateAndOutput) {
    setupTypeTable();
    // rate: outputFactor {3,5}, price 1000, divisor 10 -> ~995.5556
    const BuildingTypeDef& td = g_buildingTypes[1];
    CHECK(feq(Building_ComputeProductionRate(td, 1000, 10), 995.555591583252, 1e-2));

    // live output interpolation
    BuildingRec b{};
    b.typeIndex = 1;          // +0 word != 0xFFFF
    b.activeFlag = 1;
    b.fillLevel = 50;
    b.fillCap = 100.0f;
    b.outZeroFill = 2.0f;
    b.outFullFill = 10.0f;
    b.outBonus = 1;
    CHECK(feq(Building_ComputeMaxOutput(&b), 6.0));
    CHECK(feq(Building_ComputeCurrentOutput(&b), 7.0));
    CHECK(feq(Building_ComputeOutputRatio(&b), 7.0 / 6.0));

    // inactive building
    b.activeFlag = 0;
    CHECK(feq(Building_ComputeOutputRatio(&b), 0.0));
    CHECK(feq(Building_ComputeMaxOutput(&b), -1.0));
}
