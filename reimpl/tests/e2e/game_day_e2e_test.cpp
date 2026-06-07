// GUARDED end-to-end: load the REAL shipped AUGSBURG.cty into the LIVE entity
// arrays through the full world-load driver, then run 10 FULL GAME DAYS
// (RunGameDays) over it and assert the WHOLE world evolves and is deterministic:
//   * the clock advances exactly +10 days,
//   * treasury / prices / AI mood all EVOLVE over the run,
//   * the full-world hash (HashFullWorld) is BYTE-IDENTICAL on a full reload+rerun.
// Skipped (trivial pass) when the original assets are absent. Honors GUILD_GAME_DIR.
#include "test.h"

#include "play/game_day.h"
#include "play/world_digest.h"   // HashFullWorld
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"
#include "shim/IFileSystem.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;

namespace {

class DiskFile : public shim::IFile {
public:
    explicit DiskFile(std::vector<u8> data) : data_(std::move(data)) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_.size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_.size() : 0;
        std::int64_t t = base + off; if (t < 0) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_.size(); }
private:
    std::vector<u8> data_;
    std::size_t pos_ = 0;
};

class DiskFs : public shim::IFileSystem {
public:
    explicit DiskFs(std::string root) : root_(std::move(root)) {}
    shim::IFile* open(const char* path, const char* mode) override {
        if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'))) return nullptr;
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new DiskFile(std::move(data));
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        std::ifstream f(full(path), std::ios::binary); return (bool)f;
    }
private:
    std::string full(const char* path) const {
        std::string p = path;
        for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

const char* FindAssetRoot() {
    static std::string root;
    auto tryRoot = [](const std::string& r) -> bool {
        std::ifstream f(r + "/Resources/gamedata/Cities/AUGSBURG.cty", std::ios::binary);
        return (bool)f;
    };
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        if (tryRoot(env)) { root = env; return root.c_str(); }
    const char* cands[] = {
        "europe_guild_1400_original",
        "reimpl/europe_guild_1400_original",
        "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original",
    };
    for (const char* c : cands)
        if (tryRoot(c)) { root = c; return root.c_str(); }
    return nullptr;
}

void CountLive(int& persons, int& objects) {
    persons = objects = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++persons;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++objects;
}

// Seed synthetic AI workers ON TOP of the loaded real city (additive — keeps the
// real records the loader scattered) so the AI sub-turn has live actors to drive.
void SeedWorkersOnLoadedCity(std::uint32_t seed) {
    crt::Srand(seed);
    int placed = 0;
    for (int i = 0; i < sim::kPersonCapacity && placed < 12; ++i) {
        if (sim::g_persons[i].marker != -1) continue;
        sim::Person& p = sim::g_persons[i];
        p.marker = 4; p.isPlayer = 1; p.id = 5000 + placed; p.ownerPlayer = 0;
        u8 kinds[] = {5, 3, 6, 5, 3, 5, 6, 5, 3, 5, 6, 5};
        p.kind = kinds[placed % 12];
        sim::PersonSetByte(&p, 61, (u8)(70 + (crt::RandNext() % 60)));
        sim::PersonSetByte(&p, 65, (u8)(70 + (crt::RandNext() % 60)));
        sim::PersonSetByte(&p, sim::kPfReputation, (u8)(crt::RandNext() % 40));
        sim::PersonSetDword(&p, sim::kPfTurnBits, (i32)0xFFFFFFFFu);
        ++placed;
    }
}

} // namespace

TEST(GameDayE2E, AugsburgFullDaysEvolveAndAreDeterministic) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent -> clean skip
    std::printf("[game_day_e2e] asset root: %s\n", root);

    const int K = 10;
    const std::uint32_t kSeed = 4242;
    const std::uint32_t kWorkerSeed = 31337;

    // Load AUGSBURG, seed workers, run K full game days; capture per-day deltas +
    // the final full-world hash. The economy/clock state lives in GameDayState; the
    // AI half mutates the live arrays in place.
    auto loadAndRun = [&](std::vector<play::GameDayDeltas>& days,
                          int& livePersons, int& liveObjects,
                          std::uint64_t* h0, std::uint64_t* hFinal) -> bool {
        DiskFs fs(root);
        io::VfsInit(&fs, /*caseInsensitive=*/false);
        sim::ResetEntityArrays();
        sim::ResetBuildingPersons();
        io::WorldState world{};
        bool ok = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
        CountLive(livePersons, liveObjects);
        SeedWorkersOnLoadedCity(kWorkerSeed);

        crt::Srand(kSeed);
        play::GameDayState st = play::SeedGameDay(kSeed);
        if (h0) *h0 = play::HashFullWorld();
        days = play::RunGameDays(kSeed, K, st);
        if (hFinal) *hFinal = play::HashFullWorld();
        io::VfsShutdown();
        return ok;
    };

    std::vector<play::GameDayDeltas> dA, dB;
    int personsA = 0, objectsA = 0, personsB = 0, objectsB = 0;
    std::uint64_t h0A = 0, hFinalA = 0, h0B = 0, hFinalB = 0;
    bool okA = loadAndRun(dA, personsA, objectsA, &h0A, &hFinalA);
    bool okB = loadAndRun(dB, personsB, objectsB, &h0B, &hFinalB);

    std::printf("[game_day_e2e] LoadWorld ok=%d  live persons(real)=%d objects=%d\n",
                okA ? 1 : 0, personsA, objectsA);
    std::printf("[game_day_e2e] full-world hash %016llx -> %016llx over %d days\n",
                (unsigned long long)h0A, (unsigned long long)hFinalA, K);
    for (int t = 0; t < (int)dA.size(); ++t) {
        const auto& d = dA[t];
        std::printf("[game_day_e2e] day %2d: clock %d->%d  treasury=%lld price=%d "
                    "events=%d chron=%d | ai workers=%d mood=%d confront=%d decay=%d "
                    "groups=%d | hash=%016llx\n",
                    t, d.events.dayBefore, d.events.dayAfter,
                    (long long)d.economy.treasuryAfter, d.economy.priceLevel,
                    d.events.eventsFiredThisTurn, d.events.chronicleAddedThisTurn,
                    d.ai.workersEvaluated, d.ai.moodDeltasApplied,
                    d.ai.confrontationsSpawned, d.ai.moodDecaysApplied,
                    d.ai.groupBroadcasts, (unsigned long long)d.hashAfter);
    }

    // The real city loaded real records into the live arrays.
    CHECK(okA);
    CHECK(personsA >= 1);
    CHECK(objectsA >= 1);

    // The composition ran K full days.
    CHECK_EQ((int)dA.size(), K);
    if ((int)dA.size() != K) return;

    // CLOCK advanced exactly +K days.
    CHECK_EQ(dA.front().dayBefore, 0);
    CHECK_EQ(dA.back().dayAfter, K);
    for (int t = 0; t < K; ++t)
        CHECK_EQ(dA[t].dayAfter - dA[t].dayBefore, 1);

    // ECONOMY evolved: treasury grew and the price level is live, every day ran
    // all six Amt passes.
    CHECK(dA.back().economy.treasuryAfter > dA.front().economy.treasuryBefore);
    CHECK(dA.back().economy.priceLevel > 0);
    for (auto& d : dA) CHECK_EQ(d.economy.passesRun, 6);
    // prices move across the run.
    int priceMoves = 0;
    for (int t = 1; t < K; ++t)
        if (dA[t].economy.priceAfter != dA[t - 1].economy.priceAfter) ++priceMoves;
    CHECK(priceMoves >= 1);

    // AI mood evolved: the mood/relation director moved live records, and events
    // fired over the run.
    int moodTotal = 0, firedTotal = 0;
    for (auto& d : dA) { moodTotal += d.ai.moodDeltasApplied;
                         firedTotal += d.events.eventsFiredThisTurn; }
    CHECK(moodTotal >= 1);
    CHECK(firedTotal >= 1);

    // The FULL-WORLD hash evolved over the run.
    CHECK(h0A != 0u);
    CHECK(hFinalA != h0A);
    int hashChanges = 0;
    for (int t = 1; t < K; ++t)
        if (dA[t].hashAfter != dA[t - 1].hashAfter) ++hashChanges;
    CHECK(hashChanges >= K - 2);

    // FULL DETERMINISM: a complete reload + rerun produced byte-identical hashes
    // for the SIMULATED state — every per-day digest and the final full-world
    // digest match across the two independent loads (asserted below + via hFinalA).
    CHECK_EQ(personsA, personsB);
    CHECK_EQ(objectsA, objectsB);
    // NOTE: the PRE-run baseline (h0) is NOT cross-reload stable: io::LoadWorld
    // leaves a freshly-allocated plant-map heap pointer (kind-30 object +113, a
    // runtime artifact — see the real_session round-trip notes) in g_objects, which
    // HashFullWorld folds. Its ADDRESS differs per load. The first game day
    // normalizes it, so from day 0 onward the per-day digests and the final digest
    // ARE byte-identical across reloads (the meaningful determinism witness).
    CHECK(h0A != 0u);
    CHECK(h0B != 0u);
    CHECK_EQ(hFinalA, hFinalB);
    bool sameDaily = (dA.size() == dB.size());
    for (size_t i = 0; i < dA.size() && i < dB.size(); ++i) {
        if (dA[i].hashAfter != dB[i].hashAfter) sameDaily = false;
        if (dA[i].economy.treasuryAfter != dB[i].economy.treasuryAfter) sameDaily = false;
        if (dA[i].ai.relationDeltaSum != dB[i].ai.relationDeltaSum) sameDaily = false;
    }
    CHECK(sameDaily);
}
