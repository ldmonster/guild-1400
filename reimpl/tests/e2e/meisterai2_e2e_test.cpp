// End-to-end flow for the MeisterAi RuleEval continuation: the Guild-Master AI
// evaluates a building's stock/setting rules in priority order, picks the first
// rule that fires, and emits the resulting (settingId, newValue) command. This
// mirrors how VIBE_MeisterAi_ProcessBuildingNeeds walks the rule table and queues
// the first applicable adjustment for a building each turn.
#include "ai/meisterai2.h"

#include <vector>

#include "tests/framework/test.h"

using namespace guild::ai;

namespace {

// --- a small simulated city/law environment ---------------------------------
struct World {
    int group = 0;
    int cityCount = 100;
    Rule2Law laws[26];   // indexed by law id; the rules read 6,7,10,11,12,13,15
    int rngValue = 0;
};

World* g_world = nullptr;

int GroupOf(int) { return g_world->group; }
int CityCount() { return g_world->cityCount; }
Rule2Law LawOf(int id) { return g_world->laws[id]; }
int FixedRng(int) { return g_world->rngValue; }

RuleEnv2 BindEnv() {
    RuleEnv2 e;
    e.group_from_code = &GroupOf;
    e.law_record = &LawOf;
    e.city_count = &CityCount;
    e.rng_mod = &FixedRng;
    return e;
}

// An emitted "adjust setting" command (settingId, newValue) the AI queues.
struct Command { int settingId; int value; };

// The director: try each rule in priority order; queue the first that fires.
// Returns the number of commands emitted (0 or 1, like the original per-building
// pass which applies one setting change at a time).
int RunBuildingDecision(const Rule2Building& b, const RuleEnv2& env,
                        std::vector<Command>& out) {
    int v = 0;
    if (RuleEvalSeasonalStock(b, env, &v))      { out.push_back({10, v}); return 1; }
    if (RuleEvalRangeLow(b, env, &v))           { out.push_back({11, v}); return 1; }
    if (RuleEvalInterpolatedTarget(b, env, &v)) { out.push_back({12, v}); return 1; }
    if (RuleEvalRangeHigh(b, env, &v))          { out.push_back({13, v}); return 1; }
    if (RuleEvalCostBenefit(b, env, &v))        { out.push_back({15, v}); return 1; }
    if (RuleEvalAdjustSetting(b, env, &v))      { out.push_back({7,  v}); return 1; }
    if (RuleEvalIncreaseSetting(b, env, &v))    { out.push_back({6,  v}); return 1; }
    return 0;
}

} // namespace

TEST(MeisterAi2E2E, SeasonalStockWins) {
    World w;
    g_world = &w;
    RuleEnv2 env = BindEnv();

    // Tavern (group 7): SeasonalStock is highest priority and fires first.
    w.group = 7;
    w.laws[10].low = 0; w.laws[10].high = 100; w.laws[10].current = 0;
    Rule2Building b;
    b.f43 = 2.5f;          // target = trunc((2.5-2)*100) = 50 > current 0

    std::vector<Command> cmds;
    CHECK_EQ(RunBuildingDecision(b, env, cmds), 1);
    CHECK_EQ((int)cmds.size(), 1);
    CHECK_EQ(cmds[0].settingId, 10);
    CHECK_EQ(cmds[0].value, 50);
    g_world = nullptr;
}

TEST(MeisterAi2E2E, FallsThroughToRangeLow) {
    World w;
    g_world = &w;
    RuleEnv2 env = BindEnv();

    // Non-tavern, non-special group: seasonal does not fire (target == current),
    // so the decision falls through to RangeLow.
    w.group = 0;
    // SeasonalStock: target = trunc((3-2.5)*100)=50; not < current 50 -> no fire.
    w.laws[10].low = 0; w.laws[10].high = 100; w.laws[10].current = 50;
    // RangeLow default: target = trunc(10*0.66 + 10)=16; current 15 < 16 -> fires,
    // value = high - rng = 20 - 0 = 20.
    w.laws[11].low = 10; w.laws[11].high = 20; w.laws[11].current = 15;
    w.rngValue = 0;

    Rule2Building b;
    b.f43 = 2.5f;
    std::vector<Command> cmds;
    CHECK_EQ(RunBuildingDecision(b, env, cmds), 1);
    CHECK_EQ(cmds[0].settingId, 11);
    CHECK_EQ(cmds[0].value, 20);
    g_world = nullptr;
}

TEST(MeisterAi2E2E, CityGateBlocksRangeRules) {
    World w;
    g_world = &w;
    RuleEnv2 env = BindEnv();

    // City below the count gate: the range rules cannot fire; only the ungated
    // setting rules (Adjust/Increase) remain.
    w.group = 0;
    w.cityCount = 2;                                    // < 4 -> range rules blocked
    w.laws[10].low = 0; w.laws[10].high = 100; w.laws[10].current = 100; // seasonal no-fire (50<100 actually fires)
    // Make seasonal NOT fire: default branch fires when target < current; set
    // current low so 50 is not < it.
    w.laws[10].current = 10;                            // 50 < 10 false -> no fire
    w.laws[12].low = 0; w.laws[12].high = 100; w.laws[12].current = 30; // interp target ~25, |25-30|=5>3 -> fires? guard below
    // Drive interpolation to within +-3 so it does not fire either.
    w.laws[12].current = 25;                            // target 25 -> |0|<=3 no fire
    // AdjustSetting (law 7): target = trunc((2.5-2)*5)=2 (clamped [1,4]); current 0 -> fires.
    w.laws[7].current = 0;

    Rule2Building b;
    b.f43 = 2.5f; b.f7 = 350.0f;
    std::vector<Command> cmds;
    CHECK_EQ(RunBuildingDecision(b, env, cmds), 1);
    CHECK_EQ(cmds[0].settingId, 7);
    CHECK_EQ(cmds[0].value, 2);
    g_world = nullptr;
}

TEST(MeisterAi2E2E, NothingToDo) {
    World w;
    g_world = &w;
    RuleEnv2 env = BindEnv();

    // Everything already at its target: no rule fires.
    w.group = 0;
    w.cityCount = 2;                       // range rules blocked
    w.laws[10].low = 0; w.laws[10].high = 100; w.laws[10].current = 10; // seasonal no-fire
    w.laws[12].low = 0; w.laws[12].high = 100; w.laws[12].current = 25; // interp no-fire
    // CostBenefit: score 0 < 0.5, enable 0, base !> high -> no fire.
    w.laws[15].enable = 0;
    // Adjust/Increase: target 2 == current 2 -> no fire.
    w.laws[7].current = 2;
    w.laws[6].current = 2;

    Rule2Building b;
    b.f43 = 2.5f; b.f7 = 350.0f;
    std::vector<Command> cmds;
    CHECK_EQ(RunBuildingDecision(b, env, cmds), 0);
    CHECK_EQ((int)cmds.size(), 0);
    g_world = nullptr;
}
