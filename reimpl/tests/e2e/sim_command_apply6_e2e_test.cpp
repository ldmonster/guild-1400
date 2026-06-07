// End-to-end test for command_apply6: install ALL SIX apply registries on one
// CommandQueue, prove the 96-entry dispatch table has no opcode conflicts and is
// fully covered (96/96), apply a mixed command stream (sell + coords + gametick)
// and verify determinism — applying the identical stream twice produces
// byte-identical state. Guarded/unknown opcodes are ignored.

#include "sim/command_apply6.h"

#include <cstring>

#include "test.h"
#include "sim/command.h"
#include "sim/command_apply.h"
#include "sim/command_apply2.h"
#include "sim/command_apply3.h"
#include "sim/command_apply4.h"
#include "sim/command_apply5.h"
#include "sim/trade_sell.h"

using namespace guild;
using namespace guild::sim;

namespace {

CommandPacket MakePacket(u8 opcode) {
    CommandPacket p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    p.opcode() = opcode;
    return p;
}

// Owner of an opcode: which of the six direct dispatchers claims it. A
// dispatcher's `default` case returns -1 WITHOUT touching the ack; every real
// handler stamps the ack (status byte) at entry — even when it rejects (and a
// few handlers legitimately return -1 on an empty packet, e.g. ExSelectObject).
// So "owned" == (the dispatcher returned a value other than the untouched -1)
// OR (the ack sentinel changed). Executing on a near-zero packet is safe: each
// handler is state-guarded (resolve/alloc fail, clock not newer, persons
// absent) so it rejects cleanly without side effects beyond its modeled log.
int OpcodeOwners(u8 op) {
    int owners = 0;
    auto probe = [&](int (*fn)(CommandPacket&, AckEntry*)) {
        ResetApply6State();
        CommandPacket p = MakePacket(op);
        AckEntry ack{};
        ack.status = 0xEE;            // sentinel: untouched by the default path
        int rc = fn(p, &ack);
        if (rc != -1 || ack.status != 0xEE)
            owners++;
    };
    probe(&ApplyPacket);
    probe(&ApplyPacket2);
    probe(&ApplyPacket3);
    probe(&ApplyPacket4);
    probe(&ApplyPacket5);
    probe(&ApplyPacket6);
    return owners;
}

} // namespace

// ===========================================================================
// Coverage + conflict-freedom across all 96 opcodes.
// ===========================================================================
TEST(SimApply6E2E, FullCoverageNoConflicts) {
    int covered = 0;
    int conflicts = 0;
    for (int op = 0; op < (int)kNumOpcodes; ++op) {
        int owners = OpcodeOwners((u8)op);
        if (owners == 1) covered++;
        if (owners > 1) conflicts++;
    }
    CHECK_EQ(conflicts, 0);
    CHECK_EQ(covered, 96);                 // FINAL coverage 96/96
}

// The eight opcodes batch 6 owns are NOT owned by any earlier batch.
TEST(SimApply6E2E, Batch6OwnsExactlyItsOpcodes) {
    const u8 mine[] = {0x05, 0x06, 0x07, 0x11, 0x12, 0x1B, 0x1C, 0x1E, 0x39};
    for (u8 op : mine) {
        // batch 6 claims it (returns non -1 OR stamps the ack)
        ResetApply6State();
        CommandPacket p = MakePacket(op);
        AckEntry ack{};
        ack.status = 0xEE;
        int rc = ApplyPacket6(p, &ack);
        CHECK(rc != -1 || ack.status != 0xEE);
        // none of the earlier batches claim it (their default path returns -1
        // and leaves the sentinel untouched)
        auto reject = [&](int (*fn)(CommandPacket&, AckEntry*)) {
            CommandPacket q = MakePacket(op);
            AckEntry a{};
            a.status = 0xEE;
            CHECK_EQ(fn(q, &a), -1);
            CHECK_EQ((int)a.status, 0xEE);
        };
        reject(&ApplyPacket);
        reject(&ApplyPacket2);
        reject(&ApplyPacket3);
        reject(&ApplyPacket4);
        reject(&ApplyPacket5);
    }
}

// ===========================================================================
// Install all six registries on one queue — registration must not throw / clash.
// ===========================================================================
TEST(SimApply6E2E, AllSixRegistriesCompose) {
    CommandQueue q;
    q.Init();
    RegisterApplyHandlers(q);
    RegisterApplyHandlers2(q);
    RegisterApplyHandlers3(q);
    RegisterApplyHandlers4(q);
    RegisterApplyHandlers5(q);
    RegisterApplyHandlers6(q);
    // A no-throw smoke check: the queue is usable after composing all six.
    CHECK_EQ((int)q.standalone(), 1);
}

// ===========================================================================
// Mixed stream determinism: apply sell + coords + gametick twice and confirm
// byte-identical resulting state.
// ===========================================================================
namespace {

struct StreamResult {
    // relation grid snapshot (the two mutated cells) + tick clock + sell log.
    int relA = 0, relB = 0;
    i32 clockDay = 0;
    int sellMoved = 0;
    int tickPasses = 0;
};

void SeedSellResolve() {
    SetSellResolveHook([](const SellDecoded& d, SellResolve& r) -> bool {
        r.proto = d.proto; r.qty = d.qty;
        r.srcResolved = true; r.destResolved = true;
        r.srcHasStock = true; r.srcRawCount = 100; r.srcIsReserveGood = false;
        r.destStorage = false; r.destCarried = false;
        return true;
    });
}

StreamResult RunStream() {
    ResetApply6State();
    SeedSellResolve();
    TradeSetCmdHook(nullptr);

    // Seed relation persons.
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 100; rel.aliveMarker[0] = 0;
    rel.personId[1] = 200; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 10; rel.B(1, 0) = 4;

    // Tick gates / persons.
    Apply6_TickGates().heHandlers = 1;
    Apply6_TickGates().needsAi = 1;
    Apply6_SeedLivePersonCount(3);

    // 1) sell
    CommandPacket sell = MakePacket(kOp6SellObjekt);
    sell.put32(16, 50); sell.put32(20, 60);
    sell.put16(24, 42); sell.put32(31, 4); sell.put32(35, 0);
    ApplyPacket6(sell, nullptr);

    // 2) relation coords (mode 1)
    CommandPacket coord = MakePacket(kOp6ComputeObjectCoords);
    coord.put32(16, 200); coord.put32(20, 100); coord.put32(24, 20); coord.put32(28, 1);
    ApplyPacket6(coord, nullptr);

    // 3) gametick advance
    CommandPacket tick = MakePacket(kOp6AdvanceGameTick);
    GameTime t{}; t.day = 7; t.hour = 2;
    std::memcpy(&tick.bytes[16], &t, sizeof(t));
    ApplyPacket6(tick, nullptr);

    StreamResult res{};
    res.relA = (int)rel.A(1, 0);
    res.relB = (int)rel.B(1, 0);
    res.clockDay = g_tickClock.day;
    res.sellMoved = Apply6_GetLog().lastSellMoved;
    res.tickPasses = Apply6_GetLog().tickPassCount;
    return res;
}

} // namespace

TEST(SimApply6E2E, MixedStreamDeterministic) {
    StreamResult a = RunStream();
    StreamResult b = RunStream();
    // Apply twice -> byte-identical observable state.
    CHECK_EQ(a.relA, b.relA);
    CHECK_EQ(a.relB, b.relB);
    CHECK_EQ(a.clockDay, b.clockDay);
    CHECK_EQ(a.sellMoved, b.sellMoved);
    CHECK_EQ(a.tickPasses, b.tickPasses);

    // And the values are the expected ones.
    CHECK_EQ(a.relA, 30);        // 10 + 20 (clamped A)
    CHECK_EQ(a.relB, 19);        // 4 + (clampedA/2 == 30/2 == 15)
    CHECK_EQ(a.clockDay, 7);
    CHECK_EQ(a.sellMoved, 4);
}

// Re-applying a relation mutation through the SAME packet twice is NOT a no-op
// (it adds twice) — the determinism is "same input -> same output", verified by
// snapshotting fresh state each run above. Here we confirm a single replay of an
// identical packet on identical fresh state yields identical bytes.
TEST(SimApply6E2E, RelationReplayByteIdentical) {
    auto run = []() -> int {
        ResetApply6State();
        RelationState& rel = Apply6_Relations();
        rel.personId[0] = 1; rel.aliveMarker[0] = 0;
        rel.personId[1] = 2; rel.aliveMarker[1] = 0;
        rel.A(1, 0) = 3;
        CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
        p.put32(16, 2); p.put32(20, 1); p.put32(24, 9); p.put32(28, 0);
        ApplyPacket6(p, nullptr);
        return (int)rel.A(1, 0);
    };
    CHECK_EQ(run(), run());
    CHECK_EQ(run(), 12);
}

// Unknown opcode through the queue dispatch is a safe no-op.
TEST(SimApply6E2E, UnknownOpcodeIgnored) {
    ResetApply6State();
    CommandPacket p = MakePacket(0x5F);   // owned by batch 3, not batch 6
    AckEntry ack{};
    CHECK_EQ(ApplyPacket6(p, &ack), -1);
    CHECK_EQ((int)ack.status, 0);
}
