// Wave-14 1:1 golden pins — NpcTarget combat / move-direction queries
// (src/sim/npctarget.cpp, VIBE_NpcTarget_* @0x474dd4 / 0x474fe8 / 0x475274 /
// 0x475638). These pin the RECOVERED constants and accept rules that the
// wave-12 boundary suite exercised but did not value-assert:
//   - FindNearestEnemy reject curve  count*8.0 + 52.0      (flt_61A680/flt_61A684)
//   - PickDirectionSeqA accept "< 66.0"                    (flt_61A688)
//   - PickDirectionSeqB pairwise accept "> 33.0"           (flt_61A68C)
//   - EvalCombatOrMoveAction sub-method -> verb/mode/return dispatch:
//       30 -> verb 7  mode 1 (attack), 32/31/33 -> verb 22 mode 4 (move), ret 56.
// Every asserted value traces to the source recovery / npctarget.h provenance.
#include "test.h"

#include "sim/npctarget.h"

using namespace guild;
using namespace guild::sim;

namespace {
struct Person { u8 kind; i32 id; u8 catA, catB, catC, sub; };

u8  GKind(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->kind : 0; }
i32 GId(PersonHandle p)    { return p ? reinterpret_cast<Person*>(p)->id   : -1; }
u8  GCatA(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catA : 0; }
u8  GCatB(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catB : 0; }
u8  GCatC(PersonHandle p)  { return p ? reinterpret_cast<Person*>(p)->catC : 0; }
u8  GSub(PersonHandle p)   { return p ? reinterpret_cast<Person*>(p)->sub  : 0; }
u8  GRankOk(u8, bool* ok)  { if (ok) *ok = true; return 1; }

// A controllable favorability: returns g_fav for every (a,b) query.
double g_fav = 50.0;
double GFavConst(u16, u16, int) { return g_fav; }

// Three candidate persons resolved by id 1/2/3, all distinct from self (id 7).
Person g_c1{ 11, 1, 0, 0, 0, 0 };
Person g_c2{ 12, 2, 0, 0, 0, 0 };
Person g_c3{ 13, 3, 0, 0, 0, 0 };
PersonHandle GFindById(i32 id) {
    if (id == 1) return &g_c1;
    if (id == 2) return &g_c2;
    if (id == 3) return &g_c3;
    return nullptr;
}
// Always hands the first direction three resolvable candidate ids (>=3 so the
// average gate engages on dir[0..k]); the picked direction value is dir[k].
int GCollectThree(u8, int max, i32* out) {
    int n = max < 3 ? max : 3;
    out[0] = 1; if (n > 1) out[1] = 2; if (n > 2) out[2] = 3;
    return n;
}
// FindNearestEnemy candidate ring: hand exactly one candidate (count == 1) so
// the reject curve is 1*8 + 52 == 60.
Person g_enemy{ 9, 99, 0, 0, 0, 0 };
int GCollectOne(u8, int max, PersonHandle* out) {
    (void)max; out[0] = &g_enemy; return 1;
}

NpcTargetHooks MakeHooks() {
    NpcTargetHooks h{};
    h.personKind = &GKind; h.personId = &GId;
    h.officeCatA = &GCatA; h.officeCatB = &GCatB; h.officeCatC = &GCatC;
    h.subMethod  = &GSub;  h.officeRank = &GRankOk;
    h.favorability = &GFavConst;
    h.findPersonById = &GFindById;
    h.collectByCategory = &GCollectThree;
    h.collectSuccessors = &GCollectOne;
    return h;
}
} // namespace

// --- PickDirectionSeqA accept threshold == 66.0 (strict "<") ----------------
TEST(NpcTargetGolden, SeqAThreshold66) {
    Person self{ 3, 7, 0, 0, /*C*/4, 0 };   // catC set -> office tier path
    NpcTargetHooks h = MakeHooks();
    SetNpcTargetHooks(&h);

    // avg favorability == g_fav for all three candidates.
    g_fav = 65.0;                 // 65 < 66 -> a direction is accepted (nonzero)
    CHECK(NpcTarget_PickDirectionSeqA(&self, 0) != 0);

    g_fav = 66.0;                 // 66 < 66 is FALSE -> no direction qualifies
    CHECK_EQ((int)NpcTarget_PickDirectionSeqA(&self, 0), 0);

    g_fav = 100.0;                // well above -> reject
    CHECK_EQ((int)NpcTarget_PickDirectionSeqA(&self, 0), 0);

    SetNpcTargetHooks(nullptr);
}

// --- PickDirectionSeqB pairwise accept threshold == 33.0 (strict ">") -------
TEST(NpcTargetGolden, SeqBThreshold33) {
    Person self{ 3, 7, 0, 0, /*C*/4, 0 };
    NpcTargetHooks h = MakeHooks();
    SetNpcTargetHooks(&h);

    // Pairwise average is also g_fav (symmetric constant Fav).
    g_fav = 34.0;                 // 34 > 33 -> accepted
    CHECK(NpcTarget_PickDirectionSeqB(&self, 0) != 0);

    g_fav = 33.0;                 // 33 > 33 is FALSE -> reject
    CHECK_EQ((int)NpcTarget_PickDirectionSeqB(&self, 0), 0);

    g_fav = 0.0;                  // far below -> reject
    CHECK_EQ((int)NpcTarget_PickDirectionSeqB(&self, 0), 0);

    SetNpcTargetHooks(nullptr);
}

// --- FindNearestEnemy reject curve count*8 + 52 (== 60 for one candidate) ---
TEST(NpcTargetGolden, FindNearestEnemyRejectCurve) {
    Person self{ 3, 7, /*A==0 -> ring path*/0, 0, /*C*/4, 0 };
    g_enemy = Person{ 9, 99, 0, 0, 0, 0 };
    NpcTargetHooks h = MakeHooks();
    h.collectSuccessors = &GCollectOne;   // exactly one candidate -> n == 1
    SetNpcTargetHooks(&h);

    // score < 60 -> keep the enemy (no visible fallback installed).
    g_fav = 59.0;
    CHECK_EQ(NpcTarget_FindNearestEnemy(&self), static_cast<PersonHandle>(&g_enemy));

    // score == 60: curve is `60 < score` -> 60 < 60 false -> NOT rejected (kept).
    g_fav = 60.0;
    CHECK_EQ(NpcTarget_FindNearestEnemy(&self), static_cast<PersonHandle>(&g_enemy));

    // score > 60 -> "too friendly", rejected; no visible fallback -> null.
    g_fav = 61.0;
    CHECK_EQ(NpcTarget_FindNearestEnemy(&self), static_cast<PersonHandle>(nullptr));

    SetNpcTargetHooks(nullptr);
}

// --- EvalCombatOrMoveAction sub-method dispatch (verb/mode/target/return) ----
TEST(NpcTargetGolden, EvalCombatDispatchMapping) {
    NpcTargetHooks h = MakeHooks();

    // sub 30 -> attack: FindNearestEnemy hit -> verb 7, mode 1, target enemy id, 56.
    Person s30{ 3, 7, /*A*/0, 0, /*C*/4, /*sub*/30 };
    g_enemy = Person{ 9, 99, 0, 0, 0, 0 };
    g_fav = 10.0;                 // < 60 -> enemy kept
    SetNpcTargetHooks(&h);
    NpcActionDesc o30{};
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&s30, false, false, 1, &o30), 56);
    CHECK_EQ((int)o30.verb, 7);
    CHECK_EQ(o30.mode, 1);
    CHECK_EQ(o30.target, 99);     // IdOf(enemy)

    // sub 32 -> move via SeqA: verb 22, mode 4, target == accepted direction (1..6).
    Person s32{ 3, 7, 0, 0, /*C*/4, /*sub*/32 };
    g_fav = 10.0;                 // < 66 -> a direction accepted
    NpcActionDesc o32{};
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&s32, false, false, 1, &o32), 56);
    CHECK_EQ((int)o32.verb, 22);
    CHECK_EQ(o32.mode, 4);
    CHECK(o32.target >= 1 && o32.target <= 6);

    // sub 31 and 33 -> move via SeqB: same verb 22 / mode 4.
    for (u8 sub : {(u8)31, (u8)33}) {
        Person sB{ 3, 7, 0, 0, 4, sub };
        g_fav = 50.0;             // > 33 -> accepted
        NpcActionDesc oB{};
        CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&sB, false, false, 1, &oB), 56);
        CHECK_EQ((int)oB.verb, 22);
        CHECK_EQ(oB.mode, 4);
        CHECK(oB.target >= 1 && oB.target <= 6);
    }

    SetNpcTargetHooks(nullptr);
}
