// tests/e2e/dialog_bank_e2e_test.cpp — GUARDED Wave 30 PLAY B2: the REAL take-loan
// dialog on REAL assets. Loads the REAL shipped loan form
// (Resources/forms.BIN -> Locations/Geldleihe/Geldleihe_Kreditaufnehmen.form) through
// the REAL parser, lays out + renders the dialog (real window count, non-clear px),
// mounts + loads AUGSBURG and seats a borrower, then clicks confirm -> the REAL
// opcode-15 loan command -> observable world effect (cash + debt), deterministic.
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/archive_mount.h"
#include "io/save_world_load.h"
#include "io/save_person.h"          // kPlantBytes / kKindPlant
#include "io/vfs.h"
#include "play/dialog_bank.h"
#include "play/slice_bank.h"
#include "render/surface.h"
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

const char* kLoanFormMember = "Locations/Geldleihe/Geldleihe_Kreditaufnehmen.form";

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/forms.BIN") && fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

bool ReadLoanForm(shim::IFileSystem& fs, std::vector<u8>& out) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI",
                                 {"Resources/forms.BIN"}, /*caseInsensitive=*/true);
    bool ok = false;
    if (a.vfsBound) {
        io::ArchiveMount* m = a.archiveForMember(kLoanFormMember);
        if (m) ok = m->OpenMember(kLoanFormMember, out) && !out.empty();
    }
    io::VfsShutdown();
    return ok;
}

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

// Mount + load AUGSBURG, blank the folded tables, seat a known borrower. Returns the
// borrower id (0 on failure). Leaves the VFS bound (caller shuts it down).
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
    if (!io::LoadWorld(app::RealCityPath("Augsburg").c_str(), w)) return 0;
    NormalizePlantPointers(w.objectCount);

    const i32 kBorrower = 71001;
    int slot = -1;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker == 0 && sim::g_persons[i].id == 0) { slot = i; break; }
    if (slot < 0) slot = sim::kPersonCapacity - 1;
    sim::Person& p = sim::g_persons[slot];
    p.marker = 0; p.kind = 5; p.id = kBorrower;
    sim::g_personIds[slot] = kBorrower;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankCashFieldOff, &startCash, sizeof startCash);
    i32 zero = 0;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankDebtFieldOff, &zero, sizeof zero);
    return kBorrower;
}

} // namespace

TEST(DialogBankE2E, RealFormTakeLoanMovesCashDebtDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("[bank-dialog-e2e] assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    std::vector<u8> formBytes;
    {
        shim::DiskFileSystem fs(GameDir());
        if (!ReadLoanForm(fs, formBytes)) {
            std::printf("[bank-dialog-e2e] could not read %s — skipping\n", kLoanFormMember);
            CHECK(true);
            return;
        }
    }

    // The REAL loan form parses (FRM2, 5 windows).
    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000} };
    BankDialog probe = BuildBankDialog(formBytes.data(), formBytes.size(), lenders,
                                       /*borrower*/0, /*amount*/0, /*player*/0,
                                       kLoanFormMember);
    std::printf("[bank-dialog-e2e] form '%s' parsed=%d frm2=%d windows=%d "
                "root=(%d,%d %dx%d) rowWin=%d btnWin=%d sliderWin=%d\n",
                kLoanFormMember, (int)probe.formParsed, (int)probe.frm2,
                probe.windowCount, probe.panelX, probe.panelY, probe.panelW,
                probe.panelH, probe.rowWindow, probe.buttonWindow, probe.sliderWindow);
    CHECK(probe.formParsed);
    CHECK(probe.frm2);
    CHECK_EQ(probe.windowCount, 5);          // the real Kreditaufnehmen.form: 5 windows
    CHECK(probe.rootWindow >= 0);
    CHECK(probe.rowWindow >= 0);
    CHECK(probe.buttonWindow >= 0);
    CHECK(probe.sliderWindow >= 0);

    SetBankApplyHooks(nullptr);

    auto runOnce = [&](BankDialogResult& out, i32& idOut) {
        shim::DiskFileSystem fs(GameDir());
        i32 id = MountLoadAndSeatBorrower(fs, /*startCash=*/300);
        if (id == 0) { io::VfsShutdown(); return false; }
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunBankDialog(formBytes.data(), formBytes.size(), lenders,
                            /*borrower*/id, /*amount*/1000, /*player*/0,
                            /*selectedRow*/0, /*econSeed*/0xA065B, q,
                            /*fbW*/560, /*fbH*/640, kLoanFormMember);
        idOut = id;
        io::VfsShutdown();
        return true;
    };

    BankDialogResult a; i32 id = 0;
    if (!runOnce(a, id)) {
        std::printf("[bank-dialog-e2e] load failed — skipping\n");
        CHECK(true);
        return;
    }

    std::printf("[bank-dialog-e2e] borrower=%d rows=%d rowsDrawn=%d buttonsDrawn=%d "
                "nonClear=%d opcode=%d applied=%d cash %lld->%lld debt %lld->%lld\n",
                id, (int)a.dialog.lenders.size(), a.render.rowsDrawn,
                a.render.buttonsDrawn, a.render.nonClearPixels,
                (int)a.slice.command.opcode, (int)a.slice.applied,
                (long long)a.slice.cashBefore, (long long)a.slice.cashAfter,
                (long long)a.slice.debtBefore, (long long)a.slice.debtAfter);

    // The real form rendered widgets (lender rows + confirm button) + non-clear px.
    CHECK_EQ((int)a.dialog.lenders.size(), 2);
    CHECK_EQ(a.dialog.rowCount(), 2);
    CHECK_EQ(a.render.rowsDrawn, 2);
    CHECK_EQ(a.render.buttonsDrawn, 2);
    CHECK(a.render.nonClearPixels > 0);

    // The confirm click emitted the REAL opcode-15 command + applied it.
    CHECK(a.click.hitConfirm);
    CHECK(a.commandIssued);
    CHECK_EQ((int)a.slice.command.opcode, 15);
    CHECK(a.slice.enqueued);
    CHECK(a.slice.applied);

    // Observable world effect: cash + debt each rose by the loan amount.
    CHECK_EQ(a.slice.cashAfter - a.slice.cashBefore, 1000);
    CHECK_EQ(a.slice.debtAfter - a.slice.debtBefore, 1000);
    CHECK(a.slice.loanChangedWorld());
    CHECK(a.slice.dayChangedWorld());

    // Deterministic: a full rerun reproduces the same render + hashes.
    BankDialogResult b; i32 id2 = 0;
    if (runOnce(b, id2)) {
        CHECK_EQ((long long)b.slice.hashBefore, (long long)a.slice.hashBefore);
        CHECK_EQ((long long)b.slice.hashAfterCommand, (long long)a.slice.hashAfterCommand);
        CHECK_EQ((long long)b.slice.hashAfterDay, (long long)a.slice.hashAfterDay);
        CHECK_EQ(b.render.nonClearPixels, a.render.nonClearPixels);
    }

    std::printf("[bank-dialog-e2e] determinism OK; nonClear=%d cashDelta=%lld\n",
                a.render.nonClearPixels,
                (long long)(a.slice.cashAfter - a.slice.cashBefore));
}
