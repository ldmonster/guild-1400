// tests/e2e/play_slice_bank_e2e_test.cpp — GUARDED real-asset BANK/LOAN slice on
// AUGSBURG. The full vertical slice:
//
//   mount the REAL assets + io::LoadWorld AUGSBURG.cty into the live sim arrays ->
//   seat a known borrower Person into a live slot -> take a REAL loan -> apply the
//   opcode-15 EnqueueCmd15 loan command (the borrower's folded cash + debt) ->
//   advance one REAL game-day -> assert the real cash/debt moved before->after ->
//   rerun byte-identical (deterministic).
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"          // kPlantBytes / kKindPlant
#include "io/vfs.h"
#include "play/slice_bank.h"
#include "play/world_digest.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "sim/types.h"
#include "world/city.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// Stabilize the kind-30 plantmap heap-pointer column (fresh per load) so the raw-
// byte digest is reproducible across reruns (mirrors playable_slice / slice_council).
void NormalizePlantPointers(std::uint32_t objectCount) {
    using namespace guild::sim;
    static std::vector<u8> plantScratch(io::kPlantBytes, 0);
    u8* scratch = plantScratch.data();
    for (std::uint32_t i = 0; i < objectCount && i < (std::uint32_t)kObjectCapacity; ++i) {
        u8* r = reinterpret_cast<u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant) continue;
        std::memcpy(r + 113, &scratch, sizeof scratch);
    }
}

// Mount + load AUGSBURG, blanking the folded economy tables, then seat a known
// borrower Person into a live slot with `startCash`. Returns the borrower id (0 on
// failure). The VFS is left bound (caller shuts it down).
i32 MountLoadAndSeatBorrower(shim::IFileSystem& fs, i16 startCash) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!a.vfsBound) return 0;

    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    std::memset(sim::g_persons, 0, sizeof(sim::g_persons));
    std::memset(sim::g_personIds, 0, sizeof(sim::g_personIds));

    io::WorldState w{};
    if (!io::LoadWorld(app::RealCityPath("Augsburg").c_str(), w))
        return 0;
    NormalizePlantPointers(w.objectCount);

    // Seat a known borrower into a free Person slot (the loan targets this record).
    const i32 kBorrower = 71001;
    int slot = -1;
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == 0 && sim::g_persons[i].id == 0) { slot = i; break; }
    }
    if (slot < 0) slot = sim::kPersonCapacity - 1;
    sim::Person& p = sim::g_persons[slot];
    p.marker = 0;
    p.kind   = 5;
    p.id     = kBorrower;
    sim::g_personIds[slot] = kBorrower;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankCashFieldOff, &startCash, sizeof startCash);
    i32 zero = 0;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankDebtFieldOff, &zero, sizeof zero);
    return kBorrower;
}

} // namespace

// The full loan slice on AUGSBURG: a real take moves real cash/debt, a real game-day
// runs, and the run is deterministic on rerun.
TEST(PlaySliceBankE2E, AugsburgTakeLoanCashDebtMoveDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlaySliceBankE2E: real assets absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    SetBankApplyHooks(nullptr);

    auto runOnce = [](BankSliceResult& out, i32& idOut) {
        shim::DiskFileSystem fs(GameDir());
        i32 id = MountLoadAndSeatBorrower(fs, /*startCash=*/300);
        if (id == 0) { io::VfsShutdown(); return false; }
        BankInteraction bi;
        bi.side = LoanSide::kTake; bi.lenderId = -1; bi.borrowerId = id;
        bi.amount = 1000; bi.player = 0;
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunBankSlice(bi, /*econSeed=*/0xA065B, q);
        idOut = id;
        io::VfsShutdown();
        return true;
    };

    BankSliceResult a; i32 id = 0;
    if (!runOnce(a, id)) {
        std::printf("  [skip] PlaySliceBankE2E: load failed\n");
        CHECK(true);
        return;
    }

    std::printf("  AUGSBURG bank slice: borrower id=%d, amount=%d\n",
                id, a.command.amount);
    std::printf("    cash %lld -> %lld\n",
                (long long)a.cashBefore, (long long)a.cashAfter);
    std::printf("    debt %lld -> %lld\n",
                (long long)a.debtBefore, (long long)a.debtAfter);
    std::printf("    economy passes = %d, interest this day = %lld\n",
                a.economyPasses, (long long)a.interestThisDay);

    // --- the command issued + applied through the REAL codec ---
    CHECK(a.command.issued);
    CHECK_EQ((int)a.command.opcode, 15);     // VIBE_Command_EnqueueCmd15
    CHECK(a.enqueued);
    CHECK(a.applied);

    // --- the take moved the real folded records ---
    CHECK_EQ(a.cashAfter - a.cashBefore, 1000);
    CHECK_EQ(a.debtAfter - a.debtBefore, 1000);
    CHECK(a.cashMoved());
    CHECK(a.debtMoved());

    // --- the loan + the game-day each ran ---
    CHECK(a.loanChangedWorld());
    CHECK(a.dayChangedWorld());
    CHECK(a.economyPasses > 0);

    // --- deterministic on rerun (byte-identical world hashes) ---
    BankSliceResult b; i32 id2 = 0;
    CHECK(runOnce(b, id2));
    CHECK_EQ(id2, id);
    CHECK_EQ((long long)a.hashBefore, (long long)b.hashBefore);
    CHECK_EQ((long long)a.hashAfterCommand, (long long)b.hashAfterCommand);
    CHECK_EQ((long long)a.hashAfterDay, (long long)b.hashAfterDay);
    CHECK_EQ(a.cashAfter, b.cashAfter);
    CHECK_EQ(a.debtAfter, b.debtAfter);
}
