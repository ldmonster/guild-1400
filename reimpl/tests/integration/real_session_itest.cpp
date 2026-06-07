// tests/integration/real_session_itest.cpp — integration: a small real-FORMAT world
// (seeded live arrays + the real per-table io serializers) driven through the M3
// path WITHOUT real assets: capture -> run real economy turns -> save -> reload ->
// CompareSessions equal AND HashFullWorld equal. Also proves a post-turn save reloads
// to the post-turn world (sim state persisted, not a static template).
#include "test.h"

#include "play/real_session.h"
#include "play/session_flow.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "io/save.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "crt/rand.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

class RwFile : public shim::IFile {
public:
    RwFile(std::vector<u8>* store, bool writing) : store_(store), writing_(writing) {
        if (writing_) store_->clear();
    }
    std::size_t read(void* dst, std::size_t n) override {
        if (writing_) return 0;
        std::size_t avail = store_->size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, store_->data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (!writing_) return 0;
        const u8* p = static_cast<const u8*>(src);
        store_->insert(store_->end(), p, p + n);
        pos_ += n; return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)store_->size() : 0;
        std::int64_t t = base + off; if (t < 0) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)store_->size(); }
private:
    std::vector<u8>* store_; bool writing_; std::size_t pos_ = 0;
};

class RwFs : public shim::IFileSystem {
public:
    shim::IFile* open(const char* path, const char* mode) override {
        bool writing = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'));
        if (!writing && files_.find(path) == files_.end()) return nullptr;
        return new RwFile(&files_[path], writing);
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
    bool has(const std::string& p) const { return files_.count(p) != 0; }
    const std::vector<u8>& blob(const std::string& p) { return files_[p]; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

} // namespace

// ---------------------------------------------------------------------------
// A seeded world driven through REAL economy turns then round-tripped through the
// real save format: CompareSessions equal AND the full-world hash matches.
// ---------------------------------------------------------------------------
TEST(RealSessionIntegration, EconomyTurnsThenRoundTrip) {
    RwFs fs; io::VfsInit(&fs, false);

    // seed a small live world.
    SessionConfig cfg; cfg.seed = 0x2024CAFE; cfg.persons = 12; cfg.objects = 7;
    SessionWorld seeded; NewGame(cfg, seeded);

    // run a handful of REAL per-day economy turns over the live world (mutates the
    // economy globals + consumes the seeded RNG; entity records unchanged).
    crt::Srand(0x5151);
    EconomyTurnState st = SeedEconomyTurnState();
    const int K = 5;
    for (int day = 0; day < K; ++day) { st.day = day; RunEconomyTurn(st); }

    // capture the post-turn live world + its full-world hash, then save.
    SessionWorld saved;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, cfg.cityName, saved);
    std::uint64_t hashSaved = HashFullWorld();

    CHECK(SaveSession(saved, "city.SAV"));
    CHECK(fs.has("city.SAV"));

    // reload and compare.
    SessionWorld reloaded;
    CHECK(LoadSession("city.SAV", saved.personCount, saved.objectCount, reloaded));
    std::uint64_t hashReloaded = HashFullWorld();

    EquivResult eq = CompareSessions(saved, reloaded);
    CHECK(eq.allEqual());
    CHECK_EQ(hashSaved, hashReloaded);

    std::printf("[real_session][itest] persons=%u objects=%u turns=%d "
                "hashSaved=%llu hashReloaded=%llu equal=%d\n",
                saved.personCount, saved.objectCount, K,
                (unsigned long long)hashSaved, (unsigned long long)hashReloaded,
                (int)eq.allEqual());

    io::VfsShutdown();
}

// ---------------------------------------------------------------------------
// A post-turn save reloads to the POST-turn world, and a pre-turn save reloads to a
// DIFFERENT (pre-turn) world — proving simulated state is genuinely persisted.
// ---------------------------------------------------------------------------
TEST(RealSessionIntegration, PostTurnSavePersistsSimState) {
    RwFs fs; io::VfsInit(&fs, false);

    SessionConfig cfg; cfg.seed = 7; cfg.persons = 10; cfg.objects = 5;
    SessionWorld seeded; NewGame(cfg, seeded);

    // pre-turn snapshot + save.
    SessionWorld pre;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, cfg.cityName, pre);
    CHECK(SaveSession(pre, "pre.SAV"));
    std::vector<u8> preBlob = fs.blob("pre.SAV");

    // run turns that visibly mutate the person table (RunTurns advances cash/bits).
    int muts = RunTurns(seeded, 4);
    CHECK_EQ(muts, 10 * 4);

    // post-turn snapshot + save.
    SessionWorld post;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, cfg.cityName, post);
    CHECK(SaveSession(post, "post.SAV"));
    std::vector<u8> postBlob = fs.blob("post.SAV");

    // same shape, different bytes.
    CHECK_EQ(preBlob.size(), postBlob.size());
    CHECK(preBlob != postBlob);

    // the post save reloads to the post world.
    SessionWorld rePost;
    CHECK(LoadSession("post.SAV", post.personCount, post.objectCount, rePost));
    CHECK(CompareSessions(post, rePost).allEqual());

    // the pre save reloads to a world that differs from the post world.
    SessionWorld rePre;
    CHECK(LoadSession("pre.SAV", pre.personCount, pre.objectCount, rePre));
    EquivResult diff = CompareSessions(rePre, rePost);
    CHECK(!diff.personsEqual);   // person table changed across the turns

    io::VfsShutdown();
}
