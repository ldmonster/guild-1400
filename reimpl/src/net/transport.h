#pragma once
#include "guild/common/types.h"
#include "shim/INetSocket.h"

// wsock32 TCP client transport (module prefix VIBE_Net_*) that feeds the
// Command lockstep system. The original is a single global TCP client; here the
// state lives in a NetTransport instance and all socket I/O is routed through
// shim::INetSocket. The framing / partial-I/O / sequence logic is translated 1:1.
//
//   VIBE_Net_ConnectToServer       @0x43b51c  (sockopt tuning; socket I/O via shim)
//   VIBE_Net_Disconnect            @0x43b868
//   VIBE_Net_SendPacket            @0x43bc54  (partial-send aware flush)
//   VIBE_Net_ReceivePacket         @0x43b8d0  (partial-read 2-stage reassembly + seq gap)
//   VIBE_Net_AccumulatePacketStats @0x43b260
//
// PACKET HEADER / record layout (recovered from VIBE_Command_EnqueuePacket @0x49388c
// and VIBE_Net_ReceivePacket @0x43b8d0). The on-wire framing is the first 3 bytes;
// the remaining header fields are read by the transport only for sequencing.
//   +0x00  u8   type/opcode        (0x20 + [+0x10]==0x0E => Sync)
//   +0x01  u16  total length       (entire packet incl. header; the frame size)
//   +0x03  u8   flag               (set 0 by encoder)
//   +0x04  u32  cmdId / owner      (-1 = none / not a command)
//   +0x08  u32  Count              (sequence number)
//   +0x0C  u32  pending/extra      (set 0 by encoder)
//   +0x10  u8   sync marker byte   (0x0E with type 0x20 => Sync)
//   ...    record stride is 0x99 (153) bytes in the original send queue.
//
// On-wire framing the transport itself depends on: byte 0 = type, bytes 1..2 =
// little-endian u16 total length. recv() reads exactly that many bytes total.

namespace guild::net {

// Header field byte offsets (see table above). Used by both encode helpers and
// the receive-side sequence logic, mirroring the raw +0xNN accesses in the IDB.
enum PacketOffset : u32 {
    kOffType   = 0x00,  // +0x00  u8  type/opcode
    kOffLen    = 0x01,  // +0x01  u16 total length (frame size)
    kOffFlag   = 0x03,  // +0x03  u8  flag (0)
    kOffCmdId  = 0x04,  // +0x04  u32 cmdId/owner (-1 = none)
    kOffCount  = 0x08,  // +0x08  u32 Count (sequence)
    kOffExtra  = 0x0C,  // +0x0C  u32 pending/extra (0)
    kOffSync   = 0x10,  // +0x10  u8  sync marker
};

// Minimum frame: the 3-byte on-wire header [type][u16 len].
constexpr u16 kHeaderBytes = 3;
// Sync packet discriminator: type==0x20 (32) && byte[+0x10]==0x0E (14).
constexpr u8 kSyncType   = 0x20;
constexpr u8 kSyncMarker = 0x0E;
// Sentinel for "no command" in the cmdId field (+0x04).
constexpr u32 kNoCmdId = 0xFFFFFFFFu;
// WinSock WSAEWOULDBLOCK; the shim mirrors this as recv/send returning 0.
constexpr int kWsaWouldBlock = 10035;

// Result of a transport send/recv pump step. Mirrors the original int returns:
//   1 = transport closed / fatal (socket invalid or torn down)
//   0 = progress made or would-block (call again later)
//   The "completed a whole packet" event is reported via the callbacks below.
enum class PumpResult { Progress = 0, Closed = 1 };

// Direction tag for AccumulatePacketStats (a3: 0 = tx, 1 = rx in the original).
enum class StatDir : u8 { Tx = 0, Rx = 1 };

// Sequence-gap event kinds emitted by ReceivePacket's sequencing branch.
// These correspond 1:1 to the four nt_Recv() diagnostic strings in the binary.
enum class SeqEvent {
    None,        // not a command (cmdId == -1): no sequence processing
    Advance,     // Count == LastRequCount + 1: in-order, advance
    DupSync,     // Count == cm_LastSyncCount: duplicate-sync diagnostic
    Sync,        // type 0x20 / marker 0x0E: Sync packet diagnostic
    Lost,        // gap detected ("Lost a Command"): resync to Count
};

// Per-type packet/byte counters. The original keeps two arrays of 96 entries
// indexed by packet type (byte[0]); each entry holds {count, bytes} per dir.
// dword_764CF8 (counts) / dword_764CFC (bytes), stride 16, dir offset 4*{0,2}.
struct PacketStats {
    // index = type (0..95); [0]=rx slot, [1]=tx slot mirrors original 4*{0|2}.
    struct Slot { u32 count = 0; u32 bytes = 0; };
    Slot rx[96];
    Slot tx[96];
};

class NetTransport {
public:
    explicit NetTransport(shim::INetSocket* sock) : sock_(sock) {}

    // VIBE_Net_ConnectToServer @0x43b51c — establish the single TCP client and
    // apply the tuned sockopts. Resets all buffers/cursors/counters. Returns
    // true on success (socket connected), false on failure (mirrors !=-1).
    // The original applies: SO_REUSEADDR, SO_DONTLINGER, SO_RCVBUF=0x40000,
    // SO_SNDBUF=0x20000, and FIONBIO (non-blocking). The shim guarantees a
    // non-blocking, tuned socket, so those tunables are documented constants here.
    bool ConnectToServer(const char* host, u16 port);

    // VIBE_Net_Disconnect @0x43b868 — tear down the client, mark disconnected.
    void Disconnect();

    // Hand the transport the buffer to flush (the Command layer owns this memory
    // in the original via dword_764CE8). `buf` points at a framed packet whose
    // bytes[1..2] give the total length. Pass nullptr/len 0 to mean "nothing".
    void SetSendBuffer(u8* buf) { tx_buf_ = buf; }
    // Buffer ReceivePacket reassembles into (dword_764CE4). Must be large enough
    // for the largest frame (<= record stride 0x99 in practice). `cap` is the
    // capacity of `buf` in bytes; pass it so ReceivePacket can refuse a frame whose
    // declared on-wire length (HdrLen) would run past the buffer. cap==0 means
    // "capacity unknown" — the only remaining guard is the structural total<3 check
    // (a frame can never be smaller than its own 3-byte header) which is safe on
    // every valid frame and never fires on the in-bounds path. Callers that know the
    // buffer size SHOULD pass it; the original's reassembly buffer is the 153-byte
    // command record (kPacketStride), so a valid frame is always in [3, cap].
    void SetRecvBuffer(u8* buf, u16 cap = 0) { rx_buf_ = buf; rx_cap_ = cap; }

    // VIBE_Net_SendPacket @0x43bc54 — flush the send buffer. Partial-send aware:
    // advances the cursor and returns Progress until the whole frame is out, then
    // accounts stats and clears the buffer. WouldBlock => Progress (retry later).
    PumpResult SendPacket();

    // VIBE_Net_ReceivePacket @0x43b8d0 — two-stage partial-read reassembly:
    // stage 1 reads the 3-byte header, stage 2 reads the body up to total length.
    // On a complete packet it runs the sequence-gap logic and accounts rx stats.
    // The last completed packet (if any) is exposed via Completed*/last_event().
    PumpResult ReceivePacket();

    // True for one pump after a whole packet was reassembled this call.
    bool CompletedThisCall() const { return completed_this_call_; }
    // Sequence event classified for the last completed rx packet.
    SeqEvent last_event() const { return last_event_; }

    // VIBE_Net_AccumulatePacketStats @0x43b260 — fold a finished packet's
    // {type, len} into the per-type counters. a2>type-byte && type<96 guard.
    void AccumulatePacketStats(const u8* pkt, u32 len, StatDir dir);

    // --- inspectable transport state (the original globals) -----------------
    bool connected() const { return sock_ && sock_->connected() && !disconnected_; }
    bool disconnected() const { return disconnected_; }   // dword_764CF0
    u16  send_cursor() const { return tx_cursor_; }        // word_764CEE
    u16  recv_cursor() const { return rx_cursor_; }        // word_764CEC
    u32  last_req_count() const { return last_req_count_; } // dword_7652F8
    u32  last_sync_count() const { return last_sync_count_; } // dword_11AA470
    void set_last_sync_count(u32 c) { last_sync_count_ = c; }
    const PacketStats& stats() const { return stats_; }

private:
    void Teardown();  // shutdown + close + mark disconnected (shared error path)

    shim::INetSocket* sock_ = nullptr;

    u8*  tx_buf_ = nullptr;   // dword_764CE8 — outbound frame (Command-owned)
    u8*  rx_buf_ = nullptr;   // dword_764CE4 — inbound reassembly buffer
    u16  rx_cap_ = 0;         // capacity of rx_buf_ in bytes (0 == unknown)
    u16  tx_cursor_ = 0;      // word_764CEE — bytes already sent of current frame
    u16  rx_cursor_ = 0;      // word_764CEC — bytes already received of current frame

    bool disconnected_ = false;  // dword_764CF0

    u32  last_req_count_  = 0;    // dword_7652F8 — nt_LastRequCount
    u32  last_sync_count_ = 0;    // dword_11AA470 — cm_LastSyncCount

    bool     completed_this_call_ = false;
    SeqEvent last_event_ = SeqEvent::None;

    PacketStats stats_;

    // Byte/packet telemetry counters (dword_62E5D8 rx bytes, dword_62E5E0 tx bytes).
    u32  rx_bytes_total_ = 0;
    u32  tx_bytes_total_ = 0;
};

// --- framing helpers (encode/decode the 3-byte on-wire header) --------------
// These are not distinct functions in the binary (the Command layer writes the
// fields inline, see VIBE_Command_EnqueuePacket); provided here so tests and the
// Command layer share one definition of the header layout.

// Write the on-wire framing + sequencing header into `buf`. Returns total length.
u16 EncodeHeader(u8* buf, u8 type, u16 totalLen, u32 cmdId, u32 count);

inline u8  HdrType(const u8* p)  { return p[kOffType]; }
inline u16 HdrLen(const u8* p)   { return static_cast<u16>(p[kOffLen] | (p[kOffLen + 1] << 8)); }
inline u32 HdrCmdId(const u8* p) {
    return static_cast<u32>(p[kOffCmdId]) | (static_cast<u32>(p[kOffCmdId + 1]) << 8)
         | (static_cast<u32>(p[kOffCmdId + 2]) << 16) | (static_cast<u32>(p[kOffCmdId + 3]) << 24);
}
inline u32 HdrCount(const u8* p) {
    return static_cast<u32>(p[kOffCount]) | (static_cast<u32>(p[kOffCount + 1]) << 8)
         | (static_cast<u32>(p[kOffCount + 2]) << 16) | (static_cast<u32>(p[kOffCount + 3]) << 24);
}

} // namespace guild::net
