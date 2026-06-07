// Unit (golden-vector) tests for the HUD label drawer passes — gilde.exe
// VIBE_Hud_DrawObjectNameLabels / DrawAnimalLabels / DrawCharacterLabels /
// DrawDamageLabels / DrawStatusBanner / DrawNameInputCaption (0x4bbaec / 0x4bbbbc /
// 0x4bbc8c / 0x4bb7a0 / 0x4bcb74 / 0x4bcafc).  Deterministic table-walk + placement.
#include "test.h"
#include "gui/hud_label_draw.h"
#include "gui/hud.h"

using namespace guild::gui;

namespace {
// A recording sink that captures every Layer()/Emit() in order.
struct RecSink : HudLabelSink {
    std::vector<int> layers;
    std::vector<LabelDraw> draws;
    void Layer(int s) override { layers.push_back(s); }
    void Emit(const LabelDraw& d) override { draws.push_back(d); }
};

HudLabelObject MakeObj(bool present, bool proj, int l, int t, int r, int b,
                       const char* txt) {
    HudLabelObject o;
    o.present = present;
    o.projectable = proj;
    o.bounds = {l, t, r, b};
    o.text = txt;
    return o;
}
} // namespace

// ---- Centred placement golden vector (reused helper, verified end-to-end) ----
// box {left=100, top=50, right=300, bottom=120} -> x = 100 + (300-100)/2 - 80 = 120, y=50.
TEST(HudLabelDraw, ObjectNamePlacementGolden) {
    RecSink s;
    std::vector<HudLabelObject> objs{
        MakeObj(true, true, 100, 50, 300, 120, "Mill"),
    };
    int n = Hud_DrawObjectNameLabels(objs, s);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.layers.size(), (size_t)1);
    CHECK_EQ(s.layers[0], kHudLabelStateLayer); // 67
    CHECK_EQ(s.draws.size(), (size_t)1);
    CHECK_EQ(s.draws[0].x, 120);
    CHECK_EQ(s.draws[0].y, 50);
    CHECK_EQ(s.draws[0].width, kHudLabelWidth); // 160
    CHECK_EQ(s.draws[0].charH, kHudLabelCharH40); // 40
    CHECK(s.draws[0].text == "Mill");
}

// Negative-centred placement: a box that straddles x=0 so left + (right-left)/2 - 80 < 0.
// box {left=-40, top=10, right=40, bottom=60} -> x = -40 + 80/2 - 80 = -40 + 40 - 80 = -80.
TEST(HudLabelDraw, ObjectNameNegativeX) {
    RecSink s;
    std::vector<HudLabelObject> objs{ MakeObj(true, true, -40, 10, 40, 60, "X") };
    Hud_DrawObjectNameLabels(objs, s);
    CHECK_EQ(s.draws.size(), (size_t)1);
    CHECK_EQ(s.draws[0].x, -80);
    CHECK_EQ(s.draws[0].y, 10);
}

// Gating: not-present and off-screen entries are skipped, but State_Finalize(67) still
// runs (the original emits it up-front, before the loop).
TEST(HudLabelDraw, ObjectNameGates) {
    RecSink s;
    std::vector<HudLabelObject> objs{
        MakeObj(false, true, 0, 0, 10, 10, "absent"),   // !present -> skip
        MakeObj(true, false, 0, 0, 10, 10, "offscreen"),// !projectable -> skip
        MakeObj(true, true, 0, 0, 200, 40, "shown"),    // -> drawn (x = 0 + 100 - 80 = 20)
    };
    int n = Hud_DrawObjectNameLabels(objs, s);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.layers.size(), (size_t)1); // layer emitted even with skips
    CHECK_EQ(s.draws.size(), (size_t)1);
    CHECK_EQ(s.draws[0].x, 20);
    CHECK(s.draws[0].text == "shown");
}

// ---- Animal labels: gated on a selected building ----
TEST(HudLabelDraw, AnimalLabelsGate) {
    RecSink off;
    std::vector<HudLabelObject> animals{ MakeObj(true, true, 0, 0, 160, 40, "Cow") };
    // No building selected -> whole pass skipped, including State_Finalize.
    CHECK_EQ(Hud_DrawAnimalLabels(false, animals, off), 0);
    CHECK_EQ(off.layers.size(), (size_t)0);
    CHECK_EQ(off.draws.size(), (size_t)0);

    RecSink on;
    int n = Hud_DrawAnimalLabels(true, animals, on);
    CHECK_EQ(n, 1);
    CHECK_EQ(on.layers.size(), (size_t)1);
    CHECK_EQ(on.draws[0].x, 0 + (160 - 0) / 2 - 80); // = 0
    CHECK_EQ(on.draws[0].charH, kHudLabelCharH40);
    CHECK(on.draws[0].text == "Cow");
}

// ---- Character labels: anchor >>16 + the -20 top inset, charH 32 ----
TEST(HudLabelDraw, CharacterLabelAnchor) {
    RecSink s;
    // anchorX = 0x012C0000 (300.0 in 16.16), anchorY = 0x00640000 (100.0).
    HudCharacterLabel c;
    c.active = true; c.visible = true;
    c.anchorX = 0x012C0000;
    c.anchorY = 0x00640000;
    c.text = "Hero";
    int n = Hud_DrawCharacterLabels({c}, s);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.draws[0].x, 300);       // 0x012C0000 >> 16
    CHECK_EQ(s.draws[0].y, 100 - 20);  // (0x00640000 >> 16) - 20
    CHECK_EQ(s.draws[0].charH, kHudLabelCharH32); // 32
    CHECK_EQ(s.draws[0].width, kHudLabelWidth);
}

TEST(HudLabelDraw, CharacterLabelGates) {
    RecSink s;
    HudCharacterLabel inactive; inactive.active = false; inactive.visible = true;
    HudCharacterLabel occluded; occluded.active = true; occluded.visible = false;
    int n = Hud_DrawCharacterLabels({inactive, occluded}, s);
    CHECK_EQ(n, 0);
    CHECK_EQ(s.layers.size(), (size_t)1); // layer still emitted
    CHECK_EQ(s.draws.size(), (size_t)0);
}

// ---- Damage labels: expiry sweep (reused) then centred draw at top-64 ----
TEST(HudLabelDraw, DamageLabelExpiryAndDraw) {
    ResetDamageLabels();
    // Slot 0: live, recent.  Slot 1: live but stale (expires this frame).
    g_damageLabels[0].inUse = true;  g_damageLabels[0].timestamp = 1000;
    g_damageLabels[1].inUse = true;  g_damageLabels[1].timestamp = 100;
    const int now = 500; // slot1: 100 + 300 = 400 < 500 -> expire; slot0: 1300 !< 500.

    std::vector<HudLabelObject> targets(kDamageLabelCount);
    targets[0] = MakeObj(true, true, 50, 200, 250, 260, "12"); // x = 50 + 100 - 80 = 70
    targets[1] = MakeObj(true, true, 0, 0, 100, 40, "99");

    RecSink s;
    int n = Hud_DrawDamageLabels(now, targets, s);
    CHECK(g_damageLabels[1].inUse == false); // expired by the sweep
    CHECK(g_damageLabels[0].inUse == true);
    CHECK_EQ(n, 1);                          // only slot0 drawn
    CHECK_EQ(s.layers.size(), (size_t)1);
    CHECK_EQ(s.draws.size(), (size_t)1);
    CHECK_EQ(s.draws[0].x, 70);
    CHECK_EQ(s.draws[0].y, 200 - kDamageLabelTopInset); // top - 64 = 136
    CHECK_EQ(s.draws[0].charH, kHudLabelCharH40);
    CHECK(s.draws[0].text == "12");
}

// ---- Status banner: placement x, width 600, lifetime ----
TEST(HudLabelDraw, StatusBannerDrawAndExpire) {
    RecSink s;
    bool expired = true;
    // rightEdge = 1024 -> x = 1024 - 300 = 724.  start=10, now=200 -> 10+350=360 > 200 ->
    // not expired.
    int n = Hud_DrawStatusBanner("Saved", /*startTick*/10, /*now*/200,
                                 /*rightEdge*/1024, /*modeFlags*/0, s, &expired);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.layers.size(), (size_t)1);
    CHECK_EQ(s.layers[0], kStatusBannerStateLayer); // 66
    CHECK_EQ(s.draws[0].x, 724);
    CHECK_EQ(s.draws[0].width, kStatusBannerWidth); // 600
    CHECK_EQ(s.draws[0].charH, kStatusBannerCharH); // 40
    CHECK(s.draws[0].text == "Saved");
    CHECK(expired == false);

    // Now past the 350-tick window: 10 + 350 = 360 <= 360 -> expired.
    bool exp2 = false;
    RecSink s2;
    Hud_DrawStatusBanner("Saved", 10, 360, 1024, 0, s2, &exp2);
    CHECK(exp2 == true);

    // Empty banner -> nothing drawn, no layer.
    RecSink s3;
    bool exp3 = true;
    int n3 = Hud_DrawStatusBanner("", 0, 0, 1024, 0, s3, &exp3);
    CHECK_EQ(n3, 0);
    CHECK_EQ(s3.layers.size(), (size_t)0);
    CHECK(exp3 == false);
}

// The mode-flag 0x8000 scale selector must not change the integer placement (only the
// renderer-side scale); both flag states place the banner at the same x.
TEST(HudLabelDraw, StatusBannerScaleFlagInvariantX) {
    RecSink wide, narrow;
    bool e1, e2;
    Hud_DrawStatusBanner("B", 0, 0, 800, 0x0000, wide, &e1);   // wide scale
    Hud_DrawStatusBanner("B", 0, 0, 800, 0x8000, narrow, &e2); // narrow scale
    CHECK_EQ(wide.draws[0].x, narrow.draws[0].x);
    CHECK_EQ(wide.draws[0].x, 500); // 800 - 300
}

// ---- Name-input caption: two-flag gate + anchor placement, charH 168 ----
TEST(HudLabelDraw, NameInputCaptionPlacement) {
    RecSink s;
    // anchorX2 = 0x00C80000 (200.0), anchorY = 0x00320000 (50.0).
    int n = Hud_DrawNameInputCaption(true, true, 0x00C80000, 0x00320000, "Name?", s);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.layers[0], kHudLabelStateLayer);
    CHECK_EQ(s.draws[0].x, 200 - 54); // (anchorX2>>16) - 54 = 146
    CHECK_EQ(s.draws[0].y, 50 + 52);  // (anchorY>>16) + 52 = 102
    CHECK_EQ(s.draws[0].charH, kNameInputCaptionH); // 168
}

TEST(HudLabelDraw, NameInputCaptionGates) {
    RecSink a, b;
    CHECK_EQ(Hud_DrawNameInputCaption(false, true, 0, 0, "x", a), 0); // input inactive
    CHECK_EQ(a.layers.size(), (size_t)0);
    CHECK_EQ(Hud_DrawNameInputCaption(true, false, 0, 0, "x", b), 0); // anchor invalid
    CHECK_EQ(b.layers.size(), (size_t)0);
}
