// End-to-end flow for the Personnel/Recruit remainder + Animal/Plant + Avatar
// module: recruit a candidate (cost + eligibility), "hire" staff (record an
// employer binding via a mocked command sink), assign an avatar, then run a
// season of animal + plant updates and verify the resulting staff / animal /
// plant / avatar state against a reference.
#include "sim/recruit_cost.h"
#include "sim/recruit.h"
#include "sim/animal.h"
#include "sim/plant.h"
#include "sim/avatar.h"
#include "sim/person.h"
#include "sim/entity.h"
#include "crt/rand.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;

namespace {

Person* MakePerson(int idx, i32 id, u8 kind = 6) {
    Person& p = g_persons[idx];
    std::memset(&p, 0, sizeof(p));
    p.marker = static_cast<i16>(idx);
    PersonSetByte(&p, kPfKind, kind);
    PersonSetDword(&p, kPfId, id);
    PersonSetByte(&p, kPfIsPlayer, 1);
    PersonSetDword(&p, kPfRelationBase, -1);  // unbound employer
    g_personIds[idx] = id;
    g_personArrayLoaded = true;
    return &p;
}

// Mock "hire command" sink: the real hire routes through the deterministic
// command channel (ExAssignPersonToOffice etc.); here we record the binding.
struct HireCommandSink {
    struct Hire { i32 employerId; i32 staffId; int fee; };
    std::vector<Hire> hires;
    void Hire_(i32 employerId, i32 staffId, int fee) {
        hires.push_back({employerId, staffId, fee});
    }
};

// Cost-formula hooks.
int WealthHook(u16 marker, const Person*) {
    if (marker == 0) return 2000;   // employer (recruiter)
    if (marker == 1) return 600;    // candidate
    return 0;
}
double FavorHook(int, int, int) { return 50.0; }
int DebugNoHalve(int, const Person*, void*) { return 0; }

struct MockAnimalWorld : IAnimalWorld {
    int spawns = 0, destroys = 0, updates = 0;
    i32 next = 9000;
    i32 SpawnAnimal(u8, float* x, float* y, float* z) override {
        ++spawns; *x = *y = *z = 0; return next++;
    }
    void UpdateAnimal(AnimalRec*) override { ++updates; }
    void DestroyAnimal(i32) override { ++destroys; }
};

struct MockPlantWorld : IPlantWorld {
    int releases = 0, loads = 0;
    i32 next = 700;
    void ReleaseModel(i32) override { ++releases; }
    i32 LoadModel(PlantRec*) override { ++loads; return next++; }
    void SetModelVisible(i32, bool) override {}
};
u8 MaxStage5(u16) { return 5; }

} // namespace

TEST(SimPersonnelE2E, RecruitHireAssignThenWorldTick) {
    // ---- Setup persons: an employer (slot 0) and a candidate (slot 1). ----
    ResetEntityArrays();
    ResetAvatars();
    RecruitSetFocusRecord(nullptr);
    PersonSetOfficeDefinitionHook(nullptr);
    RecruitSetTotalWealthHook(&WealthHook);
    RecruitSetFavorabilityHook(&FavorHook);
    RecruitSetCostDebugHook(&DebugNoHalve);

    MakePerson(0, 100);
    Person* candidate = MakePerson(1, 200);
    PersonSetWord(candidate, kPfCash, 60);
    // reputation bytes candidate[0x80..0x84] = {180,180,0,0,0} -> +2 (two >168).
    PersonSetByte(candidate, 0x80, 180);
    PersonSetByte(candidate, 0x81, 180);

    // ---- Step 1: eligibility. Empty filter -> eligible. ----
    CandidateFilter filter{};
    filter.a4 = 0;
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &filter), 1);

    // ---- Step 2: recruitment cost. ----
    // a = 600/2000*5+1 = 2.5; b = (100-50)*0.1 = 5.0; feeF = 24/60*(7.5)=3.0
    // -> lrint 3; rep bonus +2 (180,180) => 5.
    int fee = RecruitComputeRecruitmentCost(100, 200);
    CHECK_EQ(fee, 5);

    // ---- Step 3: proximity check (rank window). ----
    CHECK_EQ(RecruitCheckRecruitProximity(100, 200), 1);

    // ---- Step 4: "hire" via the mocked command sink + bind employer. ----
    HireCommandSink sink;
    sink.Hire_(/*employer*/ 100, /*staff*/ 200, fee);
    PersonSetDword(candidate, kPfRelationBase, 100);   // ExAssignPersonToOffice
    PersonSetByte(candidate, kPfRecruited, 1);          // now off the market
    CHECK_EQ(sink.hires.size(), (size_t)1);
    CHECK_EQ(sink.hires[0].employerId, 100);
    CHECK_EQ(sink.hires[0].staffId, 200);
    CHECK_EQ(sink.hires[0].fee, 5);
    // A second eligibility pass now rejects the hired candidate (recruited flag).
    CHECK_EQ(PersonEvaluateCandidateEligibility(0, 1, &filter), 0);

    // ---- Step 5: assign an avatar to the new staff member. ----
    u16 owned[1] = {/*scene-entity id*/ 321};
    AvatarEntry* av = Avatar_FindOrAllocForPerson(owned, 1);
    CHECK(av != nullptr);
    av->ownerId = 321;                                  // bind it
    CHECK(Avatar_LookupById(321) == av);

    // ---- Step 6: run a world tick — animals spawn/despawn, plants grow. ----
    ResetAnimalPool();
    Animal_AllocPool();
    MockAnimalWorld aw;
    SetAnimalWorld(&aw);
    guild::crt::Srand(12345);
    g_gameTick = 100000;
    g_weatherState = 2;                                 // heavy -> cap 32
    g_animalLastSpawnTick = 0;                          // 0+350 < 100000 -> spawn gate open

    // Seed a couple of old animals that should despawn over winter.
    g_gameTick = 100000;
    AnimalRec* a0 = Animal_AllocSlot(); a0->actor = 11; a0->kind = kAnimalSheep;
    a0->spawnTick = 1000;                               // very old
    AnimalRec* a1 = Animal_AllocSlot(); a1->actor = 12; a1->kind = kAnimalCat;
    a1->spawnTick = 1000;
    CHECK_EQ(g_animalCount, 2);

    // Run 32 winter ticks (one full cursor sweep): every live animal is updated
    // and (winter) despawned. Spawning is suppressed in winter (season==3).
    for (int t = 0; t < kAnimalPoolCapacity; ++t)
        Animal_Update(/*season winter*/ 3);
    CHECK_EQ(aw.spawns, 0);                             // winter: no spawns
    CHECK(aw.updates >= 2);                             // both animals stepped
    CHECK_EQ(g_animalCount, 0);                         // all despawned
    CHECK_EQ(aw.destroys, 2);

    // ---- Step 7: plant plot grows one stage per growth tick. ----
    ResetPlant();
    PlantSetMaxStageHook(&MaxStage5);
    MockPlantWorld pw;
    SetPlantWorld(&pw);
    PlantRec plot[kPlantPlotCapacity];
    std::memset(plot, 0xFF, sizeof(plot));             // all empty
    plot[0].stage = 0; plot[0].model = 0; plot[0].typeId = 4;
    plot[1].stage = 2; plot[1].model = 88; plot[1].typeId = 4;

    Plant_AdvanceGrowthStage(plot);
    CHECK_EQ(plot[0].stage, (u8)1);
    CHECK_EQ(plot[1].stage, (u8)3);
    CHECK_EQ(pw.releases, 1);                           // plot[1] had a model
    CHECK(plot[0].model != 0 && plot[1].model != 0);   // both (re)loaded

    // ---- Final reference assertions: the hired staff is bound + recruited,
    // its avatar resolves, animals cleared, plants advanced. ----
    CHECK_EQ(PersonGetDword(candidate, kPfRelationBase), 100);
    CHECK_EQ(PersonGetByte(candidate, kPfRecruited), (u8)1);
    CHECK(Avatar_LookupById(321) != nullptr);
    CHECK_EQ(g_animalCount, 0);
    CHECK_EQ(plot[0].stage, (u8)1);

    Animal_FreePool();
}
