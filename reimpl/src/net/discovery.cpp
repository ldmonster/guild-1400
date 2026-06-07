#include "net/discovery.h"

#include <cstdio>
#include <cstring>

// Translation notes — WinSock UDP -> INetDatagram
// -----------------------------------------------
// The original opens raw AF_INET/SOCK_DGRAM sockets and calls bind/sendto/recvfrom
// /inet_ntoa directly. Per the OS-boundary rule those collapse onto INetDatagram:
//   socket(2,2,0)+setsockopt(SO_BROADCAST)+bind  -> open(port, broadcast)
//   sendto(s, p, n, 0, &to/.255, 16)             -> sendTo(port, p, n)
//   recvfrom(s, buf, 256, 0, &from, &len)        -> recvFrom(out)  (with from-IP)
//   inet_ntoa(from.sin_addr)                     -> Datagram::fromIp (provided)
//   closesocket(s)                               -> close()
// The drain loop's blocking semantics (non-blocking socket + timed poll) are
// modeled with a monotonic-clock callback: each recvFrom miss ticks the clock so
// the (elapsed < timeoutMs) guard terminates without real time. The packet-accept
// predicate, dedup-by-source-IP scan, 128-byte entry layout, and the kind-1/kind-4
// blob copies + type tags are translated byte-for-byte.

namespace guild::net {

// ---------------------------------------------------------------------------
// BroadcastAdvertiser
// ---------------------------------------------------------------------------

// gilde.exe 0x43aac0 — VIBE_Net_OpenBroadcastSocket  (__usercall, ax = port).
// Original: if (s == -1) { s = socket(2,2,0); setsockopt(s,SOL_SOCKET,SO_BROADCAST,
// &one,4); bind(s, INADDR_ANY:0, 16); to = { AF_INET, INADDR_BROADCAST, htons(port) }; }
bool BroadcastAdvertiser::OpenBroadcastSocket(u16 port) {
    if (!open_) {                       // if (s == -1)
        if (!sock_)
            return false;
        // socket(2,2,0) + setsockopt(SO_BROADCAST,1) + bind(INADDR_ANY:0).
        if (!sock_->open(/*bindPort*/ 0, /*broadcast*/ true))
            return false;
        open_ = true;
        port_ = port;                   // *(_WORD*)to.sa_data = htons(port)
    }
    return open_;
}

// gilde.exe 0x43aba8 — VIBE_Net_SendBroadcast  (__usercall, eax = data, edx = len).
// Original: if (s != -1) return sendto(s, data, len, 0, &to, 16); return data;
int BroadcastAdvertiser::SendBroadcast(const void* data, std::size_t len) {
    if (open_ && sock_)                 // if (s != -1)
        return sock_->sendTo(port_, data, len);
    return -1;                          // not open: the guard skips sendto
}

// gilde.exe 0x43ab80 — VIBE_Net_CloseBroadcastSocket.
// Original: if (s != -1) { closesocket(s); s = -1; }
void BroadcastAdvertiser::CloseBroadcastSocket() {
    if (open_) {                        // if (s != -1)
        if (sock_)
            sock_->close();
        open_ = false;                  // s = -1
    }
}

// ---------------------------------------------------------------------------
// DiscoverServers
// ---------------------------------------------------------------------------

// gilde.exe 0x43abcc — VIBE_Net_DiscoverServers.
// (__usercall: ax=port, edx=timeoutMs, ecx=maxServers, ebx=outBuf.)
//
// Original control flow preserved:
//   if (maxServers <= 0) return -2;
//   memset(outBuf, 0, maxServers*128);
//   WSAStartup; s = socket(2,2,0); bind(s, INADDR_ANY:port); ioctlsocket(FIONBIO);
//   while (timeGetTime()-start < timeoutMs && found < maxServers) {
//     if (recvfrom(s, buf, 256, ...) >= 6 && buf[4..5]==106 &&
//         (buf[2..3]==1 || buf[2..3]==4) && buf[0..1]==1) {
//       ip = inet_ntoa(from);
//       for (j=0; j<found; ++j) if (!StrCmp(ip, &outBuf[128*j])) goto next; // dup
//       // new server: ip at +0; blob at +16; type dword at +124
//       found++;
//     }
//   }
//   closesocket(s); WSACleanup(); return found;
int DiscoverServers(INetDatagram* sock, u16 port, u32 timeoutMs, int maxServers,
                    std::vector<DiscoveredServer>& out,
                    u32 (*nowMs)(void* user), void* user, u32 tickMs) {
    out.clear();
    if (maxServers <= 0)                 // if (v34 <= 0) return -2;
        return -2;

    if (!sock || !nowMs)
        return -1;
    // WSAStartup + socket(2,2,0) + bind(INADDR_ANY, port) + ioctlsocket(FIONBIO).
    if (!sock->open(port, /*broadcast*/ false))
        return -1;                       // socket/bind error path -> WSACleanup; -1

    const u32 start = nowMs(user);       // Time = timeGetTime();
    int found = 0;                       // v5

    // while (timeGetTime()-start < timeoutMs && found < maxServers)
    while ((nowMs(user) - start) < timeoutMs && found < maxServers) {
        Datagram dg;
        if (!sock->recvFrom(dg)) {
            // recvfrom would-block (no datagram pending). In the binary the timed
            // poll consumes wall time; here the nowMs() callback advances `tickMs`
            // per invocation, so the (elapsed < timeoutMs) guard terminates. The
            // explicit reference keeps tickMs meaningful for callers that ignore it.
            (void)tickMs;
            continue;
        }

        // (unsigned)recvfrom(...) >= 6 && buf[4..5]==106 &&
        // (buf[2..3]==1 || buf[2..3]==4) && buf[0..1]==1
        const std::vector<u8>& buf = dg.data;
        if (buf.size() < kAdMinBytes)
            continue;
        const u16 magic = static_cast<u16>(buf[0] | (buf[1] << 8));
        const u16 kind  = static_cast<u16>(buf[2] | (buf[3] << 8));
        const u16 tag   = static_cast<u16>(buf[4] | (buf[5] << 8));
        if (tag != kAdTag || (kind != kAdKindOne && kind != kAdKindSav)
            || magic != kAdMagic)
            continue;

        // Dedup by source IP against the entries already collected
        // (for j<found: if (!StrCmp(inet_ntoa(from), &outBuf[128*j])) skip).
        bool dup = false;
        for (int j = 0; j < found; ++j) {
            if (out[static_cast<std::size_t>(j)].ip == dg.fromIp) {
                dup = true;              // matches -> goto LABEL_10 (drop)
                break;
            }
        }
        if (dup)
            continue;

        // New server entry. ip at +0; blob at +16; type dword at +124.
        DiscoveredServer srv;
        srv.ip = dg.fromIp;
        if (kind == kAdKindOne) {
            // qmemcpy(entry+16, buf, 0x28); qmemcpy(entry+56, &buf[40], 2);
            // *((DWORD*)entry+31) = 63;
            std::size_t n = kAdKindOneBytes;          // 42
            if (n > buf.size()) n = buf.size();
            srv.blob.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
            srv.type = kEntryTypeOne;                 // 63
        } else {
            // qmemcpy(entry+16, buf, 0x68); qmemcpy(entry+120, &buf[104], 2);
            // *((DWORD*)entry+31) = 127;
            std::size_t n = kAdKindSavBytes;          // 106
            if (n > buf.size()) n = buf.size();
            srv.blob.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
            srv.type = kEntryTypeSav;                 // 127
        }
        out.push_back(std::move(srv));
        ++found;
    }

    sock->close();                       // closesocket(s); WSACleanup();
    return found;                        // return v5;
}

// Reproduce the exact 128-byte on-stack record the original writes per server:
// IP dotted-quad copied at +0 (NUL-terminated), advert blob at +16, type dword at
// +124, every other byte zero (the leading memset(outBuf, 0, maxServers*128)).
std::vector<u8> EncodeEntry(const DiscoveredServer& s) {
    std::vector<u8> e(kEntryStride, 0);
    // inet_ntoa copy at entry+0 (the 2-byte-at-a-time string copy, NUL included).
    std::size_t ipLen = s.ip.size();
    if (ipLen > kEntryStride - 1)            // keep within the 128-byte record
        ipLen = kEntryStride - 1;
    std::memcpy(e.data() + kEntryIpOffset, s.ip.data(), ipLen);
    e[kEntryIpOffset + ipLen] = 0;           // terminator
    // blob at entry+16 (clamped to the record; 42 or 106 bytes in practice).
    std::size_t bn = s.blob.size();
    if (bn > kEntryTypeOff - kEntryBlobOff)  // don't overrun into the type dword
        bn = kEntryTypeOff - kEntryBlobOff;
    std::memcpy(e.data() + kEntryBlobOff, s.blob.data(), bn);
    // type dword at entry+124 (little-endian).
    e[kEntryTypeOff + 0] = static_cast<u8>(s.type);
    e[kEntryTypeOff + 1] = static_cast<u8>(s.type >> 8);
    e[kEntryTypeOff + 2] = static_cast<u8>(s.type >> 16);
    e[kEntryTypeOff + 3] = static_cast<u8>(s.type >> 24);
    return e;
}

// ---------------------------------------------------------------------------
// NetStats — Reset / Format
// ---------------------------------------------------------------------------

// gilde.exe 0x43b230 — VIBE_Net_ResetStatistics.
// Original: VIBE_Light_SetGrayColorThunk(0, 1536) is a memset(dword_764CF8, 0,
// 1536) thunk over the 96*16-byte per-type table; then zero the four totals.
void NetStats::ResetStatistics() {
    // memset(dword_764CF8, 0, 1536) over the 96*16-byte table. Field-wise zero
    // (behaviorally identical to the byte memset) keeps the type non-trivial-safe.
    for (auto& t : perType)
        t = PerType{};
    recvPackets = 0;   // dword_62E5DC = 0
    recvBytes   = 0;   // dword_62E5D8 = 0
    sendPackets = 0;   // dword_62E5E4 = 0
    sendBytes   = 0;   // dword_62E5E0 = 0
}

// Percentage helper matching the original: count * 100.0 / total. The binary
// divides unconditionally (total==0 yields inf/nan, which it tolerates); we return
// 0.0 for total==0 so the formatted output stays finite & deterministic.
static double Pct(u32 count, u32 total) {
    if (total == 0)
        return 0.0;                       // (count * 100.0) / total, guarded
    return static_cast<double>(count) * 100.0 / static_cast<double>(total);
}

// gilde.exe 0x43b2a8 — VIBE_Net_FormatStatistics.
// Original: sprintf summary "Recv: %u Packets w/ %u bytes, Send: %u Packets w/ %u
// bytes"; then for each opcode i in [0,0x60): sprintf "%u. Recv: %u Packets (%02f%%)
// w/ %u bytes (%02f%%), Send: %u Packets (%02f%%) w/ %u bytes (%02f%%)" using the
// per-type table and the totals as the denominators. (dbl_6166D4 == 100.0.)
std::vector<std::string> NetStats::FormatStatistics() const {
    std::vector<std::string> lines;
    char buf[256];

    std::snprintf(buf, sizeof(buf),
                  "Recv: %u Packets w/ %u bytes, Send: %u Packets w/ %u bytes",
                  recvPackets, recvBytes, sendPackets, sendBytes);
    lines.emplace_back(buf);

    for (u32 i = 0; i < 96; ++i) {
        const PerType& t = perType[i];
        std::snprintf(
            buf, sizeof(buf),
            "%u. Recv: %u Packets (%02f%%) w/ %u bytes (%02f%%), "
            "Send: %u Packets (%02f%%) w/ %u bytes (%02f%%)",
            i,
            t.recvCount, Pct(t.recvCount, recvPackets),
            t.recvBytes, Pct(t.recvBytes, recvBytes),
            t.sendCount, Pct(t.sendCount, sendPackets),
            t.sendBytes, Pct(t.sendBytes, sendBytes));
        lines.emplace_back(buf);
    }
    return lines;
}

// ---------------------------------------------------------------------------
// LocalHostAddress
// ---------------------------------------------------------------------------

// gilde.exe 0x43b440 — VIBE_Net_GetLocalHostAddress.
// Original: if (byte_62E5C8) return &byte_62E5C8;  (cached fast path)
//   WSAStartup(if needed); gethostname(name,512); he=gethostbyname(name);
//   if (he) inet_ntoa(*(in_addr*)he->h_addr_list[0]) -> copy into byte_62E5C8;
//   WSACleanup(if needed); return &byte_62E5C8.
// The DNS round-trip is the Resolver callback; the cache is the byte_62E5C8 latch.
const std::string& LocalHostAddress::Get(Resolver resolve, void* user) {
    if (!cached_.empty())                 // if (byte_62E5C8) return cached
        return cached_;
    std::string addr;
    if (resolve && resolve(user, addr))   // gethostname+gethostbyname+inet_ntoa
        cached_ = addr;
    return cached_;
}

} // namespace guild::net
