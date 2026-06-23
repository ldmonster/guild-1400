#pragma once
// =============================================================================
// guild::shim_impl — REAL network transport backend on SDL_net (SDL2_net).
//
// The 1:1-reconstructed networking protocol logic lives in src/net/ against two
// abstract boundaries:
//   * guild::net::INetDatagram  — connectionless UDP + LAN broadcast (the
//     discovery/advertise path; VIBE_Net_DiscoverServers @0x43abcc et al.)
//   * guild::shim::INetSocket   — the non-blocking TCP command stream
//     (NetTransport / VIBE_Net_* @0x43b8xx).
//
// gilde.exe drove those through wsock32 (socket/bind/sendto/recvfrom/connect/
// send/recv/select). Per the user's go-ahead (Rule 6), the transport is swapped to
// **SDL_net** — the protocol/packet/discovery SEMANTICS stay byte-for-byte (1:1
// behavioral); only the socket calls underneath become SDL_net. SDL_net's
// SDLNet_UDP_Open enables SO_BROADCAST, so the .255 broadcast discovery is faithful.
//
// Compiled only when GUILD_HAVE_SDL2_NET is defined (find_package(SDL2_net)); the
// portable/headless build keeps the in-process loopback backend.
// =============================================================================
#include "shim/INetSocket.h"
#include "net/discovery.h"   // guild::net::INetDatagram / Datagram

#include <cstdint>
#include <memory>

namespace guild::shim_impl {

// Process-wide SDL_net init refcount (SDLNet_Init / SDLNet_Quit). Safe to nest.
bool SdlNetGlobalInit();
void SdlNetGlobalQuit();

// UDP/broadcast datagram backend — implements the discovery boundary.
class SdlNetDatagram : public guild::net::INetDatagram {
public:
    SdlNetDatagram() = default;
    ~SdlNetDatagram() override { close(); SdlNetGlobalQuit(); }

    bool open(guild::u16 bindPort, bool broadcast) override;  // SDLNet_UDP_Open
    void close() override;                                    // SDLNet_UDP_Close
    int  sendTo(guild::u16 dstPort, const void* data, std::size_t n) override; // -> .255
    bool recvFrom(guild::net::Datagram& out) override;        // non-blocking UDP_Recv

private:
    void* sock_ = nullptr;   // SDLsocket/UDPsocket (opaque to avoid SDL_net in the header)
    void* pkt_  = nullptr;   // a reusable UDPpacket scratch (recv)
    bool  inited_ = false;
};

// TCP client backend — implements the command-stream boundary (non-blocking).
class SdlNetSocket : public guild::shim::INetSocket {
public:
    SdlNetSocket() = default;
    ~SdlNetSocket() override { close(); SdlNetGlobalQuit(); }

    bool connect(const char* host, std::uint16_t port) override;  // SDLNet_TCP_Open
    void close() override;
    bool connected() const override { return sock_ != nullptr; }
    int  send(const void* data, std::size_t n) override;   // SDLNet_TCP_Send
    int  recv(void* dst, std::size_t n) override;          // poll + SDLNet_TCP_Recv

private:
    void* sock_ = nullptr;   // TCPsocket
    void* set_  = nullptr;   // SDLNet_SocketSet for non-blocking readiness polling
    bool  inited_ = false;
};

} // namespace guild::shim_impl
