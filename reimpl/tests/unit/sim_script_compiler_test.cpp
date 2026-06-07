// Unit tests for the .esc lexer / symbol table / compiler front-end.
//   src/sim/script_lexer.{h,cpp}, script_symbols.{h,cpp}, script_compiler.{h,cpp}
#include "tests/framework/test.h"
#include "sim/script_lexer.h"
#include "sim/script_symbols.h"
#include "sim/script_compiler.h"

using namespace guild;
using namespace guild::sim;

// --- char-class table recovered byte-for-byte (byte_64A208) ----------------
TEST(SimScriptLex, CharClassTableSpotChecks) {
    // digit-class test is byte_64A208[(u8)(c+1)] & 0x20
    CHECK(IsDigitClass('0'));
    CHECK(IsDigitClass('5'));
    CHECK(IsDigitClass('9'));
    CHECK(!IsDigitClass('a'));
    CHECK(!IsDigitClass('Z'));
    CHECK(!IsDigitClass('+'));
    // exact bytes from get_bytes @0x64A208
    CHECK_EQ((int)kCharClass[0x00], 0x00);
    CHECK_EQ((int)kCharClass[0x0a], 0x03);
    CHECK_EQ((int)kCharClass[0x31], 0x38);   // index '0'+1
    CHECK_EQ((int)kCharClass[0x42], 0x58);
    CHECK_EQ((int)kCharClass[0x7f], 0x01);
}

// --- operator / keyword token tables ---------------------------------------
TEST(SimScriptLex, OperatorTableSubcodes) {
    // Each operator's sub-code must equal the recovered ScriptOp value.
    auto find = [](const char* t) -> int {
        for (const auto& e : kOperatorTable) if (std::string(e.text) == t) return e.subcode;
        return -1;
    };
    CHECK_EQ(find("=="), (int)kOpEq);
    CHECK_EQ(find("+"),  (int)kOpAdd);
    CHECK_EQ(find("-"),  (int)kOpSub);
    CHECK_EQ(find("!="), (int)kOpNe);
    CHECK_EQ(find("<="), (int)kOpLe);
    CHECK_EQ(find("<"),  (int)kOpLt);
    CHECK_EQ(find(">="), (int)kOpGe);
    CHECK_EQ(find(">"),  (int)kOpGt);
    CHECK_EQ(find("/"),  (int)kOpDiv);
    CHECK_EQ(find("*"),  (int)kOpMul);
    CHECK_EQ(find("|"),  (int)kOpBitOr);
    CHECK_EQ(find("&"),  (int)kOpBitAnd);
    CHECK_EQ(find("||"), (int)kOpOr);
    CHECK_EQ(find("&&"), (int)kOpAnd);
}

TEST(SimScriptLex, KeywordTableSubcodes) {
    auto find = [](const char* t) -> int {
        for (const auto& e : kKeywordTable) if (std::string(e.text) == t) return e.subcode;
        return -1;
    };
    CHECK_EQ(find("while"),   (int)kKwWhile);
    CHECK_EQ(find("for"),     (int)kKwFor);
    CHECK_EQ(find("return"),  (int)kKwReturn);
    CHECK_EQ(find("if"),      (int)kKwIf);
    CHECK_EQ(find("include"), (int)kKwInclude);
}

// --- tokenizing a small .esc source -> expected token stream ----------------
TEST(SimScriptLex, TokenizeDeclAndExpr) {
    // declare x first so the lexer classifies it as a variable (class 2).
    ScriptSymbols syms;
    syms.DefineVariable("x", kVarInt, 1);
    std::vector<std::string> cmds = {"PlaySound"};

    ScriptLexer lex("x = 3 + 4 ;", &syms, &cmds);
    auto toks = lex.Tokenize();

    // x(var) = 3 + 4 ; <end>
    CHECK(toks.size() >= 7);
    CHECK_EQ((int)toks[0].cls, (int)kTokVariable);
    CHECK_EQ(toks[0].value, 0);
    CHECK_EQ((int)toks[1].cls, (int)kTokSymbol);   // '='  (operator sub-code)
    CHECK_EQ((int)toks[2].cls, (int)kTokIntLit);
    CHECK_EQ(toks[2].value, 3);
    CHECK_EQ((int)toks[3].cls, (int)kTokSymbol);
    CHECK_EQ((int)toks[3].sub, (int)kOpAdd);
    CHECK_EQ((int)toks[4].cls, (int)kTokIntLit);
    CHECK_EQ(toks[4].value, 4);
    CHECK_EQ((int)toks.back().cls, (int)kTokBlockEnd);
}

TEST(SimScriptLex, TokenizeKeywordsAndCommand) {
    ScriptSymbols syms;
    std::vector<std::string> cmds = {"Sleep"};
    ScriptLexer lex("while ( 1 ) Sleep", &syms, &cmds);
    auto toks = lex.Tokenize();
    CHECK_EQ((int)toks[0].cls, (int)kTokKeyword);
    CHECK_EQ((int)toks[0].sub, (int)kKwWhile);
    CHECK_EQ((int)toks[1].cls, (int)kTokSymbol);   // '('
    CHECK_EQ((int)toks[1].sub, 27);
    CHECK_EQ((int)toks[2].cls, (int)kTokIntLit);
    CHECK_EQ(toks[2].value, 1);
    CHECK_EQ((int)toks[3].cls, (int)kTokSymbol);   // ')'
    CHECK_EQ((int)toks[3].sub, 28);
    CHECK_EQ((int)toks[4].cls, (int)kTokFuncCall); // command Sleep
    CHECK_EQ(toks[4].value, 0);
}

TEST(SimScriptLex, StringLiteral) {
    ScriptSymbols syms;
    std::vector<std::string> cmds;
    ScriptLexer lex("\"hello world\"", &syms, &cmds);
    auto t = lex.Next();
    CHECK_EQ((int)t.cls, (int)kTokStrLit);
    CHECK(t.text == "hello world");
}

// --- symbol table: DefineVariable / Lookup / GetVariableAddress -------------
TEST(SimScriptSym, DefineAndLookupVariables) {
    ScriptSymbols s;
    int a = s.DefineVariable("alpha", kVarInt, 1);
    int b = s.DefineVariable("beta",  kVarByte, 1);
    int c = s.DefineVariable("arr",   kVarInt, 4);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 1);
    CHECK_EQ(c, 2);
    CHECK_EQ(s.LookupVariable("alpha"), 0);
    CHECK_EQ(s.LookupVariable("beta"),  1);
    CHECK_EQ(s.LookupVariable("arr"),   2);
    CHECK_EQ(s.LookupVariable("missing"), -1);

    // type nibbles recovered from DefineVariable
    CHECK_EQ((int)s.vars()[0].type(), (int)kVarInt);
    CHECK_EQ((int)s.vars()[1].type(), (int)kVarByte);
    CHECK_EQ(s.vars()[2].count, 4);

    // GetVariableAddress: sum of 4*count over all vars = 4*1 + 4*1 + 4*4 = 24
    CHECK_EQ(s.VariableStorageEnd(), 24);

    // storage handles are contiguous: alpha@0, beta@1, arr@2..5
    CHECK_EQ(s.vars()[0].storage, 0);
    CHECK_EQ(s.vars()[1].storage, 1);
    CHECK_EQ(s.vars()[2].storage, 2);
}

TEST(SimScriptSym, DefineAndLookupFunctions) {
    ScriptSymbols s;
    int f = s.DefineFunction("doStuff", 42);
    CHECK_EQ(f, 0);
    CHECK_EQ(s.LookupFunction("doStuff"), 0);
    CHECK_EQ(s.LookupFunction("nope"), -1);
    CHECK_EQ(s.funcs()[0].bodyCursor, 42);
}

// --- compiler: declarations + function body registration -------------------
TEST(SimScriptCompile, VariableAndFunctionDeclarations) {
    const char* src =
        "int counter ;\n"
        "int total ;\n"
        "int box [ 3 ] ;\n"
        "int add ( int a int b ) { return a ; }\n";
    CompiledScript cs = CompileScript(src, {});
    CHECK(cs.ok);
    CHECK_EQ((int)cs.symbols.vars().size(), 3);
    CHECK_EQ(cs.symbols.LookupVariable("counter"), 0);
    CHECK_EQ(cs.symbols.LookupVariable("total"), 1);
    CHECK_EQ(cs.symbols.LookupVariable("box"), 2);
    CHECK_EQ(cs.symbols.vars()[2].count, 3);

    CHECK_EQ((int)cs.symbols.funcs().size(), 1);
    CHECK_EQ(cs.symbols.LookupFunction("add"), 0);
    CHECK_EQ(cs.symbols.funcs()[0].paramCount(), 2);
    CHECK_EQ((int)cs.symbols.funcs()[0].paramTypes[0], (int)kVarInt);
    CHECK(cs.symbols.funcs()[0].paramNames[0] == "a");
    CHECK(cs.symbols.funcs()[0].bodyCursor >= 0);
}

TEST(SimScriptCompile, BraceImbalanceIsError) {
    CompiledScript cs = CompileScript("int f ( ) { return 1 ;", {});
    CHECK(!cs.ok);
}

// --- compiler + executor: while loop + var decl + assignment ----------------
TEST(SimScriptCompile, WhileLoopComputesSum) {
    // counter from 1..5, accumulate into total. Variables declared first so the
    // lexer resolves them; loop body is brace-delimited.
    const char* src =
        "int i ;\n"
        "int total ;\n"
        "i = 1 ;\n"
        "total = 0 ;\n"
        "while ( i <= 5 ) { total = total + i ; i = i + 1 ; }\n";
    CompiledScript cs = CompileScript(src, {});
    CHECK(cs.ok);
    CHECK_EQ((int)cs.symbols.vars().size(), 2);

    ScriptHost host;  // no commands needed
    ScriptExecutor exec(cs, host);
    exec.Run();

    int iIdx = cs.symbols.LookupVariable("i");
    int tIdx = cs.symbols.LookupVariable("total");
    // 1+2+3+4+5 = 15 ; i ends at 6
    CHECK_EQ(cs.symbols.storage()[cs.symbols.vars()[tIdx].storage], 15);
    CHECK_EQ(cs.symbols.storage()[cs.symbols.vars()[iIdx].storage], 6);
}
