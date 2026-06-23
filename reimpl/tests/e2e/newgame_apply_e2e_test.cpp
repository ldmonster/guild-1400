// E2E (guarded on the real game install): the new-game commit applied over the
// REAL loaded world — exactly the original sequence: Save_LoadGameFile
// ("<city>.cty", 0x533d4b) then VIBE_Command_EnqueueInheritanceTransfer
// (0x533e03) — the player + parents land in the LIVE sim::g_persons array
// alongside the city's loaded population, and the start-gold pass funds the
// player at the chosen difficulty.
#include "test.h"

#include "play/newgame_apply.h"
#include "gui/newgame_setup.h"
#include "app/real_boot.h"         // MountRealGameAssets / RealGameAssets
#include "io/save_world_load.h"    // LoadWorld
#include "io/vfs.h"                // VfsShutdown
#include "sim/command.h"
#include "sim/command_apply.h"
#include "sim/command_apply5.h"
#include "sim/command_apply6.h"    // RegisterApplyHandlers6 / Apply6_Relations (0x1B)
#include "sim/entity.h"
#include "sim/person_create.h"
#include "shim_impl/disk_filesystem.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

i32 R32(const sim::Person* p, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, 4);
    return v;
}

} // namespace

TEST(NewGameApplyE2E, CommitIntoRealLoadedAugsburg) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] NewGameApplyE2E.CommitIntoRealLoadedAugsburg: real game "
                    "dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // 1. Mount the real assets + load the real city (the 0x533d4b load).
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);
    sim::ResetEntityArrays();
    sim::ResetApply5State();
    sim::ResetPersonCreate();
    io::WorldState world{};
    CHECK(io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world));
    int loadedPersons = 0;
    i32 maxId = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == -1) continue;
        ++loadedPersons;
        if (sim::g_persons[i].id > maxId) maxId = sim::g_persons[i].id;
    }
    sim::g_personNextId = maxId + 1;     // allocate after the city's population
    sim::g_personArrayLoaded = true;
    crt::Srand(0x4711);

    // 2. The committed parameter block the menu chain produced.
    gui::NewGameParams p;
    gui::NewGame_ApplyCity(p, "stadt_AUGSBURG", "AUGSBURG", false);
    p.difficulty = 2;
    p.historyFlag = 1;
    gui::NewGame_ApplyPlayer(p, "Test", "Player", /*wappen=*/3, /*gender=*/0,
                             /*faith=*/0);
    gui::NewGame_ApplyProfession(p, /*beruf=*/1);
    gui::NewGame_Commit(p);

    // 3. The commit (0x5336f0 + the 0x533f40 gold pass). The host queue carries
    // batch 6 (app::wiring registers it on the owned queue), so the six
    // opcode-27 relation packets dispatch to ExComputeObjectCoords @0x49818C.
    sim::ResetApply6State();
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    sim::RegisterApplyHandlers6(q);
    play::NewGameApplyInputs in;
    in.rateByte = 100;
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, p.difficulty, q, in);

    std::printf("[newgame-e2e] loadedPersons=%d player=%d mother=%d father=%d "
                "goldBase=%d funded=%zu\n",
                loadedPersons, r.playerId, r.motherId, r.fatherId,
                r.startGoldBase, r.goldSeededIds.size());

    CHECK(loadedPersons > 0);            // the city shipped a population
    CHECK(r.applied);
    CHECK(!r.createFailed);

    // The player + parents live in the SAME array as the loaded population.
    sim::Person* player = sim::PersonFindRecordById(r.playerId);
    sim::Person* mother = sim::PersonFindRecordById(r.motherId);
    sim::Person* father = sim::PersonFindRecordById(r.fatherId);
    CHECK(player != nullptr);
    CHECK(mother != nullptr);
    CHECK(father != nullptr);
    CHECK_EQ((int)player->kind, 6);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(player) + 0x30, "Test"), 0);
    CHECK_EQ(R32(player, 0x54), gui::Wappen_IdForIndex(3));
    CHECK_EQ(R32(player, 0x60), r.fatherId);
    CHECK_EQ(R32(player, 0x64), r.motherId);
    CHECK_EQ(R32(player, 0x18C), gui::kStartCommandValue);

    // Start gold at difficulty 2: base 750; the fresh player is funded.
    CHECK_EQ(r.startGoldBase, 750);
    bool playerFunded = false;
    for (i32 id : r.goldSeededIds) playerFunded |= (id == r.playerId);
    CHECK(playerFunded);

    // The six opcode-27 relation packets (0x533930..0x5339ae, mode 0 delta 127)
    // landed in the primary 768x768 grid at the records' slot indices — over
    // the REAL loaded population.
    auto slotOf = [](const sim::Person* rec) -> int {
        return static_cast<int>(rec - sim::g_persons);
    };
    const int P = slotOf(player), F = slotOf(father), M = slotOf(mother);
    sim::RelationState& rel = sim::Apply6_Relations();
    CHECK_EQ((int)rel.A(P, F), 127);
    CHECK_EQ((int)rel.A(F, P), 127);
    CHECK_EQ((int)rel.A(P, M), 127);
    CHECK_EQ((int)rel.A(M, P), 127);
    CHECK_EQ((int)rel.A(F, M), 127);
    CHECK_EQ((int)rel.A(M, F), 127);
    CHECK_EQ((int)rel.B(P, F), 0);       // mode 0 never touches the second grid

    sim::ResetEntityArrays();
    io::VfsShutdown();
}
