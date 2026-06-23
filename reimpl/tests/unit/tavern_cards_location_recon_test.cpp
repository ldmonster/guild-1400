#include "test.h"

#include "world/tavern_cards_location_recon.h"

using namespace guild::world;

// ===========================================================================
// Shared geometry — x = 28, +58/col, wrap to 0 every 8, sprite 29 then 70.
// ===========================================================================
TEST(LocationReconTavernCards, LayoutFirstRowGeometry) {
    CardSlot slots[20] = {};
    int n = Tavern_LayoutCardSlots(8, slots, 20);
    CHECK_EQ(n, 8);
    // x positions: 28, 86, 144, 202, 260, 318, 376, 434
    int expX[8] = {28, 86, 144, 202, 260, 318, 376, 434};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(slots[i].x, expX[i]);
        CHECK_EQ(slots[i].sprite, tavern_cards::kSpriteEarly);  // all 29 in the first 8
    }
}

TEST(LocationReconTavernCards, LayoutWrapAndLateSprite) {
    CardSlot slots[20] = {};
    int n = Tavern_LayoutCardSlots(10, slots, 20);
    CHECK_EQ(n, 10);
    // slot index 8 is the start of row 2: x resets to 0, sprite becomes 70.
    CHECK_EQ(slots[8].x, 0);
    CHECK_EQ(slots[8].sprite, tavern_cards::kSpriteLate);  // 70
    CHECK_EQ(slots[9].x, 58);                              // 0 + 58
    CHECK_EQ(slots[9].sprite, tavern_cards::kSpriteLate);
}

TEST(LocationReconTavernCards, LayoutStateMachineMatchesOriginal) {
    // Replay the original loop tail directly and compare.
    CardSlotLayout L;
    int x = tavern_cards::kStartX, sprite = tavern_cards::kSpriteEarly, count = 0;
    for (int k = 0; k < 17; ++k) {
        CHECK_EQ(L.x(), x);
        CHECK_EQ(L.sprite(), sprite);
        // original post-placement update:
        ++count;
        if (count % 8) x += 58; else x = 0;
        sprite = (count <= 7) ? 29 : 70;
        L.advance();
    }
}

// ===========================================================================
// BuildCardSlotsSmall (0x5160f0) — exactly 3 face-down cards.
// ===========================================================================
TEST(LocationReconTavernCards, SmallPlacesThree) {
    CardSlot slots[8] = {};
    int n = Tavern_BuildCardSlotsSmall(slots, 8);
    CHECK_EQ(n, tavern_cards::kSmallSlots);
    CHECK_EQ(n, 3);
    CHECK_EQ(slots[0].x, 28);
    CHECK_EQ(slots[1].x, 86);
    CHECK_EQ(slots[2].x, 144);
    CHECK_EQ(slots[0].sprite, 29);
    CHECK_EQ(slots[1].sprite, 29);
    CHECK_EQ(slots[2].sprite, 29);
}

// ===========================================================================
// Hand / Table builders (0x51618c / 0x5162c0) — N cards, phase-byte agnostic geometry.
// ===========================================================================
TEST(LocationReconTavernCards, HandTablePhaseAgnosticGeometry) {
    CardSlot a[20] = {}, b[20] = {};
    // phase==6 vs phase!=6 produce identical geometry.
    int na = Tavern_BuildCardSlotsHand(6, 5, a, 20);
    int nb = Tavern_BuildCardSlotsHand(0, 5, b, 20);
    CHECK_EQ(na, 5);
    CHECK_EQ(nb, 5);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(a[i].x, b[i].x);
        CHECK_EQ(a[i].sprite, b[i].sprite);
    }
    // Table builder matches Hand geometry too.
    CardSlot t[20] = {};
    int nt = Tavern_BuildCardSlotsTable(6, 5, t, 20);
    CHECK_EQ(nt, 5);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(t[i].x, a[i].x);
        CHECK_EQ(t[i].sprite, a[i].sprite);
    }
}

TEST(LocationReconTavernCards, HandZeroCards) {
    CardSlot a[4] = {};
    CHECK_EQ(Tavern_BuildCardSlotsHand(6, 0, a, 4), 0);
}

// ===========================================================================
// RefreshCardWindows (0x516794) — pile routing + footer rule.
// ===========================================================================
TEST(LocationReconTavernCards, PileRouting) {
    CHECK_EQ(Tavern_PileTargetWindow(6), 3);   // "open" pile -> window 3
    CHECK_EQ(Tavern_PileTargetWindow(0), 2);   // other pile -> window 2
    CHECK_EQ(Tavern_PileTargetWindow(5), 2);
}

TEST(LocationReconTavernCards, FooterShowdownFullPotA) {
    // state8==5 && pileA.phase==6 -> showdown, full pot, text 0x1496
    auto f = Tavern_CardFooter(/*state8*/5, /*A*/6, /*B*/0, /*pot*/1000);
    CHECK_EQ(f.textId, tavern_cards::kFooterShowdownText);
    CHECK_EQ(f.value, 1000);
}

TEST(LocationReconTavernCards, FooterShowdownFullPotB) {
    // state8==6 && pileB.phase==6 -> showdown
    auto f = Tavern_CardFooter(6, 0, 6, 800);
    CHECK_EQ(f.textId, tavern_cards::kFooterShowdownText);
    CHECK_EQ(f.value, 800);
}

TEST(LocationReconTavernCards, FooterNormalHalfPot) {
    // not a showdown pairing -> half pot, text 0x1497
    auto f = Tavern_CardFooter(5, 0, 6, 1000);  // state8==5 but pileA!=6, pileB irrelevant
    CHECK_EQ(f.textId, tavern_cards::kFooterNormalText);
    CHECK_EQ(f.value, 500);                      // 1000 / 2

    auto g = Tavern_CardFooter(6, 6, 0, 999);    // state8==6 needs pileB==6, it's 0
    CHECK_EQ(g.textId, tavern_cards::kFooterNormalText);
    CHECK_EQ(g.value, 499);                       // 999 / 2 (integer)
}
