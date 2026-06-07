#pragma once
#include "guild/common/types.h"

#include <cstddef>
#include <string>
#include <vector>

// gilde.exe — guild::net  (MODULE: LAN server DISCOVERY + broadcast advertise)
//
// The multiplayer browser ("Search Network Games") finds hosts on the LAN over a
// connectionless UDP broadcast. The flow, recovered 1:1, is asymmetric:
//
//   HOST side  — opens one broadcast UDP socket bound to INADDR_ANY:0 with
//                SO_BROADCAST set and a fixed destination of 255.255.255.255:port,
//                then periodically blasts a self-describing "server info" datagram:
//                  VIBE_Net_OpenBroadcastSocket  @0x43aac0  (lazy create + bind)
//                  VIBE_Net_SendBroadcast        @0x43aba8  (sendto to .255)
//                  VIBE_Net_CloseBroadcastSocket @0x43ab80  (teardown)
//
//   CLIENT side— VIBE_Net_DiscoverServers @0x43abcc binds a UDP socket on `port`
//                and drains inbound datagrams for `timeoutMs`, collecting up to
//                `maxServers` distinct hosts (dedup by source IP string) into a
//                128-byte-stride output table; each entry holds the dotted-quad IP
//                at +0 and the host's advertised info blob at +16, with a type tag
//                (63 single-game / 127 saved-game) at +124.
//
// Per the OS boundary rule, raw WinSock (socket/bind/sendto/recvfrom/inet_ntoa) is
// abstracted behind INetDatagram (declared here, in guild::net, so the module owns
// its boundary). A test supplies a tiny in-process loopback datagram bus so the
// whole host-advertise -> client-discover flow runs deterministically with no OS.
//
// Also recovered: the per-type packet/byte telemetry that the transport feeds and
// the Network Statistics panel renders:
//   VIBE_Net_ResetStatistics   @0x43b230  (zero the totals + 96*4-dword table)
//   VIBE_Net_AccumulatePacketStats @0x43b260 (already lives in net/transport)
//   VIBE_Net_FormatStatistics  @0x43b2a8  (render the per-opcode percentages)
//   VIBE_Net_GetLocalHostAddress @0x43b440 (cached gethostbyname dotted-quad)

namespace guild::net {

// ===========================================================================
// Connectionless datagram boundary (the WinSock UDP calls of this module)
// ---------------------------------------------------------------------------
// One received datagram, carrying the sender's address as a dotted-quad string
// (what inet_ntoa() of the source sockaddr yields in the original) plus payload.
struct Datagram {
    std::string         fromIp;   // inet_ntoa(from.sin_addr)
    std::vector<u8>     data;     // recvfrom() payload bytes
};

// Abstracts socket(AF_INET,SOCK_DGRAM)/bind/setsockopt(SO_BROADCAST)/sendto/
// recvfrom. Non-blocking: recv() returns false (no datagram) immediately rather
// than blocking, mirroring the original's ioctlsocket(FIONBIO)+timed drain loop.
class INetDatagram {
public:
    virtual ~INetDatagram() = default;

    // socket(2,2,0) + (optional SO_BROADCAST) + bind(INADDR_ANY, bindPort).
    // bindPort 0 means "any" (the host-advertise socket binds to :0). Returns
    // false on failure (the original's socket()==-1 / bind()==-1 paths).
    virtual bool open(u16 bindPort, bool broadcast) = 0;

    // closesocket().
    virtual void close() = 0;

    // sendto(data, .255.255.255.255 : dstPort). Returns bytes sent (>=0) or -1.
    virtual int sendTo(u16 dstPort, const void* data, std::size_t n) = 0;

    // recvfrom(): pop the next pending datagram into `out`. Returns true if one
    // was available, false for "would block" (no datagram pending right now).
    virtual bool recvFrom(Datagram& out) = 0;
};

// ===========================================================================
// Discovery reply geometry (recovered from DiscoverServers @0x43abcc)
// ---------------------------------------------------------------------------
// A valid advertise datagram is at least 6 bytes and matches this header:
//   +0x00  u16  magic  == 1          (*(_WORD*)buf == 1)
//   +0x02  u16  kind   == 1 or 4     (single-game vs saved-game advert)
//   +0x04  u16  tag    == 106        (*(_WORD*)&buf[4] == 106)
// kind 1 copies 0x28(40)+2 = 42 payload bytes and tags the entry type 63.
// kind 4 copies 0x68(104)+2 = 106 payload bytes and tags the entry type 127.
constexpr u16 kAdMagic   = 1;     // *(_WORD*)buf
constexpr u16 kAdKindOne = 1;     // single-game advert
constexpr u16 kAdKindSav = 4;     // saved-game advert
constexpr u16 kAdTag     = 106;   // *(_WORD*)&buf[4]
constexpr std::size_t kAdMinBytes = 6;

// Output table stride and field offsets (the original indexes v30 by 128*i).
constexpr std::size_t kEntryStride   = 128;  // 128 * v5
constexpr std::size_t kEntryIpOffset = 0;    // dotted-quad IP string at +0
constexpr std::size_t kEntryBlobOff  = 16;   // advert blob copied to entry+16 (v35)
constexpr std::size_t kEntryTypeOff  = 124;  // *((DWORD*)entry+31): 63 or 127
constexpr u32 kEntryTypeOne = 63;            // kind-1 entry tag
constexpr u32 kEntryTypeSav = 127;           // kind-4 entry tag
constexpr std::size_t kAdKindOneBytes = 0x28 + 2; // 42  (qmemcpy 40 + 2)
constexpr std::size_t kAdKindSavBytes = 0x68 + 2; // 106 (qmemcpy 104 + 2)
constexpr std::size_t kRecvBufBytes   = 256;       // recvfrom(s, buf, 256, ...)

// The well-known discovery port and default browse window the menu uses
// (VIBE_Menu_SearchNetworkGames: DiscoverServers(0x3039, 0xBB8, ...)).
constexpr u16 kDiscoveryPort   = 0x3039; // 12345
constexpr u32 kDiscoveryWindow = 0x0BB8; // 3000 ms

// ===========================================================================
// BroadcastAdvertiser — the host side (Open/Send/Close)
// ---------------------------------------------------------------------------
// Wraps the single broadcast socket (the original's file-global `s` @0x62E5C0)
// and its fixed .255 destination. open() is lazy/idempotent like the binary
// (`if (s == -1) { socket(); ... }`).
class BroadcastAdvertiser {
public:
    explicit BroadcastAdvertiser(INetDatagram* sock) : sock_(sock) {}

    // VIBE_Net_OpenBroadcastSocket @0x43aac0 — lazily create the broadcast socket
    // (SO_BROADCAST=1), bind to INADDR_ANY:0, and latch the .255:port destination.
    // Idempotent: a no-op if already open. Returns true if open afterwards.
    bool OpenBroadcastSocket(u16 port);

    // VIBE_Net_SendBroadcast @0x43aba8 — sendto(s, data, len, .255:port) if open.
    // Returns bytes sent, or -1 / 0 mirroring the guard (no-op if not open).
    int SendBroadcast(const void* data, std::size_t len);

    // VIBE_Net_CloseBroadcastSocket @0x43ab80 — closesocket + mark closed.
    void CloseBroadcastSocket();

    bool open() const { return open_; }   // s != -1
    u16  port() const { return port_; }

private:
    INetDatagram* sock_ = nullptr;
    bool open_ = false;   // s != -1
    u16  port_ = 0;       // htons(v3) target port latched in `to`
};

// ===========================================================================
// DiscoverServers — the client side
// ---------------------------------------------------------------------------
// One found server entry, decoded from the 128-byte output record the original
// builds. Kept as a struct (vs a raw byte table) for testability; the raw
// 128-byte image is reproduced exactly by EncodeEntry() for golden vectors.
struct DiscoveredServer {
    std::string ip;            // dotted-quad of the sender (entry+0)
    std::vector<u8> blob;      // advert payload (entry+16, 42 or 106 bytes)
    u32 type = 0;              // entry+124: 63 (kind 1) or 127 (kind 4)
};

// VIBE_Net_DiscoverServers @0x43abcc — bind a UDP socket on `port`, drain inbound
// datagrams while (elapsed < timeoutMs && found < maxServers), accept those that
// match the advert header, dedup by source IP, and collect up to maxServers.
// `nowMs` is a monotonic clock callback (timeGetTime in the original); each
// recvFrom miss advances it by `tickMs` so the loop terminates deterministically.
// Returns the number of servers found (>=0), or -2 if maxServers <= 0, or -1 on a
// socket open failure (the original's WSAStartup/socket/bind error path).
int DiscoverServers(INetDatagram* sock, u16 port, u32 timeoutMs, int maxServers,
                    std::vector<DiscoveredServer>& out,
                    u32 (*nowMs)(void* user), void* user, u32 tickMs);

// Serialize one DiscoveredServer into the exact 128-byte on-stack record image
// the original produces (IP at +0, blob at +16, type dword at +124, rest zero).
// Used by golden-vector tests to assert byte-for-byte fidelity.
std::vector<u8> EncodeEntry(const DiscoveredServer& s);

// ===========================================================================
// Per-type packet telemetry (Reset/Format)
// ---------------------------------------------------------------------------
// Mirrors the original's flat globals: four total counters plus a 96-entry table
// of {recvCount, recvBytes, sendCount, sendBytes} (dword_764CF8.. stride 16).
// AccumulatePacketStats lives in net/transport (reused); this gathers the totals
// and the table so Reset/Format can be exercised in isolation.
struct NetStats {
    u32 recvPackets = 0;   // dword_62E5DC
    u32 recvBytes   = 0;   // dword_62E5D8
    u32 sendPackets = 0;   // dword_62E5E4
    u32 sendBytes   = 0;   // dword_62E5E0
    struct PerType { u32 recvCount = 0; u32 recvBytes = 0;
                     u32 sendCount = 0; u32 sendBytes = 0; };
    PerType perType[96];   // dword_764CF8 table (16-byte stride)

    // VIBE_Net_ResetStatistics @0x43b230 — zero totals + the whole 96*16 table.
    void ResetStatistics();

    // VIBE_Net_FormatStatistics @0x43b2a8 — render the summary + one line per
    // opcode (with recv/send packet & byte percentages). Returns the joined text.
    // The original sprintf's into a scratch buffer and pushes each line to a list
    // box; here the formatted lines are returned so a test can read them.
    std::vector<std::string> FormatStatistics() const;
};

// VIBE_Net_GetLocalHostAddress @0x43b440 — gethostname + gethostbyname, cached as
// a dotted-quad string (byte_62E5C8). Abstracted behind a resolver callback so it
// is testable without DNS; returns the cached value once resolved.
class LocalHostAddress {
public:
    using Resolver = bool (*)(void* user, std::string& outDottedQuad);

    // First call invokes `resolve`; subsequent calls return the cached string
    // (the original's `if (byte_62E5C8) return ...` fast path).
    const std::string& Get(Resolver resolve, void* user);
    const std::string& cached() const { return cached_; }

private:
    std::string cached_;   // byte_62E5C8 (empty == not yet resolved)
};

} // namespace guild::net
