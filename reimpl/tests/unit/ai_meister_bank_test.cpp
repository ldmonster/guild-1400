// Golden-vector unit tests for VIBE_Ai_CalcBankmeister (gilde.exe 0x459264),
// the Bank-Meister per-frame AI calculator. Each vector is hand-derived from the
// decompile/disasm so the reconstruction stays verifiably 1:1. Self-contained:
// no game assets; deterministic synthetic NPC/person/object state; injected
// engine-leaf hooks; captured command sink; seeded RNG for the random branches.
#include "tests/framework/test.h"

#include "sim/ai_meister_bank.h"
#include "sim/entity.h"
#include "sim/ai_meister_internal.h"   // aimei::makeObjHandle
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// record byte offsets (mirror the .cpp / disasm)
constexpr int kM_bldgRec = 0x16C;
constexpr int kM_budget  = 0x1B8;
constexpr int kM_flags2  = 0x1C8;
constexpr int kB_id1     = 0x01;
constexpr int kB_owner39 = 0x27;
constexpr int kB_cash101 = 0x65;
constexpr int kB_root93  = 0x5D;
constexpr int kP_kind    = 0x02;

inline void wr32(void* b, int off, i32 v) { std::memcpy(static_cast<u8*>(b) + off, &v, 4); }
inline void wr16(void* b, int off, u16 v) { std::memcpy(static_cast<u8*>(b) + off, &v, 2); }
inline void wr8 (void* b, int off, u8 v)  { *(static_cast<u8*>(b) + off) = v; }
inline u8   rd8 (const void* b, int off)  { return *(static_cast<const u8*>(b) + off); }

// --- shared leaf-hook state (single-threaded test harness) -------------------
int   g_heCount = 0;            // handler-count hook return
int   g_coinReturns[8];         // per-call coin counts (FIFO)
int   g_coinIdx = 0;
int   g_coinCalls = 0;
i32   g_lastCoinScene = -1;
u8    g_lastCoinDenom = 0xFF;

int heCountHook(int /*filterCode*/, i32 /*key*/) { return g_heCount; }
int coinHook(i32 scene, u8 denom) {
    g_lastCoinScene = scene;
    g_lastCoinDenom = denom;
    ++g_coinCalls;
    int v = (g_coinIdx < 8) ? g_coinReturns[g_coinIdx] : 0;
    ++g_coinIdx;
    return v;
}

BankAiLeaves makeLeaves() {
    BankAiLeaves L;
    L.heCountMatching     = &heCountHook;
    L.coinCountAtLocation = &coinHook;
    return L;
}

// Build a fresh bank scenario in the global arrays. meister=person[mIdx],
// building=object[bIdx]. Returns the meister record base.
struct Scn {
    u8* meister;
    int bIdx;
    int owner;
};
Scn setupBank(int mIdx, int bIdx, int owner, i32 budget, i32 cash,
              i32 bldgId, i32 sceneRoot, u8 ownerKind) {
    ResetEntityArrays();
    g_heCount = 0; g_coinIdx = 0; g_coinCalls = 0;
    std::memset(g_coinReturns, 0, sizeof(g_coinReturns));
    g_lastCoinScene = -1; g_lastCoinDenom = 0xFF;

    u8* m = reinterpret_cast<u8*>(&g_persons[mIdx]);
    u8* b = reinterpret_cast<u8*>(&g_objects[bIdx]);
    std::memset(m, 0, sizeof(Person));
    std::memset(b, 0, sizeof(ObjectRec));

    wr32(m, kM_budget, budget);
    wr8 (m, kM_flags2, 0);
    wr32(m, kM_bldgRec, aimei::makeObjHandle(bIdx));   // 64-bit-safe handle column

    wr32(b, kB_id1, bldgId);
    wr16(b, kB_owner39, static_cast<u16>(owner));
    wr32(b, kB_cash101, cash);
    wr32(b, kB_root93, sceneRoot);

    // owner person record (kind byte) + parallel id column.
    u8* op = reinterpret_cast<u8*>(&g_persons[owner]);
    std::memset(op, 0, sizeof(Person));
    wr8(op, kP_kind, ownerKind);
    g_personIds[owner] = 0xABCD;                        // owner actor id

    return Scn{m, bIdx, owner};
}

} // namespace

// ===========================================================================
// guard: flags2 bit 8 already set -> immediate no-op, nothing queued.
// ===========================================================================
TEST(AiMeisterBank, AlreadyDoneSkips) {
    Scn s = setupBank(1, 2, 3, /*budget*/1000, /*cash*/0, /*id*/77, /*root*/9, /*kind*/0);
    wr8(s.meister, kM_flags2, 8);  // bit 8 set
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);
    CHECK_EQ(sink.emitted.size(), 0u);
    // flag stays set.
    CHECK(rd8(s.meister, kM_flags2) & 8);
    ResetBankAiState();
}

// ===========================================================================
// reserve: |cash - lawTarget(16)| > 3, cash below target -> +1 reserve step.
// budget 0 so all coin thresholds are 0 -> no coin ops; in-band default melt
// gated off (kind 0, cnt 0 not > 0).
// ===========================================================================
TEST(AiMeisterBank, ReserveStepUpWhenBelow) {
    // cash 0, lawTarget 16, diff -16, abs 16 > 3 -> target = cash+1 = 1.
    Scn s = setupBank(1, 2, 3, /*budget*/0, /*cash*/0, /*id*/77, /*root*/9, /*kind*/0);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);
    // first command = the reserve set.
    CHECK(sink.emitted.size() >= 1u);
    const BankCommand& c = sink.emitted[0];
    CHECK_EQ((int)c.kind, (int)BankCommand::kReserveSet);
    CHECK_EQ(c.buildingId, 77);
    CHECK_EQ(c.reserveValue, 1);
    CHECK_EQ((int)c.reserveColumn, 0x65);
    CHECK(rd8(s.meister, kM_flags2) & 8);  // done flag set
    ResetBankAiState();
}

// reserve: cash way above target -> -1 step.
TEST(AiMeisterBank, ReserveStepDownWhenAbove) {
    // cash 100, lawTarget 16, diff 84 > 3, cash>target -> target = cash-1 = 99.
    Scn s = setupBank(1, 2, 3, /*budget*/0, /*cash*/100, /*id*/55, /*root*/9, /*kind*/0);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);
    CHECK(sink.emitted.size() >= 1u);
    CHECK_EQ((int)sink.emitted[0].kind, (int)BankCommand::kReserveSet);
    CHECK_EQ(sink.emitted[0].reserveValue, 99);
    ResetBankAiState();
}

// reserve: |diff| <= 3 with no handlers and lawTarget-2 < cash -> downward random
// step, result clamped >= max(lawTarget-2, ...) and >= 4. With cash=16=target,
// lawTarget-2=14 < 16 -> downward; target = (16-1)-rand in {15,14,13}, then
// max(target,14) -> in {14,15}, then >=4. Always within [14,15].
TEST(AiMeisterBank, ReserveRandomDownClamped) {
    for (u32 seed = 1; seed <= 6; ++seed) {
        Scn s = setupBank(1, 2, 3, /*budget*/0, /*cash*/16, /*id*/8, /*root*/9, /*kind*/0);
        guild::crt::Srand(seed);
        BankCmdSink sink; g_bankCmdSink = &sink;
        BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;  // heCount=0
        CalcBankmeister(s.meister);
        CHECK(sink.emitted.size() >= 1u);
        const BankCommand& c = sink.emitted[0];
        CHECK_EQ((int)c.kind, (int)BankCommand::kReserveSet);
        CHECK(c.reserveValue >= 14 && c.reserveValue <= 15);
        ResetBankAiState();
    }
}

// reserve: |diff| <= 3 with handlers > 3 and cash < lawTarget -> upward random
// step, clamped <= lawTarget+3. cash=15, target=16, handlers=5: target =
// (15+1)+rand in {16,17,18}, min(.,19) -> in {16,17,18}.
TEST(AiMeisterBank, ReserveRandomUpClamped) {
    for (u32 seed = 1; seed <= 6; ++seed) {
        Scn s = setupBank(1, 2, 3, /*budget*/0, /*cash*/15, /*id*/8, /*root*/9, /*kind*/0);
        g_heCount = 5;
        guild::crt::Srand(seed);
        BankCmdSink sink; g_bankCmdSink = &sink;
        BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;
        CalcBankmeister(s.meister);
        CHECK(sink.emitted.size() >= 1u);
        const BankCommand& c = sink.emitted[0];
        CHECK_EQ((int)c.kind, (int)BankCommand::kReserveSet);
        CHECK(c.reserveValue >= 16 && c.reserveValue <= 18);
        ResetBankAiState();
    }
}

// reserve: |diff| <= 3, no handlers, lawTarget-2 >= cash, handlers !>3 -> NO step.
// cash=13, target=16, diff -3 abs 3 (not >3). lawTarget-2 = 14 >= 13 so downward
// not taken; handlers 0 not > 3 -> no reserve emit. budget 0 -> no coin ops.
TEST(AiMeisterBank, ReserveNoStepInBand) {
    Scn s = setupBank(1, 2, 3, /*budget*/0, /*cash*/13, /*id*/8, /*root*/9, /*kind*/0);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;
    CalcBankmeister(s.meister);
    // no reserve command (and budget 0 -> no coin commands either).
    for (const auto& c : sink.emitted)
        CHECK_EQ((int)c.kind, (int)BankCommand::kCoinOp); // none should be reserve
    bool anyReserve = false;
    for (const auto& c : sink.emitted)
        if (c.kind == BankCommand::kReserveSet) anyReserve = true;
    CHECK(!anyReserve);
    ResetBankAiState();
}

// ===========================================================================
// coin balancing: budget 100 -> low=15, want=25, high=30, melt-to=20.
// denom1 count 0  (<15)  -> MINT (25-0)*1.03 = 25.75 -> trunc 25, accum += 25
// denom2 count 40 (>30)  -> MELT (40-20)*1.03 = 20.6 -> trunc 20, accum -= 20
// denom3 count 20 (in-band 15..30) -> no op
// default recount (currency 0) returns 10 < 1.5*100=150 -> mint rem=(100-(25-20))
//   = 95 ; 95*0.8 = 76 -> trunc 76 MINT. then done (early return).
// ===========================================================================
TEST(AiMeisterBank, CoinBalanceMintMeltDefault) {
    Scn s = setupBank(1, 2, 3, /*budget*/100, /*cash*/16, /*id*/42, /*root*/77, /*kind*/0);
    // cash==lawTarget(16) -> diff 0 abs<=3; handlers 0; lawTarget-2=14<16 -> a
    // downward reserve step happens first. We focus on the coin ops below.
    g_coinReturns[0] = 0;   // denom1
    g_coinReturns[1] = 40;  // denom2
    g_coinReturns[2] = 20;  // denom3
    g_coinReturns[3] = 10;  // default recount
    guild::crt::Srand(1);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);

    // collect coin ops in order
    int nMint = 0, nMelt = 0;
    i32 mintAmts[8]; int mi = 0;
    for (const auto& c : sink.emitted) {
        if (c.kind != BankCommand::kCoinOp) continue;
        CHECK_EQ(c.actorId, (i32)0xABCD);
        CHECK_EQ(c.buildingId, 42);
        if (c.mint) { mintAmts[mi++] = c.amount; ++nMint; } else ++nMelt;
    }
    // denom1 mint(25), default mint(76) ; denom2 melt(20)
    CHECK_EQ(nMint, 2);
    CHECK_EQ(nMelt, 1);
    CHECK_EQ(mintAmts[0], 25);   // (25-0)*1.03 trunc
    CHECK_EQ(mintAmts[1], 76);   // (100-5)*0.8 trunc
    // 4 coin queries: denom 1,2,3 + default
    CHECK_EQ(g_coinCalls, 4);
    CHECK_EQ((int)g_lastCoinScene, 77);
    ResetBankAiState();
}

// default-MELT path: count high so default recount >= 1.5*budget; owner kind not
// 6/7; 2*budget < cnt -> melt cnt - 2.25*budget.
//   budget 100 -> low15/want25/high30/meltto20. All 3 denoms in-band (20) -> no
//   per-denom ops, accum 0. default recount = 400 >= 150 -> skip mint. kind 0,
//   2*100=200 < 400 -> melt 400 - 2.25*100=225 -> 175.
TEST(AiMeisterBank, DefaultMeltWhenFlush) {
    Scn s = setupBank(1, 2, 3, /*budget*/100, /*cash*/16, /*id*/42, /*root*/5, /*kind*/0);
    g_coinReturns[0] = 20; g_coinReturns[1] = 20; g_coinReturns[2] = 20;
    g_coinReturns[3] = 400;
    guild::crt::Srand(1);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);

    int nMint = 0, nMelt = 0; i32 meltAmt = -1;
    for (const auto& c : sink.emitted) {
        if (c.kind != BankCommand::kCoinOp) continue;
        if (c.mint) ++nMint; else { ++nMelt; meltAmt = c.amount; }
    }
    CHECK_EQ(nMint, 0);
    CHECK_EQ(nMelt, 1);
    CHECK_EQ(meltAmt, 175);
    ResetBankAiState();
}

// default-melt skipped when owner kind == 6 or 7.
TEST(AiMeisterBank, DefaultMeltSkippedForKind6) {
    Scn s = setupBank(1, 2, 3, /*budget*/100, /*cash*/16, /*id*/42, /*root*/5, /*kind*/6);
    g_coinReturns[0] = 20; g_coinReturns[1] = 20; g_coinReturns[2] = 20;
    g_coinReturns[3] = 400;  // flush, but kind 6 -> no melt
    guild::crt::Srand(1);
    BankCmdSink sink; g_bankCmdSink = &sink;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;

    CalcBankmeister(s.meister);
    int nMelt = 0;
    for (const auto& c : sink.emitted)
        if (c.kind == BankCommand::kCoinOp && !c.mint) ++nMelt;
    CHECK_EQ(nMelt, 0);
    ResetBankAiState();
}

// null leaves: heCount 0 + coinCount 0 everywhere (the original's null/zero path).
// budget 50 -> low7/want12/high15/meltto10. Each denom cnt 0 (<7) -> MINT
// (12-0)*1.03=12.36 -> 12, accum=36. default cnt 0<75 -> mint rem=(50-36)=14,
// 14*0.8=11.2 -> 11. Reserve: cash 0 vs target 16, diff 16>3 -> +1 step.
TEST(AiMeisterBank, NullLeavesTakeZeroPath) {
    Scn s = setupBank(1, 2, 3, /*budget*/50, /*cash*/0, /*id*/9, /*root*/1, /*kind*/0);
    BankCmdSink sink; g_bankCmdSink = &sink;
    g_bankLeaves = nullptr;  // all leaves null -> heCount 0, coinCount 0

    CalcBankmeister(s.meister);
    bool sawReserve = false;
    int nMint = 0; i32 lastMint = -1;
    for (const auto& c : sink.emitted) {
        if (c.kind == BankCommand::kReserveSet) { sawReserve = true; CHECK_EQ(c.reserveValue, 1); }
        if (c.kind == BankCommand::kCoinOp && c.mint) { ++nMint; lastMint = c.amount; }
    }
    CHECK(sawReserve);
    CHECK_EQ(nMint, 4);          // 3 per-denom mints + 1 default mint
    CHECK_EQ(lastMint, 11);      // default: (50-36)*0.8 trunc
    CHECK(rd8(s.meister, kM_flags2) & 8);
    ResetBankAiState();
}

// no sink: logic runs (flag set) without capturing, no crash.
TEST(AiMeisterBank, NoSinkRunsClean) {
    Scn s = setupBank(1, 2, 3, /*budget*/100, /*cash*/0, /*id*/9, /*root*/1, /*kind*/0);
    g_bankCmdSink = nullptr;
    BankAiLeaves L = makeLeaves(); g_bankLeaves = &L;
    CalcBankmeister(s.meister);
    CHECK(rd8(s.meister, kM_flags2) & 8);
    ResetBankAiState();
}
