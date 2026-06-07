// tests/unit/session_flow_test.cpp — unit tests for the session/save flow
// (guild::play session_flow). Focus: a single-table serialize/deserialize golden
// round-trip, the new-game seed determinism, and that a turn actually mutates state.
#include "test.h"

#include "play/session_flow.h"
#include "io/save.h"
#include "io/save_person.h"
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
// GOLDEN: a single (person/scene) table record serialize -> deserialize round-trip
// reconstructs the serialized fields byte-for-byte through the REAL io serializers.
// ---------------------------------------------------------------------------
TEST(SessionFlowUnit, SinglePersonRecordGoldenRoundTrip) {
    RwFs fs; io::VfsInit(&fs, false);
    io::SaveVersionSet(io::kSaveVersionWriter);

    // build one 536-byte record with serialized fields set.
    std::vector<u8> rec(sim::kPersonStride, 0);
    auto put16 = [&](int off, i16 v){ std::memcpy(rec.data()+off, &v, 2); };
    auto put32 = [&](int off, u32 v){ std::memcpy(rec.data()+off, &v, 4); };
    put16(0, 3);                            // marker (live)
    rec[sim::PersonField::kPfKind] = 7;     // +2 kind
    put32(sim::PersonField::kPfId, 4242);   // +4 id
    rec[sim::PersonField::kPfIsPlayer] = 1; // +8
    put16(sim::PersonField::kPfCash, 555);  // +10 cash
    put32(sim::PersonField::kPfWealthScore, 0xABCD01u); // +428
    put32(sim::PersonField::kPfTurnBits, 0x0F0Fu);      // +456

    io::PersonSceneLinks links{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    io::VfsHandle* w = io::VfsOpenFile("rec.dat", "wb");
    CHECK(w);
    if (w) {
        CHECK(io::SaveWritePersonSceneRecord(w, rec.data(), links));
        io::VfsCloseStream(w);
    }

    std::vector<u8> got(sim::kPersonStride, 0);
    io::PersonSceneLinks gotLinks{};
    io::VfsHandle* r = io::VfsOpenFile("rec.dat", "rb");
    CHECK(r);
    if (r) {
        CHECK(io::SaveLoadPersonSceneRecord(r, got.data(), &gotLinks));
        io::VfsCloseStream(r);
    }

    // the serialized record bytes reconstruct exactly (gaps are 0 on both sides).
    CHECK(got == rec);
    // spot-check load-bearing fields.
    i16 marker; std::memcpy(&marker, got.data(), 2); CHECK_EQ(marker, (i16)3);
    u32 id; std::memcpy(&id, got.data()+sim::PersonField::kPfId, 4); CHECK_EQ(id, (u32)4242);
    u32 wealth; std::memcpy(&wealth, got.data()+sim::PersonField::kPfWealthScore, 4);
    CHECK_EQ(wealth, (u32)0xABCD01u);
    // links survive the round-trip.
    CHECK_EQ(gotLinks.idAt91, (u32)0xFFFFFFFFu);
    CHECK_EQ(gotLinks.idAt97, (u32)0xFFFFFFFFu);

    io::VfsShutdown();
}

// ---------------------------------------------------------------------------
// New-game determinism: the same config (same seed) yields an identical live world
// (self-consistency, PLAYABLE_PLAN B1).
// ---------------------------------------------------------------------------
TEST(SessionFlowUnit, NewGameDeterministicSeed) {
    SessionConfig cfg; cfg.seed = 0xC0FFEEu; cfg.persons = 12; cfg.objects = 6;

    SessionWorld a;
    NewGame(cfg, a);
    // snapshot the live person bytes for run A.
    std::vector<u8> aPersons((std::size_t)a.personCount * sim::kPersonStride);
    for (u32 i = 0; i < a.personCount; ++i)
        std::memcpy(aPersons.data() + (std::size_t)i * sim::kPersonStride,
                    &sim::g_persons[i], sim::kPersonStride);

    SessionWorld b;
    NewGame(cfg, b);
    std::vector<u8> bPersons((std::size_t)b.personCount * sim::kPersonStride);
    for (u32 i = 0; i < b.personCount; ++i)
        std::memcpy(bPersons.data() + (std::size_t)i * sim::kPersonStride,
                    &sim::g_persons[i], sim::kPersonStride);

    CHECK_EQ(a.personCount, (u32)12);
    CHECK_EQ(a.objectCount, (u32)6);
    CHECK(aPersons == bPersons);                                   // identical world
    CHECK(std::memcmp(&a.state.header, &b.state.header, sizeof(io::SaveHeader)) == 0);
    CHECK(std::memcmp(&a.state.scalar, &b.state.scalar, sizeof(io::SaveScalarBlock)) == 0);

    // a different seed yields a different world (the seed actually matters).
    SessionConfig cfg2 = cfg; cfg2.seed = 0xBEEF01u;
    SessionWorld c; NewGame(cfg2, c);
    std::vector<u8> cPersons((std::size_t)c.personCount * sim::kPersonStride);
    for (u32 i = 0; i < c.personCount; ++i)
        std::memcpy(cPersons.data() + (std::size_t)i * sim::kPersonStride,
                    &sim::g_persons[i], sim::kPersonStride);
    CHECK(cPersons != aPersons);
}

// ---------------------------------------------------------------------------
// A turn actually changes state (not inert): cash + turn-bits + clock all advance.
// ---------------------------------------------------------------------------
TEST(SessionFlowUnit, RunTurnsMutatesLiveWorld) {
    SessionConfig cfg; cfg.seed = 1; cfg.persons = 4; cfg.objects = 3;
    SessionWorld w; NewGame(cfg, w);

    i16 cashBefore; std::memcpy(&cashBefore, (u8*)&sim::g_persons[0] + sim::PersonField::kPfCash, 2);
    i32 dayBefore;  std::memcpy(&dayBefore, w.state.scalar.gameTime, 4);

    int muts = RunTurns(w, 5);
    CHECK_EQ(muts, 4 * 5);   // personCount * turns

    i16 cashAfter; std::memcpy(&cashAfter, (u8*)&sim::g_persons[0] + sim::PersonField::kPfCash, 2);
    i32 dayAfter;  std::memcpy(&dayAfter, w.state.scalar.gameTime, 4);
    u32 turnBits;  std::memcpy(&turnBits, (u8*)&sim::g_persons[0] + sim::PersonField::kPfTurnBits, 4);

    CHECK(cashAfter > cashBefore);          // cash accrued
    CHECK_EQ(dayAfter, dayBefore + 5);      // clock advanced 5 days
    CHECK(turnBits != 0);                   // per-turn bitfield rotated in
}
