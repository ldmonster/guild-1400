// ===========================================================================
// Die Gilde 1400 — Script-VM front-end. 1:1 reconstruction. See script_vm.h.
//
// Each function is a direct translation of the Hex-Rays pseudocode at the cited
// address, preserving the original's control flow, constants, field indices and
// side effects. Address markers /*0x...*/ pin each step to the image.
//
// Pointer-width accommodation: the original is a 32-bit process addressing the
// per-context record as a dword array (`a1[idx]`). To run testably on a 64-bit
// host (heap pointers don't fit in 4 bytes), every context dword *index* is
// scaled to pointer width via Dw() (header), and the separately-allocated
// var/func records widen their one pointer field to a clean pointer-aligned slot
// (header kVar_StorageP / kFunc_BodyP). The arithmetic — including the image's
// frame-window index overlaps — is reproduced exactly, only at pointer stride.
// This is the value-semantics accommodation documented in guild/common/types.h.
// ===========================================================================
#include "script/script_vm.h"
#include "sim/script_import3.h"   // guild::sim::ImportCommand / EventTokens
#include "sim/script_import2.h"   // ScriptCmdFn (== void*)

#include <cstring>
#include <cstdlib>
#include <cstdint>

namespace guild::script {

static constexpr int kPtr = static_cast<int>(sizeof(std::intptr_t));

u32 g_scopeOverride = 0;          // dword_62E8C8

namespace {
u8*                  g_currentCtx = nullptr;   // dword_62E8A8
const ScriptVmHooks* g_hooks      = nullptr;
ScriptVmHooks        g_inert;

u8 InertNextToken(u8*, TokenResult* out) { out->cls = 12; return 12; }
i32 InertEval(u32) { return 0; }
int InertReport(u8*, u32, const char*) { return 0; }
int InertFinish(u8*) { return 1; }
void* InertAlloc(std::size_t bytes, const char*) { return std::malloc(bytes ? bytes : 1); }
void  InertFree(void* p) { std::free(p); }
int  InertStrCmp(const char* a, const char* b) { return std::strcmp(a, b); }

const ScriptVmHooks& H() { return g_hooks ? *g_hooks : g_inert; }

// The unrolled 2-byte-stride NUL-terminated byte copy the originals inline.
void CopyNameZ(char* dst, const char* src) {
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

struct InertInit {
    InertInit() {
        g_inert.nextToken = &InertNextToken;
        g_inert.evaluateExpression = &InertEval;
        g_inert.reportError = &InertReport;
        g_inert.finish = &InertFinish;
        g_inert.allocDebug = &InertAlloc;
        g_inert.freeDebug = &InertFree;
        g_inert.strCmp = &InertStrCmp;
    }
} g_inertInit;
} // namespace

void SetScriptVmHooks(const ScriptVmHooks* hooks) { g_hooks = hooks; }
const ScriptVmHooks& GetScriptVmHooks() { return H(); }
u8* CurrentContext() { return g_currentCtx; }
void SetCurrentContext(u8* ctx) { g_currentCtx = ctx; }

static u8 NextToken(u8* ctx, TokenResult* out) { return H().nextToken(ctx, out); }
static i32 EvaluateExpression(u32 s) { return H().evaluateExpression(s); }
static int ReportError(u8* c, u32 cur, const char* m) { return H().reportError(c, cur, m); }
static int Finish(u8* c) { return H().finish(c); }
static void* AllocDebug(std::size_t b, const char* t) { return H().allocDebug(b, t); }
static void FreeDebug(void* p) { H().freeDebug(p); }
static int StrCmp(const char* a, const char* b) { return H().strCmp(a, b); }

static inline u8* AsPtr(std::intptr_t v) { return reinterpret_cast<u8*>(v); }
static inline std::intptr_t AsInt(const void* p) { return reinterpret_cast<std::intptr_t>(p); }
static u32 CursorVal(u8* c) { return static_cast<u32>(Dw(c, kIdx_Cursor)); }

// ===========================================================================
// 0x442c90 — VIBE_Script_DefineVariable(ctx, type, elemCount, sizeHint)
// ===========================================================================
u8* DefineVariable(u8* ctx, u8 type, i32 elemCount, i32 sizeHint) {
    i32 elemSize = sizeHint;
    switch (type) {                                  /*0x442c9a*/
        case 1:  elemSize = 4;  break;
        case 2:  elemSize = 1;  break;               /*0x442d73*/
        case 6:  elemSize = 96; break;               /*0x442d7d*/
        case 7:  elemSize = 4;  break;               /*0x442cb3*/
        default: break;
    }
    u8* varBase = AsPtr(Dw(ctx, kIdx_VarBase));      // *(ctx+136)
    u8* rec = varBase + (std::size_t)kVarRecordStride * Dw(ctx, kIdx_VarCount);
    u8 hi = static_cast<u8>(RecByte(rec, kVar_Nibble) & 0xF0);  /*0x442cd6*/
    RecByte(rec, kVar_Nibble) = static_cast<u8>((type & 0x0F) | hi); /*0x442cde*/
    std::size_t bytes = static_cast<std::size_t>(elemSize) * (elemCount > 0 ? elemCount : 0);
    void* block = AllocDebug(bytes, "evt:var");      /*0x442ced*/
    RecPtr(rec, kVar_StorageP) = AsInt(block);       /*0x442d0c*/
    if (block && bytes) std::memset(block, 0, bytes);/*0x442d2e SetGray(0,size,block)*/
    RecI32(rec, kVar_ElemCount) = elemCount;         /*0x442d4b*/
    ++Dw(ctx, kIdx_VarCount);                        /*0x442d6f*/
    return rec;
}

// ===========================================================================
// 0x441788 — VIBE_Script_ParseTypeKeyword(lexeme)
// ===========================================================================
int ParseTypeKeyword(const char* lexeme) {
    char buf[260];
    CopyNameZ(buf, lexeme);                          /*0x441798*/
    char* p = buf;                                   /*0x4417b1*/
    char* cut = nullptr;
    while (*p != ' ') {                              /*0x4417b7*/
        if (*p) { ++p; char c = *p; if (*p == ' ') { cut = p; break; } ++p; if (c) continue; }
        cut = nullptr; break;
    }
    if (*p == ' ') cut = p;
    if (!cut) {                                      /*0x4417cd*/
        p = buf;                                     /*0x4417f7*/
        while (*p != ')') {                          /*0x4417fd*/
            if (*p) { ++p; char c = *p; if (*p == ')') { cut = p; break; } ++p; if (c) continue; }
            cut = nullptr; break;
        }
        if (*p == ')') cut = p;
    }
    if (cut) *cut = 0;                               /*0x4417d3*/
    if (!StrCmp("int", buf))    return 1;            /*0x4417dd*/
    if (!StrCmp("float", buf))  return 7;            /*0x44181a*/
    if (!StrCmp("void", buf))   return 5;            /*0x441839*/
    if (!StrCmp("string", buf)) return 6;            /*0x441858*/
    if (!StrCmp("{", buf))      return 8;            /*0x441877*/
    if (!StrCmp("}", buf))      return 9;            /*0x441896*/
    if (StrCmp("byte", buf) && StrCmp("char", buf))  /*0x4418c5*/
        return 0;
    return 2;                                        /*0x4417f4*/
}

// ===========================================================================
// 0x4414d4 — VIBE_Script_LookupVariable(ctx, name)
// ===========================================================================
u8* LookupVariable(u8* ctx, const char* name) {
    u8* scope = g_scopeOverride ? AsPtr(static_cast<std::intptr_t>(g_scopeOverride)) : ctx;
    // Active call frame's local table (frame dword index 4..11).
    std::intptr_t frame = Dw(ctx, kIdx_ActiveFrame);               /*0x4414ed*/
    if (frame) {
        for (int slot = 0; slot < kMaxLocalsPerFrame; ++slot) {    /*0x441500*/
            std::intptr_t rec = Dw(AsPtr(frame), 4 + slot);        // frame +16 (dword idx 4)
            if (rec) {
                if (!StrCmp(name, reinterpret_cast<const char*>(rec + kVar_Name)))  /*0x44150a*/
                    return AsPtr(rec);
            }
            frame = Dw(ctx, kIdx_ActiveFrame);                     /*0x441527*/
            if (!frame) break;
        }
    }
    // The context's own var table.                                /*0x441529*/
    i32 varCount = static_cast<i32>(Dw(scope, kIdx_VarCount));
    if (varCount > 0) {
        u8* varBase = AsPtr(Dw(scope, kIdx_VarBase));
        int idx = 0, voff = 0;
        while (StrCmp(name, reinterpret_cast<const char*>(varBase + voff + kVar_Name))) { /*0x441549*/
            ++idx; voff += kVarRecordStride;
            if (idx >= varCount) goto globals;                     /*0x441557*/
        }
        return varBase + voff;                                     /*0x4415a8*/
    }
globals:                                                           /*0x441559*/
    {
        guild::sim::EventTokenTable& T = guild::sim::EventTokens(); // dword_767944/767948
        if (static_cast<int>(T.count) <= 0) return nullptr;        /*0x441563*/
        int count = 0;
        for (int i = 0; ; i += guild::sim::kEventTokenStride) {     /*0x441565*/
            if (!StrCmp(name, reinterpret_cast<const char*>(T.base + i + 1)))
                return T.base + i;                                 /*0x4415b1*/
            if (++count >= static_cast<int>(T.count)) return nullptr; /*0x441585*/
        }
    }
}

// ===========================================================================
// 0x4415f8 — VIBE_Script_LookupFunction(ctx, name)
// ===========================================================================
u8* LookupFunction(u8* ctx, const char* name) {
    u8* scope = g_scopeOverride ? AsPtr(static_cast<std::intptr_t>(g_scopeOverride)) : ctx;
    if (!Dw(ctx, kIdx_FuncBase)) return nullptr;                  /*0x44160b*/
    i32 funcCount = static_cast<i32>(Dw(scope, kIdx_FuncCount));
    if (funcCount <= 0) return nullptr;                           /*0x44161e*/
    u8* funcBase = AsPtr(Dw(scope, kIdx_FuncBase));
    int idx = 0, foff = 0;
    while (StrCmp(name, reinterpret_cast<const char*>(funcBase + foff + kFunc_Name))) { /*0x441633*/
        ++idx; foff += kFuncRecordStride;
        if (idx >= funcCount) return nullptr;                     /*0x441644*/
    }
    return funcBase + foff;                                       /*0x441648*/
}

// ===========================================================================
// 0x441280 — VIBE_Script_DeclareLocal(ctx, type, name, elemCount)
// ===========================================================================
u8* DeclareLocal(u8* ctx, u8 type, const char* name, i32 elemCount) {
    std::intptr_t frame = Dw(ctx, kIdx_ActiveFrame);     // a1[618]
    int slot = 0;
    u8* rec = nullptr;                                    // v16
    while (Dw(AsPtr(frame), 4 + slot)) {                  /*0x4412a2 (frame +16 + 4*slot)*/
        ++slot;
        if (slot >= kMaxLocalsPerFrame) goto check;       /*0x44137a*/
    }
    {
        i32 elemSize = 0;                                 // v15
        if (type == 1)      elemSize = 4;                 /*LABEL_4*/
        else if (type == 2) elemSize = 1;                 /*0x441387*/
        else if (type == 7) elemSize = 4;                 /*0x441396*/
        else if (type == 6) elemSize = 96;                /*0x4413a5*/

        rec = AsPtr(Dw(ctx, kIdx_LocalCursor));           // a1[622]
        u8 hi = static_cast<u8>(RecByte(rec, kVar_Nibble) & 0xF0); /*0x4412d3*/
        RecByte(rec, kVar_Nibble) = static_cast<u8>((type & 0x0F) | hi); /*0x4412da*/
        RecI32(rec, kVar_Scope) = 1;                      /*0x4412e1 scope marker*/
        RecI32(rec, kVar_ElemCount) = elemCount;          /*0x4412ea*/
        RecPtr(rec, kVar_StorageP) = AsInt(rec + kVar_InlineSt); /*0x4412ee in-line storage*/
        CopyNameZ(reinterpret_cast<char*>(rec + kVar_Name), name); /*0x4412f2*/

        std::intptr_t newCursor = static_cast<std::intptr_t>(elemCount) * elemSize + 48
                                + Dw(ctx, kIdx_LocalCursor);       /*0x44131b*/
        std::intptr_t limit = Dw(ctx, kIdx_LocalBase) + kLocalStackLimit; /*0x441323*/
        Dw(ctx, kIdx_LocalCursor) = newCursor;            /*0x441328*/
        if (limit < newCursor) {                          /*0x441330*/
            ReportError(ctx, CursorVal(ctx), "Stack Overflow!");
            Finish(ctx);                                  /*0x441346*/
        }
        Dw(AsPtr(frame), 4 + slot) = AsInt(rec);          /*0x44135e (frame +16 + 4*slot)*/
    }
check:                                                    /*LABEL_11*/
    if (rec) return rec;                                  /*0x441367*/
    ReportError(ctx, CursorVal(ctx) - 1, "Too many locals!"); /*0x4413bf*/
    return nullptr;
}

// ===========================================================================
// 0x4416c4 — VIBE_Script_SkipBraceBlock(mode)  [on the current-context cursor]
// ===========================================================================
int SkipBraceBlock(u8 mode) {
    u8* ctx = g_currentCtx;                               // v1 = dword_62E8A8
    int depth = 0;                                        // v3
    if (mode == 1) {                                      /*0x4416d7*/
        while (true) {                                    /*0x4416e7*/
            u8* p = AsPtr(Dw(ctx, kIdx_Cursor));
            // bound: srcLen + srcBase <= cursor -> stop.
            if (static_cast<std::uintptr_t>(Dw(ctx, kIdx_SrcLen)) + static_cast<std::uintptr_t>(Dw(ctx, kIdx_SrcBase))
                  <= reinterpret_cast<std::uintptr_t>(p))  /*0x4416f1*/
                break;
            if (*p == '{') {                              /*0x4416ff*/
                ++depth;
            } else if (*p == '}' && --depth <= 0) {       /*0x441712*/
                Dw(ctx, kIdx_Cursor) = AsInt(p + 1);      /*0x44171c*/
                g_currentCtx = ctx;                       /*0x441722*/
                return 1;
            }
            ++Dw(ctx, kIdx_Cursor);                       /*0x441702*/
        }
    } else if (mode == 2) {                               /*0x441732*/
        while (true) {                                    /*0x441734*/
            u8* p = AsPtr(Dw(ctx, kIdx_Cursor));
            if (reinterpret_cast<std::uintptr_t>(p) < static_cast<std::uintptr_t>(Dw(ctx, kIdx_SrcBase))) /*0x441744*/
                break;
            if (*p == '{') {                              /*0x44174d*/
                int n = depth - 1;
                if (n <= 0) { g_currentCtx = ctx; return 1; } /*0x441752*/
                depth = n - 1;                            /*0x441754*/
            } else if (*p == '}') {                       /*0x441772*/
                ++depth;
            }
            --Dw(ctx, kIdx_Cursor);                       /*0x441755*/
        }
    }
    g_currentCtx = ctx;                                   /*0x441779*/
    return 0;
}

// ===========================================================================
// 0x442b34 — VIBE_Script_ParseSymbolName(ctx, lexBuf)
// ===========================================================================
i32 ParseSymbolName(u8* ctx, u8* lexBuf) {
    std::intptr_t lex = AsInt(lexBuf);                    // v15 = a2
    // Latest func record: stride*ctx[36] + ctx[35]; clear param count (+36).
    u8* funcRec = AsPtr(Dw(ctx, kIdx_FuncBase)
                        + (std::intptr_t)kFuncRecordStride * Dw(ctx, kIdx_FuncCount));
    RecByte(funcRec, kFunc_ParamCount) = 0;              /*0x442b6a*/
    TokenResult tok = TokenResult{};                     /*0x442b6e SetGray(0,100,&tok)*/
    char* namesBase = reinterpret_cast<char*>(funcRec + kFunc_ParamNames); // v4 = v3+45

    while (true) {                                        /*0x442b83*/
        NextToken(g_currentCtx, &tok);
        if (tok.cls != 8) goto term;                      /*0x442b8c (LABEL_10)*/
        {
            int tcode = (tok.value >> 24) & 0xFF;         // v6 /*0x442b92*/
            if ((tcode < 1 || tcode > 2) && tcode != 6) goto term; /*0x442c39*/
        }
        funcRec[RecByte(funcRec, kFunc_ParamCount) + kFunc_ParamTypes]
            = static_cast<u8>((tok.value >> 24) & 0xFF);  /*0x442bac*/
        NextToken(g_currentCtx, &tok);                    /*0x442bbd*/
        if (tok.cls) break;                               /*0x442bc7 (error)*/
        CopyNameZ(namesBase + 32 * RecByte(funcRec, kFunc_ParamCount), tok.text); /*0x442bd5*/
        ++RecByte(funcRec, kFunc_ParamCount);             /*0x442bf2*/
term:                                                     /*LABEL_10 0x442bf5*/
        NextToken(g_currentCtx, &tok);
        if (tok.cls == 1 && ((tok.value >> 24) & 0xFF) == 13) { /*0x442c16*/
            ++Dw(ctx, kIdx_FuncCount);                    /*0x442c27*/
            return static_cast<i32>(lex);
        }
    }
    if (tok.cls == 2)                                     /*0x442c44*/
        ReportError(ctx, CursorVal(ctx), "Dublicate symbolname!");
    else
        ReportError(ctx, CursorVal(ctx), "Missing operand!");
    Finish(ctx);                                          /*0x442c5a*/
    return 0;
}

// ===========================================================================
// 0x442d88 — VIBE_Script_ParseDeclaration(ctx@<eax>, type@<edx>)
//
// The caller (VIBE_Script_CompileBlock @0x443692: `mov edx,[esp+var_83]; sar
// edx,18h`) passes the just-parsed type keyword's code — token.value>>24 — in
// edx. That `a1`/edx value is forwarded verbatim as the DefineVariable `type`
// argument in every declaration path, so the variable is sized/typed per the
// keyword (int/byte/string/float), not always int.
// ===========================================================================
i32 ParseDeclaration(u8* ctx, u8 type) {
    TokenResult tok;

    NextToken(g_currentCtx, &tok);                        /*0x442da4*/
    if (tok.cls) return 1;                                /*0x442dad*/
    char name[52];
    CopyNameZ(name, tok.text);                            /*0x442dc4*/

    NextToken(g_currentCtx, &tok);                        /*0x442def*/
    if (tok.cls != 1) return 1;                           /*0x442df8*/
    int sub = tok.value & 0xFF;

    if (sub == 10) {                                      // scalar decl /*0x442e01*/
        if (!LookupVariable(ctx, name)) {                 /*0x442eea*/
            u8* rec = DefineVariable(ctx, type, 1, 1);    /*0x442f09*/
            CopyNameZ(reinterpret_cast<char*>(rec + kVar_Name), name); /*0x442f0d*/
        }
        return 1;
    }
    if (sub == 2) {                                       // '=' init /*0x442e0a*/
        u8* rec = nullptr;
        if (!LookupVariable(ctx, name)) {                 /*0x442f3d*/
            rec = DefineVariable(ctx, type, 1, 1);        /*0x442f58*/
            CopyNameZ(reinterpret_cast<char*>(rec + kVar_Name), name); /*0x442f5e*/
        }
        NextToken(g_currentCtx, &tok);                    /*0x442f84*/
        if (tok.cls == 5 && rec)                          /*0x442f8d*/
            *reinterpret_cast<i32*>(AsPtr(RecPtr(rec, kVar_StorageP))) = tok.value; /*0x442f9a*/
        return 1;
    }
    if (sub != 12) {                                      // not a func sig
        if (sub == 27) {                                  // '[' array decl /*0x442e1c*/
            NextToken(g_currentCtx, &tok);                /*0x442e2b*/
            i32 size;
            if (tok.cls == 5) size = tok.value;           /*0x442e34*/
            else { size = 1; ReportError(ctx, CursorVal(ctx), "Illegal array size..."); } /*0x443073*/
            if (!LookupVariable(ctx, name)) {             /*0x442e44*/
                u8* rec = DefineVariable(ctx, type, size, size); /*0x442e53*/
                CopyNameZ(reinterpret_cast<char*>(rec + kVar_Name), name); /*0x442e58*/
            }
            NextToken(g_currentCtx, &tok);                /*0x442e86*/
            if ((tok.value & 0xFF) != 28)                 /*0x442e90*/
                ReportError(ctx, CursorVal(ctx), "Missing ']'!");
            NextToken(g_currentCtx, &tok);                /*0x442eb1*/
            if ((tok.value & 0xFF) != 10) {               /*0x442ebb*/
                ReportError(ctx, CursorVal(ctx), "Expecting ';'");
                return 1;
            }
        }
        return 1;
    }

    // sub == 12: function signature.
    u8* bodyScan = g_currentCtx ? AsPtr(Dw(g_currentCtx, kIdx_Cursor)) : nullptr; // v29 /*0x442fb9*/
    i32 result = ParseSymbolName(ctx, reinterpret_cast<u8*>(&tok)); /*0x442fbf*/
    if (result) {
        u8* b = bodyScan;                                 // v31 /*0x442fce*/
        while (b && *b != '{') {                          /*0x442fd4*/
            if (*b) { char c = *++b; if (*b == '{') break; ++b; if (c) continue; }
            b = nullptr; break;                           /*0x442fe6*/
        }
        u8* funcRec = AsPtr(Dw(ctx, kIdx_FuncBase)
                            + (std::intptr_t)kFuncRecordStride * (Dw(ctx, kIdx_FuncCount) - 1));
        RecPtr(funcRec, kFunc_BodyP) = AsInt(b);          /*0x442fe8*/
        CopyNameZ(reinterpret_cast<char*>(funcRec + kFunc_Name), name); /*0x44302e*/
        if (g_currentCtx) Dw(g_currentCtx, kIdx_Cursor) = AsInt(bodyScan); /*0x44304f*/
        return 1;
    }
    return result;                                        /*0x442db4*/
}

// ===========================================================================
// 0x4431dc — VIBE_Script_EnterFunction(ctx, funcRec)
// ===========================================================================
int EnterFunction(u8* ctx, u8* funcRecPtr) {
    if (!funcRecPtr)                                                 /*0x4431f9*/
        return ReportError(ctx, CursorVal(ctx), "evt_EnterFunction: Unknown function..."); /*0x44321f*/

    // Find a free call-frame slot: scan frames (stride kCallFrameStride) while
    // frame-base occupancy (dword idx 42 from i, i steps by 36) is set.
    int depth = 0;                                                   // v2
    {
        u8* i = ctx;
        while (depth < kMaxCallFrames && Dw(i, kIdx_CallFrames)) {   /*0x4431fb i[42]*/
            ++depth;
            i += (std::size_t)kCallFrameStride * kPtr;               // i += 36 (dwords)
        }
    }
    if (depth >= kMaxCallFrames)                                     /*0x443233*/
        return ReportError(ctx, CursorVal(ctx), "evt_EnterFunction: Callstack overflow..."); /*0x4432f3*/

    u8 scratch[800];
    std::memset(scratch, 0, sizeof(scratch));
    u8* fr = funcRecPtr;

    if (depth <= 0) {
        // Top-level call: default arg cells (no live globals -> zero-filled).
        int n = RecByte(fr, kFunc_ParamCount);                     /*0x443440*/
        int doff = 0;
        for (int k = 0; k < n; ++k) {
            (void)RecByte(fr, kFunc_ParamTypes + k);               // param type
            *reinterpret_cast<i32*>(scratch + doff + 768) = 0;     /*0x44345b*/
            doff += 4;                                             /*0x443470*/
        }
    } else {
        // Nested call: evaluate each argument expression.
        ++Dw(ctx, kIdx_Cursor);              // ++ctx[38] (consume '(') /*0x443253*/
        int doff = 0, k = 0; u8 pcount = 0;
        for (;;) {
            pcount = RecByte(fr, kFunc_ParamCount);                /*0x443277*/
            if (k >= pcount) break;
            i32 ptype = RecByte(fr, kFunc_ParamTypes + k);
            i32 v = EvaluateExpression(0);                         /*0x44328b*/
            if (ptype == 6)
                CopyNameZ(reinterpret_cast<char*>(scratch + (std::size_t)k * 96),
                          reinterpret_cast<const char*>(static_cast<std::intptr_t>(v)));
            else
                *reinterpret_cast<i32*>(scratch + doff + 768) = v; /*0x44330b*/
            ++k; doff += 4;
        }
        if (!pcount) ++Dw(ctx, kIdx_Cursor);                       /*0x443316/0x44331f*/
        Dw(AsPtr(Dw(ctx, kIdx_ActiveFrame)), 1 /*frame+4*/) = Dw(ctx, kIdx_Cursor); /*0x44333f*/
    }

    // Push the new call frame at ctx dword index (42 + 36*depth). The image
    // computes the frame-window base v16 = ctx + 36*depth (dwords) and writes
    // funcRec at v16[42] and ctx[622] at v16[45] — i.e. at the active-frame
    // base (v16+42) itself: funcRec -> frame[0], ctx[622] -> frame[3]. (Disasm
    // 0x443364/0x443372/0x443381: [ecx+9A8h]=activeFrame; eax=v16=ctx+144*depth;
    // [eax+0A8h(=168=42dw)]=funcRec == activeFrame[0]; [eax+0B4h(=180=45dw)]=
    // ctx[622] == activeFrame[3].)
    u8* newActive = ctx + (std::size_t)(kIdx_CallFrames + kCallFrameStride * depth) * kPtr; /*0x443358/0x443364*/
    Dw(ctx, kIdx_ActiveFrame) = AsInt(newActive);
    Dw(newActive, 3) = Dw(ctx, kIdx_LocalCursor);                  /*0x443372 v16[45] == frame[3]*/
    Dw(newActive, 0) = AsInt(fr);                                  /*0x443381 v16[42] == frame[0]*/

    // Bind each parameter as a local; copy the evaluated value into storage.
    char* nameBase = reinterpret_cast<char*>(fr + kFunc_ParamNames); // v39
    int doff = 0, k = 0;
    u8* fcur = fr;                                                  // v38 (advances per param)
    for (;;) {
        if (RecByte(fr, kFunc_ParamCount) <= k) {                  /*0x4433bb*/
            Dw(ctx, kIdx_Cursor) = RecPtr(fr, kFunc_BodyP);        // cursor = body /*0x4433d2*/
            Dw(AsPtr(Dw(ctx, kIdx_ActiveFrame)), 1) = RecPtr(fr, kFunc_BodyP); /*0x4433e8 frame+4*/
            std::intptr_t active = Dw(ctx, kIdx_ActiveFrame);
            Dw(AsPtr(active), 2) = 0;                              /*0x4433f8 frame+8*/
            return static_cast<int>(active);
        }
        u8 ptype = RecByte(fcur, kFunc_ParamTypes);               // *(v38+34) HIBYTE == byte 37
        u8* rec = DeclareLocal(ctx, ptype, nameBase, 1);          /*0x4434cb*/
        if (!rec) break;                                          /*0x4434d2*/
        int kind = static_cast<int>(static_cast<signed char>(16 * RecByte(rec, kVar_Nibble)) >> 4); /*0x44356b*/
        void* store = AsPtr(RecPtr(rec, kVar_StorageP));
        switch (kind) {
            case 1: *reinterpret_cast<i32*>(store) = *reinterpret_cast<i32*>(scratch + doff + 768); break; /*0x44357c*/
            case 2: *reinterpret_cast<u8*>(store)  = scratch[doff + 768]; break;                          /*0x44358d*/
            case 6: CopyNameZ(reinterpret_cast<char*>(store),
                              reinterpret_cast<const char*>(scratch + (std::size_t)k * 96)); break;       /*0x4435a5*/
            case 7: *reinterpret_cast<float*>(store) =
                        static_cast<float>(*reinterpret_cast<i32*>(scratch + doff + 768)); break;         /*0x44359e*/
            default: break;
        }
        doff += 4; ++fcur; nameBase += 32; ++k;                    /*0x4434e7*/
    }
    ReportError(ctx, CursorVal(ctx), "Could not allocate symbol...out of memory!"); /*0x443548*/
    return Finish(ctx);                                           /*0x443224*/
}

// ===========================================================================
// 0x445a28 — VIBE_Script_DestroyContext(ctx)
// ===========================================================================
int DestroyContext(u8* ctx) {
    if (!ctx) return 0;                                            /*0x445a2e*/

    if (Dw(ctx, kIdx_VarBase)) {                                   /*0x445a38*/
        u8* varBase = AsPtr(Dw(ctx, kIdx_VarBase));
        int n = static_cast<int>(Dw(ctx, kIdx_VarCount));         /*0x445b15*/
        if (n > 0) {                                              /*0x445b1f*/
            int voff = 0, cnt = 0;
            do {
                u8* rec = varBase + voff;                         /*0x445b2d*/
                void* block = AsPtr(RecPtr(rec, kVar_StorageP));
                if (block) {                                      /*0x445b2f*/
                    FreeDebug(block);                             /*0x445b38*/
                    RecPtr(rec, kVar_StorageP) = 0;               /*0x445b43*/
                }
                ++cnt;
                n = static_cast<int>(Dw(ctx, kIdx_VarCount));     /*0x445b5a*/
                voff += kVarRecordStride;                         /*0x445b60*/
            } while (cnt < n);                                    /*0x445b65*/
        }
    }

    for (int k = 0; k < 8; ++k) {                                 /*0x445a45..0x445a68*/
        std::intptr_t child = Dw(ctx, kIdx_ChildCtx0 + k);
        if (child) {
            DestroyContext(AsPtr(child));                        /*0x445a54*/
            Dw(ctx, kIdx_ChildCtx0 + k) = 0;                    /*0x445a59*/
        }
    }

    if (Dw(ctx, kIdx_VarBase))  { FreeDebug(AsPtr(Dw(ctx, kIdx_VarBase)));  Dw(ctx, kIdx_VarBase)  = 0; } /*0x445a6a*/
    if (Dw(ctx, kIdx_FuncBase)) { FreeDebug(AsPtr(Dw(ctx, kIdx_FuncBase))); Dw(ctx, kIdx_FuncBase) = 0; } /*0x445a85*/
    if (Dw(ctx, kIdx_SrcBase))  { FreeDebug(AsPtr(Dw(ctx, kIdx_SrcBase)));  Dw(ctx, kIdx_SrcBase)  = 0; } /*0x445aa0*/
    if (Dw(ctx, kIdx_LineBase)) { FreeDebug(AsPtr(Dw(ctx, kIdx_LineBase))); Dw(ctx, kIdx_LineBase) = 0; } /*0x445abb*/
    if (Dw(ctx, kIdx_LocalBase)){ FreeDebug(AsPtr(Dw(ctx, kIdx_LocalBase)));Dw(ctx, kIdx_LocalBase)= 0; } /*0x445ad6*/

    std::memset(ctx, 0, (std::size_t)kContextDwords * kPtr);     /*0x445afa SetGray(0,2584,ctx)*/
    Dw(ctx, kIdx_State) = -1;                                    /*0x445b04*/
    return 1;
}

// ===========================================================================
// 0x43dfb0 — VIBE_Character_RegisterScriptCommands()
// ===========================================================================
namespace {
char g_charFnTokens[40];
inline guild::sim::ScriptCmdFn CharTok(int i) { return &g_charFnTokens[i]; }

// argType codes: 1=int, 6=string, 7=float (per the ImportCommand calls @0x43dfb0).
const u8 t_CreateChar[]            = {6,7,7,7};
const u8 t_CreateCharAtDummy[]     = {6,1,6};
const u8 t_KillChar[]              = {1};
const u8 t_WalkToDummy[]           = {1,1};
const u8 t_WalkToDummyRotate[]     = {1,1};
const u8 t_WalkToDummyVerified[]   = {1,1,1};
const u8 t_WalkToDummyRotateVer[]  = {1,1,1};
const u8 t_PlayAnimation[]         = {1,6,1};
const u8 t_PlayAnimationSound[]    = {1,6,1,1,6};
const u8 t_PlayAnimationScript[]   = {1,6,1,1,6};
const u8 t_PlayAnimationScriptInt[]= {1,6,1,1,6,1};
const u8 t_GetCharacterHandle[]    = {6};
const u8 t_TakeObject[]            = {1,6,1,1};
const u8 t_TakeObjectLeft[]        = {1,6,1,1};
const u8 t_TakeObjectScript[]      = {1,6,1,1,1,6};
const u8 t_GiveObject[]            = {1,6};
const u8 t_DropObject[]            = {1,6,1,1};
const u8 t_DropObjectLeft[]        = {1,6,1,1};
const u8 t_SitDown[]               = {1,6};
const u8 t_SitDownAtOnce[]         = {1,6};
const u8 t_GetUp[]                 = {1,6};
const u8 t_SetCharacterCamera[]    = {1,6};
const u8 t_MoveCharacterCamera[]   = {1,6,1};
const u8 t_PreloadAnimation[]      = {1,6,6,6,6};
const u8 t_PreloadSitMesh[]        = {1};
const u8 t_CharacterIdle[]         = {1};
const u8 t_StopCharacter[]         = {1};
const u8 t_KillCharacterAnims[]    = {1};
const u8 t_DummyBlocked[]          = {1,1};
const u8 t_LookAtCharacter[]       = {1,1};
const u8 t_LookAtObject[]          = {1,1};
const u8 t_SetCharacterToDummy[]   = {1,1};
const u8 t_CharacterSitting[]      = {1};
const u8 t_GetCharSubObject[]      = {1,6};
const u8 t_SetCharacterStepSample[]= {6};
const u8 t_SetLowPoly[]            = {1,1};
const u8 t_AttachObject[]          = {1,6,6};
const u8 t_SetCharacterTransp[]    = {1,1};

const CharCommandDesc g_charCmds[] = {
    {"CreateCharacter",            1, 4, t_CreateChar,             0},
    {"CreateCharacterAtDummy",     1, 3, t_CreateCharAtDummy,      1},
    {"KillCharacter",              5, 1, t_KillChar,               2},
    {"WalkToDummy",                5, 2, t_WalkToDummy,            3},
    {"WalkToDummyRotate",          5, 2, t_WalkToDummyRotate,      4},
    {"WalkToDummyVerified",        5, 3, t_WalkToDummyVerified,    5},
    {"WalkToDummyRotateVerified",  5, 3, t_WalkToDummyRotateVer,   6},
    {"PlayAnimation",              5, 3, t_PlayAnimation,          7},
    {"PlayAnimationSound",         5, 5, t_PlayAnimationSound,     8},
    {"PlayAnimationScript",        5, 5, t_PlayAnimationScript,    9},
    {"PlayAnimationScriptInt",     5, 6, t_PlayAnimationScriptInt, 10},
    {"GetCharacterHandle",         1, 1, t_GetCharacterHandle,     11},
    {"TakeObject",                 1, 4, t_TakeObject,             12},
    {"TakeObjectLeft",             1, 4, t_TakeObjectLeft,         13},
    {"TakeObjectScript",           1, 6, t_TakeObjectScript,       14},
    {"GiveObject",                 1, 2, t_GiveObject,             15},
    {"DropObject",                 1, 4, t_DropObject,             16},
    {"DropObjectLeft",             1, 4, t_DropObjectLeft,         17},
    {"SitDown",                    1, 2, t_SitDown,                18},
    {"SitDownAtOnce",              1, 2, t_SitDownAtOnce,          19},
    {"GetUp",                      1, 2, t_GetUp,                  20},
    {"SetCharacterCamera",         5, 2, t_SetCharacterCamera,     21},
    {"MoveCharacterCamera",        5, 3, t_MoveCharacterCamera,    22},
    {"PreloadAnimation",           5, 5, t_PreloadAnimation,       23},
    {"PreloadSitMesh",             5, 1, t_PreloadSitMesh,         24},
    {"CharacterIdle",              1, 1, t_CharacterIdle,          25},
    {"StopCharacter",              5, 1, t_StopCharacter,          26},
    {"KillCharacterAnimations",    5, 1, t_KillCharacterAnims,     27},
    {"DummyBlocked",               1, 2, t_DummyBlocked,           28},
    {"CharacterCount",             1, 0, nullptr,                  29},
    {"LookAtCharacter",            5, 2, t_LookAtCharacter,        30},
    {"LookAtObject",               5, 2, t_LookAtObject,           31},
    {"SetCharacterToDummy",        5, 2, t_SetCharacterToDummy,    32},
    {"CharacterSitting",           5, 1, t_CharacterSitting,       33},
    {"GetCharacterSubObjectHandle",5, 2, t_GetCharSubObject,       34},
    {"SetCharacterStepSample",     5, 1, t_SetCharacterStepSample, 35},
    {"SetLowPoly",                 5, 2, t_SetLowPoly,             36},
    {"AttachObject",               5, 3, t_AttachObject,           37},
    {"SetCharacterTransperancy",   5, 2, t_SetCharacterTransp,     38},
};
constexpr int kCharCmdCount = static_cast<int>(sizeof(g_charCmds) / sizeof(g_charCmds[0]));
} // namespace

const CharCommandDesc* CharCommandTable(int* count) {
    if (count) *count = kCharCmdCount;
    return g_charCmds;
}

i32 RegisterScriptCommands() {
    i32 r = 0;
    for (int i = 0; i < kCharCmdCount; ++i) {
        const CharCommandDesc& d = g_charCmds[i];
        r = guild::sim::ImportCommand(d.name, CharTok(d.tokenId), d.kind, d.argc, d.argTypes);
    }
    return r;
}

} // namespace guild::script
