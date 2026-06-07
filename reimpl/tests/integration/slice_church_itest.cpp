// tests/integration/slice_church_itest.cpp — the CHURCH/RELIGION (donation) slice
// on a small real-FORMAT world (synthetic entity records + two seated objects in
// the live g_objects table; no shipped assets). Drives the real loop:
//   seed -> opcode-15 donation command (through the REAL CommandQueue codec +
//   ExRemapObjectPair apply) -> one game-day,
// and asserts:
//   * a FOLDED object money field changed (object+77 credited by the REAL apply),
//   * HashFullWorld() differs pre/post the command AND across the whole slice,
//   * the run is BYTE-IDENTICAL on a rerun (the play-layer determinism contract:
//     blank the folded tables, Srand anchors the RNG, and the day dirties them so
//     they MUST be re-zeroed for the compare).
#include "test.h"

#include "play/slice_church.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/command.h"
#include "sim/command_apply2.h"
#include "sim/entity.h"
#include "world/city.h"
#include "world/law.h"
#include "world/office.h"
#include "world/crime.h"
#include "world/relation.h"
#include "world/event.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::world;
using namespace guild::sim;

namespace {

ChurchInteraction Interaction() {
    ChurchInteraction it;
    it.action       = ChurchAction::kDonate;
    it.churchId     = 808;
    it.donorAccount = 909;
    it.amount       = 500;
    it.currencyType = 0;
    return it;
}

// Blank the folded tables, seed a small real-format substrate, seat donor+church.
// Mirrors RunChurchSlice's preamble so the itest exercises the same determinism rig
// without shipped assets. RunGameDay dirties law/office/crime/relation, so a
// faithful re-run MUST re-zero them here before re-seeding.
void SeedDonationWorld(std::uint32_t seed, const ChurchInteraction& it) {
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_persons, 0, sizeof(g_persons));
    ResetEntityArrays();
    ResetApply2State();

    std::memset(g_lawTable, 0, sizeof(g_lawTable));
    std::memset(g_officeHolders, 0, sizeof(g_officeHolders));
    std::memset(g_crimeTable, 0, sizeof(g_crimeTable));
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
    std::memset(g_eventTable, 0, sizeof(g_eventTable));
    g_eventTableCount = 0;
    g_missionLcgState = 0;

    crt::Srand(seed);
    CityInitParameterTable(100.0f);
    g_capDivisor = 0.0f;
    g_cityTotalMoney = 0.0f;
    g_cityTotalGoods = 0.0f;

    // A few alive object records (a real-format substrate for the day to run over).
    for (int i = 0; i < 4; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 100 + i * 5;
    }

    // Seat the donor (with funds) + the church (empty) the donation moves between.
    SeatChurchObject(it.donorAccount, /*money=*/it.amount * 4 + 1000);
    SeatChurchObject(it.churchId,     /*money=*/0);
}

} // namespace

// ---------------------------------------------------------------------------
// The folded money field changes + the world hash moves across the slice.
// ---------------------------------------------------------------------------
TEST(SliceChurchItest, DonationFieldAndHashEvolveOverSlice) {
    const std::uint32_t seed = 0xC0FFEE;
    ChurchInteraction it = Interaction();

    SeedDonationWorld(seed, it);
    crt::Srand(seed);
    std::uint64_t hLoad = HashFullWorld();

    i64 churchBefore = ReadObjectMoney(it.churchId);
    i64 donorBefore  = ReadObjectMoney(it.donorAccount);
    CHECK_EQ((long long)churchBefore, 0LL);

    // -- click -> opcode-15 command -> REAL apply (CommandQueue + ExRemapObjectPair) --
    {
        sim::CommandQueue q;
        q.Init();
        InstallChurchCommandHandler(q);
        ChurchCommand cmd = ClassifyChurchInteraction(it);
        CHECK(cmd.issued);
        CHECK_EQ((int)cmd.opcode, 15);
        sim::CommandPacket pkt = cmd.Encode();
        i32 slot = q.EnqueuePacket(pkt);
        CHECK(slot >= 0);
        q.FlushSendQueue();
        q.ExecCommands();
    }

    i64 churchAfter = ReadObjectMoney(it.churchId);
    i64 donorAfter  = ReadObjectMoney(it.donorAccount);
    CHECK_EQ((long long)churchAfter, (long long)(churchBefore + it.amount));
    CHECK_EQ((long long)donorAfter,  (long long)(donorBefore - it.amount));

    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();
    CHECK(hCmd != hLoad);                 // the donation moved a folded field

    // -- one game-day --
    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    GameDayDeltas d = RunGameDay(seed, st);
    CHECK(d.stepsRun > 0);
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();
    CHECK(hDay != hLoad);                 // the slice evolved the world

    std::printf("[church-itest] church %lld->%lld donor %lld->%lld steps=%d\n",
                (long long)churchBefore, (long long)churchAfter,
                (long long)donorBefore, (long long)donorAfter, d.stepsRun);
    std::printf("[church-itest] hLoad=%llu hCmd=%llu hDay=%llu\n",
                (unsigned long long)hLoad, (unsigned long long)hCmd,
                (unsigned long long)hDay);
}

// ---------------------------------------------------------------------------
// Byte-identical rerun: the whole slice reproduces the same per-step hashes.
// ---------------------------------------------------------------------------
TEST(SliceChurchItest, SliceIsByteIdenticalOnRerun) {
    const std::uint32_t seed = 0xBEEF;
    ChurchInteraction it = Interaction();

    auto runOnce = [&](std::uint64_t out[3]) {
        SeedDonationWorld(seed, it);
        crt::Srand(seed);
        out[0] = HashFullWorld();
        {
            sim::CommandQueue q;
            q.Init();
            InstallChurchCommandHandler(q);
            ChurchCommand cmd = ClassifyChurchInteraction(it);
            sim::CommandPacket pkt = cmd.Encode();
            if (q.EnqueuePacket(pkt) >= 0) { q.FlushSendQueue(); q.ExecCommands(); }
        }
        crt::Srand(seed);
        out[1] = HashFullWorld();
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
        crt::Srand(seed);
        out[2] = HashFullWorld();
    };

    std::uint64_t a[3], b[3];
    runOnce(a);
    runOnce(b);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)a[i], (long long)b[i]);
    // command + day each genuinely moved the world.
    CHECK(a[1] != a[0]);
    CHECK(a[2] != a[1]);
}
