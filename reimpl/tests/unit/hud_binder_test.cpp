// Unit: HUD overlay binder — the layout math (label/button-row positions, player-bar
// slot layout) and tooltip text-id selection / classification, golden cases.
#include "test.h"

#include "play/hud_binder.h"

using namespace guild;
using guild::play::HudOverlayBinder;

// ---------------------------------------------------------------------------
// Label caption layout — gui::Hud_LabelLayout golden x-placement per alignment.
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, CaptionLayoutGoldenPerAlignment) {
    HudOverlayBinder b;
    b.reset();

    // Centered: x = anchor - width/2 ; flag88 = 1.
    gui::HudLabelLayout c = b.layoutCaption(gui::HudLabelAlign::kCentered, 320, 80);
    CHECK_EQ(c.x, 280);
    CHECK_EQ(static_cast<int>(c.width), 80);
    CHECK_EQ(c.flag88, 1);
    CHECK_EQ(c.flag92, 0);

    // Right: x = anchor - width ; flag92 = 1.
    gui::HudLabelLayout r = b.layoutCaption(gui::HudLabelAlign::kRight, 320, 80);
    CHECK_EQ(r.x, 240);
    CHECK_EQ(r.flag88, 0);
    CHECK_EQ(r.flag92, 1);

    // Left: x = anchor ; flag92 = 0.
    gui::HudLabelLayout l = b.layoutCaption(gui::HudLabelAlign::kLeft, 320, 80);
    CHECK_EQ(l.x, 320);
    CHECK_EQ(l.flag88, 0);
    CHECK_EQ(l.flag92, 0);
}

// ---------------------------------------------------------------------------
// Button-row layout — gui::Hud_ButtonRowLayout even spread for a 3-button row.
// widths {40,60,50}, window 640: maxW=60, cell=640/4=160. maxW<=cell so pitch=160,
// v26=160, x0 = -60/2 + 160 = 130; then 290, 450.
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, ButtonRowLayoutEvenSpread) {
    HudOverlayBinder b;
    b.reset();

    std::vector<int> outX;
    int pitch = b.layoutButtonRow({40, 60, 50}, 640, outX);
    CHECK_EQ(pitch, 160);
    CHECK_EQ(static_cast<int>(outX.size()), 3);
    CHECK_EQ(outX[0], 130);
    CHECK_EQ(outX[1], 290);
    CHECK_EQ(outX[2], 450);

    // Empty row: no buttons, pitch 0.
    std::vector<int> empty;
    CHECK_EQ(b.layoutButtonRow({}, 640, empty), 0);
    CHECK_EQ(static_cast<int>(empty.size()), 0);
}

// ---------------------------------------------------------------------------
// Player-bar slot layout — 78px row pitch + the icon/sprite/label/sub-window offsets.
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, PlayerBarSlotLayoutGolden) {
    HudOverlayBinder b;
    b.reset();

    // Three owned objects -> slots 0,1,2 in order.
    auto slots = b.buildPlayerBar({100, 200, 300});
    CHECK_EQ(static_cast<int>(slots.size()), 3);
    CHECK_EQ(slots[0].slotIndex, 0);
    CHECK_EQ(slots[1].slotIndex, 1);
    CHECK_EQ(slots[2].slotIndex, 2);

    // Slot 1 layout: rowY=78, icon at (6,78), sprite at (8,95), label at (0,82,w95),
    // sub-window at (15,141,80x6).
    const gui::PlayerBarLayout& L = slots[1].layout;
    CHECK_EQ(L.rowY, 78);
    CHECK_EQ(L.iconX, 6);
    CHECK_EQ(L.iconY, 78);
    CHECK_EQ(L.spriteX, 8);
    CHECK_EQ(L.spriteY, 95);
    CHECK_EQ(L.labelX, 0);
    CHECK_EQ(L.labelY, 82);
    CHECK_EQ(L.labelWidth, 95);
    CHECK_EQ(L.subWinX, 15);
    CHECK_EQ(L.subWinY, 141);
    CHECK_EQ(L.subWinW, 80);
    CHECK_EQ(L.subWinH, 6);

    // Re-assigning an existing object id returns the SAME slot (de-dup).
    auto again = b.buildPlayerBar({200});
    CHECK_EQ(static_cast<int>(again.size()), 1);
    CHECK_EQ(again[0].slotIndex, 1);
}

// ---------------------------------------------------------------------------
// Status-text registration — de-dup by key, first-free allocation.
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, StatusTextRegisterDeDup) {
    HudOverlayBinder b;
    b.reset();

    int s0 = b.registerStatus(/*key=*/10, /*tag=*/100);
    int s1 = b.registerStatus(/*key=*/20, /*tag=*/100);
    CHECK_EQ(s0, 0);
    CHECK_EQ(s1, 1);
    // Same key -> same slot, no new allocation.
    CHECK_EQ(b.registerStatus(10, 100), 0);
    CHECK_EQ(b.registerStatus(20, 100), 1);
}

// ---------------------------------------------------------------------------
// Tooltip text-id selection — id-fallback object range [206,1010) -> id-206.
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, TooltipTextIdSelection) {
    CHECK_EQ(HudOverlayBinder::TooltipTextId(206), 0);    // low edge -> object 0
    CHECK_EQ(HudOverlayBinder::TooltipTextId(300), 94);   // mid range
    CHECK_EQ(HudOverlayBinder::TooltipTextId(1009), 803); // high edge (inclusive)
    CHECK_EQ(HudOverlayBinder::TooltipTextId(1010), 0);   // past range -> none
    CHECK_EQ(HudOverlayBinder::TooltipTextId(0), 0);      // below range -> none
}

// ---------------------------------------------------------------------------
// Tooltip classification — a hovered scene object classifies to a real builder by
// its class byte; index 0 classifies to none (objectCode 0 is the "no subject").
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, ClassifyHoverByClassByte) {
    HudOverlayBinder b;
    // Object table: index 1 class 32 (-> Object), index 2 class 11 (-> Upgrade).
    b.reset({32, 32, 11, 32});

    gui::TooltipSubject subj{};
    // Hover scene object index 1: objectCode 1, class byte 32 -> kObject.
    CHECK_EQ(static_cast<int>(b.classifyHover(1, /*tooltipId=*/0, subj)),
             static_cast<int>(gui::TooltipKind::kObject));
    CHECK_EQ(subj.objectCode, 1);

    // Hover scene object index 2: objectCode 2, class byte 11 -> kUpgrade.
    CHECK_EQ(static_cast<int>(b.classifyHover(2, 0, subj)),
             static_cast<int>(gui::TooltipKind::kUpgrade));
    CHECK_EQ(subj.objectCode, 2);

    // Hover scene object index 0: objectCode 0 -> kNone (the "no subject" code).
    CHECK_EQ(static_cast<int>(b.classifyHover(0, 0, subj)),
             static_cast<int>(gui::TooltipKind::kNone));

    // No scene object (index < 0) but a tooltip id in the object range -> object code
    // 207 - 206 = 1, whose class byte (32) is in the table -> Object builder.
    CHECK_EQ(static_cast<int>(b.classifyHover(-1, /*tooltipId=*/207, subj)),
             static_cast<int>(gui::TooltipKind::kObject));
    CHECK_EQ(subj.objectCode, 1); // 207 - 206
}

// ---------------------------------------------------------------------------
// Determinism: two binds from the same inputs produce identical overlays, and the
// bind actually changes state (non-inert: a real subject classified).
// ---------------------------------------------------------------------------
TEST(HudBinderUnit, BindDeterministicAndNonInert) {
    HudOverlayBinder b;

    b.reset({32, 32, 32, 32, 32, 32});
    play::HudOverlay a = b.bind(/*hoverSceneObject=*/3, /*hoverTooltipId=*/0, {11, 22, 33});

    b.reset({32, 32, 32, 32, 32, 32});
    play::HudOverlay c = b.bind(3, 0, {11, 22, 33});

    // Identical across runs (same seed-free, fixed-order inputs).
    CHECK_EQ(a.caption.x, c.caption.x);
    CHECK_EQ(a.buttonRowPitch, c.buttonRowPitch);
    CHECK_EQ(static_cast<int>(a.barSlots.size()), static_cast<int>(c.barSlots.size()));
    CHECK_EQ(a.barSlots.back().slotIndex, c.barSlots.back().slotIndex);
    CHECK_EQ(static_cast<int>(a.hoverKind), static_cast<int>(c.hoverKind));
    CHECK_EQ(a.hoverObjCode, c.hoverObjCode);

    // Non-inert: a real subject was classified (object index 3 -> kObject, code 3),
    // status entries + a damage label were registered, the bar laid out 3 slots.
    CHECK_EQ(static_cast<int>(a.hoverKind), static_cast<int>(gui::TooltipKind::kObject));
    CHECK_EQ(a.hoverObjCode, 3);
    CHECK_EQ(static_cast<int>(a.statusSlots.size()), 3);
    CHECK_EQ(a.statusSlots[0], 0);
    CHECK(a.damageSlot >= 0);
    CHECK_EQ(static_cast<int>(a.barSlots.size()), 3);
}
