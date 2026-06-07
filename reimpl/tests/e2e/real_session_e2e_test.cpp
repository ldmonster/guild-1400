// tests/e2e/real_session_e2e_test.cpp — GUARDED real-asset save round-trip (M3).
//
// Load the REAL shipped AUGSBURG.cty through app::MountRealGameAssets + io::LoadWorld
// into the live arrays, capture it, save through the real io spine to a .SAV in a
// WRITABLE VFS, reload, and assert table/record counts + CompareSessions + the full-
// world hash all match. Also runs K real economy turns before save and proves the
// post-turn world reloads (sim state persisted). Skips cleanly when assets absent.
#include "test.h"

#include "play/real_session.h"
#include "play/session_flow.h"
#include "play/world_digest.h"
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

// A HYBRID host filesystem: serves real files from disk on READ, and accepts WRITES
// (the .SAV) into an in-memory store (re-openable for read). This lets the real
// round-trip run end to end over the genuine AUGSBURG.cty bytes.
class HybridFile : public shim::IFile {
public:
    // read-from-disk ctor
    explicit HybridFile(std::vector<u8> data)
        : data_(std::move(data)), writing_(false) {}
    // write-to-store ctor
    explicit HybridFile(std::vector<u8>* store)
        : store_(store), writing_(true) { store_->clear(); }

    std::size_t read(void* dst, std::size_t n) override {
        if (writing_) return 0;
        std::size_t avail = data_.size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (!writing_) return 0;
        const u8* p = static_cast<const u8*>(src);
        store_->insert(store_->end(), p, p + n);
        pos_ += n; return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t len = writing_ ? (std::int64_t)store_->size()
                                    : (std::int64_t)data_.size();
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? len : 0;
        std::int64_t t = base + off; if (t < 0) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override {
        return writing_ ? (std::int64_t)store_->size() : (std::int64_t)data_.size();
    }
private:
    std::vector<u8>  data_;             // read backing (disk bytes)
    std::vector<u8>* store_ = nullptr;  // write backing (in-memory)
    bool             writing_;
    std::size_t      pos_ = 0;
};

class HybridFs : public shim::IFileSystem {
public:
    explicit HybridFs(std::string root) : root_(std::move(root)) {}
    shim::IFile* open(const char* path, const char* mode) override {
        bool writing = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'));
        if (writing)
            return new HybridFile(&writes_[path]);
        // first try a prior in-memory write, then the disk.
        auto it = writes_.find(path);
        if (it != writes_.end()) return new HybridFile(it->second);
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new HybridFile(std::move(data));
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        if (writes_.count(path)) return true;
        std::ifstream f(full(path), std::ios::binary); return (bool)f;
    }
    bool wroteSave(const std::string& p) const { return writes_.count(p) != 0; }
    std::size_t saveBytes(const std::string& p) { return writes_[p].size(); }
private:
    std::string full(const char* path) const {
        std::string p = path; for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
    std::map<std::string, std::vector<u8>> writes_;
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
// e2e: load real AUGSBURG -> capture -> save -> reload -> counts + CompareSessions
// + HashFullWorld all match. (Zero turns: pure load/save fidelity over real bytes.)
// ===========================================================================
TEST(RealSessionE2E, AugsburgLoadSaveReloadRoundTrip) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent — skip
    std::printf("[real_session][augsburg] asset root: %s\n", root);

    HybridFs fs(root);
    RealRoundTripResult r =
        RealRoundTrip(&fs, root, "Augsburg", /*turns=*/0, /*econSeed=*/12345,
                      "Gamedata\\Saves\\augsburg_rt.SAV");

    std::printf("[real_session][augsburg] loaded=%d persons=%u objects=%u saved=%d "
                "reloaded=%d structEq=%d hashEq=%d hashSaved=%llu hashReloaded=%llu\n",
                (int)r.loaded, r.personCount, r.objectCount, (int)r.saved,
                (int)r.reloaded, (int)r.structuralEqual(), (int)r.hashEqual(),
                (unsigned long long)r.hashSaved, (unsigned long long)r.hashReloaded);

    CHECK(r.loaded);
    if (r.loaded) {
        // recovered from the shipped Augsburg seed (stable across the asset).
        CHECK_EQ(r.personCount, (u32)1);    // 1 person/scene record
        CHECK_EQ(r.objectCount, (u32)55);   // 55 live object/building records

        CHECK(r.saved);
        CHECK(r.reloaded);

        // structural byte equivalence across header/scalar/both tables.
        CHECK(r.equiv.headerEqual);
        CHECK(r.equiv.scalarEqual);
        CHECK(r.equiv.personsEqual);
        CHECK(r.equiv.objectsEqual);
        CHECK(r.structuralEqual());

        // full-world digest equivalence.
        CHECK(r.hashEqual());
        CHECK(r.hashSaved != 0u);

        // the save was actually written.
        CHECK(fs.wroteSave("Gamedata\\Saves\\augsburg_rt.SAV"));
        std::printf("[real_session][augsburg] .SAV bytes = %zu\n",
                    fs.saveBytes("Gamedata\\Saves\\augsburg_rt.SAV"));

        CHECK(r.ok());
    }

    io::VfsShutdown();   // RealRoundTrip already shuts down; idempotent guard.
}

// ===========================================================================
// e2e: a post-K-turns save reloads to the POST-turn world (sim state persisted).
// Loads real AUGSBURG, runs K economy turns, then round-trips; the round-trip must
// still be structurally + hash equivalent (the saved world == the reloaded world).
// ===========================================================================
TEST(RealSessionE2E, AugsburgPostTurnsSavePersists) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent — skip

    HybridFs fs(root);
    const int K = 6;
    RealRoundTripResult r =
        RealRoundTrip(&fs, root, "Augsburg", K, /*econSeed=*/0xBEEF,
                      "Gamedata\\Saves\\augsburg_postturn.dat");

    std::printf("[real_session][augsburg+turns] turns=%d persons=%u objects=%u "
                "mutations=%d structEq=%d hashEq=%d\n",
                r.turns, r.personCount, r.objectCount, r.personMutations,
                (int)r.structuralEqual(), (int)r.hashEqual());

    CHECK(r.loaded);
    if (r.loaded) {
        CHECK_EQ(r.turns, K);
        CHECK_EQ(r.personCount, (u32)1);
        CHECK_EQ(r.objectCount, (u32)55);
        CHECK(r.personMutations >= 1);   // the post-turn person witness fired

        // the post-turn world round-trips losslessly through the real save spine.
        CHECK(r.saved);
        CHECK(r.reloaded);
        CHECK(r.structuralEqual());
        CHECK(r.hashEqual());
        CHECK(r.ok());
        CHECK(fs.wroteSave("Gamedata\\Saves\\augsburg_postturn.dat"));
    }

    io::VfsShutdown();
}

// ===========================================================================
// e2e: two independent real-asset round-trips with the same econSeed produce the
// same saved full-world hash (full-pipeline determinism over real bytes).
// ===========================================================================
TEST(RealSessionE2E, AugsburgRoundTripDeterministic) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }

    HybridFs fsA(root), fsB(root);
    RealRoundTripResult a =
        RealRoundTrip(&fsA, root, "Augsburg", 4, 0x33, "a.SAV");
    RealRoundTripResult b =
        RealRoundTrip(&fsB, root, "Augsburg", 4, 0x33, "b.SAV");

    CHECK(a.loaded);
    CHECK(b.loaded);
    if (a.loaded && b.loaded) {
        CHECK_EQ(a.hashSaved, b.hashSaved);       // identical post-turn world
        CHECK_EQ(a.hashReloaded, b.hashReloaded);
        CHECK(a.ok());
        CHECK(b.ok());
    }

    io::VfsShutdown();
}
