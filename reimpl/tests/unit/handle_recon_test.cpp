// Unit tests for the CRT handle table + XcptActTab signal dispatch + the
// He/Result blit helpers. Golden vectors derived from the Hex-Rays decompile.
#include "tests/framework/test.h"

#include "crt/handle_recon.h"
#include "crt/signal_xcpt_recon.h"
#include "crt/signal.h"
#include "sim/he_recon.h"
#include "sim/handler_entry.h"
#include "sim/result_blit_recon.h"

#include <cstring>

using namespace guild;

// ---------------------------------------------------------------------------
// HandleTable
// ---------------------------------------------------------------------------
TEST(HeReconHandleTable, AddFindFreeRoundTrip) {
    crt::HandleTable t;
    // Empty + reserve floor 0 => HasFreeSlot returns 1 (count<=0).
    CHECK_EQ(t.HasFreeSlot(), 1);

    // AddEntry grows the table and returns sequential indices.
    CHECK_EQ(t.AddEntry(100), 0);
    CHECK_EQ(t.AddEntry(200), 1);
    CHECK_EQ(t.AddEntry(300), 2);
    CHECK_EQ(t.count(), 3);
    CHECK_EQ(t.at(0), 100);
    CHECK_EQ(t.at(1), 200);
    CHECK_EQ(t.at(2), 300);

    // NB the original's HasFreeSlot returns 1 when it runs off the end with every
    // slot non-zero (i.e. "no reusable zero slot — would have to append"), and 0
    // when it finds a zero slot mid-scan. All slots non-zero here => 1.
    CHECK_EQ(t.HasFreeSlot(), 1);

    // ClearEntry zeroes a non-zero, non-zero-index slot; the scan then hits that
    // zero and returns 0.
    t.ClearEntry(1);
    CHECK_EQ(t.at(1), 0);
    CHECK_EQ(t.HasFreeSlot(), 0);
    CHECK_EQ(t.AddEntry(999), 1);   // reuse the freed slot at index 1
    CHECK_EQ(t.at(1), 999);
    CHECK_EQ(t.count(), 3);         // no growth on reuse
}

TEST(HeReconHandleTable, ClearEntryIgnoresIndexZeroAndOob) {
    crt::HandleTable t;
    t.AddEntry(11); t.AddEntry(22);
    t.ClearEntry(0);          // index 0 is reserved (the > 0 guard)
    CHECK_EQ(t.at(0), 11);
    t.ClearEntry(5);          // out of range
    CHECK_EQ(t.count(), 2);
}

TEST(HeReconHandleTable, HasFreeSlotReserveFloor) {
    crt::HandleTable t;
    t.AddEntry(1); t.AddEntry(2);   // count 2
    t.set_reserve_floor(5);          // count(2) < floor(5) => 0 (reserve guard)
    CHECK_EQ(t.HasFreeSlot(), 0);
    t.set_reserve_floor(0);
    CHECK_EQ(t.HasFreeSlot(), 1);    // all slots non-zero => runs off end => 1
}

TEST(HeReconHandleTable, SetStdHandleGrowsAndZeroFills) {
    crt::HandleTable t;
    t.SetStdHandle(0x55, 4);         // index 4 on an empty table
    CHECK_EQ(t.count(), 5);
    CHECK_EQ(t.at(0), 0);
    CHECK_EQ(t.at(3), 0);
    CHECK_EQ(t.at(4), 0x55);
    CHECK_EQ(t.HasFreeSlot(), 0);    // slot 0 is zero => scan returns 0
    // Negative index is a no-op.
    t.SetStdHandle(0x77, -1);
    CHECK_EQ(t.count(), 5);
}

namespace {
int g_setStdCalls = 0;
i32 g_lastStdId = 0;
void rec_set_std(i32 id, i32) { ++g_setStdCalls; g_lastStdId = id; }
i32  zero_get_std(i32) { return 0; }
int  g_eventCounter = 1000;
i32  make_event() { return g_eventCounter++; }
}

TEST(HeReconHandleTable, InitStdHandlesFabricatesAndMirrors) {
    g_setStdCalls = 0;
    g_eventCounter = 1000;
    crt::HandleTable t;
    t.set_get_std(zero_get_std);     // no inherited handles
    t.set_create_event(make_event);  // fabricate fresh ones
    i32 last = t.InitStdHandles();
    CHECK_EQ(t.count(), 3);
    CHECK_EQ(last, 2);               // std-error slot index
    CHECK_EQ(t.at(0), 1000);
    CHECK_EQ(t.at(1), 1001);
    CHECK_EQ(t.at(2), 1002);
    // SetStdHandle path mirrors into the Win32 leaf.
    t.set_set_std(rec_set_std);
    t.SetStdHandle(0xAB, 1);
    CHECK_EQ(g_lastStdId, crt::kStdOutputHandle);
    t.SetStdHandle(0xCD, 0);
    CHECK_EQ(g_lastStdId, crt::kStdInputHandle);
    t.SetStdHandle(0xEF, 2);
    CHECK_EQ(g_lastStdId, crt::kStdErrorHandle);
    CHECK_EQ(g_setStdCalls, 3);
}

namespace {
int g_closeCount = 0;
void rec_close(i32) { ++g_closeCount; }
}
TEST(HeReconHandleTable, CleanupClosesOwnedHandles) {
    g_closeCount = 0;
    crt::HandleTable t;
    t.AddEntry(5);
    t.set_close(rec_close);
    t.track_owned(1); t.track_owned(2); t.track_owned(3);
    t.Cleanup();
    CHECK_EQ(g_closeCount, 3);
    CHECK_EQ(t.count(), 0);
}

// ---------------------------------------------------------------------------
// XcptActionTable (signal_xcpt_recon)
// ---------------------------------------------------------------------------
TEST(HeReconXcpt, FindActionGoldenCodes) {
    crt::XcptActionTable tab;
    CHECK(tab.FindAction((i32)0xC000000B) != nullptr);
    CHECK(tab.FindAction((i32)0xC000001D) != nullptr);
    CHECK(tab.FindAction((i32)0xC0000093) != nullptr);   // last entry
    CHECK(tab.FindAction((i32)0xDEADBEEF) == nullptr);   // absent
    // Action column golden values.
    CHECK_EQ(tab.FindAction((i32)0xC000001D)->action, 4); // SIGILL
    CHECK_EQ(tab.FindAction((i32)0xC000008E)->action, 8); // SIGFPE
    CHECK_EQ(tab.FindAction((i32)0xC000000B)->action, 0);
}

namespace {
int g_defaultRaiseCalls = 0;
int rec_default_raise(i32) { ++g_defaultRaiseCalls; return 42; }
int g_handlerCalls = 0;
int g_handlerSig = -1;
void rec_xcpt_handler(int sig) { ++g_handlerCalls; g_handlerSig = sig; }
}

TEST(HeReconXcpt, RaiseNoHandlerUsesDefault) {
    g_defaultRaiseCalls = 0;
    crt::XcptActionTable tab;
    tab.set_default_raise(rec_default_raise);
    // No handler installed on a known code => default raise(a2) returns 42.
    CHECK_EQ(tab.Raise((i32)0xC000008E, 7), 42);
    CHECK_EQ(g_defaultRaiseCalls, 1);
    // Unknown code also => default.
    CHECK_EQ(tab.Raise((i32)0x12345678, 9), 42);
    CHECK_EQ(g_defaultRaiseCalls, 2);
}

TEST(HeReconXcpt, RaiseInvokesFpeHandlerAndSetsCode) {
    g_handlerCalls = 0; g_handlerSig = -1;
    crt::XcptActionTable tab;
    tab.set_handler((i32)0xC000008E, rec_xcpt_handler); // FLT_DIVIDE_BY_ZERO
    int r = tab.Raise((i32)0xC000008E, 0);
    CHECK_EQ(r, -1);                 // handler-invoked path returns -1
    CHECK_EQ(g_handlerCalls, 1);
    CHECK_EQ(g_handlerSig, 8);        // SIGFPE delivered for action 8
    // _fpecode is restored after the call.
    CHECK_EQ(tab.fpecode(), 0);
}

TEST(HeReconXcpt, RaiseAckConsumesAndReturnsOne) {
    crt::XcptActionTable tab;
    // SIG_ACK == 5 in the handler slot: consume and return 1.
    tab.set_handler((i32)0xC000001D,
                    reinterpret_cast<crt::XcptHandler>(static_cast<std::uintptr_t>(5)));
    CHECK_EQ(tab.Raise((i32)0xC000001D, 0), 1);
    CHECK(tab.FindAction((i32)0xC000001D)->handler == nullptr); // cleared
}

TEST(HeReconXcpt, RaiseIgnPassesThroughWithoutInvoke) {
    g_handlerCalls = 0;
    crt::XcptActionTable tab;
    // SIG_IGN == 1: do not invoke, return -1.
    tab.set_handler((i32)0xC0000091,
                    reinterpret_cast<crt::XcptHandler>(static_cast<std::uintptr_t>(1)));
    CHECK_EQ(tab.Raise((i32)0xC0000091, 0), -1);
    CHECK_EQ(g_handlerCalls, 0);
}

// ---------------------------------------------------------------------------
// Signal wrappers over the existing SignalTable
// ---------------------------------------------------------------------------
TEST(HeReconSignal, ResetCtrlStateClearsWhenNeeded) {
    crt::SignalTable st;
    // Fresh table => SIG_DFL slots => NeedsCtrlHandler true => reset.
    int removed = 0;
    auto remove = +[](){ };           // can't capture; track via static below
    (void)remove;
    static int s_removed = 0; s_removed = 0;
    crt::Signal_ResetCtrlState(st, +[](){ ++s_removed; });
    CHECK_EQ(s_removed, 1);
    // After reset, slots 4 and 7 are SIG_RUNNING => NeedsCtrlHandler false.
    CHECK(!st.NeedsCtrlHandler());
    crt::Signal_ResetCtrlState(st, +[](){ ++s_removed; });
    CHECK_EQ(s_removed, 1);           // not called again
    (void)removed;
}

TEST(HeReconSignal, GetHandlerIfMatched) {
    crt::SignalTable st;
    // disposition == handler (both SIG_DFL) => returns the handler (0).
    CHECK_EQ((int)crt::Signal_GetHandlerIfMatched(st, crt::kSIGFPE), 0);
    // Make handler != disposition => returns 0.
    st.SetHandlerSlot(crt::kSIGFPE, 0x12345);
    CHECK_EQ((int)crt::Signal_GetHandlerIfMatched(st, crt::kSIGFPE), 0);
    // Make them equal again.
    st.SetDispositionSlot(crt::kSIGFPE, 0x12345);
    CHECK_EQ(crt::Signal_GetHandlerIfMatched(st, crt::kSIGFPE), (std::uintptr_t)0x12345);
}

TEST(HeReconSignal, InstallTermHooks) {
    crt::TermHooks h;
    auto initFn  = +[]() -> int { return 1; };
    auto resetFn = +[]() -> int { return 2; };
    crt::Signal_InstallTermHooks(h, initFn, resetFn);
    CHECK(h.initHandlerTable == initFn);
    CHECK(h.resetCtrlState == resetFn);
}

// ---------------------------------------------------------------------------
// He_ComputeEntityScore
// ---------------------------------------------------------------------------
namespace {
struct FakePerson { u16 marker; };
const void* fake_person_find(i32 id) {
    static FakePerson p;
    if (id == 7777) { p.marker = 33; return &p; }
    return nullptr;
}
u16  fake_person_marker(const void* r) { return static_cast<const FakePerson*>(r)->marker; }
i32  fake_office_rank(u16, i32) { return 2; }
// Return 0 favorability so the score reduces to (0*w + 0.5)*N*1600 with the
// exact float 0.5 (no truncation ambiguity from the float 0.005 weight).
double fake_favorability(u16, u16, i32) { return 0.0; }
}

TEST(HeReconScore, ComputeEntityScoreGoldenVector) {
    sim::HeScoreEntityView tbl;
    std::memset(&tbl, 0, sizeof(tbl));
    // Place a matching row at index 3.
    tbl.set_entity(3, 12345);
    tbl.set_person(3, 7777);
    // action index 8 -> kHeScoreTable[8][3] == 0 (column +12). Use index 2 instead
    // (kHeScoreTable[2] = {0x302,0,2,2,...} so [3]==2, nonzero).
    tbl.set_action(3, 2);

    sim::HeScoreLeaves leaves;
    leaves.personFind = fake_person_find;
    leaves.personMarker = fake_person_marker;
    leaves.officeRank = fake_office_rank;
    leaves.favorability = fake_favorability;

    u16 marker = 0; i32 score = 0; u8 rowByte = 0xAB;
    int ok = sim::He_ComputeEntityScore(tbl, 12345, /*evalMarker*/1, &marker, &score,
                                        leaves, &rowByte);
    CHECK_EQ(ok, 1);
    CHECK_EQ((int)marker, 33);
    // v12 = scoreTable[2][3] = 2; officeRank = 2 => (2+2)=4.
    // score = (0*0.005 + 0.5) * 4 * 1600 = 0.5*4*1600 = 3200 (exact float math).
    CHECK_EQ(score, 3200);
    // 0x4c512f `mov [edx],al`: *a5 = (BYTE)kHeScoreTable[action][0]. For action 2,
    // row[0]=0x302 -> low byte 0x02.
    CHECK_EQ((int)rowByte, 0x02);
}

TEST(HeReconScore, ComputeEntityScoreRejects) {
    sim::HeScoreEntityView tbl;
    std::memset(&tbl, 0, sizeof(tbl));
    sim::HeScoreLeaves leaves;
    leaves.personFind = fake_person_find;
    leaves.personMarker = fake_person_marker;
    leaves.officeRank = fake_office_rank;
    leaves.favorability = fake_favorability;
    u16 m = 0; i32 s = 0;
    // entityId -1 => 0 immediately.
    CHECK_EQ(sim::He_ComputeEntityScore(tbl, -1, 0, &m, &s, leaves), 0);
    // No matching entityId in the table => 0.
    CHECK_EQ(sim::He_ComputeEntityScore(tbl, 0x4242, 0, &m, &s, leaves), 0);
    // action with score-row [3]==0 (index 0 -> kHeScoreTable[0][3]==0) => 0.
    tbl.set_entity(1, 555); tbl.set_action(1, 0); tbl.set_person(1, 7777);
    CHECK_EQ(sim::He_ComputeEntityScore(tbl, 555, 0, &m, &s, leaves), 0);
}

// ---------------------------------------------------------------------------
// He_TutorialEventHandler state machine
// ---------------------------------------------------------------------------
namespace {
struct TutFakeRec { u8 pad[112]; i32 state; u8 tail[216]; };
int g_openCalls=0, g_dragCalls=0, g_closeCalls=0, g_resetCalls=0, g_freeCalls=0, g_modeCalls=0;
i32 g_openRet=0, g_dragRet=0;
i32 fake_open() { ++g_openCalls; return g_openRet; }
i32 fake_drag(i32, void*) { ++g_dragCalls; return g_dragRet; }
void fake_close(i32) { ++g_closeCalls; }
void fake_reset() { ++g_resetCalls; }
void fake_free(void*) { ++g_freeCalls; }
i32 fake_mode() { ++g_modeCalls; return 1234; }
sim::TutorialLeaves makeLeaves(i32* flag) {
    sim::TutorialLeaves l;
    l.tutorialActive = true;
    l.openMainPanel = fake_open;
    l.processDragDropClick = fake_drag;
    l.closeEventPanel = fake_close;
    l.resetChapterPointer = fake_reset;
    l.freeHandlerEntry = fake_free;
    l.findModeIndex = fake_mode;
    l.chapterAdvancedFlag = flag;
    return l;
}
}

TEST(HeReconTutorial, State0OpensPanel) {
    g_openCalls=0; g_openRet=0;
    TutFakeRec rec; std::memset(&rec, 0, sizeof(rec));
    i32 flag = 0;
    auto l = makeLeaves(&flag);
    // state 0, open returns 0 => state set to 1.
    sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(g_openCalls, 1);
    CHECK_EQ(rec.state, 1);
    // open returns nonzero => state set to 2.
    std::memset(&rec, 0, sizeof(rec)); g_openRet = 9;
    sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(rec.state, 2);
}

TEST(HeReconTutorial, State1DragDrop) {
    g_dragCalls=0; g_dragRet=0;
    TutFakeRec rec; std::memset(&rec, 0, sizeof(rec)); rec.state = 1;
    i32 flag = 0; auto l = makeLeaves(&flag);
    sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(g_dragCalls, 1);
    CHECK_EQ(rec.state, 1);          // drag not complete => stays 1
    g_dragRet = 1;
    sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(rec.state, 2);          // drag complete => advances to 2
}

TEST(HeReconTutorial, State2Terminal) {
    g_closeCalls=g_resetCalls=g_freeCalls=g_modeCalls=0;
    TutFakeRec rec; std::memset(&rec, 0, sizeof(rec)); rec.state = 2;
    i32 flag = 0; auto l = makeLeaves(&flag);
    u32 r = sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(g_closeCalls, 1);
    CHECK_EQ(g_resetCalls, 1);
    CHECK_EQ(g_freeCalls, 1);
    CHECK_EQ(g_modeCalls, 1);
    CHECK_EQ((int)flag, 1);
    CHECK_EQ((int)r, 1234);
}

TEST(HeReconTutorial, InactiveTutorialNoop) {
    TutFakeRec rec; std::memset(&rec, 0, sizeof(rec)); rec.state = 2;
    i32 flag = 0; auto l = makeLeaves(&flag);
    l.tutorialActive = false;
    g_closeCalls=0;
    sim::He_TutorialEventHandler(&rec, 0, l);
    CHECK_EQ(g_closeCalls, 0);       // gated off
}

// ---------------------------------------------------------------------------
// Result blit helpers
// ---------------------------------------------------------------------------
TEST(HeReconResult, ValidatePlotsPixel) {
    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    sim::ResultBitmap ctx;
    ctx.pixels = buf; ctx.stride = 8;
    sim::Result_Handler_Validate(3, 2, &ctx, 0xBEEF);
    CHECK_EQ((int)buf[8*2 + 3], (int)0xBEEF);
}

TEST(HeReconResult, BroadcastClips) {
    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    sim::ResultBitmap ctx;
    ctx.pixels = buf; ctx.stride = 8;
    ctx.width = 8; ctx.height = 8;
    ctx.clipX0 = 1; ctx.clipY0 = 1; ctx.clipX1 = 6; ctx.clipY1 = 6;
    // In-bounds + in-clip => plotted, returns 1.
    CHECK_EQ(sim::Result_Handler_Broadcast(3, 3, &ctx, 0x1111), 1);
    CHECK_EQ((int)buf[8*3 + 3], (int)0x1111);
    // Outside clip rect => 0, not plotted.
    CHECK_EQ(sim::Result_Handler_Broadcast(0, 0, &ctx, 0x2222), 0);
    CHECK_EQ((int)buf[0], 0);
    // Outside surface => 0.
    CHECK_EQ(sim::Result_Handler_Broadcast(9, 9, &ctx, 0x3333), 0);
}

namespace {
int g_blitCalls=0; i32 g_dstRect[4]; i32 g_srcRect[4];
int rec_blit(void*, const i32 dst[4], i32, const i32 src[4]) {
    ++g_blitCalls;
    for (int i=0;i<4;++i){ g_dstRect[i]=dst[i]; g_srcRect[i]=src[i]; }
    return 0;
}
}

TEST(HeReconResult, BroadcastBlitClipMath) {
    g_blitCalls = 0;
    sim::ResultBitmap src; std::memset(&src, 0, sizeof(src));
    src.clipX1 = 100; src.clipY1 = 100;   // src width/height
    src._8 = 0x5000;                       // blit-data ptr present
    src._15 = 0;                           // not skipped
    src._13 = 0;                           // v15 = 0 => finalize path

    sim::ResultBlitLeaves leaves;
    leaves.blit = rec_blit;
    int target = 0;
    // Fully on-surface, no negatives: dst(10,20,w=30,h=40) src(5,6).
    int r = sim::Result_Broadcast(10, 20, 30, 40, &src, 5, 6, &target, leaves);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_blitCalls, 1);
    // No clamping (50 <= 100 both axes): v11=40(dstH), a3=30(dstW).
    // v13 (first blit rect arg) = {a6, v8, v11+a6, a3+v8} = {5, 6, 45, 36}.
    CHECK_EQ(g_dstRect[0], 5);
    CHECK_EQ(g_dstRect[1], 6);
    CHECK_EQ(g_dstRect[2], 45);
    CHECK_EQ(g_dstRect[3], 36);
    // v14 (second blit rect) = {v9, a2, v11+v9, a3+a2} = {10, 20, 50, 50}.
    CHECK_EQ(g_srcRect[0], 10);
    CHECK_EQ(g_srcRect[1], 20);
    CHECK_EQ(g_srcRect[2], 50);
    CHECK_EQ(g_srcRect[3], 50);
}

TEST(HeReconResult, BroadcastFullyClippedReturnsZero) {
    sim::ResultBitmap src; std::memset(&src, 0, sizeof(src));
    src.clipX1 = 100; src.clipY1 = 100;
    sim::ResultBlitLeaves leaves;
    int target = 0;
    // width clamps to 0 (a2+a3 > height but a3 forced 0): dstH 0 => return 0.
    CHECK_EQ(sim::Result_Broadcast(0, 0, 0, 10, &src, 0, 0, &target, leaves), 0);
}
