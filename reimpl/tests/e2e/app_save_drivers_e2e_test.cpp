// tests/e2e/app_save_drivers_e2e_test.cpp — GUARDED real-asset save-spine e2e.
//
// Boots the save/load orchestration spine on a REAL "Die Gilde — Europe 1400"
// install:
//   1. Mount the real disk fs, bind the VFS, and LOAD the real city header
//      (Resources/gamedata/Cities/AUGSBURG.cty) through io::LoadGameState — the
//      real, newly-wired load hook running against real bytes.
//   2. Re-bind the VFS to a writable in-memory fs and run app::SaveDoQuickSave,
//      which drives the real io::WriteGameState path to produce Quicksave.SAV.
//   3. Re-load the just-written quicksave through io::LoadGameState and assert the
//      header the spine stamped (byte_649D50 = 1) round-trips byte-faithfully.
//
// GUARDED: if Resources/forms.BIN (the real-asset sentinel) is absent the test
// records ZERO checks and returns (clean skip), so CI without the assets passes.
// Override the game dir with GUILD_GAME_DIR.
#include "test.h"

#include "app/save_drivers.h"
#include "io/gamestate.h"
#include "io/save.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::app;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// Writable in-memory VFS for the quicksave write (don't touch the real install).
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

class NoopHooks : public ISaveHooks {
public:
    int captures = 0, frames = 0;
    void hudSetStatusBanner(const std::string&) override {}
    void lightSetGrayColor(int, int) override {}
    void renderCaptureThumbnail() override { ++captures; }
    int  runFrameLoop(std::uint32_t) override { return ++frames < 5 ? 1 : 0; }
    bool netAllPlayersReady() override { return true; }
    std::uint32_t commandQueueRequestFlagBlob32(int, const void*) override { return 1; }
    bool commandGetPacketStatusById(std::uint32_t) override { return true; }
    void amtRefreshGuildState() override {}
    void netRunWaitLoop(const std::string&) override {}
};

} // namespace

TEST(AppSaveDriversE2E, RealCityLoadThenQuickSaveWrite) {
    const std::string dir = GameDir();
    shim::DiskFileSystem disk(dir);

    if (!RealAssetsPresent(disk)) {
        std::printf("  [skip] AppSaveDriversE2E.RealCityLoadThenQuickSaveWrite: "
                    "real game dir absent (%s)\n", dir.c_str());
        return; // clean skip — no checks recorded
    }

    // ---- 1. real LOAD: read the real AUGSBURG.cty header through io::LoadGameState
    io::VfsInit(&disk, false);
    io::GameState city;
    bool loadedCity = io::LoadGameState(
        "Resources/gamedata/Cities/AUGSBURG.cty", city, nullptr);
    io::VfsShutdown();

    CHECK(loadedCity);                                   // the real city loaded
    CHECK_EQ(io::SaveVersionGet() >= (u32)io::kSaveVersionLoadMin &&
             io::SaveVersionGet() <= (u32)io::kSaveVersionLoadMax, true);

    // ---- 2. real WRITE: run SaveDoQuickSave -> io::WriteGameState (writable VFS) -
    RwFs out;
    io::VfsInit(&out, false);
    NoopHooks h;
    io::GameState snapshot = city;            // save the loaded real city back out
    snapshot.thumbnail.clear();               // header+scalar are enough for the e2e

    bool wrote = SaveDoQuickSave(session::kNewGame, snapshot, h);
    CHECK(wrote);                              // the real write hook produced a file
    CHECK_EQ(h.captures, 1);                   // the quicksave thumbnail-capture step
    CHECK(out.has("Gamedata\\Saves\\Quicksave.SAV"));
    CHECK_EQ(GameSaveDriverState().saveSlotByte, (u8)1);

    // ---- 3. real RELOAD: the spine-stamped header round-trips byte-faithfully ----
    io::GameState reload;
    bool reloaded =
        io::LoadGameState("Gamedata\\Saves\\Quicksave.SAV", reload, nullptr);
    io::VfsShutdown();

    CHECK(reloaded);
    CHECK_EQ(reload.header.byte649D50, (u8)1);  // byte_649D50 = 1 stamped by the spine
}
