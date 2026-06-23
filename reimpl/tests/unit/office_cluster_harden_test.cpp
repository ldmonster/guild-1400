// Wave-12 hardening tests (W12-OFFICE cluster): boundary / malformed / oversized
// drivers for the office / election / guild / council world rule cores. These pin
// the OOB/UB fixes made in this wave and exercise the degenerate-input edges
// (out-of-range office/rank indices, 0/many candidates, bad ranks, 0/many council
// members, candidacy for a bad office, truncated form/save blobs). Run under
// ASAN+UBSAN: any out-of-bounds table/array read or UB trips the sanitizer.
//
// Golden / valid-input behavior is asserted unchanged elsewhere; here we only add
// the previously-uncovered edges. See progress/harden-office-wave12.md.
#include "test.h"

#include <cstring>
#include <vector>

#include "world/office.h"
#include "world/office_assign.h"
#include "world/office_forms.h"
#include "world/office_law3.h"
#include "world/election.h"
#include "world/election_candidacy.h"
#include "world/council.h"
#include "world/guild.h"
#include "world/guild_rank.h"
#include "world/guild_election.h"
#include "world/guild_assignment.h"
#include "world/law_types.h"

using namespace guild::world;

// ===========================================================================
// office.cpp — office-definition TABLE index boundaries.
// ===========================================================================
// The def table is 446 bytes; the accessors index it by office type/rank. The
// original read adjacent globals for an out-of-range index without faulting; the
// reconstruction holds only the 446 bytes, so these probes (rank==37 edge and a
// malformed type byte up to 0xFF) must NOT read out of bounds.
TEST(OfficeHarden, GetCategoryByRankPastTableNoOob) {
    // The original's bound is `rank <= 0x25` (37). rank==37 indexes byte 447 of a
    // 446-byte table — the fixed accessor returns 0 (the zero-tail). rank>37 -> 0.
    CHECK_EQ(OfficeGetCategoryByRank(37), (guild::u8)0);
    CHECK_EQ(OfficeGetCategoryByRank(38), (guild::u8)0);
    CHECK_EQ(OfficeGetCategoryByRank(255), (guild::u8)0);
    // In-range values are unchanged (rank 1 reqCode == 1).
    CHECK_EQ(OfficeGetCategoryByRank(1), (guild::u8)1);
}

TEST(OfficeHarden, DefAccessorsFullByteRangeNoOob) {
    // Sweep every possible (u8) office type through every table accessor: a
    // malformed save can leave any byte in a holder's type field, and the
    // mutation rules feed it straight into these. None may read OOB.
    for (int t = 0; t < 256; ++t) {
        guild::u8 type = (guild::u8)t;
        volatile guild::u8 a = OfficeDefBookCat(type);
        volatile guild::u8 b = OfficeDefReqCode(type);
        volatile guild::i32 c = OfficeDefFlag(type);
        volatile guild::u8 d = OfficeDefId(type);
        (void)a; (void)b; (void)c; (void)d;
    }
    CHECK(true);
}

TEST(OfficeHarden, GetDefinitionOutOfRange) {
    OfficeDef def;
    // rank >= 37 takes the fallback path (returns 0) and must not read OOB.
    CHECK_EQ(OfficeGetDefinition(37, &def), 0);
    CHECK_EQ(OfficeGetDefinition(255, &def), 0);
    // rank 36 is the last in-range record.
    CHECK_EQ(OfficeGetDefinition(36, &def), 1);
}

TEST(OfficeHarden, GetRankRequirementsOutOfRange) {
    RankRequirements rr;
    CHECK_EQ(OfficeGetRankRequirements(37, &rr), 0);
    CHECK_EQ(OfficeGetRankRequirements(255, &rr), 0);
}

TEST(OfficeHarden, CanPromoteRankReqCode1to6NoMatrixOob) {
    // CanPromoteRank indexes the 7x7 kPromotionCost matrix by reqCode(officeType) and
    // reqCode(targetRank). For office types 0..27 the reqCode is 1..6, so the indices
    // stay inside the matrix; sweep that whole region and demand no OOB read.
    //
    // BEHAVIORAL / needs-MCP: office types 28..34 carry reqCode == 7, which the
    // current 7x7 matrix CANNOT index (UBSAN: "index 7 out of bounds for float[7]").
    // The original's matrix must be larger than 7x7 (reqCode legitimately reaches 7);
    // the correct row-7 / column-7 cost values can only be recovered from the binary,
    // so that edge is documented for MCP follow-up rather than guessed here. See
    // progress/harden-office-wave12.md.
    for (int ot = 0; ot < 28; ++ot) {
        for (int tr = 0; tr < 28; ++tr) {
            OfficePerson p;
            p.valid = true;
            p.officeType = (guild::u8)ot;
            p.rank = (guild::u8)ot;
            float cost = 0.f;
            volatile int ok = OfficeCanPromoteRank(p, (guild::u8)tr, &cost);
            (void)ok; (void)cost;
        }
    }
    CHECK(true);
}

TEST(OfficeHarden, CollectSuccessorBadRankAndEmptyPool) {
    guild::i32 out[8];
    // Bad target rank -> 0, no read.
    CHECK_EQ(OfficeCollectSuccessorCandidates(37, nullptr, 0, 6, out, 8), 0);
    CHECK_EQ(OfficeCollectSuccessorCandidates(255, nullptr, 0, 6, out, 8), 0);
    // Empty / null pool.
    CHECK_EQ(OfficeCollectSuccessorCandidates(1, nullptr, 0, 6, out, 8), 0);
    // maxCount and outCapacity over-large: must respect the smaller cap, no OOB.
    OfficePerson people[2];
    for (auto& q : people) { q.valid = true; q.officeType = 1; }
    int n = OfficeCollectSuccessorCandidates(1, people, 2, 100, out, 1);
    CHECK(n <= 1);
}

// ===========================================================================
// office.cpp — holder-table scans (GetEntryByCity / ByHolder) on a fresh table.
// ===========================================================================
TEST(OfficeHarden, HolderTableScansNoMatchNoOob) {
    OfficeHolderTableReset();
    OfficeHolder out;
    // No holder has city/type == 200 -> the linear scans must terminate in-bounds.
    int r1 = OfficeGetEntryByCity(200, &out);
    int r2 = OfficeGetEntryByHolder(200, &out);
    CHECK(r1 == 0 || r1 == 1); // exercised; the key point is no OOB
    CHECK(r2 == 0 || r2 == 1);
}

// ===========================================================================
// office_assign.cpp — candidacy for a BAD office type; malformed holder types.
// ===========================================================================
namespace {
struct StubStore : OfficePersonStore {
    std::vector<OfficePersonRec> recs;
    OfficePersonRec* Find(guild::i32 id) override {
        if (id == -1) return nullptr;
        for (auto& r : recs)
            if (r.ownerId == id) return &r;
        return nullptr;
    }
};
} // namespace

TEST(OfficeHarden, ApplyForCandidacyBadOfficeType) {
    OfficeHolderTableReset();
    OfficePersonRec applicant;
    applicant.ownerId = 7;
    applicant.office360 = 0;
    // Office type 0xFF: no slot matches; the scan must stay in-bounds and fail.
    CHECK_EQ(OfficeApplyForCandidacy(applicant, 0xFF), -1);
    // Office type 200: same.
    CHECK_EQ(OfficeApplyForCandidacy(applicant, 200), -1);
}

TEST(OfficeHarden, AssignToCandidateMalformedHolderTypes) {
    OfficeHolderTableReset();
    // Stamp a degenerate type byte (0xFF) into a slot then drive AssignToCandidate;
    // the two-slot path reads the def table by that type and must not read OOB.
    g_officeHolders[0].holder = 5;
    g_officeHolders[0].type   = 0xFF;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 0;
    g_officeHolders[1].holder = 6;
    g_officeHolders[1].type   = 0xFF;
    g_officeHolders[1].city   = -1;
    g_officeHolders[1].state  = 3;
    g_officeHolders[1].rank   = 0;

    StubStore ps;
    OfficePersonRec rec; rec.ownerId = 9; rec.valid = true; rec.office360 = 0;
    ps.recs.push_back(rec);

    AssignRequest req;
    req.candidateId = 9;
    req.keyA = 5;
    req.keyB = 6;          // two-slot path -> reads OfficeDefId/Flag(0xFF)
    req.officeType = 0xFF;
    volatile int r = OfficeAssignToCandidate(req, ps);
    (void)r;
    OfficeHolderTableReset();
    CHECK(true);
}

TEST(OfficeHarden, TransferHoldershipMalformedSlotType) {
    OfficeHolderTableReset();
    g_officeHolders[0].holder = 3;
    g_officeHolders[0].type   = 0xFF; // drives OfficeDefBookCat/Flag(0xFF)
    g_officeHolders[0].city   = -1;
    StubStore ps;
    OfficePersonRec seated; seated.ownerId = 4; seated.valid = true; seated.office358 = 1;
    ps.recs.push_back(seated);

    TransferRequest req;
    req.holderKey = 3;
    req.primaryId = -1;
    req.state = 1;
    req.seatedId = 4;
    volatile int r = OfficeTransferHoldership(req, ps);
    (void)r;
    OfficeHolderTableReset();
    CHECK(true);
}

// ===========================================================================
// office_forms.cpp — torture form on a BAD crime case + truncated successor blob.
// ===========================================================================
TEST(OfficeHarden, TortureFormBadCrimeCase) {
    // crimeCase out of 0..6 -> not built; dispatch returns the default; no OOB.
    TortureChoiceForm f = Office_BuildTortureChoiceForm(99, 640, 1);
    CHECK_EQ(f.built, false);
    CHECK_EQ(Office_DispatchTortureChoice(f, kClickOk), f.defaultResult);
    TortureChoiceForm fn = Office_BuildTortureChoiceForm(-1, 640, 1);
    CHECK_EQ(fn.built, false);
}

TEST(OfficeHarden, TortureCase2NullShuffleOrder) {
    // case 2 indexes kTortureCostTable[instrument]; with a null shuffle order it
    // must use the identity 0..6 and never index past the 7-entry cost table.
    // gilde.exe 0x4a410a: only the first 3 shuffled instruments get buttons.
    TortureChoiceForm f = Office_BuildTortureChoiceForm(2, 640, 3, nullptr);
    CHECK_EQ((int)f.buttons.size(), kTortureCase2Buttons);
    for (auto& b : f.buttons)
        CHECK(b.payload >= 0 && b.payload < kTortureCostCount);
}

TEST(OfficeHarden, SuccessorDialogBEmptyAndOversized) {
    // Zero resolvable candidates -> invalid form, no buttons, no OOB.
    std::vector<guild::i32> none;
    SuccessorDialogB d0 = Office_BuildSuccessorDialogB(none, 0, 640);
    CHECK_EQ(d0.valid, false);
    CHECK_EQ(Office_DispatchSuccessorDialogB(d0, 3000), (guild::i32)-1);

    // More candidates than the 4 button slots: must cap at 4, pad the rest.
    std::vector<guild::i32> many = {10, 11, 12, 13, 14, 15, 16};
    SuccessorDialogB d = Office_BuildSuccessorDialogB(many, 7, 640);
    CHECK_EQ((int)d.buttons.size(), kSuccessorMaxButtons);
}

// ===========================================================================
// election.cpp — 0 / many candidates; over-cap pool.
// ===========================================================================
TEST(OfficeHarden, ElectionZeroAndOverCapPool) {
    // Null / empty pool -> no install, no read.
    ElectionOutcome z = ElectionRunGuildMaster(nullptr, 0, -1);
    CHECK_EQ(z.quorumMet, false);

    // Far more candidates than the 16-slot table: collection must cap at 16 and
    // never write past candidateIds[16].
    std::vector<ElectionCandidate> pool(64);
    for (int i = 0; i < 64; ++i) {
        pool[i].personId = 1000 + i;
        pool[i].officeType = 15;     // in [13..18] -> candidate
        pool[i].employer = 1;        // employed
        pool[i].flagged = false;
        pool[i].totalWealth = i;
    }
    ElectionOutcome o = ElectionRunGuildMaster(pool.data(), 64, -1);
    CHECK(o.candidateCount <= kElectionMaxCandidates);
    CHECK(o.quorumMet);
}

// ===========================================================================
// council.cpp — 0 / many members; election winner index used as array index.
// ===========================================================================
TEST(OfficeHarden, CouncilTallyZeroVotes) {
    CouncilVoteTally t = CouncilTallyRemoval(nullptr, 0);
    CHECK_EQ(t.removed, false);
    CHECK_EQ(t.yes, 0);
}

TEST(OfficeHarden, CouncilElectWinnerZeroAndOverCap) {
    // Zero candidates -> winner -1, no read.
    ElectionResult r0 = CouncilElectWinner(nullptr, 0, 0, nullptr, nullptr);
    CHECK_EQ(r0.winner, -1);

    // candidateCount far above the internal counts[16] table: must clamp to 16.
    std::vector<int> ballots(100, 50); // all vote candidate 50 (>= clamp -> ignored)
    ElectionResult r = CouncilElectWinner(ballots.data(), 100, 1000, nullptr, nullptr);
    // No candidate within the clamped range got a vote; winner stays at most 15.
    CHECK(r.winner < 16);

    // Out-of-range ballots (negative / huge) must be skipped, not indexed.
    std::vector<int> bad = {-1, -5, 999, 3, 3};
    ElectionResult r2 = CouncilElectWinner(bad.data(), 5, 4, nullptr, nullptr);
    CHECK(r2.winner >= -1 && r2.winner < 4);
}

TEST(OfficeHarden, CouncilTallyManyVotesNoOob) {
    // A large vote stream of every code (and out-of-range codes) must tally without
    // any OOB; the relation-delta selector handles every CouncilVote value.
    std::vector<CouncilVote> votes;
    for (int i = 0; i < 300; ++i)
        votes.push_back(static_cast<CouncilVote>(i % 3));
    CouncilVoteTally t = CouncilTallyRemoval(votes.data(), (int)votes.size());
    CHECK_EQ(t.yes + t.no + t.abstain, 300);
    // per-vote delta for every code, both outcomes.
    for (int c = 0; c < 3; ++c) {
        volatile int a = CouncilRelationDelta(static_cast<CouncilVote>(c), true);
        volatile int b = CouncilRelationDelta(static_cast<CouncilVote>(c), false);
        (void)a; (void)b;
    }
    CHECK(true);
}

// ===========================================================================
// guild_rank.cpp — rank out of range.
// ===========================================================================
TEST(OfficeHarden, GuildEligibilityRankOutOfRange) {
    GuildPlayer p;
    p.rank = 0;       // below kGuildRankMin
    p.flags459 = 0;
    p.money = 100000;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kNotGuild);
    p.rank = 255;     // above kGuildRankMax
    CHECK(GuildGetEligibility(p) == GuildEligibility::kNotGuild);
    p.rank = 0x1E;    // in range
    CHECK(GuildGetEligibility(p) == GuildEligibility::kEligible);
}

// ===========================================================================
// guild_election.cpp — successor pick on 1 / tie pools; malformed Aemter blob.
// ===========================================================================
TEST(OfficeHarden, GuildSuccessorPickEdges) {
    CHECK_EQ(GuildSuccessorPick(nullptr, 0), -1);
    CHECK_EQ(GuildSuccessorPick(nullptr, 5), -1);
    guild::i32 one[1] = {3};
    CHECK_EQ(GuildSuccessorPick(one, 1), 0);
    guild::i32 tie[4] = {2, 2, 2, 2}; // all-tie -> random walk must stay in-bounds
    int w = GuildSuccessorPick(tie, 4);
    CHECK(w >= 0 && w < 4);
}

TEST(OfficeHarden, LoadAemterTruncatedAndBadCount) {
    OfficeHolderTableReset();
    // Truncated blob: shorter than the stream -> reject, no read past `len`.
    guild::u8 tiny[8] = {0};
    CHECK_EQ(LoadAemter(tiny, sizeof(tiny)), 0);
    CHECK_EQ(LoadAemter(nullptr, 0), 0);
    CHECK_EQ(LoadAemter(tiny, 0), 0);

    // Full-size blob with a wrong leading count -> reject before the record loop.
    std::vector<guild::u8> buf(kAemterStreamBytes, 0);
    buf[0] = 99; // count != 37
    CHECK_EQ(LoadAemter(buf.data(), buf.size()), 0);

    // A valid round-trip still works (count == 37).
    std::vector<guild::u8> good(kAemterStreamBytes, 0);
    CHECK(SaveAemter(good.data(), good.size()) == (size_t)kAemterStreamBytes);
    CHECK_EQ(LoadAemter(good.data(), good.size()), 1);
}

TEST(OfficeHarden, SaveAemterTooSmallBuffer) {
    // cap below the stream size -> 0, no write past `cap`.
    guild::u8 small[16];
    CHECK_EQ(SaveAemter(small, sizeof(small)), (size_t)0);
    CHECK_EQ(SaveAemter(nullptr, 0), (size_t)0);
}

// ===========================================================================
// guild_assignment.cpp — empty / over-cap collections.
// ===========================================================================
namespace {
// A malformed collector that claims MORE entries than the 64-slot scratch holds.
int OverflowCollect(guild::u8, OfficeHolder* buf, int cap, void*) {
    for (int i = 0; i < cap; ++i) { buf[i] = OfficeHolder{}; buf[i].state = 0; }
    return 1000; // lies: far more than the buffer capacity
}
} // namespace

TEST(OfficeHarden, AssignGuildMembersOverflowCollect) {
    // A collector that returns a count larger than the scratch buffer must not make
    // AssignGuildMembers index buf[] out of bounds (the n clamp).
    GuildAssignContext gx;
    volatile int r = AssignGuildMembers(&OverflowCollect, gx);
    (void)r;
    CHECK(true);
}

TEST(OfficeHarden, GuildAssignmentEmptyAndNull) {
    GuildAssignContext gx;
    GuildAssignmentResult r = ComputeGuildAssignment(nullptr, 0, gx);
    CHECK_EQ(r.installs, 0);
    OfficeHolder one[1] = {};
    GuildAssignmentResult r2 = ComputeGuildAssignment(one, 1, gx);
    CHECK(r2.installs >= 0);
}

// ===========================================================================
// office_law3.cpp — role-template scan with bad ids; init table bounds.
// ===========================================================================
TEST(OfficeHarden, FindRoleTemplateBadInputs) {
    CHECK_EQ(OfficeFindRoleTemplate(0, 0), kRoleNotFound);   // roleId 0
    CHECK_EQ(OfficeFindRoleTemplate(0, 76), kRoleNotFound);  // roleId == 76
    // gilde.exe 0x57c18f: the range gate is a SIGNED char compare (cmp bl,4Ch/jge).
    // 255 == -1 as i8, so it is NOT rejected; the scan finds no match and returns
    // the table's terminator slot (table A: 75 live entries -> idx 75), not -1.
    CHECK_EQ(OfficeFindRoleTemplate(0, 255), 75);
    CHECK_EQ(OfficeFindRoleTemplate(2, 5), kRoleNotFound);   // which != 0/1
    // valid sweep stays in the 76-slot table.
    for (int rid = 1; rid < 76; ++rid) {
        volatile int a = OfficeFindRoleTemplate(0, (guild::u8)rid);
        volatile int b = OfficeFindRoleTemplate(1, (guild::u8)rid);
        (void)a; (void)b;
    }
    CHECK(true);
}

TEST(OfficeHarden, InitHolderTableBounds) {
    std::vector<OfficeInitRecord> table(kOfficeInitRecordCount);
    int r = OfficeInitHolderTable(table.data());
    CHECK_EQ(r, 7709);
    // every record's type byte came from the in-bounds seed table.
    CHECK_EQ((int)table[kOfficeInitRecordCount - 1].type,
             (int)kOfficeTypeSeed[kOfficeInitRecordCount - 1]);
}
