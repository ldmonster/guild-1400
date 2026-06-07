#include "test.h"

// Integration: the two-seat guild-master canvass (world/election_candidacy) runs
// against the REAL office sibling (world/office: g_officeHolders, the live holder
// table + OfficeGetEntryByHolder/OfficeHolderTableReset). We seed the actual
// holder table with type-28 (deputy) and type-29 (master) seats so the canvass
// reads its incumbent from the same global the office module owns, then confirm
// the install hook writes the seat key the real office table reports for that
// type — i.e. the election and the office table agree on the seat identity.
#include "world/election_candidacy.h"
#include "world/office.h" // g_officeHolders, OfficeGetEntryByHolder, reset

using namespace guild;
using namespace guild::world;

namespace {
struct ItestCapture {
    int  lastInstallSeat = -2;
    i32  lastInstallId   = -2;
    bool lastNotifyOffice = false;
    i32  lastNotifyId    = -2;
    u8   lastNotifyMsg   = 0;
    int  installs        = 0;
    int  clears          = 0;
};
void ItInstall(int seat, i32 id, void* ctx) {
    auto* c = static_cast<ItestCapture*>(ctx);
    c->lastInstallSeat = seat; c->lastInstallId = id;
    if (id == 0) ++c->clears; else ++c->installs;
}
void ItNotify(bool isOffice, i32 id, u8 msg, void* ctx) {
    auto* c = static_cast<ItestCapture*>(ctx);
    c->lastNotifyOffice = isOffice; c->lastNotifyId = id; c->lastNotifyMsg = msg;
}
CanvassPerson MkP(u16 emp, u8 ecat, i32 pid, i32 w, u8 wc) {
    CanvassPerson p;
    p.employer = emp; p.flagged = false; p.empCategory = ecat;
    p.personId = pid; p.totalWealth = w; p.winnerCategory = wc;
    return p;
}
} // namespace

// Seed real holder seats (type 28 + 29), vacant secondaries -> the canvass should
// read seatDeputy/seatMaster from g_officeHolders and (no incumbent) install the
// winner into the seat for its category. Cross-checks the seat key against what
// the real OfficeGetEntryByHolder reports for that type.
TEST(ElectCandidacyItest, ReadsRealHolderSeats) {
    OfficeHolderTableReset();
    for (int i = 0; i < kOfficeDefCount; ++i) {
        g_officeHolders[i].type = 0; g_officeHolders[i].holder = 0;
        g_officeHolders[i].secondary = -1;
    }
    // Entry 5 is the deputy seat (type 28, key 71); entry 9 the master (type 29,
    // key 88). Vacant -> secondary -1.
    g_officeHolders[5].type = kSeatTypeDeputy; g_officeHolders[5].holder = 71;
    g_officeHolders[9].type = kSeatTypeMaster; g_officeHolders[9].holder = 88;
    CanvassSetIncumbentRank(0);

    // Confirm the REAL office sibling reports the same seat keys we seeded.
    OfficeHolder oDep, oMas;
    CHECK_EQ(OfficeGetEntryByHolder(kSeatTypeDeputy, &oDep), 1);
    CHECK_EQ(OfficeGetEntryByHolder(kSeatTypeMaster, &oMas), 1);
    CHECK_EQ((int)oDep.holder, 71);
    CHECK_EQ((int)oMas.holder, 88);

    ItestCapture cap;
    CanvassSetHooks(&ItInstall, &ItNotify, &cap);

    // Winner category 3 (deputy band) -> installs into the deputy seat key (71).
    CanvassPerson band1[] = { MkP(10, 3, 100, 500, 3),
                              MkP(11, 3, 101, 900, 3),
                              MkP(12, 3, 102, 300, 3) };
    CanvassResult r = CollectGuildCandidates(band1, 3, nullptr, 0);

    CHECK_EQ(r.seatDeputy, 71);   // read from the real table
    CHECK_EQ(r.seatMaster, 88);
    CHECK_EQ(r.incumbentId, -1);  // both seats vacant
    CHECK_EQ(r.winnerId, 101);
    CHECK(r.choice == CanvassSeatChoice::kDeputySeatOnly);
    CHECK_EQ(r.installSeat, 71);  // == OfficeGetEntryByHolder(28).holder
    CHECK_EQ(cap.lastInstallSeat, 71);
    CHECK_EQ(cap.lastInstallId, 101);
    CHECK(cap.lastNotifyOffice);
    CanvassSetHooks(nullptr, nullptr, nullptr);
}

// With an occupied incumbent (deputy secondary set) and a far-rank winner of the
// MASTER category, the canvass swaps: clear the deputy seat, install the master
// seat. Exercises the rank-distance gate against the real holder table + the
// install hook's clear (id==0) path.
TEST(ElectCandidacyItest, FarRankSwapAgainstRealTable) {
    OfficeHolderTableReset();
    for (int i = 0; i < kOfficeDefCount; ++i) {
        g_officeHolders[i].type = 0; g_officeHolders[i].holder = 0;
        g_officeHolders[i].secondary = -1;
    }
    g_officeHolders[3].type = kSeatTypeDeputy; g_officeHolders[3].holder = 50;
    g_officeHolders[3].secondary = 999; // incumbent holds the deputy seat
    g_officeHolders[7].type = kSeatTypeMaster; g_officeHolders[7].holder = 60;
    CanvassSetIncumbentRank(10); // far from the winner's rankHigh (0): |10-0|>=6

    ItestCapture cap;
    CanvassSetHooks(&ItInstall, &ItNotify, &cap);

    // Winner category 10 (NOT 1..6) -> master seat; wealth must beat the seed.
    CanvassPerson band2[] = { MkP(20, 42, 200, 1000, 10),
                              MkP(21, 43, 201, 800, 10),
                              MkP(22, 44, 202, 700, 10) };
    CanvassResult r = CollectGuildCandidates(nullptr, 0, band2, 3);

    CHECK_EQ(r.incumbentId, 999);
    CHECK(r.install);
    CHECK_EQ(r.winnerId, 200);
    CHECK(r.choice == CanvassSeatChoice::kSwapMaster);
    CHECK_EQ(r.installSeat, 60);  // master seat key from the real table
    CHECK_EQ(r.clearedSeat, 50);  // deputy seat cleared
    CHECK_EQ(r.notifyMessage, (u8)kSeatTypeMaster); // 29
    // Two hook calls: install master (id 200) + clear deputy (id 0).
    CHECK_EQ(cap.installs, 1);
    CHECK_EQ(cap.clears, 1);
    CanvassSetHooks(nullptr, nullptr, nullptr);
}
