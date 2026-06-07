#include "play/turn_driver.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "crt/rand.h"
#include "sim/gametime.h"
#include "sim/entity.h"
#include "sim/real_hooks.h"    // InstallRealSimHooks, RealCommandQueue
#include "sim/real_hooks2.h"   // InstallRealSimHooks2
#include "sim/real_hooks3.h"   // InstallRealSimHooks3
#include "sim/real_hooks4.h"   // InstallRealSimHooks4
#include "sim/command_apply3.h"// RegisterApplyHandlers3
#include "world/city.h"        // CityInitParameterTable, g_capDivisor, g_cityTotalMoney
#include "world/economy.h"     // PersonEcoView, g_goods
#include "world/economy_tick.h"// EconomyTickPriceLevel, GetSmoothedPriceLevel
#include "world/production.h"   // ProductionComputeOutputOverTime, ProdTime
#include "world/event_fire.h"   // FireEvent, FireEventBurnTick, FireEventHooks

namespace guild::play {

bool WorldSnapshot::operator==(const WorldSnapshot& o) const {
    return day == o.day && hour == o.hour && minute == o.minute &&
           priceLevel == o.priceLevel && smoothedPrice == o.smoothedPrice &&
           cityTotalMoney == o.cityTotalMoney &&
           priceDeltaSum == o.priceDeltaSum && workMinutes == o.workMinutes &&
           treasury == o.treasury && fireValue == o.fireValue;
}

namespace {

// A minimal deterministic command-sink for the fire event. The event's only
// non-rule outputs (spawn/flag/chronicle) are mocked; RandomModulo routes
// through the REAL shared CRT generator (crt::RandNext) so it stays seeded.
struct FireSink : world::FireEventHooks {
    i32 nextCmd = 1;
    i32 SpawnFire(i32) override { return nextCmd++; }
    void SetFireFlag(i32, int) override {}
    void AdvanceTime(int) override {}            // event-local clock; world clock advanced separately
    void RecordChronicle(i32, int) override {}
    int RandomModulo(int n) override {
        return n > 0 ? (crt::RandNext() % n) : 0;
    }
};

// Sum of the price deltas the economy core wrote this turn (g_goods[3..27]).
double SumPriceDeltas() {
    double s = 0.0;
    for (int g = 3; g < world::kGoodCategoryCount; ++g)
        s += world::g_goods[g].priceDelta;
    return s;
}

} // namespace

TurnDriver::TurnDriver() {}

bool TurnDriver::installRealHooks() {
    if (hooksInstalled_) return true;
    // Exactly the de-inerting sequence RealSubsystems::commandQueueInitAndSync
    // runs. Each installer binds its wave of cross-module hooks to the real
    // reconstructed targets; before this they no-op. RealCommandQueue() is
    // Init()'d on the first InstallRealSimHooks() call.
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::InstallRealSimHooks3();
    sim::InstallRealSimHooks4();
    sim::CommandQueue* q = sim::RealCommandQueue();
    if (q) sim::RegisterApplyHandlers3(*q);
    hooksInstalled_ = true;
    return true;
}

u32 TurnDriver::queueSendCount() const {
    sim::CommandQueue* q = sim::RealCommandQueue();
    return q ? q->send_count() : 0u;
}

std::vector<WorldSnapshot> TurnDriver::run(u32 seed, int turns) {
    installRealHooks();

    // --- seed the deterministic world -------------------------------------
    crt::Srand(seed);
    sim::ResetEntityArrays();
    // Seed the 28-good economy parameter table (drift/contrib/cap defaults) and
    // the cap divisor. capDivisor=0 forces the first economy tick onto the
    // "seed" branch (flt_641DAC := target) so the EMA has a defined start.
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    // Build a small per-good PersonEcoView demand population from the seeded RNG:
    // each good 1..27 gets a few "persons" with a random need and employment.
    // (This is the input EconomyComputeGoodsDemand iterates per category.)
    std::vector<std::vector<world::PersonEcoView>> persons(world::kGoodCategoryCount);
    for (int g = 1; g < world::kGoodCategoryCount; ++g) {
        int n = 2 + (crt::RandNext() % 5);   // 2..6 persons per category
        for (int p = 0; p < n; ++p) {
            world::PersonEcoView v{};
            v.need       = static_cast<u8>(crt::RandNext() % 5);     // 0..4
            v.unemployed = (crt::RandNext() & 1) != 0;
            persons[g].push_back(v);
        }
    }

    // A fire event on one building, valued from the seed (so the event evolves
    // and is reproducible). Authoritative so its body runs each tick.
    world::FireEvent fire{};
    world::FireEventInit(&fire, /*eventId=*/7, /*owner=*/42,
                         /*value=*/40000 + static_cast<i32>(crt::RandNext() % 20000));
    fire.flags |= world::kFireFlagAuthoritative;
    fire.step = 1;          // burn phase
    fire.subPhase = 3;      // do not loop step back to ignition
    FireSink sink;

    // Player treasury proxy (a real-money counter the passes mutate each turn).
    i64 treasury = 100000;

    // --- record the initial (pre-run) snapshot ----------------------------
    sim::GameTime clock{};
    clock.day = 0; clock.hour = 6; clock.minute = 0; clock.second = 0;
    initial_ = WorldSnapshot{};
    initial_.day = clock.day; initial_.hour = clock.hour; initial_.minute = clock.minute;
    initial_.smoothedPrice  = world::GetSmoothedPriceLevel();
    initial_.cityTotalMoney = world::g_cityTotalMoney;
    initial_.treasury       = treasury;
    initial_.fireValue      = fire.value;

    std::vector<WorldSnapshot> out;
    out.reserve(turns > 0 ? turns : 0);

    for (int t = 0; t < turns; ++t) {
        // Per-turn demand drift: the live game's population needs shift each
        // round, so re-roll each good's needs from the seeded RNG. This keeps the
        // economy genuinely EVOLVING (not a fixed point the EMA converges to)
        // while staying fully deterministic/reproducible (RNG rooted at Srand).
        for (int g = 1; g < world::kGoodCategoryCount; ++g)
            for (auto& v : persons[g])
                v.need = static_cast<u8>(crt::RandNext() % 5);

        // (1) Economy price tick: demand recompute + smoothed-price EMA + price
        //     deltas. Mutates g_cityTotalMoney, flt_641DAC, g_goods[].priceDelta.
        int priceLevel = world::EconomyTickPriceLevel(persons.data());

        // (2) Production integral over the day's work window — real work-minutes.
        world::ProdTime ps{clock.day, clock.hour, 0};
        world::ProdTime pe{clock.day, 22, 0};
        int workMinutes = world::ProductionComputeOutputOverTime(ps, pe, /*pause=*/false);

        // (3) Fire event burn tick — subtracts production-derived damage from the
        //     building value (real event-handler core). The daily-hour output is
        //     the production catalyst the damage formula consumes.
        world::ProdTime ds{clock.day, 6, 0};
        world::ProdTime de{clock.day, 23, 0};
        int dailyOut = world::ProductionComputeDailyHourOutput(ds, de);
        int prevFire = fire.value;
        fire.step = 1; fire.subPhase = 3;     // keep it in a single burn tick/turn
        world::FireEventBurnTick(&fire, dailyOut, /*hasCatalyst=*/false,
                                 /*catalyst=*/0, sink, /*chronicleTextId=*/1001);
        int fireLoss = prevFire - fire.value;

        // (4) Treasury: credit the production work-minutes, debit the fire loss.
        treasury += workMinutes;
        treasury -= fireLoss;

        // (5) Advance the wall clock by one game-day (real GameTimeAdvance core).
        // NB: the original's "addDays" arg is added to the HOUR-of-day total and
        // then carried into the day field (result>=24 -> ++day); so a full day is
        // +24 on that arg, leaving hour unchanged and day += 1.
        sim::GameTimeAdvance(&clock, /*addDays=*/24, /*addSeconds=*/0,
                             /*addMinutes=*/0);

        WorldSnapshot s{};
        s.day = clock.day; s.hour = clock.hour; s.minute = clock.minute;
        s.priceLevel     = priceLevel;
        s.smoothedPrice  = world::GetSmoothedPriceLevel();
        s.cityTotalMoney = world::g_cityTotalMoney;
        s.priceDeltaSum  = SumPriceDeltas();
        s.workMinutes    = workMinutes;
        s.treasury       = treasury;
        s.fireValue      = fire.value;
        out.push_back(s);
    }
    return out;
}

} // namespace guild::play
