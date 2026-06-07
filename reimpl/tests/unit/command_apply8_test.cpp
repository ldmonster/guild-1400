#include "test.h"

#include "sim/command_apply8.h"
#include "sim/command.h"
#include "sim/command_pending.h"

#include <cstring>
#include <random>

using namespace guild;
using namespace guild::sim;

namespace {

// Standard-clamp oracle (python-verified to equal the recovered clamp cores).
i32 clamp_oracle(i32 x, i32 lo, i32 hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// --- hook spy state --------------------------------------------------------
struct Spy {
    i32  money = 0;
    u8   blob23[124];
    u8   blob24[124];
    u8   tmpl34[0x2D];
    i32  sceneId = 0;
    u32  rng = 0;
    bool getRecordOk = false;
    CommandApply8Hooks::LawRecord rec{};
    bool applyCalled = false;
    int  appliedLawType = 0, appliedValue = 0;
    int  appliedArg0 = -1;
};
Spy* g_spy = nullptr;

i32  h_money() { return g_spy->money; }
void h_b23(void* d) { std::memcpy(d, g_spy->blob23, 124); }
void h_b24(void* d) { std::memcpy(d, g_spy->blob24, 124); }
i32  h_tmpl(void* d) { std::memcpy(d, g_spy->tmpl34, 0x2D); return g_spy->sceneId; }
u32  h_rng() { return g_spy->rng; }
int  h_getrec(int, CommandApply8Hooks::LawRecord* o) { *o = g_spy->rec; return g_spy->getRecordOk ? 1 : 0; }
void h_apply(int a0, int lt, int v) {
    g_spy->applyCalled = true; g_spy->appliedArg0 = a0;
    g_spy->appliedLawType = lt; g_spy->appliedValue = v;
}

void InstallSpy(Spy& s) {
    g_spy = &s;
    CommandApply8Hooks h{};
    h.localPlayerMoney = &h_money;
    h.copyState23Blob = &h_b23;
    h.copyState24Blob = &h_b24;
    h.objectTemplate34 = &h_tmpl;
    h.randNext = &h_rng;
    h.gesetzGetRecord = &h_getrec;
    h.gesetzRequestApply = &h_apply;
    SetCommandApply8Hooks(&h);
}

} // namespace

// ---------------------------------------------------------------------------
// Simple opcode builders — opcode byte + payload field layout.
// ---------------------------------------------------------------------------
TEST(CommandApply8_Build, Request20) {
    Spy s; InstallSpy(s);
    CommandQueue q; q.Init();
    i32 slot = QueueRequest20(q, 0x11223344, (i16)0x5566);
    CHECK(slot >= 0);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)20);
    CHECK_EQ(r.get32(0x10), (u32)0x11223344);
    CHECK_EQ(r.get16(0x14), (u16)0x5566);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, Pair36) {
    Spy s; InstallSpy(s);
    CommandQueue q; q.Init();
    i32 slot = QueueRequestPair36(q, 0x0A0B0C0D, 0x01020304);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)36);
    CHECK_EQ(r.get32(0x10), (u32)0x0A0B0C0D);
    CHECK_EQ(r.get32(0x14), (u32)0x01020304);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, State23And24) {
    Spy s; InstallSpy(s);
    for (int i = 0; i < 124; ++i) { s.blob23[i] = static_cast<u8>(i + 1); s.blob24[i] = static_cast<u8>(200 - i); }
    CommandQueue q; q.Init();

    i32 s23 = QueueRequestState23(q);
    CommandPacket& r23 = q.ring_slot(static_cast<u32>(s23));
    CHECK_EQ(r23.opcode(), (u8)23);
    bool ok23 = true;
    for (int i = 0; i < 124; ++i) if (r23.bytes[0x10 + i] != static_cast<u8>(i + 1)) ok23 = false;
    CHECK(ok23);

    i32 s24 = QueueRequestState24(q);
    CommandPacket& r24 = q.ring_slot(static_cast<u32>(s24));
    CHECK_EQ(r24.opcode(), (u8)24);
    bool ok24 = true;
    for (int i = 0; i < 124; ++i) if (r24.bytes[0x10 + i] != static_cast<u8>(200 - i)) ok24 = false;
    CHECK(ok24);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, Object34) {
    Spy s; InstallSpy(s);
    for (int i = 0; i < 0x2D; ++i) s.tmpl34[i] = static_cast<u8>(0xC0 + i);
    s.sceneId = 0x77665544;
    s.rng = 0xABCDEF01u;
    CommandQueue q; q.Init();

    u8 srcTemplate[0x2D];
    for (int i = 0; i < 0x2D; ++i) srcTemplate[i] = 0x10; // overwritten by the hook
    i32 slot = QueueRequestObject34(q, srcTemplate, 0x12345678);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)34);
    // payload[0] is the scene id (overrides the template's first dword).
    CHECK_EQ(r.get32(0x10), (u32)0x77665544);
    // bytes 4..0x24 come from the template untouched (0x25..0x2C are overwritten
    // by the present-flag dword and the nonce dword, faithful to the original).
    bool body = true;
    for (int i = 4; i < 0x25; ++i) if (r.bytes[0x10 + i] != static_cast<u8>(0xC0 + i)) body = false;
    CHECK(body);
    // present flag dword @ payload+0x25 == 1.
    CHECK_EQ(r.get32(0x10 + 0x25), (u32)1);
    // nonce @ payload+0x29 == RandNext().
    CHECK_EQ(r.get32(0x10 + 0x29), (u32)0xABCDEF01u);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, BuildingActionStart) {
    Spy s; InstallSpy(s);
    CommandQueue q; q.Init();
    i32 slot = EnqueueBuildingActionStart(q, "Bakery");
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)5);
    CHECK(std::strcmp(reinterpret_cast<const char*>(r.bytes + 0x10), "Bakery") == 0);
    // NUL-padded: byte after the name is 0.
    CHECK_EQ(r.bytes[0x10 + 6], (u8)0);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, PlayerMoneyState) {
    Spy s; InstallSpy(s);
    s.money = 0x0BADF00D;
    CommandQueue q; q.Init();
    i32 slot = SendPlayerMoneyState(q);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    // QueueRequestFlagBlob32(19, blob): opcode 32, flag byte 19 @+0x10, blob @+0x11.
    CHECK_EQ(r.opcode(), (u8)32);
    CHECK_EQ(r.bytes[0x10], (u8)19);
    CHECK_EQ(r.get32(0x11), (u32)0x0BADF00D);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, Op90ThunkSwapsArgs) {
    Spy s; InstallSpy(s);
    CommandQueue q; q.Init();
    // RequestBuildOp90(q, a1, a2) writes a1@+0x10, a2@+0x14. The thunk forwards
    // (b, a), so payload+0x10 == b and payload+0x14 == a.
    i32 slot = RequestBuildOp90_Thunk(q, /*a*/ 111, /*b*/ 222);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)90);
    CHECK_EQ(r.get32(0x10), (u32)222);
    CHECK_EQ(r.get32(0x14), (u32)111);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Build, Request40StagesBlock) {
    Spy s; InstallSpy(s);
    CommandQueue q; q.Init();
    PendingState pending;
    u8 src[0x114];
    for (int i = 0; i < 0x114; ++i) src[i] = static_cast<u8>(i);
    i32 slot = QueueRequest40(q, pending, src);
    CHECK(slot >= 0);
    CommandPacket& r = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(r.opcode(), (u8)40);
    // The staged block's length header == 0x114; the producer cleared the staging
    // block after generating the fragments.
    CHECK_EQ(pending.staged, (u32)0); // GeneratePendingPackets clears it
    SetCommandApply8Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// Justice clamp cores — golden vectors + random vs clamp oracle.
// ---------------------------------------------------------------------------
TEST(CommandApply8_Justice, ClampAbsoluteGolden) {
    CHECK_EQ(JusticeClampAbsolute(5, 0, 10), 5);
    CHECK_EQ(JusticeClampAbsolute(-3, 0, 10), 0);
    CHECK_EQ(JusticeClampAbsolute(99, 0, 10), 10);
    CHECK_EQ(JusticeClampAbsolute(0, 0, 10), 0);
    CHECK_EQ(JusticeClampAbsolute(10, 0, 10), 10);
}

TEST(CommandApply8_Justice, ClampDeltaGolden) {
    CHECK_EQ(JusticeClampDelta(5, 1, 3, 0, 10), 8);
    CHECK_EQ(JusticeClampDelta(5, -1, 8, 0, 10), 0);
    CHECK_EQ(JusticeClampDelta(5, 1, 99, 0, 10), 10);
    CHECK_EQ(JusticeClampDelta(2, -1, 1, 0, 10), 1);
}

TEST(CommandApply8_Justice, ClampMatchesOracle) {
    std::mt19937 rng(12345);
    for (int t = 0; t < 2000; ++t) {
        i32 lo = static_cast<i32>(rng() % 60);
        i32 hi = lo + static_cast<i32>(rng() % 120);
        i32 v = static_cast<i32>(rng() % 400) - 100;
        CHECK_EQ(JusticeClampAbsolute(v, lo, hi), clamp_oracle(v, lo, hi));
        i32 cur = static_cast<i32>(rng() % 200);
        int sign = (rng() & 1) ? 1 : -1;
        i32 d = static_cast<i32>(rng() % 150);
        CHECK_EQ(JusticeClampDelta(cur, sign, d, lo, hi), clamp_oracle(cur + sign * d, lo, hi));
    }
}

// ---------------------------------------------------------------------------
// Justice builders — record gate + emit.
// ---------------------------------------------------------------------------
TEST(CommandApply8_Justice, SetSeverityBuilder) {
    Spy s; InstallSpy(s);

    // No record -> reject (return 0), no emit.
    s.getRecordOk = false;
    CHECK_EQ(QueueSetJusticeSeverity(3, 5), 0);
    CHECK(!s.applyCalled);

    // Record present, in-band -> emit clamped value, return 1.
    s.getRecordOk = true; s.rec.min = 0; s.rec.max = 10; s.rec.adjustable = false;
    CHECK_EQ(QueueSetJusticeSeverity(3, 7), 1);
    CHECK(s.applyCalled);
    CHECK_EQ(s.appliedLawType, 3);
    CHECK_EQ(s.appliedValue, 7);
    CHECK_EQ(s.appliedArg0, 0);

    // Out-of-band AND adjustable -> reject.
    s.applyCalled = false; s.rec.adjustable = true;
    CHECK_EQ(QueueSetJusticeSeverity(3, 99), 0);
    CHECK(!s.applyCalled);

    // Out-of-band but NOT adjustable -> clamp + emit.
    s.applyCalled = false; s.rec.adjustable = false;
    CHECK_EQ(QueueSetJusticeSeverity(3, 99), 1);
    CHECK(s.applyCalled);
    CHECK_EQ(s.appliedValue, 10);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_Justice, AdjustSeverityBuilder) {
    Spy s; InstallSpy(s);
    s.getRecordOk = true; s.rec.min = 0; s.rec.max = 20; s.rec.adjustable = false;

    // current=5, +3 -> 8.
    CHECK_EQ(QueueAdjustJusticeSeverity(7, +1, 3, 5), 1);
    CHECK_EQ(s.appliedValue, 8);
    CHECK_EQ(s.appliedLawType, 7);

    // current=5, -10 -> clamp to 0.
    CHECK_EQ(QueueAdjustJusticeSeverity(7, -1, 10, 5), 1);
    CHECK_EQ(s.appliedValue, 0);

    // current=18, +9 -> clamp to 20.
    CHECK_EQ(QueueAdjustJusticeSeverity(7, +1, 9, 18), 1);
    CHECK_EQ(s.appliedValue, 20);

    // No record -> reject.
    s.getRecordOk = false; s.applyCalled = false;
    CHECK_EQ(QueueAdjustJusticeSeverity(7, +1, 1, 5), 0);
    CHECK(!s.applyCalled);
    SetCommandApply8Hooks(nullptr);
}
