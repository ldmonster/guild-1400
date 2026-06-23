#include "net/transport.h"
#include "shim/INetSocket.h"
#include "sim/command.h"   // kPacketStride (153) — the valid-frame buffer capacity
#include "test.h"

#include <cstring>
#include <deque>
#include <vector>

using namespace guild;
using namespace guild::net;
using guild::sim::kPacketStride;   // 153 — the largest valid on-wire frame

// ---------------------------------------------------------------------------
// Mock INetSocket backed by an in-memory byte pipe. send() appends to `out`,
// recv() drains from `in`. With no data, recv() returns 0 (would-block), exactly
// as the shim contract models a non-blocking socket. A per-call throttle lets a
// test deliver/accept the stream one byte at a time.
// ---------------------------------------------------------------------------
class PipeSocket : public shim::INetSocket {
public:
    std::deque<u8> in;          // bytes the transport will recv()
    std::deque<u8> out;         // bytes the transport has send()
    bool open = true;
    int recv_chunk = 0;         // 0 = unlimited; else cap per recv()
    int send_chunk = 0;         // 0 = unlimited; else cap per send()

    bool connect(const char*, std::uint16_t) override { open = true; return true; }
    void close() override { open = false; }
    bool connected() const override { return open; }

    int send(const void* data, std::size_t n) override {
        if (!open) return -1;
        std::size_t cap = n;
        if (send_chunk > 0 && cap > (std::size_t)send_chunk) cap = send_chunk;
        const u8* p = static_cast<const u8*>(data);
        for (std::size_t i = 0; i < cap; ++i) out.push_back(p[i]);
        return (int)cap;
    }
    int recv(void* dst, std::size_t n) override {
        if (!open) return -1;
        if (in.empty()) return 0;   // would-block
        std::size_t cap = n;
        if (recv_chunk > 0 && cap > (std::size_t)recv_chunk) cap = recv_chunk;
        if (cap > in.size()) cap = in.size();
        u8* p = static_cast<u8*>(dst);
        for (std::size_t i = 0; i < cap; ++i) { p[i] = in.front(); in.pop_front(); }
        return (int)cap;
    }
};

namespace {

// Build a framed packet into `buf` and return its total length.
// Pump ReceivePacket on `t` (re-arming the buffer each call) until exactly one
// whole packet is reassembled. CompletedThisCall() is a sticky flag that is
// reset at the start of every pump call, so we must pump-then-check (do-while),
// otherwise a still-set flag from the previous packet would skip the next loop.
bool RecvOnePacket(NetTransport& t, u8* rx, int guard = 100000) {
    while (guard-- > 0) {
        t.SetRecvBuffer(rx);
        t.ReceivePacket();
        if (t.CompletedThisCall()) return true;
    }
    return false;
}

u16 MakePacket(u8* buf, u8 type, u16 bodyExtra, u32 cmdId, u32 count) {
    // total length = header span we touch (0x10) + 1 sync byte + bodyExtra,
    // but never less than the 3-byte minimum frame. Keep small & deterministic.
    u16 total = static_cast<u16>(kOffSync + 1 + bodyExtra);
    if (total < kHeaderBytes) total = kHeaderBytes;
    std::memset(buf, 0, total);
    EncodeHeader(buf, type, total, cmdId, count);
    // fill body region with a recognizable pattern
    for (u16 i = kOffSync; i < total; ++i) buf[i] = static_cast<u8>(0xA0 + (i & 0x0F));
    return total;
}

} // namespace

// --- Framing: encode -> exact header bytes -> decode identical --------------
TEST(NetTransport, FramingHeaderBytes) {
    u8 buf[256];
    u16 total = EncodeHeader(buf, 0x20, 0x0011, 0x12345678u, 0x00ABCDEFu);
    CHECK_EQ(total, (u16)0x0011);
    // type @+0
    CHECK_EQ(buf[0], (u8)0x20);
    // u16 len little-endian @+1,+2
    CHECK_EQ(buf[1], (u8)0x11);
    CHECK_EQ(buf[2], (u8)0x00);
    // flag @+3 zeroed
    CHECK_EQ(buf[3], (u8)0x00);
    // cmdId u32 LE @+4
    CHECK_EQ(buf[4], (u8)0x78);
    CHECK_EQ(buf[5], (u8)0x56);
    CHECK_EQ(buf[6], (u8)0x34);
    CHECK_EQ(buf[7], (u8)0x12);
    // Count u32 LE @+8
    CHECK_EQ(buf[8],  (u8)0xEF);
    CHECK_EQ(buf[9],  (u8)0xCD);
    CHECK_EQ(buf[10], (u8)0xAB);
    CHECK_EQ(buf[11], (u8)0x00);
    // extra @+12 zeroed
    CHECK_EQ(buf[12], (u8)0x00);
    CHECK_EQ(buf[15], (u8)0x00);

    // decode back identical
    CHECK_EQ(HdrType(buf),  (u8)0x20);
    CHECK_EQ(HdrLen(buf),   (u16)0x0011);
    CHECK_EQ(HdrCmdId(buf), (u32)0x12345678u);
    CHECK_EQ(HdrCount(buf), (u32)0x00ABCDEFu);
}

// --- SendPacket: whole-frame flush and stats --------------------------------
TEST(NetTransport, SendWholeFrame) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 pkt[256];
    u16 total = MakePacket(pkt, 0x05, 4, 1u, 1u);
    t.SetSendBuffer(pkt);
    PumpResult r = t.SendPacket();
    CHECK(r == PumpResult::Progress);
    CHECK(t.CompletedThisCall());
    CHECK_EQ((u16)s.out.size(), total);
    // exact bytes on the wire match the framed packet
    for (u16 i = 0; i < total; ++i) CHECK_EQ(s.out[i], pkt[i]);
    // tx stats accounted for type 5
    CHECK_EQ(t.stats().tx[5].count, (u32)1);
    CHECK_EQ(t.stats().tx[5].bytes, (u32)total);
    // cursor reset, buffer cleared (sending again is a no-op)
    CHECK_EQ(t.send_cursor(), (u16)0);
}

// --- SendPacket: partial send (1 byte at a time) reassembles on the wire -----
TEST(NetTransport, SendPartialByteByByte) {
    PipeSocket s;
    s.send_chunk = 1;            // force 1 byte per send()
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 pkt[256];
    u16 total = MakePacket(pkt, 0x07, 8, 2u, 2u);
    t.SetSendBuffer(pkt);

    int guard = 0;
    while (!t.CompletedThisCall() && guard++ < 10000) {
        PumpResult r = t.SendPacket();
        CHECK(r == PumpResult::Progress);
    }
    CHECK(t.CompletedThisCall());
    CHECK_EQ((u16)s.out.size(), total);
    for (u16 i = 0; i < total; ++i) CHECK_EQ(s.out[i], pkt[i]);
}

// --- ReceivePacket: partial-read reassembly, 1 byte at a time ----------------
TEST(NetTransport, RecvPartialByteByByte) {
    PipeSocket s;
    s.recv_chunk = 1;           // deliver the stream 1 byte per recv()
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 pkt[256];
    u16 total = MakePacket(pkt, 0x09, 6, 1u, 1u);
    for (u16 i = 0; i < total; ++i) s.in.push_back(pkt[i]);

    u8 rx[256];
    CHECK(RecvOnePacket(t, rx));
    // reassembled bytes identical to what was framed
    for (u16 i = 0; i < total; ++i) CHECK_EQ(rx[i], pkt[i]);
    // first command Count==1 with last_req==0 => Advance
    CHECK(t.last_event() == SeqEvent::Advance);
    CHECK_EQ(t.last_req_count(), (u32)1);
    CHECK_EQ(t.stats().rx[9].count, (u32)1);
    CHECK_EQ(t.stats().rx[9].bytes, (u32)total);
}

// --- ReceivePacket: multiple back-to-back packets in one buffer --------------
TEST(NetTransport, RecvBackToBack) {
    PipeSocket s;                // unlimited recv
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 a[256], b[256], c[256];
    u16 la = MakePacket(a, 0x10, 2, 10u, 1u);
    u16 lb = MakePacket(b, 0x11, 5, 11u, 2u);
    u16 lc = MakePacket(c, 0x12, 0, 12u, 3u);
    for (u16 i = 0; i < la; ++i) s.in.push_back(a[i]);
    for (u16 i = 0; i < lb; ++i) s.in.push_back(b[i]);
    for (u16 i = 0; i < lc; ++i) s.in.push_back(c[i]);

    u8 rx[256];
    // packet 1
    CHECK(RecvOnePacket(t, rx));
    for (u16 i = 0; i < la; ++i) CHECK_EQ(rx[i], a[i]);
    CHECK_EQ(t.last_req_count(), (u32)1);
    CHECK(t.last_event() == SeqEvent::Advance);
    // packet 2
    CHECK(RecvOnePacket(t, rx));
    for (u16 i = 0; i < lb; ++i) CHECK_EQ(rx[i], b[i]);
    CHECK_EQ(t.last_req_count(), (u32)2);
    // packet 3
    CHECK(RecvOnePacket(t, rx));
    for (u16 i = 0; i < lc; ++i) CHECK_EQ(rx[i], c[i]);
    CHECK_EQ(t.last_req_count(), (u32)3);
}

// --- Sequence-gap detection -------------------------------------------------
TEST(NetTransport, SequenceGapDetection) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));
    u8 rx[256];

    auto deliver = [&](u8 type, u32 cmdId, u32 count) {
        u8 p[256];
        u16 len = MakePacket(p, type, 0, cmdId, count);
        for (u16 i = 0; i < len; ++i) s.in.push_back(p[i]);
        CHECK(RecvOnePacket(t, rx));
    };

    // in-order 1,2 -> Advance
    deliver(0x05, 100u, 1u);
    CHECK(t.last_event() == SeqEvent::Advance);
    deliver(0x05, 100u, 2u);
    CHECK(t.last_event() == SeqEvent::Advance);
    CHECK_EQ(t.last_req_count(), (u32)2);

    // skip 3 -> Count 5 is a gap -> Lost, resync last_req to 5
    deliver(0x05, 100u, 5u);
    CHECK(t.last_event() == SeqEvent::Lost);
    CHECK_EQ(t.last_req_count(), (u32)5);

    // a non-command packet (cmdId == -1) does not touch sequence state
    deliver(0x05, kNoCmdId, 999u);
    CHECK(t.last_event() == SeqEvent::None);
    CHECK_EQ(t.last_req_count(), (u32)5);

    // Sync packet (type 0x20, marker 0x0E) out of sequence -> Sync event.
    // MakePacket fills byte[+0x10] with a pattern (not 0x0E), so force the marker.
    t.set_last_sync_count(0u);
    {
        u8 p[256];
        u16 len = MakePacket(p, kSyncType, 0, 100u, 60u);
        p[kOffSync] = kSyncMarker;     // force the sync marker byte
        for (u16 i = 0; i < len; ++i) s.in.push_back(p[i]);
        CHECK(RecvOnePacket(t, rx));
        CHECK(t.last_event() == SeqEvent::Sync);
    }

    // dup-of-sync: Count == last_sync_count -> DupSync
    t.set_last_sync_count(777u);
    deliver(0x05, 100u, 777u);
    CHECK(t.last_event() == SeqEvent::DupSync);
}

// ===========================================================================
// Malformed / hostile inbound frames (wave-11 hardening, ASAN-exercised).
// The peer's bytes are untrusted; a corrupt on-wire length must never make the
// transport write past the reassembly buffer. The recv buffers below are sized
// exactly to the largest valid frame so ASAN flags any overrun.
// ===========================================================================

// A frame whose declared length is SMALLER than its own 3-byte header. Without a
// guard, stage-2 computes recv(buf+3, total-3) where (total-3) underflows to a
// ~64K length and writes far past rx. The transport must fail safe (Closed),
// touching no memory beyond the buffer.
TEST(NetTransport, RecvDeclaredLengthBelowHeaderUnderflowGuard) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    // Header declaring total length == 1 (< kHeaderBytes==3), then a flood of body
    // bytes a buggy parser would slurp.
    s.in.push_back(0x05);            // type
    s.in.push_back(0x01);            // len lo == 1
    s.in.push_back(0x00);            // len hi
    for (int i = 0; i < 4096; ++i) s.in.push_back(0xCC);

    u8 rx[kPacketStride];            // exactly the 153-byte valid-frame capacity
    std::memset(rx, 0, sizeof(rx));
    t.SetRecvBuffer(rx, sizeof(rx));
    PumpResult r = t.ReceivePacket();
    CHECK(r == PumpResult::Closed);  // malformed -> teardown, no OOB
    CHECK(!t.CompletedThisCall());
    CHECK(t.disconnected());
}

// total == 0 (the 0-length declared frame): same underflow class, must fail safe.
TEST(NetTransport, RecvZeroDeclaredLengthGuard) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));
    s.in.push_back(0x05);
    s.in.push_back(0x00);            // len == 0
    s.in.push_back(0x00);
    for (int i = 0; i < 512; ++i) s.in.push_back(0xEE);

    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    t.SetRecvBuffer(rx, sizeof(rx));
    CHECK(t.ReceivePacket() == PumpResult::Closed);
    CHECK(t.disconnected());
}

// A frame whose declared length EXCEEDS the reassembly buffer capacity. Stage-2
// would recv(buf+3, total-3) writing past the 153-byte buffer. With the capacity
// passed to SetRecvBuffer the transport refuses the frame.
TEST(NetTransport, RecvDeclaredLengthExceedsBufferGuard) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    const u16 huge = 0xFFFF;         // 65535, far beyond the 153-byte buffer
    s.in.push_back(0x05);
    s.in.push_back(static_cast<u8>(huge & 0xFF));
    s.in.push_back(static_cast<u8>(huge >> 8));
    for (int i = 0; i < 8192; ++i) s.in.push_back(0xAB);  // would-be body flood

    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    t.SetRecvBuffer(rx, sizeof(rx));
    CHECK(t.ReceivePacket() == PumpResult::Closed);
    CHECK(t.disconnected());
}

// The maximum valid frame (total == buffer capacity) must still pass intact — the
// guard rejects only frames strictly larger than the buffer, never a boundary fit.
TEST(NetTransport, RecvMaxValidFrameAtCapacityStillCompletes) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 pkt[kPacketStride];
    std::memset(pkt, 0, sizeof(pkt));
    // A frame exactly kPacketStride (153) bytes long, a non-command (cmdId == -1)
    // so no sequence side effects.
    EncodeHeader(pkt, 0x05, kPacketStride, kNoCmdId, 0u);
    for (u16 i = kOffSync; i < kPacketStride; ++i) pkt[i] = static_cast<u8>(i);
    for (u16 i = 0; i < kPacketStride; ++i) s.in.push_back(pkt[i]);

    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    CHECK(RecvOnePacket(t, rx));     // re-arms with cap==0 each call (legacy path)
    for (u16 i = 0; i < kPacketStride; ++i) CHECK_EQ(rx[i], pkt[i]);
    CHECK(!t.disconnected());
}

// A 0-length read (recv returns would-block with no data) must make no progress
// and never advance the cursor or complete a packet.
TEST(NetTransport, RecvZeroLengthReadIsWouldBlock) {
    PipeSocket s;                    // empty in -> recv returns 0 (would-block)
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));
    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    t.SetRecvBuffer(rx, sizeof(rx));
    CHECK(t.ReceivePacket() == PumpResult::Progress);
    CHECK(!t.CompletedThisCall());
    CHECK_EQ(t.recv_cursor(), (u16)0);
    CHECK(!t.disconnected());
}

// A truncated frame: the header declares a valid length but only part of the body
// ever arrives. The transport must keep assembling (Progress), never complete, and
// never read past what it was given.
TEST(NetTransport, RecvTruncatedBodyNeverCompletes) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));

    u8 pkt[64];
    u16 total = MakePacket(pkt, 0x05, 20, kNoCmdId, 0u);  // a >40-byte frame
    // Deliver header + only half the body, then stop (the rest never comes).
    u16 delivered = static_cast<u16>(kHeaderBytes + (total - kHeaderBytes) / 2);
    for (u16 i = 0; i < delivered; ++i) s.in.push_back(pkt[i]);

    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    for (int i = 0; i < 50; ++i) {
        t.SetRecvBuffer(rx, sizeof(rx));
        CHECK(t.ReceivePacket() == PumpResult::Progress);
        CHECK(!t.CompletedThisCall());   // never completes on a truncated frame
    }
    CHECK_EQ(t.recv_cursor(), delivered); // assembled exactly what arrived, no more
}

// A header-only stream (exactly 3 bytes of a frame that declares a longer body):
// completes the header stage, then waits for the body without overrunning.
TEST(NetTransport, RecvHeaderOnlyWaitsForBody) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));
    // Declare a 32-byte frame but deliver only the 3 header bytes.
    s.in.push_back(0x05);
    s.in.push_back(0x20);   // len == 32
    s.in.push_back(0x00);

    u8 rx[kPacketStride];
    std::memset(rx, 0, sizeof(rx));
    t.SetRecvBuffer(rx, sizeof(rx));
    CHECK(t.ReceivePacket() == PumpResult::Progress);
    CHECK(!t.CompletedThisCall());
    CHECK_EQ(t.recv_cursor(), (u16)3);    // header read, body pending
}

// --- Disconnect marks the transport closed ----------------------------------
TEST(NetTransport, DisconnectClosesPumps) {
    PipeSocket s;
    NetTransport t(&s);
    CHECK(t.ConnectToServer("loopback", 1234));
    CHECK(t.connected());
    t.Disconnect();
    CHECK(t.disconnected());
    CHECK(!t.connected());
    // pumps now report Closed
    u8 rx[256];
    t.SetRecvBuffer(rx);
    CHECK(t.ReceivePacket() == PumpResult::Closed);
    u8 tx[256]; MakePacket(tx, 1, 0, 1u, 1u);
    t.SetSendBuffer(tx);
    CHECK(t.SendPacket() == PumpResult::Closed);
}
