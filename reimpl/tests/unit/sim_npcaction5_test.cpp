// Unit tests for the deferred ContextAction variants (contextaction2.{h,cpp}) and
// the remaining self-contained NpcEvent helpers (npcaction5.{h,cpp}). Each test
// drives a translated executor/step with synthetic state and a seeded RNG / mock
// leaves, checking the verdict, eligibility gate, leaf dispatch, and re-armed He
// state against a reference derived from the IDA decompilation.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/contextaction2.h"
#include "sim/interaction_handlers.h"
#include "sim/npcaction5.h"
#include "sim/npcaction.h"
#include "sim/he.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

ContextActor MakeActor() {
    ContextActor a;
    std::memset(&a, 0, sizeof(a));
    return a;
}
InteractionEventRec MakeEvent(u8 mode) {
    InteractionEventRec ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.mode = mode;
    return ev;
}

} // namespace

// ===========================================================================
// ContextAction2 — rank-train tiers.
// ===========================================================================
TEST(SimCA2, TrainRank4B_exactGate) {
    // exact ==4: rank>4 -> 4 ; rank<4 -> 1 ; rank==4 with activate+kind6 -> 2.
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate);
    a.rank = 5; CHECK_EQ((int)ContextTrainRank4B(&a, &ev), 4);
    a.rank = 3; CHECK_EQ((int)ContextTrainRank4B(&a, &ev), 1);
    a.rank = 4; a.kind = 6; ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextTrainRank4B(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivShowDialog);
    // wrong mode -> reject
    ev.mode = 2; CHECK_EQ((int)ContextTrainRank4B(&a, &ev), 1);
}

TEST(SimCA2, TrainRank4Plus_hybridGate) {
    // ==6 -> 4 ; <4 -> 1 ; 4/5 fall through to dialog (2).
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate); a.kind = 6;
    a.rank = 6; CHECK_EQ((int)ContextTrainRank4Plus(&a, &ev), 4);
    a.rank = 3; CHECK_EQ((int)ContextTrainRank4Plus(&a, &ev), 1);
    a.rank = 4; CHECK_EQ((int)ContextTrainRank4Plus(&a, &ev), 2);
    a.rank = 5; CHECK_EQ((int)ContextTrainRank4Plus(&a, &ev), 2);
}

TEST(SimCA2, TrainRank5B6A6B_geGates) {
    auto a = MakeActor(); auto ev = MakeEvent(kModeTooltip); a.kind = 6;
    a.rank = 4; CHECK_EQ((int)ContextTrainRank5B(&a, &ev), 1);
    a.rank = 5; ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextTrainRank5B(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x198C);
    a.rank = 5; CHECK_EQ((int)ContextTrainRank6A(&a, &ev), 1);
    a.rank = 6; ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextTrainRank6A(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x1992);
    a.rank = 6; ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextTrainRank6B(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x1994);
}

// ===========================================================================
// ContextAction2 — FileLawsuit pair (Gesetz mock).
// ===========================================================================
static int s_gesetzCode = 0;
static bool s_gesetzHas = false;
static bool GesetzMock(ContextActor*, int /*type*/, int* out) {
    if (out) *out = s_gesetzCode;
    return s_gesetzHas;
}

TEST(SimCA2, FileLawsuit_activate) {
    SetGesetzFindRecordHook(GesetzMock);
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate);
    a.profession = 7; a.kind = 6;
    // no record -> reject(1)
    s_gesetzHas = false; CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 1);
    // has record, not busy, not already-filed -> leaf | 2
    s_gesetzHas = true; s_gesetzCode = 0x55;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivEnactLaw);
    CHECK_EQ(ev.targetId, 0x55);              // ev+4 := record code
    CHECK_EQ(ev.lawsuitScratch, 1);           // ev+532 := type
    // busy (457&2) -> 9
    a.flag457 = 2; CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 9);
    a.flag457 = 0;
    // already filed (458 & 8) -> reject 1
    a.flag458 = 0x08; CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 1);
    // Type2 keys off 458 & 0x10
    a.flag458 = 0x08; CHECK_EQ((int)ContextFileLawsuitType2(&a, &ev), 2);
    a.flag458 = 0x10; CHECK_EQ((int)ContextFileLawsuitType2(&a, &ev), 1);
    SetGesetzFindRecordHook(nullptr);
}

TEST(SimCA2, FileLawsuit_drag) {
    SetGesetzFindRecordHook(GesetzMock);
    auto a = MakeActor(); auto src = MakeActor(); auto ev = MakeEvent(kModeDragApply);
    a.flag457 = 1; src.profession = 7; ev.dragSource = &src;
    s_gesetzHas = true; s_gesetzCode = 0x12;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 0xA);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivEnactLaw);
    // src already filed -> reject
    src.flag458 = 0x08; CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 1);
    // missing actor flag457 bit -> reject
    src.flag458 = 0; a.flag457 = 0;
    CHECK_EQ((int)ContextFileLawsuitType1(&a, &ev), 1);
    SetGesetzFindRecordHook(nullptr);
}

// ===========================================================================
// ContextAction2 — profession-menu variants (submethod vs profession gate).
// ===========================================================================
TEST(SimCA2, ProfessionMenu_submethodGate) {
    auto a = MakeActor(); auto ev = MakeEvent(kModeTooltip); a.kind = 6;
    // ProfessionMenu28 gates on submethod (+361 == profession2), NOT profession.
    a.profession = 99; a.profession2 = 28;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextProfessionMenu28(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x19A3);
    a.profession2 = 27; CHECK_EQ((int)ContextProfessionMenu28(&a, &ev), 1);
    // Range30: submethod 30..33 accepted, 29/34 rejected
    a.profession2 = 30; CHECK_EQ((int)ContextProfessionMenuRange30(&a, &ev), 2);
    a.profession2 = 33; CHECK_EQ((int)ContextProfessionMenuRange30(&a, &ev), 2);
    a.profession2 = 29; CHECK_EQ((int)ContextProfessionMenuRange30(&a, &ev), 1);
    a.profession2 = 34; CHECK_EQ((int)ContextProfessionMenuRange30(&a, &ev), 1);
}

TEST(SimCA2, ProfessionMenu_professionGate) {
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate); a.kind = 6;
    a.profession2 = 0;  // submethod irrelevant for +358-gated variants
    // Office accepts 20/24/25
    a.profession = 24; ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextProfessionMenuOffice(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivShowDialog);
    a.profession = 17; CHECK_EQ((int)ContextProfessionMenuOffice(&a, &ev), 1);
    // Type17A/B accept exactly 17
    a.profession = 17; CHECK_EQ((int)ContextProfessionMenuType17A(&a, &ev), 2);
    CHECK_EQ((int)ContextProfessionMenuType17B(&a, &ev), 2);
    a.profession = 18; CHECK_EQ((int)ContextProfessionMenuType17A(&a, &ev), 1);
    // Clergy accepts 14/10 ; Type23 accepts 23/19
    a.profession = 10; CHECK_EQ((int)ContextProfessionMenuClergy(&a, &ev), 2);
    a.profession = 14; CHECK_EQ((int)ContextProfessionMenuClergy(&a, &ev), 2);
    a.profession = 19; CHECK_EQ((int)ContextProfessionMenuType23(&a, &ev), 2);
    a.profession = 23; CHECK_EQ((int)ContextProfessionMenuType23(&a, &ev), 2);
    a.profession = 20; CHECK_EQ((int)ContextProfessionMenuType23(&a, &ev), 1);
}

// ===========================================================================
// ContextAction2 — command-by-profession + drag variants.
// ===========================================================================
TEST(SimCA2, CommandType24_activateAndDrag) {
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate);
    a.kind = 6; a.profession = 24;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextCommandType24(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivGenerateHatred);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x19CE);  // kind6 always renders
    // busy -> 9 (still rendered the tooltip first)
    a.flag457 = 2; CHECK_EQ((int)ContextCommandType24(&a, &ev), 9);
    a.flag457 = 0;
    // wrong profession -> 1
    a.profession = 25; CHECK_EQ((int)ContextCommandType24(&a, &ev), 1);
    // drag apply: 457&1 + dragSource profession 24 -> leaf, 10
    auto src = MakeActor(); src.profession = 24;
    ev.mode = kModeDragApply; a.flag457 = 1; ev.dragSource = &src;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextCommandType24(&a, &ev), 10);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivGenerateHatred);
}

TEST(SimCA2, CommandVariants_leafIds) {
    auto a = MakeActor(); auto ev = MakeEvent(kModeActivate); a.kind = 6;
    struct Case { u8 prof; char (*fn)(ContextActor*, InteractionEventRec*); int leaf; };
    Case cs[] = {
        {21, &ContextCommandType21Or26, kPrivConvert},
        {26, &ContextCommandType21Or26, kPrivConvert},
        {24, &ContextCommandType24Drag, kPrivApology},
        {18, &ContextCommandType18Or22, kPrivEvidenceReviewAlt},
        {22, &ContextCommandType18Or22, kPrivEvidenceReviewAlt},
        {26, &ContextCommandType26,     kPrivMiracle},
    };
    for (auto& c : cs) {
        a.profession = c.prof; ev.mode = kModeActivate; a.flag457 = 0;
        ResetInteractionLeafTrace();
        CHECK_EQ((int)c.fn(&a, &ev), 2);
        CHECK_EQ(g_leafTrace.privilegeLeaf, c.leaf);
    }
}

// ===========================================================================
// ContextAction2 — registration table.
// ===========================================================================
TEST(SimCA2, Registration) {
    CHECK_EQ(RegisterContextActions(), 23);
    CHECK(ContextAction2_Lookup(0x56f514) == &ContextTrainRank4B);
    CHECK(ContextAction2_Lookup(0x571134) == &ContextCommandType26);
    CHECK(ContextAction2_Lookup(0x56fbe0) == &ContextFileLawsuitType1);
    CHECK(ContextAction2_Lookup(0x000000) == nullptr);
}

// ===========================================================================
// NpcAction5 — NpcEvent helpers.
// ===========================================================================
namespace {
HeRecord* MakeHe(std::vector<uint8_t>& buf) {
    buf.assign(600, 0);
    return reinterpret_cast<HeRecord*>(buf.data());
}
// recording leaf hooks
int s_freed = 0;
i32 FreeHook(HeRecord*) { s_freed++; return -777; }
i32 s_lastReqArg = 12345;
i32 QueueHook(int arg, HeRecord*) { s_lastReqArg = arg; return 0xBEEF; }
int s_invCount = 0; int s_invFound = 0; int s_invItemAsked = -1;
int InvHook(int itemId, i32* count) { s_invItemAsked = itemId; if (count) *count = s_invCount; return s_invFound; }
i32 s_resolveField = 0;
i32 ResolveHook(i32) { return s_resolveField; }
}

TEST(SimNA5, CountdownTickEntity) {
    NpcLeafHooks hooks{}; hooks.freeHandlerEntry = FreeHook; hooks.queueRequestEntity29 = QueueHook;
    SetNpcLeafHooks(&hooks);
    GameTime clk{}; clk.day = 100; clk.hour = 6; SetNpcClock(clk);

    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);
    // state<0 -> free
    s_freed = 0; He_State(h) = -1; He_Counter172(h) = 5;
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), -777);
    CHECK_EQ(s_freed, 1);
    // counter<=0 -> free
    He_State(h) = 0; He_Counter172(h) = 0;
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), -777);
    // normal tick: decrement counter, stamp +82, +24h, queue, store handle
    He_State(h) = 0; He_Counter172(h) = 5; He_Flags(h) = 0;
    s_lastReqArg = -1;
    i32 r = NpcAction5_CountdownTickEntity(h);
    CHECK_EQ(r, 0xBEEF);
    CHECK_EQ(He_Counter172(h), 4);
    CHECK_EQ(He_ReqHandle(h), 0xBEEF);
    CHECK_EQ(s_lastReqArg, 0);
    CHECK_EQ(He_ApptTime(h).day, 101);   // 100 + 24h carry
    CHECK_EQ((int)He_ApptTime(h).hour, 6);
    // flag 0x04 set -> skip body, return record passthrough (non-handle)
    He_Flags(h) = 4; He_Counter172(h) = 5;
    i32 r2 = NpcAction5_CountdownTickEntity(h);
    CHECK_EQ(He_Counter172(h), 5);       // not decremented
    CHECK(r2 != 0xBEEF);
    SetNpcLeafHooks(nullptr);
}

TEST(SimNA5, ResolveTargetAndReset) {
    NpcLeafHooks hooks{}; hooks.freeHandlerEntry = FreeHook; hooks.findInventorySlot = InvHook;
    SetNpcLeafHooks(&hooks);
    NpcAction5Hooks h5{}; h5.resolveEntityField97 = ResolveHook; SetNpcAction5Hooks(&h5);
    GameTime clk{}; clk.day = 50; clk.hour = 2; clk.minute = 0; SetNpcClock(clk);

    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);
    He_Counter172(h) = 999;                            // +172 filter id

    // entity unresolved -> free
    s_resolveField = 0; s_freed = 0;
    CHECK_EQ(NpcAction5_ResolveTargetAndReset(h), -777);
    CHECK_EQ(s_freed, 1);

    // entity resolves but inventory missing -> free
    s_resolveField = 1; s_invFound = 0; s_invCount = 0;
    CHECK_EQ(NpcAction5_ResolveTargetAndReset(h), -777);

    // item id is HIWORD(*(h+192)); set it to 376 in the high word
    *reinterpret_cast<i32*>(reinterpret_cast<uint8_t*>(h) + 192) =
        static_cast<i32>(static_cast<uint32_t>(376) << 16);
    s_invFound = 1; s_invCount = 30; s_invItemAsked = -1;
    He_State(h) = 7; He_TargetObjId(h) = 555;
    i32 r = NpcAction5_ResolveTargetAndReset(h);
    CHECK_EQ(s_invItemAsked, 376);
    CHECK_EQ(He_State(h), 0);           // +112 cleared
    CHECK_EQ(He_TargetObjId(h), 0);     // +176 cleared
    // +180 GameTime got clock + 30 minutes -> hour 2, minute 30 ; returns hour
    GameTime* dl = reinterpret_cast<GameTime*>(reinterpret_cast<uint8_t*>(h) + 180);
    CHECK_EQ(dl->day, 50);
    CHECK_EQ((int)dl->hour, 2);
    CHECK_EQ(dl->minute, 30);
    CHECK_EQ(r, 2);
    SetNpcLeafHooks(nullptr); SetNpcAction5Hooks(nullptr);
}

TEST(SimNA5, Registration) {
    CHECK_EQ(RegisterNpcActions5(), 2);
    CHECK(NpcAction5_TableEntry(0x4db248) == &NpcAction5_CountdownTickEntity);
    CHECK(NpcAction5_TableEntry(0x4d877c) == &NpcAction5_ResolveTargetAndReset);
    CHECK(NpcAction5_TableEntry(0) == nullptr);
}
