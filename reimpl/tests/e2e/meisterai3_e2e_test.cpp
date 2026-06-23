// e2e for src/ai/meisterai3.cpp — drive the session-decision orchestrator across
// the scorer family it dispatches into, with deterministic leaves installed, and
// confirm it stores the right scorer result per session type and emits the type-3
// coordination command. Then run a small "tavern visit + event aggregation" flow.
#include "ai/meisterai3.h"
#include "ai/aimethod2.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using guild::ai::MethodEnv;
using guild::ai::Meister3Hooks;
using guild::ai::SessionInputs;

namespace {
const int* g_seq = nullptr; int g_len = 0; int g_pos = 0;
int RngStub(u16 n) { int r = (g_pos < g_len) ? g_seq[g_pos] : 0; ++g_pos; return n ? (r % n) : 0; }
void Reset(const int* s, int l) { g_seq = s; g_len = l; g_pos = 0; }
MethodEnv Env() { MethodEnv e = guild::ai::DefaultMethodEnv(); e.rng_mod = RngStub; return e; }
} // namespace

TEST(Meisterai3E2E, SessionDispatchAcrossScorers) {
    MethodEnv e = Env();

    // Type 2 (RandomValue @0x467994): returns rng_mod(7) — modulus hardcoded 7 in binary.
    {
        SessionInputs in;
        in.sessionType = 2; in.match2 = true;
        static const int seq[] = {13};  // 13 % 7 = 6
        Reset(seq, 1);
        CHECK_EQ(guild::ai::EvaluateSessionDecision(in, e), 6);
    }

    // Type 5 (purchase desire): delegates to EvalPurchaseDesire.
    {
        SessionInputs in;
        in.sessionType = 5; in.match5 = true;
        in.purWealthGauge = 8.0f; in.purLawCur = 3; in.purFav = 100.0f;
        in.purByte358 = true; in.purByte13 = 1;
        int golden = guild::ai::EvalPurchaseDesire(8.0f, 3, 100.0f, true, 1, e);
        CHECK_EQ(guild::ai::EvaluateSessionDecision(in, e), golden);
        CHECK_EQ(golden, 2);
    }

    // Type 0 (social): no id match -> result stays the session-type byte (0).
    {
        SessionInputs in;
        in.sessionType = 0; in.match0 = false;
        CHECK_EQ(guild::ai::EvaluateSessionDecision(in, e), 0);
    }

    // Type 3 (favorability): result 1 emits the coord-27 command.
    {
        static int coordCalls = 0; coordCalls = 0;
        Meister3Hooks h;
        h.queue_coord27 = [](int, int, int code) { ++coordCalls; CHECK_EQ(code, 35); };
        Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

        SessionInputs in;
        in.sessionType = 3; in.match3 = true;
        in.favAFound = true; in.favBA = 100.0f; in.favCA = 0.0f;  // d-e>20 -> CompareFavorability=1
        int cmd = 0;
        int r = guild::ai::EvaluateSessionDecision(in, e, &cmd);
        CHECK_EQ(r, 1);
        CHECK_EQ(cmd, 1);
        CHECK_EQ(coordCalls, 1);

        guild::ai::SetMeister3Hooks(prev);
    }

    // Type 4 (conflict): delegates to ShouldInitiateConflict.
    {
        SessionInputs in;
        in.sessionType = 4; in.match4 = true;
        in.conflictGate = false; in.conflictFavA = 100.0f; in.conflictFavB = 0.0f;
        in.conflictField4 = 0.0f; in.conflictFlag68 = false;
        int golden = guild::ai::ShouldInitiateConflict(false, 100.0f, 0.0f, 0.0f, false, e);
        CHECK_EQ(guild::ai::EvaluateSessionDecision(in, e), golden);
    }

    // Unknown type -> returns the type byte unchanged.
    {
        SessionInputs in; in.sessionType = 99;
        CHECK_EQ(guild::ai::EvaluateSessionDecision(in, e), 99);
    }
}

TEST(Meisterai3E2E, TavernVisitAndCommandEmission) {
    // A small flow: an actor "drinks" (ApplyDrinkAction debits + resets), then the
    // master issues the idempotent supervision commands (RequestCmd107/122) — the
    // second call is a no-op because a handler now exists. All emissions funnel
    // through the same installed slot-reset hook.
    static std::vector<int> debits;
    static std::vector<int> resetKinds;
    debits.clear(); resetKinds.clear();
    Meister3Hooks h;
    h.request_build_op90 = [](int, int amt) { debits.push_back(amt); };
    h.queue_slot_reset28 = [](int, u8 kind, int, int) { resetKinds.push_back((int)kind); };
    Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

    char code = guild::ai::ApplyDrinkAction(/*build*/ 7, /*cost*/ 120, /*slot*/ 3, /*kind*/ 2);
    CHECK_EQ(code, (char)4);
    CHECK_EQ((int)debits.size(), 1);
    CHECK_EQ(debits[0], -120);
    CHECK_EQ((int)resetKinds.size(), 1);   // the drink slot-reset
    CHECK_EQ(resetKinds[0], 2);            // slot kind byte

    // First supervision request emits (no pending handler); the repeat is inert.
    CHECK_EQ(guild::ai::RequestCmd107(/*exists*/ false, /*master*/ 7), true);
    CHECK_EQ(guild::ai::RequestCmd107(/*exists*/ true, /*master*/ 7), false);
    CHECK_EQ(guild::ai::RequestCmd122(/*exists*/ false, /*master*/ 7), true);
    CHECK_EQ((int)resetKinds.size(), 3);   // drink + 107 + 122
    CHECK_EQ(resetKinds[1], 107);
    CHECK_EQ(resetKinds[2], 122);

    guild::ai::SetMeister3Hooks(prev);
}
