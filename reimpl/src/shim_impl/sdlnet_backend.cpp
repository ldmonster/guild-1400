// guild::shim_impl — SDL_net transport backend. See header. Compiled only with
// GUILD_HAVE_SDL2_NET. The protocol stays 1:1 (src/net); this is just the socket
// layer on SDL_net instead of wsock32.
#include "shim_impl/sdlnet_backend.h"

#ifdef GUILD_HAVE_SDL2_NET

#include <SDL_net.h>

#include <cstdio>
#include <cstring>
#include <mutex>

namespace guild::shim_impl {
namespace {
std::mutex g_initMx;
int        g_initRefs = 0;   // SDLNet_Init refcount

// Format an SDL_net IPaddress.host (network byte order) as a dotted quad, exactly
// like inet_ntoa(from.sin_addr) in VIBE_Net_DiscoverServers.
std::string DottedQuad(Uint32 hostNbo) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&hostNbo);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}
} // namespace

bool SdlNetGlobalInit() {
    std::lock_guard<std::mutex> lk(g_initMx);
    if (g_initRefs == 0) {
        if (SDLNet_Init() == -1) return false;
    }
    ++g_initRefs;
    return true;
}
void SdlNetGlobalQuit() {
    std::lock_guard<std::mutex> lk(g_initMx);
    if (g_initRefs > 0 && --g_initRefs == 0) SDLNet_Quit();
}

// ---------------------------------------------------------------------------
// SdlNetDatagram (UDP + broadcast)
// ---------------------------------------------------------------------------
bool SdlNetDatagram::open(guild::u16 bindPort, bool /*broadcast*/) {
    close();
    if (!inited_) { if (!SdlNetGlobalInit()) return false; inited_ = true; }
    // SDLNet_UDP_Open binds the socket to bindPort and enables SO_BROADCAST.
    UDPsocket s = SDLNet_UDP_Open(bindPort);
    if (!s) return false;
    sock_ = s;
    pkt_  = SDLNet_AllocPacket(2048);   // reusable recv scratch (>= original 256)
    if (!pkt_) { SDLNet_UDP_Close(s); sock_ = nullptr; return false; }
    return true;
}

void SdlNetDatagram::close() {
    if (pkt_)  { SDLNet_FreePacket(static_cast<UDPpacket*>(pkt_)); pkt_ = nullptr; }
    if (sock_) { SDLNet_UDP_Close(static_cast<UDPsocket>(sock_));  sock_ = nullptr; }
}

int SdlNetDatagram::sendTo(guild::u16 dstPort, const void* data, std::size_t n) {
    if (!sock_ || !data || n == 0) return -1;
    IPaddress addr;
    // The original sends to the limited broadcast address 255.255.255.255:dstPort.
    if (SDLNet_ResolveHost(&addr, "255.255.255.255", dstPort) == -1) return -1;
    UDPpacket* p = SDLNet_AllocPacket(static_cast<int>(n));
    if (!p) return -1;
    std::memcpy(p->data, data, n);
    p->len = static_cast<int>(n);
    p->address = addr;
    const int sent = SDLNet_UDP_Send(static_cast<UDPsocket>(sock_), -1, p);  // channel -1 = use p->address
    SDLNet_FreePacket(p);
    return sent > 0 ? static_cast<int>(n) : -1;   // SDL_net returns # of destinations (1) on success
}

bool SdlNetDatagram::recvFrom(guild::net::Datagram& out) {
    if (!sock_ || !pkt_) return false;
    UDPpacket* p = static_cast<UDPpacket*>(pkt_);
    const int r = SDLNet_UDP_Recv(static_cast<UDPsocket>(sock_), p);   // 1=got, 0=none, -1=err
    if (r <= 0) return false;
    out.fromIp = DottedQuad(p->address.host);
    out.data.assign(p->data, p->data + p->len);
    return true;
}

// ---------------------------------------------------------------------------
// SdlNetSocket (TCP, non-blocking-ish via SocketSet poll)
// ---------------------------------------------------------------------------
bool SdlNetSocket::connect(const char* host, std::uint16_t port) {
    close();
    if (!host || !*host) return false;
    if (!inited_) { if (!SdlNetGlobalInit()) return false; inited_ = true; }
    IPaddress addr;
    if (SDLNet_ResolveHost(&addr, host, port) == -1) return false;
    TCPsocket s = SDLNet_TCP_Open(&addr);   // client connect (blocking)
    if (!s) return false;
    SDLNet_SocketSet set = SDLNet_AllocSocketSet(1);
    if (!set) { SDLNet_TCP_Close(s); return false; }
    SDLNet_TCP_AddSocket(set, s);
    sock_ = s;
    set_  = set;
    return true;
}

void SdlNetSocket::close() {
    if (set_ && sock_) SDLNet_TCP_DelSocket(static_cast<SDLNet_SocketSet>(set_),
                                            static_cast<TCPsocket>(sock_));
    if (set_)  { SDLNet_FreeSocketSet(static_cast<SDLNet_SocketSet>(set_)); set_ = nullptr; }
    if (sock_) { SDLNet_TCP_Close(static_cast<TCPsocket>(sock_)); sock_ = nullptr; }
}

int SdlNetSocket::send(const void* data, std::size_t n) {
    if (!sock_ || n == 0) return 0;
    // SDLNet_TCP_Send blocks until all bytes are sent or the peer drops; it returns
    // < n only on error/close. Mirror the shim contract: bytes sent, -1 on error.
    const int sent = SDLNet_TCP_Send(static_cast<TCPsocket>(sock_), data, static_cast<int>(n));
    if (sent < static_cast<int>(n)) return -1;   // peer closed / error
    return sent;
}

int SdlNetSocket::recv(void* dst, std::size_t n) {
    if (!sock_ || n == 0) return 0;
    // Non-blocking: poll readiness first (timeout 0). SDLNet_TCP_Recv would
    // otherwise block. 0 ready => "would block" => return 0 (the WSAEWOULDBLOCK
    // mapping the transport expects).
    const int ready = SDLNet_CheckSockets(static_cast<SDLNet_SocketSet>(set_), 0);
    if (ready <= 0) return 0;
    if (!SDLNet_SocketReady(static_cast<TCPsocket>(sock_))) return 0;
    const int got = SDLNet_TCP_Recv(static_cast<TCPsocket>(sock_), dst, static_cast<int>(n));
    if (got <= 0) return -1;   // 0/-1 from SDL_net == peer closed / error
    return got;
}

} // namespace guild::shim_impl

#else  // !GUILD_HAVE_SDL2_NET — keep the TU non-empty / linkable in portable builds.

namespace guild::shim_impl {
bool SdlNetGlobalInit() { return false; }
void SdlNetGlobalQuit() {}
}

#endif
