// GUARDED end-to-end: load the REAL shipped AUGSBURG.cty city into the LIVE entity
// arrays through the full world-load driver, then run K REAL AI/NPC turns over it
// and assert the world state EVOLVES and is DETERMINISTIC (identical across reruns)
// via play::HashWorldState(). Skipped (trivial pass) when the original assets are
// absent. Honors GUILD_GAME_DIR.
#include "test.h"

#include "play/turn_ai.h"
#include "play/determinism.h"
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

// Locate the real asset root (env override or known checkout locations).
const char* FindAssetRoot() {
    static std::string root;
    auto tryRoot = [](const std::string& r) -> bool {
        std::ifstream f(r + "/Resources/gamedata/Cities/AUGSBURG.cty", std::ios::binary);
        if (f) { return true; }
        return false;
    };
    if (const char* env = std::getenv("GUILD_GAME_DIR")) {
        if (tryRoot(env)) { root = env; return root.c_str(); }
    }
    const char* cands[] = {
        "europe_guild_1400_original",
        "reimpl/europe_guild_1400_original",
        "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original",
    };
    for (const char* c : cands)
        if (tryRoot(c)) { root = c; return root.c_str(); }
    return nullptr;
}

// Count live persons/objects in the live arrays.
void CountLive(int& persons, int& objects) {
    persons = objects = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++persons;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++objects;
}

// Seed a handful of synthetic AI workers ON TOP of the loaded real city so the AI
// turn has live actors to drive (the shipped AUGSBURG seed carries only 1 person).
// Additive: it does not disturb the real records the loader scattered.
void SeedWorkersOnLoadedCity(std::uint32_t seed) {
    crt::Srand(seed);
    int placed = 0;
    for (int i = 0; i < sim::kPersonCapacity && placed < 12; ++i) {
        if (sim::g_persons[i].marker != -1) continue;   // keep real records
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

TEST(TurnAiE2E, AugsburgRealCityRunsAiTurnsEvolvesAndDeterministic) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent -> skip cleanly
    std::printf("[turn_ai_e2e] asset root: %s\n", root);

    const int K = 5;

    // ---- run A: load AUGSBURG, seed workers, run K AI turns ----
    auto loadAndRun = [&](std::vector<std::uint64_t>& hashes,
                          std::vector<play::AiTurnEffects>& fx,
                          int& livePersons, int& liveObjects) -> bool {
        DiskFs fs(root);
        io::VfsInit(&fs, /*caseInsensitive=*/false);
        sim::ResetEntityArrays();
        sim::ResetBuildingPersons();
        io::WorldState world{};
        bool ok = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
        CountLive(livePersons, liveObjects);
        SeedWorkersOnLoadedCity(/*seed=*/31337);
        hashes.clear(); fx.clear();
        for (int t = 0; t < K; ++t) {
            fx.push_back(play::RunAiTurn((std::uint32_t)(200 + t)));
            hashes.push_back(play::HashWorldState());
        }
        io::VfsShutdown();
        return ok;
    };

    std::vector<std::uint64_t> hA, hB;
    std::vector<play::AiTurnEffects> fA, fB;
    int personsA = 0, objectsA = 0, personsB = 0, objectsB = 0;
    bool okA = loadAndRun(hA, fA, personsA, objectsA);
    bool okB = loadAndRun(hB, fB, personsB, objectsB);

    std::printf("[turn_ai_e2e] LoadWorld ok=%d  live persons(real)=%d objects=%d\n",
                okA ? 1 : 0, personsA, objectsA);
    for (int t = 0; t < (int)fA.size(); ++t) {
        std::printf("[turn_ai_e2e] turn %d: passes=%d aiPasses=%d factions=%d "
                    "workers=%d moodDeltas=%d confront=%d decay=%d groups=%d "
                    "events=%d npcFlags=%d hash=%016llx\n",
                    t, fA[t].passesRun, fA[t].aiPassesRun, fA[t].factionsProcessed,
                    fA[t].workersEvaluated, fA[t].moodDeltasApplied,
                    fA[t].confrontationsSpawned, fA[t].moodDecaysApplied,
                    fA[t].groupBroadcasts, fA[t].eventsTicked, fA[t].npcFlagsCleared,
                    (unsigned long long)hA[t]);
    }

    // The real city loaded and scattered real records into the live arrays.
    CHECK(okA);
    CHECK(personsA >= 1);     // AUGSBURG seed carries >=1 person record
    CHECK(objectsA >= 1);     // and live building/object records

    // The AI cascade ran every turn and did real work over the live world.
    CHECK(fA.size() == (std::size_t)K);
    CHECK(fA[0].passesRun > 0);
    CHECK(fA[0].aiPassesRun > 0);
    CHECK(fA[0].workersEvaluated >= 1);
    CHECK(fA[0].moodDeltasApplied >= 1);
    CHECK(fA[0].groupBroadcasts >= 1);

    // Evolution: the world digest changed over the K turns.
    bool evolved = false;
    for (std::size_t i = 1; i < hA.size(); ++i)
        if (hA[i] != hA[i - 1]) evolved = true;
    CHECK(evolved);

    // Determinism: re-load + re-run identically -> byte-identical per-turn digests
    // and identical effect tallies.
    CHECK_EQ(personsA, personsB);
    CHECK_EQ(objectsA, objectsB);
    CHECK(hA.size() == hB.size());
    bool sameHashes = (hA.size() == hB.size());
    for (std::size_t i = 0; i < hA.size() && sameHashes; ++i)
        sameHashes = sameHashes && (hA[i] == hB[i]);
    CHECK(sameHashes);

    bool sameFx = (fA.size() == fB.size());
    for (std::size_t i = 0; i < fA.size() && sameFx; ++i)
        sameFx = sameFx && (fA[i] == fB[i]);
    CHECK(sameFx);
}
