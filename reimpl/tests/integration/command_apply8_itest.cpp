#include "test.h"

// Integration: drive command_apply8's request builders against TWO real
// reconstructed siblings, exactly as the live wiring does.
//
//  (1) The REAL command codec (command.cpp): QueueRequestObject34 stages a
//      packet and links it through the genuine CommandQueue::EnqueuePacket
//      (which stamps the header length via the real VIBE_Command_ComputePacketSize).
//      We read the stamped packet back out of the send ring.
//  (2) The REAL CRT PRNG sibling crt::RandNext (gilde.exe 0x5cb8bc). The opcode-34
//      builder's nonce is produced by the CommandApply8Hooks.randNext slot; we
//      forward that slot into crt::RandNext (this IS the live wiring — the hook
//      comment names VIBE_Util_RandNext). After seeding the generator we can
//      predict the exact nonce the builder writes at payload byte 0x29.
#include "sim/command_apply8.h"
#include "sim/command.h"
#include "crt/rand.h"          // REAL reconstructed sibling: crt::RandNext / Srand

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Forward the opcode-34 nonce hook into the real CRT generator (the live
// VIBE_Util_RandNext). Returns the next 15-bit LCG draw.
u32 RealRandNextHook() { return static_cast<u32>(guild::crt::RandNext()); }

// The opcode-34 template the global would supply: zero-fill, return scene id.
i32 TemplateHook(void* dst) {
    std::memset(dst, 0, 0x2D);
    return 0x1234;   // dword_632240 — overwrites payload[0]
}
} // namespace

// QueueRequestObject34 -> opcode-34 packet whose nonce comes from the REAL CRT
// PRNG. Seed the generator, predict the draw, and confirm the builder wrote it
// at payload +0x29 — proving the cross-module RNG wiring end to end.
TEST(CommandApply8Itest, Object34NonceFromRealCrtRng) {
    CommandQueue q; q.Init();
    q.set_standalone(false);            // keep the packet in the send ring

    CommandApply8Hooks h{};             // zero all slots; install only what we wire
    h.randNext        = &RealRandNextHook;   // -> real crt::RandNext
    h.objectTemplate34 = &TemplateHook;
    SetCommandApply8Hooks(&h);

    // Seed the real generator, then compute the value the builder MUST observe by
    // drawing from an independently-seeded reference (same LCG, same sequence).
    guild::crt::Srand(0xC0FFEE);
    u32 expectedNonce = static_cast<u32>(guild::crt::RandNext());
    // Re-seed so the builder's single draw reproduces expectedNonce.
    guild::crt::Srand(0xC0FFEE);

    u8 templateSrc[0x2D];
    std::memset(templateSrc, 0xEE, sizeof templateSrc);
    i32 slot = QueueRequestObject34(q, templateSrc, /*tail=*/0x77);
    CHECK_EQ(slot, 1);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ(p.opcode(), 34);
    // The REAL EnqueuePacket stamped the wire length via ComputePacketSize.
    CHECK_EQ(p.len(), ComputePacketSize(p));
    CHECK_EQ(p.count(), 1u);
    // payload[0] is the template scene id; +0x25 dword == 1 (present flag).
    CHECK_EQ(p.get32(0x10), 0x1234u);
    CHECK_EQ(p.get32(0x10 + 0x25), 1u);
    // The nonce at payload +0x29 is the real PRNG draw.
    CHECK_EQ(p.get32(0x10 + 0x29), expectedNonce);

    SetCommandApply8Hooks(nullptr);
}

// Two successive opcode-34 packets draw two CONSECUTIVE values from the real LCG,
// confirming the hook advances the shared generator state (not a constant).
TEST(CommandApply8Itest, ConsecutiveNoncesAdvanceRealLcg) {
    CommandQueue q; q.Init();
    q.set_standalone(false);

    CommandApply8Hooks h{};
    h.randNext         = &RealRandNextHook;
    h.objectTemplate34 = &TemplateHook;
    SetCommandApply8Hooks(&h);

    guild::crt::Srand(1);
    u32 n0 = static_cast<u32>(guild::crt::RandNext());
    u32 n1 = static_cast<u32>(guild::crt::RandNext());
    guild::crt::Srand(1);

    u8 src[0x2D]; std::memset(src, 0, sizeof src);
    QueueRequestObject34(q, src, 0);
    QueueRequestObject34(q, src, 0);

    CHECK_EQ(q.send_count(), 2u);
    CHECK_EQ(q.ring_slot(1).get32(0x10 + 0x29), n0);
    CHECK_EQ(q.ring_slot(2).get32(0x10 + 0x29), n1);
    CHECK(n0 != n1);   // the real LCG advanced

    SetCommandApply8Hooks(nullptr);
}
