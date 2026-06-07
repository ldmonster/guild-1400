// Unit tests for the Personnel/Recruit remainder + Animal/Plant + Avatar module:
//   src/sim/recruit_cost.{h,cpp}  (recruitment cost + candidate eligibility)
//   src/sim/animal.{h,cpp}        (animal pool + spawn/despawn rules)
//   src/sim/plant.{h,cpp}         (plant growth)
//   src/sim/avatar.{h,cpp}        (avatar registry lookups)
//   src/sim/personnel.{h,cpp}     (wage formula) + recruit.{h,cpp} (proximity)
#include "sim/recruit_cost.h"
#include "sim/animal.h"
#include "sim/plant.h"
#include "sim/avatar.h"
#include "sim/personnel.h"
#include "sim/recruit.h"
#include "sim/person.h"
#include "sim/entity.h"
#include "crt/rand.h"
#include "test.h"

#include <cstring>

using namespace guild::sim;
using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;

namespace {

Person* MakePerson(int idx, i32 id, u8 kind = 6, u8 isPlayer = 1) {
    Person& p = g_persons[idx];
    std::memset(&p, 0, sizeof(p));
    p.marker = static_cast<i16>(idx);
    PersonSetByte(&p, kPfKind, kind);
    PersonSetDword(&p, kPfId, id);
    PersonSetByte(&p, kPfIsPlayer, isPlayer);
    g_personIds[idx] = id;
    g_personArrayLoaded = true;
    return &p;
}

// ---- recruitment cost hooks (golden-vector backend) ----
int g_wealthRecruiter = 1000;
int g_wealthCandidate = 500;
double g_favor = 40.0;

int WealthHook(u16 marker, const Person* /*owner*/) {
    if (marker == 0) return g_wealthRecruiter;  // recruiter slot
    if (marker == 1) return g_wealthCandidate;  // candidate slot
    return 0;
}
double FavorHook(int /*a*/, int /*b*/, int /*mode*/) { return g_favor; }
int DebugNoHalve(int, const Person*, void*) { return 0; }
int DebugHalve(int, const Person*, void*) { return 2; }

} // namespace

// ===========================================================================
// Personnel wage (existing rule, regression).
// ===========================================================================
TEST(SimPersonnelW, WageByCategory) {
    PersonnelSetBuildingBaseValueHook(
        [](int, int, int, int) -> double { return 100.0; });
    // admin categories 10/11/12 -> *9, else *3.
    PersonnelSetActionCategoryHook([](u8) -> u8 { return 11; });
    CHECK_EQ(PersonnelComputeWageByCategory(0, 0, 0, 0), 900.0);
    PersonnelSetActionCategoryHook([](u8) -> u8 { return 2; });
    CHECK_EQ(PersonnelComputeWageByCategory(0, 0, 0, 0), 300.0);
}

// ===========================================================================
// Recruitment cost — golden vector (seeded; hooks deterministic).
// ===========================================================================
TEST(SimRecruitCost, GoldenFee) {
    ResetEntityArrays();
    RecruitSetTotalWealthHook(&WealthHook);
    RecruitSetFavorabilityHook(&FavorHook);
    RecruitSetCostDebugHook(&DebugNoHalve);
    g_wealthRecruiter = 1000;
    g_wealthCandidate = 500;
    g_favor = 40.0;

    Person* rec  = MakePerson(0, 1);
    Person* cand = MakePerson(1, 2);
    // candidate +0x60 / +0x64 links -> none (FindRecordById null) => w0=w1=0.
    PersonSetDword(cand, 0x60, 0);
    PersonSetDword(cand, 0x64, 0);
    PersonSetDword(rec, 0x60, 0);
    // GetCashAmount(candidate) reads the +0x0A cash word; set to 80.
    PersonSetWord(cand, kPfCash, 80);
    // reputation bytes cand[0x80..0x84] = {200,170,100,0,220}.
    PersonSetByte(cand, 0x80, 200);
    PersonSetByte(cand, 0x81, 170);
    PersonSetByte(cand, 0x82, 100);
    PersonSetByte(cand, 0x83, 0);
    PersonSetByte(cand, 0x84, 220);

    // feeF = 24/80 * (a+b); a = 500/1000*5+1 = 3.5; b = (100-40)*0.1 = 6.0;
    // feeF = 0.3*9.5 = 2.85 -> lrint = 3; rep bonus 200>168(+1),170>168(+1),
    // 220>210(+2) = +4 => 7.
    CHECK_EQ(RecruitComputeRecruitmentCost(1, 2), 7);
}

TEST(SimRecruitCost, DebugHalveAndClamp) {
    ResetEntityArrays();
    RecruitSetTotalWealthHook(&WealthHook);
    RecruitSetFavorabilityHook(&FavorHook);
    RecruitSetCostDebugHook(&DebugHalve);
    g_wealthRecruiter = 1000;
    g_wealthCandidate = 500;
    g_favor = 40.0;

    MakePerson(0, 1);
    Person* cand = MakePerson(1, 2);
    PersonSetWord(cand, kPfCash, 80);
    // No reputation bonus this time -> fee=3, halved => 1 -> clamp to 2.
    CHECK_EQ(RecruitComputeRecruitmentCost(1, 2), 2);
}

TEST(SimRecruitCost, NotFoundReturnsZero) {
    ResetEntityArrays();
    RecruitSetTotalWealthHook(&WealthHook);
    CHECK_EQ(RecruitComputeRecruitmentCost(99, 100), 0);
}

// ===========================================================================
// Candidate eligibility filter cascade.
// ===========================================================================
TEST(SimEligibility, NullFilterEligible) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    MakePerson(0, 1);
    MakePerson(1, 2);
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, nullptr), 1);
}

TEST(SimEligibility, SelfIneligible) {
    ResetEntityArrays();
    MakePerson(3, 5);
    CHECK_EQ(PersonEvaluateCandidateEligibility(3, 3, nullptr), 0);
}

TEST(SimEligibility, RecruitedOrDeadCandidate) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    MakePerson(0, 1);
    Person* cand = MakePerson(1, 2);
    PersonSetByte(cand, kPfRecruited, 1);   // already recruited
    CandidateFilter f{};
    f.a4 = 0;                            // empty -> would be eligible, but...
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &f), 0);
}

TEST(SimEligibility, GenderGate) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    MakePerson(0, 1);
    Person* cand = MakePerson(1, 2);
    PersonSetByte(cand, kPfGender, 1);       // gender low byte == 1
    CandidateFilter f{};
    f.a4 = 1;                            // bit0: reject gender==1
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &f), 0);
    f.a4 = 2;                            // bit1: reject gender==0
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &f), 1);
}

TEST(SimEligibility, KindTooHigh) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    MakePerson(0, 1);
    MakePerson(1, 2, /*kind*/ 12);           // kind >= 10
    CandidateFilter f{};
    f.a4 = 1;                            // any attribute gate triggers >=10 check
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &f), 0);
}

TEST(SimEligibility, ExcludeIdList) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    MakePerson(0, 1);
    MakePerson(5, 2);
    CandidateFilter f{};
    f.a4 = 0;        // empty attribute set -> eligible unless excluded list hits
    // exclude list only consulted when flagsA != 0; use a profession gate that passes.
    Person* cand = &g_persons[5];
    PersonSetByte(cand, kPfProfession, 1);
    PersonSetByte(cand, kPfKind, 0);
    f.a4 = 0x10;     // require profession && kind 0  (candidate passes)
    f.flagsE = 2;        // exclude-id list active
    f.excludeIds[0] = 5; // candidate slot index 5 excluded
    f.excludeIds[1] = 0xFFFF;
    f.excludeIds[2] = 0xFFFF;
    f.excludeIds[3] = 0xFFFF;
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 5, &f), 0);
    f.excludeIds[0] = 9; // not excluded now
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 5, &f), 1);
}

TEST(SimEligibility, OfficeRankDistance) {
    ResetEntityArrays();
    RecruitSetFocusRecord(nullptr);
    PersonSetOfficeDefinitionHook(nullptr);  // no office -> rank 1 for both
    MakePerson(0, 1);
    MakePerson(1, 2);
    CandidateFilter f{};
    f.a4 = 0;                 // skip attribute gates -> LABEL_62
    f.a7 = 8;                 // office-rank distance gate
    // span = (flagsA & 0x7C00000)>>22 == 0; |1-1| <= 0 -> eligible.
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &f), 1);
}

// ===========================================================================
// Animal pool + spawn-kind + update lifetime.
// ===========================================================================
namespace {
struct MockAnimalWorld : IAnimalWorld {
    int spawns = 0, destroys = 0, updates = 0;
    i32 nextActor = 1000;
    bool culled = false;
    i32 SpawnAnimal(u8, float* x, float* y, float* z) override {
        ++spawns; *x = 1; *y = 2; *z = 3; return nextActor++;
    }
    void UpdateAnimal(AnimalRec*) override { ++updates; }
    void DestroyAnimal(i32) override { ++destroys; }
    bool IsAnimalCulled(const AnimalRec*) override { return culled; }
};
} // namespace

TEST(SimAnimal, PoolAllocFree) {
    ResetAnimalPool();
    Animal_AllocPool();
    CHECK(g_animalPool != nullptr);
    CHECK_EQ(g_animalCount, 0);
    g_gameTick = 555;
    AnimalRec* a = Animal_AllocSlot();
    CHECK(a != nullptr);
    CHECK_EQ(g_animalCount, 1);
    CHECK_EQ(a->spawnTick, 555);
    a->actor = 1;                       // mark in-use
    MockAnimalWorld w;
    SetAnimalWorld(&w);
    Animal_FreeSlot(a);
    CHECK_EQ(g_animalCount, 0);
    CHECK_EQ(w.destroys, 1);
    CHECK_EQ(a->actor, 0);
    Animal_FreePool();
    CHECK(g_animalPool == nullptr);
}

TEST(SimAnimal, AllocSlotFullReturnsNull) {
    ResetAnimalPool();
    Animal_AllocPool();
    for (int i = 0; i < kAnimalPoolCapacity; ++i) {
        AnimalRec* a = Animal_AllocSlot();
        CHECK(a != nullptr);
        a->actor = i + 1;               // occupy
    }
    CHECK(Animal_AllocSlot() == nullptr);
    CHECK_EQ(g_animalCount, kAnimalPoolCapacity);
}

TEST(SimAnimal, SpawnKindTable) {
    // common branch (roll<=30): 0->Cat, 1->Dog.
    CHECK_EQ(Animal_SpawnKindForRoll(10, 0), (u8)kAnimalCat);
    CHECK_EQ(Animal_SpawnKindForRoll(10, 1), (u8)kAnimalDog);
    // rare branch: 0->Cow,1->Sheep,2->none,3->Pig,4->Horse.
    CHECK_EQ(Animal_SpawnKindForRoll(50, 0), (u8)kAnimalCow);
    CHECK_EQ(Animal_SpawnKindForRoll(50, 1), (u8)kAnimalSheep);
    CHECK_EQ(Animal_SpawnKindForRoll(50, 2), (u8)0xFF);
    CHECK_EQ(Animal_SpawnKindForRoll(50, 3), (u8)kAnimalPig);
    CHECK_EQ(Animal_SpawnKindForRoll(50, 4), (u8)kAnimalHorse);
}

TEST(SimAnimal, UpdateWinterDespawn) {
    ResetAnimalPool();
    Animal_AllocPool();
    MockAnimalWorld w;
    SetAnimalWorld(&w);
    g_gameTick = 10000;
    // Place a live animal at slot 0 (cursor starts at 0).
    AnimalRec* a = Animal_AllocSlot();
    a->actor = 7;
    a->kind = kAnimalCat;
    a->spawnTick = 5000;                // old (10000 > 5000+2100)
    a->despawn = 0;
    int before = g_animalCount;
    // Winter (season 3): forces despawn flag AND despawn condition (season==3).
    Animal_Update(3);
    CHECK_EQ(w.updates, 1);             // AI step ran
    CHECK_EQ(w.destroys, 1);            // despawned
    CHECK_EQ(g_animalCount, before - 1);
    CHECK_EQ(a->actor, 0);             // slot cleared
}

TEST(SimAnimal, UpdateSpawnThrottle) {
    ResetAnimalPool();
    Animal_AllocPool();
    MockAnimalWorld w;
    SetAnimalWorld(&w);
    guild::crt::Srand(1);
    g_gameTick = 100;
    g_animalLastSpawnTick = 0;          // 0+350 < 100 is FALSE -> no spawn attempt
    g_weatherState = 2;
    Animal_Update(0);
    CHECK_EQ(w.spawns, 0);              // throttled
}

// ===========================================================================
// Plant growth.
// ===========================================================================
namespace {
struct MockPlantWorld : IPlantWorld {
    int releases = 0, loads = 0, vis = 0, hides = 0;
    i32 nextModel = 500;
    void ReleaseModel(i32) override { ++releases; }
    i32 LoadModel(PlantRec*) override { ++loads; return nextModel++; }
    void SetModelVisible(i32, bool v) override { if (v) ++vis; else ++hides; }
};
u8 MaxStage3(u16) { return 3; }
} // namespace

TEST(SimPlant, GrowthAdvancesAndCaps) {
    ResetPlant();
    PlantSetMaxStageHook(&MaxStage3);
    MockPlantWorld w;
    SetPlantWorld(&w);

    PlantRec plot[kPlantPlotCapacity];
    std::memset(plot, 0xFF, sizeof(plot));  // all empty (stage 0xFF)
    // Make slot 0 live at stage 1 with a loaded model.
    plot[0].stage = 1;
    plot[0].model = 42;
    plot[0].typeId = 7;
    Plant_AdvanceGrowthStage(plot);
    CHECK_EQ(plot[0].stage, (u8)2);         // grew 1 -> 2 (cap 3)
    CHECK_EQ(w.releases, 1);                // old model released
    CHECK(plot[0].model != 0);              // reloaded by EnsureModelsLoaded
    CHECK_EQ(w.vis, 1);

    // Grow to the cap and stop.
    Plant_AdvanceGrowthStage(plot);
    CHECK_EQ(plot[0].stage, (u8)3);
    Plant_AdvanceGrowthStage(plot);
    CHECK_EQ(plot[0].stage, (u8)3);         // capped
}

TEST(SimPlant, EmptySlotsUntouched) {
    ResetPlant();
    MockPlantWorld w;
    SetPlantWorld(&w);
    PlantRec plot[kPlantPlotCapacity];
    std::memset(plot, 0xFF, sizeof(plot));  // all empty
    Plant_AdvanceGrowthStage(plot);
    CHECK_EQ(w.releases, 0);
    CHECK_EQ(w.loads, 0);
}

TEST(SimPlant, HideAllModels) {
    ResetPlant();
    MockPlantWorld w;
    SetPlantWorld(&w);
    PlantRec plot[kPlantPlotCapacity];
    std::memset(plot, 0xFF, sizeof(plot));
    plot[0].stage = 2; plot[0].model = 9;
    plot[1].stage = 0; plot[1].model = 0;   // live but no model
    Plant_HideAllModels(plot);
    CHECK_EQ(w.hides, 1);                    // only the one with a model
}

// ===========================================================================
// Avatar registry.
// ===========================================================================
TEST(SimAvatar, LookupById) {
    ResetAvatars();
    g_avatars[2].ownerId = 77;
    g_avatars[5].ownerId = 99;
    CHECK(Avatar_LookupById(77) == &g_avatars[2]);
    CHECK(Avatar_LookupById(99) == &g_avatars[5]);
    // id 0 finds the first free entry (no guard) — index 0.
    CHECK(Avatar_LookupById(0) == &g_avatars[0]);
    CHECK(Avatar_LookupById(123) == nullptr);
}

TEST(SimAvatar, FindOrAllocForPerson) {
    ResetAvatars();
    g_avatars[3].ownerId = 50;
    u16 owned[2] = {50, 51};
    // First owned id 50 matches existing avatar at slot 3.
    CHECK(Avatar_FindOrAllocForPerson(owned, 2) == &g_avatars[3]);
    // No match -> first free entry (slot 0).
    u16 none[1] = {200};
    CHECK(Avatar_FindOrAllocForPerson(none, 1) == &g_avatars[0]);
}

// ===========================================================================
// Recruit proximity (existing rule, regression).
// ===========================================================================
TEST(SimRecruitProx, RankWindow) {
    ResetEntityArrays();
    PersonSetOfficeDefinitionHook(nullptr);
    Person* rec  = MakePerson(0, 1);
    Person* cand = MakePerson(1, 2);
    PersonSetDword(rec, kPfRelationBase, -1);
    PersonSetDword(cand, kPfRelationBase, -1);
    // both rank 1 (no office) -> |1-1| < 5 -> 1.
    CHECK_EQ(RecruitCheckRecruitProximity(1, 2), 1);
}
