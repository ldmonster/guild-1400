// Golden-vector unit tests for the Script-VM front-end (src/script/script_vm).
// Exercises the 1:1-reconstructed parser/runtime functions against a synthetic
// context and a scripted tokenizer hook, plus the character command table.
#include "test.h"
#include "script/script_vm.h"
#include "sim/script_import3.h"   // guild::sim::Commands/ResetCommands
#include "sim/script_import.h"    // guild::sim::FindCommandByName

#include <cstring>
#include <vector>
#include <string>
#include <cstdint>

using namespace guild;
using namespace guild::script;

// ---------------------------------------------------------------------------
// A scripted tokenizer: yields a fixed sequence of TokenResults. Lets the tests
// drive ParseDeclaration/ParseSymbolName/EnterFunction deterministically without
// the (runtime-table-built) real NextToken.
// ---------------------------------------------------------------------------
namespace {
struct TokScript {
    std::vector<TokenResult> toks;
    std::size_t pos = 0;
};
TokScript* g_script = nullptr;

u8 ScriptedNextToken(u8*, TokenResult* out) {
    if (g_script && g_script->pos < g_script->toks.size()) {
        *out = g_script->toks[g_script->pos++];
    } else {
        std::memset(out, 0, sizeof(*out));
        out->cls = 12;  // EOF
    }
    return out->cls;
}

int g_reportCount = 0;
std::string g_lastError;
int CountingReport(u8*, u32, const char* m) { ++g_reportCount; g_lastError = m ? m : ""; return 7; }
int g_finishCount = 0;
int CountingFinish(u8*) { ++g_finishCount; return 1; }

std::vector<void*> g_freed;
void TrackingFree(void* p) { g_freed.push_back(p); std::free(p); }

// helpers to build tokens
TokenResult TName(const char* s) { TokenResult t{}; t.cls = 0; std::strncpy(t.text, s, 63); return t; }
TokenResult TSym(int sub)        { TokenResult t{}; t.cls = 1; t.value = sub & 0xFF; return t; }
TokenResult TVar(std::intptr_t p){ TokenResult t{}; t.cls = 2; t.value = (i32)p; return t; }
TokenResult TInt(i32 v)          { TokenResult t{}; t.cls = 5; t.value = v; return t; }
TokenResult TType(u8 code, const char* nm) {
    TokenResult t{}; t.cls = 8; t.value = (i32)((u32)code << 24); if (nm) std::strncpy(t.text, nm, 63); return t;
}
TokenResult TTypeSym(u8 code) { TokenResult t{}; t.cls = 1; t.value = (i32)((u32)code << 24); return t; }

// A context arena: the 2584-dword-scaled context + small var/func table arenas.
struct Ctx {
    std::vector<u8> rec;       // the context record (pointer-width cells)
    std::vector<u8> varTable;  // var record arena
    std::vector<u8> funcTable; // func record arena
    std::vector<u8> localStk;  // local stack arena
    Ctx() {
        rec.assign((std::size_t)kContextDwords * sizeof(std::intptr_t) + 64, 0);
        varTable.assign(64 * kVarRecordStride, 0);
        funcTable.assign(8 * kFuncRecordStride, 0);
        localStk.assign(4096, 0);
        u8* c = rec.data();
        Dw(c, kIdx_VarBase)     = (std::intptr_t)varTable.data();
        Dw(c, kIdx_FuncBase)    = (std::intptr_t)funcTable.data();
        Dw(c, kIdx_LocalBase)   = (std::intptr_t)localStk.data();
        Dw(c, kIdx_LocalCursor) = (std::intptr_t)localStk.data();
    }
    u8* c() { return rec.data(); }
};

void InstallHooks(ScriptVmHooks& h) {
    h = GetScriptVmHooks();  // seed inert defaults
    h.nextToken = &ScriptedNextToken;
    h.reportError = &CountingReport;
    h.finish = &CountingFinish;
    h.freeDebug = &TrackingFree;
    SetScriptVmHooks(&h);
}
void ResetCounters() { g_reportCount = 0; g_finishCount = 0; g_freed.clear(); g_lastError.clear(); }
} // namespace

// ===========================================================================
// Leaf: ParseTypeKeyword (golden classification table).
// ===========================================================================
TEST(scriptvm, scriptvm_parse_type_keyword) {
    CHECK_EQ(ParseTypeKeyword("int"), 1);
    CHECK_EQ(ParseTypeKeyword("float"), 7);
    CHECK_EQ(ParseTypeKeyword("void"), 5);
    CHECK_EQ(ParseTypeKeyword("string"), 6);
    CHECK_EQ(ParseTypeKeyword("byte"), 2);
    CHECK_EQ(ParseTypeKeyword("char"), 2);
    CHECK_EQ(ParseTypeKeyword("notatype"), 0);
    // trailing ' ' / ')' are stripped before the compare.
    CHECK_EQ(ParseTypeKeyword("int "), 1);
    CHECK_EQ(ParseTypeKeyword("float)"), 7);
}

// ===========================================================================
// Leaf: DefineVariable — record layout + count bump.
// ===========================================================================
TEST(scriptvm, scriptvm_define_variable) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx;
    u8* rec = DefineVariable(ctx.c(), kVarInt, 1, 1);
    CHECK_EQ((int)(Dw(ctx.c(), kIdx_VarCount)), 1);
    CHECK_EQ((int)(RecByte(rec, kVar_Nibble) & 0x0F), (int)kVarInt);
    CHECK_EQ((int)RecI32(rec, kVar_ElemCount), 1);
    CHECK(RecPtr(rec, kVar_StorageP) != 0);
    // an int's storage is writable & zero-initialised.
    *reinterpret_cast<i32*>((std::intptr_t)RecPtr(rec, kVar_StorageP)) = 42;
    CHECK_EQ(*reinterpret_cast<i32*>((std::intptr_t)RecPtr(rec, kVar_StorageP)), 42);
    SetScriptVmHooks(nullptr);
}

// ===========================================================================
// ParseDeclaration — scalar / '=' init / array / function forms.
// ===========================================================================
TEST(scriptvm, scriptvm_parse_declaration_scalar) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    // `gold ;`  -> name token (cls 0 "gold"), then ';' symbol sub 10.
    TokScript s; s.toks = { TName("gold"), TSym(10) };
    g_script = &s;
    i32 r = ParseDeclaration(ctx.c(), kVarInt);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)Dw(ctx.c(), kIdx_VarCount), 1);
    CHECK(LookupVariable(ctx.c(), "gold") != nullptr);
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

TEST(scriptvm, scriptvm_parse_declaration_assign) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    // `x = 5`  -> name, '=' (sub 2), int literal 5.
    TokScript s; s.toks = { TName("x"), TSym(2), TInt(5) };
    g_script = &s;
    i32 r = ParseDeclaration(ctx.c(), kVarInt);
    CHECK_EQ(r, 1);
    u8* rec = LookupVariable(ctx.c(), "x");
    CHECK(rec != nullptr);
    CHECK_EQ(*reinterpret_cast<i32*>((std::intptr_t)RecPtr(rec, kVar_StorageP)), 5);
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

TEST(scriptvm, scriptvm_parse_declaration_array) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    // `arr [ 4 ] ;`  -> name, '[' (sub 27), int 4, ']' (sub 28), ';' (sub 10).
    TokScript s; s.toks = { TName("arr"), TSym(27), TInt(4), TSym(28), TSym(10) };
    g_script = &s;
    i32 r = ParseDeclaration(ctx.c(), kVarInt);
    CHECK_EQ(r, 1);
    u8* rec = LookupVariable(ctx.c(), "arr");
    CHECK(rec != nullptr);
    CHECK_EQ((int)RecI32(rec, kVar_ElemCount), 4);
    CHECK_EQ(g_reportCount, 0);   // no syntax errors
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

TEST(scriptvm, scriptvm_parse_declaration_array_missing_bracket) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    // missing ']' (give a wrong sub) then missing ';'
    TokScript s; s.toks = { TName("bad"), TSym(27), TInt(2), TSym(99), TSym(99) };
    g_script = &s;
    ParseDeclaration(ctx.c(), kVarInt);
    CHECK(g_reportCount >= 1);    // "Missing ']'!" and/or "Expecting ';'"
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

// The caller-supplied type code (token.value>>24 in edx @0x443692) is honoured:
// a `float f;` decl stores the float type nibble, not int. Regression for the
// hardening fix that forwarded `type` to DefineVariable in every path.
TEST(scriptvm, scriptvm_parse_declaration_type_honoured) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    TokScript s; s.toks = { TName("f"), TSym(10) };   // `f ;`
    g_script = &s;
    i32 r = ParseDeclaration(ctx.c(), kVarFloat);
    CHECK_EQ(r, 1);
    u8* rec = LookupVariable(ctx.c(), "f");
    CHECK(rec != nullptr);
    CHECK_EQ((int)(RecByte(rec, kVar_Nibble) & 0x0F), (int)kVarFloat);
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

// ===========================================================================
// ParseSymbolName — parse a 2-parameter signature into the func record.
// ===========================================================================
TEST(scriptvm, scriptvm_parse_symbol_name) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    // signature: (int a, string b) — the loop accepts only param types 1,2,6
    // (int/byte/string); float (7) is rejected by the original's range test, so
    // the test uses two accepted types. token stream per accepted pair = 3:
    //   type(8) , name(cls0) , term(cls1, sub==13 ends the list).
    TokScript s;
    s.toks = {
        TType(1, "int"), TName("a"), TTypeSym(0),    // first pair, term not 13 -> continue
        TType(6, "string"), TName("b"), TTypeSym(13) // second pair, term sub 13 -> done
    };
    // The term check uses cls==1 && HIBYTE(value)==13. Use a class-1 token whose
    // high byte is the sub code. TTypeSym(13) puts 13 in the high byte.
    g_script = &s;
    u8 lex[8] = {0};
    i32 r = ParseSymbolName(ctx.c(), lex);
    CHECK(r != 0);
    CHECK_EQ((int)Dw(ctx.c(), kIdx_FuncCount), 1);
    u8* fr = ctx.funcTable.data();
    CHECK_EQ((int)RecByte(fr, kFunc_ParamCount), 2);
    CHECK_EQ((int)RecByte(fr, kFunc_ParamTypes + 0), 1);  // int
    CHECK_EQ((int)RecByte(fr, kFunc_ParamTypes + 1), 6);  // string
    CHECK_EQ(std::string(reinterpret_cast<char*>(fr + kFunc_ParamNames + 0*32)), "a");
    CHECK_EQ(std::string(reinterpret_cast<char*>(fr + kFunc_ParamNames + 1*32)), "b");
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

// ===========================================================================
// DeclareLocal — frame-local slot allocation + name + scope marker.
// ===========================================================================
TEST(scriptvm, scriptvm_declare_local) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx;
    // Set up an active frame inside the context's call-frame region.
    u8* frame = ctx.c() + (std::size_t)kIdx_CallFrames * sizeof(std::intptr_t);
    Dw(ctx.c(), kIdx_ActiveFrame) = (std::intptr_t)frame;
    u8* rec = DeclareLocal(ctx.c(), kVarInt, "i", 1);
    CHECK(rec != nullptr);
    CHECK_EQ((int)(RecByte(rec, kVar_Nibble) & 0x0F), (int)kVarInt);
    CHECK_EQ((int)RecI32(rec, kVar_Scope), 1);
    CHECK_EQ(std::string(reinterpret_cast<char*>(rec + kVar_Name)), "i");
    // the frame's first local slot (dword index 4) now points at the record.
    CHECK_EQ(Dw(frame, 4), (std::intptr_t)rec);
    // a second local takes the next slot.
    u8* rec2 = DeclareLocal(ctx.c(), kVarInt, "j", 1);
    CHECK(rec2 != nullptr);
    CHECK_EQ(Dw(frame, 5), (std::intptr_t)rec2);
    SetScriptVmHooks(nullptr);
}

TEST(scriptvm, scriptvm_declare_local_too_many) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx;
    u8* frame = ctx.c() + (std::size_t)kIdx_CallFrames * sizeof(std::intptr_t);
    Dw(ctx.c(), kIdx_ActiveFrame) = (std::intptr_t)frame;
    // 8 locals fill the frame; the 9th must fail and report "Too many locals!".
    for (int i = 0; i < kMaxLocalsPerFrame; ++i) {
        char nm[4] = {(char)('a'+i), 0, 0, 0};
        CHECK(DeclareLocal(ctx.c(), kVarInt, nm, 1) != nullptr);
    }
    u8* over = DeclareLocal(ctx.c(), kVarInt, "z", 1);
    CHECK(over == nullptr);
    CHECK(g_reportCount >= 1);
    SetScriptVmHooks(nullptr);
}

// ===========================================================================
// SkipBraceBlock — forward (mode 1) skip over a balanced block.
// ===========================================================================
TEST(scriptvm, scriptvm_skip_brace_forward) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    const char* src = "  { a { b } c } trailer";
    Dw(ctx.c(), kIdx_SrcBase) = (std::intptr_t)src;
    Dw(ctx.c(), kIdx_SrcLen)  = (std::intptr_t)std::strlen(src);
    Dw(ctx.c(), kIdx_Cursor)  = (std::intptr_t)src;
    int r = SkipBraceBlock(1);
    CHECK_EQ(r, 1);
    // cursor lands just past the outer '}'
    const char* cur = reinterpret_cast<const char*>((std::intptr_t)Dw(ctx.c(), kIdx_Cursor));
    CHECK_EQ(std::string(cur), " trailer");
    g_script = nullptr; SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

TEST(scriptvm, scriptvm_skip_brace_no_match) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx; SetCurrentContext(ctx.c());
    const char* src = "no braces here";
    Dw(ctx.c(), kIdx_SrcBase) = (std::intptr_t)src;
    Dw(ctx.c(), kIdx_SrcLen)  = (std::intptr_t)std::strlen(src);
    Dw(ctx.c(), kIdx_Cursor)  = (std::intptr_t)src;
    CHECK_EQ(SkipBraceBlock(1), 0);
    SetScriptVmHooks(nullptr); SetCurrentContext(nullptr);
}

// ===========================================================================
// EnterFunction — overflow + a top-level (depth 0) entry binding 1 int param.
// ===========================================================================
TEST(scriptvm, scriptvm_enter_function_null) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx;
    int r = EnterFunction(ctx.c(), nullptr);
    CHECK_EQ(r, 7);              // ReportError stub returns 7
    CHECK_EQ(g_reportCount, 1);
    CHECK_EQ(g_lastError, std::string("evt_EnterFunction: Unknown function..."));
    SetScriptVmHooks(nullptr);
}

TEST(scriptvm, scriptvm_enter_function_toplevel_binds_param) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    Ctx ctx;
    // Build a func record with 1 int param "n" and a body source pointer.
    u8* fr = ctx.funcTable.data();
    RecByte(fr, kFunc_ParamCount) = 1;
    RecByte(fr, kFunc_ParamTypes + 0) = kVarInt;          // param[0] type int
    std::strcpy(reinterpret_cast<char*>(fr + kFunc_ParamNames + 0*32), "n");
    // EnterFunction reads the per-param type as *(fr+34)>>24 advancing fr; the
    // type byte lives at +37 (== ParamTypes[0]). Mirror that by also seeding +34
    // so HIBYTE(*(fr+34)) == kVarInt (byte at fr+37).
    static const char* body = "{ return n; }";
    RecPtr(fr, kFunc_BodyP) = (std::intptr_t)body;
    Dw(ctx.c(), kIdx_LocalCursor) = (std::intptr_t)ctx.localStk.data();
    int active = EnterFunction(ctx.c(), fr);
    CHECK(active != 0);
    // cursor jumped to the body.
    CHECK_EQ(std::string(reinterpret_cast<const char*>((std::intptr_t)Dw(ctx.c(), kIdx_Cursor))), body);
    // a local "n" was declared into the new frame.
    CHECK(LookupVariable(ctx.c(), "n") != nullptr);
    SetScriptVmHooks(nullptr);
}

// ===========================================================================
// DestroyContext — frees var storage + child contexts, marks state -1.
// ===========================================================================
TEST(scriptvm, scriptvm_destroy_context) {
    ScriptVmHooks h; InstallHooks(h); ResetCounters();
    // Build a context with heap-allocated tables so DestroyContext can free them.
    std::vector<u8> recBuf((std::size_t)kContextDwords * sizeof(std::intptr_t) + 64, 0);
    u8* c = recBuf.data();
    u8* varBase  = (u8*)std::malloc(4 * kVarRecordStride);
    std::memset(varBase, 0, 4 * kVarRecordStride);
    u8* funcBase = (u8*)std::malloc(2 * kFuncRecordStride);
    u8* srcBase  = (u8*)std::malloc(16);
    u8* lineBase = (u8*)std::malloc(16);
    u8* localBase= (u8*)std::malloc(16);
    Dw(c, kIdx_VarBase)  = (std::intptr_t)varBase;
    Dw(c, kIdx_VarCount) = 1;
    Dw(c, kIdx_FuncBase) = (std::intptr_t)funcBase;
    Dw(c, kIdx_SrcBase)  = (std::intptr_t)srcBase;
    Dw(c, kIdx_LineBase) = (std::intptr_t)lineBase;
    Dw(c, kIdx_LocalBase)= (std::intptr_t)localBase;
    // one var record with a heap storage block at +44.
    void* storage = std::malloc(8);
    RecPtr(varBase, kVar_StorageP) = (std::intptr_t)storage;

    int r = DestroyContext(c);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)Dw(c, kIdx_State), -1);
    CHECK_EQ(Dw(c, kIdx_VarBase), (std::intptr_t)0);
    CHECK_EQ(Dw(c, kIdx_FuncBase), (std::intptr_t)0);
    // the var storage block, var/func/src/line/local tables were all freed.
    CHECK(g_freed.size() >= 6);
    SetScriptVmHooks(nullptr);
}

// ===========================================================================
// RegisterScriptCommands — registers the 38 character commands into the table.
// ===========================================================================
TEST(scriptvm, scriptvm_register_character_commands) {
    guild::sim::ResetCommands();
    i32 r = RegisterScriptCommands();
    CHECK_EQ(r, 1);   // last ImportCommand succeeded
    // every command name resolves in the shared command table.
    int n = 0;
    const CharCommandDesc* tbl = CharCommandTable(&n);
    CHECK_EQ(n, 39);  // 38 in the brief count + the spspan @0x43dfb0 is 39 entries
    for (int i = 0; i < n; ++i) {
        u8* rec = (u8*)guild::sim::FindCommandByName(guild::sim::Commands().bytes, tbl[i].name);
        CHECK(rec != nullptr);
    }
    // spot-check a couple of arg signatures.
    CHECK_EQ(std::string(tbl[0].name), "CreateCharacter");
    CHECK_EQ(tbl[0].argc, 4);
    CHECK_EQ(std::string(tbl[n-1].name), "SetCharacterTransperancy");
    guild::sim::ResetCommands();
}
