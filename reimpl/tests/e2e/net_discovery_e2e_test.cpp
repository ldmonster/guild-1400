#include "test.h"
#include "net/discovery.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::net;

// End-to-end LAN discovery flow over an in-process datagram bus: several hosts
// open broadcast sockets and advertise; a client runs DiscoverServers and must
// collect them deterministically, with byte-exact 128-byte entry images. No OS.
namespace {

struct DgBus {
    std::vector<std::pair<u16, Datagram>> queues;  // (dstPort, datagram) FIFO
};

class LoopbackDatagram : public INetDatagram {
public:
    LoopbackDatagram(DgBus* bus, std::string ip) : bus_(bus), ip_(std::move(ip)) {}
    bool open(u16 bindPort, bool) override { bound_ = true; bindPort_ = bindPort; return true; }
    void close() override { bound_ = false; }
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
    DgBus* bus_;
    std::string ip_;
    bool bound_ = false;
    u16 bindPort_ = 0;
};

struct StepClock { u32 t = 0; u32 step = 0; };
u32 stepNow(void* user) {
    auto* c = static_cast<StepClock*>(user);
    u32 v = c->t; c->t += c->step; return v;
}

// Build the host-side advert blob (matches MakeAdvert in the unit test): a valid
// header (magic=1, tag=106) of the requested kind, padded with `fill`.
std::vector<u8> Advert(u16 kind, std::size_t total, u8 fill) {
    std::vector<u8> b(total, fill);
    b[0] = 1; b[1] = 0;
    b[2] = static_cast<u8>(kind); b[3] = static_cast<u8>(kind >> 8);
    b[4] = 106; b[5] = 0;
    return b;
}

// Run one full host-advertise -> client-discover round and return the entries.
std::vector<DiscoveredServer> RunRound(DgBus& bus) {
    // Two hosts open a broadcast socket and advertise themselves.
    LoopbackDatagram sockA(&bus, "192.168.5.1");
    LoopbackDatagram sockB(&bus, "192.168.5.2");
    BroadcastAdvertiser hostA(&sockA);
    BroadcastAdvertiser hostB(&sockB);
    CHECK(hostA.OpenBroadcastSocket(kDiscoveryPort));
    CHECK(hostB.OpenBroadcastSocket(kDiscoveryPort));

    auto adA = Advert(kAdKindOne, 50, 0xC1);   // single-game host
    auto adB = Advert(kAdKindSav, 120, 0xD2);  // saved-game host
    hostA.SendBroadcast(adA.data(), adA.size());
    hostB.SendBroadcast(adB.data(), adB.size());
    hostA.SendBroadcast(adA.data(), adA.size());  // host A re-advertises (dup)

    hostA.CloseBroadcastSocket();
    hostB.CloseBroadcastSocket();

    LoopbackDatagram client(&bus, "0.0.0.0");
    StepClock clk{0, 1};
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, kDiscoveryWindow,
                            /*maxServers*/ 8, out, stepNow, &clk, /*tickMs*/ 1);
    CHECK_EQ(n, 2);                       // dup of host A folded out
    return out;
}

} // namespace

TEST(NetDiscoveryE2E, HostAdvertiseClientDiscover) {
    DgBus bus;
    std::vector<DiscoveredServer> srv = RunRound(bus);

    CHECK_EQ(srv.size(), static_cast<std::size_t>(2));
    CHECK_EQ(srv[0].ip, std::string("192.168.5.1"));
    CHECK_EQ(srv[0].type, static_cast<u32>(kEntryTypeOne));  // 63
    CHECK_EQ(srv[1].ip, std::string("192.168.5.2"));
    CHECK_EQ(srv[1].type, static_cast<u32>(kEntryTypeSav));  // 127

    // Byte-exact 128-byte entry image for the single-game host.
    std::vector<u8> e0 = EncodeEntry(srv[0]);
    CHECK_EQ(e0.size(), static_cast<std::size_t>(128));
    CHECK_EQ(std::string(reinterpret_cast<const char*>(e0.data())),
             std::string("192.168.5.1"));
    CHECK_EQ(e0[124], static_cast<u8>(63));
    // advert payload landed at +16; the first header bytes (magic/kind/tag) carry.
    CHECK_EQ(e0[16 + 0], static_cast<u8>(1));    // magic lo
    CHECK_EQ(e0[16 + 4], static_cast<u8>(106));  // tag lo
}

TEST(NetDiscoveryE2E, Deterministic) {
    // Two independent rounds must yield identical entry images: the codec and the
    // discovery loop are deterministic (no wall-clock / no real socket).
    DgBus bus1; DgBus bus2;
    std::vector<DiscoveredServer> a = RunRound(bus1);
    std::vector<DiscoveredServer> b = RunRound(bus2);
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::vector<u8> ea = EncodeEntry(a[i]);
        std::vector<u8> eb = EncodeEntry(b[i]);
        CHECK_EQ(ea.size(), eb.size());
        CHECK(std::memcmp(ea.data(), eb.data(), ea.size()) == 0);
    }
}

TEST(NetDiscoveryE2E, StatsRoundTrip) {
    // The host advertises, the client tallies what it accepts into NetStats, and
    // the formatted report reflects the totals (summary line + 96 per-type lines).
    DgBus bus;
    std::vector<DiscoveredServer> srv = RunRound(bus);

    NetStats st;
    st.ResetStatistics();
    for (const auto& s : srv) {
        ++st.recvPackets;
        st.recvBytes += static_cast<u32>(s.blob.size());
    }
    std::vector<std::string> lines = st.FormatStatistics();
    CHECK_EQ(lines.size(), static_cast<std::size_t>(97));
    CHECK_EQ(lines[0],
        std::string("Recv: 2 Packets w/ ")
        + std::to_string(srv[0].blob.size() + srv[1].blob.size())
        + " bytes, Send: 0 Packets w/ 0 bytes");
}
