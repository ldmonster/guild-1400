// Unit tests for the Script-VM per-command bodies (script_import2).
// Golden vectors computed by hand / python; libc oracles for the string copies.
#include "sim/script_import2.h"
#include "sim/script_vm.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A heap-backed 2584-byte script context, zero-initialized.
struct Ctx {
    std::vector<u8> buf = std::vector<u8>(kScriptContextStride, 0);
    u8* base() { return buf.data(); }
    i32& at_i32(int off) { return *reinterpret_cast<i32*>(buf.data() + off); }
    u8&  at_u8 (int off) { return buf[off]; }
    u8*& at_ptr(int off) { return *reinterpret_cast<u8**>(buf.data() + off); }
};

// ---- recording hooks --------------------------------------------------------
struct Rec {
    int reportCount = 0;
    const char* lastMsg = nullptr;
    int createCalls = 0; const char* createModel = nullptr; ScriptHandle createResult = 0;
    int validReturn = 1;
    ScriptHandle walkChar = 0, walkTarget = 0, walkExtra = 0; int walkCalls = 0;
    ScriptHandle handlerChar = 0, handlerTarget = 0; int handlerCalls = 0;
    int blockedReturn = 0; int blockedCalls = 0;
    ScriptHandle soundChar = 0; int soundVal = 0; ScriptCmdRecord* soundCmd = nullptr; int soundCalls = 0;
    void* insertCb = nullptr; i64 insertArg = 0; void* insertResult = nullptr; int insertCalls = 0;
};
Rec g_rec;

void hReport(u8*, u32, const char* msg) { g_rec.reportCount++; g_rec.lastMsg = msg; }
ScriptHandle hCreate(const char* m, i32*) { g_rec.createCalls++; g_rec.createModel = m; return g_rec.createResult; }
int  hValid(ScriptHandle) { return g_rec.validReturn; }
void hWalk(ScriptHandle c, ScriptHandle t, ScriptHandle e) { g_rec.walkCalls++; g_rec.walkChar = c; g_rec.walkTarget = t; g_rec.walkExtra = e; }
void hHandler(ScriptHandle c, ScriptHandle t) { g_rec.handlerCalls++; g_rec.handlerChar = c; g_rec.handlerTarget = t; }
int  hBlocked(ScriptHandle*, int) { g_rec.blockedCalls++; return g_rec.blockedReturn; }
void hSound(ScriptHandle c, ScriptCmdRecord* cmd, int s) { g_rec.soundCalls++; g_rec.soundChar = c; g_rec.soundCmd = cmd; g_rec.soundVal = s; }
void* hInsert(ScriptHandle, void* cb, i64 arg, int) { g_rec.insertCalls++; g_rec.insertCb = cb; g_rec.insertArg = arg; return g_rec.insertResult; }

ScriptCmdHooks MakeHooks() {
    ScriptCmdHooks h;
    h.reportError = hReport; h.createChar = hCreate; h.isValidPtr = hValid;
    h.queueWalk = hWalk; h.cmdHandler = hHandler; h.dummyBlocked = hBlocked;
    h.createSound = hSound; h.insertAction = hInsert;
    return h;
}
void Reset() { g_rec = Rec(); g_rec.validReturn = 1; }

} // namespace

// ---------------------------------------------------------------------------
TEST(ScriptImport2Cmd, CallUserFunctionInvoked) {
    Ctx ctx; auto& E = ScriptEngine(); E.currentCtx = ctx.base();
    static int seen = -1;
    struct F { static void f(int a) { seen = a; } };
    void (*slot)(int) = &F::f;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    CHECK_EQ(CallUserFunction(&slot, 42), 1);
    CHECK_EQ(seen, 42);
    CHECK_EQ(g_rec.reportCount, 0);
    // Null slot -> error path, returns 0.
    void (*nullslot)(int) = nullptr;
    CHECK_EQ(CallUserFunction(&nullslot, 7), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, CallUserFunctionExtended) {
    Ctx ctx; auto& E = ScriptEngine(); E.currentCtx = ctx.base();
    static int calls = 0;
    struct F { static void f() { calls++; } };
    void (*slot)(void) = &F::f;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    CHECK_EQ(CallUserFunctionExtended(&slot), 1);
    CHECK_EQ(calls, 1);
    void (*ns)(void) = nullptr;
    CHECK_EQ(CallUserFunctionExtended(&ns), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, CreateCharacterSuccessAndFail) {
    Ctx ctx; auto& E = ScriptEngine(); E.currentCtx = ctx.base();
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    const char* model = "hero.mdl"; const char** mp = &model;
    g_rec.createResult = 0x1234;
    CHECK_EQ(CmdCreateCharacter(mp, nullptr), 0x1234);
    CHECK_EQ(g_rec.createCalls, 1);
    CHECK(std::strcmp(g_rec.createModel, "hero.mdl") == 0);
    CHECK_EQ(g_rec.reportCount, 0);
    // failure
    g_rec.createResult = 0;
    CHECK_EQ(CmdCreateCharacter(mp, nullptr), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, SleepFirstEntryArmsAndSnapshots) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr; E.gameTick = 100;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    i32 dur = 140;
    // First entry (execCmd not the sleep cmd): returns 1, arms wait, snapshots.
    CHECK_EQ(CmdSleep(&dur), 1);
    CHECK_EQ(reinterpret_cast<ScriptCmdFn>(ctx.at_ptr(kScCmdBlocked)), kFnCmdSleep);
    CHECK_EQ((i32)ctx.at_i32(kScSleepWake), 100);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, SleepResumeStillSleepingVsWake) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.gameTick = 100;
    // mark execCmd as the sleeping command (resume path)
    ScriptCmdRecord cmd{}; cmd.fn = kFnCmdSleep; E.execCmd = &cmd;
    ctx.at_i32(kScSleepWake) = 100;   // snapshotted earlier at tick 100
    i32 dur = 140;                    // 140/14 == 10 -> wake at 110
    Reset();
    // tick 105: 100+10=110 > 105 -> still sleeping (re-arm), returns 0.
    E.gameTick = 105;
    ctx.at_ptr(kScCmdBlocked) = nullptr;   // clear arm to observe re-arm
    CHECK_EQ(CmdSleep(&dur), 0);
    CHECK_EQ(reinterpret_cast<ScriptCmdFn>(ctx.at_ptr(kScCmdBlocked)), kFnCmdSleep);
    // tick 115: 110 > 115 false -> wake (no re-arm), returns 0.
    E.gameTick = 115;
    ctx.at_ptr(kScCmdBlocked) = nullptr;
    CHECK_EQ(CmdSleep(&dur), 0);
    CHECK(ctx.at_ptr(kScCmdBlocked) == nullptr);
    // dur == -1 -> sleep forever -> always re-arm.
    i32 forever = -1;
    ctx.at_ptr(kScCmdBlocked) = nullptr;
    CHECK_EQ(CmdSleep(&forever), 0);
    CHECK_EQ(reinterpret_cast<ScriptCmdFn>(ctx.at_ptr(kScCmdBlocked)), kFnCmdSleep);
    E.execCmd = nullptr;
}

TEST(ScriptImport2Cmd, KillLocalScripts) {
    // 4 slots is enough to exercise the predicate; but the loop walks 128 slots
    // (330752 bytes). Allocate the full table.
    std::vector<u8> table(330752, 0);
    auto slot = [&](int i) { return table.data() + i * kScriptContextStride; };
    auto setI = [&](int i, int off, i32 v) { *reinterpret_cast<i32*>(slot(i) + off) = v; };
    // current context = slot 0, owner 7, handle 100
    setI(0, kScHandle, 100); setI(0, kScOwner, 7);
    auto& E = ScriptEngine(); E.currentCtx = slot(0);
    // slot 1: same owner, runnable, different handle -> killed
    setI(1, kScHandle, 101); setI(1, kScOwner, 7); slot(1)[kScRunFlags] = kRunFlagRunnable;
    // slot 2: same owner but NOT runnable -> spared
    setI(2, kScHandle, 102); setI(2, kScOwner, 7); slot(2)[kScRunFlags] = 0;
    // slot 3: different owner -> spared
    setI(3, kScHandle, 103); setI(3, kScOwner, 9); slot(3)[kScRunFlags] = kRunFlagRunnable;
    // slot 4: free (handle -1) -> spared
    setI(4, kScHandle, -1); setI(4, kScOwner, 7); slot(4)[kScRunFlags] = kRunFlagRunnable;
    // all other slots default handle 0 != -1, owner 0 != 7 -> spared
    static std::vector<i32> finished;
    finished.clear();
    auto finish = +[](u8* c) { finished.push_back(*reinterpret_cast<i32*>(c + kScHandle)); };
    CHECK_EQ(CmdKillLocalScripts(table.data(), finish), 0);
    CHECK_EQ((int)finished.size(), 1);
    CHECK_EQ(finished[0], 101);
}

TEST(ScriptImport2Cmd, WalkToDummyValidQueues) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0x1000, dummy = 0x2000;
    ScriptHandle* cp = &chr; ScriptHandle* dp = &dummy;
    ctx.at_u8(kScStmtMode) = 0;     // not blocking
    CHECK_EQ(CmdWalkToDummy(cp, dp), 1);
    CHECK_EQ(g_rec.walkCalls, 1);
    CHECK_EQ(g_rec.walkChar, 0x1000);
    CHECK_EQ(g_rec.walkTarget, 0x2000);
    CHECK(ctx.at_ptr(kScCmdBlocked) == nullptr);   // stmtMode != 1 -> no arm
    // stmtMode==1 -> arms the wait
    ctx.at_u8(kScStmtMode) = 1;
    CHECK_EQ(CmdWalkToDummy(cp, dp), 1);
    CHECK_EQ(reinterpret_cast<ScriptCmdFn>(ctx.at_ptr(kScCmdBlocked)), kFnCmdWalkToDummy);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, WalkToDummyInvalidReports) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0, dummy = 0x2000;   // null char -> error
    ScriptHandle* cp = &chr; ScriptHandle* dp = &dummy;
    CHECK_EQ(CmdWalkToDummy(cp, dp), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    // valid pointers but isValidPtr fails -> returns 0, no report
    chr = 0x1000; g_rec.validReturn = 0;
    CHECK_EQ(CmdWalkToDummy(cp, dp), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, WalkToDummyRotateUsesHandler) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0x11, dummy = 0x22; ScriptHandle* cp = &chr; ScriptHandle* dp = &dummy;
    CHECK_EQ(CmdWalkToDummyRotate(cp, dp), 1);
    CHECK_EQ(g_rec.handlerCalls, 1);
    CHECK_EQ(g_rec.handlerChar, 0x11);
    CHECK_EQ(g_rec.handlerTarget, 0x22);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, WalkVerifiedBlockedReports) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0x11; ScriptHandle dummy = 0x22; ScriptHandle* cp = &chr;
    g_rec.blockedReturn = 1;   // dummy blocked
    CHECK_EQ(CmdWalkToDummyVerified(cp, &dummy, 0), 0);
    CHECK_EQ(g_rec.blockedCalls, 1);
    CHECK_EQ(g_rec.reportCount, 1);
    CHECK_EQ(g_rec.walkCalls, 0);
    // not blocked -> queues walk
    Reset(); g_rec.blockedReturn = 0;
    CHECK_EQ(CmdWalkToDummyVerified(cp, &dummy, 0), 1);
    CHECK_EQ(g_rec.walkCalls, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, WalkRotateVerifiedBlockedReports) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0x11; ScriptHandle dummy = 0x22; ScriptHandle* cp = &chr;
    g_rec.blockedReturn = 1;
    CHECK_EQ(CmdWalkToDummyRotateVerified(cp, &dummy, 0), 0);
    CHECK_EQ(g_rec.reportCount, 1);
    Reset(); g_rec.blockedReturn = 0;
    CHECK_EQ(CmdWalkToDummyRotateVerified(cp, &dummy, 0), 1);
    CHECK_EQ(g_rec.handlerCalls, 1);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, PlayCharacterAni) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    ScriptCmdRecord cmd{}; E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    ScriptHandle chr = 0x55; i32 snd = 9; ScriptHandle* cp = &chr; i32* sp = &snd;
    // valid char, stmtMode != 1 -> createSound, return 0
    CHECK_EQ(CmdPlayCharacterAni(cp, sp), 0);
    CHECK_EQ(g_rec.soundCalls, 1);
    CHECK_EQ(g_rec.soundChar, 0x55);
    CHECK_EQ(g_rec.soundVal, 9);
    // null char -> report, return 1
    Reset(); chr = 0;
    CHECK_EQ(CmdPlayCharacterAni(cp, sp), 1);
    CHECK_EQ(g_rec.reportCount, 1);
    (void)cmd;
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, PlayCharacterAniSoundFlagsAndCopies) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    // Provide a record buffer the copies write into (rec+240, rec+304).
    std::vector<u8> rec(512, 0xAA);
    g_rec.insertResult = rec.data();
    ScriptHandle chr = 0x77; u8 flags = 0x3C; i32 snd = 5;
    ScriptHandle* cp = &chr;
    // The original copies a stream of 2-byte units, stopping at the FIRST zero
    // byte of EITHER half of a unit (the do/while breaks on the low byte and on
    // the high byte). So "A B C D \0" copies A,B,C,D then the terminating low
    // byte 0; "X Y \0" copies X,Y,0.
    char ani[] = {'A','B','C','D',0, 0};
    char snm[] = {'X','Y',0, 0};
    char* ap = ani; char* np = snm;
    CHECK_EQ(CmdPlayCharacterAniSound(cp, &ap, &flags, &snd, &np), 0);
    CHECK_EQ(g_rec.insertCalls, 1);
    CHECK_EQ(g_rec.insertCb, kCbPlayAnimationSound);
    // arg == flags | 0x2E00000000
    CHECK_EQ(g_rec.insertArg, (i64)0x3C | 0x2E00000000LL);
    // verbatim 2-byte-unit copy into rec+240 and rec+304.
    CHECK_EQ(rec[240], (u8)'A'); CHECK_EQ(rec[241], (u8)'B');
    CHECK_EQ(rec[242], (u8)'C'); CHECK_EQ(rec[243], (u8)'D');
    CHECK_EQ(rec[244], (u8)0);                         // terminating low byte
    CHECK_EQ(rec[304], (u8)'X'); CHECK_EQ(rec[305], (u8)'Y');
    CHECK_EQ(rec[306], (u8)0);
    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2Cmd, PlayCharacterAniScriptCopyAndTooLong) {
    Ctx ctx; auto& E = ScriptEngine();
    E.currentCtx = ctx.base(); E.execCmd = nullptr; E.ownerId = 0x4242;
    Reset(); ScriptCmdHooks h = MakeHooks(); SetScriptCmdHooks(&h);
    std::vector<u8> rec(512, 0xAA);
    g_rec.insertResult = rec.data();
    ScriptHandle chr = 0x88; u8 flags = 1; i32 snd = 0; ScriptHandle* cp = &chr;
    char script[] = "anim01";        // short
    char extra[]  = "ctx";
    char* scp = script; char* exp = extra;
    CHECK_EQ(CmdPlayCharacterAniScript(cp, &flags, &snd, &scp, &exp), 0);
    CHECK_EQ(g_rec.insertCb, kCbPlayAnimationScript);
    // StrNCopyPad into rec+240 (63) and rec+144 (95)
    CHECK(std::memcmp(rec.data() + 240, "anim01", 6) == 0);
    CHECK_EQ(rec[246], (u8)0);                       // zero-filled remainder
    CHECK(std::memcmp(rec.data() + 144, "ctx", 3) == 0);
    CHECK_EQ(*reinterpret_cast<i32*>(rec.data() + 380), (i32)0x4242);
    // name too long (>= 0x5F) -> report, return 1
    Reset(); g_rec.insertResult = rec.data();
    char longname[200]; std::memset(longname, 'z', sizeof(longname)); longname[199] = 0;
    char* lp = longname; char ex2[] = "c"; char* e2 = ex2;
    CHECK_EQ(CmdPlayCharacterAniScript(cp, &flags, &snd, &lp, &e2), 1);
    CHECK_EQ(g_rec.reportCount, 1);
    SetScriptCmdHooks(nullptr);
}
