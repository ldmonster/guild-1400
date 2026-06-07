// Unit: per-pass effects of the playable turn driver. Each individual sim core
// the driver wires (economy price tick, production integral, fire-event burn,
// time advance) must CHANGE its expected field — proving the real reconstructed
// code (not an inert default) runs in a single step.
#include "test.h"

#include "play/turn_driver.h"

#include "crt/rand.h"
#include "sim/gametime.h"
#include "sim/entity.h"
#include "sim/real_hooks.h"
#include "world/city.h"
#include "world/economy.h"
#include "world/economy_tick.h"
#include "world/production.h"
#include "world/event_fire.h"

using namespace guild;

// ---- the real-hook install flips the inert runtime to real --------------------
TEST(TurnDriverUnit, InstallsRealSimHooks) {
    play::TurnDriver d;
    CHECK(!d.hooksInstalled());
    CHECK(d.installRealHooks());
    CHECK(d.hooksInstalled());
    CHECK(d.installRealHooks());          // idempotent
    // The shared real CommandQueue exists (Init()'d by InstallRealSimHooks) so
    // the apply-3 jump table could be registered onto it.
    CHECK(sim::RealCommandQueue() != nullptr);
}

// ---- economy price tick mutates the smoothed price / city money ---------------
TEST(TurnDriverUnit, EconomyTickEvolvesPrice) {
    crt::Srand(123);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    std::vector<std::vector<world::PersonEcoView>> persons(world::kGoodCategoryCount);
    for (int g = 1; g < world::kGoodCategoryCount; ++g)
        persons[g].push_back(world::PersonEcoView{/*need=*/3, /*unemployed=*/true});

    float before = world::GetSmoothedPriceLevel();
    float moneyBefore = world::g_cityTotalMoney;
    world::EconomyTickPriceLevel(persons.data());
    float after = world::GetSmoothedPriceLevel();
    // The demand recompute set the city money total and the first tick seeded the
    // smoothed price from it — both must move off the zero seed.
    CHECK(world::g_cityTotalMoney != moneyBefore);
    CHECK(after != before);
}

// ---- production integral returns a positive work-minute count -----------------
TEST(TurnDriverUnit, ProductionIntegralIsPositive) {
    world::ProdTime s{0, 6, 0};
    world::ProdTime e{0, 22, 0};
    int m = world::ProductionComputeOutputOverTime(s, e, /*pause=*/false);
    CHECK(m > 0);
    // Daily-hour output (the fire-damage input) is also positive over a full day.
    int daily = world::ProductionComputeDailyHourOutput(s, e);
    CHECK(daily > 0);
}

// ---- fire-event burn tick subtracts damage from the building value ------------
namespace {
struct CountSink : world::FireEventHooks {
    i32 SpawnFire(i32) override { return 1; }
    void SetFireFlag(i32, int) override {}
    void AdvanceTime(int) override {}
    void RecordChronicle(i32, int) override {}
    int RandomModulo(int n) override { return n > 0 ? crt::RandNext() % n : 0; }
};
}

TEST(TurnDriverUnit, FireBurnTickReducesValue) {
    world::FireEvent ev{};
    world::FireEventInit(&ev, 7, 42, 50000);
    ev.flags |= world::kFireFlagAuthoritative;
    ev.step = 1; ev.subPhase = 3;
    CountSink sink;
    int before = ev.value;
    world::FireEventBurnTick(&ev, /*dailyHourOutput=*/1000, /*hasCatalyst=*/false,
                             /*catalyst=*/0, sink, /*chronicleTextId=*/1);
    CHECK(ev.value < before);             // real fire damage applied
}

// ---- time advance moves the wall clock forward one day ------------------------
TEST(TurnDriverUnit, TimeAdvanceMovesDay) {
    sim::GameTime c{};
    c.day = 5; c.hour = 6; c.minute = 0; c.second = 0;
    // The original folds "addDays" into the hour total then carries to days, so a
    // full calendar day is +24 on that arg (hour stays 6, day -> 6).
    sim::GameTimeAdvance(&c, /*addDays=*/24, /*addSeconds=*/0, /*addMinutes=*/0);
    CHECK_EQ(c.day, 6);
    CHECK_EQ(static_cast<int>(c.hour), 6);
}

// ---- a single driver turn moves multiple counters off their initial values ----
TEST(TurnDriverUnit, OneTurnEvolvesState) {
    play::TurnDriver d;
    auto snaps = d.run(/*seed=*/777, /*turns=*/1);
    CHECK_EQ(static_cast<int>(snaps.size()), 1);
    if (snaps.empty()) return;
    const play::WorldSnapshot& a = snaps[0];
    const play::WorldSnapshot& init = d.initialSnapshot();
    CHECK(a.day == init.day + 1);                 // clock advanced one day
    CHECK(a.workMinutes > 0);                      // production ran
    CHECK(a.fireValue < init.fireValue);           // fire damage applied
    CHECK(a.smoothedPrice != init.smoothedPrice);  // economy price moved
    CHECK(a.treasury != init.treasury);            // treasury changed
}
