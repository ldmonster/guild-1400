// tests/e2e/play_slice_market_e2e_test.cpp — GUARDED real-asset MARKET/TRADE slice
// on AUGSBURG. The full vertical slice:
//
//   load the REAL good/building type catalog (A_Obj.dat / A_Geb.dat -> g_sceneTypes,
//   the real market-price source) + AUGSBURG.cty into the live sim arrays ->
//   buy a REAL ware on a real building -> apply the opcode-17 trade command (treasury
//   + stock + folded price cache) -> advance one REAL game-day -> assert the real
//   treasury/price moved before->after -> rerun byte-identical (deterministic).
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/slice_market.h"
#include "play/world_digest.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/building_production.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/data_load.h"
#include "world/city.h"
#include "world/economy_tick.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("data/A_Obj.dat") && fs.exists("data/A_Geb.dat");
}

// Pick a ware whose real catalog price is positive (a tradeable good).
i16 FirstPriceableWare() {
    for (i16 ware = 1; ware < 200; ++ware)
        if (sim::Building_ComputeMarketPrice(ware, 100) > 0.0)
            return ware;
    return 0;
}

// Load the real type catalog + AUGSBURG into the live arrays, blanking the folded
// economy tables first (the determinism rule). Returns the first live building id,
// or 0 on failure. The VFS is left bound (caller shuts it down).
i32 MountAndLoad(shim::IFileSystem& fs) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!a.vfsBound)
        return 0;
    // Blank the folded world tables for reproducibility.
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);
    // Load the REAL good/building type catalog (the market-price source).
    if (world::WorldLoadBuildingAndObjectData(&fs, "data/") != 0)
        return 0;
    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    io::WorldState w{};
    if (!io::LoadWorld(app::RealCityPath("Augsburg").c_str(), w))
        return 0;
    // First live building.
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive)
            return sim::g_objects[i].id;
    return 0;
}

} // namespace

// The full slice on AUGSBURG: a real buy moves real treasury/stock/price, a real
// game-day reprices, and the run is deterministic on rerun.
TEST(PlaySliceMarketE2E, AugsburgBuyTreasuryPriceMoveDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlaySliceMarketE2E: real assets absent (%s)\n",
                    GameDir().c_str());
        return;
    }

    SetMarketApplyHooks(nullptr);

    auto runOnce = [](MarketSliceResult& out, i16& wareOut, i32& idOut) {
        shim::DiskFileSystem fs(GameDir());
        i32 id = MountAndLoad(fs);
        if (id == 0) { io::VfsShutdown(); return false; }
        i16 ware = FirstPriceableWare();
        if (ware == 0) { io::VfsShutdown(); return false; }

        // Seed a known starting stock on the traded building so a buy has headroom
        // and the stock move is observable (the real record's fill column).
        if (sim::ObjectRec* o = sim::BuildingFindById(id)) {
            reinterpret_cast<sim::BuildingRec*>(o)->fillLevel = 1000;
            i32 zero = 0;
            std::memcpy(reinterpret_cast<u8*>(o) + kTreasuryFieldOff, &zero, 4);
        }

        MarketInteraction mi;
        mi.side = MarketSide::kBuy; mi.ware = ware; mi.qty = 25;
        mi.buildingId = id; mi.buildingKind = 2; mi.player = 0;
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunMarketSlice(mi, /*econSeed=*/0xA065B, q);
        wareOut = ware; idOut = id;
        io::VfsShutdown();
        return true;
    };

    MarketSliceResult a;
    i16 ware = 0; i32 id = 0;
    if (!runOnce(a, ware, id)) {
        std::printf("  [skip] PlaySliceMarketE2E: load failed\n");
        return;
    }

    std::printf("  AUGSBURG market slice: building id=%d, ware=%d\n", id, (int)ware);
    std::printf("    unit price = %d, qty = %d, total = %lld\n",
                a.command.unitPrice, a.command.qty, (long long)a.command.totalValue);
    std::printf("    treasury %lld -> %lld\n",
                (long long)a.treasuryBefore, (long long)a.treasuryAfter);
    std::printf("    stock %d -> %d\n", a.stockBefore, a.stockAfter);
    std::printf("    price (before day) %d -> (after day) %d\n",
                a.priceBefore, a.priceAfter);
    std::printf("    economy passes = %d\n", a.economyPasses);

    // --- the command issued + applied through the REAL codec ---
    CHECK(a.command.issued);
    CHECK_EQ((int)a.command.opcode, 17);     // VIBE_Command_QueueRequest17
    CHECK(a.enqueued);
    CHECK(a.applied);

    // --- the REAL price oracle produced a positive unit price ---
    CHECK(a.command.unitPrice > 0);

    // --- the buy moved the real folded records ---
    CHECK_EQ(a.stockAfter - a.stockBefore, 25);             // stock up by qty
    CHECK_EQ(a.treasuryBefore - a.treasuryAfter, a.command.totalValue); // money out
    CHECK(a.treasuryMoved());
    CHECK(a.stockMoved());

    // --- the trade + the game-day each moved the whole-world hash ---
    CHECK(a.tradeChangedWorld());
    CHECK(a.dayChangedWorld());
    // --- the day repriced the ware (the folded price cache evolved) ---
    CHECK(a.priceAfter != a.priceBefore);
    CHECK(a.economyPasses > 0);

    // --- deterministic on rerun (byte-identical world hashes) ---
    MarketSliceResult b;
    i16 ware2 = 0; i32 id2 = 0;
    CHECK(runOnce(b, ware2, id2));
    CHECK_EQ(ware2, ware);
    CHECK_EQ(id2, id);
    CHECK_EQ(a.hashBefore, b.hashBefore);
    CHECK_EQ(a.hashAfterCommand, b.hashAfterCommand);
    CHECK_EQ(a.hashAfterDay, b.hashAfterDay);
    CHECK_EQ(a.treasuryAfter, b.treasuryAfter);
    CHECK_EQ(a.priceAfter, b.priceAfter);
}
