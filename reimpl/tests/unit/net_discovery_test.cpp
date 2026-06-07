#include "test.h"
#include "net/discovery.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::net;

// ---------------------------------------------------------------------------
// In-process loopback datagram bus (test backend for INetDatagram)
// ---------------------------------------------------------------------------
// A trivial shared switch: senders push (fromIp, bytes) onto a port's queue;
// receivers bound to that port drain it. Models broadcast (sendTo .255 => the
// switch fans the datagram to every bound receiver on the dest port). No OS.
namespace {

struct DgBus {
    // pending[port] = queue of (fromIp, bytes)
    std::vector<std::pair<u16, Datagram>> queues;  // (port, datagram) FIFO
};

class LoopbackDatagram : public INetDatagram {
public:
    LoopbackDatagram(DgBus* bus, std::string ip) : bus_(bus), ip_(std::move(ip)) {}

    bool open(u16 bindPort, bool broadcast) override {
        bound_ = true;
        bindPort_ = bindPort;
        broadcast_ = broadcast;
        return true;
    }
    void close() override { bound_ = false; }

    int sendTo(u16 dstPort, const void* data, std::size_t n) override {
        Datagram dg;
        dg.fromIp = ip_;
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
        return false;  // would-block
    }

private:
    DgBus* bus_;
    std::string ip_;
    bool bound_ = false;
    bool broadcast_ = false;
    u16 bindPort_ = 0;
};

// A monotonic clock that ticks `step` ms per call (drives the timed drain loop).
struct StepClock { u32 t = 0; u32 step = 0; };
u32 stepNow(void* user) {
    auto* c = static_cast<StepClock*>(user);
    u32 v = c->t;
    c->t += c->step;
    return v;
}

// Build a kind-1 (single-game) advert datagram payload: magic=1, kind=1, tag=106,
// then arbitrary info bytes out to >= 42 bytes.
std::vector<u8> MakeAdvert(u16 kind, std::size_t total, u8 fill) {
    std::vector<u8> b(total, fill);
    b[0] = 1; b[1] = 0;                 // magic == 1
    b[2] = static_cast<u8>(kind); b[3] = static_cast<u8>(kind >> 8);
    b[4] = 106; b[5] = 0;              // tag == 106
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// Golden vector: the 128-byte discovery entry image.
// ---------------------------------------------------------------------------
// Reproduces the original's per-server record: IP at +0, blob at +16, type at
// +124, rest zero. Verified against the recovered offsets in DiscoverServers.
TEST(NetDiscovery, EncodeEntryGolden) {
    DiscoveredServer s;
    s.ip = "192.168.0.7";
    s.blob.assign(kAdKindOneBytes, 0xAB);   // 42 bytes
    s.type = kEntryTypeOne;                 // 63

    std::vector<u8> e = EncodeEntry(s);
    CHECK_EQ(e.size(), static_cast<std::size_t>(kEntryStride)); // 128

    // IP string at +0, NUL terminated.
    CHECK_EQ(std::string(reinterpret_cast<const char*>(e.data())), std::string("192.168.0.7"));
    // blob at +16.
    for (std::size_t i = 0; i < kAdKindOneBytes; ++i)
        CHECK_EQ(e[kEntryBlobOff + i], static_cast<u8>(0xAB));
    // type dword at +124 little-endian == 63.
    CHECK_EQ(e[124], static_cast<u8>(63));
    CHECK_EQ(e[125], static_cast<u8>(0));
    CHECK_EQ(e[126], static_cast<u8>(0));
    CHECK_EQ(e[127], static_cast<u8>(0));
    // byte at +12 (between IP and blob) must be zero (leading memset).
    CHECK_EQ(e[12], static_cast<u8>(0));
}

TEST(NetDiscovery, EncodeEntrySavedGameType) {
    DiscoveredServer s;
    s.ip = "10.0.0.1";
    s.blob.assign(kAdKindSavBytes, 0x5A);   // 106 bytes
    s.type = kEntryTypeSav;                 // 127
    std::vector<u8> e = EncodeEntry(s);
    CHECK_EQ(e[124], static_cast<u8>(127));
    // blob occupies +16..+121 (106 bytes); +122/+123 still zero before type dword.
    CHECK_EQ(e[16 + 105], static_cast<u8>(0x5A));
    CHECK_EQ(e[122], static_cast<u8>(0));
    CHECK_EQ(e[123], static_cast<u8>(0));
}

// ---------------------------------------------------------------------------
// DiscoverServers: accept predicate + dedup + type tagging.
// ---------------------------------------------------------------------------
TEST(NetDiscovery, AcceptsValidAdvertsDedupsBySource) {
    DgBus bus;
    LoopbackDatagram client(&bus, "0.0.0.0");

    // Two distinct hosts advertise (kind 1 and kind 4) + a duplicate from host A.
    LoopbackDatagram hostA(&bus, "192.168.1.10");
    LoopbackDatagram hostB(&bus, "192.168.1.20");
    auto adA = MakeAdvert(kAdKindOne, 64, 0x11);
    auto adB = MakeAdvert(kAdKindSav, 128, 0x22);
    hostA.sendTo(kDiscoveryPort, adA.data(), adA.size());
    hostB.sendTo(kDiscoveryPort, adB.data(), adB.size());
    hostA.sendTo(kDiscoveryPort, adA.data(), adA.size());  // duplicate IP

    StepClock clk{0, 1};
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, /*timeoutMs*/ 10000,
                            /*maxServers*/ 8, out, stepNow, &clk, /*tickMs*/ 1);

    CHECK_EQ(n, 2);                       // duplicate of host A dropped
    CHECK_EQ(out.size(), static_cast<std::size_t>(2));
    CHECK_EQ(out[0].ip, std::string("192.168.1.10"));
    CHECK_EQ(out[0].type, static_cast<u32>(kEntryTypeOne));        // 63
    CHECK_EQ(out[0].blob.size(), static_cast<std::size_t>(kAdKindOneBytes)); // 42
    CHECK_EQ(out[1].ip, std::string("192.168.1.20"));
    CHECK_EQ(out[1].type, static_cast<u32>(kEntryTypeSav));        // 127
    CHECK_EQ(out[1].blob.size(), static_cast<std::size_t>(kAdKindSavBytes)); // 106
}

TEST(NetDiscovery, RejectsBadHeaderBytes) {
    DgBus bus;
    LoopbackDatagram client(&bus, "0.0.0.0");
    LoopbackDatagram host(&bus, "1.2.3.4");

    // bad tag (not 106)
    auto bad1 = MakeAdvert(kAdKindOne, 64, 0); bad1[4] = 99;
    // bad magic (not 1)
    auto bad2 = MakeAdvert(kAdKindOne, 64, 0); bad2[0] = 2;
    // bad kind (not 1 or 4)
    auto bad3 = MakeAdvert(3, 64, 0);
    // too short (< 6 bytes)
    std::vector<u8> bad4 = {1, 0, 1};
    host.sendTo(kDiscoveryPort, bad1.data(), bad1.size());
    host.sendTo(kDiscoveryPort, bad2.data(), bad2.size());
    host.sendTo(kDiscoveryPort, bad3.data(), bad3.size());
    host.sendTo(kDiscoveryPort, bad4.data(), bad4.size());

    StepClock clk{0, 1};
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, 10000, 8, out, stepNow, &clk, 1);
    CHECK_EQ(n, 0);
}

TEST(NetDiscovery, MaxServersZeroReturnsMinusTwo) {
    DgBus bus;
    LoopbackDatagram client(&bus, "0.0.0.0");
    StepClock clk{0, 1};
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, 1000, 0, out, stepNow, &clk, 1);
    CHECK_EQ(n, -2);
}

TEST(NetDiscovery, TimeoutTerminatesEmpty) {
    DgBus bus;                            // no adverts queued
    LoopbackDatagram client(&bus, "0.0.0.0");
    StepClock clk{0, 100};               // each poll burns 100ms
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, 300, 8, out, stepNow, &clk, 1);
    CHECK_EQ(n, 0);
}

TEST(NetDiscovery, StopsAtMaxServers) {
    DgBus bus;
    LoopbackDatagram client(&bus, "0.0.0.0");
    for (int i = 0; i < 5; ++i) {
        LoopbackDatagram h(&bus, "10.0.0." + std::to_string(i));
        auto ad = MakeAdvert(kAdKindOne, 64, static_cast<u8>(i));
        h.sendTo(kDiscoveryPort, ad.data(), ad.size());
    }
    StepClock clk{0, 1};
    std::vector<DiscoveredServer> out;
    int n = DiscoverServers(&client, kDiscoveryPort, 100000, /*max*/ 3, out, stepNow, &clk, 1);
    CHECK_EQ(n, 3);                      // found < maxServers gate
}

// ---------------------------------------------------------------------------
// BroadcastAdvertiser: open is lazy/idempotent; send guarded by open.
// ---------------------------------------------------------------------------
TEST(NetDiscovery, AdvertiserOpenIdempotentAndSendGuard) {
    DgBus bus;
    LoopbackDatagram sock(&bus, "host");
    BroadcastAdvertiser adv(&sock);

    // send before open -> guarded no-op (-1).
    u8 msg[8] = {1, 0, 1, 0, 106, 0, 0, 0};
    CHECK_EQ(adv.SendBroadcast(msg, sizeof(msg)), -1);

    CHECK(adv.OpenBroadcastSocket(kDiscoveryPort));
    CHECK(adv.open());
    CHECK_EQ(adv.port(), kDiscoveryPort);
    // second open is a no-op (still open, port unchanged).
    CHECK(adv.OpenBroadcastSocket(0x9999));
    CHECK_EQ(adv.port(), kDiscoveryPort);

    CHECK_EQ(adv.SendBroadcast(msg, sizeof(msg)), static_cast<int>(sizeof(msg)));
    CHECK_EQ(bus.queues.size(), static_cast<std::size_t>(1));
    CHECK_EQ(bus.queues[0].first, kDiscoveryPort);

    adv.CloseBroadcastSocket();
    CHECK(!adv.open());
    CHECK_EQ(adv.SendBroadcast(msg, sizeof(msg)), -1);  // guarded again
}

// ---------------------------------------------------------------------------
// NetStats: Reset zeros everything; Format renders summary + 96 per-type lines.
// ---------------------------------------------------------------------------
TEST(NetDiscovery, StatsResetZeros) {
    NetStats st;
    st.recvPackets = 7; st.recvBytes = 700;
    st.sendPackets = 3; st.sendBytes = 300;
    st.perType[5].recvCount = 9;
    st.perType[95].sendBytes = 11;
    st.ResetStatistics();
    CHECK_EQ(st.recvPackets, 0u);
    CHECK_EQ(st.recvBytes, 0u);
    CHECK_EQ(st.sendPackets, 0u);
    CHECK_EQ(st.sendBytes, 0u);
    CHECK_EQ(st.perType[5].recvCount, 0u);
    CHECK_EQ(st.perType[95].sendBytes, 0u);
}

TEST(NetDiscovery, StatsFormatGolden) {
    NetStats st;
    st.ResetStatistics();
    st.recvPackets = 4; st.recvBytes = 200;
    st.sendPackets = 2; st.sendBytes = 100;
    st.perType[0].recvCount = 1; st.perType[0].recvBytes = 50;
    st.perType[0].sendCount = 1; st.perType[0].sendBytes = 100;

    std::vector<std::string> lines = st.FormatStatistics();
    CHECK_EQ(lines.size(), static_cast<std::size_t>(97));  // summary + 96 entries
    CHECK_EQ(lines[0],
        std::string("Recv: 4 Packets w/ 200 bytes, Send: 2 Packets w/ 100 bytes"));
    // opcode 0: recv 1/4 = 25%, bytes 50/200 = 25%, send 1/2 = 50%, 100/100 = 100%.
    CHECK_EQ(lines[1],
        std::string("0. Recv: 1 Packets (25.000000%) w/ 50 bytes (25.000000%), "
                    "Send: 1 Packets (50.000000%) w/ 100 bytes (100.000000%)"));
    // empty opcode line: zero counts, guarded 0% denominators.
    CHECK_EQ(lines[2],
        std::string("1. Recv: 0 Packets (0.000000%) w/ 0 bytes (0.000000%), "
                    "Send: 0 Packets (0.000000%) w/ 0 bytes (0.000000%)"));
}

// ---------------------------------------------------------------------------
// LocalHostAddress: resolve once, then cache.
// ---------------------------------------------------------------------------
namespace {
int g_resolveCalls = 0;
bool fakeResolve(void*, std::string& out) {
    ++g_resolveCalls;
    out = "127.0.0.1";
    return true;
}
}

TEST(NetDiscovery, LocalHostAddressCaches) {
    g_resolveCalls = 0;
    LocalHostAddress lha;
    CHECK_EQ(lha.Get(fakeResolve, nullptr), std::string("127.0.0.1"));
    CHECK_EQ(lha.Get(fakeResolve, nullptr), std::string("127.0.0.1"));
    CHECK_EQ(g_resolveCalls, 1);          // cached fast path on the 2nd call
    CHECK_EQ(lha.cached(), std::string("127.0.0.1"));
}
