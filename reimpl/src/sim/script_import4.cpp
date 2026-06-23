// Script-VM long-tail slice 4 — see script_import4.h for the function inventory
// and the gilde.exe address map.  Each body is a 1:1 translation of the Hex-Rays
// pseudocode (32-bit x86, imagebase 0x400000); offsets reference the kSc* field
// constants from script_vm.h where they line up.
#include "sim/script_import4.h"
#include "sim/script_import.h"   // real sibling: FindByHandle (0x442174)
#include <cstring>
#include <cstdio>

namespace guild::sim {
namespace {

// --- inert default hook implementations (defined in THIS library .cpp) -------

// Faithful VIBE_Util_StrCmp (0x5d3f10): byte strcmp, 0 == equal.
int DefStrCmp(const char* a, const char* b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    while (*pa && *pa == *pb) { ++pa; ++pb; }
    if (*pa == *pb) return 0;
    return *pa < *pb ? -1 : 1;
}
int  DefReadDword(int, u8*)                       { return 0; }   // EOF
u8*  DefLoadFromDir(const char*)                  { return nullptr; }
u8*  DefLoadScript(const char*)                   { return nullptr; }
void DefRunMain(u8*)                              {}
int  DefCompileBlock(u8*)                         { return 0; }   // compile fail
u8*  DefLookupFunction(u8*, const char*)          { return nullptr; }
void DefEnterFunction(u8*, u8*)                   {}
u8   DefNextToken(const char*, u8* out)           { if (out) out[0] = 0; return 0; }
void DefReportError(u8*, u32, const char*)        {}
void DefFinishCtx(u8*)                            {}
int  DefDeclareLocal(u8*, u8, const char*, int)   { return 0; }
void DefDestroyCtx(u8*)                           {}
void DefFreeDebug(void*)                          {}
int  DefRunFrameLoop(int, int)                    { return 0; }   // loop ends
void DefStrNCopyPad(char* dst, const char* src, int n) {
    if (!dst) return;
    int i = 0;
    for (; i < n && src && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = 0;
}
char* DefStrtok(char*, int)                       { return nullptr; }
int   DefParseInt(const char* s) {                              // atoi, base 10
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    int sign = 1;
    if (*s == '-') { sign = -1; ++s; } else if (*s == '+') ++s;
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return sign * v;
}

const ScriptImport4Hooks kDefaults = {
    &DefStrCmp, &DefReadDword, &DefLoadFromDir, &DefLoadScript, &DefRunMain,
    &DefCompileBlock, &DefLookupFunction, &DefEnterFunction, &DefNextToken,
    &DefReportError, &DefFinishCtx, &DefDeclareLocal, &DefDestroyCtx,
    &DefFreeDebug, &DefRunFrameLoop, &DefStrNCopyPad, &DefStrtok, &DefParseInt,
};
const ScriptImport4Hooks* g_hooks = &kDefaults;

// ctype-bit table byte_64A208: bit 0x20 == "decimal digit" (the original tests
// (c + 1)).  We only need the digit class, so derive it directly.
bool CtypeDigit(unsigned char c) { return c >= '0' && c <= '9'; }

// Copy a NUL-terminated UTF-16 string (2-byte stride) verbatim, exactly as the
// engine's open-coded byte-pair copy loops do (used by ParseDeclaration,
// ParseAndRunCall, RunWithArgs). Copies until a 16-bit zero is written.
void CopyUtf16(char* dst, const char* src) {
    char* d = dst;
    const char* s = src;
    while (true) {
        char lo = s[0];
        d[0] = lo;
        if (!lo) break;
        char hi = s[1];
        d[1] = hi;
        d += 2; s += 2;
        if (!hi) break;
    }
}

} // namespace

ScriptEngineTables& ScriptTables() { static ScriptEngineTables t; return t; }
void ResetScriptTables() { ScriptTables() = ScriptEngineTables(); }

void SetScriptImport4Hooks(const ScriptImport4Hooks* hooks) {
    g_hooks = hooks ? hooks : &kDefaults;
}
const ScriptImport4Hooks& GetScriptImport4Hooks() { return *g_hooks; }

// ---------------------------------------------------------------------------
// 0x442a98 / 0x442aa8 / 0x442ab8 — statement-mode flag setters.
// ---------------------------------------------------------------------------
u8* SetBreakFlag() {
    u8* ctx = ScriptEngine().currentCtx;                 /*0x442a98*/
    if (ctx) ctx[kScStmtMode] = 1;                       /*0x442a9d*/
    return ctx;                                          /*0x442aa4*/
}
u8* SetContinueFlag() {
    u8* ctx = ScriptEngine().currentCtx;                 /*0x442aa8*/
    if (ctx) ctx[kScStmtMode] = 2;                       /*0x442aad*/
    return ctx;                                          /*0x442ab4*/
}
u8* ClearReturnFlag() {
    u8* ctx = ScriptEngine().currentCtx;                 /*0x442ab8*/
    if (ctx) ctx[kScStmtMode] = 0;                       /*0x442abd*/
    return ctx;                                          /*0x442ac4*/
}

// ---------------------------------------------------------------------------
// 0x5e3d2c — VIBE_Script_ReadSkipValue
// ---------------------------------------------------------------------------
i32 ReadSkipValue(int stream) {
    i32 v2 = 0;                                          /*0x5e3d31*/
    return g_hooks->readDword(stream, reinterpret_cast<u8*>(&v2)); /*0x5e3d3b*/
}

// ---------------------------------------------------------------------------
// 0x445cfc — VIBE_Script_RunByHandle
// ---------------------------------------------------------------------------
i32 RunByHandle(const char* name) {
    u8* v3 = g_hooks->loadFromDir(name);                 /*0x445cfe*/
    if (v3) g_hooks->runMain(v3);                        /*0x445d1e*/
    if (v3) {
        // dword_62E8DC = *((dword*)v3 + 32)  -> record +128 (the handle word).
        i32 r = *reinterpret_cast<i32*>(v3 + 128);       /*0x445d13*/
        ScriptTables().runResult = r;
        return r;                                        /*0x445d18*/
    }
    ScriptTables().runResult = -1;                       /*0x445d25*/
    return -1;                                           /*0x445d2f*/
}

// 0x445d38 — VIBE_Script_GetRunByHandlePtr.
i32 (*GetRunByHandlePtr())(const char*) { return &RunByHandle; } /*0x445d3d*/

// ---------------------------------------------------------------------------
// 0x4ba284 — VIBE_Script_FindActiveByHandle (reuses real FindByHandle sibling).
// ---------------------------------------------------------------------------
u8* FindActiveByHandle(u8* ctxTableBase, i32 handle) {
    u8* result;
    while (true) {                                       /*0x4ba299*/
        result = FindByHandle(ctxTableBase, handle);     /*0x4ba299*/
        if (!result || (result[kScRunFlags] & 1) == 0)   /*0x4ba2a9*/
            break;
        g_hooks->runFrameLoop(0, handle);                /*0x4ba2b3*/
    }
    return result;                                       /*0x4ba2ad*/
}

// ---------------------------------------------------------------------------
// 0x43d0bc — VIBE_Script_LoadRunAndStoreResult
// ---------------------------------------------------------------------------
u8* LoadRunAndStoreResult(u8* subObj) {
    // result = LoadFromScriptDir(subObj + 144).
    u8* result = g_hooks->loadFromDir(reinterpret_cast<const char*>(subObj + 144)); /*0x43d0c5*/
    if (result) {                                        /*0x43d0ce*/
        // RunWithArgs(result, 1, *(subObj+384)) — one int arg.
        i32 arg = *reinterpret_cast<i32*>(subObj + 384);
        i32 runRec[2] = {0, 0};
        RunWithArgs(result, 1, &arg, runRec);            /*0x43d0de*/
        // result = *(result + 380); subObj[+132] = result.
        u8* res = *reinterpret_cast<u8**>(result + 380); /*0x43d0e3*/
        *reinterpret_cast<u8**>(subObj + 132) = res;     /*0x43d0ec*/
        result = res;
    }
    return result;                                       /*0x43d0d2*/
}

// ---------------------------------------------------------------------------
// 0x487098 — VIBE_Script_RunWaitLoop (reuses real FindByHandle sibling).
// ---------------------------------------------------------------------------
void RunWaitLoop(u8* ctxTableBase, i32 handle, WaitLoopGuards& g,
                 void (*resetMouse)()) {
    if (g.noLoopGuard) return;                           /*0x4870a4*/
    // while ( RunFrameLoop(...) && !abort && !esc ) { if (!FindByHandle) return; }
    while (g_hooks->runFrameLoop(0, handle) && !g.abortFlag && !g.escFlag) { /*0x4870df*/
        if (!FindByHandle(ctxTableBase, handle)) return; /*0x4870ea*/
    }
    u8* ctx = FindByHandle(ctxTableBase, handle);        /*0x4870c2*/
    if (ctx) g_hooks->finishCtx(ctx);                    /*0x4870cb*/
    if (resetMouse) resetMouse();                        /*0x4870d0*/
}

// ---------------------------------------------------------------------------
// 0x445288 — VIBE_Script_ShutdownEngine
// ---------------------------------------------------------------------------
i32 ShutdownEngine() {
    ScriptEngineTables& t = ScriptTables();
    if (t.ctxTable) {
        for (int i = 0; i != kScriptContextStride * kScriptContextCount; i += kScriptContextStride) { /*0x44528e*/
            if (t.ctxTable[i + kScInUse])                /*0x445297*/
                g_hooks->destroyCtx(t.ctxTable + i);     /*0x44534f*/
        }
    }
    if (t.cmdTable) {
        for (int j = 0; j != kCommandStride * kCommandCapacity; j += kCommandStride) /*0x4452ae*/
            t.cmdTable[j] = 0;                           /*0x4452b5*/
    }
    if (t.eventToks) {                                   /*0x4452cc*/
        g_hooks->freeDebug(t.eventToks);                 /*0x4452d2*/
        t.eventToks = nullptr;                           /*0x4452d7*/
    }
    if (t.cmdTable) {                                    /*0x4452e5*/
        g_hooks->freeDebug(t.cmdTable);                  /*0x4452eb*/
        t.cmdTable = nullptr;                            /*0x4452f0*/
    }
    if (t.scratchB0) {                                   /*0x4452fe*/
        g_hooks->freeDebug(t.scratchB0);                 /*0x445304*/
        t.scratchB0 = nullptr;                           /*0x445309*/
    }
    i32 result = 0;
    if (t.scratchB4) {                                   /*0x445316*/
        g_hooks->freeDebug(t.scratchB4);                 /*0x44531a*/
        t.scratchB4 = nullptr;                           /*0x44531f*/
    }
    if (t.logRing) {                                     /*0x44532d*/
        g_hooks->freeDebug(t.logRing);                   /*0x445333*/
        t.logRing = nullptr;                             /*0x445338*/
    }
    if (t.ctxTable) {                                    /*0x445346*/
        g_hooks->freeDebug(t.ctxTable);                  /*0x44535d*/
        t.ctxTable = nullptr;                            /*0x445362*/
    }
    return result;                                       /*0x445348*/
}

// ---------------------------------------------------------------------------
// 0x44250c — VIBE_Script_ProcessStringLiteral
// Mirrors the two terminator scans verbatim (each advances by 1 and NULs the
// cursor if the byte runs out before the terminator).
// ---------------------------------------------------------------------------
const char* ProcessStringLiteral(u8* ctx, const char* src, char* dst) {
    // Scan to the closing '"' (34). v5/v7 = position of '"' (or null).
    const char* v5 = src;                                /*0x44251b*/
    while (*v5 != 34) {                                  /*0x442521*/
        if (*v5) {                                       /*0x44251d*/
            char v6 = *++v5;                             /*0x442528*/
            if (*v5 == 34) break;                        /*0x44252c*/
            ++v5;                                        /*0x44252e*/
            if (v6) continue;                            /*0x442531*/
        }
        v5 = nullptr;                                    /*0x442533*/
        break;
    }
    const char* v7 = v5;                                 /*0x442535*/
    const char* v12 = v5;                                /*0x442539*/

    // Scan to the ';' (59). v8 = position of ';' (or null).
    const char* v8 = src;                                /*0x44253c*/
    while (*v8 != 59) {                                  /*0x442542*/
        if (*v8) {                                       /*0x44253e*/
            char v9 = *++v8;                             /*0x442549*/
            if (*v8 == 59) break;                        /*0x44254d*/
            ++v8;                                        /*0x44254f*/
            if (v9) continue;                            /*0x442552*/
        }
        v8 = nullptr;                                    /*0x442554*/
        break;
    }

    if (v7 && v8 < v7) {                                 /*0x44255c*/
        g_hooks->reportError(ctx, 0, "evt_ProcessString: Missing \""); /*0x442567*/
        return nullptr;                                  /*0x44256c*/
    }
    int v11 = static_cast<int>(v12 - src);               /*0x44257b*/
    g_hooks->strNCopyPad(dst, src, v11);                 /*0x442581*/
    if (dst) dst[v11] = 0;                               /*0x442589*/
    return v12 + 1;                                      /*0x44258d*/
}

// ---------------------------------------------------------------------------
// 0x4413d0 — VIBE_Script_ParseDeclaration
// v11[0] is the token class; the lexed token payload (the symbol name / slot
// id) lands at v12 (the 4-byte word right after the class byte in the original's
// scratch, i.e. out[4..]).  We model the lexer scratch as `out` (>=64 bytes):
// out[0] = class, out+4 = the i32 slot/value, out+8.. = the name string.
// ---------------------------------------------------------------------------
i32 ParseDeclaration(u8* ctx) {
    const ScriptImport4Hooks& h = *g_hooks;
    // a1[38] is the source cursor (offset 152 == kScCursor).
    const char* cursor = *reinterpret_cast<const char**>(ctx + kScCursor);

    u8 out[80] = {};
    h.nextToken(cursor, out);                            /*0x4413eb*/
    u8 cls = out[0];
    i32 v12word = *reinterpret_cast<i32*>(out + 4);      // (dword)v12

    int v3 = 1;                                          /*0x4413e6*/ // default init
    (void)v12word;
    // if ( cls && (cls != 2 || *(v12+40)) )  -> error path.  The original tests
    // *(_DWORD*)(v12 + 40): a nonzero "already-bound" marker on the looked-up
    // symbol record.  We carry that marker in out[8] as the bound flag.
    bool boundDup = (cls == 2) && (out[8] != 0);
    if (cls && (cls != 2 || boundDup)) {                 /*0x441489*/
        const char* msg = (cls == 2)
            ? "Syntax error: Propably duplicate symbol..."   /*0x441499*/
            : "Syntax error: unknown";                       /*0x4414cd*/
        h.reportError(ctx, *reinterpret_cast<u32*>(ctx + kScCursor), msg); /*0x4414a6*/
        h.finishCtx(ctx);                                /*0x4414ad*/
        return 0;                                        /*0x4414b2*/
    }

    char name[48] = {};
    // The name source is v12+1 (UTF-16 in the symbol record) when cls!=0, else
    // &v12 (the inline token value bytes).  In both cases a 2-byte-stride copy.
    const char* src = (cls != 0)
        ? reinterpret_cast<const char*>(out + 4 + 1)     /*0x4414c7*/
        : reinterpret_cast<const char*>(out + 4);        /*0x441409*/
    CopyUtf16(name, src);                                /*0x441412..0x441428*/

    h.nextToken(*reinterpret_cast<const char**>(ctx + kScCursor), out); /*0x441435*/
    // `= <int>` : a class-1 sub-code 27 ('=') followed by a class-5 literal.
    if (out[0] == 1 && static_cast<u8>(*reinterpret_cast<i32*>(out + 4)) == 27) { /*0x441446*/
        h.nextToken(*reinterpret_cast<const char**>(ctx + kScCursor), out); /*0x441452*/
        if (out[0] == 5)                                 /*0x44145c*/
            v3 = *reinterpret_cast<i32*>(out + 4);       /*0x44145e*/
    }
    return h.declareLocal(ctx, 0, name, v3);             /*0x44146b*/
}

// ---------------------------------------------------------------------------
// 0x443084 — VIBE_Script_HandleExitKeyword
// ctx + 2472 (kScScopeBase) holds the current scope-frame pointer; its first
// dword is the symbol-name string compared against "exit".  Scope frames are a
// 144-byte array based at ctx + 168, indices 0..14 (the topmost is index 15 /
// ctx+2160); each frame's +0 (i.e. +168 in ctx terms) is its "active" marker and
// +4 its saved source cursor.
// ---------------------------------------------------------------------------
i32 HandleExitKeyword(u8* ctx) {
    const ScriptImport4Hooks& h = *g_hooks;
    // result = StrCmp("exit", **(ctx+2472));  (scope frame's first symbol name).
    u8* scope = *reinterpret_cast<u8**>(ctx + kScScopeBase);
    const char* sym = scope ? *reinterpret_cast<const char**>(scope) : "";
    int result = h.strCmp("exit", sym);                  /*0x443097*/
    // jz loc_443153: when the scope symbol IS "exit" (StrCmp == 0) this is a
    // no-op that returns 0; the frame pop runs only when the names DIFFER.
    if (result == 0) return result;                      /*0x44309e -> 0x443153*/

    int v3 = 15;                                         /*0x4430aa*/
    // if ( !*(ctx + 2328) ) scan downward for an active frame (+168).
    if (*reinterpret_cast<i32*>(ctx + 2328) == 0) {      /*0x4430af*/
        // v4 = ctx + 2160 ; loop: --v3; v4 -= 144; while v3>=0 && !*(v4+168).
        do {                                             /*0x4430cc*/
            --v3;                                        /*0x4430bb*/
        } while (v3 >= 0 &&
                 *reinterpret_cast<i32*>(ctx + 168 + 144 * v3) == 0);
    }

    if (v3 < 0) {                                         /*0x4430d0*/
        return 0;                                         /*0x443158*/
    } else if (v3 < 1) {                                  /*0x4430d9*/
        // pop the OUTERMOST frame (v3 == 0): clear the 32-byte locals window.
        u8* sc = *reinterpret_cast<u8**>(ctx + kScScopeBase);
        for (int i = 0; i != 32; i += 4)                 /*0x44315f*/
            *reinterpret_cast<i32*>(sc + i + 12) = 0;
        *reinterpret_cast<i32*>(ctx + 2488) = *reinterpret_cast<i32*>(sc + 12); /*0x443180*/
        ctx[kScRunFlags] &= ~1u;                          /*0x443186*/
        int v9 = 144 * v3;                                /*0x443196*/
        *reinterpret_cast<u8**>(ctx + kScScopeBase) = nullptr; /*0x443199*/
        *reinterpret_cast<i32*>(ctx + v9 + 168) = 0;      /*0x4431a3*/
        *reinterpret_cast<i32*>(ctx + v9 + 172) = 0;      /*0x4431ae*/
        return 0;                                         /*0x4431b9*/
    } else {
        // pop frame v3, jump to the parent (v3-1).
        u8* sc = *reinterpret_cast<u8**>(ctx + kScScopeBase);
        for (int j = 0; j != 32; j += 4)                  /*0x4430df*/
            *reinterpret_cast<i32*>(sc + j + 12) = 0;
        *reinterpret_cast<i32*>(ctx + 2488) = *reinterpret_cast<i32*>(sc + 12); /*0x443103*/
        u8* v6 = ctx + 144 * (v3 - 1) + 168;              /*0x44311b*/
        *reinterpret_cast<u8**>(ctx + kScScopeBase) = v6; /*0x44311d*/
        *reinterpret_cast<i32*>(ctx + kScCursor) = *reinterpret_cast<i32*>(v6 + 4); /*0x443126*/
        int v7 = 144 * v3;                                /*0x443135*/
        *reinterpret_cast<i32*>(ctx + v7 + 168) = 0;      /*0x443138*/
        *reinterpret_cast<i32*>(ctx + v7 + 172) = 0;      /*0x443143*/
        return 1;                                         /*0x44314e*/
    }
}

// ---------------------------------------------------------------------------
// 0x443c88 — VIBE_Script_ParseAndRunCall
// ---------------------------------------------------------------------------
u8 ParseAndRunCall(u8* ctx, const char* argLine, int sep) {
    const ScriptImport4Hooks& h = *g_hooks;
    char v19[256] = {};
    // Copy the (UTF-16) argument line into v19 verbatim.
    CopyUtf16(v19, argLine);                              /*0x443cb8..0x443cce*/

    char v18[1024] = {};   // up-to-16 tokens, 64-byte stride.
    int v7 = 0;            // token count
    char* tok = h.strtok(v19, sep);                      /*0x443cda*/
    while (tok) {                                         /*0x443ce1..0x443d1b*/
        ++v7;                                            /*0x443ced*/
        CopyUtf16(&v18[64 * (v7 - 1)], tok);             /*0x443cef..0x443d05*/
        tok = h.strtok(nullptr, sep);                    /*0x443d14*/
    }

    i32 v20[8] = {};       // bound argument dwords
    for (int i = 0; i < v7; ++i) {                       /*0x443d24..0x443d57*/
        const char* field = &v18[64 * i];
        // first char is a digit (ctype 0x20 on (c+1)) -> ParseInt, else keep ptr.
        if (CtypeDigit(static_cast<unsigned char>(field[0]))) /*0x443d3e*/
            v20[i] = h.parseInt(field);                  /*0x443d42*/
        else
            v20[i] = static_cast<i32>(reinterpret_cast<std::intptr_t>(field)); /*0x443d7d*/
    }

    switch (v7) {                                        /*0x443d5e*/
        case 0:  h.runMain(ctx); return 0;               /*0x443d6c*/
        case 1: case 2: case 3: case 4:
        case 5: case 6: case 7:
            return static_cast<u8>(RunWithArgs(ctx, v7, v20,
                                               ScriptTables().scratchB0
                                                 ? reinterpret_cast<i32*>(ScriptTables().scratchB0)
                                                 : v20));
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// 0x443a90 — VIBE_Script_RunWithArgs
// ---------------------------------------------------------------------------
i32 RunWithArgs(u8* ctx, int argc, const i32* firstArgs, i32* runRecord,
                char* strScratch) {
    const ScriptImport4Hooks& h = *g_hooks;
    if (!ctx) return 0;                                  /*0x443a9d*/
    int result = h.compileBlock(ctx);                    /*0x443aab*/
    if (!result) return result;                          /*0x443aa1*/

    u8* fn = h.lookupFunction(ctx, "main");              /*0x443abd*/
    if (!fn) {                                            /*0x443ac2*/
        h.reportError(ctx, 0,
            "evt_RunScript:No entrypoint(main) found in script..."); /*0x443b5f*/
        return 0;                                         /*0x443b64*/
    }
    // *(fn + 36) is the declared arg count.
    if (*reinterpret_cast<u8*>(fn + 36) != static_cast<u8>(argc)) { /*0x443ad0*/
        h.reportError(ctx, 0, "evt_RunScript:Wrong parameter count..."); /*0x443b79*/
        h.finishCtx(ctx);                                /*0x443b82*/
        return 0;                                         /*0x443b87*/
    }

    // Bind args by per-arg type byte *(fn + 37 + i).
    int v6 = 0;          // dword-buffer offset (stride 4)
    int slot = 0;        // string-scratch slot (stride 96 chars)
    for (int v15 = 0; v15 < static_cast<int>(*reinterpret_cast<u8*>(fn + 36)); ++v15) { /*0x443b06*/
        u8 type = *reinterpret_cast<u8*>(fn + 37 + v15); /*0x443b0c*/
        i32 raw = firstArgs ? firstArgs[v15] : 0;
        switch (type) {                                  /*0x443b15*/
            case 6: { // copy a UTF-16 string into the scratch ring (96 stride).
                if (strScratch) {
                    const char* s = reinterpret_cast<const char*>(
                        static_cast<std::intptr_t>(raw));
                    if (s) CopyUtf16(strScratch + 96 * slot, s); /*0x443b21..0x443b37*/
                }
                ++slot;
                break;
            }
            case 1: // dword
                if (runRecord) runRecord[v6 / 4] = raw;  /*0x443ba1*/
                break;
            case 2: // byte (zero-extended)
                if (runRecord) runRecord[v6 / 4] =
                    static_cast<u8>(raw);                /*0x443bb6*/
                break;
        }
        v6 += 4;                                         /*0x443b43*/
    }

    h.enterFunction(ctx, fn);                            /*0x443bc4*/
    ctx[kScRunFlags] |= 1;                               /*0x443bcd*/
    // scope base = ctx + 168.
    *reinterpret_cast<u8**>(ctx + kScScopeBase) = ctx + 168; /*0x443bda*/

    // Log-ring entry: sprintf("Run script: %s", ctx) into logRing[132*idx + 4],
    // clear logRing[132*idx + 0] = 0, advance the ring index (wrap when > 30).
    // gilde.exe 0x443bf0..0x443c3c — dword_767950 ring, dword_62E8C0 cursor.
    // (Reproduced guarded on a non-null ring; the original writes unconditionally
    //  into the engine-allocated ring.)
    ScriptEngineTables& T = ScriptTables();
    if (T.logRing) {
        u8* ring = static_cast<u8*>(T.logRing);
        u8* rec = ring + 132 * T.logRingIdx;
        std::snprintf(reinterpret_cast<char*>(rec + 4), 128, "Run script: %s",
                      reinterpret_cast<const char*>(ctx));               /*0x443bf0*/
        *reinterpret_cast<i32*>(rec) = 0;                                /*0x443c1a*/
    }
    ++T.logRingIdx;                                                      /*0x443c2c*/
    if (T.logRingIdx > 30) T.logRingIdx = 0;                            /*0x443c32*/

    // ctx + 2576 (kScSceneSlot) = dword_649D60 (current scene id).
    *reinterpret_cast<i32*>(ctx + kScSceneSlot) = ScriptEngine().sceneId; /*0x443c45*/
    // ctx + 132 (owner) = dword_62E8D4 (ownerId).
    *reinterpret_cast<i32*>(ctx + kScOwner) = ScriptEngine().ownerId; /*0x443c50*/
    return 1;                                            /*0x443c56*/
}

} // namespace guild::sim
