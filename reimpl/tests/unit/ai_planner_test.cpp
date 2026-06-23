// Unit tests for the Guild AI behavior planner (guild::ai).
//   - method stack push/pop/peek/contains/overflow
//   - score-table best-selection picks the expected method (golden score vector)
//   - need decay reduces the need by the expected amount per tick and clamps
//   - the seeded random need-pick selects the expected need (golden, crt::Srand)
//
// Golden RNG vectors computed with python3 against the LCG
//   state = state*1103515245 + 12345 (mod 2^32); return (state>>16)&0x7FFF.
#include "tests/framework/test.h"

#include "ai/methodstack.h"
#include "ai/score.h"
#include "ai/needs.h"
#include "ai/method.h"
#include "crt/rand.h"

using namespace guild;

// ---------------------------------------------------------------------------
// Method stack
// ---------------------------------------------------------------------------
TEST(AiPlannerStack, PushPopPeek) {
    ai::MethodStack s;
    CHECK(s.empty());
    CHECK_EQ(s.count(), 0);

    CHECK_EQ(s.Push(10), 1);    // returns post-increment count
    CHECK_EQ(s.Push(20), 2);
    CHECK_EQ(s.Push(30), 3);
    CHECK_EQ(s.count(), 3);

    CHECK_EQ((int)s.Peek(0), 10);
    CHECK_EQ((int)s.Peek(1), 20);
    CHECK_EQ((int)s.Peek(2), 30);
    CHECK_EQ((int)s.Top(), 30);

    s.Pop();
    CHECK_EQ(s.count(), 2);
    CHECK_EQ((int)s.Top(), 20);
    CHECK(!s.Contains(30));
    CHECK(s.Contains(10));
    CHECK(s.Contains(20));

    s.Pop();
    s.Pop();
    CHECK(s.empty());
    // Pop on empty is a no-op clamp (orig had no guard; reimpl never goes < 0).
    s.Pop();
    CHECK_EQ(s.count(), 0);
}

TEST(AiPlannerStack, Overflow) {
    ai::MethodStack s;
    // Push beyond capacity: count keeps rising but writes are clamped.
    for (int i = 0; i < ai::kMethodStackCapacity + 5; ++i)
        s.Push((u8)(i & 0xFF));
    CHECK_EQ(s.count(), ai::kMethodStackCapacity + 5);
    // The last in-range slot still holds its value; out-of-range peeks return 0.
    CHECK_EQ((int)s.Peek(ai::kMethodStackCapacity - 1),
             (int)((ai::kMethodStackCapacity - 1) & 0xFF));
    CHECK_EQ((int)s.Peek(ai::kMethodStackCapacity), 0);
    CHECK_EQ((int)s.Peek(ai::kMethodStackCapacity + 4), 0);
}

TEST(AiPlannerStack, ContainsEmpty) {
    ai::MethodStack s;
    CHECK(!s.Contains(0));
    CHECK(!s.Contains(7));
}

// ---------------------------------------------------------------------------
// Score-table planner — golden best-selection
// ---------------------------------------------------------------------------
namespace {
// Per-method score table the eval callbacks read. We stash a score per method id
// in a file-static so the callbacks are plain function pointers (matching the
// binary's tbl[9]/tbl[10] contract).
float g_scoreA[64] = {};
float g_scoreB[64] = {};
u8    g_actionByte[64] = {};

ai::EvalResult EvalFromTable(const ai::Method& m, int) {
    ai::EvalResult r;
    r.produced = true;
    r.scoreA = g_scoreA[m.id];
    r.scoreB = g_scoreB[m.id];
    r.frameA.bytes[0] = m.id;            // tag the frame so we can identify the winner
    r.frameB.bytes[0] = (u8)(m.id ^ 0xFF);
    return r;
}
u8 ApplyAccept(const ai::Method& m, int, ai::EvalResult&) {
    return g_actionByte[m.id];           // non-zero => accept; identifies chosen method
}
} // namespace

TEST(AiPlannerScore, PicksHighestCriterionA) {
    ai::Planner p;
    p.set_current_class(0);

    // Build a small catalog: ids 1,2,3 with distinct scores. Higher scoreA wins
    // (and scoreA >= scoreB so criterion A is selected). Method 2 has the top A.
    for (int i = 0; i < 64; ++i) { g_scoreA[i] = -1e30f; g_scoreB[i] = -1e30f; g_actionByte[i] = 0; }
    struct { u8 id; u8 cls; float a; float b; u8 act; } spec[] = {
        {1, 1, 10.0f, 5.0f, 41},
        {2, 2, 50.0f, 5.0f, 42},   // highest A
        {3, 3, 30.0f, 5.0f, 43},
    };
    for (int i = 0; i < p.method_count(); ++i) p.method(i) = ai::Method{};
    for (int i = 0; i < 3; ++i) {
        ai::Method& m = p.method(i);
        m.id = spec[i].id; m.classId = spec[i].cls; m.enabled = true;
        m.eval = EvalFromTable; m.apply = ApplyAccept;
        g_scoreA[spec[i].id] = spec[i].a;
        g_scoreB[spec[i].id] = spec[i].b;
        g_actionByte[spec[i].id] = spec[i].act;
    }

    ai::ActionFrame fa, fb;
    u8 chosen = p.SelectBest(/*personIndex*/0, &fa, &fb);
    CHECK_EQ((int)chosen, 42);            // method 2's action byte
    CHECK_EQ((int)fa.bytes[0], 2);        // winning primary frame is method 2's
}

TEST(AiPlannerScore, PicksCriterionBWhenAUnset) {
    ai::Planner p;
    p.set_current_class(0);
    for (int i = 0; i < 64; ++i) { g_scoreA[i] = -1e30f; g_scoreB[i] = -1e30f; g_actionByte[i] = 0; }
    for (int i = 0; i < p.method_count(); ++i) p.method(i) = ai::Method{};

    // All methods have very negative A but B is meaningfully ranked: criterion B
    // wins because criterion A was never raised above the sentinel for acceptance.
    // Here A == sentinel for all (so "A ever set" is the -1e30 bit pattern, which
    // IS non-zero) — to exercise the B path we make B strictly dominate and A all
    // equal so the first-seen A is kept but B selects method 3 (top B). Since A is
    // equal across methods, the tie keeps the LAST method that ties on A for B but
    // the planner's final compare uses bestA>=bestB: make bestB > bestA so B wins.
    struct { u8 id; u8 cls; float a; float b; u8 act; } spec[] = {
        {1, 1, 1.0f, 10.0f, 41},
        {2, 2, 1.0f, 90.0f, 42},  // highest B
        {3, 3, 1.0f, 30.0f, 43},
    };
    for (int i = 0; i < 3; ++i) {
        ai::Method& m = p.method(i);
        m.id = spec[i].id; m.classId = spec[i].cls; m.enabled = true;
        m.eval = EvalFromTable; m.apply = ApplyAccept;
        g_scoreA[spec[i].id] = spec[i].a;
        g_scoreB[spec[i].id] = spec[i].b;
        g_actionByte[spec[i].id] = spec[i].act;
    }
    ai::ActionFrame fa, fb;
    u8 chosen = p.SelectBest(0, &fa, &fb);
    // bestA == 1.0 (method 1, first to reach it), bestB == 90.0 (method 2).
    // 1.0 >= 90.0 is false => criterion B winner (method 2) is returned.
    CHECK_EQ((int)chosen, 42);
    CHECK_EQ((int)fa.bytes[0], 2);        // criterion-B primary frame is method 2's
}

TEST(AiPlannerScore, SkipsSameClassAndDisabled) {
    ai::Planner p;
    p.set_current_class(2);               // person currently doing class-2 method
    for (int i = 0; i < 64; ++i) { g_scoreA[i] = -1e30f; g_scoreB[i] = -1e30f; g_actionByte[i] = 0; }
    for (int i = 0; i < p.method_count(); ++i) p.method(i) = ai::Method{};

    struct { u8 id; u8 cls; bool en; float a; u8 act; } spec[] = {
        {2, 2, true,  99.0f, 42},  // same class as current -> skipped despite top A
        {3, 3, false, 80.0f, 43},  // disabled -> skipped
        {4, 4, true,  20.0f, 44},  // the only eligible candidate
    };
    for (int i = 0; i < 3; ++i) {
        ai::Method& m = p.method(i);
        m.id = spec[i].id; m.classId = spec[i].cls; m.enabled = spec[i].en;
        m.eval = EvalFromTable; m.apply = ApplyAccept;
        g_scoreA[spec[i].id] = spec[i].a;
        g_scoreB[spec[i].id] = spec[i].a;
        g_actionByte[spec[i].id] = spec[i].act;
    }
    ai::ActionFrame fa, fb;
    u8 chosen = p.SelectBest(0, &fa, &fb);
    CHECK_EQ((int)chosen, 44);            // only method 4 is eligible
}

// ---------------------------------------------------------------------------
// Need decay — golden amounts (crt::Srand for determinism)
// ---------------------------------------------------------------------------
TEST(AiPlannerNeeds, DecayAmountGolden) {
    // seed=1   -> rand0=16838 -> mag=6  -> amount=240
    // seed=42  -> rand0=19081 -> mag=9  -> amount=360
    // seed=1000-> rand0=28322 -> mag=2  -> amount=80
    struct { u32 seed; int amount; u32 lowNibble; } golden[] = {
        {1, 240, 6}, {42, 360, 9}, {1000, 80, 2},
    };
    for (auto& g : golden) {
        crt::Srand(g.seed);
        ai::NeedAgent a;
        a.type = 7; a.id = 1234; a.needWord = 0; // low nibble clear -> decay runs
        int amount = ai::ApplyRandomDecayField(a, nullptr);
        CHECK_EQ(amount, g.amount);
        CHECK_EQ((unsigned)(a.needWord & 0xF), g.lowNibble); // mag stored in low nibble
    }
}

TEST(AiPlannerNeeds, DecaySkipsWhenLowNibbleSet) {
    crt::Srand(1);
    ai::NeedAgent a;
    a.type = 7; a.id = 1; a.needWord = 0x5; // low nibble already set -> early-out
    int amount = ai::ApplyRandomDecayField(a, nullptr);
    CHECK_EQ(amount, 0);
    CHECK_EQ((unsigned)a.needWord, 0x5u);   // untouched
    // And no RNG was consumed: the very next draw equals seed-1's first draw.
    CHECK_EQ(crt::RandNext(), 16838);
}

// ---------------------------------------------------------------------------
// Seeded random need-pick — golden selections
// ---------------------------------------------------------------------------
TEST(AiPlannerNeeds, PickFourAllEligibleGolden) {
    // needWord with bits in all four groups => all 4 slots eligible.
    //   group bits: 0xF0(id2), 0xF00(id3), 0x3000(id4), 0x1C000(id5)
    // seed=1  -> start=2 -> slot2 -> id4
    // seed=7  -> start=0 -> slot0 -> id2
    // seed=42 -> start=1 -> slot1 -> id3
    // seed=12345 -> start=0 -> slot0 -> id2
    struct { u32 seed; u8 id; } golden[] = {{1,4},{7,2},{42,3},{12345,2}};
    for (auto& g : golden) {
        crt::Srand(g.seed);
        ai::NeedAgent a;
        a.id = 9; a.needWord = 0xF0 | 0xF00 | 0x3000 | 0x1C000;
        u8 id = ai::PickRandomFlagFromFourA(a, nullptr);
        CHECK_EQ((int)id, (int)g.id);
    }
}

TEST(AiPlannerNeeds, PickFourPartialEligibleGolden) {
    // Only groups for slots 2 and 3 set => forward scan lands on slot 2 (id4).
    // seed=1 start2->slot2; seed=7 start0->slot2; seed=42 start1->slot2.
    for (u32 seed : {1u, 7u, 42u}) {
        crt::Srand(seed);
        ai::NeedAgent a;
        a.id = 9; a.needWord = 0x3000 | 0x1C000; // slots 2,3 only
        u8 id = ai::PickRandomFlagFromFourA(a, nullptr);
        CHECK_EQ((int)id, 4);
    }
}

TEST(AiPlannerNeeds, PickFourClearsBits) {
    crt::Srand(7); // start0 -> slot0 -> id2, clears 0xF0
    ai::NeedAgent a;
    a.id = 9; a.needWord = 0xF0 | 0xF00 | 0x3000 | 0x1C000;
    u8 id = ai::PickRandomFlagFromFourA(a, nullptr);
    CHECK_EQ((int)id, 2);
    CHECK_EQ((unsigned)(a.needWord & 0xF0), 0u);          // group cleared
    CHECK_EQ((unsigned)(a.needWord & (0xF00|0x3000|0x1C000)), (unsigned)(0xF00|0x3000|0x1C000)); // others intact
}

TEST(AiPlannerNeeds, PickEightGolden) {
    // All 8 groups eligible. ids slot0..7 = {2,3,4,5,6,7,8,9}.
    // seed=1  -> start6 -> slot6 -> id8
    // seed=7  -> start4 -> slot4 -> id6
    // seed=42 -> start1 -> slot1 -> id3
    struct { u32 seed; u8 id; } golden[] = {{1,8},{7,6},{42,3}};
    u32 allBits = 0xF0|0xF00|0x3000|0x1C000|0xE0000|0x700000|0x1800000|0x1E000000;
    for (auto& g : golden) {
        crt::Srand(g.seed);
        ai::NeedAgent a;
        a.id = 5; a.needWord = allBits;
        u8 id = ai::PickRandomFlagFromEight(a, nullptr);
        CHECK_EQ((int)id, (int)g.id);
    }
}

TEST(AiPlannerNeeds, PickNoneEligible) {
    crt::Srand(1);
    ai::NeedAgent a;
    a.id = 9; a.needWord = 0; // no flags => no eligible slot
    CHECK_EQ((int)ai::PickRandomFlagFromFourA(a, nullptr), 0);
    CHECK_EQ((int)ai::PickRandomFlagFromEight(a, nullptr), 0);
}

// ---------------------------------------------------------------------------
// AiAction leaf predicates / RNG modulo
// ---------------------------------------------------------------------------
TEST(AiPlannerMethod, RandomModuloGolden) {
    crt::Srand(1); // rand0=16838 % 4 == 2
    CHECK_EQ(ai::RandomModulo(4), 2);
    // n==0 path consumes no RNG and returns 0.
    CHECK_EQ(ai::RandomModulo(0), 0);
    CHECK_EQ(ai::RandomModulo(4), (int)(5758 % 4)); // next draw
}

TEST(AiPlannerMethod, CheckSameFaction) {
    // flags & 3 must equal lo%4 and flags & 7 must equal hi%8, then a 1-in-4 roll.
    // Use lo=2 (lo%4=2), hi=2 (hi%8=2). flags with low3 bits == 2 -> 0b010 = 2.
    crt::Srand(1); // first RandomModulo(4) -> 16838%4 == 2 (non-zero => roll fails)
    CHECK(!ai::CheckSameFaction(/*flags*/2, /*lo*/2, /*hi*/2));
    // Seed so the roll yields 0 (accept): need RandNext()%4==0. seed=7 -> 19564%4==0.
    crt::Srand(7);
    CHECK(ai::CheckSameFaction(2, 2, 2));
    // Mismatched faction short-circuits before the roll (no RNG consumed).
    crt::Srand(1);
    CHECK(!ai::CheckSameFaction(/*flags*/1, /*lo*/2, /*hi*/2));
    CHECK_EQ(crt::RandNext(), 16838); // RNG untouched by the short-circuit
}

TEST(AiPlannerMethod, PrepareGroupMember) {
    int out = -1;
    CHECK(!ai::PrepareGroupMember(/*cap*/1, /*active*/5, &out)); // cap<2
    CHECK(!ai::PrepareGroupMember(/*cap*/10, /*active*/0, &out)); // no active crimes
    CHECK(ai::PrepareGroupMember(/*cap*/10, /*active*/3, &out));  // half=5 >= 3 -> 3
    CHECK_EQ(out, 3);
    CHECK(ai::PrepareGroupMember(/*cap*/6, /*active*/9, &out));   // half=3 < 9 -> 3
    CHECK_EQ(out, 3);
}

TEST(AiPlannerMethod, AlwaysAllow) {
    CHECK_EQ(ai::AlwaysAllow(), 1);
}

// ---------------------------------------------------------------------------
// Consume+restock pickers (PickRandomNeedAndClearGroup{,B}) — golden vs. the
// binary's two-RandNext draw order, the unk_647728 {scale,cap} table and the
// trunc(cap*0.5) / trunc(mag*scale) ConvertX truncation (gilde.exe 0x58aea8 /
// 0x58b0cc). Goldens computed against the LCG (see header).
// ---------------------------------------------------------------------------
namespace {
struct CapturingHook : ai::NeedsCommandHook {
    u32 lastNeedWord = 0;
    i32 lastDelta = 0;
    int emits = 0, adjusts = 0;
    void EmitNeedDelta(i32, u32 nw) override { lastNeedWord = nw; ++emits; }
    void AdjustStock(i16, i32 d) override { lastDelta = d; ++adjusts; }
};
} // namespace

TEST(AiPlannerNeeds, RestockGroupAGolden) {
    // needWord=0 -> all four low-group slots eligible (bits clear). ids {2,3,4,5}.
    //  seed=1     -> slot/idx -> id4, bound=trunc(4*0.5)=2, mag=0 -> zeromag, ret 0
    //  seed=7     -> id2, mag=6, amount=trunc(6*15)=90
    //  seed=42    -> id3, mag=1, amount=20
    //  seed=1000  -> id4, mag=1, amount=10
    //  seed=12345 -> id2, mag=4, amount=60
    struct { u32 seed; u8 id; int amount; } g[] = {
        {1, 0, 0}, {7, 2, 90}, {42, 3, 20}, {1000, 4, 10}, {12345, 2, 60},
    };
    for (auto& c : g) {
        crt::Srand(c.seed);
        ai::NeedAgent a; a.type = 7; a.id = 77; a.needWord = 0;
        CapturingHook h;
        u8 id = ai::PickRandomNeedAndClearGroup(a, &h);
        CHECK_EQ((int)id, (int)c.id);
        if (c.id) { CHECK_EQ(h.lastDelta, -c.amount); CHECK_EQ(h.adjusts, 1); }
        else      { CHECK_EQ(h.adjusts, 0); }  // bailed before emit
    }
}

TEST(AiPlannerNeeds, RestockGroupBGolden) {
    // needWord=0 -> all four high-group slots eligible. ids {5,6,7,8}.
    //  seed=1     -> id7, mag=2, amount=10,  needWord -> 0x200000
    //  seed=7     -> id5, mag=2, amount=0 (scale 0), needWord -> 0x1000000 (still picks)
    //  seed=42    -> id6, mag=1, amount=8,  needWord -> 0x20000
    //  seed=1000  -> id7, mag=3, amount=15, needWord -> 0x300000
    //  seed=12345 -> id5, mag=0 -> zeromag, ret 0
    struct { u32 seed; u8 id; int amount; u32 nw; bool ok; } g[] = {
        {1, 7, 10, 0x200000u, true},
        {7, 5, 0,  0x1000000u, true},
        {42, 6, 8, 0x20000u, true},
        {1000, 7, 15, 0x300000u, true},
        {12345, 0, 0, 0u, false},
    };
    for (auto& c : g) {
        crt::Srand(c.seed);
        ai::NeedAgent a; a.type = 3; a.id = 88; a.needWord = 0;
        CapturingHook h;
        u8 id = ai::PickRandomNeedAndClearGroupB(a, &h);
        CHECK_EQ((int)id, (int)c.id);
        if (c.ok) {
            CHECK_EQ(h.lastDelta, -c.amount);
            CHECK_EQ((unsigned)h.lastNeedWord, (unsigned)c.nw);
            CHECK_EQ(h.adjusts, 1);
        } else {
            CHECK_EQ(h.adjusts, 0);
        }
    }
}
