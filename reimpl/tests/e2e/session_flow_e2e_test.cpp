// tests/e2e/session_flow_e2e_test.cpp — end-to-end session/save flow:
//   new-game seed -> save -> reload -> assert round-trip equivalence (table/record
//   counts + key fields), and prove a save taken AFTER simulating turns differs from
//   the pre-sim save (sim state is genuinely persisted). Plus a GUARDED bonus that
//   loads the real shipped AUGSBURG.cty through the full world load driver.
#include "test.h"

#include "play/session_flow.h"
#include "io/save.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim/IFileSystem.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// --- a growable in-memory VFS (write persists, re-openable for read) --------
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
    std::size_t bytesOf(const std::string& p) { return files_[p].size(); }
    const std::vector<u8>& blob(const std::string& p) { return files_[p]; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// --- a passthrough disk FS for the guarded real-asset bonus -----------------
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
    std::vector<u8> data_; std::size_t pos_ = 0;
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
        std::string p = path; for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

const char* FindAssetRoot() {
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    static std::string root;
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) { std::string s = c; auto pos = s.find("/Resources/");
                 root = s.substr(0, pos); return root.c_str(); }
    }
    return nullptr;
}

} // namespace

// ===========================================================================
// e2e: build/seed a world -> save -> reload -> assert round-trip equivalence.
// ===========================================================================
TEST(SessionFlowE2E, NewGameSaveReloadRoundTrip) {
    RwFs fs; io::VfsInit(&fs, false);

    SessionConfig cfg; cfg.seed = 0x1234ABCDu; cfg.persons = 20; cfg.objects = 10;
    cfg.cityName = "Augsburg";

    SessionWorld w; NewGame(cfg, w);
    CHECK_EQ(w.personCount, (u32)20);
    CHECK_EQ(w.objectCount, (u32)10);

    CHECK(SaveSession(w, "Gamedata\\Saves\\session.SAV"));
    CHECK(fs.has("Gamedata\\Saves\\session.SAV"));

    SessionWorld loaded;
    CHECK(LoadSession("Gamedata\\Saves\\session.SAV", w.personCount, w.objectCount, loaded));

    // version reflects the written file.
    CHECK_EQ(io::SaveVersionGet(), (u32)io::kSaveVersionWriter);

    // table/record counts match.
    CHECK_EQ(loaded.personCount, w.personCount);
    CHECK_EQ(loaded.objectCount, w.objectCount);

    // structural + byte equivalence across header / scalar / both tables.
    EquivResult eq = CompareSessions(w, loaded);
    CHECK(eq.headerEqual);
    CHECK(eq.scalarEqual);
    CHECK(eq.personsEqual);
    CHECK(eq.objectsEqual);
    CHECK(eq.allEqual());

    // key field spot-checks.
    CHECK(std::strcmp(loaded.state.header.name, "Augsburg") == 0);
    CHECK_EQ(loaded.state.scalar.g647724, (u32)20); // dword_647724 == person count
    CHECK_EQ(loaded.state.scalar.g632240, (u32)1000000); // starting treasury

    // the live arrays were repopulated from the load.
    int live = 0;
    for (int i = 0; i < (int)w.personCount; ++i)
        if (sim::g_persons[i].marker != -1) ++live;
    CHECK_EQ(live, (int)w.personCount);

    io::VfsShutdown();
}

// ===========================================================================
// e2e: a save AFTER simulating turns differs from the pre-sim save (proves the
// simulated state is actually persisted, not a static template).
// ===========================================================================
TEST(SessionFlowE2E, PostSimSaveDiffersFromPreSim) {
    RwFs fs; io::VfsInit(&fs, false);

    SessionConfig cfg; cfg.seed = 7; cfg.persons = 16; cfg.objects = 8;
    SessionWorld w; NewGame(cfg, w);

    CHECK(SaveSession(w, "pre.SAV"));
    std::vector<u8> preBlob = fs.blob("pre.SAV");

    int muts = RunTurns(w, 4);
    CHECK_EQ(muts, 16 * 4);

    CHECK(SaveSession(w, "post.SAV"));
    std::vector<u8> postBlob = fs.blob("post.SAV");

    // both saves are the same size (same table shape) but DIFFERENT bytes.
    CHECK_EQ(preBlob.size(), postBlob.size());
    CHECK(preBlob != postBlob);

    // and the post-sim save reloads to the post-sim world (round-trip of sim state).
    SessionWorld rePost;
    CHECK(LoadSession("post.SAV", w.personCount, w.objectCount, rePost));
    EquivResult eq = CompareSessions(w, rePost);
    CHECK(eq.allEqual());

    // reloading the PRE save gives a world that differs from the post-sim world.
    SessionWorld rePre;
    CHECK(LoadSession("pre.SAV", w.personCount, w.objectCount, rePre));
    EquivResult diff = CompareSessions(rePre, rePost);
    CHECK(!diff.personsEqual);   // person table changed across the turns
    CHECK(!diff.scalarEqual);    // game clock advanced

    io::VfsShutdown();
}

// ===========================================================================
// e2e: two independent new-game + save runs with the same seed produce
// byte-identical .SAV blobs (full-pipeline determinism).
// ===========================================================================
TEST(SessionFlowE2E, SaveBlobDeterministic) {
    SessionConfig cfg; cfg.seed = 0x55AA55AAu; cfg.persons = 9; cfg.objects = 5;

    std::vector<u8> blobA, blobB;
    {
        RwFs fs; io::VfsInit(&fs, false);
        SessionWorld w; NewGame(cfg, w);
        CHECK(SaveSession(w, "d.SAV"));
        blobA = fs.blob("d.SAV");
        io::VfsShutdown();
    }
    {
        RwFs fs; io::VfsInit(&fs, false);
        SessionWorld w; NewGame(cfg, w);
        CHECK(SaveSession(w, "d.SAV"));
        blobB = fs.blob("d.SAV");
        io::VfsShutdown();
    }
    CHECK(!blobA.empty());
    CHECK(blobA == blobB);   // identical pipeline output for the same seed
}

// ===========================================================================
// GUARDED BONUS: load the real shipped AUGSBURG.cty through io::LoadWorld via the
// session-flow LoadRealCity wrapper. Skips (trivial pass) when the asset is absent.
// ===========================================================================
TEST(SessionFlowE2E, LoadRealAugsburgCityBonus) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }  // assets not present — skip
    std::printf("[session_flow][augsburg] asset root: %s\n", root);

    DiskFs fs(root);
    io::VfsInit(&fs, /*caseInsensitive=*/false);

    u32 persons = 0, objects = 0;
    bool ok = LoadRealCity("Resources/gamedata/Cities/AUGSBURG.cty", &persons, &objects);
    std::printf("[session_flow][augsburg] LoadRealCity -> %s  persons=%u objects=%u\n",
                ok ? "true" : "false", persons, objects);

    CHECK(ok);
    if (ok) {
        CHECK_EQ(io::SaveVersionGet(), (u32)0x1003B);   // shipped seed version
        CHECK_EQ(persons, (u32)1);                       // 1 person/scene record
        CHECK_EQ(objects, (u32)55);                      // 55 live object/building recs
        // the live arrays were populated by the real loader.
        int live = 0;
        for (int i = 0; i < sim::kObjectCapacity; ++i)
            if (sim::g_objects[i].alive) ++live;
        CHECK_EQ(live, 55);
    }

    io::VfsShutdown();
}
