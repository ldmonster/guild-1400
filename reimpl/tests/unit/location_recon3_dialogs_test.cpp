#include "test.h"

#include "world/location_recon3_dialogs.h"

using namespace guild::world;
using namespace guild::world::loc3;

// ===========================================================================
// VIBE_Location_BriberyMenu 0x512a6c — gate / capacity.
// ===========================================================================
namespace {
struct BribeStub : BriberyDeps {
    bool valid = true, busy = true;
    int rivals = 0, freeH = 0, cap = 4;
    bool TargetValid() override { return valid; }
    bool AnimalTargetBusy() override { return busy; }
    int  RivalOfficialCount() override { return rivals; }
    int  FreeHandlerCount() override { return freeH; }
    int  CapacityByte() override { return cap; }
};
} // namespace

TEST(LocationRecon3, BriberyInvalidTargetBlocks) {
    BribeStub s; s.valid = false;
    CHECK(Bribery_Gate(s) == BriberyResult::CantBribeHere);
}

TEST(LocationRecon3, BriberyNotBusyBlocks) {
    BribeStub s; s.busy = false;
    CHECK(Bribery_Gate(s) == BriberyResult::CantBribeHere);
}

TEST(LocationRecon3, BriberyOverCapacity) {
    BribeStub s; s.cap = 4; s.rivals = 5; s.freeH = 3; // total 8 >= 2*4
    CHECK(Bribery_Gate(s) == BriberyResult::TooMany);
}

TEST(LocationRecon3, BriberyAtCapacityBoundaryBlocks) {
    BribeStub s; s.cap = 4; s.rivals = 4; s.freeH = 4; // total 8 == 2*4 -> >= -> TooMany
    CHECK(Bribery_Gate(s) == BriberyResult::TooMany);
}

TEST(LocationRecon3, BriberyJustUnderCapacityOpens) {
    BribeStub s; s.cap = 4; s.rivals = 3; s.freeH = 4; // total 7 < 8
    CHECK(Bribery_Gate(s) == BriberyResult::MenuOpened);
}

TEST(LocationRecon3, BriberyOverCapacityPredicate) {
    CHECK(Bribery_OverCapacity(8, 4));   // 8 >= 8
    CHECK(Bribery_OverCapacity(9, 4));
    CHECK(!Bribery_OverCapacity(7, 4));  // 7 < 8
    CHECK(Bribery_OverCapacity(0, 0));   // 0 >= 0
}

// ===========================================================================
// VIBE_Location_TradeTransport 0x513568 — bar fill, rebuild, row enable.
// ===========================================================================
TEST(LocationRecon3, TransportConstants) {
    CHECK(kTransportBarCap == 0.25);
    // dbl_621918 == 1/750.
    CHECK(kTransportBarRate > 0.00133 && kTransportBarRate < 0.00134);
}

TEST(LocationRecon3, TransportBarFillBelowCap) {
    // 100 ticks * (1/750) = 0.13333... < 0.25
    f32 f = Transport_BarFill(100);
    CHECK(f > 0.133f && f < 0.134f);
}

TEST(LocationRecon3, TransportBarFillClampsToQuarter) {
    // 1000 ticks * (1/750) = 1.333 >= 0.25 -> clamps to exactly 0.25
    CHECK_EQ(Transport_BarFill(1000), 0.25f);
    // exactly at the cap (187.5 ticks would give 0.25; 188 ticks > cap -> 0.25)
    CHECK_EQ(Transport_BarFill(200), 0.25f);
}

TEST(LocationRecon3, TransportBarFillZero) {
    CHECK_EQ(Transport_BarFill(0), 0.0f);
}

TEST(LocationRecon3, TransportRebuildExport) {
    TransportRebuild r = Transport_RebuildFor(1);
    CHECK_EQ(r.subtype, kTransportSubExport); // 475
    CHECK(r.textId == kTransportTextExport);  // 0x1854
}

TEST(LocationRecon3, TransportRebuildImport) {
    TransportRebuild r = Transport_RebuildFor(0);
    CHECK_EQ(r.subtype, kTransportSubImport); // 476
    CHECK(r.textId == kTransportTextImport);  // 0x1855
}

TEST(LocationRecon3, TransportRowEnableExport) {
    TransportRowEnable e = Transport_RowEnable(1);
    CHECK_EQ(e.exportRow, 1);
    CHECK_EQ(e.importRow, 0);
}

TEST(LocationRecon3, TransportRowEnableImport) {
    TransportRowEnable e = Transport_RowEnable(0);
    CHECK_EQ(e.exportRow, 0);
    CHECK_EQ(e.importRow, 1);
}

// ===========================================================================
// VIBE_Location_TradeSearchExport/Import 0x513c60 / 0x5142ec.
// ===========================================================================
TEST(LocationRecon3, SearchGoodTypeTables) {
    CHECK_EQ(kSearchExportGoods[0], 452);
    CHECK_EQ(kSearchExportGoods[1], 453);
    CHECK_EQ(kSearchExportGoods[2], 454);
    CHECK_EQ(kSearchImportGoods[0], 449);
    CHECK_EQ(kSearchImportGoods[1], 450);
    CHECK_EQ(kSearchImportGoods[2], 451);
}

TEST(LocationRecon3, SearchExportRowCount) {
    // type byte 8 -> 2 rows
    CHECK_EQ(Search_ExportRowCount(8, 5), 2);
    // capacity < 2 -> 2 rows
    CHECK_EQ(Search_ExportRowCount(3, 1), 2);
    CHECK_EQ(Search_ExportRowCount(3, 0), 2);
    // otherwise 3
    CHECK_EQ(Search_ExportRowCount(3, 2), 3);
    CHECK_EQ(Search_ExportRowCount(0, 9), 3);
    CHECK_EQ(kSearchImportRowCount, 3);
}

TEST(LocationRecon3, SearchAggregateSumsByGoodType) {
    // 4 handlers: two of good 452 (weights 3,5), one of 454 (weight 7), one off-table (999).
    SearchHandler hs[4] = {
        {452, 3}, {454, 7}, {452, 5}, {999, 100},
    };
    int out[3] = {7, 7, 7};
    Search_Aggregate(kSearchExportGoods, hs, 4, out);
    CHECK_EQ(out[0], 8);   // 452: 3 + 5
    CHECK_EQ(out[1], 0);   // 453: none
    CHECK_EQ(out[2], 7);   // 454: 7
}

TEST(LocationRecon3, SearchAggregateLowByteOnly) {
    // weightByte is the original's +172 byte (8-bit). 0x103 must truncate to 0x03.
    SearchHandler hs[1] = {{449, 0x103}};
    int out[3] = {0, 0, 0};
    Search_Aggregate(kSearchImportGoods, hs, 1, out);
    CHECK_EQ(out[0], 3);
}

TEST(LocationRecon3, SearchAggregateEmpty) {
    int out[3] = {1, 2, 3};
    Search_Aggregate(kSearchExportGoods, nullptr, 0, out);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 0);
    CHECK_EQ(out[2], 0);
}

TEST(LocationRecon3, SearchRowMessageIdExport) {
    CHECK_EQ(Search_RowMessageId(1, kExportMsgOne, kExportMsgMany), kExportMsgOne);  // 5134
    CHECK_EQ(Search_RowMessageId(5, kExportMsgOne, kExportMsgMany), kExportMsgMany); // 5135
    CHECK_EQ(Search_RowMessageId(0, kExportMsgOne, kExportMsgMany), 0);              // "%s"
}

TEST(LocationRecon3, SearchRowMessageIdImport) {
    CHECK_EQ(Search_RowMessageId(1, kImportMsgOne, kImportMsgMany), kImportMsgOne);  // 5142
    CHECK_EQ(Search_RowMessageId(9, kImportMsgOne, kImportMsgMany), kImportMsgMany); // 5143
    CHECK_EQ(Search_RowMessageId(0, kImportMsgOne, kImportMsgMany), 0);
}

// ===========================================================================
// VIBE_Location_ResidenceMistress 0x515524.
// ===========================================================================
TEST(LocationRecon3, MistressTakeEnabledUnderLimit) {
    CHECK(Mistress_TakeEnabled(0));
    CHECK(Mistress_TakeEnabled(3));   // 3 < 4
    CHECK(!Mistress_TakeEnabled(4));  // 4 is not < 4
    CHECK(!Mistress_TakeEnabled(5));
}

TEST(LocationRecon3, MistressHeaderText) {
    CHECK(Mistress_HeaderText(1) == kMistressHdrMale);   // 0x165E
    CHECK(Mistress_HeaderText(7) == kMistressHdrMale);
    CHECK(Mistress_HeaderText(0) == kMistressHdrFemale); // 0x165D
}

TEST(LocationRecon3, MistressConstants) {
    CHECK_EQ(kMistressHandlerOp, 111);
    CHECK_EQ(kMistressMaxRecorded, 16);
    CHECK_EQ(kMistressTakeLimit, 4);
}

// ===========================================================================
// VIBE_Location_TavernStammtischJoin/Leave 0x517a58 / 0x517724.
// ===========================================================================
TEST(LocationRecon3, StammtischCardGeometryEvenOdd) {
    // window 300 wide, card 60 wide -> w = 300 - 120 = 180; cw = 60; c = 0.
    // i=0 (parity 0): x = 0 + 60*1 + 10*(-1) + 60*0 = 50;  y = 100.
    StammtischCard c0 = Stammtisch_CardGeometry(0, 300, 60);
    CHECK_EQ(c0.x, 50);
    CHECK_EQ(c0.y, 100);
    // i=1 (parity 1): x = 0 + 60*2 + 10*0 + 60*1 = 180; y = 100.
    StammtischCard c1 = Stammtisch_CardGeometry(1, 300, 60);
    CHECK_EQ(c1.x, 180);
    CHECK_EQ(c1.y, 100);
    // i=2 (parity 0): x = 50; y = 130*1 + 100 = 230.
    StammtischCard c2 = Stammtisch_CardGeometry(2, 300, 60);
    CHECK_EQ(c2.x, 50);
    CHECK_EQ(c2.y, 230);
    // i=3 (parity 1): x = 180; y = 230.
    StammtischCard c3 = Stammtisch_CardGeometry(3, 300, 60);
    CHECK_EQ(c3.x, 180);
    CHECK_EQ(c3.y, 230);
}

TEST(LocationRecon3, StammtischCardGeometryRemainder) {
    // w = 200 - 2*30 = 140; cw = 46; c = (140 % 3)/2 = (2)/2 = 1.
    // i=0: x = 1 + 46*1 + 10*(-1) + 30*0 = 37; y = 100.
    StammtischCard c0 = Stammtisch_CardGeometry(0, 200, 30);
    CHECK_EQ(c0.x, 37);
    CHECK_EQ(c0.y, 100);
}

TEST(LocationRecon3, StammtischJoinStateFull) {
    // 4 seated, player not among them -> Full(2).
    CHECK(Stammtisch_JoinState(4, false, false) == StammtischState::Full);
}

TEST(LocationRecon3, StammtischJoinStateOpening) {
    // <4 seated, not here, not elsewhere -> AlreadyMember(3) = join offer.
    CHECK(Stammtisch_JoinState(2, false, false) == StammtischState::AlreadyMember);
    CHECK(Stammtisch_JoinState(0, false, false) == StammtischState::AlreadyMember);
}

TEST(LocationRecon3, StammtischJoinStateElsewhere) {
    // <4 seated, not here, but seated elsewhere -> MemberElsewhere(4).
    CHECK(Stammtisch_JoinState(1, false, true) == StammtischState::MemberElsewhere);
}

TEST(LocationRecon3, StammtischJoinStatePlayerSeated) {
    // player already at this table -> stays CanJoin(1) (the leave path).
    CHECK(Stammtisch_JoinState(3, true, false) == StammtischState::CanJoin);
    CHECK(Stammtisch_JoinState(4, true, false) == StammtischState::CanJoin);
}

TEST(LocationRecon3, StammtischConstants) {
    CHECK_EQ(kStammtischSeats, 4);
    CHECK_EQ(kHotkeyLeave, 37);
    CHECK_EQ(kHotkeySwapBuilding, 48);
    CHECK_EQ(kStammtischShopSub, 301);
}

// ===========================================================================
// VIBE_Location_TavernDarkCornerBrowse 0x51816c.
// ===========================================================================
TEST(LocationRecon3, DarkCornerHireBlocked) {
    CHECK(DarkCorner_HireOffer(true, 100000, 10) == DarkCornerOffer::Blocked);
}

TEST(LocationRecon3, DarkCornerHireAffordable) {
    // wealth 5000 > price 4000 -> HireButton.
    CHECK(DarkCorner_HireOffer(false, 5000, 4000) == DarkCornerOffer::HireButton);
}

TEST(LocationRecon3, DarkCornerHireTooPoor) {
    // wealth equal to price -> NOT strictly greater -> TooPoor (no offer).
    CHECK(DarkCorner_HireOffer(false, 4000, 4000) == DarkCornerOffer::TooPoor);
    CHECK(DarkCorner_HireOffer(false, 100, 4000) == DarkCornerOffer::TooPoor);
}

TEST(LocationRecon3, DarkCornerRowTextArg) {
    // 5 * (statusByte >> 24) + 4145.
    CHECK_EQ(DarkCorner_RowTextArg(0), 4145);
    // statusByte 0x02000000 >> 24 == 2 -> 5*2 + 4145 = 4155.
    CHECK_EQ(DarkCorner_RowTextArg(0x02000000), 4155);
}

TEST(LocationRecon3, DarkCornerConstants) {
    CHECK_EQ(kDarkCornerHandlerOp, 116);
    CHECK_EQ(kDarkCornerSlots, 3);
    CHECK_EQ(kDarkCornerMsgFilled, 5300);
    CHECK_EQ(kDarkCornerMsgEmpty, 5301);
}
