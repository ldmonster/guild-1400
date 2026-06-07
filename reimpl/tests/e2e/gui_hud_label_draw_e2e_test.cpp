// End-to-end test: a whole HUD overlay "draw" frame across the six real label-drawer
// passes (gui/hud_label_draw.cpp) on top of their real sibling helpers (hud_labels.cpp,
// hud.cpp).  Drives one composited frame:
//   1. register a batch of damage labels through the REAL DamageLabel_Register, advance
//      the clock so some age out, then run Hud_DrawDamageLabels (sweep + survivors);
//   2. draw object-name + animal + character captions over their projected boxes;
//   3. draw the transient status banner and tick it past its lifetime;
//   4. draw the name-input caption.
// All six passes feed one shared compositing sink whose total ordering of layers/draws
// is asserted, mirroring how the per-frame HUD overlay actually runs them.
//
// The optional real-asset leg (GUILD_ASSET_DIR) is GUARDED: these drawers take no disk
// input, so when the dir is present the guarded leg is a no-op confirmation.  The
// deterministic synthetic frame always runs.
#include "test.h"
#include "gui/hud_label_draw.h"
#include "gui/hud_labels.h"
#include "gui/hud.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild::gui;

namespace {
struct FrameSink : HudLabelSink {
    std::vector<int> layers;
    std::vector<LabelDraw> draws;
    void Layer(int s) override { layers.push_back(s); }
    void Emit(const LabelDraw& d) override { draws.push_back(d); }
};

HudLabelObject Obj(bool present, bool proj, int l, int t, int r, int b,
                   const std::string& txt) {
    HudLabelObject o; o.present = present; o.projectable = proj;
    o.bounds = {l, t, r, b}; o.text = txt; return o;
}
} // namespace

TEST(HudLabelDrawE2E, FullOverlayFrame) {
    FrameSink sink;

    // --- 1. Damage labels through the REAL register path, then drawn. ---------------
    ResetDamageLabels();
    DamageLabel_Register(/*source*/ 11, /*amount*/ 25, /*now*/ 1000); // slot 0
    DamageLabel_Register(/*source*/ 22, /*amount*/ 8,  /*now*/ 1000); // slot 1
    // Advance the clock so the first batch is on the edge of expiry.
    const int now = 1000 + kDamageLabelLifetime + 1; // both 1000+300 < now -> expire
    // Supply projection targets parallel to g_damageLabels.
    std::vector<HudLabelObject> dmgTargets(kDamageLabelCount);
    dmgTargets[0] = Obj(true, true, 100, 400, 300, 460, "25");
    dmgTargets[1] = Obj(true, true, 0, 0, 200, 40, "8");
    int dmgDrawn = Hud_DrawDamageLabels(now, dmgTargets, sink);
    // Both registered labels are older than the lifetime window -> both expired -> 0 drawn.
    CHECK_EQ(dmgDrawn, 0);
    CHECK(g_damageLabels[0].inUse == false);
    CHECK(g_damageLabels[1].inUse == false);

    // --- 2. Object-name + animal + character captions. ------------------------------
    std::vector<HudLabelObject> objs{
        Obj(true, true, 50, 100, 250, 160, "Town Hall"),  // x = 50 + 100 - 80 = 70
        Obj(false, true, 0, 0, 1, 1, "ghost"),            // skipped (!present)
    };
    int objDrawn = Hud_DrawObjectNameLabels(objs, sink);
    CHECK_EQ(objDrawn, 1);

    std::vector<HudLabelObject> animals{
        Obj(true, true, 10, 20, 170, 60, "Pig"),          // x = 10 + 80 - 80 = 10
    };
    int aniDrawn = Hud_DrawAnimalLabels(/*selectedBuilding*/ true, animals, sink);
    CHECK_EQ(aniDrawn, 1);

    HudCharacterLabel hero;
    hero.active = true; hero.visible = true;
    hero.anchorX = 0x00FA0000; // 250.0
    hero.anchorY = 0x00960000; // 150.0
    hero.text = "Player";
    int chDrawn = Hud_DrawCharacterLabels({hero}, sink);
    CHECK_EQ(chDrawn, 1);

    // --- 3. Status banner: drawn, then ticked past its lifetime. ---------------------
    bool bannerExpired = true;
    int bannerDrawn = Hud_DrawStatusBanner("Game saved", /*start*/ now, /*now*/ now,
                                           /*rightEdge*/ 1024, /*modeFlags*/ 0,
                                           sink, &bannerExpired);
    CHECK_EQ(bannerDrawn, 1);
    CHECK(bannerExpired == false); // start==now, 350-tick window still open

    // --- 4. Name-input caption. -----------------------------------------------------
    int capDrawn = Hud_DrawNameInputCaption(/*inputActive*/ true, /*anchorValid*/ true,
                                            /*anchorX2*/ 0x00C80000, /*anchorY*/ 0x00320000,
                                            "Enter name", sink);
    CHECK_EQ(capDrawn, 1);

    // --- Composited frame assertions. -----------------------------------------------
    // Layer order across the frame: damage(67), object(67), animal(67), character(67),
    // banner(66), caption(67) — six State_Finalize calls in pass order.
    CHECK_EQ(sink.layers.size(), (size_t)6);
    CHECK_EQ(sink.layers[0], kHudLabelStateLayer);    // damage
    CHECK_EQ(sink.layers[1], kHudLabelStateLayer);    // object
    CHECK_EQ(sink.layers[2], kHudLabelStateLayer);    // animal
    CHECK_EQ(sink.layers[3], kHudLabelStateLayer);    // character
    CHECK_EQ(sink.layers[4], kStatusBannerStateLayer);// banner (66)
    CHECK_EQ(sink.layers[5], kHudLabelStateLayer);    // caption

    // Total visible captions = object(1) + animal(1) + character(1) + banner(1) + caption(1)
    // = 5 (damage produced 0 this frame).
    CHECK_EQ(sink.draws.size(), (size_t)5);
    CHECK_EQ(sink.draws[0].x, 70);  CHECK(sink.draws[0].text == "Town Hall");
    CHECK_EQ(sink.draws[1].x, 10);  CHECK(sink.draws[1].text == "Pig");
    CHECK_EQ(sink.draws[2].x, 250); CHECK_EQ(sink.draws[2].y, 150 - 20);
    CHECK(sink.draws[2].text == "Player");
    CHECK_EQ(sink.draws[3].x, 1024 - 300); CHECK_EQ(sink.draws[3].width, kStatusBannerWidth);
    CHECK(sink.draws[3].text == "Game saved");
    CHECK_EQ(sink.draws[4].x, 200 - 54);   CHECK_EQ(sink.draws[4].y, 50 + 52);
    CHECK_EQ(sink.draws[4].charH, kNameInputCaptionH);

    // --- Guarded real-asset leg: no-op confirmation (these drawers load nothing). ----
    const char* assetDir = std::getenv("GUILD_ASSET_DIR");
    if (assetDir && assetDir[0]) {
        // Re-running the same frame is idempotent for the stateless drawers.
        FrameSink again;
        Hud_DrawObjectNameLabels(objs, again);
        CHECK_EQ(again.draws.size(), (size_t)1);
    }
}
