// Integration tests: HUD floating-label placement/expiry against the REAL sibling
// HUD modules (hud.cpp damage-label table + register/evict, hud_draw object-action
// panel, hud_grid tiled rows).  This exercises the full DrawDamageLabels flow:
// register labels via the real VIBE_DamageLabel_RegisterEntry, then run the real
// expiry sweep + centred placement and confirm the survivors still resolve.
#include "test.h"
#include "gui/hud.h"
#include "gui/hud_labels.h"
#include "gui/hud_draw.h"
#include "gui/hud_grid.h"

using namespace guild::gui;

// Register labels through the real sibling (hud.cpp), expire through this module's
// sweep, and confirm the table state is consistent end-to-end.
TEST(HudLabelsItest, RegisterThenExpireRoundtrip) {
    ResetDamageLabels();
    // Real register: three sources at distinct ticks.
    int s0 = DamageLabel_Register(101, 7, 1000);
    int s1 = DamageLabel_Register(102, 3, 1100);
    int s2 = DamageLabel_Register(103, 9, 1500);
    CHECK(s0 >= 0 && s1 >= 0 && s2 >= 0);
    CHECK(s0 != s1 && s1 != s2);

    // Re-register an existing source: de-dups to the same slot and (matching the real
    // VIBE_DamageLabel_RegisterEntry) returns early WITHOUT restamping the timestamp,
    // so s1's expiry clock stays at 1100.
    CHECK_EQ(DamageLabel_Register(102, 99, 1200), s1);
    CHECK_EQ(g_damageLabels[s1].timestamp, 1100);

    // Sweep at now=1450: s0 (1000+300=1300 < 1450) and s1 (1100+300=1400 < 1450) both
    // expire; s2 (1500+300 not < 1450) survives.
    int expired = DamageLabel_ExpireSweep(1450);
    CHECK_EQ(expired, 2);
    CHECK_EQ(g_damageLabels[s0].inUse, false);
    CHECK_EQ(g_damageLabels[s1].inUse, false);
    CHECK_EQ(g_damageLabels[s2].inUse, true);

    // The freed slot is now reusable by the real register path.
    int s3 = DamageLabel_Register(104, 1, 1600);
    CHECK_EQ(s3, s0);            // first-free scan reclaims slot 0
    CHECK_EQ(g_damageLabels[s3].source, 104);
}

// The centred-label placement math is the same one the object-action panel and the
// person/training grids assume for label-anchored content; cross-check that a label
// centred over a box matches the geometry the grid pitch produces.
TEST(HudLabelsItest, CenteredPlacementMatchesGridGeometry) {
    // A training row backdrop is 87 wide-ish; centring a 160 label over a 64px-wide
    // box gives left + (64/2) - 80.
    LabelPlacement p = Hud_CenteredLabelPlacement({64, 12, 128, 400});
    CHECK_EQ(p.x, 64 + 32 - 80);   // 16
    CHECK_EQ(p.y, 12);

    // Reuse the real training-row builder and confirm its slot x lines up with a
    // caption centred over it (sanity that we share the same coordinate space).
    TrainingRow row = Hud_BuildTrainingRow(/*index*/ 0, /*gfx*/ 1726);
    LabelPlacement cap = Hud_CenteredLabelPlacement(
        {row.childX, row.childY, row.childX + 87, 999});
    CHECK_EQ(cap.x, row.childX + 43 - 80);
    CHECK_EQ(cap.y, row.childY);
}

// Owner-marking against a slot table that mirrors the real stride-table shape.
TEST(HudLabelsItest, MarkOwnedAcrossManyObjects) {
    std::vector<OwnedObject> objs;
    for (int h = 0; h < 8; ++h)
        objs.push_back({/*handle*/ 100 + h, /*flags*/ 0xCAFE});
    std::vector<OwnerSlot> slots = {
        {true, 0x1, 100, 1},  // -> obj 0
        {true, 0x2, 103, 1},  // -> obj 3
        {true, 0x3, 107, 3},  // flag bit0 set (3&1) -> obj 7
        {true, 0x4, 105, 2},  // flag bit0 clear -> no mark
        {false,0x5, 102, 1},  // inactive
    };
    int marked = Hud_MarkOwnedObjects(objs, slots);
    CHECK_EQ(marked, 3);
    CHECK_EQ(objs[0].flags, 1);
    CHECK_EQ(objs[3].flags, 1);
    CHECK_EQ(objs[7].flags, 1);
    CHECK_EQ(objs[5].flags, 0);  // no slot
    CHECK_EQ(objs[2].flags, 0);
}

// Status-banner + name-input caption lifetimes share the HUD tick clock; confirm the
// two timeouts (350 banner / per-frame caption) behave consistently around a tick.
TEST(HudLabelsItest, BannerAndCaptionLifetimes) {
    // Banner armed at tick 2000.
    CHECK_EQ(Hud_StatusBannerExpired(2000, 2349), false);
    CHECK_EQ(Hud_StatusBannerExpired(2000, 2350), true);
    // Caption placement from a packed world anchor used the same frame.
    LabelPlacement cap = Hud_NameInputCaptionPlacement(0x00C80000, 0x00960000);
    CHECK_EQ(cap.x, 200 - 54);  // x2hi=200
    CHECK_EQ(cap.y, 150 + 52);  // yhi=150
}
