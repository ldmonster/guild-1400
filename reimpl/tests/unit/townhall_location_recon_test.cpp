#include "test.h"

#include "world/townhall_location_recon.h"

using namespace guild::world;

namespace {

// ---- gates / sinks for the contact-dispatch loop --------------------------------
struct Gate : TownHallGate {
    bool b512 = true, b1024 = true, b2048 = true;
    bool statActive = false;
    int  statOp = 0;
    bool HandlerFlag(int m) override {
        if (m == townhall::kFlagBuyBuilding) return b512;
        if (m == townhall::kFlagAgenda)      return b1024;
        if (m == townhall::kFlagOfficeInfo)  return b2048;
        return true;
    }
    bool StatObjectActive() override { return statActive; }
    int  StatObjectOpcode() override { return statOp; }
};

struct LawGate : TownHallLawGate {
    bool lawBooks = true, applyOffice = true;
    bool officePriv = false, playerState = false, flag2000 = true;
    bool HandlerFlag(int m) override {
        if (m == townhall::kFlagLawBooks)    return lawBooks;
        if (m == townhall::kFlagApplyOffice) return applyOffice;
        if (m == townhall::kFlagCitizenship) return flag2000;
        return true;
    }
    bool OfficeGrantsCitizenship() override { return officePriv; }
    bool PlayerCitizenshipState() override { return playerState; }
};

struct Sink : TownHallSink {
    int building = 0, agenda = 0, officeInfo = 0, citizenship = 0;
    int lawSection = -1, apply = 0, statGen = 0, statTax = 0;
    void ShowBuildingInfoDialog(int) override { ++building; }
    void RunAgendaWindow(int) override { ++agenda; }
    void RunOfficeInfoWindow(int) override { ++officeInfo; }
    void ShowCitizenshipDialog(int) override { ++citizenship; }
    void OpenLawBookSection(int s) override { lawSection = s; }
    void ShowApplicationDialog(int) override { ++apply; }
    void ShowStatisticsGeneral(int) override { ++statGen; }
    void ShowStatisticsTax(int) override { ++statTax; }
};

} // namespace

// ===========================================================================
// Contact-dispatch loop (0x51f70c)
// ===========================================================================
TEST(LocationReconTownHall, ContactAllFlagsOpen) {
    Gate g;
    auto ids = TownHall_BuildContactDispatch(g);
    CHECK(ids.buyBuilding != 0);
    CHECK(ids.agenda != 0);
    CHECK(ids.officeInfo != 0);
    // distinct ids
    CHECK(ids.buyBuilding != ids.agenda);
    CHECK(ids.agenda != ids.officeInfo);
}

TEST(LocationReconTownHall, ContactGatedEntriesSuppressed) {
    Gate g; g.b1024 = false;  // agenda flag closed
    auto ids = TownHall_BuildContactDispatch(g);
    CHECK(ids.buyBuilding != 0);
    CHECK_EQ(ids.agenda, 0);
    CHECK(ids.officeInfo != 0);
}

TEST(LocationReconTownHall, ContactDispatchRoutesByClick) {
    Gate g;
    auto ids = TownHall_BuildContactDispatch(g);
    Sink s;
    CHECK(TownHall_DispatchContact(ids.buyBuilding, ids, 7, s));
    CHECK_EQ(s.building, 1);
    CHECK(TownHall_DispatchContact(ids.agenda, ids, 7, s));
    CHECK_EQ(s.agenda, 1);
    CHECK(TownHall_DispatchContact(ids.officeInfo, ids, 7, s));
    CHECK_EQ(s.officeInfo, 1);
    // clicked == 0 -> nothing
    CHECK(!TownHall_DispatchContact(0, ids, 7, s));
}

TEST(LocationReconTownHall, StatisticsOverlayByOpcode) {
    Sink s;
    Gate g; g.statActive = true; g.statOp = townhall::kStatObjGeneral;
    TownHall_RunStatisticsOverlay(g, 1, s);
    CHECK_EQ(s.statGen, 1);
    CHECK_EQ(s.statTax, 0);

    g.statOp = townhall::kStatObjTax;
    TownHall_RunStatisticsOverlay(g, 1, s);
    CHECK_EQ(s.statTax, 1);

    Sink s2; Gate g2; g2.statActive = false; g2.statOp = townhall::kStatObjGeneral;
    TownHall_RunStatisticsOverlay(g2, 1, s2);
    CHECK_EQ(s2.statGen, 0);  // gated off when no stat object selected
}

// ===========================================================================
// Law / apply loop (0x52036c)
// ===========================================================================
TEST(LocationReconTownHall, LawBooksGate) {
    LawGate g; g.lawBooks = true; g.applyOffice = false;
    g.officePriv = false; g.playerState = false;
    auto ids = TownHall_BuildLawAndApply(g);
    CHECK(ids.lawTax != 0 && ids.lawCriminal != 0 && ids.lawConstitution != 0);
    CHECK_EQ(ids.applyOffice, 0);
    CHECK_EQ(ids.citizenship, 0);  // needs all 3 of priv/state/flag
}

TEST(LocationReconTownHall, CitizenshipEntryNeedsAllThree) {
    // priv && state && flag all required
    LawGate g; g.officePriv = true; g.playerState = true; g.flag2000 = true;
    CHECK(TownHall_BuildLawAndApply(g).citizenship != 0);

    LawGate g2; g2.officePriv = true; g2.playerState = true; g2.flag2000 = false;
    CHECK_EQ(TownHall_BuildLawAndApply(g2).citizenship, 0);

    LawGate g3; g3.officePriv = false; g3.playerState = true; g3.flag2000 = true;
    CHECK_EQ(TownHall_BuildLawAndApply(g3).citizenship, 0);

    LawGate g4; g4.officePriv = true; g4.playerState = false; g4.flag2000 = true;
    CHECK_EQ(TownHall_BuildLawAndApply(g4).citizenship, 0);
}

TEST(LocationReconTownHall, LawDispatchSections) {
    LawGate g; g.officePriv = true; g.playerState = true;
    auto ids = TownHall_BuildLawAndApply(g);
    Sink s;
    TownHall_DispatchLawAndApply(ids.lawTax, ids, 1, s);
    CHECK_EQ(s.lawSection, townhall::kLawSectionTax);
    TownHall_DispatchLawAndApply(ids.lawCriminal, ids, 1, s);
    CHECK_EQ(s.lawSection, townhall::kLawSectionCriminal);
    TownHall_DispatchLawAndApply(ids.lawConstitution, ids, 1, s);
    CHECK_EQ(s.lawSection, townhall::kLawSectionConstitution);
    TownHall_DispatchLawAndApply(ids.citizenship, ids, 1, s);
    CHECK_EQ(s.citizenship, 1);
    TownHall_DispatchLawAndApply(ids.applyOffice, ids, 1, s);
    CHECK_EQ(s.apply, 1);
}

// ===========================================================================
// Citizenship dialog rule core (0x51f81c)
// ===========================================================================
namespace {
struct CitDeps : CitizenshipDeps {
    int  wealth = 0; bool tutInactive = true; int rand3 = 0; bool confirm = false;
    // byte_63CC1D — max number of family grants.  Default 0 -> no family fan-out (the
    // loop breaks immediately), matching a fresh family record.
    int  grantCap = 0;
    int  memStatus[16] = {0};
    bool memOther[16];
    CitDeps() { for (bool& b : memOther) b = true; }
    int  PlayerWealth() override { return wealth; }
    bool TutorialInactive() override { return tutInactive; }
    int  RandomModulo3() override { return rand3; }
    bool ConfirmAndAfford() override { return confirm; }
    int  FamilyGrantCap() override { return grantCap; }
    int  FamilyMemberStatus(int i) override { return (i < 16) ? memStatus[i] : 0; }
    bool FamilyMemberIsOther(int i) override { return (i < 16) ? memOther[i] : true; }
};
struct CitSink : CitizenshipCommandSink {
    int grants = 0, payments = 0, privileges = 0, panelEvents = 0;
    int lastMonths = -1; int payPrice = -1, payOffice = -1, payBuyer = -1;
    int privOpcode = -1, privFlag = -1;
    void ScheduleGrant(int, int months) override { ++grants; lastMonths = months; }
    void QueuePayment(int office, int buyer, int price) override {
        ++payments; payOffice = office; payBuyer = buyer; payPrice = price;
    }
    void QueuePrivilege(int, int opcode, int flag) override {
        ++privileges; privOpcode = opcode; privFlag = flag;
    }
    void DispatchPanelEvent(int, int) override { ++panelEvents; }
};
} // namespace

TEST(LocationReconTownHall, CitizenshipWealthGate) {
    CitDeps d;
    d.wealth = townhall::kCitizenshipPrice - 1;
    CHECK(!TownHall_CitizenshipEligible(d));
    d.wealth = townhall::kCitizenshipPrice;        // boundary: >= is eligible
    CHECK(TownHall_CitizenshipEligible(d));
    d.wealth = townhall::kCitizenshipPrice + 1;
    CHECK(TownHall_CitizenshipEligible(d));
}

TEST(LocationReconTownHall, CitizenshipTutorialDelayFixed5) {
    CitDeps d; d.wealth = 20000; d.confirm = true; d.tutInactive = false; d.rand3 = 2;
    CitSink s;
    auto out = TownHall_GrantCitizenship(d, s, /*buyer*/3, /*office*/9);
    CHECK(out.eligible);
    CHECK(out.granted);
    CHECK_EQ(out.grantMonths, 5);          // tutorial -> fixed +5
    CHECK_EQ(s.grants, 1);                  // only the buyer (no family members)
    CHECK_EQ(s.lastMonths, 5);
    CHECK_EQ(s.payments, 1);
    CHECK_EQ(s.payPrice, townhall::kCitizenshipPrice);
    CHECK_EQ(s.payOffice, 9);
    CHECK_EQ(s.payBuyer, 3);
    CHECK_EQ(s.privileges, 1);
    CHECK_EQ(s.privOpcode, townhall::kCitizenshipReqOpcode);
    CHECK_EQ(s.privFlag, townhall::kCitizenshipFlagArg);
    CHECK_EQ(s.panelEvents, 1);
}

TEST(LocationReconTownHall, CitizenshipFreePlayDelayRandPlus1) {
    CitDeps d; d.wealth = 20000; d.confirm = true; d.tutInactive = true; d.rand3 = 2;
    CitSink s;
    auto out = TownHall_GrantCitizenship(d, s, 1, 1);
    CHECK_EQ(out.grantMonths, 3);          // rand%3 (=2) + 1
    d.rand3 = 0;
    CitSink s2;
    auto out2 = TownHall_GrantCitizenship(d, s2, 1, 1);
    CHECK_EQ(out2.grantMonths, 1);         // 0 + 1
}

TEST(LocationReconTownHall, CitizenshipFansOutToFamily) {
    CitDeps d; d.wealth = 20000; d.confirm = true; d.tutInactive = false;
    d.grantCap = 8;                              // byte_63CC1D high enough not to cap
    d.memStatus[0] = townhall::kMemberStatusA;  // 6 -> included
    d.memStatus[1] = 3;                          // other -> excluded
    d.memStatus[2] = townhall::kMemberStatusB;  // 7 -> included
    d.memStatus[3] = townhall::kMemberStatusA;  // 6 but...
    d.memOther[3]  = false;                      // ...same person -> excluded
    CitSink s;
    TownHall_GrantCitizenship(d, s, 1, 1);
    // buyer + member0 + member2 = 3 grants
    CHECK_EQ(s.grants, 3);
}

TEST(LocationReconTownHall, CitizenshipFamilyGrantCapStopsScan) {
    // byte_63CC1D caps the number of FAMILY grants emitted (not the buyer's).  With 3
    // eligible family slots but a cap of 1, only the first eligible family member is
    // granted: buyer + 1 = 2 grants.  1:1 with `if (byte_63CC1D <= v48) break;`.
    CitDeps d; d.wealth = 20000; d.confirm = true; d.tutInactive = false;
    d.grantCap = 1;
    d.memStatus[0] = townhall::kMemberStatusA;  // 6 -> first family grant
    d.memStatus[1] = townhall::kMemberStatusB;  // 7 -> would qualify but cap reached
    d.memStatus[2] = townhall::kMemberStatusA;  // 6 -> would qualify but cap reached
    CitSink s;
    TownHall_GrantCitizenship(d, s, 1, 1);
    CHECK_EQ(s.grants, 2);                       // buyer + exactly 1 family grant
}

TEST(LocationReconTownHall, CitizenshipZeroCapNoFamily) {
    // byte_63CC1D == 0 -> the family loop breaks immediately; only the buyer is granted.
    CitDeps d; d.wealth = 20000; d.confirm = true; d.tutInactive = false;
    d.grantCap = 0;
    d.memStatus[0] = townhall::kMemberStatusA;
    d.memStatus[1] = townhall::kMemberStatusB;
    CitSink s;
    TownHall_GrantCitizenship(d, s, 1, 1);
    CHECK_EQ(s.grants, 1);                       // buyer only
}

TEST(LocationReconTownHall, CitizenshipNoGrantWithoutConfirm) {
    CitDeps d; d.wealth = 20000; d.confirm = false;
    CitSink s;
    auto out = TownHall_GrantCitizenship(d, s, 1, 1);
    CHECK(out.eligible);
    CHECK(!out.granted);
    CHECK_EQ(s.grants, 0);
}

// ===========================================================================
// Building-info buy gate (0x51f2e8)
// ===========================================================================
namespace {
struct BInfoDeps : BuildingInfoDeps {
    bool afford = true, confirm = true;
    bool CanAfford(int) override { return afford; }
    bool ConfirmBuy(int) override { return confirm; }
};
struct BInfoSink : BuildingInfoBuySink {
    int buys = 0; int lastObj = -1, lastPrice = -1;
    void EnqueueBuy(int obj, int, int price) override { ++buys; lastObj = obj; lastPrice = price; }
};
} // namespace

TEST(LocationReconTownHall, BuildingRowOfferedFlagBit2) {
    BuildingInfoRow r{};
    r.personFlagByte = 0;  CHECK(!TownHall_BuildingRowOffered(r));
    r.personFlagByte = 2;  CHECK(TownHall_BuildingRowOffered(r));
    r.personFlagByte = 6;  CHECK(TownHall_BuildingRowOffered(r));  // bit 2 set
    r.personFlagByte = 1;  CHECK(!TownHall_BuildingRowOffered(r)); // bit 0 only
}

TEST(LocationReconTownHall, BuildingBuyHappyPath) {
    BuildingInfoRow r{}; r.price = 5000; r.buildingObj = 42; r.ownedByActive = false;
    BInfoDeps d; BInfoSink s;
    CHECK(TownHall_TryBuyBuilding(r, d, s));
    CHECK_EQ(s.buys, 1);
    CHECK_EQ(s.lastObj, 42);
    CHECK_EQ(s.lastPrice, 5000);
}

TEST(LocationReconTownHall, BuildingBuyBlockedWhenOwned) {
    BuildingInfoRow r{}; r.ownedByActive = true;
    BInfoDeps d; BInfoSink s;
    CHECK(!TownHall_TryBuyBuilding(r, d, s));
    CHECK_EQ(s.buys, 0);
}

TEST(LocationReconTownHall, BuildingBuyBlockedWhenUnaffordableOrUnconfirmed) {
    BuildingInfoRow r{}; r.ownedByActive = false;
    BInfoSink s;
    { BInfoDeps d; d.afford = false; CHECK(!TownHall_TryBuyBuilding(r, d, s)); }
    { BInfoDeps d; d.confirm = false; CHECK(!TownHall_TryBuyBuilding(r, d, s)); }
    CHECK_EQ(s.buys, 0);
}
