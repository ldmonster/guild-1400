// End-to-end flow across the office/law modules:
//   1. Build the promotion candidate list for a person (OfficeBuildPromotionList).
//   2. Take the winning candidate's office type, resolve its holder slot
//      (OfficeGetHolderEntryByCity).
//   3. Request a law-apply for that office context (GesetzRequestApply): the value
//      is clamped into the law record's [lo, hi] bounds and a command emitted.
//   4. Apply-and-notify mutates the live law-table threshold (GesetzApplyAndNotify).
// This exercises the recovered insertion sort, the holder lookup, and the law
// clamp/apply path together.
#include <cstring>

#include "test.h"

#include "world/gesetz_flow.h"
#include "world/law.h"
#include "world/law_apply2.h"
#include "world/office.h"

using namespace guild;
using namespace guild::world;

namespace {
OfficePerson Person(i32 owner, u8 office, u8 rank) {
    OfficePerson p{};
    p.ownerId = owner; p.officeType = office; p.rank = rank;
    p.candidacy = 0; p.valid = true;
    return p;
}
}  // namespace

TEST(LawApply2E2E, PromoteThenEnactLawFlow) {
    // --- Stage 0: holder table with a few promotable slots for person(off=1,rk=1).
    OfficeHolderTableReset();
    auto set = [](int i, int st, int rk, int ty, i32 city) {
        g_officeHolders[i].state = (guild::u8)st; g_officeHolders[i].rank = rk;
        g_officeHolders[i].type = (guild::u8)ty;  g_officeHolders[i].holder = (guild::u8)(ty + 10);
        g_officeHolders[i].city = city;
    };
    set(0, 3, 0, 8, 7000);  // cat2 cost -5.0
    set(1, 3, 1, 2, 7001);  // cat2 cost  0.0
    set(2, 3, 2, 5, 7002);  // cat2 cost -2.5

    OfficePerson p = Person(4242, 1, 1);

    // --- Stage 1: build the ranked promotion list.
    PromotionEntry list[6];
    int n = OfficeBuildPromotionList(p, 6, list);
    CHECK_EQ(n, 3);
    // The recovered ordering: [type5][type2][type8].
    CHECK_EQ(list[0].type, static_cast<int>(5));
    CHECK_EQ(list[1].type, static_cast<int>(2));
    CHECK_EQ(list[2].type, static_cast<int>(8));

    // --- Stage 2: resolve the top candidate's holder slot by its city/owner.
    // The top candidate is type 5, which lives in slot 2 (city 7002).
    OfficePerson candidate = Person(7002, 5, 2);
    OfficeDef    def{};
    OfficeHolder holder{};
    CHECK_EQ(OfficeGetHolderEntryByCity(candidate, &def, &holder), 1);
    CHECK_EQ(holder.type, static_cast<int>(5));
    CHECK_EQ(holder.city, 7002);

    // --- Stage 3: enact a law with a value outside the clamp bounds. Set a known
    // law record so the clamp math is deterministic: bounds [10, 100].
    LawTableResetDefaults();
    const guild::u8 lawId = 3;
    g_lawTable[lawId].penalty = 0;          // (+16) unused by clamp here
    // The clamp reads record bytes +4 (lo) and +8 (hi); LawField indexes those
    // raw dwords. Write them via the struct's overlapping fields:
    //   +4 == &id+4. We set them through memcpy-equivalent struct writes below.
    // LawRecord layout: id@0, pad1[15], penalty@16... +4 and +8 fall inside pad1.
    auto* raw = reinterpret_cast<guild::u8*>(&g_lawTable[lawId]);
    i32 lo = 10, hi = 100;
    std::memcpy(raw + 4, &lo, 4);
    std::memcpy(raw + 8, &hi, 4);

    GesetzCommandLogReset();
    GesetzPerson gp{};
    gp.present = true; gp.valid = true; gp.ownerId = p.ownerId;

    // value 500 > hi(100) -> clamps to 100.
    int r = GesetzRequestApply(lawId, 500, gp);
    CHECK_EQ(r, 0);
    int cmdCount = 0;
    const GesetzCommand* cmds = GesetzCommandLog(&cmdCount);
    CHECK_EQ(cmdCount, 1);
    CHECK_EQ(cmds[0].opcode, 70);
    CHECK_EQ(cmds[0].lawId, lawId);
    CHECK_EQ(cmds[0].value, 100);          // clamped to hi
    CHECK_EQ(cmds[0].masterId, p.ownerId);

    // value -7 < lo(10) -> clamps to 10.
    GesetzCommandLogReset();
    GesetzRequestApply(lawId, -7, gp);
    GesetzCommandLog(&cmdCount);
    CHECK_EQ(cmdCount, 1);
    CHECK_EQ(GesetzCommandLog(nullptr)[0].value, 10);

    // --- Stage 4: apply the committed enactment to the live law table.
    GesetzNotifyLogReset();
    GesetzApplyCmd apply{};
    apply.initiatorId = p.ownerId;
    apply.lawId = lawId;
    apply.newThreshold = 100;
    int applied = GesetzApplyAndNotify(apply, /*localMasterId=*/-1,
                                       /*initiatorResolves=*/true);
    CHECK_EQ(applied, 1);
    CHECK_EQ(g_lawTable[lawId].threshold, 100);

    // Initiator (4242) != local master (-1) and resolves -> one notify fired.
    int notifyCount = 0;
    const GesetzNotifyEvent* ev = GesetzNotifyLog(&notifyCount);
    CHECK_EQ(notifyCount, 1);
    CHECK_EQ(ev[0].masterId, p.ownerId);
    CHECK_EQ(ev[0].lawId, lawId);
    CHECK_EQ(ev[0].newThreshold, 100);
}

TEST(LawApply2E2E, TryPromoteCommandRoundTrip) {
    // Two same-type slots so the bookCat/reqCode gate passes; the command carries
    // the holder char-ids and tag 6.
    OfficeHolderTableReset();
    g_officeHolders[0].state = 3; g_officeHolders[0].type = 7;
    g_officeHolders[0].holder = 100; g_officeHolders[0].city = 8000;
    g_officeHolders[1].state = 3; g_officeHolders[1].type = 7;
    g_officeHolders[1].holder = 101; g_officeHolders[1].city = 8001;

    OfficeSetPromoteCommandHook(nullptr, nullptr);
    OfficePromoteCommandLogReset();

    OfficePerson p    = Person(900, 7, 0);
    OfficePerson from = Person(8000, 7, 0);
    OfficePerson to   = Person(8001, 7, 0);

    CHECK_EQ(OfficeTryPromoteCharacter(p, from, to), 0);
    int n = 0;
    const OfficePromoteCommand* log = OfficePromoteCommandLog(&n);
    CHECK_EQ(n, 1);
    CHECK_EQ(log[0].personId, 900);
    CHECK_EQ(log[0].fromType, static_cast<int>(100));
    CHECK_EQ(log[0].toType, static_cast<int>(101));
    CHECK_EQ(log[0].tag, 6);
}
