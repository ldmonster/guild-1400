// Golden unit tests for the UI/panel cluster (ui_recon5_panels).
// Vectors are derived directly from the gilde.exe decompile: bit masks, loop
// bounds, dispatch flags, sort/clamp arithmetic. No main() (shared test_main).
#include "play/ui_recon5_panels.h"
#include "test.h"

using namespace guild::play;

// --- 0x5596ce/db/e8 — HUD status flag-bit thunks ----------------------------
TEST(UiRecon5, HudClearFlagBits) {
    CHECK_EQ(HudClearStatusFlagBit2(0xFF), (guild::u8)0xFD); // ~2
    CHECK_EQ(HudClearStatusFlagBit4(0xFF), (guild::u8)0xFB); // ~4
    CHECK_EQ(HudClearStatusFlagBit8(0xFF), (guild::u8)0xF7); // ~8
    // bit already clear -> unchanged
    CHECK_EQ(HudClearStatusFlagBit2(0x01), (guild::u8)0x01);
    CHECK_EQ(HudClearStatusFlagBit4(0x0B), (guild::u8)0x0B);
    CHECK_EQ(HudClearStatusFlagBit8(0x07), (guild::u8)0x07);
    // only the targeted bit is touched
    CHECK_EQ(HudClearStatusFlagBit2(0x06), (guild::u8)0x04);
    CHECK_EQ(HudClearStatusFlagBit4(0x06), (guild::u8)0x02);
    CHECK_EQ(HudClearStatusFlagBit8(0x0E), (guild::u8)0x06);
}

// --- 0x4b1ba8 — PlayerBar slot-table reset ----------------------------------
TEST(UiRecon5, PlayerBarResetSlots) {
    PlayerBarSlot slots[kPlayerBarSlotCount];
    int n = PlayerBarResetSlots(slots, kPlayerBarSlotCount);
    CHECK_EQ(n, 32);               // 320/10
    CHECK_EQ(kPlayerBarSlotCount, 32);
    CHECK_EQ(kPlayerBarSlotStride, 10);
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(slots[i].objA, -1);
        CHECK_EQ(slots[i].objB, -1);
        CHECK_EQ(slots[i].objC, -1);
        CHECK_EQ(slots[i].handle, 0xFFFFu); // 0xFFFF, NOT -1
        CHECK_EQ(slots[i].objD, -1);
        CHECK_EQ(slots[i].objE, -1);
        CHECK_EQ(slots[i].flag, (guild::u8)0);
    }
    // honors cap
    int n2 = PlayerBarResetSlots(slots, 5);
    CHECK_EQ(n2, 5);
}

// --- 0x548c54 — Abduct destination gate FSM ---------------------------------
TEST(UiRecon5, AbductGateOrder) {
    // First failing gate wins, in source order.
    CHECK(AbductEvalGate(false, true, true, true, 5, true) == AbductGate::DenySkillLevel2);
    CHECK(AbductEvalGate(true, false, true, true, 5, true) == AbductGate::DenyNullTarget);
    CHECK(AbductEvalGate(true, true, false, true, 5, true) == AbductGate::DenyOfficePick);
    CHECK(AbductEvalGate(true, true, true, false, 5, true) == AbductGate::DenyOfficeConfirm);
    CHECK(AbductEvalGate(true, true, true, true, 0, true) == AbductGate::NoEntities);
    CHECK(AbductEvalGate(true, true, true, true, -3, true) == AbductGate::NoEntities);
    CHECK(AbductEvalGate(true, true, true, true, 5, false) == AbductGate::DenySkillLevel3);
    CHECK(AbductEvalGate(true, true, true, true, 5, true) == AbductGate::ShowPicker);
    // exactly 1 entity still passes the >0 gate
    CHECK(AbductEvalGate(true, true, true, true, 1, true) == AbductGate::ShowPicker);
}
TEST(UiRecon5, AbductConstants) {
    CHECK_EQ(kAbductActionKind, (guild::u8)47);
    CHECK_EQ(kAbductCmdTag, (guild::u8)9);
    CHECK_EQ(kAbductBuildOp, 90);
    CHECK_EQ(kAbductBuildOpArg, -3);
    CHECK_EQ(kAbductPickHeaderId, (guild::u16)4978);
    CHECK_EQ(kAbductDestNameId, (guild::u16)4979);
    CHECK_EQ(kAbductNoneMsgId, (guild::u16)4969);
    CHECK_EQ(kAbductDoneMsgId, (guild::u16)4981);
    CHECK_EQ(kAbductFrameLoopId, (guild::u32)423879);
    // singular/plural template id
    CHECK_EQ(AbductListTextId(1), (guild::u16)4965); // count==1 -> +0
    CHECK_EQ(AbductListTextId(2), (guild::u16)4966); // else     -> +1
    CHECK_EQ(AbductListTextId(0), (guild::u16)4966);
}

// --- 0x55bff8 — InfoPanel trait-row bitfield mapping -------------------------
TEST(UiRecon5, InfoPanelTraitRowsGate) {
    InfoPanelTraitRow rows[9];
    // whole block gated off
    CHECK_EQ(InfoPanelTraitRows(false, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                                0xFFFFFFFF, 0xFFFFFFFF, rows, 9), 0);
    // no masks set -> no rows
    CHECK_EQ(InfoPanelTraitRows(true, 0, 0, 0, 0, 0, rows, 9), 0);
}
TEST(UiRecon5, InfoPanelTraitRowsAll) {
    InfoPanelTraitRow rows[9];
    int n = InfoPanelTraitRows(true,
                               0xFF,        // word22: 0x0F & 0xF0 -> 4657,4658
                               0xFF,        // word45: 0x0F & 0x30 -> 4659,4660
                               0x1C000,     // word11: 0x1C000     -> 4661
                               0x1FF,       // word23: 0x0E,0x70,0x180 -> 4662,4663,4664
                               0x1E,        // word47: 0x1E        -> 4665
                               rows, 9);
    CHECK_EQ(n, 9);
    CHECK_EQ(rows[0].textId, (guild::u16)4657);
    CHECK_EQ(rows[1].textId, (guild::u16)4658);
    CHECK_EQ(rows[2].textId, (guild::u16)4659);
    CHECK_EQ(rows[3].textId, (guild::u16)4660);
    CHECK_EQ(rows[4].textId, (guild::u16)4661);
    CHECK_EQ(rows[5].textId, (guild::u16)4662);
    CHECK_EQ(rows[6].textId, (guild::u16)4663);
    CHECK_EQ(rows[7].textId, (guild::u16)4664);
    CHECK_EQ(rows[8].textId, (guild::u16)4665);
    // advance-row column mirrors source shape
    CHECK(rows[3].advanceRow == false); // 4660 bare ++
    CHECK(rows[6].advanceRow == false); // 4663 bare ++
    CHECK(rows[8].advanceRow == false); // 4665 no advance
    CHECK(rows[0].advanceRow == true);
}
TEST(UiRecon5, InfoPanelTraitRowsSubset) {
    InfoPanelTraitRow rows[9];
    // only word23 nibbles
    int n = InfoPanelTraitRows(true, 0, 0, 0, 0x0E, 0, rows, 9);
    CHECK_EQ(n, 1);
    CHECK_EQ(rows[0].textId, (guild::u16)4662);
    // word11 needs the 0x1C000 bits specifically; 0x1 alone (with word11 nonzero
    // enabling the block) must NOT emit 4661.
    n = InfoPanelTraitRows(true, 0, 0, 0x1, 0, 0, rows, 9);
    CHECK_EQ(n, 0);
}
TEST(UiRecon5, InfoPanelBasesAndAge) {
    CHECK_EQ(InfoPanelFatherBase(true), 370);
    CHECK_EQ(InfoPanelFatherBase(false), 294);
    CHECK_EQ(InfoPanelMotherBase(true), 498);
    CHECK_EQ(InfoPanelMotherBase(false), 471);
    CHECK_EQ(InfoPanelProfBase(true), 560);
    CHECK_EQ(InfoPanelProfBase(false), 525);
    CHECK_EQ(InfoPanelAgeTextId(117), 1067);
    CHECK_EQ(InfoPanelAgeTextId(116), 1070);
    CHECK_EQ(InfoPanelAgeTextId(1000), 1067);
}

// --- 0x55a9f8 — City tower pennant interpolation -----------------------------
TEST(UiRecon5, CityTowerPennantPos) {
    Vec2 lo{ 10.0f, 20.0f };
    Vec2 ru{ 110.0f, 220.0f }; // dx=100, dz=200
    // corner 0,0 -> exactly linksOben
    Vec2 a = CityTowerPennantPos(lo, ru, 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(a.x == 10.0f);
    CHECK(a.z == 20.0f);
    // corner 1,1 scale 1 -> full span
    Vec2 b = CityTowerPennantPos(lo, ru, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(b.x == 110.0f);
    CHECK(b.z == 220.0f);
    // half scale
    Vec2 c = CityTowerPennantPos(lo, ru, 1.0f, 1.0f, 0.5f, 0.5f);
    CHECK(c.x == 60.0f);  // 10 + 0.5*100
    CHECK(c.z == 120.0f); // 20 + 0.5*200
    CHECK_EQ(kCityTowerPennantCount, 4);
    CHECK_EQ(kCityTowerFrameLoopId, (guild::u32)425983);
}

// --- 0x5441d0 — MapView dispatcher structure --------------------------------
TEST(UiRecon5, MapViewDispatchFlags) {
    CHECK_EQ(kMapViewModeTooltip, (guild::u8)0x10);
    CHECK_EQ(kMapViewModeAuflauer, (guild::u8)0x04);
    CHECK_EQ(kMapViewModeMissions, (guild::u8)0x08);
    CHECK_EQ(kMapViewModeReturnObj, (guild::u8)0x01);
    CHECK_EQ(kMapViewModePersonSel, (guild::u8)0x02);
    CHECK_EQ(kMapViewSkipTypeA, (guild::u8)68);
    CHECK_EQ(kMapViewSkipTypeB, (guild::u8)69);
    CHECK_EQ(kMapViewTypeMoney, (guild::u8)71);
    CHECK_EQ(kMapViewMaxMarkers, 256);
    CHECK_EQ(kMapViewMarkerStride, 24);
    CHECK_EQ(kMapViewFrameLoopId, (guild::u32)423879);
}
TEST(UiRecon5, MapViewRadioTable) {
    CHECK_EQ(kMapViewRadioCount, 8);
    const i32 expY[8]   = {88,166,218,270,354,406,458,536};
    const i32 expSp[8]  = {1334,1338,1340,1337,1335,1339,1336,1341};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(kMapViewRadioY[i], expY[i]);
        CHECK_EQ(kMapViewRadioSprite[i], expSp[i]);
    }
}
TEST(UiRecon5, MapViewSortMarkers) {
    MapMarker m[4] = {
        {0,0,0,300, 0,0},
        {1,0,0,100, 0,0},
        {2,0,0,200, 0,0},
        {3,0,0, 50, 0,0},
    };
    MapViewSortMarkersByScreenY(m, 4);
    CHECK_EQ(m[0].screenY, 50);
    CHECK_EQ(m[1].screenY, 100);
    CHECK_EQ(m[2].screenY, 200);
    CHECK_EQ(m[3].screenY, 300);
    // whole records moved (entity ids follow the screenY they came with)
    CHECK_EQ(m[0].obj, 3);
    CHECK_EQ(m[3].obj, 0);
}
TEST(UiRecon5, MapViewScrollDelta) {
    MapScrollDelta u = MapViewScrollDelta(true, false, false, false);
    CHECK(u.active); CHECK_EQ(u.dx, 0); CHECK_EQ(u.dy, -4);
    MapScrollDelta d = MapViewScrollDelta(false, true, false, false);
    CHECK_EQ(d.dy, 4);
    MapScrollDelta l = MapViewScrollDelta(false, false, true, false);
    CHECK_EQ(l.dx, -4); CHECK_EQ(l.dy, 0);
    MapScrollDelta r = MapViewScrollDelta(false, false, false, true);
    CHECK_EQ(r.dx, 4);
    // priority: up beats all
    MapScrollDelta pri = MapViewScrollDelta(true, true, true, true);
    CHECK_EQ(pri.dy, -4); CHECK_EQ(pri.dx, 0);
    // none
    MapScrollDelta none = MapViewScrollDelta(false, false, false, false);
    CHECK(!none.active);
}
TEST(UiRecon5, MapViewClampFocus) {
    // mapW=1024,mapH=768 -> x in [0,512], y in [0,408]
    MapClampPos a = MapViewClampFocus(-5, -5, 1024, 768);
    CHECK_EQ(a.x, 0); CHECK_EQ(a.y, 0);
    MapClampPos b = MapViewClampFocus(9999, 9999, 1024, 768);
    CHECK_EQ(b.x, 1024 - 512); // 512
    CHECK_EQ(b.y, 768 - 360);  // 408
    MapClampPos c = MapViewClampFocus(100, 100, 1024, 768);
    CHECK_EQ(c.x, 100); CHECK_EQ(c.y, 100);
    CHECK_EQ(kMapViewClampMarginX, 512);
    CHECK_EQ(kMapViewClampMarginY, 360);
}

// --- 0x4ba614 — Hud selection shadow/count ----------------------------------
TEST(UiRecon5, HudShadowSelection) {
    CHECK_EQ(kHudSelectionCount, 768);
    CHECK_EQ(kHudSelectionStride, 536);
    guild::u8 cur[8]  = {0,1,0,1,1,0,0,1};
    guild::u8 prev[8] = {9,9,9,9,9,9,9,9};
    int sel = -1;
    int w = HudShadowSelection(cur, prev, 8, &sel);
    CHECK_EQ(w, 8);
    CHECK_EQ(sel, 4); // four nonzero
    for (int i = 0; i < 8; ++i) CHECK_EQ(prev[i], cur[i]);
}
TEST(UiRecon5, HudClearAfterCount) {
    CHECK(HudShouldClearAfterCount(true, 5) == true);
    CHECK(HudShouldClearAfterCount(true, 0) == false);
    CHECK(HudShouldClearAfterCount(false, 5) == false);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-12): the panel writers take a caller buffer + capacity. A cap
// SMALLER than the logical row/slot count must stop writing at the cap (never
// past the caller array). A zero-length selection table must write nothing.
// These pin the existing cap guards (caught by ASAN if a write escaped).
// ---------------------------------------------------------------------------
TEST(UiRecon5, PlayerBarResetCapSmallerThanSlots) {
    PlayerBarSlot slots[3];
    int n = PlayerBarResetSlots(slots, 3);   // cap 3 < 32 logical slots
    CHECK_EQ(n, 3);
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(slots[i].objA, -1);
        CHECK_EQ(slots[i].handle, 0xFFFFu);
    }
    // cap 0 -> nothing written.
    PlayerBarSlot one[1];
    CHECK_EQ(PlayerBarResetSlots(one, 0), 0);
}

TEST(UiRecon5, InfoPanelTraitRowsCapSmallerThanEmitted) {
    // All masks set would emit 9 rows; a cap of 2 must stop at 2.
    InfoPanelTraitRow rows[2];
    int n = InfoPanelTraitRows(true, 0xFF, 0xFF, 0x1C000, 0x1FF, 0x1E, rows, 2);
    CHECK_EQ(n, 2);
    CHECK_EQ(rows[0].textId, (guild::u16)4657);
    CHECK_EQ(rows[1].textId, (guild::u16)4658);
    // cap 0 -> nothing written.
    InfoPanelTraitRow none[1];
    CHECK_EQ(InfoPanelTraitRows(true, 0xFF, 0xFF, 0x1C000, 0x1FF, 0x1E, none, 0), 0);
}

TEST(UiRecon5, HudShadowSelectionZeroLength) {
    guild::u8 cur[1] = {1};
    guild::u8 prev[1] = {9};
    int sel = -1;
    int w = HudShadowSelection(cur, prev, 0, &sel);   // count 0 -> no reads/writes
    CHECK_EQ(w, 0);
    CHECK_EQ(sel, 0);
    CHECK_EQ(prev[0], (guild::u8)9);                   // untouched
}

TEST(UiRecon5, MapViewSortMarkersZeroAndOneElement) {
    MapViewSortMarkersByScreenY(nullptr, 0);   // empty -> no access
    MapMarker one[1] = { {7,0,0,42, 0,0} };
    MapViewSortMarkersByScreenY(one, 1);       // single -> no swap, no OOB
    CHECK_EQ(one[0].screenY, 42);
}
