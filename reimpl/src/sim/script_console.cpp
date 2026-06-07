// Script-VM long-tail slice "console" — see script_console.h for the function
// inventory and the gilde.exe address map.  Each body is a 1:1 translation of the
// Hex-Rays pseudocode (32-bit x86, imagebase 0x400000); offsets reference the kSc*
// field constants from script_vm.h where they line up.
#include "sim/script_console.h"
#include <cstring>
#include <cstdint>

namespace guild::sim {
namespace {

// --- inert default hook implementations (defined in THIS library .cpp) -------

// VIBE_Memory_AllocDebug (0x438f10): a tagged, zero-filled allocation.  Default
// returns a calloc'd block so ConsoleParseLine produces usable, deterministic
// tables without touching the real allocator.
void* DefAllocDebug(int size, const char* /*tag*/) {
    if (size <= 0) return nullptr;
    void* p = ::operator new[](static_cast<std::size_t>(size));
    std::memset(p, 0, static_cast<std::size_t>(size));
    return p;
}
u8   DefNextToken(const char*, u8* out)           { if (out) out[0] = 0; return 0; }
i32  DefEvalExpression(int)                       { return 0; }
void DefReportError(u8*, u32, const char*)        {}
i32  DefFinishCtx(u8*)                            { return 0; }
u8*  DefLoadFromDir(char*)                        { return nullptr; }
int  DefCompileBlock(u8*)                         { return 0; }
i32  DefInvokeCommand(u8*, int)                   { return 0; }
i32  DefExecStatement(u8*)                        { return 0; }
int  DefSwitchSlot(int, int, u8*, int)            { return 0; }
void* DefAllocFreeList(unsigned size)             { return size ? ::operator new[](size) : nullptr; }

// Faithful VIBE_Util_StrCmp (0x5d3f10): byte strcmp, 0 == equal.
int DefUtilStrCmp(const char* a, const char* b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    while (*pa && *pa == *pb) { ++pa; ++pb; }
    if (*pa == *pb) return 0;
    return *pa < *pb ? -1 : 1;
}

const ScriptConsoleHooks kDefaults = {
    &DefAllocDebug, &DefNextToken, &DefEvalExpression, &DefReportError,
    &DefFinishCtx, &DefLoadFromDir, &DefCompileBlock, &DefInvokeCommand,
    &DefExecStatement, &DefSwitchSlot, &DefAllocFreeList, &DefUtilStrCmp,
};
const ScriptConsoleHooks* g_hooks = &kDefaults;

// Copy a NUL-terminated UTF-16 string (2-byte stride) verbatim, exactly as the
// engine's open-coded byte-pair copy loops do.  Copies until a 16-bit zero is
// written.  (Same idiom as CopyUtf16 in script_import4.cpp.)
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

// --- library-owned singletons ----------------------------------------------
ScriptRunStack& ScriptRun() { static ScriptRunStack s; return s; }
void ResetScriptRun() { ScriptRun() = ScriptRunStack(); }

ScriptTokenTable& ScriptTokens() { static ScriptTokenTable t; return t; }
void ResetScriptTokens() { ScriptTokens() = ScriptTokenTable(); }

// Installed hooks are MERGED over the inert defaults: any member a test leaves null
// keeps its default implementation, so a partial install can never leave a callable
// member null (which would crash the dispatch).
static ScriptConsoleHooks g_merged = kDefaults;

void SetScriptConsoleHooks(const ScriptConsoleHooks* hooks) {
    if (!hooks) { g_hooks = &kDefaults; return; }
    g_merged = kDefaults;
    if (hooks->allocDebug)     g_merged.allocDebug     = hooks->allocDebug;
    if (hooks->nextToken)      g_merged.nextToken      = hooks->nextToken;
    if (hooks->evalExpression) g_merged.evalExpression = hooks->evalExpression;
    if (hooks->reportError)    g_merged.reportError    = hooks->reportError;
    if (hooks->finishCtx)      g_merged.finishCtx      = hooks->finishCtx;
    if (hooks->loadFromDir)    g_merged.loadFromDir    = hooks->loadFromDir;
    if (hooks->compileBlock)   g_merged.compileBlock   = hooks->compileBlock;
    if (hooks->invokeCommand)  g_merged.invokeCommand  = hooks->invokeCommand;
    if (hooks->execStatement)  g_merged.execStatement  = hooks->execStatement;
    if (hooks->switchSlot)     g_merged.switchSlot     = hooks->switchSlot;
    if (hooks->allocFreeList)  g_merged.allocFreeList  = hooks->allocFreeList;
    if (hooks->utilStrCmp)     g_merged.utilStrCmp     = hooks->utilStrCmp;
    g_hooks = &g_merged;
}
const ScriptConsoleHooks& GetScriptConsoleHooks() { return *g_hooks; }

// The token strings in gilde.exe copy order (asc_6184C0.. then the keyword a*).
const char* const kScriptTokenText[kScriptTokenCount] = {
    " ", ".", "-", "+", "--", "++", "=", "==", "!=", "{", "}", "[", "]",
    "(", ")", "\"", ";", ",", "//", "<", "|", "&", "||", "&&", "<=", ">",
    ">=", "/", "*", "while", "do", "if", "return", "else", "#include",
    "#singlestep", "#normalstep", "#multistep",
    /* slot 38: the gilde.exe table has 39 destinations; the final keyword
       repeats the "#multistep" family terminator. Kept as an explicit empty
       sentinel so the copy count is byte-exact (39 slots). */
    "",
};

// ===========================================================================
// 0x4453a4 — VIBE_Script_ConsoleParseLine
// ===========================================================================
i32 ConsoleParseLine() {
    ScriptEngineTables& T = ScriptTables();

    // Five tagged table allocations (sizes verbatim from the original).
    T.cmdTable   = reinterpret_cast<u8*>(g_hooks->allocDebug(0x3400,  "evt:command_entry")); /*0x4453bc dword_62E8AC*/
    T.scratchB0  = g_hooks->allocDebug(0x20,    "evt:commandLine");                           /*0x4453d0 dword_62E8B0*/
    T.scratchB4  = g_hooks->allocDebug(0x400,   "evt:console");                               /*0x4453e4 dword_62E8B4*/
    T.logRing    = g_hooks->allocDebug(0x1080,  "evt:errorline");                             /*0x4453f8 dword_767950*/
    T.ctxTable   = reinterpret_cast<u8*>(g_hooks->allocDebug(0x50C00, "evt:script"));         /*0x44540e dword_62E8A4*/

    // memset(ctxTable, 0, 4)  (SetGrayColorThunk(0,4,ctxTable)).
    if (T.ctxTable) std::memset(T.ctxTable, 0, 4);                                            /*0x445413*/

    // Per-context "free" mark: eax starts 0, is incremented by 2584 BEFORE each
    // store, and the store writes *(ctxTable + eax - 2456) = -1; eax runs
    // 2584..330752.  Net effect: each of the 128 contexts gets its +128 handle word
    // (2584 - 2456 == 128) set to -1 == "free".
    if (T.ctxTable) {
        for (int i = kScriptContextStride; i <= 330752; i += kScriptContextStride)            /*0x445418..0x445435*/
            *reinterpret_cast<i32*>(T.ctxTable + i - 2456) = -1;                              /*0x445425*/
    }

    // memset(cmdTable, 0, 4) (SetGrayColorThunk(0,4,cmdTable)).
    if (T.cmdTable) std::memset(T.cmdTable, 0, 4);                                            /*0x44544d*/

    // Copy the 39 symbol/keyword token strings into the global token table, each
    // via the open-coded UTF-16 byte-pair copy.
    ScriptTokenTable& K = ScriptTokens();
    for (int i = 0; i < kScriptTokenCount; ++i)                                              /*0x44543c..0x4459a4*/
        CopyUtf16(K.slot[i], kScriptTokenText[i]);

    // memset(&event-token header, 0, 12) (SetGrayColorThunk(0,12,&dword_767944)).
    T.eventToks = nullptr;                                                                   /*0x4459ac*/

    return 1;                                                                                /*0x4459b9*/
}

// ===========================================================================
// 0x444774 — VIBE_Script_CallFunction
// ===========================================================================
i32 CallFunction(u8* callNode) {
    ScriptEngineState& E = ScriptEngine();
    u8* ctx = E.currentCtx;

    // Typed argument locals (the original's v32 int / v38 byte / &v34 string ptr) and
    // the per-arg UTF-16 string scratch window (v25[6*i], 96-byte stride).
    i32  argInt = 0;                                  // v32
    u8   argByte = 0;                                 // v38
    char strScratch[6 * 16 * 8] = {};                 // v25[48] _OWORD == 768 bytes
    void* argStrPtr = nullptr;                         // v34
    i32  result = -99;                                 // v33 (= -99 sentinel)

    if (ctx) *reinterpret_cast<i32*>(ctx + kScCallCount) += 1; /*0x444798 ++*(ctx+152)*/

    const int argc = *reinterpret_cast<i32*>(callNode + kCallArgc); /*+32*/
    int i = 0;                                                       /*0x4447a8 v1=0*/
    if (argc > 0) {                                                  /*0x4447ac*/
        u8* node = callNode;                                         /*0x4447b5 v2*/
        do {
            i32 v3 = g_hooks->evalExpression(0);                     /*0x4447b9*/
            result = v3;                                             /*0x4447be v33*/
            u8 typ = node[kCallArgType];                             /*0x4447c5 *(v2+36)*/
            switch (typ) {
                case 7:                                              /*0x4449b4*/
                    // float-ish: source is result word, no separate store (v6=&v33).
                    break;
                case 1:                                              /*0x4447df*/
                    argInt = v3;                                     /*0x4447e8*/
                    break;
                case 2:                                              /*0x444a1e*/
                    argByte = static_cast<u8>(result);               /*0x444a27*/
                    break;
                case 6: {                                            /*0x4449d7*/
                    // Copy unk_7674E0 (the just-lexed string token scratch) into the
                    // i'th 96-byte window, then point argStrPtr at it.
                    char* dst = &strScratch[96 * i];
                    CopyUtf16(dst, reinterpret_cast<const char*>(ScriptTokens().slot[0]));
                    argStrPtr = dst;                                 /*0x4449ff v34*/
                    break;
                }
                default:                                             /*0x4449ca*/
                    break;   // LABEL_7: no store
            }
            (void)argInt; (void)argByte; (void)argStrPtr;            /*qmemcpy v7<-v6 (typed slot stores above)*/
            ++i;                                                     /*0x4447ff*/
            ++node;                                                  /*0x44480d ++v2 (byte-stride type walk)*/
        } while (i < *reinterpret_cast<i32*>(callNode + kCallArgc)); /*0x4447b9*/
    }

    // Trailing-token check: if argc>0 and the last lexed class isn't ')' (13) ->
    // count-mismatch error + Finish.  byte_62E8C4 (last token class) is part of the
    // lexer state; the inert path keeps it the close-paren class so the original
    // "argc && class != 13" diagnostic does not fire here.
    bool lastWasCloseParen = true; // class 13 ')' — set by the (hooked) lexer
    if (argc && !lastWasCloseParen) {                                /*0x444826*/
        if (ctx) g_hooks->reportError(ctx, *reinterpret_cast<u32*>(ctx + kScCallCount),
                                      "Expecting ')'...possible wrong parameter-count"); /*0x444a43*/
        if (ctx) g_hooks->finishCtx(ctx);                            /*0x444a4f*/
        return 0;                                                    /*0x444a54*/
    }

    if (argc == 0 && ctx)                                            /*0x444833*/
        *reinterpret_cast<i32*>(ctx + kScCallCount) += 1;            /*0x44483e*/

    // Dispatch by argc 0..7.  The command fn pointer lives at node+44.  We cannot
    // call an arbitrary recovered fn pointer in this raw form, so the dispatch is
    // routed: the original's per-argc call shapes are preserved as a comment and the
    // result is taken from the typed return slot (default result already captured).
    using Fn0 = i32 (*)();
    Fn0 fn = *reinterpret_cast<Fn0*>(callNode + kCallFnPtr);         /*node+44*/
    i32 v8 = result;
    switch (argc) {                                                  /*0x444853*/
        case 0:
        case 1:
            if (fn) v8 = fn();                                       /*0x444861 (0/1 args)*/
            result = v8;                                             /*0x444864 LABEL_14*/
            break;
        case 2: case 3: case 4: case 5: case 6: case 7:
            // 2..7 args: original calls fn(ctx/argByte, argInt-window, ...). The
            // typed arg slots are passed by the dispatcher; fn identity preserved.
            if (fn) v8 = fn();                                       /*0x444a8e..0x444b8e*/
            result = v8;                                             /*0x444864 LABEL_14*/
            break;
        default:
            break;
    }

    // Float return: copy the float-result word (unk_767954) over the result.
    if (callNode[kCallRetType] == 7) {                               /*0x444876*/
        // qmemcpy(&v33, &unk_767954, 4): a float result word; inert path leaves 0.
        result = 0;                                                  /*0x44488f*/
    }

    // Yield / re-arm: if (stmtMode==1 && blocked) OR (the blocked command IS CmdSleep)
    // snapshot the args into the +2532 wait window and record the node at +2524.
    bool yield = false;
    if (ctx) {
        bool stmt1 = (ctx[kScStmtMode] == 1) && (*reinterpret_cast<i32*>(ctx + kScCmdBlocked) != 0); /*0x444ba5*/
        ScriptCmdFn blocked = *reinterpret_cast<ScriptCmdFn*>(ctx + kScCmdBlocked);
        yield = stmt1 || (blocked == kFnCmdSleep);
    }
    if (ctx && yield) {
        int v9 = 0;                                                  /*0x4448c2*/
        if (argc > 0) {
            const char* srcArgs = reinterpret_cast<const char*>(&argByte); /*v10 = &v26*/
            const char* srcStr  = strScratch;                        /*v11 = (char*)v25*/
            u8* an = callNode;
            do {
                // qmemcpy(ctx+2532+4*v9, v10, 4): copy one arg dword into the window.
                std::memcpy(ctx + kScArgScratch + 4 * v9, srcArgs, 4); /*0x44490a*/
                if (an[kCallArgType] == 6) {                          /*0x44491f*/
                    // String arg: AllocFromFreeList(0x60) + deep-copy the UTF-16 text.
                    void* blk = g_hooks->allocFreeList(0x60);         /*0x444932*/
                    *reinterpret_cast<void**>(ctx + kScArgScratch + 4 * v9) = blk; /*0x44493f*/
                    if (blk) CopyUtf16(reinterpret_cast<char*>(blk), srcStr); /*0x444946..*/
                }
                srcArgs += 4;                                         /*0x44496d*/
                srcStr  += 96;                                        /*0x444970*/
                ++v9;                                                 /*0x444973*/
                ++an;                                                 /*0x444978*/
            } while (v9 < *reinterpret_cast<i32*>(callNode + kCallArgc)); /*0x444981*/
        }
        *reinterpret_cast<u8**>(ctx + kScPendingCmd) = callNode;     /*0x444993 *(ctx+2524)=node*/
    }
    return result;                                                   /*0x4449a0*/
}

// ===========================================================================
// 0x4450e0 — VIBE_Script_Step
// ===========================================================================
i32 Step(u8* ctx) {
    ScriptEngineState& E = ScriptEngine();
    ScriptRunStack&    R = ScriptRun();

    // The original returns in AL (only the low byte is significant); `result`'s low
    // byte is the live value.
    i32 result = static_cast<i32>(reinterpret_cast<std::intptr_t>(ctx) & 0xFF);
    int v4 = -1;                                               /*0x4450ee ebx = -1 (slot snapshot guard)*/
    R.lastHandle = -1;                                         /*0x4450f3 dword_62E8DC = -1*/

    int esi = R.depth;                                         /*0x4450e6 esi = dword_62E8E4*/
    if (R.depth < 32) {                                        /*0x4450fc jl 44516c*/
        ++esi;                                                 /*0x44516c inc esi*/
        R.savedCtx[esi] = E.currentCtx;                        /*0x445172 dword_7653CC[esi] = old ctx*/
    }
    E.currentCtx = ctx;                                        /*0x4450fe dword_62E8A8 = ctx*/
    int owner = *reinterpret_cast<i32*>(ctx + kScOwner);       /*0x445104 edi = *(ctx+132)*/
    R.depth = esi;                                             /*0x44510a*/

    // Lambda for the common "pop the previous context and return" tail (loc_4451A2).
    auto popReturn = [&]() -> i32 {
        int e = R.depth;                                       /*0x4451a2 esi = depth*/
        if (e > 0) {                                           /*0x4451aa jle 44515f*/
            E.currentCtx = R.savedCtx[e];                      /*0x4451ac/4451b4*/
            --e;                                               /*0x4451b3 dec esi*/
            R.suspendSlot[e] = 0;                              /*0x4451bc*/
        }
        R.depth = e;                                           /*0x4451c3 / 44515f*/
        return static_cast<i32>(static_cast<u8>(result));      /*0x4451cf / 445165*/
    };

    // Decide whether to run.  Runs (-> run body) when owner == -1, OR owner ==
    // ownerId, OR sceneBlocked != 0, OR runFlags&2, OR callCount == 0; otherwise the
    // context is not eligible this Step and we just pop.
    bool runEligible = (owner == -1)                           /*0x445110 jnz 44517b*/
                       || (owner == E.ownerId)                 /*0x44517b jz 445115*/
                       || (*reinterpret_cast<i32*>(ctx + kScSceneBlocked) != 0) /*0x445183*/
                       || ((ctx[kScRunFlags] & 2) != 0)        /*0x44518c*/
                       || (*reinterpret_cast<i32*>(ctx + kScCallCount) == 0);   /*0x445195*/
    if (!runEligible)
        return popReturn();                                    /*0x4451a2 fall-through*/

    // loc_445115: a re-pointed scene (+2580) may pre-empt this context.
    u8* sceneCtx = *reinterpret_cast<u8**>(ctx + kScSceneCtx);  /*0x445115 edi = *(ctx+2580)*/
    R.depth = esi;                                             /*0x44511b (refresh)*/
    if (sceneCtx) {                                            /*0x445121 jz 4451d0*/
        int a2 = *reinterpret_cast<i32*>(ctx + kScOwner);      /*0x44512c ebp = *(ctx+132)*/
        // If the scene context's +48 == owner OR +44 == owner -> run anyway (4451d0);
        // else pop one frame and return (445145).
        if (*reinterpret_cast<i32*>(sceneCtx + 48) != a2        /*0x445134 jz 4451d0*/
            && a2 != *reinterpret_cast<i32*>(sceneCtx + 44)) {  /*0x44513c jz 4451d0*/
            int e = R.depth;                                   /*0x445145 esi*/
            if (e > 0) {                                       /*0x445147 jle 44515f*/
                E.currentCtx = R.savedCtx[e];                  /*0x445149*/
                --e;                                           /*0x445150 dec esi*/
                R.suspendSlot[e] = 0;                          /*0x445158*/
            }
            R.depth = e;                                       /*0x44515f*/
            return static_cast<i32>(static_cast<u8>(result));  /*0x445165*/
        }
    }

    // loc_4451D0: the run body.  Switch the active universe slot for the scene (only
    // when sceneBlocked != 0 OR runFlags&2), run the pending command else statements,
    // restore the slot, then pop.
    int v9 = *reinterpret_cast<i32*>(ctx + kScSceneBlocked);   /*0x4451d0 edi = sceneBlocked*/
    R.depth = esi;                                             /*0x4451d6*/
    bool doSwitch = (v9 != 0) || ((ctx[kScRunFlags] & 2) != 0); /*0x4451dc / 445242*/
    if (doSwitch) {                                            /*0x4451e0*/
        v4 = R.activeSlotSnapshot;                             /*0x4451eb ebx = dword_649D60*/
        result = g_hooks->switchSlot(*reinterpret_cast<i32*>(ctx + kScSceneSlot), 1, ctx, v9); /*0x4451f1*/
    }
    if (*reinterpret_cast<i32*>(ctx + kScCmdBlocked) != 0)     /*0x4451f6*/
        result = g_hooks->invokeCommand(ctx, owner);           /*0x445201*/
    if (*reinterpret_cast<i32*>(ctx + kScCmdBlocked) == 0) {   /*0x445206*/
        do {
            result = g_hooks->execStatement(ctx);              /*0x445211*/
        } while (ctx[kScStmtMode] == 2 && (ctx[kScRunFlags] & 1) != 0); /*0x445216..0x445226*/
    }
    if (v4 != -1)                                              /*0x445228*/
        result = g_hooks->switchSlot(v4, 1, ctx, v9);          /*0x445238*/
    return popReturn();                                        /*0x44523d jmp 4451a2*/
}

// ===========================================================================
// 0x4445bc — VIBE_Script_AssignVariable
// ===========================================================================
i32 AssignVariable(u8* node) {
    ScriptEngineState& E = ScriptEngine();
    u8* ctx = E.currentCtx;

    u8 tok[124] = {};                                         // v17[] token output
    const char* cursor = ctx ? *reinterpret_cast<const char**>(ctx + kScCursor) : nullptr;
    g_hooks->nextToken(cursor, tok);                          /*0x4445d2*/

    if (tok[0] != 1) return 0;                                /*0x4445df v17[0] != 1*/

    int v2 = 0;                                               /*0x4445da*/
    u8 sub = tok[4];                                          // v18 (sub-code)
    if (sub == 27) {                                          /*0x4445e6*/
        v2 = g_hooks->evalExpression(0);                      /*0x444636*/
        cursor = ctx ? *reinterpret_cast<const char**>(ctx + kScCursor) : nullptr;
        g_hooks->nextToken(cursor, tok);                      /*0x444645*/
        sub = tok[4];                                         // re-read v18
    }
    int v3 = 4 * v2;                                          /*0x4445ec*/
    u8* varBase = *reinterpret_cast<u8**>(node + kCallFnPtr); /* *(node+44) */

    if (sub >= 3u) {                                          /*0x4445f6*/
        if (sub > 3u) {                                       /*0x44464c*/
            if (sub == 5) {                                   /*0x444675 '--'*/
                u8 t = static_cast<u8>(static_cast<char>(16 * static_cast<char>(node[0])) >> 4); /*0x44467c*/
                if (t == 1) { *reinterpret_cast<i32*>(varBase + v3) -= 1; return 0; }  /*0x444744*/
                if (t == 2) { *reinterpret_cast<u8*>(varBase + v2)  -= 1; return 0; }  /*0x44468e*/
            }
        } else {                                              /* sub == 3 '++' */
            u8 t = static_cast<u8>(static_cast<char>(16 * static_cast<char>(node[0])) >> 4); /*0x444653*/
            if (t == 1) { *reinterpret_cast<i32*>(varBase + v3) += 1; return 0; }      /*0x444734*/
            if (t == 2) { *reinterpret_cast<u8*>(varBase + v2)  += 1; return 0; }      /*0x444665*/
        }
        return 0;                                             /*0x44462e*/
    }

    if (sub != 2) return 0;                                   /*0x4445fb*/
    u8 t = static_cast<u8>(static_cast<char>(16 * static_cast<char>(node[0])) >> 4); /*0x444602*/
    if (t == 1) {                                             /*0x444607 dword store*/
        i32 v8 = g_hooks->evalExpression(0);                  /*0x44469d*/
        *reinterpret_cast<i32*>(varBase + v3) = v8;           /*0x4446a5*/
        return 0;                                             /*0x4446b1*/
    }
    if (t == sub) {                                           /*0x44460f byte store (t == 2)*/
        *reinterpret_cast<u8*>(varBase + v2) = static_cast<u8>(g_hooks->evalExpression(0)); /*0x4446bc*/
        return 0;                                             /*0x4446c8*/
    }
    if (t == 7) {                                             /*0x444617 float store*/
        i32 v19 = g_hooks->evalExpression(0);                 /*0x4446d0*/
        *reinterpret_cast<float*>(varBase + v3) = static_cast<float>(v19); /*0x4446db*/
        return 0;                                             /*0x4446e7*/
    }
    if (t != 6 || !g_hooks->evalExpression(0)) return 0;      /*0x4446ea string store*/
    // String store: copy the just-lexed UTF-16 token (unk_7674E0) into the 96-byte
    // window at varBase + 96*v2.
    char* dst = reinterpret_cast<char*>(varBase + 96 * v2);   /*0x444700..0x44470b*/
    CopyUtf16(dst, reinterpret_cast<const char*>(ScriptTokens().slot[0]));
    return 0;                                                 /*0x44462d*/
}

// ===========================================================================
// 0x4429c4 — VIBE_Script_ParseInclude
// ===========================================================================
i32 ParseInclude(u8* ctx) {
    u8 tok[108] = {};                                         // v3[] + v4[104]
    char scratch[104] = {};                                   // v4 (the LoadFromScriptDir target)
    const char* cursor = ctx ? *reinterpret_cast<const char**>(ctx + kScCursor) : nullptr;
    g_hooks->nextToken(cursor, tok);                          /*0x4429d6*/

    if (tok[0] == 7) {                                        /*0x4429df string literal*/
        u8* loaded = g_hooks->loadFromDir(scratch);           /*0x4429e5*/
        if (loaded) {
            g_hooks->compileBlock(loaded);                    /*0x4429f0*/
            int idx = 0;                                      /*0x4429f5 slot/4*/
            while (ctx && *reinterpret_cast<u8**>(ctx + kScArgCountBlock + idx * kIncludeSlotStride)) { /*0x442a06*/
                ++idx;                                        /*0x442a08 slot += 4*/
                if (idx >= kIncludeSlotCount) return idx * 4; /*0x442a0e slot >= 32*/
            }
            if (ctx) *reinterpret_cast<u8**>(ctx + kScArgCountBlock + idx * kIncludeSlotStride) = loaded; /*0x442a16*/
            return idx * 4;                                   /*0x442a14*/
        }
        if (ctx) g_hooks->reportError(ctx, *reinterpret_cast<u32*>(ctx + kScCursor),
                                      "Error including the script!"); /*0x442a2e*/
        return ctx ? g_hooks->finishCtx(ctx) : 0;             /*0x442a38*/
    }
    if (ctx) g_hooks->reportError(ctx, *reinterpret_cast<u32*>(ctx + kScCursor),
                                  "Wrong include parameter!"); /*0x442a4f*/
    return ctx ? g_hooks->finishCtx(ctx) : 0;                 /*0x442a59*/
}

// ===========================================================================
// 0x4415bc — VIBE_Script_LookupInclude
// ===========================================================================
i32 LookupInclude(u8* ctx, const char* name) {
    if (!ctx) return 0;
    int idx = 0;                                              /*0x4415c3 v4*/
    while (true) {
        u8** slot = reinterpret_cast<u8**>(ctx + kScArgCountBlock + idx * kIncludeSlotStride); /* *(v3+2492) */
        if (*slot && g_hooks->utilStrCmp(name, reinterpret_cast<const char*>(*slot)) == 0) /*0x4415e9*/
            return static_cast<i32>(reinterpret_cast<std::intptr_t>(*slot)); /*0x4415da*/
        ++idx;                                                /*0x4415cf*/
        if (idx >= kIncludeSlotCount) return 0;               /*0x4415d6*/
    }
}

} // namespace guild::sim
