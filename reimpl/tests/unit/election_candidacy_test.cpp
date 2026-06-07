#include "test.h"

// Golden-vector unit tests for the two-seat guild-master canvass
// (VIBE_Amt_CollectGuildCandidates 0x480e4c) and the CalcZuenfte dispatch /
// notify-arg cores (0x481b38 / 0x480d08 / 0x480da8). Expectations were derived
// with a Python model of the recovered decision rules.
#include "world/election_candidacy.h"
#include "world/office.h" // g_officeHolders, OfficeHolderTableReset

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {
// Capture the install/notify hook calls so the decision can be asserted.
struct ElecCapture {
    int  installSeats[8]; i32 installIds[8]; int nInstall = 0;
    bool notifyOffice[8]; i32 notifyId[8]; u8 notifyMsg[8]; int nNotify = 0;
};
void OnInstall(int seat, i32 id, void* ctx) {
    auto* c = static_cast<ElecCapture*>(ctx);
    if (c->nInstall < 8) { c->installSeats[c->nInstall] = seat;
                           c->installIds[c->nInstall] = id; ++c->nInstall; }
}
void OnNotify(bool isOffice, i32 id, u8 msg, void* ctx) {
    auto* c = static_cast<ElecCapture*>(ctx);
    if (c->nNotify < 8) { c->notifyOffice[c->nNotify] = isOffice;
                          c->notifyId[c->nNotify] = id;
                          c->notifyMsg[c->nNotify] = msg; ++c->nNotify; }
}

// Reset the office holder table so no incumbent seats exist (vacant election).
void ClearSeats() {
    OfficeHolderTableReset();
    for (int i = 0; i < kOfficeDefCount; ++i) {
        g_officeHolders[i].type      = 0;
        g_officeHolders[i].holder    = 0;
        g_officeHolders[i].secondary = -1;
    }
    CanvassSetIncumbentRank(0);
}

CanvassPerson MkP(u16 emp, bool flag, u8 ecat, i32 pid, i32 w, u8 wc) {
    CanvassPerson p;
    p.employer = emp; p.flagged = flag; p.empCategory = ecat;
    p.personId = pid; p.totalWealth = w; p.winnerCategory = wc;
    return p;
}
} // namespace

// Vector A: 3 employed members, all candidates (cat 1..6), winner is wealthiest
// (id 101, wealth 800). Winner's category 3 -> deputy seat (28).
TEST(ElectCandidacy, VectorA_DeputySeatVacant) {
    ClearSeats();
    ElecCapture cap;
    CanvassSetHooks(&OnInstall, &OnNotify, &cap);

    CanvassPerson band1[] = { MkP(10, false, 3, 100, 500, 3),
                              MkP(11, false, 3, 101, 800, 3),
                              MkP(12, false, 3, 102, 300, 5) };
    CanvassResult r = CollectGuildCandidates(band1, 3, nullptr, 0);

    CHECK_EQ(r.members, 3);
    CHECK_EQ(r.candidates, 3);
    CHECK(r.quorumMet);
    CHECK_EQ(r.winnerIndex, 1);
    CHECK_EQ(r.winnerId, 101);
    CHECK(r.install);
    CHECK_EQ(r.incumbentId, -1);
    CHECK(r.choice == CanvassSeatChoice::kDeputySeatOnly);
    CHECK_EQ(r.notifyMessage, (u8)kSeatTypeDeputy); // 28
    // One install (deputy seat), one office-notify of the winner.
    CHECK_EQ(cap.nInstall, 1);
    CHECK_EQ(cap.installIds[0], 101);
    CHECK_EQ(cap.nNotify, 1);
    CHECK(cap.notifyOffice[0]);
    CHECK_EQ(cap.notifyId[0], 101);
    CHECK_EQ(cap.notifyMsg[0], (u8)28);
    CanvassSetHooks(nullptr, nullptr, nullptr);
}

// Vector B: band1 has 2 employed non-candidates (count members), band2 has the
// candidates (cat 40..45) with a duplicate id (dropped). Winner cat 10 -> master
// seat (29).
TEST(ElectCandidacy, VectorB_MasterSeatDedup) {
    ClearSeats();
    ElecCapture cap;
    CanvassSetHooks(&OnInstall, &OnNotify, &cap);

    CanvassPerson band1[] = { MkP(5, false, 99, 200, 10, 10),
                              MkP(6, false, 99, 201, 10, 10) };
    CanvassPerson band2[] = { MkP(7, false, 42, 300, 1000, 10),
                              MkP(8, false, 44, 301, 999, 10),
                              MkP(8, false, 44, 301, 999, 10) }; // dup id 301
    CanvassResult r = CollectGuildCandidates(band1, 2, band2, 3);

    CHECK_EQ(r.members, 5);    // 2 + 3 employed non-flagged
    CHECK_EQ(r.candidates, 2); // dup dropped
    CHECK(r.quorumMet);
    CHECK_EQ(r.winnerId, 300);
    CHECK(r.choice == CanvassSeatChoice::kMasterSeatOnly);
    CHECK_EQ(r.notifyMessage, (u8)kSeatTypeMaster); // 29
    CHECK_EQ(cap.installIds[0], 300);
    CHECK_EQ(cap.notifyMsg[0], (u8)29);
    CanvassSetHooks(nullptr, nullptr, nullptr);
}

// Vector C: quorum fails (only 2 members) -> no install, no hooks fire.
TEST(ElectCandidacy, VectorC_QuorumFail) {
    ClearSeats();
    ElecCapture cap;
    CanvassSetHooks(&OnInstall, &OnNotify, &cap);

    CanvassPerson band1[] = { MkP(5, false, 3, 400, 100, 3),
                              MkP(6, false, 3, 401, 100, 3) };
    CanvassResult r = CollectGuildCandidates(band1, 2, nullptr, 0);

    CHECK_EQ(r.members, 2);
    CHECK(!r.quorumMet);
    CHECK(!r.install);
    CHECK_EQ(cap.nInstall, 0);
    CHECK_EQ(cap.nNotify, 0);
    CanvassSetHooks(nullptr, nullptr, nullptr);
}

// Vector D: unemployed (0xFFFF) and flagged persons are skipped from both the
// quorum count and candidacy; wealthiest of the remaining wins.
TEST(ElectCandidacy, VectorD_SkipUnemployedFlagged) {
    ClearSeats();
    CanvassPerson band1[] = { MkP(0xFFFF, false, 3, 500, 9999, 3), // unemployed
                              MkP(10, true, 3, 501, 9999, 3),      // flagged
                              MkP(10, false, 3, 502, 50, 3),
                              MkP(11, false, 3, 503, 60, 3),
                              MkP(12, false, 3, 504, 55, 3) };
    CanvassResult r = CollectGuildCandidates(band1, 5, nullptr, 0);
    CHECK_EQ(r.members, 3);
    CHECK_EQ(r.candidates, 3);
    CHECK_EQ(r.winnerId, 503); // wealth 60 wins
}

// Vector E: candidate cap at 16 (v33[16]).
TEST(ElectCandidacy, VectorE_CandidateCap16) {
    ClearSeats();
    CanvassPerson pool[20];
    for (int i = 0; i < 20; ++i)
        pool[i] = MkP((u16)(100 + i), false, 3, 600 + i, 100 + i, 3);
    CanvassResult r = CollectGuildCandidates(pool, 20, nullptr, 0);
    CHECK_EQ(r.candidates, 16); // capped
    // Sweep stops collecting at 16, so members also stops being incremented past
    // the 16th (the original do/while breaks when collected==16). Winner is the
    // wealthiest among the first 16 collected (ids 600..615; wealth 100..115).
    CHECK_EQ(r.winnerId, 615);
}

// CalcZuenfte dispatch: the query-category map {1->24,2->25,3->26,5->23}.
TEST(ElectCandidacy, QueryCategoryMap) {
    CHECK_EQ(OfficeBuildingQueryCategory(1), 24);
    CHECK_EQ(OfficeBuildingQueryCategory(2), 25);
    CHECK_EQ(OfficeBuildingQueryCategory(3), 26);
    CHECK_EQ(OfficeBuildingQueryCategory(5), 23);
    CHECK_EQ(OfficeBuildingQueryCategory(0), -1);
    CHECK_EQ(OfficeBuildingQueryCategory(4), -1); // gap (no case 4)
    CHECK_EQ(OfficeBuildingQueryCategory(6), -1);
}

// IsOfficeBuildingValid: handler match fans CalcZuenfte and returns 0; else the
// member-present query for the mapped category.
TEST(ElectCandidacy, IsOfficeBuildingValidPaths) {
    static int fanRanks[8]; static int fanN;
    fanN = 0;
    OfficeBuildingSetZuenfteHook(
        [](u8 rank, int /*arg*/, void*) {
            if (fanN < 8) fanRanks[fanN++] = rank;
        },
        nullptr);

    OfficeBuildingValidInputs hm;
    hm.handlerMatch = true;
    CHECK_EQ(IsOfficeBuildingValid(hm, 99), 0);
    CHECK_EQ(fanN, 4);
    CHECK_EQ(fanRanks[0], 0x1E);
    CHECK_EQ(fanRanks[3], 0x21);

    OfficeBuildingValidInputs ok;
    ok.handlerMatch = false; ok.role = 2; ok.memberPresent = true;
    CHECK_EQ(IsOfficeBuildingValid(ok, 0), 1);
    ok.memberPresent = false;
    CHECK_EQ(IsOfficeBuildingValid(ok, 0), 0);
    ok.role = 4; ok.memberPresent = true; // role 4 not mapped -> 0
    CHECK_EQ(IsOfficeBuildingValid(ok, 0), 0);
    OfficeBuildingSetZuenfteHook(nullptr, nullptr);
}

// Notify-arg gate: profession {5,6,7} broadcasts; offset 525/560 by the +9 byte.
TEST(ElectCandidacy, NotifyArgGate) {
    int arg = -1;
    NotifyTarget t;
    t.valid = true; t.profession = 6; t.secondFlag = false;
    CHECK(NotifyOfficeComputeArg(t, 34, arg));
    CHECK_EQ(arg, 34 + 525); // 559
    t.secondFlag = true;
    CHECK(NotifyOfficeComputeArg(t, 34, arg));
    CHECK_EQ(arg, 34 + 560); // 594
    t.profession = 7; t.secondFlag = false;
    CHECK(NotifyPersonComputeArg(t, 29, arg));
    CHECK_EQ(arg, 29 + 525); // 554
    // Non-broadcasting profession.
    t.profession = 1;
    CHECK(!NotifyOfficeComputeArg(t, 34, arg));
    // Person variant null guard.
    t.profession = 6; t.valid = false;
    CHECK(!NotifyPersonComputeArg(t, 34, arg));
}

// Broadcast walks the office-object slots, sending only to roles 6/7.
TEST(ElectCandidacy, NotifyBroadcastSelectsRoles) {
    static int sentIds[8]; static int sentN; sentN = 0;
    NotifySlot slots[5];
    slots[0] = {6, 1000}; slots[1] = {5, 1001}; slots[2] = {7, 1002};
    slots[3] = {0, 1003}; slots[4] = {6, 1004};
    int n = NotifyBroadcast(
        slots, 5, /*subject*/ 42, /*arg*/ 559,
        [](i32 id, int /*subj*/, int /*arg*/, void*) {
            if (sentN < 8) sentIds[sentN++] = id;
        },
        nullptr);
    CHECK_EQ(n, 3);
    CHECK_EQ(sentIds[0], 1000);
    CHECK_EQ(sentIds[1], 1002);
    CHECK_EQ(sentIds[2], 1004);
}
