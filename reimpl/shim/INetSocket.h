#pragma once
// Host networking boundary: a single non-blocking TCP client, matching the
// original wsock32 usage that feeds the Command lockstep system. Single-player
// uses an in-process loopback backend.
#include <cstddef>
#include <cstdint>

namespace guild::shim {

class INetSocket {
public:
    virtual ~INetSocket() = default;
    virtual bool connect(const char* host, std::uint16_t port) = 0;
    virtual void close() = 0;
    virtual bool connected() const = 0;

    // Non-blocking. Return bytes transferred (0 if would-block), or -1 on error.
    virtual int send(const void* data, std::size_t n) = 0;
    virtual int recv(void* dst, std::size_t n) = 0;
};

} // namespace guild::shim
