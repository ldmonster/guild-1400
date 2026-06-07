// Unit tests for the Guild world/economy module (guild::world).
#include <cstring>
#include <vector>

#include "tests/framework/test.h"
#include "world/city.h"
#include "world/economy.h"
#include "world/production.h"
#include "world/tax.h"
#include "world/types.h"

using namespace guild::world;

// ---------------------------------------------------------------------------
// City record layout: parse a synthetic .ini into the 756-byte struct and
// verify fields land at the right byte offsets.
// ---------------------------------------------------------------------------
TEST(WorldCity, RecordSizeAndOffsets) {
    CHECK_EQ(sizeof(CityRecord), (size_t)756);
    // Spot-check a few offsets via offsetof (the static_asserts in types.h
    // already enforce all of them at compile time).
    CHECK_EQ((int)offsetof(CityRecord, faith), 73);
    CHECK_EQ((int)offsetof(CityRecord, importGoods), 682);
    CHECK_EQ((int)offsetof(CityRecord, exportGoods), 714);
    CHECK_EQ((int)offsetof(CityRecord, mapOffset), 748);
    CHECK_EQ((int)offsetof(CityRecord, verfassung), 476);
}

TEST(WorldCity, ParseSyntheticIni) {
    const char* ini =
        "[A - ALLGEMEIN]\n"
        "Stadtname=Augsburg\n"
        "KartenPosition=123,456\n"
        "Glaube=2\n"
        "HistorieStart=1410\n"
        "HistorieEnde=1480\n"
        "Land=3\n"
        "Sprache=1\n"
        "MaxPlayer=4\n"
        "KartenOffset=7,8\n"
        "[A - EINWOHNER]\n"
        "Einwohner_0=1400,2500\n"
        "Einwohner_1=1450,3100\n"
        "[A - PRUNK]\n"
        "Prunk_0=1400,5\n"
        "[B - WETTER]\n"
        "Regenwahrscheinlichkeit=10,20,30,40\n"
        "Schneewahrscheinlichkeit=1,2,3,4\n"
        "Zufrierenwahrscheinlichkeit=55\n"
        "[C - PRIVILEGIEN]\n"
        "Privilegien=1,0,1,1,0,1,0,0,1,1,0\n"
        "[F - GESETZE]\n"
        "Verfassungsgesetze=5,6,7,0,0,0,0,0,0,0,0,0\n"
        "Strafgesetze=11,12,0,0,0,0,0,0,0,0,0,0\n";

    IniDocument* doc = IniParse(ini);
    CityLoadFromIni(doc, 0);
    const CityRecord& c = g_cities[0];

    // Name as UTF-16 of the ASCII bytes.
    CHECK_EQ((int)c.name[0], (int)'A');
    CHECK_EQ((int)c.name[1], (int)'u');
    CHECK_EQ((int)c.name[7], (int)'g');
    CHECK_EQ((int)c.name[8], 0);

    CHECK_EQ(c.mapPos[0], 123);
    CHECK_EQ(c.mapPos[1], 456);
    CHECK_EQ((int)c.faith, 2);
    CHECK_EQ(c.historyStart, 1410);
    CHECK_EQ(c.historyEnd, 1480);
    CHECK_EQ((int)c.land, 3);
    CHECK_EQ((int)c.language, 1);
    CHECK_EQ((int)c.maxPlayer, 4);
    CHECK_EQ(c.mapOffset[0], 7);
    CHECK_EQ(c.mapOffset[1], 8);

    CHECK_EQ(c.einwohner[0].year, 1400);
    CHECK_EQ(c.einwohner[0].value, 2500);
    CHECK_EQ(c.einwohner[1].year, 1450);
    CHECK_EQ(c.einwohner[1].value, 3100);
    CHECK_EQ(c.prunk[0].value, 5);

    CHECK_EQ(c.rainProb[0], 10);
    CHECK_EQ(c.rainProb[3], 40);
    CHECK_EQ(c.snowProb[2], 3);
    CHECK_EQ(c.freezeProb, 55);

    CHECK_EQ((int)c.privileges[0], 1);
    CHECK_EQ((int)c.privileges[1], 0);
    CHECK_EQ((int)c.privileges[9], 1);

    CHECK_EQ((int)c.verfassung[0], 5);
    CHECK_EQ((int)c.verfassung[2], 7);
    CHECK_EQ((int)c.straf[0], 11);
    CHECK_EQ((int)c.straf[1], 12);

    IniFree(doc);
}

TEST(WorldCity, ParseIntLenient) {
    CHECK_EQ(UtilParseInt("  -42xyz"), -42);
    CHECK_EQ(UtilParseInt("+7"), 7);
    CHECK_EQ(UtilParseInt("0"), 0);
    CHECK_EQ(UtilParseInt("1400"), 1400);
}

TEST(WorldCity, InitParameterTableSeeds) {
    CityInitParameterTable(1000.0f);
    CHECK_EQ((int)g_goods[3].driftWeight, 300);
    CHECK_EQ((int)g_goods[10].driftWeight, 65535);  // word_12347F0 == -1
    CHECK_EQ((int)g_goods[17].driftWeight, 800);
    CHECK_EQ((int)g_goods[1].contribWeight, 100);
    CHECK_EQ((int)g_goods[2].contribWeight, 10);
    CHECK_EQ((int)g_goods[11].cap, 2000);
    CHECK_EQ((int)g_goods[3].cap, 150);
    CHECK_EQ(g_capDivisor, 1000.0f);
    for (int g = 0; g < kGoodCategoryCount; ++g) {
        CHECK_EQ(g_goods[g].accum, 0.0f);
        CHECK_EQ(g_goods[g].priceDelta, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Profession classification.
// ---------------------------------------------------------------------------
TEST(WorldEconomy, ClassifyProfession) {
    CHECK(ClassifyProfession(3) == ProfClass::Flat);
    CHECK(ClassifyProfession(7) == ProfClass::Service);
    CHECK(ClassifyProfession(15) == ProfClass::Service);
    CHECK(ClassifyProfession(19) == ProfClass::Service);
    CHECK(ClassifyProfession(25) == ProfClass::Service);   // >22
    CHECK(ClassifyProfession(5) == ProfClass::Default);
    CHECK(ClassifyProfession(6) == ProfClass::Default);
}

// ---------------------------------------------------------------------------
// Supply/demand totals over synthetic person sets (golden values from python).
// ---------------------------------------------------------------------------
TEST(WorldEconomy, DemandAccumulation) {
    CityInitParameterTable(1000.0f);
    std::vector<PersonEcoView> persons[28];
    persons[5] = {{10, false}, {20, false}, {5, true}};    // default class
    persons[3] = {{4, false}, {7, false}};                  // flat class
    persons[25] = {{8, true}};                              // service (>22)
    EconomyComputeGoodsDemand(persons);

    CHECK_EQ(g_goods[5].accum, 62.5f);
    CHECK_EQ(g_goods[3].accum, 16.5f);
    CHECK_EQ(g_goods[25].accum, 11.5f);
}

TEST(WorldEconomy, DemandCityTotals) {
    CityInitParameterTable(1000.0f);
    std::vector<PersonEcoView> persons[28];
    // goods 1 & 2 feed the money total; both default-class with contrib 100/10.
    persons[1] = {{10, false}};   // default: 10*2-1 = 19
    persons[2] = {{5, false}};    // default: 5*2-1 = 9
    persons[5] = {{10, false}};   // goods total: good5 contrib 10 * (10*2-1=19)
    EconomyComputeGoodsDemand(persons);
    // money = contrib[1]*19 + contrib[2]*9 = 100*19 + 10*9 = 1900 + 90 = 1990
    CHECK_EQ(g_cityTotalMoney, 1990.0f);
    // goods = sum_{3..27} contrib*accum; only good5 nonzero: 10*19 = 190
    CHECK_EQ(g_cityTotalGoods, 190.0f);
}

TEST(WorldEconomy, SupplyAccumulation) {
    CityInitParameterTable(1000.0f);
    std::vector<PersonEcoView> persons = {{12, false}, {6, true}};  // good6 default
    EconomyComputeGoodsSupply(6, persons);
    CHECK_EQ(g_goods[6].accum, 28.5f);
}

// ---------------------------------------------------------------------------
// Price-delta drives prices toward equilibrium (direction + magnitude).
// ---------------------------------------------------------------------------
TEST(WorldEconomy, PriceDeltaNormalGood) {
    CityInitParameterTable(1000.0f);
    // good 5: drift 350, cap 500 (<1000 -> processed). set accum to 62.5.
    g_goods[5].accum = 62.5f;
    EconomyComputePriceDeltas();
    // raw = 350*62.5/1000 = 21.875 (>=1) -> delta = raw - 1 = 20.875 (rise)
    CHECK_EQ(g_goods[5].priceDelta, 20.875f);
    CHECK(g_goods[5].priceDelta > 0.0f);  // surplus demand -> price up
}

TEST(WorldEconomy, PriceDeltaInvertedGood) {
    CityInitParameterTable(1000.0f);
    g_goods[4].accum = 30.0f;   // drift 100, cap 500
    EconomyComputePriceDeltas();
    // raw = 100*30/1000 = 3.0 (>=1) -> inverted -> delta = -(raw-1) = -2.0
    CHECK_EQ(g_goods[4].priceDelta, -2.0f);
}

TEST(WorldEconomy, PriceDeltaCappedSkipped) {
    CityInitParameterTable(100.0f);   // capDivisor 100
    // good 11 has cap 2000 >= 100 -> delta forced to 0 regardless of accum.
    g_goods[11].accum = 999.0f;
    EconomyComputePriceDeltas();
    CHECK_EQ(g_goods[11].priceDelta, 0.0f);
}

TEST(WorldEconomy, PriceDeltaBelowEquilibrium) {
    CityInitParameterTable(1000.0f);
    // good 5: small accum so raw<1 -> delta = -(1-raw) < 0 (price falls).
    g_goods[5].accum = 1.0f;   // raw = 350/1000 = 0.35
    EconomyComputePriceDeltas();
    CHECK_EQ(g_goods[5].priceDelta, -0.65f);
    CHECK(g_goods[5].priceDelta < 0.0f);
}

TEST(WorldEconomy, LookupRateScalar) {
    const guild::i8 table[5] = {10, 20, 30, 40, 50};
    EconomySetRateScalarTable(table);
    CHECK_EQ((int)EconomyLookupRateScalar(0), 10);
    CHECK_EQ((int)EconomyLookupRateScalar(4), 50);
    CHECK_EQ((int)EconomyLookupRateScalar(5), 126);   // out of range -> 126
    CHECK_EQ((int)EconomyLookupRateScalar(99), 126);
}

// ---------------------------------------------------------------------------
// Tax formula vs python golden values.
// ---------------------------------------------------------------------------
TEST(WorldTax, TradeIncomeFormula) {
    CHECK_EQ(TaxComputeTradeIncome(10, 5000), 499);
    CHECK_EQ(TaxComputeTradeIncome(7, 1234), 86);
    CHECK_EQ(TaxComputeTradeIncome(10, -50), 0);    // clamped at 0
    CHECK_EQ(TaxComputeTradeIncome(0, 5000), 0);
}

static int g_capturedTax = -1;
static bool captureTaxHook(guild::i32, guild::i32, guild::i32 amount) {
    g_capturedTax = amount;
    return true;
}

TEST(WorldTax, CollectTradeRoutesThroughCommandQueue) {
    g_capturedTax = -1;
    TaxSetCommitHook(&captureTaxHook);
    TradeTaxResult res;
    guild::i32 city = 0;
    // flags 1|2 -> queue command + add to city accumulator.
    int rc = TaxCollectTradeIncome(/*thr*/10, /*activeLaws*/2, /*acct*/5000,
                                   /*obj*/77, /*payer*/88, /*flags*/3, &res, &city);
    CHECK_EQ(rc, 1);
    CHECK_EQ(res.amount, 499);
    CHECK(res.committed);
    CHECK_EQ(g_capturedTax, 499);
    CHECK_EQ(city, 499);
    TaxSetCommitHook(nullptr);
}

TEST(WorldTax, CollectTradeLawSlotFull) {
    TradeTaxResult res;
    int rc = TaxCollectTradeIncome(10, /*activeLaws*/6, 5000, 0, 0, 3, &res, nullptr);
    CHECK_EQ(rc, 0);            // >=6 active finance laws -> bail
    CHECK_EQ(res.amount, 0);
}

// ---------------------------------------------------------------------------
// Production output-over-time vs python golden values.
// ---------------------------------------------------------------------------
TEST(WorldProduction, OutputSameDay) {
    ProdTime s{0, 10, 0}, e{0, 15, 0};
    CHECK_EQ(ProductionComputeOutputOverTime(s, e, false), 300);  // 10:00->15:00 in [8,20]
}

TEST(WorldProduction, OutputClampedBeforeOpen) {
    ProdTime s{0, 6, 0}, e{0, 9, 0};
    CHECK_EQ(ProductionComputeOutputOverTime(s, e, false), 60);   // clamp to [8,9]
}

TEST(WorldProduction, OutputMultiDay) {
    ProdTime s{0, 18, 0}, e{2, 10, 0};
    CHECK_EQ(ProductionComputeOutputOverTime(s, e, false), 960);
}

TEST(WorldProduction, OutputStartAfterEnd) {
    ProdTime s{0, 15, 0}, e{0, 10, 0};
    CHECK_EQ(ProductionComputeOutputOverTime(s, e, false), 0);
}

TEST(WorldProduction, OutputPauseModeIsPlainDiff) {
    ProdTime s{0, 6, 0}, e{0, 9, 0};
    // pause mode ignores the window: plain minute diff = 180.
    CHECK_EQ(ProductionComputeOutputOverTime(s, e, true), 180);
}

TEST(WorldProduction, DailyHourOutput) {
    ProdTime s{0, 8, 0}, e{0, 12, 0};
    CHECK_EQ(ProductionComputeDailyHourOutput(s, e), 240);    // [8,12] in [6,23]
    ProdTime s2{0, 20, 0}, e2{1, 8, 0};
    CHECK_EQ(ProductionComputeDailyHourOutput(s2, e2), 300);  // 180 + 120
}
