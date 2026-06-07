#include "net/transport.h"
#include "shim/INetSocket.h"
#include "test.h"

#include <cstring>
#include <deque>
#include <memory>
#include <vector>

using namespace guild;
using namespace guild::net;

// ---------------------------------------------------------------------------
// Loopback INetSocket pair: two endpoints sharing a pair of byte queues. What
// endpoint A sends, endpoint B receives, and vice versa — an in-process TCP
// stand-in. recv() with no data returns 0 (would-block), per the shim contract.
// A throttle simulates partial reads/writes to exercise the reassembly path.
// ---------------------------------------------------------------------------
struct Pipe { std::deque<u8> q; };

class LoopbackSocket : public shim::INetSocket {
public:
    LoopbackSocket(Pipe* tx, Pipe* rx) : tx_(tx), rx_(rx) {}
    int chunk = 0;              // 0 = unlimited; else cap bytes per send/recv

    bool connect(const char*, std::uint16_t) override { open_ = true; return true; }
    void close() override { open_ = false; }
    bool connected() const override { return open_; }

    int send(const void* data, std::size_t n) override {
        if (!open_) return -1;
        std::size_t cap = (chunk > 0 && n > (std::size_t)chunk) ? chunk : n;
        const u8* p = static_cast<const u8*>(data);
        for (std::size_t i = 0; i < cap; ++i) tx_->q.push_back(p[i]);
        return (int)cap;
    }
    int recv(void* dst, std::size_t n) override {
        if (!open_) return -1;
        if (rx_->q.empty()) return 0;   // would-block
        std::size_t cap = (chunk > 0 && n > (std::size_t)chunk) ? chunk : n;
        if (cap > rx_->q.size()) cap = rx_->q.size();
        u8* p = static_cast<u8*>(dst);
        for (std::size_t i = 0; i < cap; ++i) { p[i] = rx_->q.front(); rx_->q.pop_front(); }
        return (int)cap;
    }
private:
    Pipe* tx_; Pipe* rx_; bool open_ = false;
};

namespace {

// Vary the body size so packets are different lengths but bounded by the 3-byte
// length field. Body uses a deterministic per-packet pattern for verification.
// Pump one whole packet through send/recv. CompletedThisCall() is sticky and
// reset on each pump call, so pump-then-check (the flag may still be set from a
// prior packet, which would otherwise skip a plain while-guard loop).
bool SendOne(NetTransport& t, u8* buf, int guard = 100000) {
    t.SetSendBuffer(buf);
    while (guard-- > 0) { t.SendPacket(); if (t.CompletedThisCall()) return true; }
    return false;
}
bool RecvOne(NetTransport& t, u8* rx, int guard = 100000) {
    while (guard-- > 0) { t.SetRecvBuffer(rx); t.ReceivePacket(); if (t.CompletedThisCall()) return true; }
    return false;
}

u16 BuildPacket(u8* buf, u8 type, u16 bodyLen, u32 cmdId, u32 count, u8 seed) {
    u16 total = static_cast<u16>(kOffSync + 1 + bodyLen);
    if (total < kHeaderBytes) total = kHeaderBytes;
    std::memset(buf, 0, total);
    EncodeHeader(buf, type, total, cmdId, count);
    for (u16 i = kOffSync; i < total; ++i)
        buf[i] = static_cast<u8>(seed + i);
    return total;
}

} // namespace

// A sends a burst of varied-size command packets; B reassembles them in order
// with the correct sequence numbers. Throttled to 3 bytes/op on both ends so
// every packet crosses multiple recv() calls and spans buffer boundaries.
TEST(NetE2E, LoopbackBurstReassembleInOrder) {
    Pipe a2b, b2a;
    LoopbackSocket sa(&a2b, &b2a);   // A: sends into a2b, recv from b2a
    LoopbackSocket sb(&b2a, &a2b);   // B: sends into b2a, recv from a2b
    sa.chunk = 3;
    sb.chunk = 3;

    NetTransport A(&sa);
    NetTransport B(&sb);
    CHECK(A.ConnectToServer("loopback", 7000));
    CHECK(B.ConnectToServer("loopback", 7000));

    const int N = 12;
    u8 sent[N][512];
    u16 lens[N];
    // varied sizes: bodyLen cycles 0,3,7,...; cmdId fixed; Count 1..N (in order)
    for (int i = 0; i < N; ++i) {
        u16 bodyLen = static_cast<u16>((i * 7) % 41);
        lens[i] = BuildPacket(sent[i], static_cast<u8>(0x30 + (i & 7)),
                              bodyLen, 500u, (u32)(i + 1), static_cast<u8>(i));
    }

    // Drive the flow: A flushes packet i fully, then B pumps until it completes
    // one packet. Interleave A-send/B-recv so the stream is produced and consumed
    // incrementally (the realistic lockstep pump).
    u8 rxbuf[512];
    int received = 0;
    std::vector<u32> recvCounts;
    std::vector<std::vector<u8>> recvBytes;

    int sendIdx = 0;
    int guard = 0;
    while (received < N && guard++ < 1000000) {
        // pump A's send: load next packet if idle, then push some bytes
        if (sendIdx < N) {
            if (A.send_cursor() == 0 && !A.CompletedThisCall()) {
                A.SetSendBuffer(sent[sendIdx]);
            }
            A.SendPacket();
            if (A.CompletedThisCall()) {
                ++sendIdx;
            }
        }
        // pump B's recv
        if (!B.CompletedThisCall()) B.SetRecvBuffer(rxbuf);
        B.ReceivePacket();
        if (B.CompletedThisCall()) {
            recvCounts.push_back(B.last_req_count());
            std::vector<u8> bytes(rxbuf, rxbuf + HdrLen(rxbuf));
            recvBytes.push_back(bytes);
            ++received;
            CHECK(B.last_event() == SeqEvent::Advance);
        }
    }

    CHECK_EQ(received, N);
    // sequence numbers arrived strictly in order 1..N
    for (int i = 0; i < N; ++i) CHECK_EQ(recvCounts[i], (u32)(i + 1));
    // each reassembled packet is byte-identical to what A framed
    for (int i = 0; i < N; ++i) {
        CHECK_EQ((u16)recvBytes[i].size(), lens[i]);
        bool same = (recvBytes[i].size() == lens[i]) &&
                    std::memcmp(recvBytes[i].data(), sent[i], lens[i]) == 0;
        CHECK(same);
    }
    // B advanced its sequence cursor to N
    CHECK_EQ(B.last_req_count(), (u32)N);
}

// Bidirectional: both endpoints exchange a packet and reassemble correctly.
TEST(NetE2E, LoopbackBidirectional) {
    Pipe a2b, b2a;
    LoopbackSocket sa(&a2b, &b2a);
    LoopbackSocket sb(&b2a, &a2b);
    NetTransport A(&sa);
    NetTransport B(&sb);
    CHECK(A.ConnectToServer("h", 1));
    CHECK(B.ConnectToServer("h", 1));

    u8 pa[256], pb[256];
    u16 la = BuildPacket(pa, 0x21, 10, 1u, 1u, 0x11);
    u16 lb = BuildPacket(pb, 0x22, 4,  2u, 1u, 0x22);

    CHECK(SendOne(A, pa));
    CHECK(SendOne(B, pb));

    u8 rb[256], ra[256];
    CHECK(RecvOne(B, rb));
    CHECK(RecvOne(A, ra));

    CHECK_EQ((int)std::memcmp(rb, pa, la), 0);
    CHECK_EQ((int)std::memcmp(ra, pb, lb), 0);
    CHECK_EQ(B.last_req_count(), (u32)1);
    CHECK_EQ(A.last_req_count(), (u32)1);
}
