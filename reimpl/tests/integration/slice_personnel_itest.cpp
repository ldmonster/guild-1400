// tests/integration/slice_personnel_itest.cpp — the PERSONNEL slice on a small
// real-FORMAT world (synthetic person records in the live sim arrays, no shipped
// assets), driving the REAL recruit rules core + REAL economy day.
//
// Asserts:
//   * a real folded person field (candidate employer +92) CHANGED pre/post hire,
//   * HashFullWorld() differs across the command and across the whole day,
//   * the run is DETERMINISTIC (a rerun produces byte-identical hashes),
//   * persons live in g_persons/g_personIds — both are zeroed before seeding and
//     re-anchored before each compare (the play-layer ZeroWorldGlobals/Srand rule).
#include "test.h"

#include "play/slice_personnel.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/person_personnel2.h"
#include "sim/recruit.h"
#include "sim/types.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// Seed a small real-FORMAT person roster: slot 0 master (kind 6), workers after.
void SeedPersons(int n) {
    std::memset(g_persons, 0, sizeof(g_persons));
    std::memset(g_personIds, 0, sizeof(g_personIds));
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_sceneNodes, 0, sizeof(g_sceneNodes));
    ResetEntityArrays();
    for (int i = 0; i < n && i < kPersonCapacity; ++i) {
        Person& p = g_persons[i];
        std::memset(&p, 0, kPersonStride);
        p.marker = (i16)i;
        i32 id = 7000 + i;
        PersonSetDword(&p, kPfId, id);
        g_personIds[i] = id;
        PersonSetByte(&p, kPfIsPlayer, 1);
        PersonSetByte(&p, kPfKind, (u8)(i == 0 ? 6 : 1));
        PersonSetWord(&p, kPfCash, (i16)(300 + 25 * i));
        PersonSetDword(&p, kPfRelationBase, -1);
        PersonSetDword(&p, kPfSuperiorId, -1);
    }
    g_personArrayLoaded = n > 0;
    g_sceneArrayLoaded  = true;
}

} // namespace

// ---------------------------------------------------------------------------
// The real rules core accepts the hire and a folded field changes pre/post.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelItest, HireMutatesFoldedPersonFieldDeterministic) {
    const i32 master = 7000, worker = 7001;

    SeedPersons(6);
    crt::Srand(0xCAFE);
    std::uint64_t hBefore = HashFullWorld();

    // The candidate is unbound before the hire.
    Person* candPre = PersonFindRecordById(worker);
    CHECK(candPre != nullptr);
    i32 empBefore = candPre ? PersonGetDword(candPre, kPnEmployerOff) : -999;
    CHECK_EQ(empBefore, -1);
    // Real proximity rules-core: candidate eligible.
    int prox = RecruitCheckRecruitProximity(master, worker);
    std::printf("[pers-it] proximity(master,worker)=%d empBefore=%d\n", prox, empBefore);
    CHECK(prox >= 0);

    // --- issue the hire through the real validate+cost+apply path ---
    PersonnelClick c;
    c.action = PersonnelAction::kHire; c.recruiterId = master; c.candidateId = worker;
    SetPersonnelApplyHooks(nullptr);
    PersonnelOrder o = IssuePersonnelClick(c);
    std::printf("[pers-it] issued=%d fee=%d candEmployerAfter=%d employmentAfter=%d\n",
                (int)o.issued, o.fee, o.candEmployerAfter, o.employmentAfter);
    CHECK(o.issued);
    CHECK(o.applied);
    CHECK_EQ(o.candEmployerAfter, master);

    crt::Srand(0xCAFE);
    std::uint64_t hAfterCmd = HashFullWorld();
    CHECK(hAfterCmd != hBefore);   // the folded g_persons changed

    // --- a real economy day (RunPersonnelSliceSynthetic seeds its OWN roster with
    // ids 5000+slot, so drive its hire against THOSE ids: recruiter 5000, worker
    // 5001) ---
    PersonnelClick sc;
    sc.action = PersonnelAction::kHire; sc.recruiterId = 5000; sc.candidateId = 5001;
    PersonnelSliceResult r =
        RunPersonnelSliceSynthetic(0x4242, 6, sc, 0xCAFE);  // re-seeds + runs the day
    std::printf("[pers-it] hashAfterSeed=%llu hashAfterCommand=%llu hashAfterDay=%llu "
                "econPasses=%d\n",
                (unsigned long long)r.hashAfterSeed,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay, r.economyPasses);
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.economyPasses > 0);

    // --- determinism: a full rerun is byte-identical ---
    PersonnelSliceResult r2 =
        RunPersonnelSliceSynthetic(0x4242, 6, sc, 0xCAFE);
    CHECK_EQ(r.hashAfterSeed,    r2.hashAfterSeed);
    CHECK_EQ(r.hashAfterCommand, r2.hashAfterCommand);
    CHECK_EQ(r.hashAfterDay,     r2.hashAfterDay);

    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// FindEmploymentRelation sees the binding once a master references the candidate.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelItest, EmploymentRelationReflectsBinding) {
    const i32 master = 7000, worker = 7001;
    SeedPersons(4);

    // Make the master a kind-6 employer that lists the worker in its relation
    // slot 0 (+92) — exactly the structural link FindEmploymentRelation scans for.
    Person* m = PersonFindRecordById(master);
    CHECK(m != nullptr);
    if (m) {
        PersonSetByte(m, kPfKind, 6);
        PersonSetDword(m, kPf2RelArray + 0, worker);  // +92 employer-of relation
    }
    Person* w = PersonFindRecordById(worker);
    CHECK(w != nullptr);
    if (w) {
        int empRel = PersonFindEmploymentRelation(w);
        std::printf("[pers-it] FindEmploymentRelation(worker)=%d (0 == bound)\n", empRel);
        CHECK_EQ(empRel, 0);   // a master references the worker -> employed
    }
    ResetEntityArrays();
}
