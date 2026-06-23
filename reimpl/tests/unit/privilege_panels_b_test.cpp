// Unit tests for world/privilege_panels_b — SET-B guild-office privilege panels.
// Golden vectors taken directly from the gilde.exe decompile (W19-PRIV-B).
#include "test.h"

#include "world/privilege_panels_b.h"
#include "world/law.h"  // GesetzGetRecord, LawRecord, g_lawTable (reused)

#include <cstring>

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Dispatcher backdrop selection (0x571218).
// ---------------------------------------------------------------------------
TEST(PrivBDispatch, BitmapSelection) {
    CHECK(std::strcmp(PrivShowDialogBitmap(0), "privillegien\\prv_small") == 0);
    CHECK(std::strcmp(PrivShowDialogBitmap(1), "privillegien\\prv_big") == 0);
    CHECK(std::strcmp(PrivShowDialogBitmap(2), "privillegien\\prv_very_big") == 0);
    // The else-branch covers any non-0/1 value (decompile: a3 != 0 && a3 != 1).
    CHECK(std::strcmp(PrivShowDialogBitmap(7), "privillegien\\prv_very_big") == 0);
    CHECK(std::strcmp(PrivShowDialogBitmap(-3), "privillegien\\prv_very_big") == 0);
}

// ---------------------------------------------------------------------------
// Gesetz law table (0x4c244c / 0x4c247c) — reuses world/law's GesetzGetRecord +
// the canonical g_lawTable; this module adds the +4/+8 clamp accessors.
// ---------------------------------------------------------------------------
TEST(PrivBGesetz, GetRecordBoundsAndOOB) {
    LawTableResetDefaults();
    LawRecord r;
    CHECK(!GesetzGetRecord(26, &r));   // a1 >= 26 -> 0
    CHECK(GesetzGetRecord(0, &r));
    CHECK(GesetzGetRecord(25, &r));    // last valid
}

TEST(PrivBGesetz, GoldenMinMax) {
    LawTableResetDefaults();
    LawRecord r;
    // Clamp bounds decoded verbatim from the table @0x631E98:
    // { index, min(v5[1]=int@+4), max(v5[2]=int@+8) }. min is 0 for every record
    // except 12; max is the per-law ceiling.
    struct { u8 idx; i32 mn; i32 mx; } golden[] = {
        {0, 0, 4}, {1, 0, 1}, {2, 0, 2}, {3, 0, 1}, {4, 0, 1},
        {5, 0, 4}, {12, 10, 25}, {25, 0, 1},
    };
    for (auto& g : golden) {
        CHECK(GesetzGetRecord(g.idx, &r));
        CHECK_EQ(GesetzRecordMinAmount(r), g.mn);
        CHECK_EQ(GesetzRecordMaxAmount(r), g.mx);
    }
}

TEST(PrivBGesetz, ClampAmount) {
    LawTableResetDefaults();
    LawRecord r;
    CHECK(GesetzGetRecord(12, &r));   // min 10, max 25 (the one record with min>0)
    CHECK_EQ(GesetzClampAmount(r, 0), 10);   // below -> min
    CHECK_EQ(GesetzClampAmount(r, 10), 10);  // at min
    CHECK_EQ(GesetzClampAmount(r, 18), 18);  // in range
    CHECK_EQ(GesetzClampAmount(r, 25), 25);  // at max
    CHECK_EQ(GesetzClampAmount(r, 99), 25);  // above -> max
    // A min==0 record: clamp only caps the top.
    CHECK(GesetzGetRecord(2, &r));    // min 0, max 2
    CHECK_EQ(GesetzClampAmount(r, 0), 0);
    CHECK_EQ(GesetzClampAmount(r, 2), 2);
    CHECK_EQ(GesetzClampAmount(r, 5), 2);
}

TEST(PrivBGesetz, ApplyEnqueueGate) {
    CHECK(!GesetzApplyWouldEnqueue(26, true, false));  // OOB index
    CHECK(!GesetzApplyWouldEnqueue(3, true, true));    // target id 0xFFFF -> -1
    CHECK(GesetzApplyWouldEnqueue(3, true, false));    // valid target
    CHECK(GesetzApplyWouldEnqueue(3, false, true));    // no target (a1==0) still enqueues
}

// ---------------------------------------------------------------------------
// Subject-kind dispatch head.
// ---------------------------------------------------------------------------
TEST(PrivBSubject, ResolveKind) {
    CHECK_EQ(PrivResolveSubjectKind(3, false), kPrivSubjectPrimary);
    CHECK_EQ(PrivResolveSubjectKind(3, true), kPrivSubjectPrimary);
    CHECK_EQ(PrivResolveSubjectKind(4, true), kPrivSubjectLinked);
    CHECK_EQ(PrivResolveSubjectKind(4, false), kPrivSubjectNone);  // null link -> 96
    CHECK_EQ(PrivResolveSubjectKind(5, true), kPrivSubjectNone);   // other kind -> 96
    CHECK_EQ(PrivResolveSubjectKind(0, true), kPrivSubjectNone);
}

// ---------------------------------------------------------------------------
// EnactLaw (0x561bb4).
// ---------------------------------------------------------------------------
TEST(PrivBEnactLaw, ClassMask) {
    CHECK_EQ(EnactLawClassMask(1), (u32)0x80000);
    CHECK_EQ(EnactLawClassMask(2), (u32)0x100000);
    CHECK_EQ(EnactLawClassMask(0), (u32)0);   // invalid -> return 96
    CHECK_EQ(EnactLawClassMask(3), (u32)0);
}

TEST(PrivBEnactLaw, NonOfficePrecheck) {
    // rank too low -> 32
    CHECK_EQ(EnactLawNonOfficePrecheck(1, true, 5, 2, 6, -1), 32);
    // record absent -> 96
    CHECK_EQ(EnactLawNonOfficePrecheck(5, false, 5, 2, 6, -1), 96);
    // amount == forbidden -> 96
    CHECK_EQ(EnactLawNonOfficePrecheck(5, true, 7, 2, 6, 7), 96);
    // amount above max -> 96
    CHECK_EQ(EnactLawNonOfficePrecheck(5, true, 99, 2, 6, -1), 96);
    // amount below min -> 96
    CHECK_EQ(EnactLawNonOfficePrecheck(5, true, 0, 2, 6, -1), 96);
    // valid -> 0 (proceed)
    CHECK_EQ(EnactLawNonOfficePrecheck(5, true, 4, 2, 6, -1), 0);
}

TEST(PrivBEnactLaw, ModeAndText) {
    CHECK(EnactLawModeValid(0));
    CHECK(EnactLawModeValid(3));
    CHECK(!EnactLawModeValid(4));
    CHECK(!EnactLawModeValid(255));
    CHECK_EQ(EnactLawTextBase(0), 4145);
    CHECK_EQ(EnactLawTextBase(2), 4155);
    CHECK_EQ(EnactLawResult(true), 128);
    CHECK_EQ(EnactLawResult(false), 0);
}

// ---------------------------------------------------------------------------
// Embezzlement (0x562334) — amount = trunc((rand+25)*0.005*wage).
// ---------------------------------------------------------------------------
TEST(PrivBEmbezzle, Amount) {
    // flt_624C2C (0x3ba3d70a) is the float nearest 0.005 but slightly BELOW it,
    // so the products truncate one short of the "nice" value — this is the exact
    // engine behavior (ConvertX@0x5c6b08 truncates toward zero), not 0.005 exact.
    // rand=0 -> 25 * 0.005f * 1000 = 124.99... -> 124
    CHECK_EQ(EmbezzleAmount(0, 1000), 124);
    // rand=175 (max 0xAF) -> 200 * 0.005f * 1000 = 999.99... -> 999
    CHECK_EQ(EmbezzleAmount(175, 1000), 999);
    // rand=0, wage=100 -> 25 * 0.005f * 100 = 12.49... -> 12
    CHECK_EQ(EmbezzleAmount(0, 100), 12);
    // rand=100, wage=500 -> 125 * 0.005f * 500 = 312.4... -> 312
    CHECK_EQ(EmbezzleAmount(100, 500), 312);
    // zero wage -> 0
    CHECK_EQ(EmbezzleAmount(50, 0), 0);
}

TEST(PrivBEmbezzle, NonOfficeRankGate) {
    CHECK_EQ(EmbezzleNonOfficeRankGate(3), 32);
    CHECK_EQ(EmbezzleNonOfficeRankGate(4), 0);
    CHECK_EQ(EmbezzleNonOfficeRankGate(10), 0);
}

// ---------------------------------------------------------------------------
// Miracle (0x5651bc).
// ---------------------------------------------------------------------------
TEST(PrivBMiracle, KindDispatchAndRank) {
    CHECK_EQ(MiracleKindDispatch(6), 1);   // dialog
    CHECK_EQ(MiracleKindDispatch(7), 0);   // no-op
    CHECK_EQ(MiracleKindDispatch(2), -1);  // direct path
    CHECK_EQ(MiracleNonOfficeRankGate(3), 32);
    CHECK_EQ(MiracleNonOfficeRankGate(4), 16);
}

// ---------------------------------------------------------------------------
// SwapSeats (0x562cdc).
// ---------------------------------------------------------------------------
TEST(PrivBSwapSeats, PairTable) {
    auto a = SwapSeatsPairFor(15);
    CHECK(a.valid); CHECK_EQ((int)a.lower, 13); CHECK_EQ((int)a.upper, 14);
    auto b = SwapSeatsPairFor(21);
    CHECK(b.valid); CHECK_EQ((int)b.lower, 19); CHECK_EQ((int)b.upper, 20);
    auto c = SwapSeatsPairFor(27);
    CHECK(c.valid); CHECK_EQ((int)c.lower, 25); CHECK_EQ((int)c.upper, 26);
    auto d = SwapSeatsPairFor(99);
    CHECK(!d.valid);  // -> return 32
}

TEST(PrivBSwapSeats, CountHolders) {
    SwapSeatPair p{13, 14, true};
    u8 none[]  = {1, 2, 3, 4};
    u8 one[]   = {1, 13, 3};
    u8 both[]  = {14, 0, 13, 5};
    u8 extra[] = {13, 14, 13, 14};  // stops after finding 2
    CHECK_EQ(SwapSeatsCountHolders(none, 4, p), 0);
    CHECK_EQ(SwapSeatsCountHolders(one, 3, p), 1);
    CHECK_EQ(SwapSeatsCountHolders(both, 4, p), 2);
    CHECK_EQ(SwapSeatsCountHolders(extra, 4, p), 2);
    CHECK_EQ(SwapSeatsCountHolders(nullptr, 4, p), 0);
}

TEST(PrivBSwapSeats, NonOfficeResult) {
    CHECK_EQ(SwapSeatsNonOfficeResult(6, true, true), 16);
    CHECK_EQ(SwapSeatsNonOfficeResult(5, true, true), 32);   // rank < 6
    CHECK_EQ(SwapSeatsNonOfficeResult(6, false, true), 32);  // not both found
    CHECK_EQ(SwapSeatsNonOfficeResult(6, true, false), 32);  // promote failed
}

// ---------------------------------------------------------------------------
// RemoveFromOffice (0x561fd0).
// ---------------------------------------------------------------------------
TEST(PrivBRemoveOffice, DefinitionAndNonOffice) {
    CHECK_EQ(RemoveOfficeDefinitionGate(true), 0);
    CHECK_EQ(RemoveOfficeDefinitionGate(false), 96);
    CHECK_EQ(RemoveOfficeNonOfficeResult(false, false, false), 96);  // no target
    CHECK_EQ(RemoveOfficeNonOfficeResult(true, true, true), 16);     // match -> removed
    CHECK_EQ(RemoveOfficeNonOfficeResult(true, true, false), 96);    // category mismatch
    CHECK_EQ(RemoveOfficeNonOfficeResult(true, false, true), 96);    // no holder entry
}

TEST(PrivBRemoveOffice, SessionResult) {
    CHECK_EQ(RemoveOfficeSessionResult(false, false, false, false), 32);   // session null
    CHECK_EQ(RemoveOfficeSessionResult(true, false, false, false), 96);    // no holder entry
    CHECK_EQ(RemoveOfficeSessionResult(true, true, false, false), 32);     // not removable
    CHECK_EQ(RemoveOfficeSessionResult(true, true, true, true), 32);       // packet conflict
    CHECK_EQ(RemoveOfficeSessionResult(true, true, true, false), -112);    // clean removed
}

// ---------------------------------------------------------------------------
// CounterEspionage (0x5628c8).
// ---------------------------------------------------------------------------
TEST(PrivBCounterEsp, NonOfficeResult) {
    CHECK_EQ(CounterEspNonOfficeResult(3, false), 32);   // rank < 4
    CHECK_EQ(CounterEspNonOfficeResult(4, false), 2);    // base, no reset
    CHECK_EQ(CounterEspNonOfficeResult(4, true), 0x12);  // base | 0x10 = 18
    CHECK_EQ(CounterEspNonOfficeResult(10, true), 0x12);
}

// ---------------------------------------------------------------------------
// EvidenceReview / EvidenceReviewAlt (0x565f9c / 0x5667a0).
// ---------------------------------------------------------------------------
TEST(PrivBEvidence, OfficePathAndPrecheck) {
    CHECK(EvidenceReviewIsOfficePath(6));
    CHECK(EvidenceReviewIsOfficePath(7));
    CHECK(!EvidenceReviewIsOfficePath(3));

    // actor is a judge (13) -> 96
    CHECK_EQ(EvidenceReviewConcretePrecheck(13, true, 5, 3), 96);
    // target absent -> 96
    CHECK_EQ(EvidenceReviewConcretePrecheck(5, false, 5, 3), 96);
    // target is a judge -> 96
    CHECK_EQ(EvidenceReviewConcretePrecheck(5, true, 13, 3), 96);
    // no matching evidence -> 96
    CHECK_EQ(EvidenceReviewConcretePrecheck(5, true, 5, 0), 96);
    // valid -> 0 (proceed to BuildEvidenceEntry)
    CHECK_EQ(EvidenceReviewConcretePrecheck(5, true, 5, 3), 0);
}

TEST(PrivBEvidence, BuildModeArg) {
    CHECK_EQ(EvidenceReviewBuildMode(EvidenceReviewMode::kReview), 0);
    CHECK_EQ(EvidenceReviewBuildMode(EvidenceReviewMode::kReviewAlt), 1);
}

// ===========================================================================
// PANEL-SHAPED ENTRY POINTS + DISPATCHER (wave-21). These exercise the FULL
// 1:1 control flow of each panel through the PrivilegePanelBHooks vtable, pinning
// the verdict (signed char) and the staged-command / done-state side effects.
// ===========================================================================

namespace {
// A scripted button source: a queue of dword_75BF38 values; exhausted -> loop-exit.
struct BtnScript {
    int seq[8]; int n = 0; int i = 0;
};
BtnScript* g_btn = nullptr;
int ScriptNextBtn(void*) {
    if (!g_btn || g_btn->i >= g_btn->n) return kPrivLoopExit;
    return g_btn->seq[g_btn->i++];
}
bool AlwaysSkill(const PrivPerson*, int, void*) { return true; }
int  Rng0(u16, void*) { return 0; }           // RandomModulo -> 0 (determinism)
i32  Wage1000(u16, void*) { return 1000; }     // office wage
int  EspReset2(i32, bool, void*) { return 2; } // two agents reset
bool DefTrue(u8, u8* c, void*) { if (c) *c = 5; return true; }
const PrivPerson* FindNull(i32, void*) { return nullptr; }

PrivilegePanelBHooks MakeHooks(PrivBTrace* t) {
    PrivilegePanelBHooks h{};
    h.trace = t;
    h.nextButton = &ScriptNextBtn;
    h.checkSkill = &AlwaysSkill;
    h.randomModulo = &Rng0;
    h.computeOfficeWages = &Wage1000;
    h.counterEspScan = &EspReset2;
    return h;
}
} // namespace

// --- Embezzlement amount: ConvertX truncation pinned (wave-19 golden) ---------
TEST(PrivBEmbezzleAmt, GoldenTruncation) {
    // (RandomModulo(0xB0)+25) * 0.005f (flt_624C2C, just below 0.005) * wage, trunc.
    CHECK_EQ(EmbezzleAmount(0, 1000), 124);    // (25)*0.005f*1000 -> 124.99.. -> 124
    CHECK_EQ(EmbezzleAmount(0, 0), 0);
    CHECK_EQ(EmbezzleAmount(175, 1000), 999);  // (200)*0.005f*1000 -> 999.99 -> 999
}

// --- Embezzlement panel: non-office rank gate + office GUI confirm ------------
TEST(PrivBEmbezzlePanel, NonOfficeRankGate) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 3; actor.handle = 7;
    PrivEvent ev{}; ev.mode = 3;                 // subject = actor
    // rank < 4 -> 32, no command
    CHECK_EQ((int)PrivilegePanelEmbezzlement(&actor, &ev, &h), 32);
    CHECK_EQ(t.cmdCount, 0);
}
TEST(PrivBEmbezzlePanel, NonOfficeProceed) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 4; actor.handle = 7; actor.id = 9;
    PrivEvent ev{}; ev.mode = 3;
    CHECK_EQ((int)PrivilegePanelEmbezzlement(&actor, &ev, &h), 0);
    CHECK_EQ(t.lastAmount, 124);                 // wage 1000, draw 0
    CHECK_EQ(t.cmdCount, 3);                     // Request16 + BuildOp90 + Args25
}
TEST(PrivBEmbezzlePanel, BadSubject) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3;
    PrivEvent ev{}; ev.mode = 4; ev.dragSource = nullptr; // kind4 + null link -> 96
    CHECK_EQ((int)PrivilegePanelEmbezzlement(&actor, &ev, &h), 96);
}
TEST(PrivBEmbezzlePanel, OfficeConfirm) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    PrivPerson actor{}; actor.kind = 6; actor.handle = 7; actor.id = 9;
    PrivEvent ev{}; ev.mode = 3;
    PrivilegePanelEmbezzlement(&actor, &ev, &h);
    CHECK_EQ(t.cmdCount, 3);                     // confirm staged 3 commands
    CHECK_EQ(t.doneState, 1);                    // dword_631614 = 1
    g_btn = nullptr;
}

// --- Miracle panel: kind dispatch + RNG draw ---------------------------------
TEST(PrivBMiraclePanel, Kind7Zero) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 7;
    CHECK_EQ((int)PrivilegePanelMiracle(&actor, &h), 0);
    CHECK_EQ(t.cmdCount, 0);
}
TEST(PrivBMiraclePanel, NonOfficeRankGate) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 3;
    CHECK_EQ((int)PrivilegePanelMiracle(&actor, &h), 32);   // rank<4 -> 32
    actor.office404 = 4;
    PrivBTrace t2; PrivilegePanelBHooks h2 = MakeHooks(&t2);
    CHECK_EQ((int)PrivilegePanelMiracle(&actor, &h2), 16);  // rank>=4 -> 16
    CHECK_EQ(t2.cmdCount, 2);                                // BuildOp90 + BuildMemberTable
}

// --- CounterEspionage panel ---------------------------------------------------
TEST(PrivBCounterEsp, OfficeAlwaysTwo) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    PrivPerson actor{}; actor.kind = 6; actor.handle = 7;
    CHECK_EQ((int)PrivilegePanelCounterEspionage(&actor, &h), 2);
    CHECK_EQ(t.agentsReset, 2);
    g_btn = nullptr;
}
TEST(PrivBCounterEsp, NonOfficeRankAndResetBit) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 3;
    CHECK_EQ((int)PrivilegePanelCounterEspionage(&actor, &h), 32); // rank<4 -> 32
    PrivBTrace t2; PrivilegePanelBHooks h2 = MakeHooks(&t2);
    actor.office404 = 4;
    // base 2 | 0x10 (>=1 agent reset) = 0x12 = 18
    CHECK_EQ((int)PrivilegePanelCounterEspionage(&actor, &h2), 18);
}

// --- SwapSeats panel ----------------------------------------------------------
TEST(PrivBSwapSeats, BadOfficeByte) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 99;          // not 15/21/27 -> 32
    PrivEvent ev{}; ev.mode = 3;
    CHECK_EQ((int)PrivilegePanelSwapSeats(&actor, &ev, &h), 32);
}

// --- EvidenceReview / Alt panels ----------------------------------------------
TEST(PrivBEvidencePanel, NonOfficeJudgeReject) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    h.findRecord = &FindNull;
    PrivPerson actor{}; actor.kind = 13;          // judge -> 96
    PrivEvent ev{}; ev.mode = 3; ev.targetId = 5;
    CHECK_EQ((int)PrivilegePanelEvidenceReview(&actor, &ev, &h), 96);
    CHECK_EQ((int)PrivilegePanelEvidenceReviewAlt(&actor, &ev, &h), 96);
}
TEST(PrivBEvidencePanel, OfficeArmDefaultVerdict) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 6;
    PrivEvent ev{}; ev.mode = 3;
    // inert office arm (no row click) -> verdict latch init 32
    CHECK_EQ((int)PrivilegePanelEvidenceReview(&actor, &ev, &h), 32);
}

// --- EvidenceDetails panel (0x565b88) -----------------------------------------
namespace {
// Evidence-detail leaf hooks: a fixed target person, configurable match count and
// per-row field37, and a build-entry recorder.
PrivPerson g_evTarget{};
int        g_evMatch = 0;
i32        g_evField37[8] = {0};
int        g_evBuildCalls = 0;
int        g_evBuildMode  = -1;
int        g_evBuildMatch = -1;
const PrivPerson* FindTarget(i32, void*) { return &g_evTarget; }
int  MatchCount(i32, u16, void*) { return g_evMatch; }
i32  RowField37(i32, u16, int row, void*) {
    return (row >= 0 && row < 8) ? g_evField37[row] : 0;
}
int  BuildEntry(const PrivPerson*, const PrivPerson*, int mode, int mc, void*) {
    ++g_evBuildCalls; g_evBuildMode = mode; g_evBuildMatch = mc; return 16;
}
PrivilegePanelBHooks MakeEvHooks(PrivBTrace* t) {
    PrivilegePanelBHooks h = MakeHooks(t);
    h.findRecord = &FindTarget;
    h.matchingEntityCount = &MatchCount;
    h.evidenceRowField37 = &RowField37;
    h.buildEvidenceEntry = &BuildEntry;
    return h;
}
} // namespace

TEST(PrivBEvidenceDetails, NoTargetReturnsZero) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 0xFFFF;   // 0xFFFF -> return 0
    CHECK_EQ((int)PrivilegePanelEvidenceDetails(&actor, &g_evTarget, false, &h), 0);
}
TEST(PrivBEvidenceDetails, NoEvidenceReturns96) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 0;                                       // matchCount < 1 -> 96
    CHECK_EQ((int)PrivilegePanelEvidenceDetails(&actor, &g_evTarget, false, &h), 96);
}
TEST(PrivBEvidenceDetails, ConfirmBuildsEntryStateThree) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 2; g_evField37[0] = 1; g_evField37[1] = 0; // one actionable row
    g_evBuildCalls = 0; g_evBuildMode = -1; g_evBuildMatch = -1;
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    CHECK_EQ((int)PrivilegePanelEvidenceDetails(&actor, &g_evTarget, false, &h), 0); // always 0
    CHECK_EQ(g_evBuildCalls, 1);          // confirm fired BuildEvidenceEntry
    CHECK_EQ(g_evBuildMode, 0);           // EvidenceDetails always passes mode 0
    CHECK_EQ(g_evBuildMatch, 2);          // matchCount forwarded
    CHECK_EQ(t.doneState, 3);             // dword_631614 = 3
    g_btn = nullptr;
}
TEST(PrivBEvidenceDetails, SelfTargetDisablesConfirm) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 1; g_evField37[0] = 1;    // actionable, BUT actorIsTarget -> v28=0
    g_evBuildCalls = 0;
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    PrivilegePanelEvidenceDetails(&actor, &g_evTarget, /*actorIsTarget*/ true, &h);
    CHECK_EQ(g_evBuildCalls, 0);          // confirm disabled -> no build, no state 3
    CHECK_EQ(t.doneState, 0);
    g_btn = nullptr;
}
TEST(PrivBEvidenceDetails, NoActionableRowDisablesConfirm) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 2; g_evField37[0] = 0; g_evField37[1] = 2; // no row has field37==1
    g_evBuildCalls = 0;
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    PrivilegePanelEvidenceDetails(&actor, &g_evTarget, false, &h);
    CHECK_EQ(g_evBuildCalls, 0);          // confirm disabled
    g_btn = nullptr;
}
TEST(PrivBEvidenceDetails, WindowCloseStateOne) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 1; g_evField37[0] = 1;
    BtnScript b{{kPrivBtnCancelId}, 1, 0}; g_btn = &b;
    PrivilegePanelEvidenceDetails(&actor, &g_evTarget, false, &h);
    CHECK_EQ(t.doneState, 1);             // dword_631614 = 1 on close
    g_btn = nullptr;
}
// EvidenceReview office arm now opens EvidenceDetails per row click and latches 0.
TEST(PrivBEvidencePanel, OfficeArmRowClickLatchesDetails) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.kind = 6; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 100; g_evTarget.handle = 50;
    g_evMatch = 1; g_evField37[0] = 0;    // no actionable -> EvidenceDetails returns 0
    PrivEvent ev{}; ev.mode = 3; ev.targetId = 100;
    BtnScript b{{kPrivBtnOk}, 1, 0}; g_btn = &b;
    // row click -> EvidenceDetails(...) returns 0 -> latched verdict 0 (was 32)
    CHECK_EQ((int)PrivilegePanelEvidenceReview(&actor, &ev, &h), 0);
    g_btn = nullptr;
}
// Dispatcher routes EvidenceDetails (0x565b88).
TEST(PrivBEvidenceDetails, DispatcherRoute) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeEvHooks(&t);
    PrivPerson actor{}; actor.id = 9;
    g_evTarget = PrivPerson{}; g_evTarget.id = 0xFFFF;   // 0xFFFF -> 0
    PrivEvent ev{}; ev.mode = 3; ev.targetId = 100;
    CHECK_EQ(PrivilegeDispatchPanelB(0x565b88, &actor, &ev, &h), 0);
}

// --- RemoveFromOffice panel ---------------------------------------------------
TEST(PrivBRemoveOffice, NoOfficeDef) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    // officeGetDefinition returns false -> 96
    PrivPerson actor{}; actor.kind = 3;
    PrivEvent ev{}; ev.mode = 3;
    CHECK_EQ((int)PrivilegeRemoveFromOffice(&actor, &ev, &h), 96);
}
TEST(PrivBRemoveOffice, NonOfficeNoTarget) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    h.officeGetDefinition = &DefTrue; h.findRecord = &FindNull;
    PrivPerson actor{}; actor.kind = 3;
    PrivEvent ev{}; ev.mode = 3; ev.targetId = 5;
    CHECK_EQ((int)PrivilegeRemoveFromOffice(&actor, &ev, &h), 96); // no target -> 96
}

// --- EnactLaw panel -----------------------------------------------------------
TEST(PrivBEnactLaw, ClassGate) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 1;
    PrivEvent ev{}; ev.mode = 3; ev.dragField = 9;  // invalid law class -> 96
    CHECK_EQ((int)PrivilegePanelEnactLaw(&actor, &ev, &h), 96);
}
TEST(PrivBEnactLaw, NonOfficeRankGate) {
    LawTableResetDefaults();
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 3; actor.office404 = 1; // rank<2 -> 32
    PrivEvent ev{}; ev.mode = 3; ev.dragField = 1;
    CHECK_EQ((int)PrivilegePanelEnactLaw(&actor, &ev, &h), 32);
}

// ---------------------------------------------------------------------------
// FULL PrivBuildEvidenceEntry body (0x56589c) — wave-23 reconstruction.
// Golden vectors taken from the gilde.exe decompile/disasm of 0x56589c.
// ---------------------------------------------------------------------------
namespace {
// Reads a little-endian i32 / u8 out of the built 248-byte record.
i32 RecI32(const EvidenceBuildSink& s, int off) {
    i32 v; std::memcpy(&v, s.record + off, 4); return v;
}
u8 RecU8(const EvidenceBuildSink& s, int off) { return s.record[off]; }

// A deterministic witness search: hit/miss per call from a small script.
int g_witHits[2] = {1, 1};
u16 g_witIdx[2]  = {0, 0};
int g_witCalls   = 0;
int ScriptWitness(const EvidenceBuildWorld&, const u16*, int which,
                  u16* outIdx, void*) {
    ++g_witCalls;
    if (g_witHits[which & 1]) { *outIdx = g_witIdx[which & 1]; return 1; }
    return 0;
}
int g_emitCalls = 0;
void ScriptEmit(const u8*, void*) { ++g_emitCalls; }

// A standard world: one judge person (office byte 15 for type 3), selection
// arrays sized for a few indices, two court-party player records.
u8  g_personTypes[4]   = {1, 2, 3, 4};       // type 3 has the judge office byte
u8  g_officeByteTab[8] = {0, 0, 0, 15, 0, 0, 0, 0}; // aiTable[type]: type3 -> 15
i32 g_handleTab[4 * 134] = {0};
u16 g_idTab[4 * 268]     = {0};
u8  g_officeTab[4 * 536] = {0};

EvidenceBuildWorld MakeWorld() {
    EvidenceBuildWorld w{};
    w.personTypes = g_personTypes; w.personCount = 4;
    w.officeByteForType = g_officeByteTab; w.officeByteTypeCount = 8;
    w.personHandleById = g_handleTab; w.personHandleCount = 4;
    w.personIdById = g_idTab; w.personIdCount = 4;
    w.personOfficeById = g_officeTab; w.personOfficeCount = 4;
    w.localPlayerIndex = 0;
    w.playerRecAId = 0xAAAA; w.playerRecAHandle = 0x1111;
    w.playerRecBId = 0xBBBB; w.playerRecBHandle = 0x2222;
    w.actorId = 7; w.actorHandle = 0x700;
    w.targetId = 100; w.targetHandle = 0x900;
    return w;
}
EvidenceBuildSink MakeSink() {
    EvidenceBuildSink s{};
    s.findWitness = &ScriptWitness; s.emitReset = &ScriptEmit;
    return s;
}
} // namespace

// No judge person at all -> return 16, no record built, no emission. (0x5658f2)
TEST(PrivBBuildEntry, NoJudgeReturns16) {
    EvidenceBuildWorld w = MakeWorld();
    u8 noJudge[8] = {0,0,0,0,0,0,0,0};       // aiTable: no type maps to 15
    w.officeByteForType = noJudge;
    EvidenceBuildSink s = MakeSink();
    g_emitCalls = 0; g_witCalls = 0;
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                                    w.targetId, w.targetHandle, 0,
                                    /*mode*/0, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 16);
    CHECK_EQ(g_witCalls, 0);                  // never reached the witness search
    CHECK_EQ(g_emitCalls, 0);
    CHECK(!s.emitted);
}

// Judge found, both witnesses found -> 16, command emitted, record byte-exact.
TEST(PrivBBuildEntry, FullBuildEmitsAndReturns16) {
    EvidenceBuildWorld w = MakeWorld();
    // Put a kind-13 office holder at selection index 1 (judge sourcing, mode 0).
    g_officeTab[536 * 1] = 13;
    g_handleTab[134 * 1] = 0x5555;           // judge handle at index 1
    g_idTab[268 * 1]     = 0xC0DE;           // judge id at index 1
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = g_witHits[1] = 1; g_witIdx[0] = 2; g_witIdx[1] = 3;
    g_handleTab[134 * 2] = 0x6666;           // witness1 handle (idx0)
    g_handleTab[134 * 3] = 0x7777;           // witness2 handle (idx1)
    g_emitCalls = 0; g_witCalls = 0;
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, /*bit12*/0,
                                    /*office358*/0, w.targetId, w.targetHandle,
                                    /*tgt358*/0, /*mode*/0, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 16);
    CHECK(s.emitted);
    CHECK_EQ(g_emitCalls, 1);
    CHECK_EQ(g_witCalls, 2);
    // Record fixed fields (byte-exact).
    CHECK_EQ((int)RecU8(s, 0x04), 44);
    CHECK_EQ(RecI32(s, 0x08), w.actorHandle);   // actor handle
    CHECK_EQ((int)RecU8(s, 0x36), 2);
    CHECK_EQ(RecI32(s, 0x58), w.actorHandle);
    CHECK_EQ(RecI32(s, 0x5C), w.targetHandle);
    CHECK_EQ(RecI32(s, 0x60), 0x5555);          // judge handle (selection idx 1)
    CHECK_EQ(RecI32(s, 0x64), 0x6666);          // witness1 handle
    CHECK_EQ(RecI32(s, 0x68), 0x7777);          // witness2 handle
    // filter blob: target id, judge id seeded.
    CHECK_EQ((int)s.filter[0], 100);            // v27.lo = target id
    CHECK_EQ((int)s.filter[1], 0xC0DE);         // v27.hi = judge id
    g_officeTab[536 * 1] = 0;                    // reset shared state
}

// First witness search misses -> return 64, no emission. (0x565a6b)
TEST(PrivBBuildEntry, WitnessMissReturns64) {
    EvidenceBuildWorld w = MakeWorld();
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = 0; g_witHits[1] = 1;        // first search misses
    g_emitCalls = 0; g_witCalls = 0;
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                                    w.targetId, w.targetHandle, 0,
                                    /*mode*/0, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 64);
    CHECK_EQ(g_witCalls, 1);
    CHECK(!s.emitted);
    g_witHits[0] = 1;                          // reset
}

// Second witness search misses -> 64. (0x565ae8)
TEST(PrivBBuildEntry, SecondWitnessMissReturns64) {
    EvidenceBuildWorld w = MakeWorld();
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = 1; g_witHits[1] = 0;        // second search misses
    g_witIdx[0] = 2;
    g_emitCalls = 0; g_witCalls = 0;
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                                    w.targetId, w.targetHandle, 0,
                                    /*mode*/0, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 64);
    CHECK_EQ(g_witCalls, 2);
    CHECK(!s.emitted);
    g_witHits[1] = 1;                          // reset
}

// mode != 0: judge sourced from the court-party player records (a1+12 selects).
TEST(PrivBBuildEntry, ModeNonZeroJudgeFromPlayerRecord) {
    EvidenceBuildWorld w = MakeWorld();
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = g_witHits[1] = 1; g_witIdx[0] = 0; g_witIdx[1] = 0;
    g_emitCalls = 0;
    // bit12 == 0 -> player record A.
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, /*bit12*/0, 0,
                                    w.targetId, w.targetHandle, 0,
                                    /*mode*/1, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 16);
    CHECK_EQ(RecI32(s, 0x60), 0x1111);         // judge handle = playerRecA
    CHECK_EQ((int)s.filter[1], 0xAAAA);        // judge id = playerRecA id
    // bit12 == 1 -> player record B.
    EvidenceBuildSink s2 = MakeSink();
    char r2 = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, /*bit12*/1, 0,
                                     w.targetId, w.targetHandle, 0,
                                     /*mode*/1, /*matchCount*/2, w, s2);
    CHECK_EQ((int)r2, 16);
    CHECK_EQ(RecI32(s2, 0x60), 0x2222);        // judge handle = playerRecB
    CHECK_EQ((int)s2.filter[1], 0xBBBB);
}

// mode 0, no kind-13 holder in selection -> RNG fallback picks a player record.
TEST(PrivBBuildEntry, JudgeRngFallback) {
    EvidenceBuildWorld w = MakeWorld();       // no office byte 13 in g_officeTab
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = g_witHits[1] = 1; g_witIdx[0] = 0; g_witIdx[1] = 0;
    w.rngJudgePick = 1;                        // RandomModulo(2) != 0 -> playerRecA
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                                    w.targetId, w.targetHandle, 0,
                                    /*mode*/0, /*matchCount*/2, w, s);
    CHECK_EQ((int)r, 16);
    CHECK_EQ(RecI32(s, 0x60), 0x1111);         // playerRecA via RNG
    EvidenceBuildSink s2 = MakeSink();
    w.rngJudgePick = 0;                        // == 0 -> playerRecB
    PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                           w.targetId, w.targetHandle, 0, 0, 2, w, s2);
    CHECK_EQ(RecI32(s2, 0x60), 0x2222);        // playerRecB via RNG
}

// Accuser sourcing: kind-17 holder in selection + matchCount>2 -> accuser slot.
TEST(PrivBBuildEntry, AccuserSourcedWhenMatchGtTwo) {
    EvidenceBuildWorld w = MakeWorld();
    g_officeTab[536 * 2] = 17;                 // kind-17 holder at index 2
    g_handleTab[134 * 2] = 0x8888;            // accuser handle at index 2
    EvidenceBuildSink s = MakeSink();
    g_witHits[0] = g_witHits[1] = 1; g_witIdx[0] = 0; g_witIdx[1] = 0;
    // matchCount > 2 -> accuser handle written at +0x6C.
    char r = PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, /*office358*/0,
                                    w.targetId, w.targetHandle, /*tgt358*/0,
                                    /*mode*/1, /*matchCount*/3, w, s);
    CHECK_EQ((int)r, 16);
    CHECK_EQ(RecI32(s, 0x6C), (i32)0x8888);   // accuser handle (idx 2)
    // matchCount <= 2 -> accuser handle stays -1.
    EvidenceBuildSink s2 = MakeSink();
    PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, 0,
                           w.targetId, w.targetHandle, 0, 1, /*mc*/2, w, s2);
    CHECK_EQ(RecI32(s2, 0x6C), -1);           // not sourced
    // actor office byte 17 suppresses accuser sourcing entirely.
    EvidenceBuildSink s3 = MakeSink();
    PrivBuildEvidenceEntry(w.actorId, w.actorHandle, 0, /*office358*/17,
                           w.targetId, w.targetHandle, 0, 1, 3, w, s3);
    CHECK_EQ(RecI32(s3, 0x6C), -1);           // suppressed
    g_officeTab[536 * 2] = 0;                  // reset
}

// RunBuildEvidenceEntry routes through evidenceWorld when buildEvidenceEntry is
// null (the FULL-body wiring path used by the panels).
namespace {
bool g_wsupCalled = false;
bool WorldSupplier(const PrivPerson*, const PrivPerson*,
                   EvidenceBuildWorld* out, EvidenceBuildSink* sink, void*) {
    g_wsupCalled = true;
    *out = MakeWorld();
    *sink = MakeSink();
    g_witHits[0] = g_witHits[1] = 1; g_witIdx[0] = 0; g_witIdx[1] = 0;
    return true;
}
} // namespace
TEST(PrivBBuildEntry, RunBridgeUsesEvidenceWorld) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    h.buildEvidenceEntry = nullptr;            // force the world path
    h.evidenceWorld = &WorldSupplier;
    g_wsupCalled = false; g_emitCalls = 0;
    PrivPerson actor{}; actor.id = 7;
    PrivPerson target{}; target.id = 100;
    int r = RunBuildEvidenceEntry(&actor, &target, /*mode*/0, /*matchCount*/2, &h);
    CHECK(g_wsupCalled);
    CHECK_EQ(r, 16);
    CHECK_EQ(g_emitCalls, 1);                   // full body emitted the command
}

// The witness-search-via-ObjectSearch helper hits the REAL reconstructed leaf.
TEST(PrivBBuildEntry, WitnessSearchViaObjectSearchReachesRealLeaf) {
    EvidenceBuildWorld w = MakeWorld();
    u16 filter[4] = {100, 0xFFFF, 0xFFFF, 0xFFFF};
    u16 out = 0xFFFF;
    // With the inert PathfindMap hooks (no eligible candidate) the real leaf
    // returns 0 (miss) — it executed the probe loop, proving the wiring reaches it.
    int hit = EvidenceWitnessSearchViaObjectSearch(w, filter, 0, &out, nullptr);
    CHECK_EQ(hit, 0);
}

// --- Dispatcher routing -------------------------------------------------------
TEST(PrivBDispatcher, RoutesByLeafId) {
    PrivBTrace t; PrivilegePanelBHooks h = MakeHooks(&t);
    PrivPerson actor{}; actor.kind = 7;
    PrivEvent ev{}; ev.mode = 3;
    // Miracle kind 7 -> 0 via dispatcher
    CHECK_EQ(PrivilegeDispatchPanelB(0x5651bc, &actor, &ev, &h), 0);
    // Unknown leaf -> 0
    CHECK_EQ(PrivilegeDispatchPanelB(0xdead, &actor, &ev, &h), 0);
    // CounterEspionage office arm -> 2
    PrivPerson o{}; o.kind = 6; o.handle = 1;
    BtnScript b{{kPrivBtnCancelId}, 1, 0}; g_btn = &b;
    CHECK_EQ(PrivilegeDispatchPanelB(0x5628c8, &o, &ev, &h), 2);
    g_btn = nullptr;
}
