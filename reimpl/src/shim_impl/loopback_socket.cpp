#include "shim_impl/loopback_socket.h"
#include <cstring>

namespace guild::shim {

std::pair<std::unique_ptr<LoopbackSocket>, std::unique_ptr<LoopbackSocket>>
LoopbackSocket::makePair() {
    auto a = std::unique_ptr<LoopbackSocket>(new LoopbackSocket());
    auto b = std::unique_ptr<LoopbackSocket>(new LoopbackSocket());

    auto buf_a = std::make_shared<std::deque<std::uint8_t>>(); // a's inbox / b's outbox
    auto buf_b = std::make_shared<std::deque<std::uint8_t>>(); // b's inbox / a's outbox

    a->alive_ = std::make_shared<int>(1);
    b->alive_ = std::make_shared<int>(1);

    a->inbox_ = buf_a;
    a->outbox_ = buf_b;
    a->peer_alive_ = b->alive_;
    a->open_ = true;

    b->inbox_ = buf_b;
    b->outbox_ = buf_a;
    b->peer_alive_ = a->alive_;
    b->open_ = true;

    return {std::move(a), std::move(b)};
}

bool LoopbackSocket::connect(const char* /*host*/, std::uint16_t /*port*/) {
    // A loopback socket is connected at creation via makePair(); a bare connect()
    // has no peer to reach.
    return open_ && !peer_alive_.expired();
}

void LoopbackSocket::close() {
    open_ = false;
    inbox_.reset();
    outbox_.reset();
    alive_.reset();        // signal the peer that we disconnected
    peer_alive_.reset();
}

bool LoopbackSocket::connected() const {
    return open_ && !peer_alive_.expired();
}

int LoopbackSocket::send(const void* data, std::size_t n) {
    if (!connected())
        return -1;
    if (n == 0)
        return 0;
    const auto* p = static_cast<const std::uint8_t*>(data);
    outbox_->insert(outbox_->end(), p, p + n);
    return static_cast<int>(n); // loopback never blocks on send
}

int LoopbackSocket::recv(void* dst, std::size_t n) {
    if (!open_ || !inbox_)
        return -1;
    if (n == 0)
        return 0;
    std::size_t avail = inbox_->size();
    if (avail == 0)
        return 0; // would-block
    std::size_t take = n < avail ? n : avail; // supports partial reads
    auto* out = static_cast<std::uint8_t*>(dst);
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = inbox_->front();
        inbox_->pop_front();
    }
    return static_cast<int>(take);
}

} // namespace guild::shim
