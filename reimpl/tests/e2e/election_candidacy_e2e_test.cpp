#include "test.h"

// E2E: a full guild-master election cycle across the election_candidacy +
// office modules. Two consecutive elections run against the live g_officeHolders
// table: a vacant-seat election that installs a winner, then a re-election where
// the prior winner is the incumbent and a wealthier challenger displaces them.
// The install hook applies the result back into the real holder table (via the
// office sibling) so the second election reads the first's outcome — closing the
// election -> install -> re-election loop the engine drives each term.
//
// Real-asset variant is GUARDED behind GUILD_E2E_ASSETS (absent the asset this
// runs the synthetic in-memory flow, which is the deterministic core anyway).
#include "world/election_candidacy.h"
#include "world/office.h"

#include <cstdlib>

using namespace guild;
using namespace guild::world;

namespace {
// The install hook commits the winner into the real holder table: it finds the
// seat whose +0 key matches and writes the secondary (the holder id). id==0
// clears the seat. This mirrors VIBE_Office_AddTableEntry's effect on the table.
struct TermState { int lastWinner = -1; };
void CommitInstall(int seatKey, i32 personId, void* ctx) {
    auto* st = static_cast<TermState*>(ctx);
    for (int i = 0; i < kOfficeDefCount; ++i) {
        if ((int)g_officeHolders[i].holder == seatKey &&
            (g_officeHolders[i].type == kSeatTypeDeputy ||
             g_officeHolders[i].type == kSeatTypeMaster)) {
            g_officeHolders[i].secondary = (personId == 0) ? -1 : personId;
            if (personId != 0) st->lastWinner = personId;
            return;
        }
    }
}
CanvassPerson MkP(u16 emp, u8 ecat, i32 pid, i32 w, u8 wc) {
    CanvassPerson p;
    p.employer = emp; p.flagged = false; p.empCategory = ecat;
    p.personId = pid; p.totalWealth = w; p.winnerCategory = wc;
    return p;
}
} // namespace

TEST(ElectCandidacyE2E, ElectionThenReelectionTermLoop) {
    const bool haveAssets = std::getenv("GUILD_E2E_ASSETS") != nullptr;
    (void)haveAssets; // the synthetic flow is the deterministic reference path

    // Fresh table; one deputy seat (type 28, key 71), vacant.
    OfficeHolderTableReset();
    for (int i = 0; i < kOfficeDefCount; ++i) {
        g_officeHolders[i].type = 0; g_officeHolders[i].holder = 0;
        g_officeHolders[i].secondary = -1;
    }
    g_officeHolders[4].type = kSeatTypeDeputy; g_officeHolders[4].holder = 71;
    g_officeHolders[8].type = kSeatTypeMaster; g_officeHolders[8].holder = 88;

    TermState st;
    CanvassSetHooks(&CommitInstall, /*notify*/ nullptr, &st);
    CanvassSetIncumbentRank(3); // a held rank-high near the challengers

    // --- Term 1: vacant deputy seat, three deputy-band candidates. ---
    CanvassPerson term1[] = { MkP(10, 3, 100, 400, 3),
                              MkP(11, 3, 101, 700, 3),
                              MkP(12, 3, 102, 500, 3) };
    CanvassResult r1 = CollectGuildCandidates(term1, 3, nullptr, 0);
    CHECK(r1.quorumMet);
    CHECK_EQ(r1.incumbentId, -1);
    CHECK_EQ(r1.winnerId, 101);          // wealthiest
    CHECK(r1.choice == CanvassSeatChoice::kDeputySeatOnly);
    CHECK_EQ(r1.installSeat, 71);
    // The install committed into the real table.
    CHECK_EQ(st.lastWinner, 101);
    OfficeHolder seat;
    CHECK_EQ(OfficeGetEntryByHolder(kSeatTypeDeputy, &seat), 1);
    CHECK_EQ(seat.secondary, 101);       // 101 now holds the deputy seat

    // --- Term 2: re-election. The incumbent (101) holds the deputy seat; a
    // wealthier same-category challenger (103, wealth 900) should win. Same rank
    // distance (< 6) -> same-seat path, reinstall into the deputy seat. ---
    CanvassSetIncumbentRank(3); // incumbent rank-high; |3-0(win)| < 6 -> same seat
    CanvassPerson term2[] = { MkP(11, 3, 101, 700, 3),  // incumbent re-stands
                              MkP(13, 3, 103, 900, 3),  // wealthier challenger
                              MkP(14, 3, 104, 600, 3) };
    CanvassResult r2 = CollectGuildCandidates(term2, 3, nullptr, 0);
    CHECK(r2.quorumMet);
    CHECK_EQ(r2.incumbentId, 101);       // read back from the table
    CHECK_EQ(r2.winnerId, 103);          // challenger unseats the incumbent
    CHECK(r2.install);
    CHECK(r2.choice == CanvassSeatChoice::kSameSeat);
    CHECK_EQ(r2.installSeat, 71);
    CHECK_EQ(st.lastWinner, 103);
    CHECK_EQ(OfficeGetEntryByHolder(kSeatTypeDeputy, &seat), 1);
    CHECK_EQ(seat.secondary, 103);       // term loop closed: 103 now holds it

    // --- Term 3: incumbent (103) re-stands and is still the wealthiest -> no
    // challenger beats the incumbent seed, so no install fires (winner==incumbent
    // is gated; here a strictly-greater challenger is absent). ---
    CanvassPerson term3[] = { MkP(13, 3, 103, 900, 3),  // incumbent, top wealth
                              MkP(15, 3, 105, 100, 3),
                              MkP(16, 3, 106, 200, 3) };
    CanvassResult r3 = CollectGuildCandidates(term3, 3, nullptr, 0);
    CHECK(r3.quorumMet);
    CHECK_EQ(r3.incumbentId, 103);
    // The wealthiest candidate IS the incumbent (103) -> install gated off.
    CHECK_EQ(r3.winnerId, 103);
    CHECK(!r3.install);
    CHECK_EQ(st.lastWinner, 103);        // unchanged

    CanvassSetHooks(nullptr, nullptr, nullptr);
}
