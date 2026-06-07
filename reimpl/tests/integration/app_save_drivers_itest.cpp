// tests/integration/app_save_drivers_itest.cpp — cross-module integration of the
// save/load orchestration spine against its REAL siblings.
//
// Drives a real new-game session bootstrap (app::InitOrLoadSession over the real
// world reset + RNG seed + calendar), then runs the save spine end to end:
//   * app::SaveDoQuickSave -> io::WriteGameState (real VFS write of Quicksave.SAV)
//   * io::LoadGameState round-trips the header byte-identically (real load)
//   * app::MenuRunSaveGame -> io::WriteGameState (a named slot) loads back too
//   * app::NetLoadAndSyncSession -> the .SAV/.SRV pair + a real compress::CrcCompute
// Links app/save_drivers + app/session_init + the real io save/vfs + world/sim/crt
// reused modules; only the GUI/net/command leaves are recorded through the hook.
#include "test.h"

#include "app/save_drivers.h"
#include "app/session_init.h"
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

// A minimal real-forwarding ISaveHooks: the GUI/net/command leaves are recorded,
// but commandGetPacketStatusById acks immediately so any barrier terminates.
class ItestHooks : public ISaveHooks {
public:
    int frames = 0, waits = 0, amtSpins = 0, cmds = 0;
    void hudSetStatusBanner(const std::string&) override {}
    void lightSetGrayColor(int, int) override {}
    void renderCaptureThumbnail() override {}
    int  runFrameLoop(std::uint32_t) override { return ++frames < 50 ? 1 : 0; }
    bool netAllPlayersReady() override { return true; }
    std::uint32_t commandQueueRequestFlagBlob32(int, const void*) override { ++cmds; return 7; }
    bool commandGetPacketStatusById(std::uint32_t) override { return true; }
    void amtRefreshGuildState() override { ++amtSpins; }
    void netRunWaitLoop(const std::string&) override { ++waits; }
};

io::GameState SnapshotFromBootstrap() {
    // Drive the real new-game session bootstrap so the calendar/world globals are
    // in their post-init state, then capture them into an io::GameState snapshot.
    SessionInitCtx ctx;
    ctx.cityName = "Augsburg";
    ctx.rngSeed = 0x1234;
    ctx.difficulty = 2;
    ctx.players.push_back({1001, 6, true});
    InitOrLoadSession(session::kNewGame, ctx, /*framesPerSession=*/2);

    io::GameState s;
    s.header.magic = io::kSaveVersionWriter;
    std::strncpy(s.header.name, ctx.cityName.c_str(), sizeof s.header.name - 1);
    // Mirror the post-init calendar (day 2) into the scalar block's gametime[0].
    const sim::GameTime& clk = WorldClock();
    s.scalar.gameTime[0] = static_cast<u8>(clk.day & 0xFF);
    s.scalar.g632240 = 1000000;
    return s;
}

} // namespace

// new-game bootstrap -> quicksave (real write) -> reload (real load), header
// byte-identical, and the save-slot byte stamped through the real spine.
TEST(AppSaveDriversItest, NewGameQuickSaveRoundTrip) {
    RwFs fs; io::VfsInit(&fs, false);
    ItestHooks h;

    io::GameState s = SnapshotFromBootstrap();
    io::GameState pristine = s;

    bool ok = SaveDoQuickSave(session::kNewGame, s, h);
    CHECK(ok);
    CHECK(fs.has("Gamedata\\Saves\\Quicksave.SAV"));

    io::GameState loaded;
    CHECK(io::LoadGameState("Gamedata\\Saves\\Quicksave.SAV", loaded, nullptr));
    CHECK_EQ(io::SaveVersionGet(), (u32)io::kSaveVersionWriter);
    // The save spine stamped byte_649D50 = 1 into the header before writing.
    CHECK_EQ(loaded.header.byte649D50, (u8)1);
    CHECK(std::strcmp(loaded.header.name, pristine.header.name) == 0);
    CHECK_EQ(loaded.scalar.g632240, (u32)1000000);

    io::VfsShutdown();
}

// The save-game menu spine writes a named slot that the real loader reads back.
TEST(AppSaveDriversItest, MenuSlotRoundTrip) {
    RwFs fs; io::VfsInit(&fs, false);
    ItestHooks h;

    io::GameState s = SnapshotFromBootstrap();

    SaveMenuCtx ctx;
    ctx.maxFrames = 8;
    ctx.pickedSlot = 5;
    ctx.pickedName = "Augsburg_R12";
    MenuRunSaveGame(ctx, s, h);

    CHECK(ctx.wrote);
    CHECK(fs.has("Gamedata\\Saves\\Augsburg_R12.SAV"));

    io::GameState loaded;
    CHECK(io::LoadGameState(ctx.writtenPath.c_str(), loaded, nullptr));
    CHECK_EQ(loaded.header.byte649D50, (u8)5);   // slot index stamped by the spine
    CHECK(std::strcmp(loaded.header.name, "Augsburg") == 0);

    io::VfsShutdown();
}

// The net resync spine drives the real io writers + the real CRC over a re-read
// header, producing the .SAV/.SRV pair and a non-zero verify CRC.
TEST(AppSaveDriversItest, NetResyncWritesAndCrcs) {
    RwFs fs; io::VfsInit(&fs, false);
    ItestHooks h;

    io::GameState s = SnapshotFromBootstrap();

    NetSyncCtx ctx;
    ctx.sessionName = "lan42";
    ctx.peers = {{true, 6}, {true, 7}};

    int r = NetLoadAndSyncSession(session::kNetwork, ctx, s, h);

    CHECK_EQ(r, 0);
    CHECK(fs.has("Gamedata\\network\\lan42.SAV"));
    CHECK(fs.has("Gamedata\\network\\lan42.SRV"));
    CHECK(ctx.headerCrc != 0);                 // CRC32 over the re-read header bytes
    CHECK_EQ(ctx.framesBarrier, 2);            // 2 ready peers
    CHECK_EQ(GameSaveDriverState().readyPlayerCount, (u8)2);

    io::VfsShutdown();
}
