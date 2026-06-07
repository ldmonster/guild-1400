// tests/unit/app_save_drivers_test.cpp — unit tests for the save/load
// orchestration spine (guild::app save_drivers, gilde.exe save callers).
//
// Drives each orchestrator with a recording ISaveHooks + an in-memory VFS so the
// real io::WriteGameState/LoadGameState path runs, and asserts the recovered
// branch behaviour (network vs single quicksave, the speed-key trigger gate, the
// save-menu pump+write, the net resync barrier+writes).
#include "test.h"

#include "app/save_drivers.h"
#include "io/gamestate.h"
#include "io/save.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::app;

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
    std::size_t fileCount() const { return files_.size(); }
    bool has(const std::string& p) const { return files_.count(p) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// --- recording ISaveHooks ---------------------------------------------------
class RecHooks : public ISaveHooks {
public:
    std::vector<std::string> banners;
    int frames = 0, captures = 0, grays = 0, waits = 0, amtSpins = 0;
    int cmdEnqueues = 0;
    bool allReady = true;
    int ackAfter = 1;  // GetPacketStatusById returns true after this many polls

    void hudSetStatusBanner(const std::string& t) override { banners.push_back(t); }
    void lightSetGrayColor(int, int) override { ++grays; }
    void renderCaptureThumbnail() override { ++captures; }
    int  runFrameLoop(std::uint32_t) override { ++frames; return frames < 100 ? 1 : 0; }
    bool netAllPlayersReady() override { return allReady; }
    std::uint32_t commandQueueRequestFlagBlob32(int, const void*) override {
        ++cmdEnqueues; polls_ = 0; return 0xABCDu;
    }
    bool commandGetPacketStatusById(std::uint32_t) override {
        return ++polls_ >= ackAfter;
    }
    void amtRefreshGuildState() override { ++amtSpins; }
    void netRunWaitLoop(const std::string&) override { ++waits; }
private:
    int polls_ = 0;
};

io::GameState MakeState() {
    io::GameState s;
    s.header.magic = io::kSaveVersionWriter;
    std::strncpy(s.header.name, "Augsburg", sizeof s.header.name - 1);
    s.scalar.g632240 = 1000000;
    return s;
}

} // namespace

// Single-player quicksave: pumps a frame, captures the thumbnail, stamps the slot
// byte, writes the real file, and brackets it with the saving/restore banners.
TEST(AppSaveDrivers, QuickSaveSinglePlayerWrites) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    bool ok = SaveDoQuickSave(/*flags=*/session::kNewGame, s, h);

    CHECK(ok);
    CHECK_EQ(h.frames, 1);          // one banner-show pump
    CHECK_EQ(h.captures, 1);        // thumbnail captured
    CHECK_EQ(GameSaveDriverState().saveSlotByte, (std::uint8_t)1); // byte_649D50 = 1
    CHECK(h.banners.size() == 2);   // "Saving..." then restore
    CHECK(fs.has("Gamedata\\Saves\\Quicksave.SAV"));

    io::VfsShutdown();
}

// Network host quicksave with all peers ready: no file is written; instead it
// fades, enqueues the sync command, and spins the Amt barrier until acked.
TEST(AppSaveDrivers, QuickSaveNetworkHostEnqueuesSync) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h; h.allReady = true; h.ackAfter = 3;
    io::GameState s = MakeState();

    bool ok = SaveDoQuickSave(/*flags=*/(std::uint16_t)(session::kNetwork |
                                                        session::kHost), s, h);

    CHECK(!ok);                      // network path writes no local file
    CHECK_EQ(h.cmdEnqueues, 1);
    CHECK_EQ(h.grays, 1);
    CHECK_EQ(h.amtSpins, 2);         // spun until the 3rd poll acked
    CHECK_EQ(h.captures, 0);         // no single-player thumbnail
    CHECK(!fs.has("Gamedata\\Saves\\Quicksave.SAV"));

    io::VfsShutdown();
}

// Network host quicksave when peers are NOT ready: shows the "wait" banner only.
TEST(AppSaveDrivers, QuickSaveNetworkNotReadyBanner) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h; h.allReady = false;
    io::GameState s = MakeState();

    SaveDoQuickSave((std::uint16_t)(session::kNetwork | session::kHost), s, h);

    CHECK_EQ(h.cmdEnqueues, 0);
    CHECK(h.banners.size() == 1);
    CHECK(h.banners[0] == "ERR_NET_NOT_READY");

    io::VfsShutdown();
}

// Network non-host quicksave: "host only" banner, no command.
TEST(AppSaveDrivers, QuickSaveNetworkClientHostOnly) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    SaveDoQuickSave(session::kNetwork, s, h);

    CHECK_EQ(h.cmdEnqueues, 0);
    CHECK(h.banners.size() == 1);
    CHECK(h.banners[0] == "ERR_NET_HOST_ONLY");

    io::VfsShutdown();
}

// Tutorial single game: quicksave is disabled (no write, no banner).
TEST(AppSaveDrivers, QuickSaveTutorialDisabled) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    bool ok = SaveDoQuickSave((std::uint16_t)(session::kNewGame | session::kTutorial),
                              s, h);

    CHECK(!ok);
    CHECK(h.banners.empty());
    CHECK(!fs.has("Gamedata\\Saves\\Quicksave.SAV"));

    io::VfsShutdown();
}

// The speed-key trigger: fires only when scancode==16, menu idle, and the
// headless-suppress mask bit is clear.
TEST(AppSaveDrivers, SpeedKeyTriggerGate) {
    RwFs fs; io::VfsInit(&fs, false);
    io::GameState s = MakeState();

    // wrong scancode -> no trigger
    { RecHooks h; CHECK(!InputQuickSaveTrigger(17, false, 0, session::kNewGame, s, h)); }
    // menu busy -> no trigger
    { RecHooks h; CHECK(!InputQuickSaveTrigger(16, true, 0, session::kNewGame, s, h)); }
    // headless-suppress set -> no trigger
    { RecHooks h; CHECK(!InputQuickSaveTrigger(16, false, kHeadlessSuppressBit,
                                               session::kNewGame, s, h)); }
    // all gates clear -> triggers + writes
    { RecHooks h; CHECK(InputQuickSaveTrigger(16, false, 0, session::kNewGame, s, h));
      CHECK_EQ(h.captures, 1); }

    io::VfsShutdown();
}

// The save-game menu loop: pumps frames, then on a picked+named slot writes the
// real per-slot file and stops.
TEST(AppSaveDrivers, MenuRunSaveGameWritesPickedSlot) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    SaveMenuCtx ctx;
    ctx.maxFrames = 10;
    ctx.pickedSlot = 3;
    ctx.pickedName = "MyGame";
    ctx.slotOccupied = false;

    MenuRunSaveGame(ctx, s, h);

    CHECK(ctx.wrote);
    CHECK(ctx.writtenPath == "Gamedata\\Saves\\MyGame.SAV");
    CHECK_EQ(GameSaveDriverState().saveSlotByte, (std::uint8_t)3);
    CHECK(fs.has("Gamedata\\Saves\\MyGame.SAV"));
    CHECK(ctx.framesPumped >= 1);

    io::VfsShutdown();
}

// Occupied slot, overwrite declined: no write happens.
TEST(AppSaveDrivers, MenuRunSaveGameOverwriteDeclined) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    SaveMenuCtx ctx;
    ctx.maxFrames = 5;
    ctx.pickedSlot = 1;
    ctx.pickedName = "Existing";
    ctx.slotOccupied = true;
    ctx.overwriteConfirmed = false;

    MenuRunSaveGame(ctx, s, h);

    CHECK(!ctx.wrote);
    CHECK(!fs.has("Gamedata\\Saves\\Existing.SAV"));

    io::VfsShutdown();
}

// The net load+resync spine on a non-network session returns result*4 (=64) and
// touches no file.
TEST(AppSaveDrivers, NetSyncNonNetworkEarlyOut) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();
    NetSyncCtx ctx; ctx.sessionName = "lan";

    int r = NetLoadAndSyncSession(/*flags=*/session::kNewGame, ctx, s, h);

    CHECK_EQ(r, 64);
    CHECK(ctx.writtenPaths.empty());
    CHECK_EQ(h.waits, 0);

    io::VfsShutdown();
}

// The net resync spine: counts ready players, writes the .SAV/.SRV/.SAV trio,
// CRCs the header, enqueues the sync command, and spins the barrier per peer.
TEST(AppSaveDrivers, NetSyncWritesTrioAndBarrier) {
    RwFs fs; io::VfsInit(&fs, false);
    RecHooks h;
    io::GameState s = MakeState();

    NetSyncCtx ctx;
    ctx.sessionName = "lan";
    ctx.peers = {{true, 6}, {true, 7}, {false, 6}, {true, 3}}; // 2 ready (6,7)

    int r = NetLoadAndSyncSession(session::kNetwork, ctx, s, h);

    CHECK_EQ(r, 0);
    CHECK_EQ(GameSaveDriverState().readyPlayerCount, (std::uint8_t)2);
    CHECK_EQ(h.amtSpins, 2);              // one barrier spin per ready peer
    CHECK_EQ(h.cmdEnqueues, 1);
    CHECK(h.waits == 2);                  // wait before + after the barrier
    CHECK(ctx.headerCrc != 0);           // header CRC computed over the re-read file
    CHECK(fs.has("Gamedata\\network\\lan.SAV"));
    CHECK(fs.has("Gamedata\\network\\lan.SRV"));

    io::VfsShutdown();
}
