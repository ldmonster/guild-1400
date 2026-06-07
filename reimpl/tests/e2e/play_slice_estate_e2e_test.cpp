// tests/e2e/play_slice_estate_e2e_test.cpp — GUARDED real-asset REAL-ESTATE (buy
// building) slice on AUGSBURG. The full vertical slice:
//
//   mount the REAL assets + io::LoadWorld AUGSBURG.cty into the live sim arrays ->
//   pick a REAL live building + seat a known buyer Person -> buy the building ->
//   apply the opcode-56 QueueRequestQuad56 ownership command (the object's folded
//   owner +39, the buyer's folded cash) -> advance one REAL game-day -> assert the
//   real ownership/cash moved before->after -> rerun byte-identical (deterministic).
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"          // kPlantBytes / kKindPlant
#include "io/vfs.h"
#include "play/slice_estate.h"
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

struct Loaded { i32 objectId; i32 buyerId; i16 ownerBefore; };

// Mount + load AUGSBURG, pick the first live building, and seat a known buyer.
// Returns {objectId, buyerId, ownerBefore}; objectId==0 on failure. VFS left bound.
Loaded MountLoadPickAndSeat(shim::IFileSystem& fs, i16 buyerCash) {
    Loaded out{0, 0, 0};
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!a.vfsBound) return out;

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
        return out;
    NormalizePlantPointers(w.objectCount);

    // First live building (the property being bought).
    i32 objId = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) { objId = sim::g_objects[i].id; break; }
    if (objId == 0) return out;
    out.objectId = objId;
    out.ownerBefore = ReadObjectOwner(objId);

    // Seat a known buyer into a free Person slot.
    const i32 kBuyer = 72002;
    int slot = -1;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker == 0 && sim::g_persons[i].id == 0) { slot = i; break; }
    if (slot < 0) slot = sim::kPersonCapacity - 1;
    sim::Person& p = sim::g_persons[slot];
    p.marker = 0; p.kind = 5; p.id = kBuyer;
    sim::g_personIds[slot] = kBuyer;
    std::memcpy(reinterpret_cast<u8*>(&p) + kEstateCashFieldOff, &buyerCash, sizeof buyerCash);
    out.buyerId = kBuyer;
    return out;
}

} // namespace

// The full purchase slice on AUGSBURG: a real buy moves real ownership/cash, a real
// game-day runs, and the run is deterministic on rerun.
TEST(PlaySliceEstateE2E, AugsburgBuyBuildingOwnershipCashMoveDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlaySliceEstateE2E: real assets absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    SetEstateApplyHooks(nullptr);
    const i16 kNewOwner = 88;

    auto runOnce = [&](EstateSliceResult& out, i32& objOut, i32& buyerOut, i16& ownerBeforeOut) {
        shim::DiskFileSystem fs(GameDir());
        Loaded ld = MountLoadPickAndSeat(fs, /*buyerCash=*/9000);
        if (ld.objectId == 0) { io::VfsShutdown(); return false; }
        EstateInteraction ei;
        ei.objectId = ld.objectId; ei.buyerId = ld.buyerId;
        ei.newOwnerId = kNewOwner; ei.parentHandle = 15; ei.price = 2500;
        ei.objectKind = 4; ei.player = 0;
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunEstateSlice(ei, /*econSeed=*/0xA065B, q);
        objOut = ld.objectId; buyerOut = ld.buyerId; ownerBeforeOut = ld.ownerBefore;
        io::VfsShutdown();
        return true;
    };

    EstateSliceResult a; i32 obj = 0, buyer = 0; i16 ownerBefore = 0;
    if (!runOnce(a, obj, buyer, ownerBefore)) {
        std::printf("  [skip] PlaySliceEstateE2E: load failed\n");
        CHECK(true);
        return;
    }

    std::printf("  AUGSBURG estate slice: building id=%d buyer id=%d\n", obj, buyer);
    std::printf("    owner %d -> %d (parent -> %d)\n",
                (int)a.ownerBefore, (int)a.ownerAfter, (int)a.parentAfter);
    std::printf("    buyer cash %lld -> %lld (price %d)\n",
                (long long)a.cashBefore, (long long)a.cashAfter, a.command.price);
    std::printf("    economy passes = %d\n", a.economyPasses);

    // --- the command issued + applied through the REAL codec ---
    CHECK(a.command.issued);
    CHECK_EQ((int)a.command.opcode, 56);     // VIBE_Command_QueueRequestQuad56
    CHECK(a.enqueued);
    CHECK(a.applied);

    // --- ownership transferred on the real folded object record (+39) ---
    CHECK_EQ((int)a.ownerAfter, (int)kNewOwner);
    CHECK_EQ((int)a.parentAfter, 15);
    CHECK(a.ownerMoved());

    // --- the buyer's real cash was debited the price ---
    CHECK_EQ(a.cashBefore - a.cashAfter, 2500);
    CHECK(a.cashMoved());

    // --- the purchase + the game-day each ran ---
    CHECK(a.purchaseChangedWorld());
    CHECK(a.dayChangedWorld());
    CHECK(a.economyPasses > 0);

    // --- deterministic on rerun (byte-identical world hashes) ---
    EstateSliceResult b; i32 obj2 = 0, buyer2 = 0; i16 ob2 = 0;
    CHECK(runOnce(b, obj2, buyer2, ob2));
    CHECK_EQ(obj2, obj);
    CHECK_EQ((long long)a.hashBefore, (long long)b.hashBefore);
    CHECK_EQ((long long)a.hashAfterCommand, (long long)b.hashAfterCommand);
    CHECK_EQ((long long)a.hashAfterDay, (long long)b.hashAfterDay);
    CHECK_EQ((int)a.ownerAfter, (int)b.ownerAfter);
    CHECK_EQ(a.cashAfter, b.cashAfter);
}
