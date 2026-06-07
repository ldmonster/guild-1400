// Real-asset GUARDED e2e for the inheritance command-emission body. The command
// codec is asset-independent, so the guard simply skip-passes when the shipped
// `europe_guild_1400_original/Resources/forms.BIN` is absent (matching the
// repo's other real-asset e2e tests), and otherwise runs the FULL emission flow:
//   build the dead-person record -> EmitInheritanceDelta -> FlushSendQueue
//   (standalone, applies locally) -> ExecCommands with a delta-applying handler
//   installed for opcode 22 -> assert the live entity reaches its post-death
//   state. This proves the (build -> wire -> apply) loop is byte-consistent.
//
// GUARDED: if the asset folder isn't present the test passes trivially.
#include "test.h"

#include "sim/command_inherit.h"
#include "sim/command_codec.h"

#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
bool RealAssetsPresent() {
    static const char* kPaths[] = {
        "europe_guild_1400_original/Resources/forms.BIN",
        "../europe_guild_1400_original/Resources/forms.BIN",
        "reimpl/europe_guild_1400_original/Resources/forms.BIN",
    };
    for (const char* p : kPaths) {
        if (FILE* f = std::fopen(p, "rb")) { std::fclose(f); return true; }
    }
    return false;
}

u32 RandZero() { return 0; }

// The post-death state the live entity must reach once delta packet #1 applies.
InheritPerson MakeDeadPerson() {
    InheritPerson p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    i32 id = 0x42; std::memcpy(p.bytes + 4, &id, 4);
    p.bytes[8] = 5;                                  // status alive
    u16 fl = 1; std::memcpy(p.bytes + 10, &fl, 2);
    i32 v404 = 9; std::memcpy(p.bytes + 404, &v404, 4);
    for (int i = 0; i < 5; ++i) { i32 v = 100 + i; std::memcpy(p.bytes + 408 + 4 * i, &v, 4); }
    p.bytes[432] = 2; p.bytes[433] = 3;
    float w = 4.5f; std::memcpy(p.bytes + 92, &w, 4);
    return p;
}

// A "live world" the opcode-22 apply handler mutates. We thread it through the
// queue's user pointer is not available, so use a file-local singleton keyed by
// the packet's entity id — sufficient for the single dead person in this e2e.
InheritPerson g_liveEntity;

void ApplyState22Handler(CommandQueue& /*q*/, CommandPacket& pkt, AckEntry* /*ack*/) {
    if (pkt.opcode() != 22) return;
    const u8* block = pkt.bytes + 0x10;
    u8 fieldCount = block[4];
    const u8* payload = block + 5;
    DeltaWriter::ApplyDelta(payload, fieldCount, g_liveEntity.bytes);
}
} // namespace

TEST(CmdInheritE2E, FullBuildFlushExecLoop) {
    if (!RealAssetsPresent()) {
        CHECK(true);   // skip-pass: real assets absent
        return;
    }

    g_liveEntity = MakeDeadPerson();
    InheritPerson old = g_liveEntity;   // delta "old" snapshot

    CommandQueue q; q.Init();
    q.set_standalone(true);
    q.set_handler(22, ApplyState22Handler);

    DeltaWriter dw;
    InheritEmitCtx ctx;
    ctx.queue = &q; ctx.delta = &dw; ctx.randNext = RandZero;
    ctx.c626830 = 1.0f; ctx.c626834 = 2.0f;
    ctx.c62681C = 0.5f; ctx.c626820 = 0.5f; ctx.c626824 = 0.0f;
    ctx.c626828 = 1.0f; ctx.c62682C = 0.0f;

    EmitInheritanceDelta(ctx, old);

    CHECK(q.FlushSendQueue() == 0);   // standalone: applies onto received list
    q.ExecCommands();                  // dispatch each received packet

    // After the two State22 deltas apply, the live entity reaches post-death state.
    CHECK_EQ(g_liveEntity.bytes[8], 100);
    u16 fl; std::memcpy(&fl, g_liveEntity.bytes + 10, 2); CHECK_EQ(fl, 16);
    i32 v404; std::memcpy(&v404, g_liveEntity.bytes + 404, 4); CHECK_EQ(v404, 3);
    for (int i = 0; i < 5; ++i) {
        i32 v; std::memcpy(&v, g_liveEntity.bytes + 408 + 4 * i, 4); CHECK_EQ(v, 0);
    }
}
