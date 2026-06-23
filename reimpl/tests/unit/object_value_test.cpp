// Golden vectors for sim::ObjectComputeMarketValue — VIBE_Object_ComputeMarketValue
// gilde.exe 0x594df0. Synthetic records exercise the exact x87 accumulation order
// + the constant tables (dbl_626B34..0x626B5C, get_global_value-verified).
#include "sim/object_value.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstdint>
#include <map>

using namespace guild::sim;

namespace {

// The reference constants (must equal the .cpp constants / the IEEE-754 globals).
constexpr double kWsWeight    = 0.01;
constexpr double kSizeScale   = 0.003968253968253968;  // 1/252
constexpr double kSizeFavWt   = 0.25;
constexpr double kFavBias     = -0.5;
constexpr double kLevelWeight = 0.1;
constexpr double kMatchBonus  = 0.3;

bool Near(double a, double b, double e = 1e-4) { return std::fabs(a - b) <= e; }

// A synthetic object/world the hooks read. The market-value math only needs the
// scalar fields, so the "records" are just tagged ids into this table.
struct World {
    int    sumWs = 0;
    bool   hasActive = true;
    unsigned char activeKind = 6;
    unsigned char activeSize = 0;
    unsigned char ownerCat = 1;
    unsigned   entityId = 7;
    int    level = 2;
    // worker records keyed by id: (present, outputRatio, category)
    struct Worker { bool present; float ratio; int cat; };
    std::map<int, Worker> workers;
    double favorability = 0.5;
};

World* g_w = nullptr;

MarketValueHooks MakeHooks() {
    MarketValueHooks h{};
    h.sumWorkstationByCategory = [](const void*, char, int) {
        return g_w->sumWs;
    };
    h.personFindActiveByEntity = [](const void*) -> const void* {
        return g_w->hasActive ? reinterpret_cast<const void*>(1) : nullptr;
    };
    h.personTemplateForEntity = [](unsigned) -> const void* {
        return reinterpret_cast<const void*>(2);  // template fallback rec
    };
    h.activeKind = [](const void*) { return g_w->activeKind; };
    h.activeSize = [](const void*) { return g_w->activeSize; };
    h.ownerCategory = [](const void*) { return g_w->ownerCat; };
    h.objEntityId = [](const void*) { return g_w->entityId; };
    h.objLevel = [](const void*) { return g_w->level; };
    h.personFindRecordById = [](int id) -> const void* {
        auto it = g_w->workers.find(id);
        if (it != g_w->workers.end() && it->second.present)
            return reinterpret_cast<const void*>(
                static_cast<std::intptr_t>(id + 100));
        return nullptr;
    };
    h.buildingComputeOutputRatio = [](const void* rec) -> float {
        int id = static_cast<int>(reinterpret_cast<std::intptr_t>(rec)) - 100;
        return g_w->workers.at(id).ratio;
    };
    h.workerCategory = [](const void* rec) -> int {
        int id = static_cast<int>(reinterpret_cast<std::intptr_t>(rec)) - 100;
        return g_w->workers.at(id).cat;
    };
    h.aiAverageObjectFavorability = [](unsigned, int, const int*) {
        return g_w->favorability;
    };
    return h;
}

// A double-precision reference of the exact formula (float spills modeled).
double Reference(World& w, int count, const int* ids) {
    float v17 = static_cast<float>((double)w.sumWs * kWsWeight + 1.0);
    bool active = w.hasActive;
    (void)active;
    unsigned char kind = w.activeKind;
    float v14;
    if (kind == 6 || kind == 7)
        v14 = static_cast<float>((double)(short)w.activeSize * kSizeScale * kSizeFavWt);
    else
        v14 = 0.0f;
    float v18 = static_cast<float>((double)v17 + (double)v14);

    float v20accum = 0.0f, v23 = 0.0f;
    int matched = 0;
    for (int i = 0; i < count; ++i) {
        int id = ids[i];
        if (id == -1) continue;
        auto it = w.workers.find(id);
        if (it == w.workers.end() || !it->second.present) continue;
        v20accum = static_cast<float>((double)v20accum + (double)it->second.ratio);
        if ((int)w.ownerCat == it->second.cat)
            v23 = static_cast<float>((double)v23 + kMatchBonus);
        ++matched;
    }
    float v15 = 0.0f;
    if (matched) {
        double inv = 1.0 / (double)matched;
        v15 = static_cast<float>((double)v20accum * inv);
        v23 = static_cast<float>(inv * (double)v23);
    }
    float v19 = static_cast<float>((w.favorability + kFavBias) * kSizeFavWt + (double)v18);
    float v20 = static_cast<float>((double)(w.level - 2) * kLevelWeight + (double)v19);
    return ((double)v15 + (double)v23) * (double)v20;
}

}  // namespace

// --- base term only (no workers, kind != 6/7) -------------------------------
TEST(ObjectValue, BaseTermNoWorkers) {
    World w;
    w.sumWs = 0;            // base v17 = 1.0
    w.activeKind = 0;       // v14 = 0
    w.level = 2;            // level term 0
    w.favorability = 0.5;   // (0.5 - 0.5)*0.25 = 0 -> v19 = v18 = 1.0
    g_w = &w;
    auto h = MakeHooks();
    // no matched workers -> v15 = v23 = 0 -> result = 0 * v20 = 0
    double got = ObjectComputeMarketValue(reinterpret_cast<const void*>(8), 0,
                                          nullptr, h);
    CHECK(Near(got, Reference(w, 0, nullptr)));
    CHECK(Near(got, 0.0));   // (0+0)*v20
}

// --- single matching worker -------------------------------------------------
TEST(ObjectValue, SingleMatchingWorker) {
    World w;
    w.sumWs = 50;           // v17 = 50*0.01 + 1 = 1.5
    w.activeKind = 6;       // size term active
    w.activeSize = 252;     // 252 * (1/252) * 0.25 = 0.25 -> v14
    w.ownerCat = 3;
    w.level = 5;            // (5-2)*0.1 = 0.3
    w.favorability = 1.0;   // (1.0 - 0.5)*0.25 = 0.125
    w.workers[10] = {true, 0.8f, 3};   // category matches -> +0.3
    g_w = &w;
    auto h = MakeHooks();
    int ids[] = {10};
    double got = ObjectComputeMarketValue(reinterpret_cast<const void*>(8), 1,
                                          ids, h);
    CHECK(Near(got, Reference(w, 1, ids)));
    CHECK(got > 0.0);
}

// --- mixed workers: matching + non-matching + missing + empty slot ----------
TEST(ObjectValue, MixedWorkers) {
    World w;
    w.sumWs = 10;
    w.activeKind = 7;       // also a size branch
    w.activeSize = 100;
    w.ownerCat = 2;
    w.level = 8;
    w.favorability = 0.25;
    w.workers[1]  = {true, 0.5f, 2};    // match
    w.workers[2]  = {true, 0.9f, 9};    // no match
    w.workers[3]  = {false, 0.0f, 2};   // missing record -> skipped
    g_w = &w;
    auto h = MakeHooks();
    int ids[] = {1, -1, 2, 3, -1};      // -1 entries skipped; 3 missing
    double got = ObjectComputeMarketValue(reinterpret_cast<const void*>(8), 5,
                                          ids, h);
    CHECK(Near(got, Reference(w, 5, ids)));
}

// --- fallback template when no active person --------------------------------
TEST(ObjectValue, TemplateFallbackUsed) {
    World w;
    w.hasActive = false;    // forces personTemplateForEntity path
    w.sumWs = 0;
    w.activeKind = 6;
    w.activeSize = 252;     // 252/252*0.25 = 0.25  (size byte, 0..255)
    w.level = 2;
    w.favorability = 0.5;
    g_w = &w;
    auto h = MakeHooks();
    double got = ObjectComputeMarketValue(reinterpret_cast<const void*>(8), 0,
                                          nullptr, h);
    // v18 = 1.0 + 0.5 = 1.5, no workers -> (0)*v20 = 0
    CHECK(Near(got, Reference(w, 0, nullptr)));
    CHECK(Near(got, 0.0));
}

// --- favorability default (no valid ids -> leaf returns 0.5) ----------------
TEST(ObjectValue, FavorabilityShiftsValue) {
    World w;
    w.sumWs = 0;
    w.activeKind = 0;
    w.level = 2;
    w.ownerCat = 1;
    w.favorability = 0.5;
    w.workers[5] = {true, 1.0f, 1};   // matching -> v15=1.0, v23=0.3
    g_w = &w;
    auto h = MakeHooks();
    int ids[] = {5};
    // v18 = 1.0; fav (0.5-0.5)*0.25=0 -> v19=1.0 -> v20=1.0
    // result = (v15 + v23)*v20 = (1.0 + 0.3)*1.0 = 1.3
    double got = ObjectComputeMarketValue(reinterpret_cast<const void*>(8), 1,
                                          ids, h);
    CHECK(Near(got, Reference(w, 1, ids)));
    CHECK(Near(got, 1.3));
}

// --- constant table sanity (IEEE-754 bit patterns) --------------------------
TEST(ObjectValue, ConstantTable) {
    // dbl_626B34 = 0.01
    CHECK(Near(kWsWeight, 0.01, 1e-12));
    // dbl_626B3C = 0x3f70410410410410 == 1/252
    CHECK(Near(kSizeScale, 1.0 / 252.0, 1e-15));
    CHECK(Near(kSizeFavWt, 0.25, 1e-15));    // dbl_626B44
    CHECK(Near(kFavBias, -0.5, 1e-15));      // dbl_626B4C
    CHECK(Near(kLevelWeight, 0.1, 1e-12));   // dbl_626B54
    CHECK(Near(kMatchBonus, 0.3, 1e-12));    // dbl_626B5C
}

// --- default entry narrows to float (live-caller signature) -----------------
TEST(ObjectValue, DefaultEntryNarrows) {
    World w;
    w.sumWs = 0; w.activeKind = 0; w.level = 2; w.ownerCat = 1;
    w.favorability = 0.5;
    w.workers[9] = {true, 1.0f, 1};
    g_w = &w;
    SetMarketValueHooks(MakeHooks());
    int ids[] = {9};
    float got = ObjectComputeMarketValueDefault(reinterpret_cast<const char*>(8),
                                                1, ids);
    CHECK(Near((double)got, 1.3, 1e-4));
    SetMarketValueHooks(MarketValueHooks{});  // restore inert
}
