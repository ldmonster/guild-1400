// Script-VM long-tail, slice 2: the per-command ("Cmd*") opcode bodies.
// Faithful 1:1 from gilde.exe pseudocode. See script_import2.h for the map and
// the shared-engine-state / hooks design notes.
#include "sim/script_import2.h"
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Shared engine state (one definition).  dword_62E8A8 / 62E8CC / 62EB38 / 62E8D4.
// ---------------------------------------------------------------------------
ScriptEngineState& ScriptEngine() {
    static ScriptEngineState s;
    return s;
}

// Identity tokens for the resume (+44 fn-ptr) comparisons. The originals
// compare the literal function address; here each body has a unique sentinel.
namespace {
char g_fnSleep, g_fnWalk, g_fnWalkRot, g_fnWalkRotVer, g_fnWalkVer;
char g_fnAni, g_fnAniSnd, g_fnAniScr;
char g_cbAniSound, g_cbAniScript;
} // namespace
const ScriptCmdFn kFnCmdSleep                    = &g_fnSleep;
const ScriptCmdFn kFnCmdWalkToDummy              = &g_fnWalk;
const ScriptCmdFn kFnCmdWalkToDummyRotate        = &g_fnWalkRot;
const ScriptCmdFn kFnCmdWalkToDummyRotateVerified= &g_fnWalkRotVer;
const ScriptCmdFn kFnCmdWalkToDummyVerified      = &g_fnWalkVer;
const ScriptCmdFn kFnCmdPlayCharacterAni         = &g_fnAni;
const ScriptCmdFn kFnCmdPlayCharacterAniSound    = &g_fnAniSnd;
const ScriptCmdFn kFnCmdPlayCharacterAniScript   = &g_fnAniScr;
void* const kCbPlayAnimationSound  = &g_cbAniSound;
void* const kCbPlayAnimationScript = &g_cbAniScript;

// ---------------------------------------------------------------------------
// Cross-module leaf hooks (inert defaults in this library .cpp; tests install).
// ---------------------------------------------------------------------------
namespace {
const ScriptCmdHooks* g_hooks = nullptr;
ScriptCmdHooks        g_inert{};   // all-null -> inert (see accessors below)
} // namespace
void SetScriptCmdHooks(const ScriptCmdHooks* hooks) { g_hooks = hooks; }
const ScriptCmdHooks& GetScriptCmdHooks() { return g_hooks ? *g_hooks : g_inert; }

namespace {
// Inert-default-safe wrappers (a null hook behaves as the original would for
// a no-op host: report does nothing, predicates fail/zero, creators fail).
void HostReportError(u8* ctx, u32 cursor, const char* msg) {
    const auto& h = GetScriptCmdHooks();
    if (h.reportError) h.reportError(ctx, cursor, msg);
}
ScriptHandle HostCreateChar(const char* model, i32* outFields) {
    const auto& h = GetScriptCmdHooks();
    return h.createChar ? h.createChar(model, outFields) : 0;
}
int HostIsValidPtr(ScriptHandle p) {
    const auto& h = GetScriptCmdHooks();
    return h.isValidPtr ? h.isValidPtr(p) : (p != 0);   // default: nonzero==valid
}
void HostQueueWalk(ScriptHandle c, ScriptHandle t, ScriptHandle e) {
    const auto& h = GetScriptCmdHooks();
    if (h.queueWalk) h.queueWalk(c, t, e);
}
void HostCmdHandler(ScriptHandle c, ScriptHandle t) {
    const auto& h = GetScriptCmdHooks();
    if (h.cmdHandler) h.cmdHandler(c, t);
}
int HostDummyBlocked(ScriptHandle* d, int e) {
    const auto& h = GetScriptCmdHooks();
    return h.dummyBlocked ? h.dummyBlocked(d, e) : 0;    // default: not blocked
}
void HostCreateSound(ScriptHandle c, ScriptCmdRecord* cmd, int s) {
    const auto& h = GetScriptCmdHooks();
    if (h.createSound) h.createSound(c, cmd, s);
}
void* HostInsertAction(ScriptHandle c, void* cb, i64 arg, int e) {
    const auto& h = GetScriptCmdHooks();
    return h.insertAction ? h.insertAction(c, cb, arg, e) : nullptr;  // default: fail
}

// The character object's "in-motion / busy" flag at +296 (polled on resume).
inline i32 CharBusy(ScriptHandle character) {
    return *reinterpret_cast<i32*>(character + 296);
}

// Context-field accessors over the byte-exact 2584-byte context.
inline u8*&  CtxCmdBlocked(u8* ctx) { return *reinterpret_cast<u8**>(ctx + kScCmdBlocked); }
inline u8&   CtxStmtMode (u8* ctx)  { return *(ctx + kScStmtMode); }
inline u32&  CtxCursor   (u8* ctx)  { return *reinterpret_cast<u32*>(ctx + kScCursor); }
inline u32&  CtxSleepWake(u8* ctx)  { return *reinterpret_cast<u32*>(ctx + kScSleepWake); }

// Store an identity token into the (pointer-sized) wait slot.
inline void ArmWait(u8* ctx, ScriptCmdFn token) {
    CtxCmdBlocked(ctx) = reinterpret_cast<u8*>(token);
}
inline bool IsResuming(ScriptCmdFn token) {
    auto& E = ScriptEngine();
    return E.execCmd && E.execCmd->fn == token;
}

// VIBE_Util_StrNCopyPad @0x5d9360: copy up to n non-NUL bytes, zero-fill the
// rest of n (no terminator beyond the n-byte field).
void StrNCopyPad(u8* dst, const char* src, int n) {
    while (n && *src) { *dst++ = static_cast<u8>(*src++); --n; }
    while (n) { *dst++ = 0; --n; }
}

// Copy a verbatim 2-byte-stride (UTF-16 / wide-char) string until a 0 char,
// matching the inlined byte loops in CmdPlayCharacterAniSound.
void CopyWideZ(char* dst, const char* src) {
    for (;;) {
        char lo = src[0];
        dst[0] = lo;
        if (!lo) break;
        char hi = src[1];
        dst[1] = hi;
        dst += 2; src += 2;
        if (!hi) break;
    }
}
} // namespace

// Error message strings recovered from the image (data refs).
static const char kErrCallUserFunc[]   = "CallUserFunction(): Invalid userfunction";
static const char kErrCallUserFuncX[]  = "CallUserFunctionExtended(): Invalid userfunction";
static const char kErrCreateChar[]     = "CreateCharacter(): Could not load character";
static const char kErrWalkToDummy[]    = "WalkToDummy(): invalid character or dummy";
static const char kErrWalkRot[]        = "WalkToDummyRotate(): invalid character or dummy";
static const char kErrWalkRotVer[]     = "WalkToDummyRotateVerified(): invalid character or dummy";
static const char kErrWalkRotVerBlk[]  = "WalkToDummyRotateVerified(): dummy blocked";
static const char kErrWalkVer[]        = "WalkToDummyVerified(): invalid character or dummy";
static const char kErrWalkVerBlk[]     = "WalkToDummyVerified(): dummy blocked";
static const char kErrAni[]            = "PlayCharacterAni(): invalid character or dummy";
static const char kErrAniSnd[]         = "PlayCharacterAniSound(): invalid character";
static const char kErrAniScr[]         = "PlayCharacterAniScript(): invalid character";
static const char kErrAniScrLong[]     = "PlayCharacterAniScript(): script name too long";

// (0x43c650 CmdReturnTrue / 0x43c658 CmdReturnFalse live in script_import.cpp.)

// ===========================================================================
// 0x43c708 — VIBE_Script_CmdSleep
// ===========================================================================
i32 CmdSleep(const i32* durationPtr) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdSleep)) {
        // Still sleeping: -1 == forever; or wake-tick not yet reached -> re-arm.
        if (*durationPtr == -1 ||
            CtxSleepWake(E.currentCtx) + static_cast<u32>(*durationPtr / 14) > E.gameTick) {
            ArmWait(E.currentCtx, kFnCmdSleep);
        }
        return 0;
    }
    // First entry: arm the wait and snapshot the current tick.
    ArmWait(E.currentCtx, kFnCmdSleep);
    CtxSleepWake(E.currentCtx) = E.gameTick;
    return 1;
}

// ===========================================================================
// 0x43c790 — VIBE_Script_CmdKillLocalScripts
// ===========================================================================
i32 CmdKillLocalScripts(u8* ctxTableBase, void (*finish)(u8* ctx)) {
    auto& E = ScriptEngine();
    // 330752 / 2584 == 128 slots; faithful raw stride loop.
    for (int off = 0; off != 330752; off += kScriptContextStride) {
        u8* slot = ctxTableBase + off;
        const i32 handle   = *reinterpret_cast<i32*>(slot + kScHandle);
        const u8  runFlags = *(slot + kScRunFlags);
        const i32 owner    = *reinterpret_cast<i32*>(slot + kScOwner);
        const i32 curOwner = *reinterpret_cast<i32*>(E.currentCtx + kScOwner);
        const i32 curHandle= *reinterpret_cast<i32*>(E.currentCtx + kScHandle);
        if (handle != -1 && (runFlags & kRunFlagRunnable) != 0 &&
            owner == curOwner && handle != curHandle) {
            if (finish) finish(slot);
        }
    }
    return 0;
}

// ===========================================================================
// 0x43c7ec / 0x43c81c — CallUserFunction[Extended]
// ===========================================================================
i32 CallUserFunction(void (**fnSlot)(int), int arg) {
    auto& E = ScriptEngine();
    if (*fnSlot) { (*fnSlot)(arg); return 1; }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrCallUserFunc);
    return 0;
}
i32 CallUserFunctionExtended(void (**fnSlot)(void)) {
    auto& E = ScriptEngine();
    if (*fnSlot) { (*fnSlot)(); return 1; }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrCallUserFuncX);
    return 0;
}

// ===========================================================================
// 0x43c9c0 — VIBE_Script_CmdCreateCharacter
// ===========================================================================
ScriptHandle CmdCreateCharacter(const char** modelPtr, i32* outFields) {
    auto& E = ScriptEngine();
    ScriptHandle result = HostCreateChar(*modelPtr, outFields);
    if (!result) {
        HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrCreateChar);
        // Original returns garbage ecx on failure; we return 0 (load failed).
        return 0;
    }
    return result;
}

// ===========================================================================
// 0x43cb28 — VIBE_Script_CmdWalkToDummy
// ===========================================================================
i32 CmdWalkToDummy(ScriptHandle* charPtr, ScriptHandle* dummyPtr) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdWalkToDummy)) {
        // char busy (+296) -> re-arm wait; either way "still walking" returns 0.
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdWalkToDummy);
        return 0;
    }
    if (*dummyPtr && *charPtr) {
        if (!HostIsValidPtr(*dummyPtr) || !HostIsValidPtr(*charPtr)) return 0;
        HostQueueWalk(*charPtr, *dummyPtr, 0);
        if (CtxStmtMode(E.currentCtx) == 1) ArmWait(E.currentCtx, kFnCmdWalkToDummy);
        return 1;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkToDummy);
    return 0;
}

// ===========================================================================
// 0x43cbc8 — VIBE_Script_CmdWalkToDummyRotate
// ===========================================================================
i32 CmdWalkToDummyRotate(ScriptHandle* charPtr, ScriptHandle* dummyPtr) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdWalkToDummyRotate)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdWalkToDummyRotate);
        return 0;
    }
    if (*dummyPtr && *charPtr) {
        if (!HostIsValidPtr(*dummyPtr) || !HostIsValidPtr(*charPtr)) return 0;
        HostCmdHandler(*charPtr, *dummyPtr);
        if (CtxStmtMode(E.currentCtx) == 1) ArmWait(E.currentCtx, kFnCmdWalkToDummyRotate);
        return 1;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkRot);
    return 0;
}

// ===========================================================================
// 0x43cc68 — VIBE_Script_CmdWalkToDummyRotateVerified
// ===========================================================================
i32 CmdWalkToDummyRotateVerified(ScriptHandle* charPtr, ScriptHandle* dummyPtr, int extra) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdWalkToDummyRotateVerified)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdWalkToDummyRotateVerified);
        return 0;
    }
    if (*dummyPtr && *charPtr) {
        if (HostDummyBlocked(dummyPtr, extra)) {
            HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkRotVerBlk);
            return 0;
        }
        HostCmdHandler(*charPtr, *dummyPtr);
        if (CtxStmtMode(E.currentCtx) == 1) ArmWait(E.currentCtx, kFnCmdWalkToDummyRotateVerified);
        return 1;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkRotVer);
    return 0;
}

// ===========================================================================
// 0x43cd18 — VIBE_Script_CmdWalkToDummyVerified
// ===========================================================================
i32 CmdWalkToDummyVerified(ScriptHandle* charPtr, ScriptHandle* dummyPtr, int extra) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdWalkToDummyVerified)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdWalkToDummyVerified);
        return 0;
    }
    if (*dummyPtr && *charPtr) {
        if (HostDummyBlocked(dummyPtr, extra)) {
            HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkVerBlk);
            return 0;
        }
        HostQueueWalk(*charPtr, *dummyPtr, reinterpret_cast<intptr_t>(dummyPtr));
        if (CtxStmtMode(E.currentCtx) == 1) ArmWait(E.currentCtx, kFnCmdWalkToDummyVerified);
        return 1;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrWalkVer);
    return 0;
}

// ===========================================================================
// 0x43cdc8 — VIBE_Script_CmdPlayCharacterAni
// ===========================================================================
i32 CmdPlayCharacterAni(ScriptHandle* charPtr, i32* soundPtr) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdPlayCharacterAni)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdPlayCharacterAni);
        return 0;
    }
    if (*charPtr) {
        HostCreateSound(*charPtr, E.execCmd, *soundPtr);
        if (CtxStmtMode(E.currentCtx) != 1) return 0;
        ArmWait(E.currentCtx, kFnCmdPlayCharacterAni);
        return 0;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrAni);
    return 1;
}

// ===========================================================================
// 0x43ce98 — VIBE_Script_CmdPlayCharacterAniSound
// ===========================================================================
i32 CmdPlayCharacterAniSound(ScriptHandle* charPtr, char** aniName, u8* flagsPtr,
                             i32* soundPtr, char** sndName) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdPlayCharacterAniSound)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdPlayCharacterAniSound);
        return 0;
    }
    if (*charPtr) {
        const i64 arg = static_cast<i64>(*flagsPtr) | 0x2E00000000LL;
        u8* rec = static_cast<u8*>(HostInsertAction(*charPtr, kCbPlayAnimationSound, arg, *soundPtr));
        if (rec) {
            CopyWideZ(reinterpret_cast<char*>(rec + 240), *aniName);
            CopyWideZ(reinterpret_cast<char*>(rec + 304), *sndName);
        }
        if (CtxStmtMode(E.currentCtx) != 1) return 0;
        ArmWait(E.currentCtx, kFnCmdPlayCharacterAniSound);
        return 0;
    }
    HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrAniSnd);
    return 1;
}

// ===========================================================================
// 0x43cfb0 — VIBE_Script_CmdPlayCharacterAniScript
// ===========================================================================
i32 CmdPlayCharacterAniScript(ScriptHandle* charPtr, u8* flagsPtr, i32* soundPtr,
                              char** scriptName, char** extraName) {
    auto& E = ScriptEngine();
    if (IsResuming(kFnCmdPlayCharacterAniScript)) {
        if (CharBusy(*charPtr)) ArmWait(E.currentCtx, kFnCmdPlayCharacterAniScript);
        return 0;
    }
    if (!*charPtr) {
        HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrAniScr);
        return 1;
    }
    const i64 arg = static_cast<i64>(*flagsPtr) | 0x2E00000000LL;
    u8* rec = static_cast<u8*>(HostInsertAction(*charPtr, kCbPlayAnimationScript, arg, *soundPtr));
    if (rec) {
        if (std::strlen(*scriptName) >= 0x5F) {
            HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrAniScrLong);
            return 1;
        }
        StrNCopyPad(rec + 240, *scriptName, 63);
        StrNCopyPad(rec + 144, *extraName, 95);
        *reinterpret_cast<i32*>(rec + 380) = E.ownerId;
    }
    if (CtxStmtMode(E.currentCtx) != 1) return 0;
    ArmWait(E.currentCtx, kFnCmdPlayCharacterAniScript);
    return 0;
}

} // namespace guild::sim
