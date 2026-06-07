// Unit tests for guild::gui HUD floating-label placement / expiry / owner-marking.
// Golden vectors computed by hand (see hud_labels.cpp provenance) for the exact
// integer math of gilde.exe 0x4bbaec / 0x4bb7a0 / 0x4bcb74 / 0x4bcafc / 0x4bacb4.
#include "test.h"
#include "gui/hud_labels.h"
#include "gui/hud.h"

using namespace guild::gui;

// ---- Centred-caption placement: x = left + (right-left)/2 - 80 ; y = top --------
TEST(HudLabels, CenteredPlacementGolden) {
    struct { ScreenBounds b; int ex; int ey; } v[] = {
        {{100, 50, 260, 999}, 100, 50},
        {{0,   0, 160, 999},   0,  0},
        {{200, 30, 200, 999}, 120, 30},   // zero-width box -> -80 inset
        {{-40, 10,  40, 999}, -80, 10},   // straddles origin
        {{640, 200, 800, 999}, 640, 200},
    };
    for (auto& t : v) {
        LabelPlacement p = Hud_CenteredLabelPlacement(t.b);
        CHECK_EQ(p.x, t.ex);
        CHECK_EQ(p.y, t.ey);
    }
}

TEST(HudLabels, CenteredPlacementSignedDivideTruncates) {
    // (right-left) = 3 -> 3/2 == 1 (truncate toward zero), not rounded.
    LabelPlacement p = Hud_CenteredLabelPlacement({0, 0, 3, 0});
    CHECK_EQ(p.x, 0 + 1 - 80);
    // negative span truncates toward zero too: span -3 -> -1
    LabelPlacement q = Hud_CenteredLabelPlacement({3, 0, 0, 0});
    CHECK_EQ(q.x, 3 + (-1) - 80);
}

TEST(HudLabels, LabelConstants) {
    CHECK_EQ(kHudLabelWidth, 160);
    CHECK_EQ(kHudLabelHalf, 80);
    CHECK_EQ(kHudLabelCharH40, 40);
    CHECK_EQ(kHudLabelCharH32, 32);
}

// ---- Damage-label expiry sweep: timestamp + 300 < now (unsigned) ----------------
TEST(HudLabels, DamageExpireSweep) {
    ResetDamageLabels();
    // three live labels stamped at different times.
    DamageLabel_Register(/*source*/ 11, /*amount*/ 5, /*now*/ 100);
    DamageLabel_Register(22, 5, 200);
    DamageLabel_Register(33, 5, 500);
    // sweep at now=450: label@100 (100+300=400 < 450) expires; @200 (500 !< 450) and
    // @500 stay.
    int expired = DamageLabel_ExpireSweep(450);
    CHECK_EQ(expired, 1);
    CHECK_EQ(g_damageLabels[0].inUse, false);
    CHECK_EQ(g_damageLabels[1].inUse, true);
    CHECK_EQ(g_damageLabels[2].inUse, true);
}

TEST(HudLabels, DamageExpireBoundary) {
    ResetDamageLabels();
    DamageLabel_Register(7, 1, 100);
    // 100 + 300 = 400; strictly-less-than, so now=400 does NOT expire, now=401 does.
    CHECK_EQ(DamageLabel_ExpireSweep(400), 0);
    CHECK_EQ(g_damageLabels[0].inUse, true);
    CHECK_EQ(DamageLabel_ExpireSweep(401), 1);
    CHECK_EQ(g_damageLabels[0].inUse, false);
}

// ---- Status banner: x = right-300, scale select, lifetime 350 -------------------
TEST(HudLabels, StatusBanner) {
    CHECK_EQ(Hud_StatusBannerX(800), 500);
    CHECK_EQ(Hud_StatusBannerX(1024), 724);
    // 0x8000 set -> narrow (505), clear -> wide (2047).
    CHECK(Hud_StatusBannerScale(0x8000) == kStatusBannerScaleNarrow);
    CHECK(Hud_StatusBannerScale(0x0000) == kStatusBannerScaleWide);
    CHECK(Hud_StatusBannerScale(0x1234) == kStatusBannerScaleWide); // bit not set
    // lifetime: start+350 <= now expires.
    CHECK_EQ(Hud_StatusBannerExpired(100, 449), false);
    CHECK_EQ(Hud_StatusBannerExpired(100, 450), true);
    CHECK_EQ(Hud_StatusBannerExpired(100, 451), true);
}

// ---- Name-input caption: x=(x2>>16)-54 ; y=(y>>16)+52 ---------------------------
TEST(HudLabels, NameInputCaption) {
    LabelPlacement p = Hud_NameInputCaptionPlacement(0x012C0000, 0x00640000);
    CHECK_EQ(p.x, 300 - 54);  // 246
    CHECK_EQ(p.y, 100 + 52);  // 152
    LabelPlacement q = Hud_NameInputCaptionPlacement(0x00200000, 0x00100000);
    CHECK_EQ(q.x, 32 - 54);   // -22
    CHECK_EQ(q.y, 16 + 52);   // 68
    // arithmetic (sign-preserving) shift of a negative packed word.
    LabelPlacement r = Hud_NameInputCaptionPlacement((int)0xFFEC0000, 0x00000000);
    CHECK_EQ(r.x, -20 - 54);  // 0xFFEC = -20 after >>16
    CHECK_EQ(r.y, 0 + 52);
}

// ---- Player-owned object marking ------------------------------------------------
TEST(HudLabels, MarkOwnedObjects) {
    std::vector<OwnedObject> objs = {
        {/*handle*/ 1000, /*flags(prefill)*/ 0xFF},
        {2000, 0xFF},
        {3000, 0xFF},
    };
    std::vector<OwnerSlot> slots = {
        {/*active*/ true,  /*rec*/ 0x10, /*owner*/ 1000, /*flag*/ 1}, // marks obj0
        {/*active*/ true,  /*rec*/ 0x20, /*owner*/ 2000, /*flag*/ 0}, // flag bit clear -> no mark
        {/*active*/ false, /*rec*/ 0x30, /*owner*/ 3000, /*flag*/ 1}, // inactive -> skip
        {/*active*/ true,  /*rec*/ 0,    /*owner*/ 1000, /*flag*/ 1}, // recordPtr 0 -> skip
    };
    int marked = Hud_MarkOwnedObjects(objs, slots);
    CHECK_EQ(marked, 1);
    CHECK_EQ(objs[0].flags, 1);  // cleared to 0 then |= 1
    CHECK_EQ(objs[1].flags, 0);  // cleared, no matching active slot
    CHECK_EQ(objs[2].flags, 0);
}

TEST(HudLabels, MarkOwnedClearsWhenNoSlots) {
    std::vector<OwnedObject> objs = {{5, 0x7}, {6, 0x7}};
    std::vector<OwnerSlot> slots; // empty
    int marked = Hud_MarkOwnedObjects(objs, slots);
    CHECK_EQ(marked, 0);
    CHECK_EQ(objs[0].flags, 0);
    CHECK_EQ(objs[1].flags, 0);
}
