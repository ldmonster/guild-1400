// tests/e2e/slice_tavern_e2e_test.cpp — the TAVERN / SOCIAL slice on the REAL shipped
// AUGSBURG city (GUARDED; skips cleanly if the asset is absent / honors
// GUILD_GAME_DIR).
//
// AUGSBURG ships exactly ONE person record (cityRecCount == 1): slot 0, id 510, the
// player/guild-master entity, relation/superior fields (+0x5C/+0x60) == -1 (unbound).
// With a single person a two-party dark-corner RECRUIT between two REAL persons is
// impossible, so — per the task brief — we DOCUMENT that and assert on what is
// present:
//
//   * the real person loads with its shipped relation field (+0x5C) == -1 (unbound),
//   * the op-83 ("buy dunkle ecke") apply path, run against the REAL loaded record
//     (the real person as the recruited target), writes that folded person field
//     (+0x5C / +0x60) and debits its cash word (+0x0A) — a genuine real
//     before(-1) -> after change,
//   * HashFullWorld() (which folds g_persons incl. the real record) moves off
//     baseline and is byte-identical on rerun,
//   * one real economy day then evolves the world deterministically.
//
// Every link is a REAL reconstructed sibling (MountRealGameAssets / io::LoadWorld /
// the op-83 tavern apply / RunEconomyTurn / HashFullWorld).
#include "test.h"

#include "play/slice_tavern.h"
#include "play/world_digest.h"
#include "play/turn_economy.h"
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

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

bool LoadAugsburg(u32* persons, u32* objects) {
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!a.vfsBound) { io::VfsShutdown(); return false; }
    // Blank EVERY folded world table (incl. economy tables a prior RunEconomyTurn
    // dirtied) so the reload hash is a pure function of the city + seed.
    TavernZeroWorldGlobals();
    ResetEntityArrays();
    io::WorldState w{};
    bool ok = io::LoadWorld(app::RealCityPath("Augsburg").c_str(), w);
    if (persons) *persons = w.cityRecCount;
    if (objects) *objects = w.objectCount;
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// The real AUGSBURG person: load it, apply the real op-83 recruit, run a day.
// ---------------------------------------------------------------------------
TEST(SliceTavernE2E, AugsburgRealPersonRecruitDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SliceTavernE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[tav-e2e] asset dir: %s\n", GameDir().c_str());

    u32 persons = 0, objects = 0;
    bool ok = LoadAugsburg(&persons, &objects);
    CHECK(ok);
    if (!ok) { io::VfsShutdown(); return; }
    std::printf("[tav-e2e] loaded persons=%u objects=%u\n", persons, objects);
    CHECK_EQ(persons, (u32)1);
    CHECK_EQ(objects, (u32)55);

    // Find the single real person.
    Person* real = nullptr;
    i32 realId = 0;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker != -1) {
            real = &g_persons[i]; realId = g_personIds[i]; break;
        }
    }
    CHECK(real != nullptr);
    if (!real) { io::VfsShutdown(); return; }
    int kind   = PersonGetByte(real, kPfKind);
    int player = PersonGetByte(real, kPfIsPlayer);
    int cash   = PersonGetWord(real, kTvCashOff);
    i32 relBefore = PersonGetDword(real, kTvRelationOff);
    std::printf("[tav-e2e] real person id=%d kind=%d isPlayer=%d cash=%d relation(+0x5C)=%d\n",
                realId, kind, player, cash, relBefore);
    CHECK_EQ(realId, 510);
    CHECK_EQ(relBefore, -1);        // unbound in the shipped save

    // Baseline hash of the real loaded world (RNG re-anchored for reproducibility).
    crt::Srand(0xA065B);
    std::uint64_t hLoad = HashFullWorld();

    // --- the op-83 dark-corner-buy command, built byte-exact -------------
    // One-person world: the real person is BOTH the conceptual target (the recruited
    // dark-corner NPC) and we use a fixed buyer token for the binding party. The
    // affordability gate reads the buyer's cash; since the buyer token has no record,
    // we drive the build against the real person as the buyer to exercise the real
    // CheckResourceAmount gate, then apply the recruit binding onto the real record.
    const i32 buyerToken = 510;     // the real player (id 510) is the buyer
    const i32 npcToken   = 510;     // the real record stands in as the recruited NPC
    const int price = (cash > 2) ? 2 : 0;   // affordable against the shipped cash

    TavernClick c;
    c.action = TavernAction::kRecruitThug;
    c.playerId = buyerToken; c.targetId = npcToken;
    c.locationId = 7; c.price = price;
    TavernPacket pkt = BuildTavernPacket(c);
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 83);
    u8 wire[36];
    pkt.encode(wire);
    CHECK_EQ((int)wire[0], 83);
    CHECK_EQ((unsigned)((u32)wire[16] | ((u32)wire[17] << 8) |
                        ((u32)wire[18] << 16) | ((u32)wire[19] << 24)),
             0x62757920u);          // "buy " tag on the real wire image
    std::printf("[tav-e2e] op-83 packet built: opcode=%d tag=buy player=%d target=%d price=%d\n",
                (int)pkt.opcode, pkt.playerId, pkt.targetId, pkt.price);

    SetTavernApplyHooks(nullptr);   // inert-default recruit apply
    TavernOrder o = IssueTavernClick(c);
    i32 relAfter = PersonGetDword(real, kTvRelationOff);
    i32 supAfter = PersonGetDword(real, kTvSuperiorOff);
    int cashAfter = PersonGetWord(real, kTvCashOff);
    std::printf("[tav-e2e] recruit issued=%d applied=%d relation(+0x5C) %d->%d "
                "superior(+0x60)=%d cash %d->%d\n",
                (int)o.issued, (int)o.applied, relBefore, relAfter, supAfter,
                cash, cashAfter);
    CHECK(o.issued);
    CHECK(o.applied);
    CHECK_EQ(relAfter, buyerToken);     // real folded field bound through real apply
    CHECK_EQ(supAfter, buyerToken);
    CHECK(relAfter != relBefore);       // the REAL person field genuinely changed
    CHECK_EQ(cashAfter, cash - price);  // the real cash word was debited the price

    crt::Srand(0xA065B);
    std::uint64_t hCmd = HashFullWorld();
    CHECK(hCmd != hLoad);               // the real person mutation moved the hash

    // --- a real economy day ---
    crt::Srand(0xA065B);
    EconomyTurnState st = SeedEconomyTurnState();
    st.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(st);
    crt::Srand(0xA065B);
    std::uint64_t hDay = HashFullWorld();
    std::printf("[tav-e2e] hLoad=%llu hCmd=%llu hDay=%llu econPasses=%d\n",
                (unsigned long long)hLoad, (unsigned long long)hCmd,
                (unsigned long long)hDay, d.passesRun);
    CHECK(d.passesRun > 0);
    CHECK(hDay != hCmd);

    io::VfsShutdown();

    // --- determinism: a full rerun reproduces the same three hashes ---
    u32 p2 = 0, o2 = 0;
    bool ok2 = LoadAugsburg(&p2, &o2);
    CHECK(ok2);
    if (ok2) {
        Person* real2 = nullptr;
        for (int i = 0; i < kPersonCapacity; ++i)
            if (g_persons[i].marker != -1) { real2 = &g_persons[i]; break; }
        crt::Srand(0xA065B);
        std::uint64_t hLoad2 = HashFullWorld();
        CHECK_EQ((long long)hLoad2, (long long)hLoad);
        if (real2) {
            TavernClick c2 = c;
            (void)IssueTavernClick(c2);
        }
        crt::Srand(0xA065B);
        std::uint64_t hCmd2 = HashFullWorld();
        CHECK_EQ((long long)hCmd2, (long long)hCmd);
        crt::Srand(0xA065B);
        EconomyTurnState st2 = SeedEconomyTurnState();
        st2.day = 0;
        RunEconomyTurn(st2);
        crt::Srand(0xA065B);
        std::uint64_t hDay2 = HashFullWorld();
        CHECK_EQ((long long)hDay2, (long long)hDay);
    }
    io::VfsShutdown();
}
