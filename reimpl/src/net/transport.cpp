#include "net/transport.h"

// Translation notes on the WinSock -> shim::INetSocket mapping
// -----------------------------------------------------------
// The original calls raw recv()/send() and inspects three outcomes:
//   ret > 0           : bytes transferred (advance cursor)
//   ret == 0          : on recv -> peer closed gracefully -> teardown
//                       on send -> never (send returns 0 only for 0-length)
//   ret == -1         : error; WSAGetLastError()==WSAEWOULDBLOCK(10035) -> retry,
//                       any other error -> ReportWinsockError + teardown
//
// shim::INetSocket collapses this to: ">0 bytes", "0 == would-block", "-1 error".
// A non-blocking socket with no data pending returns WSAEWOULDBLOCK in the
// original, which the shim reports as 0 (would-block). The graceful-close case
// (original recv()==0) and hard errors both map to the shim's -1 here, so both
// route through Teardown(), exactly as the original's two close paths do. The
// per-call control flow (header stage, body stage, cursor math, return codes,
// stats accounting, sequence-gap classification) is preserved 1:1.

namespace guild::net {

// gilde.exe 0x43b51c — VIBE_Net_ConnectToServer  (__usercall, eax = host, edx = port)
// The original tunes the socket with SO_REUSEADDR / SO_DONTLINGER /
// SO_RCVBUF=0x40000 / SO_SNDBUF=0x20000 / FIONBIO; the shim owns a tuned
// non-blocking socket, so those become documented constants here. All transport
// state is reset to mirror the prologue stores at 0x43b548..0x43b587.
bool NetTransport::ConnectToServer(const char* host, u16 port) {
    disconnected_   = false;   // dword_764CF0 = 0
    rx_bytes_total_ = 0;       // dword_62E5D8 = 0
    tx_bytes_total_ = 0;       // dword_62E5E0 = 0
    rx_buf_     = nullptr;     // dword_764CE4 = 0
    tx_buf_     = nullptr;     // dword_764CE8 = 0
    tx_cursor_  = 0;           // word_764CEE = 0
    rx_cursor_  = 0;           // word_764CEC = 0

    if (!host)                 // 0x43b590: !a1 -> return -1
        return false;
    if (!sock_)
        return false;
    // socket(2,1,6) + setsockopt(...) + connect + select(writefds, 500ms) in the
    // original. The tuned non-blocking connect is the shim's responsibility.
    return sock_->connect(host, port);
}

// gilde.exe 0x43b868 — VIBE_Net_Disconnect
void NetTransport::Disconnect() {
    if (connected()) {              // dword_764CE0 != -1
        sock_->close();            // shutdown(2) + closesocket
        disconnected_ = true;      // dword_764CF0 = 1
    } else if (sock_) {
        sock_->close();
    }
    disconnected_ = true;
    // WSACleanup() + dword_62E5C4 = 0 in the original (global winsock teardown).
}

// Shared error/close teardown (the repeated shutdown+close+flag block in both
// SendPacket and ReceivePacket, plus the WSACleanup/dword_62E5C4 epilogue).
void NetTransport::Teardown() {
    if (sock_)
        sock_->close();            // shutdown(2) + closesocket; dword_764CE0 = -1
    disconnected_ = true;          // dword_764CF0 = 1
    // WSACleanup() + dword_62E5C4 = 0 follow in the original.
}

// gilde.exe 0x43bc54 — VIBE_Net_SendPacket
// Partial-send aware flush of the framed buffer at tx_buf_. The frame's total
// length is bytes[1..2] (HdrLen). Advances tx_cursor_ by each send(); returns
// Progress (0) until the cursor reaches the length, then accounts tx stats and
// clears the buffer.
PumpResult NetTransport::SendPacket() {
    completed_this_call_ = false;
    if (!sock_ || !sock_->connected() || disconnected_)  // dword_764CE0 == -1
        return PumpResult::Closed;                        // return 1
    if (!tx_buf_)                                         // !dword_764CE8
        return PumpResult::Progress;                      // return 0

    const u16 total = HdrLen(tx_buf_);                    // *(WORD*)(buf+1)
    const int n = sock_->send(tx_buf_ + tx_cursor_,
                              static_cast<u16>(total - tx_cursor_));
    if (n < 0) {                                          // send() == -1
        // WSAEWOULDBLOCK(10035) -> retry (return 0); other error -> teardown.
        // The shim surfaces would-block as 0, so a true -1 is a fatal error.
        Teardown();
        return PumpResult::Closed;                        // return 1
    }
    // n >= 0: 0 == would-block (return 0), >0 advances cursor.
    tx_cursor_ = static_cast<u16>(tx_cursor_ + n);        // word_764CEE += v1
    tx_bytes_total_ += static_cast<u32>(n);               // dword_62E5E0 += v1
    if (tx_cursor_ != total)                              // word_764CEE != v2
        return PumpResult::Progress;                      // return 0

    // Whole frame flushed.
    AccumulatePacketStats(tx_buf_, tx_cursor_, StatDir::Tx);
    tx_buf_ = nullptr;                                    // dword_764CE8 = 0
    tx_cursor_ = 0;                                       // word_764CEE = 0
    completed_this_call_ = true;
    return PumpResult::Progress;                          // return 0
}

// gilde.exe 0x43b8d0 — VIBE_Net_ReceivePacket  (nt_Recv)
// Two-stage partial-read reassembly into rx_buf_:
//   stage 1: while rx_cursor_ < 3, read the 3-byte header.
//   stage 2: read body up to HdrLen(buf) total.
// On a complete frame, classify the sequence event (cmdId/Count) then account
// rx stats and clear the buffer for the next frame.
PumpResult NetTransport::ReceivePacket() {
    completed_this_call_ = false;
    last_event_ = SeqEvent::None;
    if (!sock_ || !sock_->connected() || disconnected_)  // dword_764CE0 == -1
        return PumpResult::Closed;                        // return 1
    if (!rx_buf_)                                          // !dword_764CE4
        return PumpResult::Progress;                       // return 0

    // --- Stage 1: header (3 bytes) -----------------------------------------
    if (rx_cursor_ < kHeaderBytes) {                       // word_764CEC < 3
        const int n = sock_->recv(rx_buf_ + rx_cursor_,
                                  static_cast<u16>(kHeaderBytes - rx_cursor_));
        if (n < 0) {                                       // recv()==0 (peer close)
                                                           // or recv()==-1 (error)
            Teardown();
            return PumpResult::Closed;                      // return 1 (both paths)
        }
        // n == 0 would-block: cursor unchanged; n > 0 advances.
        rx_bytes_total_ += static_cast<u32>(n);            // dword_62E5D8 += v1
        rx_cursor_ = static_cast<u16>(rx_cursor_ + n);     // word_764CEC += v1
    }
    if (rx_cursor_ < kHeaderBytes)                         // still short of header
        return PumpResult::Progress;                        // return 0

    // --- Stage 2: body up to total length ----------------------------------
    const u16 total = HdrLen(rx_buf_);                     // *(WORD*)(buf+1)
    const int n = sock_->recv(rx_buf_ + rx_cursor_,
                              static_cast<u16>(total - rx_cursor_));
    if (n < 0) {                                            // peer close or error
        Teardown();
        return PumpResult::Closed;                           // return 1
    }
    rx_bytes_total_ += static_cast<u32>(n);                // dword_62E5D8 += v4
    rx_cursor_ = static_cast<u16>(rx_cursor_ + n);         // word_764CEC += v4
    if (rx_cursor_ != total)                               // word_764CEC != v6
        return PumpResult::Progress;                         // return 0

    // --- Whole packet received: sequence-gap classification ----------------
    // Only commands (cmdId != -1) participate in sequence tracking.
    if (HdrCmdId(rx_buf_) != kNoCmdId) {                   // *(buf+4) != -1
        const u32 count = HdrCount(rx_buf_);               // *(buf+8)
        if (last_req_count_ + 1 == count) {                // in order
            last_req_count_ = count;                       // dword_7652F8 = Count
            last_event_ = SeqEvent::Advance;
        } else if (count == last_sync_count_) {            // dup of cm_LastSyncCount
            last_event_ = SeqEvent::DupSync;
        } else if (rx_buf_[kOffType] == kSyncType &&
                   rx_buf_[kOffSync] == kSyncMarker) {     // type 0x20 / marker 0x0E
            last_event_ = SeqEvent::Sync;
        } else {                                           // gap -> "Lost a Command"
            last_event_ = SeqEvent::Lost;
            last_req_count_ = count;                        // resync to Count
        }
    }

    AccumulatePacketStats(rx_buf_, rx_cursor_, StatDir::Rx);
    rx_buf_ = nullptr;                                     // dword_764CE4 = 0
    rx_cursor_ = 0;                                        // word_764CEC = 0
    completed_this_call_ = true;
    return PumpResult::Progress;                           // return 0
}

// gilde.exe 0x43b260 — VIBE_Net_AccumulatePacketStats  (__usercall, eax=pkt, edx=len, bl=dir)
// Guard: pkt && len > pkt[0] && pkt[0] < 96. Indexes the count/byte arrays by
// packet type (pkt[0]); the original's dir offset is 4*{0 (rx) | 2 (tx)} within
// a 16-byte-per-type stride. Here that maps to PacketStats::rx/tx[type].
void NetTransport::AccumulatePacketStats(const u8* pkt, u32 len, StatDir dir) {
    if (!pkt)
        return;
    const u8 type = pkt[0];
    if (!(len > type && type < 96))                        // a2 > *result && *result < 96
        return;
    PacketStats::Slot& slot = (dir == StatDir::Rx) ? stats_.rx[type]   // a3 -> 4*0
                                                    : stats_.tx[type];  // !a3 -> 4*2
    ++slot.count;                                          // ++*(&dword_764CF8 + v5)
    slot.bytes += len;                                     // *(dword_764CFC + ...) += a2
}

// EncodeHeader: not a standalone function in the binary; the Command layer writes
// these fields inline (VIBE_Command_EnqueuePacket @0x49388c: stores len@+1,
// cmdId@+4, Count@+8, zeroes flag@+3 and extra@+12). Centralized here so the
// framing definition is shared. Little-endian, matching the 32-bit x86 target.
u16 EncodeHeader(u8* buf, u8 type, u16 totalLen, u32 cmdId, u32 count) {
    buf[kOffType]     = type;                                   // +0x00
    buf[kOffLen]      = static_cast<u8>(totalLen & 0xFF);       // +0x01 lo
    buf[kOffLen + 1]  = static_cast<u8>((totalLen >> 8) & 0xFF);// +0x02 hi
    buf[kOffFlag]     = 0;                                      // +0x03
    buf[kOffCmdId]     = static_cast<u8>(cmdId & 0xFF);         // +0x04
    buf[kOffCmdId + 1] = static_cast<u8>((cmdId >> 8) & 0xFF);
    buf[kOffCmdId + 2] = static_cast<u8>((cmdId >> 16) & 0xFF);
    buf[kOffCmdId + 3] = static_cast<u8>((cmdId >> 24) & 0xFF);
    buf[kOffCount]     = static_cast<u8>(count & 0xFF);         // +0x08
    buf[kOffCount + 1] = static_cast<u8>((count >> 8) & 0xFF);
    buf[kOffCount + 2] = static_cast<u8>((count >> 16) & 0xFF);
    buf[kOffCount + 3] = static_cast<u8>((count >> 24) & 0xFF);
    buf[kOffExtra]     = 0;                                     // +0x0C
    buf[kOffExtra + 1] = 0;
    buf[kOffExtra + 2] = 0;
    buf[kOffExtra + 3] = 0;
    return totalLen;
}

} // namespace guild::net
