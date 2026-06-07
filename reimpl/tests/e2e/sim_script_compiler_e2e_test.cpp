// End-to-end: a small .esc program is tokenized -> compiled -> executed through
// the recovered lexer/compiler/symbol-table + the source-text statement
// interpreter, invoking a mock registered command. Results and emitted commands
// are checked against a hand-computed reference.
#include "tests/framework/test.h"
#include "sim/script_lexer.h"
#include "sim/script_symbols.h"
#include "sim/script_compiler.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

// Hand reference for the program below:
//   i = 0
//   while (i < 4) {            // i = 0,1,2,3
//       if (i & 1) Beep(i);    // odd i -> emit Beep(1), Beep(3)
//       i = i + 1;
//   }
//   acc = (1+2+3+4+5)          // = 15 via a second loop
// Expected: Beep called with 1 then 3; final i == 4; acc == 15.
TEST(SimScriptE2E, LoopIfCommandCall) {
    const char* src =
        "int i ;\n"
        "int acc ;\n"
        "i = 0 ;\n"
        "while ( i < 4 ) {\n"
        "    if ( i & 1 ) Beep ( i ) ;\n"
        "    i = i + 1 ;\n"
        "}\n"
        "i = 1 ;\n"
        "acc = 0 ;\n"
        "while ( i <= 5 ) { acc = acc + i ; i = i + 1 ; }\n";

    std::vector<std::string> commandNames = {"Beep"};

    // 1) Compile: builds the symbol table from the declarations.
    CompiledScript cs = CompileScript(src, commandNames);
    CHECK(cs.ok);
    CHECK_EQ((int)cs.symbols.vars().size(), 2);
    CHECK_EQ(cs.symbols.LookupVariable("i"), 0);
    CHECK_EQ(cs.symbols.LookupVariable("acc"), 1);

    // 2) Execute, capturing emitted commands. The executor's lexer is given the
    //    command set so call sites classify as class 4 (kTokFuncCall).
    std::vector<std::pair<std::string, std::vector<i32>>> emitted;
    ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<i32>& args) -> i32 {
        emitted.push_back({name, args});
        return 0;
    };

    ScriptExecutor exec(cs, host, commandNames);
    exec.Run();

    int iIdx   = cs.symbols.LookupVariable("i");
    int accIdx = cs.symbols.LookupVariable("acc");

    // final i == 6 after the second loop (1..5 then +1)
    CHECK_EQ(cs.symbols.storage()[cs.symbols.vars()[iIdx].storage], 6);
    // acc == 15
    CHECK_EQ(cs.symbols.storage()[cs.symbols.vars()[accIdx].storage], 15);

    // Beep emitted for odd i in [0,4): i==1 and i==3
    CHECK_EQ((int)emitted.size(), 2);
    if (emitted.size() == 2) {
        CHECK(emitted[0].first == "Beep");
        CHECK_EQ((int)emitted[0].second.size(), 1);
        CHECK_EQ(emitted[0].second[0], 1);
        CHECK(emitted[1].first == "Beep");
        CHECK_EQ(emitted[1].second[0], 3);
    }
}

// Tokenize -> compile a function-with-while + verify the compiled symbol table
// matches the recovered record layout (the brief's "expected symbol table").
TEST(SimScriptE2E, CompiledSymbolTableLayout) {
    const char* src =
        "int n ;\n"
        "int result ;\n"
        "int sumTo ( int limit ) {\n"
        "    int k ;\n"
        "    k = 0 ;\n"
        "    while ( k <= limit ) { k = k + 1 ; }\n"
        "    return k ;\n"
        "}\n";
    CompiledScript cs = CompileScript(src, {});
    CHECK(cs.ok);

    // Two top-level variables, one function.
    CHECK_EQ((int)cs.symbols.vars().size(), 2);
    CHECK_EQ((int)cs.symbols.funcs().size(), 1);

    const ScriptVar& n = cs.symbols.vars()[0];
    CHECK(n.name == "n");
    CHECK_EQ((int)n.type(), (int)kVarInt);
    CHECK_EQ(n.count, 1);
    CHECK_EQ(n.storage, 0);

    const ScriptVar& r = cs.symbols.vars()[1];
    CHECK(r.name == "result");
    CHECK_EQ(r.storage, 1);

    const ScriptFunc& f = cs.symbols.funcs()[0];
    CHECK(f.name == "sumTo");
    CHECK_EQ(f.paramCount(), 1);
    CHECK_EQ((int)f.paramTypes[0], (int)kVarInt);
    CHECK(f.paramNames[0] == "limit");
    CHECK(f.bodyCursor >= 0);
    // body cursor points at the '{' that opens the function body.
    CHECK_EQ(cs.source[f.bodyCursor], '{');

    // storage end = 4*1 + 4*1 = 8
    CHECK_EQ(cs.symbols.VariableStorageEnd(), 8);
}
