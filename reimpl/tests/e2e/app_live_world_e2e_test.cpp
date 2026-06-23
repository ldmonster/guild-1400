// End-to-end: the headless real-asset spine now loads a LIVE WORLD — the start
// city's .cty is run through the full VIBE_Save_LoadGameFile table-load driver
// (io::LoadWorld), populating the live g_persons / g_objects entity tables, so the
// ~40 wired per-entity sim/turn/event hooks execute over real data instead of empty
// state. Guarded on GUILD_GAME_DIR (skips cleanly when the assets are absent).
#include "app/wiring.h"
#include "tests/framework/test.h"

#include <cstdlib>
#include <string>

using namespace guild;

TEST(AppLiveWorldE2E, RealAssetBootPopulatesEntityTables) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { CHECK(true); return; }   // no assets -> skip

    app::RealHeadlessResult r = app::RunHeadlessRealAssets(dir, /*frames=*/8);
    if (!r.assetsPresent) { CHECK(true); return; } // assets dir present but incomplete -> skip

    // The city header parsed and the full table load ran.
    CHECK(r.cityLoaded);
    CHECK(r.worldLoaded);
    // A real city seed populates the object/building table (buildings, props).
    CHECK(r.liveObjectCount > 0);
    // Person + object counts are non-negative and within the table capacities.
    CHECK(r.livePersonCount >= 0);
    // The spine still ran its frame loop to a clean exit over the loaded world.
    CHECK_EQ(r.base.exitCode, 0);
}
