// privilege_panels_a_test.cpp — golden-pins for the SET-A guild-office privilege
// panels (world/privilege_panels_a.{h,cpp}), 1:1 from gilde.exe.
#include "tests/framework/test.h"
#include "world/privilege_panels_a.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {

// A scriptable hook context: a button queue + a record table + an RNG sequence.
struct Ctx {
    PrivilegePanelTrace trace;
    std::vector<int> buttons;          // nextButton() drains this; then loop-exit
    size_t btnIdx = 0;
    std::vector<int> rng;              // randomModulo() drains this (clamped to n)
    size_t rngIdx = 0;
    std::vector<int> rngMods;          // recorded moduli for assertions
    std::vector<PrivPerson> people;    // findRecord/pickOfficeHolder table
    size_t pickIdx = 0;                // pickOfficeHolder() walks people in order
    i32 wealth = 0;
    i32 currency = 0;
    int matchCount = 0;
    bool skillOk = true;
    bool resourceOk = true;
    bool confirmOk = true;
    int relationVal = 0;
    std::vector<int> entityMsgs;       // entity-message handles fired
};

const PrivPerson* FindRecord(i32 id, void* c) {
    Ctx* x = (Ctx*)c;
    for (auto& p : x->people) if ((i32)p.id == id) return &p;
    return nullptr;
}
i32 Wealth(const PrivPerson*, void* c) { return ((Ctx*)c)->wealth; }
i32 Currency(const PrivPerson*, void* c) { return ((Ctx*)c)->currency; }
int Rng(u16 n, void* c) {
    Ctx* x = (Ctx*)c;
    x->rngMods.push_back(n);
    int v = (x->rngIdx < x->rng.size()) ? x->rng[x->rngIdx++] : 0;
    return v % (n ? n : 1);
}
int Relation(u16, u16, void* c) { return ((Ctx*)c)->relationVal; }
bool Skill(const PrivPerson*, int, void* c) { return ((Ctx*)c)->skillOk; }
bool Resource(int, u8, void* c) { return ((Ctx*)c)->resourceOk; }
int Matching(i32, u16, void* c) { return ((Ctx*)c)->matchCount; }
const PrivPerson* Pick(int, void* c) {
    Ctx* x = (Ctx*)c;
    if (x->pickIdx < x->people.size()) return &x->people[x->pickIdx++];
    return nullptr;
}
int NextBtn(void* c) {
    Ctx* x = (Ctx*)c;
    if (x->btnIdx < x->buttons.size()) return x->buttons[x->btnIdx++];
    return kPrivLoopExit;
}
void SendMsg(i32 handle, int, void* c) { ((Ctx*)c)->entityMsgs.push_back(handle); }
bool Confirm(int, void* c) { return ((Ctx*)c)->confirmOk; }

PrivilegePanelHooks MakeHooks(Ctx& x) {
    PrivilegePanelHooks h{};
    h.trace = &x.trace;
    h.findRecord = FindRecord;
    h.computeTotalWealth = Wealth;
    h.sumCurrencyHeld = Currency;
    h.randomModulo = Rng;
    h.relationEntry = Relation;
    h.checkSkill = Skill;
    h.checkResource = Resource;
    h.findMatchingEntityIds = Matching;
    h.pickOfficeHolder = Pick;
    h.nextButton = NextBtn;
    h.sendEntityMessage = SendMsg;
    h.confirmBox = Confirm;
    h.ctx = &x;
    return h;
}

PrivPerson MkPerson(u16 id, i32 handle, u8 kind) {
    PrivPerson p{}; p.id = id; p.handle = handle; p.kind = kind; return p;
}

} // namespace

// --------------------------------------------------------------------------
// Cost math (wealth * factor, ConvertX-truncated). Golden values.
// --------------------------------------------------------------------------
TEST(PrivA, CostTruncation) {
    // The factors are the .rdata float32 literals (0.0199999.../0.0599999...), so
    // the products fall JUST under the integer and ConvertX truncates DOWN — this
    // is the exact 1:1 binary behavior (Rule 1 integer/float edge case):
    // 2% of 1050 = 20.9999995 -> 20 (NOT 21)
    CHECK_EQ(PrivCostFromWealth((i32)1050, kPrivMedicusWealthFactor), 20);
    // 6% of 1050 = 62.9999986 -> 62 (NOT 63)
    CHECK_EQ(PrivCostFromWealth((i32)1050, kPrivConvertWealthFactor), 62);
    // 2% of 999 = 19.98 -> truncates to 19
    CHECK_EQ(PrivCostFromWealth((i32)999, kPrivHatredWealthFactor), 19);
    // 8% of 1000 = 80.0 -> 80 (double factor)
    CHECK_EQ(PrivCostFromWealth((i32)1000, kPrivDivorceWealthFactorA), 80);
    // 4% of 1000 = 40
    CHECK_EQ(PrivCostFromWealth((i32)1000, kPrivDivorceWealthFactorB), 40);
}

TEST(PrivA, CostConstantsDecoded) {
    CHECK(kPrivMedicusWealthFactor > 0.0199f && kPrivMedicusWealthFactor < 0.0201f);
    CHECK(kPrivConvertWealthFactor > 0.0599f && kPrivConvertWealthFactor < 0.0601f);
    CHECK(kPrivHatredWealthFactor > 0.0199f && kPrivHatredWealthFactor < 0.0201f);
    CHECK_EQ(kPrivDivorceWealthFactorA, 0.08);
    CHECK_EQ(kPrivDivorceWealthFactorB, 0.04);
    CHECK_EQ(kPrivDivorceAffordRatio, 0.12);
    CHECK_EQ(kPersonStride, 536);
    CHECK_EQ(kPersonArrayCount, 768);
    CHECK_EQ(kPersonArrayByteSpan, 411648);
}

// --------------------------------------------------------------------------
// GenerateHatred passive path: gates + RNG order + relation delta + commands.
// --------------------------------------------------------------------------
TEST(PrivA, HatredPassiveOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 3; // < 4 -> 32
    PrivEvent ev{}; ev.targetId = 2; ev.partnerId = 3;
    x.people = { MkPerson(2, 200, 0), MkPerson(3, 300, 0) };
    CHECK_EQ((int)PrivilegePanelGenerateHatred(&actor, &ev, &h), 32);
}

TEST(PrivA, HatredPassiveSuccess) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 4;
    PrivEvent ev{}; ev.targetId = 2; ev.partnerId = 3;
    x.people = { MkPerson(2, 200, 0), MkPerson(3, 300, 0) };
    x.wealth = 1000; x.currency = 100000;          // 2% of 2000 = 40
    x.rng = {5, 7};                                // two draws RandomModulo(0x1E)
    x.relationVal = 0;
    CHECK_EQ((int)PrivilegePanelGenerateHatred(&actor, &ev, &h), 16);
    // RNG draw order: two RandomModulo(30) draws.
    CHECK_EQ((int)x.rngMods.size(), 2u);
    CHECK_EQ(x.rngMods[0], 0x1E);
    CHECK_EQ(x.rngMods[1], 0x1E);
    // Cost = 2% of (1000+1000) = 39.9999991 -> 39 (float32 truncation, 1:1).
    CHECK_EQ(x.trace.cost, 39);
    // Commands: Coord27 x2, EnqueueCmd15, BuildOp90(-4).
    CHECK_EQ(x.trace.cmdCount, 4);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kCoord27);
    CHECK_EQ(x.trace.cmds[0].a, 200); CHECK_EQ(x.trace.cmds[0].b, 300);
    // v14 = r0(5)+3 - (relation(0)+127) = 8 - 127 = -119
    CHECK_EQ(x.trace.cmds[0].c, -119);
    CHECK_EQ(x.trace.cmds[1].op, (int)PrivCommand::kCoord27);
    // v48 = r1(7)+3 - (relation(0)+127) = 10 - 127 = -117
    CHECK_EQ(x.trace.cmds[1].c, -117);
    CHECK_EQ(x.trace.cmds[2].op, (int)PrivCommand::kEnqueueCmd15);
    CHECK_EQ(x.trace.cmds[2].c, 39);
    CHECK_EQ(x.trace.cmds[3].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[3].b, -4);
}

TEST(PrivA, HatredPassiveCantAfford) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 4;
    PrivEvent ev{}; ev.targetId = 2; ev.partnerId = 3;
    x.people = { MkPerson(2, 200, 0), MkPerson(3, 300, 0) };
    x.wealth = 1000; x.currency = 1;               // currency < cost(40) -> 32
    CHECK_EQ((int)PrivilegePanelGenerateHatred(&actor, &ev, &h), 32);
    CHECK_EQ(x.trace.cmdCount, 0);
}

TEST(PrivA, HatredOfficeConfirm) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);        // office holder GUI path
    PrivEvent ev{};
    x.people = { MkPerson(2, 200, 0), MkPerson(3, 300, 0) }; // two pickers
    x.wealth = 500; x.skillOk = true; x.resourceOk = true;
    x.rng = {0, 0};
    x.buttons = { kPrivBtnOk };                    // one OK then loop-exit
    CHECK_EQ((int)PrivilegePanelGenerateHatred(&actor, &ev, &h), 1);
    CHECK_EQ(x.trace.doneState, 1);
    CHECK_EQ(x.trace.cost, 19);                     // 2% of 1000 = 19.99.. -> 19
    CHECK_EQ(x.trace.cmdCount, 4);
}

TEST(PrivA, HatredOfficeCancelPicker) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    PrivEvent ev{};
    x.people = {};                                  // first picker returns null
    CHECK_EQ((int)PrivilegePanelGenerateHatred(&actor, &ev, &h), 0);
}

// --------------------------------------------------------------------------
// Blackmail success roll: RandomModulo(8) <= matchCount.
// --------------------------------------------------------------------------
TEST(PrivA, BlackmailConfirmSuccessRoll) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    PrivPerson subject = MkPerson(2, 200, 6);
    x.matchCount = 4;
    x.rng = {3};                                    // RandomModulo(8)=3 <= 4 -> success
    x.buttons = { kPrivBtnOk };
    CHECK_EQ(PrivilegeBlackmailConfirm(&actor, &subject, &h), 1);
    CHECK_EQ(x.rngMods[0], 8);
    CHECK_EQ(x.trace.doneState, 1);
    // success: SlotReset28 command emitted.
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kSlotReset28);
}

TEST(PrivA, BlackmailConfirmFailRoll) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    PrivPerson subject = MkPerson(2, 200, 7);
    x.matchCount = 1;
    x.rng = {5};                                    // 5 > 1 -> failure
    x.buttons = { kPrivBtnOk };
    CHECK_EQ(PrivilegeBlackmailConfirm(&actor, &subject, &h), 0);
    // failure: relation -26 + BuildOp90 -2.
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kCoord27);
    CHECK_EQ(x.trace.cmds[0].c, -26);
    CHECK_EQ(x.trace.cmds[1].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[1].b, -2);
    CHECK_EQ(x.trace.lastMessageId, 6489);
}

TEST(PrivA, BlackmailConfirmRejectsNonOfficeSubject) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    PrivPerson subject = MkPerson(2, 200, 5);       // kind 5 != 6/7
    CHECK_EQ(PrivilegeBlackmailConfirm(&actor, &subject, &h), 0);
    CHECK_EQ(x.trace.cmdCount, 0);
}

TEST(PrivA, BlackmailPassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0);
    PrivEvent ev{}; ev.targetId = 2;
    x.people = { MkPerson(2, 200, 0) };
    x.matchCount = 2;
    x.rng = {5};                                    // 5 > 2 -> failure path (-26)
    CHECK_EQ((int)PrivilegePanelBlackmail(&actor, &ev, &h), 16);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kCoord27);
    CHECK_EQ(x.trace.cmds[0].c, -26);
}

TEST(PrivA, BlackmailPassiveNoTarget) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0);
    PrivEvent ev{}; ev.targetId = 99;               // not in table
    CHECK_EQ((int)PrivilegePanelBlackmail(&actor, &ev, &h), 96);
}

// --------------------------------------------------------------------------
// MakePeace: passive draws RNG up-front, writes positive relations.
// --------------------------------------------------------------------------
TEST(PrivA, MakePeacePassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 3;
    PrivEvent ev{}; ev.targetId = 2; ev.partnerId = 3;
    x.people = { MkPerson(2, 200, 0), MkPerson(3, 300, 0) };
    x.rng = {10, 20};                               // 15+10=25 , 15+20=35
    CHECK_EQ((int)PrivilegePanelMakePeace(&actor, &ev, &h), 16);
    CHECK_EQ(x.rngMods[0], 0x28); CHECK_EQ(x.rngMods[1], 0x28);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kCoord27);
    CHECK_EQ(x.trace.cmds[0].c, 25);
    CHECK_EQ(x.trace.cmds[1].c, 35);
    CHECK_EQ(x.trace.cmds[2].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[2].b, -3);
}

TEST(PrivA, MakePeacePassiveOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 2; // < 3 -> 32
    PrivEvent ev{};
    x.rng = {0, 0};
    CHECK_EQ((int)PrivilegePanelMakePeace(&actor, &ev, &h), 32);
    // RNG still drawn up-front (2 draws) before the gate.
    CHECK_EQ((int)x.rngMods.size(), 2u);
}

// --------------------------------------------------------------------------
// Interrogation: charges actor -2 and target -3, immune gate, notify.
// --------------------------------------------------------------------------
TEST(PrivA, InterrogationPassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 2; actor.flag457 = 0;
    PrivEvent ev{}; ev.targetId = 2;
    x.people = { MkPerson(2, 200, 6) };             // office-holder target -> notify
    CHECK_EQ((int)PrivilegePanelInterrogation(&actor, &ev, &h), 16);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[0].b, -2);
    CHECK_EQ(x.trace.cmds[1].b, -3);
    CHECK_EQ(x.trace.cmds[2].op, (int)PrivCommand::kArgs25);
    CHECK_EQ(x.trace.cmds[2].b, 456);
    CHECK_EQ((int)x.entityMsgs.size(), 1u);          // office-holder notified
}

TEST(PrivA, InterrogationImmune) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5;
    actor.flag457 = 0x80;                            // sign bit set -> immune
    PrivEvent ev{};
    CHECK_EQ((int)PrivilegePanelInterrogation(&actor, &ev, &h), 32);
}

TEST(PrivA, InterrogationOfficeImmuneReturnsNeg127) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6); actor.flag457 = 0x80;
    PrivEvent ev{};
    CHECK_EQ((int)PrivilegePanelInterrogation(&actor, &ev, &h), -127);
}

// --------------------------------------------------------------------------
// Medicus: coin-flip success, fee transfer.
// --------------------------------------------------------------------------
TEST(PrivA, MedicusPassiveSuccess) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 2;
    x.wealth = 1000;                                 // 2% = 20
    x.rng = {1};                                     // RandomModulo(2)=1 -> success
    CHECK_EQ((int)PrivilegePanelMedicus(&actor, &h), 17);
    CHECK_EQ(x.rngMods[0], 2);
    CHECK_EQ(x.trace.cost, 19);                       // 2% of 1000 = 19.99.. -> 19
    // BuildOp90(0), Args25(456), EnqueueCmd15(fee) on success.
    bool sawFee = false;
    for (int i = 0; i < x.trace.cmdCount; ++i)
        if (x.trace.cmds[i].op == (int)PrivCommand::kEnqueueCmd15) sawFee = true;
    CHECK(sawFee);
}

TEST(PrivA, MedicusPassiveFailFlip) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 2;
    x.wealth = 1000;
    x.rng = {0};                                     // flip=0 -> no fee transfer
    CHECK_EQ((int)PrivilegePanelMedicus(&actor, &h), 17);
    for (int i = 0; i < x.trace.cmdCount; ++i)
        CHECK(x.trace.cmds[i].op != (int)PrivCommand::kEnqueueCmd15);
}

TEST(PrivA, MedicusOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 1; // < 2 -> 0
    CHECK_EQ((int)PrivilegePanelMedicus(&actor, &h), 0);
}

// --------------------------------------------------------------------------
// Divorce: afford gate + partner-link delta on both records.
// --------------------------------------------------------------------------
TEST(PrivA, DivorcePassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 8; actor.partnerId = 2;
    x.people = { MkPerson(2, 200, 0) };
    x.wealth = 1000; x.currency = 1000;             // 1000 >= 12% of 1000 (=120) ok
    CHECK_EQ((int)PrivilegePanelDivorce(&actor, &h), 17);
    // both records get a +0x5C link delta + a fee enqueue.
    int deltas = 0, fees = 0;
    for (int i = 0; i < x.trace.cmdCount; ++i) {
        if (x.trace.cmds[i].op == (int)PrivCommand::kDeltaField) ++deltas;
        if (x.trace.cmds[i].op == (int)PrivCommand::kEnqueueCmd15) ++fees;
    }
    CHECK_EQ(deltas, 2);
    CHECK_EQ(fees, 1);
    CHECK_EQ(x.trace.cost, 80);                      // 8% of 1000
}

TEST(PrivA, DivorceCantAfford) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 8;
    x.wealth = 1000; x.currency = 50;               // 50 < 120 -> 34
    CHECK_EQ((int)PrivilegePanelDivorce(&actor, &h), 34);
}

TEST(PrivA, DivorceOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 7; // < 8 -> 34
    CHECK_EQ((int)PrivilegePanelDivorce(&actor, &h), 34);
}

// --------------------------------------------------------------------------
// Convert: 768-array sweep, charm vs resistance roll, religion gate.
// --------------------------------------------------------------------------
TEST(PrivA, ConvertPassiveSweep) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0);
    actor.office404 = 6; actor.religion = 1; actor.charm132 = 100;
    PrivEvent ev{};
    x.wealth = 1000; x.currency = 1000;             // 6% = 60 <= 1000 ok
    // Three candidates: [0] convertible (diff religion, low resist), [1] same
    // religion (skip), [2] kind 7 (skip in passive).
    std::vector<PrivPerson> people = {
        MkPerson(10, 1000, 1), MkPerson(11, 1100, 1), MkPerson(12, 1200, 7),
    };
    people[0].religion = 2; people[0].byte994 = 0;  // 100 > 0+roll -> convert
    people[1].religion = 1;                          // same religion -> skip
    people[2].religion = 2;                          // diff but kind 7 -> skip
    x.rng = {5};                                     // one roll for the one candidate
    char r = PrivilegePanelConvert(&actor, &ev, &h, people.data(), (int)people.size());
    CHECK_EQ((int)r, 16);
    // Only one delta-field (the convertible candidate) + the fee enqueue + BuildOp90.
    int deltas = 0;
    for (int i = 0; i < x.trace.cmdCount; ++i)
        if (x.trace.cmds[i].op == (int)PrivCommand::kDeltaField) ++deltas;
    CHECK_EQ(deltas, 1);
    CHECK_EQ(x.rngMods.size(), 1u);
    CHECK_EQ(x.rngMods[0], 0x7E);
    CHECK_EQ(x.trace.cost, 59);                       // 6% of 1000 = 59.99.. -> 59
}

TEST(PrivA, ConvertPassiveOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5; // < 6 -> 32
    PrivEvent ev{};
    std::vector<PrivPerson> people;
    CHECK_EQ((int)PrivilegePanelConvert(&actor, &ev, &h, people.data(), 0), 32);
}

// --------------------------------------------------------------------------
// Apology: range gate, rival-pairs request.
// --------------------------------------------------------------------------
TEST(PrivA, ApologyPassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5;
    PrivEvent ev{}; ev.dragField = 3;               // 1..9 and <= office404
    CHECK_EQ((int)PrivilegePanelApology(&actor, &ev, &h), 0);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kRivalPairs);
    CHECK_EQ(x.trace.cmds[1].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[1].b, -3);
}

TEST(PrivA, ApologyPassiveOutOfRange) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 2;
    PrivEvent ev{}; ev.dragField = 5;               // > office404 -> 96
    CHECK_EQ((int)PrivilegePanelApology(&actor, &ev, &h), 96);
}

// --------------------------------------------------------------------------
// Charm: passive [1,5] gate; office picker -> 1.
// --------------------------------------------------------------------------
TEST(PrivA, CharmPassive) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5;
    PrivEvent ev{}; ev.dragField = 3; ev.targetId = 2;
    x.people = { MkPerson(2, 200, 0) };
    CHECK_EQ(PrivilegeCharmConfirm(&actor, &ev, &h), 16);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kBuildOp90);
    CHECK_EQ(x.trace.cmds[0].b, -3);
    CHECK_EQ(x.trace.cmds[1].op, (int)PrivCommand::kCoord27);
}

TEST(PrivA, CharmPassiveNoTarget) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5;
    PrivEvent ev{}; ev.dragField = 3; ev.targetId = 99;
    CHECK_EQ(PrivilegeCharmConfirm(&actor, &ev, &h), 96);
}

TEST(PrivA, CharmOffice) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    PrivEvent ev{};
    x.people = { MkPerson(2, 200, 0) };             // picker returns a person -> 1
    CHECK_EQ(PrivilegeCharmConfirm(&actor, &ev, &h), 1);
}

// --------------------------------------------------------------------------
// ExpelWorker: mode gates + dismiss.
// --------------------------------------------------------------------------
TEST(PrivA, ExpelPassiveSelf) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 5; actor.flags456 = 0;
    PrivEvent ev{}; ev.mode = 3; ev.targetId = 2;
    x.people = { MkPerson(2, 200, 0) };
    CHECK_EQ((int)PrivilegePanelExpelWorker(&actor, &ev, &h), 16);
    bool sawPair = false;
    for (int i = 0; i < x.trace.cmdCount; ++i)
        if (x.trace.cmds[i].op == (int)PrivCommand::kPair33) sawPair = true;
    CHECK(sawPair);
}

TEST(PrivA, ExpelPassiveOfficeGate) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 4; // < 5 -> 32
    PrivEvent ev{}; ev.mode = 3;
    CHECK_EQ((int)PrivilegePanelExpelWorker(&actor, &ev, &h), 32);
}

TEST(PrivA, ExpelOfficeImmune) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6);
    actor.flags456 = 0x80;                           // sign bit -> immune -> -127
    PrivEvent ev{}; ev.mode = 3;
    CHECK_EQ((int)PrivilegePanelExpelWorker(&actor, &ev, &h), -127);
}

// --------------------------------------------------------------------------
// ChangeProfession: office-only; confirm emits BuildOp72.
// --------------------------------------------------------------------------
TEST(PrivA, ChangeProfessionNonOffice) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0);          // kind != 6 -> 0
    CHECK_EQ((int)PrivilegePanelChangeProfession(&actor, &h), 0);
}

TEST(PrivA, ChangeProfessionConfirm) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6); actor.rank = 2;
    x.skillOk = true; x.confirmOk = true;
    x.buttons = { 5 };                               // grid cell index 5 (!= rank 2)
    CHECK_EQ((int)PrivilegePanelChangeProfession(&actor, &h), 16);
    CHECK_EQ(x.trace.cmds[0].op, (int)PrivCommand::kBuildOp72);
    CHECK_EQ(x.trace.cmds[0].b, 5);
    CHECK_EQ(x.trace.doneState, 1);
}

TEST(PrivA, ChangeProfessionSameProfSkip) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 6); actor.rank = 5;
    x.buttons = { 5 };                               // same as rank -> skip, no emit
    CHECK_EQ((int)PrivilegePanelChangeProfession(&actor, &h), 0);
    CHECK_EQ(x.trace.cmdCount, 0);
}

// --------------------------------------------------------------------------
// Dispatcher routing.
// --------------------------------------------------------------------------
TEST(PrivA, DispatcherRoutes) {
    Ctx x; auto h = MakeHooks(x);
    PrivPerson actor = MkPerson(1, 100, 0); actor.office404 = 1;
    PrivEvent ev{};
    // Medicus office gate (office<2 -> 0); routed by leaf id 0x560500.
    CHECK_EQ(PrivilegeDispatchPanelA(0x560500, &actor, &ev, &h), 0);
    // Unknown leaf id -> 0.
    CHECK_EQ(PrivilegeDispatchPanelA(0xDEADBEEF, &actor, &ev, &h), 0);
}
