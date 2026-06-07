#include "test.h"

// Integration: drive office_law3's promotion-outcome flow against a REAL
// reconstructed sibling — VIBE_Office_TryPromoteCharacter (law_apply2.cpp
// 0x47ebd4), the holder-slot promotion validator/emitter. No mock promote
// result: OfficeAwaitPromoteResult routes its first step through
// OfficeFlowHooks.tryPromote, and here that hook forwards into the genuine
// OfficeTryPromoteCharacter against the real g_officeHolders table + the real
// office-definition table (OfficeGetDefinition). So whether the await reports a
// successful promotion is the real sibling's gate verdict (both holder slots
// resolve and share bookCat/reqCode), end to end.
//
// The personType / refresh / packetStatus leaves are local recorders so the
// post-promote control flow is steerable; the load-bearing decision (does a
// command get issued, or is it -1) is the reconstructed validator's.
#include "world/office_law3.h"
#include "world/law_apply2.h"   // REAL OfficeTryPromoteCharacter (0x47ebd4)
#include "world/office.h"       // g_officeHolders, OfficeGetDefinition, reset

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// Owner ids the from/to holder slots are keyed by (OfficeHolder::city == owner).
constexpr i32 kFromOwner = 5001;
constexpr i32 kToOwner   = 5002;
constexpr i32 kPersonId  = 9000;

// The three OfficePerson views the real validator resolves the slots against. The
// hook maps the await's (person, fromCity, toCity) ids onto these.
OfficePerson g_pPerson;
OfficePerson g_pFrom;
OfficePerson g_pTo;

// --- OfficeFlowHooks.tryPromote bridges into the REAL sibling ------------------
// The await passes ids; we forward the captured OfficePerson views into the real
// OfficeTryPromoteCharacter exactly as the live holder-resolve would.
i32 g_tryPromoteCalls = 0;
i32 RealTryPromote(i32 person, i32 fromCity, i32 toCity, void*) {
    ++g_tryPromoteCalls;
    (void)person; (void)fromCity; (void)toCity;
    return OfficeTryPromoteCharacter(g_pPerson, g_pFrom, g_pTo);  // REAL
}

// personType recorder: return a non-guild-head type so a successful promote ends
// the flow immediately (the spin-on-packet path is the guild-head case).
u8  g_personTypeVal = 3;
u8  RecPersonType(i32, void*) { return g_personTypeVal; }
void RecRefresh(void*) {}
int RecPacketStatus(i32, void*) { return 1; }

OfficeFlowHooks MakeFlowHooks() {
    OfficeFlowHooks h{};
    h.tryPromote   = RealTryPromote;   // <-- real wiring
    h.personType   = RecPersonType;
    h.refresh      = RecRefresh;
    h.packetStatus = RecPacketStatus;
    h.ctx          = nullptr;
    return h;
}

// Populate two holder slots (type T, same def => matching bookCat/reqCode) keyed
// by the from/to owner ids, and set up the three valid person views.
void SetupSuccessfulPromotion(u8 type) {
    OfficeHolderTableReset();
    g_officeHolders[3].city   = kFromOwner;   // from slot resolves on this owner
    g_officeHolders[3].type   = type;
    g_officeHolders[3].holder = 11;
    g_officeHolders[7].city   = kToOwner;     // to slot resolves on this owner
    g_officeHolders[7].type   = type;         // same type => same def => match
    g_officeHolders[7].holder = 22;

    g_pPerson = OfficePerson{}; g_pFrom = OfficePerson{}; g_pTo = OfficePerson{};
    g_pPerson.ownerId = kPersonId; g_pPerson.valid = true; g_pPerson.officeType = type;
    g_pFrom.ownerId = kFromOwner;  g_pFrom.valid = true;  g_pFrom.officeType = type;
    g_pTo.ownerId   = kToOwner;    g_pTo.valid = true;    g_pTo.officeType = type;
}

} // namespace

// Both holder slots resolve and share the same def (same type) -> the REAL
// validator emits a command (returns 0, not -1); personType != 6 -> the await
// reports a successful promotion outcome.
TEST(OfficeLaw3Itest, RealPromoteEmitsCommandAndAwaitSucceeds) {
    SetupSuccessfulPromotion(/*type*/ 6);   // holds an office (officeType != 0)
    OfficePromoteCommandLogReset();

    // Sanity-pin the sibling in isolation: it issues (returns != -1) for this setup.
    CHECK(OfficeTryPromoteCharacter(g_pPerson, g_pFrom, g_pTo) != -1);
    int logged = 0;
    const OfficePromoteCommand* log = OfficePromoteCommandLog(&logged);
    CHECK_EQ(logged, 1);                    // the real validator queued one command
    if (log) {
        CHECK_EQ(log[0].personId, kPersonId);
        CHECK_EQ(static_cast<int>(log[0].tag), 6);
    }

    g_tryPromoteCalls = 0;
    g_personTypeVal = 3;                    // not guild-head -> no packet spin
    const OfficeFlowHooks hooks = MakeFlowHooks();
    OfficeSetFlowHooks(hooks);

    bool ok = OfficeAwaitPromoteResult(kPersonId, kFromOwner, kToOwner);

    CHECK(ok);                              // real promote succeeded -> true
    CHECK_EQ(g_tryPromoteCalls, 1);         // the flow consulted the real sibling

    OfficeFlowHooksReset();
}

// When the "to" slot owner does not resolve in the real holder table, the REAL
// validator returns -1 (gate failure); the await reports failure end to end.
TEST(OfficeLaw3Itest, RealPromoteGateFailsAwaitReturnsFalse) {
    SetupSuccessfulPromotion(/*type*/ 6);
    OfficePromoteCommandLogReset();
    // Break the to-slot resolve: its owner is no longer in the holder table.
    g_pTo.ownerId = 999999;                 // matches no g_officeHolders[].city

    CHECK_EQ(OfficeTryPromoteCharacter(g_pPerson, g_pFrom, g_pTo), -1);
    int logged = 0;
    OfficePromoteCommandLog(&logged);
    CHECK_EQ(logged, 0);                    // no command issued

    g_tryPromoteCalls = 0;
    const OfficeFlowHooks hooks = MakeFlowHooks();
    OfficeSetFlowHooks(hooks);

    bool ok = OfficeAwaitPromoteResult(kPersonId, kFromOwner, 999999);

    CHECK(!ok);                             // real gate failed -> await false
    CHECK_EQ(g_tryPromoteCalls, 1);

    OfficeFlowHooksReset();
}

// Guild-head case (personType == 6): a successful real promote then spins the
// packet-status pump until non-zero; with our recorder returning a non-2 final
// status the await reports success. Exercises the post-promote spin path while
// the promote decision itself stays the real sibling's.
TEST(OfficeLaw3Itest, RealPromoteGuildHeadSpinsThenSucceeds) {
    SetupSuccessfulPromotion(/*type*/ 6);
    OfficePromoteCommandLogReset();

    g_tryPromoteCalls = 0;
    g_personTypeVal = 6;                    // guild-head -> take the spin path
    const OfficeFlowHooks hooks = MakeFlowHooks();
    OfficeSetFlowHooks(hooks);

    bool ok = OfficeAwaitPromoteResult(kPersonId, kFromOwner, kToOwner);

    CHECK(ok);                              // packetStatus()==1 (non-2) -> success
    CHECK_EQ(g_tryPromoteCalls, 1);

    OfficeFlowHooksReset();
}
