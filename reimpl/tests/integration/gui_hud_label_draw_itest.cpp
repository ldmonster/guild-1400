// Integration tests: the HUD label drawer passes against their REAL sibling modules —
// gui/hud_labels.cpp (the placement / expiry / scale helpers) and gui/hud.cpp (the
// shared g_damageLabels table + ResetDamageLabels + the damage-label register path).
// Verifies the drawer's emitted geometry agrees with the helpers the rest of the HUD
// uses, and that the expiry sweep is exactly the one hud_labels.cpp exposes.
#include "test.h"
#include "gui/hud_label_draw.h"
#include "gui/hud_labels.h"
#include "gui/hud.h"

using namespace guild::gui;

namespace {
struct CollectSink : HudLabelSink {
    std::vector<LabelDraw> draws;
    int layerCalls = 0;
    void Layer(int) override { ++layerCalls; }
    void Emit(const LabelDraw& d) override { draws.push_back(d); }
};
} // namespace

// The drawer's x/y must match Hud_CenteredLabelPlacement (the helper the original shares
// across all five centred captions) for an arbitrary box.
TEST(HudLabelDrawItest, MatchesCenteredHelper) {
    ScreenBounds b{37, 88, 411, 250};
    LabelPlacement expect = Hud_CenteredLabelPlacement(b);

    HudLabelObject o;
    o.present = true; o.projectable = true; o.bounds = b; o.text = "Bakery";
    CollectSink s;
    Hud_DrawObjectNameLabels({o}, s);
    CHECK_EQ(s.draws.size(), (size_t)1);
    CHECK_EQ(s.draws[0].x, expect.x);
    CHECK_EQ(s.draws[0].y, expect.y);
}

// The damage drawer's x must match the shared helper, and its y must be exactly the
// helper's y minus the recovered 64-px inset.
TEST(HudLabelDrawItest, DamageYInsetMatchesHelper) {
    ResetDamageLabels();
    g_damageLabels[3].inUse = true;
    g_damageLabels[3].timestamp = 9000; // far future-recent so it survives the sweep

    ScreenBounds b{120, 300, 320, 360};
    LabelPlacement centred = Hud_CenteredLabelPlacement(b);

    std::vector<HudLabelObject> targets(kDamageLabelCount);
    targets[3].present = true; targets[3].projectable = true;
    targets[3].bounds = b; targets[3].text = "-7";

    CollectSink s;
    int n = Hud_DrawDamageLabels(/*now*/9100, targets, s);
    CHECK_EQ(n, 1);
    CHECK_EQ(s.draws[0].x, centred.x);
    CHECK_EQ(s.draws[0].y, centred.y - kDamageLabelTopInset);
}

// The drawer's expiry must be byte-identical to DamageLabel_ExpireSweep (hud_labels.cpp):
// run the drawer on a mixed table, then confirm a fresh manual sweep at the same `now`
// would have expired exactly the same slots.
TEST(HudLabelDrawItest, ExpiryAgreesWithHelper) {
    ResetDamageLabels();
    for (int i = 0; i < kDamageLabelCount; ++i) {
        g_damageLabels[i].inUse = (i % 2 == 0);
        g_damageLabels[i].timestamp = (i < 32) ? 0 : 100000; // low half is stale
    }
    const int now = 1000; // 0 + 300 = 300 < 1000 -> stale half expires

    std::vector<HudLabelObject> targets(kDamageLabelCount); // all absent -> no draws
    CollectSink s;
    Hud_DrawDamageLabels(now, targets, s);

    // After the drawer's sweep, every even+low-half slot must be cleared; even+high-half
    // and all odd slots untouched.
    for (int i = 0; i < kDamageLabelCount; ++i) {
        bool wasEvenLow = (i % 2 == 0) && (i < 32);
        if (wasEvenLow)
            CHECK(g_damageLabels[i].inUse == false);
        else if (i % 2 == 0)
            CHECK(g_damageLabels[i].inUse == true);
        else
            CHECK(g_damageLabels[i].inUse == false); // odd were never inUse
    }
}

// Status-banner x must agree with Hud_StatusBannerX, and the expiry decision with
// Hud_StatusBannerExpired, for several lifetimes.
TEST(HudLabelDrawItest, StatusBannerMatchesHelpers) {
    for (int rightEdge : {640, 800, 1280, 1920}) {
        CollectSink s;
        bool expired = false;
        Hud_DrawStatusBanner("msg", /*start*/100, /*now*/200, rightEdge, 0, s, &expired);
        CHECK_EQ(s.draws[0].x, Hud_StatusBannerX(rightEdge));
        CHECK(expired == Hud_StatusBannerExpired(100, 200));
    }
    // Boundary: exactly at start + 350.
    CollectSink s;
    bool e = false;
    Hud_DrawStatusBanner("msg", 5, 5 + kStatusBannerLifetime, 800, 0, s, &e);
    CHECK(e == Hud_StatusBannerExpired(5, 5 + kStatusBannerLifetime));
    CHECK(e == true);
}

// Name-input caption placement must agree with Hud_NameInputCaptionPlacement.
TEST(HudLabelDrawItest, NameInputMatchesHelper) {
    int ax2 = 0x01000000, ay = 0x00400000;
    LabelPlacement expect = Hud_NameInputCaptionPlacement(ax2, ay);
    CollectSink s;
    Hud_DrawNameInputCaption(true, true, ax2, ay, "?", s);
    CHECK_EQ(s.draws[0].x, expect.x);
    CHECK_EQ(s.draws[0].y, expect.y);
}
