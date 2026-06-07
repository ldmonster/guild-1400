// tests/integration/slice_production_itest.cpp — Wave 27 P5 BUILDINGS/PRODUCTION
// slice, integration. A small real-FORMAT world (live entity records + a seeded
// production-table workshop, no shipped assets). Drives the real loop:
//   click on a workshop -> opcode-28 production order -> apply (writes the slot) ->
//   the REAL per-building production tick -> one game-day (real economy passes)
// and asserts:
//   * a folded PRODUCTION field changed (the slot yield moved) AND the treasury
//     evolved over the day,
//   * HashFullWorld() differs pre vs post,
//   * the run is byte-identical on rerun (determinism).
//
// Every step calls the REAL sibling (QueueRequestSlotReset28 / RunProductionTick /
// RunEconomyTurn / HashFullWorld) over the live arrays.
#include "test.h"

#include "play/slice_production.h"
#include "play/interact_building.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/command.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_production.h"
#include "crt/rand.h"
#include "world/city.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {
const int kBld  = 1;     // production-table building row
const i16 kProt = 51;

// Seed a small real-format world: live object records + the workshop's production
// tables + a deterministic economy baseline. Fully zeroes the entity arrays first so
// unserialized pad bytes are clean across the two determinism passes.
void SeedRealFormatWorld(std::uint32_t seed) {
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_persons, 0, sizeof(g_persons));
    std::memset(g_sceneNodes, 0, sizeof(g_sceneNodes));
    ResetEntityArrays();
    ResetProductionTables();
    for (int i = 0; i < 6; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 400 + i * 3;
    }
    g_sceneNodeCount = 0;

    // A faithful one-slot workshop the REAL production tick can integrate: a scene
    // type for the prot (kind 6, cached price) + a rising schedule + slot scales.
    SceneTypeDef& td = g_sceneTypes[kProt];
    std::memset(&td, 0, sizeof td);
    td.kind = 6; td.baseValue = 4000; td.priceField = 1; td.divisor = 1; td.cachedPrice = 600;
    g_sceneTypesLoaded = true;
    ProdSchedule& sc = g_prodSchedules[kBld];
    std::memset(&sc, 0, sizeof sc);
    sc.hasProduction = 1;
    sc.input[0] = {0,0}; sc.input[1] = {2000,1000};
    sc.output[0] = {0,0}; sc.output[1] = {2000,1000};
    ProdBuilding pb = ProdBuildingAt(kBld);
    pb.inScale() = 1000; pb.outScale() = 1000;

    crt::Srand(seed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
}

struct RunOut {
    std::uint64_t hashLoad = 0, hashCmd = 0, hashDay = 0;
    float yieldBefore = 0, yieldAfter = 0;
    i64   treasuryBefore = 0, treasuryAfter = 0;
    int   appliedSlot = -1;
    bool  enqueued = false, issued = false;
};

RunOut RunSlice(std::uint32_t seed, std::uint32_t econSeed) {
    RunOut o;
    SeedRealFormatWorld(seed);
    crt::Srand(econSeed);
    o.hashLoad = HashFullWorld();

    // --- click -> opcode-28 order -> apply ---
    {
        CommandQueue q;
        q.Init();
        q.set_standalone(true);
        InstallProductionCommandHandler(q);
        SetProductionApplyHook(nullptr);

        ScenePickObject obj; obj.id = kBld + 1;
        float eye[3] = {0,0,0};
        CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 64, 64);
        auto kindOf = [](i32){ return 247; };
        ProductionClickResult pc =
            IssueProductionClick(q, cam, 0.0f, 0.0f, &obj, 1, 64.0f, kindOf,
                                 kBld, kProt, ProductionMenuItem::kWriteProduction);
        o.issued = pc.command.issued;
        o.enqueued = pc.enqueued;
        o.appliedSlot = pc.appliedSlot;
    }
    o.hashCmd = HashFullWorld();

    // --- real production tick + game-day ---
    {
        ProdBuilding pb = ProdBuildingAt(kBld);
        int s = (o.appliedSlot >= 0) ? o.appliedSlot : 0;
        o.yieldBefore = pb.slotYield(s);
        Building_RunProductionTick(kBld, /*nowDay=*/1, /*nowMinute=*/1500);
        o.yieldAfter = pb.slotYield(s);

        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        o.treasuryBefore = st.treasury;
        RunEconomyTurn(st);
        o.treasuryAfter = st.treasury;
    }
    o.hashDay = HashFullWorld();
    return o;
}

} // namespace

// ---------------------------------------------------------------------------
// The folded production field changes + treasury evolves; world hash moves.
// ---------------------------------------------------------------------------
TEST(SliceProductionItest, ProductionAndWorldEvolve) {
    RunOut o = RunSlice(0x77AA, 0xCAFE);

    std::printf("[prod-itest] issued=%d enqueued=%d slot=%d | yield %.2f -> %.2f | "
                "treasury %lld -> %lld\n",
                (int)o.issued, (int)o.enqueued, o.appliedSlot,
                o.yieldBefore, o.yieldAfter,
                (long long)o.treasuryBefore, (long long)o.treasuryAfter);
    std::printf("[prod-itest] hashLoad=%llu hashCmd=%llu hashDay=%llu\n",
                (unsigned long long)o.hashLoad, (unsigned long long)o.hashCmd,
                (unsigned long long)o.hashDay);

    CHECK(o.issued);
    CHECK(o.enqueued);
    CHECK(o.appliedSlot >= 0);

    // The command alone changed the world (the slot record was written).
    CHECK(o.hashCmd != o.hashLoad);
    // The REAL production tick moved the slot yield (a folded production field).
    CHECK(o.yieldAfter != o.yieldBefore);
    // The game-day moved the treasury (real economy passes).
    CHECK(o.treasuryAfter != o.treasuryBefore);
    // The whole-world hash moved across the slice.
    CHECK(o.hashDay != o.hashLoad);
    CHECK(o.hashDay != o.hashCmd);

    ResetEntityArrays();
    ResetProductionTables();
}

// ---------------------------------------------------------------------------
// Determinism: a full rerun reproduces byte-identical hashes + observables.
// ---------------------------------------------------------------------------
TEST(SliceProductionItest, DeterministicAcrossReruns) {
    RunOut a = RunSlice(0x1234, 0x2024);
    RunOut b = RunSlice(0x1234, 0x2024);

    std::printf("[prod-itest] rerun hashDay a=%llu b=%llu\n",
                (unsigned long long)a.hashDay, (unsigned long long)b.hashDay);

    CHECK_EQ(a.hashLoad, b.hashLoad);
    CHECK_EQ(a.hashCmd, b.hashCmd);
    CHECK_EQ(a.hashDay, b.hashDay);
    CHECK_EQ(a.yieldAfter, b.yieldAfter);
    CHECK_EQ((long long)a.treasuryAfter, (long long)b.treasuryAfter);

    ResetEntityArrays();
    ResetProductionTables();
}
