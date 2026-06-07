#pragma once
// Portable default backend for INetSocket — an in-process bidirectional byte pipe.
// NOT a translation of gilde.exe; a clean implementation of the interface contract.
// Two LoopbackSockets are created as a connected pair: bytes written to one with
// send() become readable on the other with recv(). This lets the lockstep command
// system talk to itself for single-player / deterministic tests, without real
// sockets. Non-blocking semantics: recv() returns 0 (would-block) when no data.
#include "shim/INetSocket.h"
#include <cstdint>
#include <deque>
#include <memory>

namespace guild::shim {

class LoopbackSocket : public INetSocket {
public:
    // Create a connected pair. Each returned socket already reports connected().
    static std::pair<std::unique_ptr<LoopbackSocket>, std::unique_ptr<LoopbackSocket>>
    makePair();

    ~LoopbackSocket() override { close(); }

    // INetSocket
    bool connect(const char* host, std::uint16_t port) override;
    void close() override;
    bool connected() const override;
    int send(const void* data, std::size_t n) override;
    int recv(void* dst, std::size_t n) override;

private:
    // Shared FIFO buffer between the two endpoints. Each socket reads from its own
    // inbox and writes to the peer's inbox.
    using Buffer = std::shared_ptr<std::deque<std::uint8_t>>;

    LoopbackSocket() = default;

    Buffer inbox_;   // bytes destined for this socket
    Buffer outbox_;  // bytes this socket sends (== peer's inbox_)
    // Liveness token: this socket owns alive_ (shared_ptr); the peer keeps a
    // weak_ptr to it (peer_alive_). When either side close()s, it drops alive_,
    // so the other side observes peer_alive_.expired() == peer disconnected.
    std::shared_ptr<int> alive_;
    std::weak_ptr<int> peer_alive_;
    bool open_ = false;
};

} // namespace guild::shim
