// tests/e2e/dialog_market_e2e_test.cpp — GUARDED Wave 28 PLAY: the REAL market
// dialog on REAL assets. Loads the REAL shipped trade form
// (Resources/forms.BIN -> Handel/Handel_Produktion.form) through the REAL parser,
// loads the REAL good/building catalog + AUGSBURG.cty into the live sim, lays out
// + renders the dialog (real widget rects, non-clear pixels), then clicks the BUY
// button -> the REAL opcode-17 command -> observable world effect (treasury/stock),
// deterministic on rerun.
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/archive_mount.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/dialog_market.h"
#include "play/slice_market.h"
#include "play/world_digest.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/building_production.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"
#include "world/data_load.h"
#include "world/economy_tick.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

const char* kTradeFormMember = "Handel/Handel_Produktion.form";

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("data/A_Obj.dat") && fs.exists("data/A_Geb.dat");
}

i16 FirstPriceableWare() {
    for (i16 ware = 1; ware < 200; ++ware)
        if (sim::Building_ComputeMarketPrice(ware, 100) > 0.0)
            return ware;
    return 0;
}

// Read the real trade form bytes from forms.BIN (via the mounted archive).
bool ReadTradeForm(shim::IFileSystem& fs, std::vector<u8>& out) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI",
                                 {"Resources/forms.BIN"}, /*caseInsensitive=*/true);
    bool ok = false;
    if (a.vfsBound) {
        io::ArchiveMount* m = a.archiveForMember(kTradeFormMember);
        if (m) ok = m->OpenMember(kTradeFormMember, out) && !out.empty();
    }
    io::VfsShutdown();
    return ok;
}

// Mount + load the real catalog + AUGSBURG into the live arrays (the determinism
// rig from play_slice_market_e2e). Returns the first live building id, or 0.
i32 MountAndLoad(shim::IFileSystem& fs) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!a.vfsBound) return 0;
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);
    if (world::WorldLoadBuildingAndObjectData(&fs, "data/") != 0) return 0;
    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    io::WorldState w{};
    if (!io::LoadWorld(app::RealCityPath("Augsburg").c_str(), w)) return 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) return sim::g_objects[i].id;
    return 0;
}

} // namespace

TEST(DialogMarketE2E, AugsburgRealFormBuyClickWorldEffectDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] DialogMarketE2E: real assets absent (%s)\n",
                    GameDir().c_str());
        return;
    }

    // --- Read the REAL trade form bytes once + report its structure. ---
    std::vector<u8> formBytes;
    {
        shim::DiskFileSystem fs(GameDir());
        if (!ReadTradeForm(fs, formBytes)) {
            std::printf("  [skip] DialogMarketE2E: could not read %s\n", kTradeFormMember);
            return;
        }
    }
    std::printf("  REAL trade form: %s  (%zu bytes)\n",
                kTradeFormMember, formBytes.size());

    SetMarketApplyHooks(nullptr);

    // The whole flow, factored so we can rerun for determinism.
    auto runOnce = [&](DialogSliceResult& out, i16& wareOut, i32& idOut) -> bool {
        shim::DiskFileSystem fs(GameDir());
        i32 id = MountAndLoad(fs);
        if (id == 0) { io::VfsShutdown(); return false; }
        i16 ware = FirstPriceableWare();
        if (ware == 0) { io::VfsShutdown(); return false; }

        // Give the traded building headroom + a zero treasury so the move is clean.
        if (sim::ObjectRec* o = sim::BuildingFindById(id)) {
            reinterpret_cast<sim::BuildingRec*>(o)->fillLevel = 1000;
            i32 zero = 0;
            std::memcpy(reinterpret_cast<u8*>(o) + kTreasuryFieldOff, &zero, 4);
        }

        // The trade rows the panel would show (the real priceable ware).
        std::vector<TradeRow> rows = { {ware, 1000, 2000, 0, 0, 0} };

        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunMarketDialogSlice(formBytes.data(), formBytes.size(), rows,
                                   /*clickRow*/0, MarketSide::kBuy,
                                   /*buildingId*/id, /*qty*/25, /*player*/0,
                                   /*econSeed*/0xA065B, q, /*fbW*/960, /*fbH*/720,
                                   kTradeFormMember);
        wareOut = ware; idOut = id;
        io::VfsShutdown();
        return true;
    };

    DialogSliceResult a; i16 ware = 0; i32 id = 0;
    if (!runOnce(a, ware, id)) {
        std::printf("  [skip] DialogMarketE2E: load failed\n");
        return;
    }

    std::printf("  parsed form: frm2=%d windows=%d rootWindow=%d panel=(%d,%d %dx%d)\n",
                (int)a.dialog.frm2, a.dialog.windowCount, a.dialog.rootWindow,
                a.dialog.panelX, a.dialog.panelY, a.dialog.panelW, a.dialog.panelH);
    std::printf("  laid-out: rows=%d widgets=%zu buttons=%d labels=%d  rendered nonClear=%d\n",
                (int)a.dialog.rows.size(), a.dialog.widgets.size(),
                a.dialog.buttonCount(), a.dialog.labelCount(), a.render.nonClearPixels);
    int btnId = -1;
    for (const auto& w : a.dialog.widgets)
        if (w.role == WidgetRole::kBuyButton) { btnId = w.widgetId; break; }
    std::printf("  BUY button widget id=%d (gfx 1720)\n", btnId);
    std::printf("  AUGSBURG building id=%d ware=%d  unitPrice=%d qty=%d\n",
                id, (int)ware, a.slice.command.unitPrice, a.slice.command.qty);
    std::printf("    treasury %lld -> %lld   stock %d -> %d   price %d -> %d\n",
                (long long)a.slice.treasuryBefore, (long long)a.slice.treasuryAfter,
                a.slice.stockBefore, a.slice.stockAfter,
                a.slice.priceBefore, a.slice.priceAfter);

    // --- the REAL form parsed + the dialog laid out + rendered ---
    CHECK(a.dialog.formParsed);
    CHECK(a.dialog.frm2);                       // the shipped trade form is FRM2
    CHECK(a.dialog.windowCount > 0);            // real windows built
    CHECK(a.dialog.rootWindow >= 0);
    CHECK((int)a.dialog.rows.size() >= 1);
    CHECK(a.dialog.buttonCount() >= 1);         // at least the BUY button
    CHECK(a.render.nonClearPixels > 0);         // the dialog actually painted
    CHECK(a.render.buttonsDrawn >= 1);

    // --- the BUY click hit the real button + issued the real command ---
    CHECK(a.click.hitButton);
    CHECK(a.click.role == WidgetRole::kBuyButton);
    CHECK(a.commandIssued);
    CHECK_EQ((int)a.slice.command.opcode, 17);  // VIBE_Command_QueueRequest17
    CHECK(a.slice.command.side == MarketSide::kBuy);
    CHECK_EQ((int)a.slice.command.proto, (int)ware);
    CHECK_EQ(a.slice.command.qty, 25);
    CHECK(a.slice.command.unitPrice > 0);       // the real oracle priced the ware
    CHECK(a.slice.enqueued);
    CHECK(a.slice.applied);

    // --- observable real-world effect: stock + treasury moved ---
    CHECK_EQ(a.slice.stockAfter - a.slice.stockBefore, 25);
    CHECK_EQ(a.slice.treasuryBefore - a.slice.treasuryAfter,
             a.slice.command.totalValue);
    CHECK(a.slice.treasuryMoved());
    CHECK(a.slice.stockMoved());
    CHECK(a.slice.tradeChangedWorld());
    CHECK(a.slice.dayChangedWorld());
    CHECK(a.slice.economyPasses > 0);

    // --- deterministic on rerun (byte-identical world hashes) ---
    DialogSliceResult b; i16 ware2 = 0; i32 id2 = 0;
    CHECK(runOnce(b, ware2, id2));
    CHECK_EQ(ware2, ware);
    CHECK_EQ(id2, id);
    CHECK_EQ(a.slice.hashBefore, b.slice.hashBefore);
    CHECK_EQ(a.slice.hashAfterCommand, b.slice.hashAfterCommand);
    CHECK_EQ(a.slice.hashAfterDay, b.slice.hashAfterDay);
    CHECK_EQ(a.slice.treasuryAfter, b.slice.treasuryAfter);
    CHECK_EQ(a.render.nonClearPixels, b.render.nonClearPixels);  // render reproducible
}
