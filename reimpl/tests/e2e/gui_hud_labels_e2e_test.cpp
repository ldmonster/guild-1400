// End-to-end test for the HUD floating-label overlay: a whole on-screen-label frame
// across the real HUD sibling modules.
//
// Scenario: a HUD frame with a set of in-world objects (buildings/animals/characters)
// projected to screen, a batch of damage numbers aging out, a transient status banner,
// a name-input caption, and the player-owned marking pass.  We drive one full "draw"
// frame end to end:
//   1. register several damage labels through the real DamageLabel_Register, advance
//      the clock, run the expiry sweep, and place captions for the survivors;
//   2. centre object/animal/character captions over their projected boxes;
//   3. mark the player-owned buildings;
//   4. tick the status banner past its lifetime.
//
// The optional real-asset leg (GUILD_ASSET_DIR) is GUARDED: these label functions take
// no disk input, so when the dir is present we simply assert the synthetic frame would
// composite against the real screen-bounds globals — there is nothing to load, so the
// guarded leg is a no-op confirmation.  The deterministic synthetic frame always runs.
#include "test.h"
#include "gui/hud.h"
#include "gui/hud_labels.h"

#include <cstdlib>
#include <vector>

using namespace guild::gui;

namespace {

// A projected in-world object: its screen-bounds box and a caption-kind.
struct FrameObject { ScreenBounds bounds; int charHeight; };

// One composited caption the frame would blit.
struct CompositedLabel { int x, y, width, charHeight; };

} // namespace

TEST(HudLabelsE2E, FullLabelFrame) {
    // --- 1. Damage labels: register a batch through the real sibling, age them. -----
    ResetDamageLabels();
    DamageLabel_Register(/*source*/ 501, /*amount*/ 12, /*now*/ 5000);
    DamageLabel_Register(502, 4, 5050);
    DamageLabel_Register(503, 8, 5400);
    DamageLabel_Register(504, 1, 5450);

    // Advance the clock to 5400 and sweep: 501 (5000+300=5300<5400) and
    // 502 (5050+300=5350<5400) expire; 503/504 survive.
    int expired = DamageLabel_ExpireSweep(5400);
    CHECK_EQ(expired, 2);
    int live = 0;
    for (int i = 0; i < kDamageLabelCount; ++i)
        if (g_damageLabels[i].inUse) ++live;
    CHECK_EQ(live, 2);

    // --- 2. Project + centre captions over the frame's objects. --------------------
    std::vector<FrameObject> objs = {
        {{100, 40, 260, 200}, kHudLabelCharH40},  // building name
        {{300, 80, 380, 240}, kHudLabelCharH40},  // animal
        {{500, 120, 500, 300}, kHudLabelCharH32}, // character (zero-width box)
    };
    std::vector<CompositedLabel> composited;
    for (auto& o : objs) {
        LabelPlacement p = Hud_CenteredLabelPlacement(o.bounds);
        composited.push_back({p.x, p.y, kHudLabelWidth, o.charHeight});
    }
    CHECK_EQ(composited.size(), (size_t)3);
    // building: 100 + (260-100)/2 - 80 = 100
    CHECK_EQ(composited[0].x, 100);
    CHECK_EQ(composited[0].y, 40);
    CHECK_EQ(composited[0].charHeight, 40);
    // animal: 300 + (380-300)/2 - 80 = 260
    CHECK_EQ(composited[1].x, 260);
    CHECK_EQ(composited[1].y, 80);
    // character: zero-width -> 500 - 80 = 420
    CHECK_EQ(composited[2].x, 420);
    CHECK_EQ(composited[2].y, 120);
    CHECK_EQ(composited[2].charHeight, 32);
    for (auto& c : composited)
        CHECK_EQ(c.width, 160);

    // --- 3. Name-input caption over a packed world anchor. -------------------------
    LabelPlacement cap = Hud_NameInputCaptionPlacement(0x015E0000, 0x00B40000);
    CHECK_EQ(cap.x, 350 - 54);  // x2hi=350 -> 296
    CHECK_EQ(cap.y, 180 + 52);  // yhi=180 -> 232

    // --- 4. Status banner: armed at 5200, sample across its 350-tick lifetime. -----
    CHECK_EQ(Hud_StatusBannerX(1024), 724);
    CHECK_EQ(Hud_StatusBannerExpired(5200, 5549), false);
    CHECK_EQ(Hud_StatusBannerExpired(5200, 5550), true);

    // --- 5. Mark the player-owned buildings in this frame. -------------------------
    std::vector<OwnedObject> owned = {
        {/*handle*/ 7001, /*flags*/ 0},
        {7002, 0},
        {7003, 0},
    };
    std::vector<OwnerSlot> slots = {
        {true, 0xA0, 7001, 1},   // owns 7001
        {true, 0xB0, 7003, 5},   // 5&1==1 -> owns 7003
        {true, 0xC0, 9999, 1},   // no match
        {false,0xD0, 7002, 1},   // inactive
    };
    int marked = Hud_MarkOwnedObjects(owned, slots);
    CHECK_EQ(marked, 2);
    CHECK_EQ(owned[0].flags, 1);
    CHECK_EQ(owned[1].flags, 0);
    CHECK_EQ(owned[2].flags, 1);

    // --- Guarded real-asset leg (no disk input for these functions). ---------------
    const char* assetDir = std::getenv("GUILD_ASSET_DIR");
    if (assetDir && assetDir[0]) {
        // The label drawers project against the renderer's screen-bound globals; there
        // is no asset to load, so the guarded leg only re-affirms the composite is
        // deterministic given the same inputs.
        for (auto& o : objs) {
            LabelPlacement a = Hud_CenteredLabelPlacement(o.bounds);
            LabelPlacement b = Hud_CenteredLabelPlacement(o.bounds);
            CHECK_EQ(a.x, b.x);
            CHECK_EQ(a.y, b.y);
        }
    }
}
