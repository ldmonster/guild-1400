// tests/unit/real_session_test.cpp — unit tests for the real-asset save round-trip
// helpers (guild::play real_session). No real assets here: a synthetic SEEDED live
// world is captured -> saved -> reloaded through the real io save spine, and proven
// equivalent (CompareSessions byte-identical AND HashFullWorld equal). Also checks
// CaptureLiveWorld snapshots the live arrays faithfully.
#include "test.h"

#include "play/real_session.h"
#include "play/session_flow.h"
#include "play/world_digest.h"
#include "io/save.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim/IFileSystem.h"

#include <cstring>
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
private:
    std::map<std::string, std::vector<u8>> files_;
};

} // namespace

// ---------------------------------------------------------------------------
// CaptureLiveWorld snapshots the live arrays faithfully (counts + bytes).
// ---------------------------------------------------------------------------
TEST(RealSessionUnit, CaptureLiveWorldSnapshotsArrays) {
    SessionConfig cfg; cfg.seed = 0xABCDEF01u; cfg.persons = 10; cfg.objects = 6;
    SessionWorld seeded; NewGame(cfg, seeded);

    SessionWorld cap;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, "Augsburg", cap);

    CHECK_EQ(cap.personCount, (u32)10);
    CHECK_EQ(cap.objectCount, (u32)6);
    CHECK_EQ(cap.personBytes.size(), (std::size_t)10 * sim::kPersonStride);
    CHECK_EQ(cap.objectBytes.size(), (std::size_t)6  * sim::kObjectStride);

    // the captured bytes match the live arrays exactly.
    bool personsMatch = true, objectsMatch = true;
    for (u32 i = 0; i < cap.personCount; ++i)
        if (std::memcmp(cap.personBytes.data() + (std::size_t)i * sim::kPersonStride,
                        &sim::g_persons[i], sim::kPersonStride) != 0) personsMatch = false;
    for (u32 i = 0; i < cap.objectCount; ++i)
        if (std::memcmp(cap.objectBytes.data() + (std::size_t)i * sim::kObjectStride,
                        &sim::g_objects[i], sim::kObjectStride) != 0) objectsMatch = false;
    CHECK(personsMatch);
    CHECK(objectsMatch);

    // the header carries the city name + counts.
    CHECK(std::strcmp(cap.state.header.name, "Augsburg") == 0);
    CHECK_EQ(cap.state.scalar.g647724, (u32)10);
}

// ---------------------------------------------------------------------------
// A synthetic seeded world round-trips through the save helpers: save -> reload ->
// CompareSessions byte-identical AND HashFullWorld equal.
// ---------------------------------------------------------------------------
TEST(RealSessionUnit, SyntheticWorldRoundTripHashEqual) {
    RwFs fs; io::VfsInit(&fs, false);

    SessionConfig cfg; cfg.seed = 0x13572468u; cfg.persons = 14; cfg.objects = 8;
    SessionWorld seeded; NewGame(cfg, seeded);

    // capture the live world and its full-world hash.
    SessionWorld saved;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, cfg.cityName, saved);
    std::uint64_t hashSaved = HashFullWorld();

    // save through the real io spine to a .SAV name (NOT a .bin — that reads as zip).
    CHECK(SaveSession(saved, "Gamedata\\Saves\\unit.SAV"));
    CHECK(fs.has("Gamedata\\Saves\\unit.SAV"));

    // reload into the live arrays + a SessionWorld.
    SessionWorld reloaded;
    CHECK(LoadSession("Gamedata\\Saves\\unit.SAV",
                      saved.personCount, saved.objectCount, reloaded));
    std::uint64_t hashReloaded = HashFullWorld();

    // structural byte equivalence.
    EquivResult eq = CompareSessions(saved, reloaded);
    CHECK(eq.headerEqual);
    CHECK(eq.scalarEqual);
    CHECK(eq.personsEqual);
    CHECK(eq.objectsEqual);
    CHECK(eq.allEqual());

    // full-world digest equivalence (live arrays reconstructed identically).
    CHECK_EQ(hashSaved, hashReloaded);
    CHECK(hashSaved != 0u);

    io::VfsShutdown();
}

// ---------------------------------------------------------------------------
// A .dat save name also round-trips (the helper is name-agnostic as long as the
// name does not collide with the PKZIP .bin transparent-backing rule).
// ---------------------------------------------------------------------------
TEST(RealSessionUnit, DatSaveNameRoundTrips) {
    RwFs fs; io::VfsInit(&fs, false);

    SessionConfig cfg; cfg.seed = 99; cfg.persons = 5; cfg.objects = 3;
    SessionWorld seeded; NewGame(cfg, seeded);

    SessionWorld saved;
    CaptureLiveWorld(seeded.personCount, seeded.objectCount, cfg.cityName, saved);
    CHECK(SaveSession(saved, "rt.dat"));
    CHECK(fs.has("rt.dat"));

    SessionWorld reloaded;
    CHECK(LoadSession("rt.dat", saved.personCount, saved.objectCount, reloaded));
    CHECK(CompareSessions(saved, reloaded).allEqual());

    io::VfsShutdown();
}
