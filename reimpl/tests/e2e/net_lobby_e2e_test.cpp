// End-to-end multiplayer lobby flow against the REAL shipped game assets. Guarded:
// if the asset folder isn't present the test passes trivially so the suite stays
// green everywhere (point elsewhere with GUILD_GAME_DIR).
//
// The host loads a real city seed off disk, advertises the game on the (in-process)
// LAN, a joining peer discovers + decodes the advert, then the host ships the real
// city bytes through the lockstep save-transfer; the bytes the client reassembles
// must equal the real file byte-for-byte. Exercises the lobby spine across the REAL
// discovery + net_savegame + sim::command siblings.
#include "test.h"
#include "net/lobby.h"
#include "net/discovery.h"
#include "net/net_savegame.h"
#include "sim/command.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace guild;
using namespace guild::net;

namespace {

std::string gameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool assetsPresent() {
    shim::DiskFileSystem fs(gameDir());
    return fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// Read a whole file off the real disk filesystem into a byte vector.
std::vector<u8> readFile(shim::DiskFileSystem& fs, const char* path) {
    std::vector<u8> out;
    shim::IFile* f = fs.open(path, "rb");
    if (!f)
        return out;
    u8 chunk[4096];
    for (;;) {
        std::size_t n = f->read(chunk, sizeof(chunk));
        if (n == 0)
            break;
        out.insert(out.end(), chunk, chunk + n);
    }
    fs.close(f);
    return out;
}

struct DgBus { std::vector<std::pair<u16, Datagram>> queues; };
class LoopbackDatagram : public INetDatagram {
public:
    LoopbackDatagram(DgBus* bus, std::string ip) : bus_(bus), ip_(std::move(ip)) {}
    bool open(u16 bindPort, bool) override { bindPort_ = bindPort; return true; }
    void close() override {}
    int sendTo(u16 dstPort, const void* data, std::size_t n) override {
        Datagram dg; dg.fromIp = ip_;
        const u8* p = static_cast<const u8*>(data);
        dg.data.assign(p, p + n);
        bus_->queues.emplace_back(dstPort, std::move(dg));
        return static_cast<int>(n);
    }
    bool recvFrom(Datagram& out) override {
        for (auto it = bus_->queues.begin(); it != bus_->queues.end(); ++it) {
            if (it->first == bindPort_) {
                out = std::move(it->second);
                bus_->queues.erase(it);
                return true;
            }
        }
        return false;
    }
private:
    DgBus* bus_; std::string ip_; u16 bindPort_ = 0;
};
struct StepClock { u32 t = 0; u32 step = 0; };
u32 stepNow(void* user) { auto* c = static_cast<StepClock*>(user); u32 v = c->t; c->t += c->step; return v; }

class HostLobbyHook : public ILobbyHook {
public:
    explicit HostLobbyHook(sim::CommandQueue* q) : q_(q) {}
    bool pumpAndRefresh(u8& readyMask) override {
        ++pumps_;
        if (pumps_ == 1) { readyMask |= kReadyHost; }
        else { q_->FlushSendQueue(); q_->ExecCommands(); readyMask |= kReadyAllPeers; }
        return true;
    }
private:
    sim::CommandQueue* q_;
    int pumps_ = 0;
};

} // namespace

TEST(NetLobbyE2E, RealCityHostJoinShipReassemble) {
    if (!assetsPresent()) { CHECK(true); return; }   // clean skip
    shim::DiskFileSystem fs(gameDir());

    // 1. Load the real city seed off disk — this is what the host ships to clients.
    std::vector<u8> city = readFile(fs, "Resources/gamedata/Cities/AUGSBURG.cty");
    CHECK(city.size() > 0);

    // 2. Host advertises the game on the (in-process) LAN; a joining peer discovers
    //    + decodes the advert through the REAL discovery siblings.
    DgBus bus;
    LoopbackDatagram hostDg(&bus, "192.0.2.10");
    LoopbackDatagram joinDg(&bus, "0.0.0.0");
    LobbyAdvert advert = BuildHostAdvert("AUGSBURG", /*players=*/4, /*scenario=*/0, nullptr);

    StepClock clk{0, 1};
    std::vector<DiscoveredServer> found;
    int n = AdvertiseAndDiscover(&hostDg, &joinDg, advert, kDiscoveryPort,
                                 kDiscoveryWindow, found, stepNow, &clk, 1);
    CHECK_EQ(n, 1);
    LobbyAdvert joinerView;
    CHECK(DecodeLobbyAdvert(found[0].blob, joinerView));
    CHECK_EQ(joinerView.name, std::string("AUGSBURG"));
    CHECK_EQ(joinerView.playerCount, static_cast<u8>(4));

    // 3. Host runs the lobby spine: ship the real city bytes + ready handshake.
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    q.set_disconnected(false);
    HostLobbyHook hook(&q);
    LobbyRunResult r = RunNetworkLobby(advert, q, hook, city, /*maxPumps=*/65536);
    CHECK(r.savedSent);
    CHECK(r.ready);
    CHECK_EQ(r.rc, 0);

    // 4. The bytes a client reassembles (real save-transfer siblings) equal the real
    //    city file byte-for-byte (the tail is the lobby's 128-block zero padding).
    sim::CommandQueue q2;
    q2.Init();
    q2.set_standalone(true);
    q2.set_disconnected(false);
    std::vector<u8> got = TransferSaveLocally(q2, city);
    CHECK_EQ(got.size(), static_cast<std::size_t>(PadSaveLength(static_cast<u32>(city.size()))));
    CHECK(std::memcmp(got.data(), city.data(), city.size()) == 0);
}

TEST(NetLobbyE2E, RealNetSeedAdvertRoundTrip) {
    if (!assetsPresent()) { CHECK(true); return; }   // clean skip
    shim::DiskFileSystem fs(gameDir());

    // A real saved-network seed (.NET) backs a kind-4 "saved game" advert. We use
    // its presence + size as the join profile's player count proxy, build the SAV
    // advert, and round-trip it through encode/decode (the discovery accept path).
    std::vector<u8> netSeed = readFile(fs, "Resources/gamedata/Cities/AUGSBURG.NET");
    CHECK(netSeed.size() > 0);

    SaveProfileHeader h;
    h.name = "AUGSBURG";
    h.playerCount = static_cast<u8>(2);
    h.scenarioWord = 0;
    u16 flags = 0;
    LobbyAdvert a = BuildSaveJoinAdvert(h, &flags);
    CHECK_EQ(a.kind, static_cast<u16>(kAdKindSav));   // 4
    CHECK_EQ(flags, static_cast<u16>(0x50));          // HOST | LoadNetSave

    std::vector<u8> blob = EncodeLobbyAdvert(a);
    CHECK_EQ(blob.size(), static_cast<std::size_t>(kSaveAdvertBytes)); // 106
    LobbyAdvert back;
    CHECK(DecodeLobbyAdvert(blob, back));
    CHECK_EQ(back.name, std::string("AUGSBURG"));
    CHECK_EQ(back.kind, static_cast<u16>(kAdKindSav));
}
