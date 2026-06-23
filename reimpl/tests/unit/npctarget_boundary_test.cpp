// Wave-12 hardening — boundary / malformed-input tests for the NpcTarget combat /
// move-direction queries (npctarget.cpp). Focus: the candidate-collection buffers
// (collectSuccessors -> cand[5], collectByCategory -> ids[6]) must never be read
// past their length even if a hook reports an over-large count; the empty / null /
// no-office paths return cleanly. Built under ASAN+UBSAN.
#include "test.h"

#include "sim/npctarget.h"

using namespace guild;
using namespace guild::sim;

namespace {
// --- shared synthetic person model -----------------------------------------
struct Person { u8 kind; i32 id; u8 catA, catB, catC, sub; };

u8  HKind(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->kind : 0; }
i32 HId(PersonHandle p)    { return p ? reinterpret_cast<Person*>(p)->id   : -1; }
u8  HCatA(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catA : 0; }
u8  HCatB(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catB : 0; }
u8  HCatC(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catC : 0; }
u8  HSub(PersonHandle p)   { return p ? reinterpret_cast<Person*>(p)->sub  : 0; }
double HFav(u16, u16, int) { return 50.0; }

// A collector that LIES about its count (claims more than `max`). The hardened
// query must clamp to the buffer length and not read the unwritten tail.
int g_lieClaim = 0;
int CollectSuccessorsLies(u8, int max, PersonHandle* out) {
    // Only fill `max` slots (as a well-behaved collector would), but RETURN a
    // number larger than max to probe the bound.
    for (int i = 0; i < max; ++i) out[i] = nullptr;
    return g_lieClaim;
}
int CollectByCategoryLies(u8, int max, i32* outIds) {
    for (int i = 0; i < max; ++i) outIds[i] = -1;
    return g_lieClaim;
}
u8  HRankOk(u8, bool* ok) { if (ok) *ok = true; return 1; }
} // namespace

// --- FindNearestEnemy: oversized collector count must be clamped -----------
TEST(NpcTargetBoundary, FindNearestEnemyOversizedSuccessorCount) {
    Person self{ /*kind*/3, /*id*/7, /*A*/0, 0, /*C*/5, 0 };
    NpcTargetHooks h{};
    h.personKind = &HKind; h.personId = &HId;
    h.officeCatA = &HCatA; h.officeCatB = &HCatB; h.officeCatC = &HCatC;
    h.favorability = &HFav;
    h.collectSuccessors = &CollectSuccessorsLies; // claims g_lieClaim entries
    SetNpcTargetHooks(&h);

    g_lieClaim = 99;                  // way past the cand[5] buffer
    PersonHandle e = NpcTarget_FindNearestEnemy(&self);
    CHECK_EQ(e, static_cast<PersonHandle>(nullptr)); // all-null cands -> none

    g_lieClaim = -3;                  // negative count must not underflow
    e = NpcTarget_FindNearestEnemy(&self);
    CHECK_EQ(e, static_cast<PersonHandle>(nullptr));

    SetNpcTargetHooks(nullptr);
}

// --- FindNearestEnemy over an empty candidate set (no hooks -> inert) -------
TEST(NpcTargetBoundary, FindNearestEnemyEmpty) {
    Person self{ 3, 7, 0, 0, 5, 0 };
    SetNpcTargetHooks(nullptr);       // inert: every query reports "no data"
    CHECK_EQ(NpcTarget_FindNearestEnemy(&self), static_cast<PersonHandle>(nullptr));
}

// --- PickDirection*: oversized collectByCategory count must be clamped ------
TEST(NpcTargetBoundary, PickDirectionSeqAOversizedCategoryCount) {
    Person self{ 3, 7, 0, 0, /*C*/4, 0 };
    NpcTargetHooks h{};
    h.personKind = &HKind; h.personId = &HId;
    h.officeCatA = &HCatA; h.officeCatB = &HCatB; h.officeCatC = &HCatC;
    h.officeRank = &HRankOk;
    h.favorability = &HFav;
    h.collectByCategory = &CollectByCategoryLies;
    SetNpcTargetHooks(&h);

    g_lieClaim = 99;                  // past the ids[6] buffer
    u8 d = NpcTarget_PickDirectionSeqA(&self, 0); // must not OOB; fav 50 < 66 -> picks
    CHECK(d <= 6);

    g_lieClaim = 99;
    u8 d2 = NpcTarget_PickDirectionSeqB(&self, 0);
    CHECK(d2 <= 6);

    SetNpcTargetHooks(nullptr);
}

// --- PickDirection with no office (rank lookup fails) -> 0 ------------------
TEST(NpcTargetBoundary, PickDirectionNoOffice) {
    Person self{ 3, 7, /*A*/9, 0, 0, 0 };  // catA set -> PickCategory returns it
    NpcTargetHooks h{};
    h.personKind = &HKind; h.personId = &HId;
    h.officeCatA = &HCatA; h.officeCatB = &HCatB; h.officeCatC = &HCatC;
    h.officeRank = [](u8, bool* ok) -> u8 { if (ok) *ok = false; return 0; }; // no such office
    SetNpcTargetHooks(&h);
    CHECK_EQ((int)NpcTarget_PickDirectionSeqA(&self, 0), 0);
    CHECK_EQ((int)NpcTarget_PickDirectionSeqB(&self, 0), 0);
    SetNpcTargetHooks(nullptr);
}

// --- EvalCombatOrMoveAction: blocked / busy / wrong-rank gates -> 0 ---------
TEST(NpcTargetBoundary, EvalCombatGatesReject) {
    Person self{ 3, 7, 0, 0, 5, /*sub*/30 };
    SetNpcTargetHooks(nullptr);
    NpcActionDesc out{};
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&self, /*busy*/true,  false, 1, &out), 0);
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&self, false, /*blocked*/true,  1, &out), 0);
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&self, false, false, /*rank*/2, &out), 0);
}

// --- EvalCombatOrMoveAction: unknown sub-method -> 0 -----------------------
TEST(NpcTargetBoundary, EvalCombatUnknownSubMethod) {
    Person self{ 3, 7, 0, 0, 5, /*sub*/99 };  // not 30/31/32/33
    NpcTargetHooks h{};
    h.personKind = &HKind; h.personId = &HId;
    h.officeCatA = &HCatA; h.officeCatC = &HCatC;
    h.subMethod = &HSub;
    SetNpcTargetHooks(&h);
    NpcActionDesc out{};
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&self, false, false, 1, &out), 0);
    SetNpcTargetHooks(nullptr);
}
