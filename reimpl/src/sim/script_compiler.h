#pragma once
// The .esc recursive-descent compiler front-end for the Guild scripting engine
// (gilde.exe).  Namespace: guild::sim.
//
// This re-implements the compile pass and the source-text-driven statement
// interpreter that the originals split across:
//   * VIBE_Script_CompileBlock     0x4435d0 — top-level block compile: alloc the
//       var (+136) and func (+140) tables, walk tokens, dispatch declarations,
//       balance braces, then trim the tables to their final sizes.
//   * VIBE_Script_ParseDeclaration 0x442d88 — a declaration statement: read the
//       type keyword (class 8), then a name; on '=' / '[' / function-sig forms
//       build the variable (DefineVariable) or function (ParseSymbolName) record.
//   * VIBE_Script_ParseSymbolName  0x442b34 — parse a function signature's
//       parameter list (type+name pairs) into the func record (+37 types,
//       +45 names), terminated by sub-code 13.
//   * VIBE_Script_EnterFunction    0x4431dc — runtime function call: evaluate
//       arguments, push a 36-dword call frame (+2472 stack), bind parameters as
//       locals (DeclareLocal), and jump the cursor to the body.
//   * VIBE_Script_ParseWhileLoop   0x44259c — runtime while: evaluate condition
//       into a 12-byte loop frame on the +2472 stack (restart cursor at +0,
//       condition at +4, type 1 at +8, has-brace at +9); skip the body if false.
//   * VIBE_Script_ParseForLoop     0x4427b4 — runtime for/once: same frame, type 2.
//   * VIBE_Script_DispatchTokenBranch 0x442ac8 — runtime if: on a false guard,
//       skip the braced block or to the next ';'.
//
// The original is a tree-walking interpreter over tokenised source text: the
// "compile" pass only populates the symbol tables (variables + function bodies);
// statements (assignments, calls, loops, ifs) are re-tokenised and executed each
// step from the live source cursor.  We follow that model exactly: CompileScript
// builds a ScriptSymbols, and ExecuteScript drives the lexer + ScriptSymbols +
// ScriptHost through the statement dispatcher, honouring the recovered loop-frame
// and branch semantics.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include "sim/script_lexer.h"
#include "sim/script_symbols.h"
#include <string>
#include <vector>

namespace guild::sim {

// A compiled script: the symbol table plus the source text it was compiled from
// (the executor re-tokenises the source from the live cursor, as the original).
struct CompiledScript {
    ScriptSymbols symbols;
    std::string   source;
    bool          ok = false;
    std::string   error;        // first compile error, if any
};

// gilde.exe 0x4435d0 — VIBE_Script_CompileBlock (top level). Tokenises the whole
// source once, registering every `<type> <name>;` declaration as a variable and
// every `<type> <name>(<params>) { ... }` as a function, while checking brace
// balance. Returns a CompiledScript with symbols populated. `commandNames` is
// the registered-command set (for class-4 classification during lexing).
CompiledScript CompileScript(const std::string& source,
                             const std::vector<std::string>& commandNames);

// A 12-byte loop frame on the block-scope stack (+2472 frame array). Recovered
// from ParseWhileLoop / ParseForLoop / ExecuteStatement.
struct LoopFrame {
    int restartCursor = 0;   // +0  (=+48 in frame array) re-entry source cursor
    int condition     = 0;   // +4  (=+52) last evaluated condition
    u8  type          = 0;   // +8  (=+56) 1 = while, 2 = for/once
    u8  hasBrace      = 0;   // +9  (=+57) body is a { } block (vs single stmt)
    // Brace-nesting depth INSIDE the loop body (== braceDepth_ just after the
    // loop's own '{' was consumed). The loop's closing '}' is the one that drops
    // braceDepth_ back to this value; deeper '}' (e.g. a nested if-block) belong
    // to inner blocks and must NOT re-test the loop. (The original tracks this via
    // the per-frame block-scope counter `*(frame+8)` / dword_62E8E0 in
    // ExecuteStatement; without it, an inner if's '}' would prematurely pop the
    // enclosing while — the exit() teardown loop of Schornstein_dunkel.esc.)
    int bodyDepth     = 0;
};

// The source-text-driven executor: drives ScriptLexer over a CompiledScript and
// dispatches statements exactly like VIBE_Script_ExecuteStatement (0x444bd0):
// assignment (class 2), command call (class 4), declaration (class 8 skip),
// keyword while/for/return/if/mode (class 10), block markers (class 1 sub
// 8/9/12/13). Loops re-run via the loop-frame stack; if skips false branches.
//
// Returns the script's return value (dword_62E8D0 stash). `emitted` collects the
// commands the host invoked (for test assertions). Bounded by a step budget.
class ScriptExecutor {
public:
    ScriptExecutor(CompiledScript& cs, ScriptHost host,
                   std::vector<std::string> commandNames = {})
        : cs_(cs), commands_(std::move(commandNames)),
          lexer_(cs.source, &cs.symbols, &commands_), host_(std::move(host)) {}

    // Run from `entryCursor` (default 0 = whole script body) to completion.
    i32 Run(int entryCursor = 0, int stepBudget = 100000);

    i32 returnValue() const { return returnValue_; }

private:
    // gilde.exe 0x443ff0 — VIBE_Script_EvaluateExpression over the live token
    // stream (operator-precedence fold; comparison ops short-circuit-return).
    i32 EvalExpression();

    // One statement (VIBE_Script_ExecuteStatement). Returns false to stop.
    bool ExecStatement();

    // gilde.exe 0x44259c / 0x4427b4 — while / for loop frame setup.
    void EnterLoop(u8 type);
    // gilde.exe 0x442ac8 — if-branch dispatch (skip false guard).
    void DispatchBranch();

    // Skip a `{ ... }` block (SkipBraceBlock 0x4416c4) or to ';' (SkipToSemicolon
    // 0x441660) from the current cursor.
    void SkipBraceBlock();
    void SkipToSemicolon();

    // Advance past the next '{' and increment braceDepth_ (the "enter block"
    // transition). Returns true if a '{' was consumed.
    bool StepIntoBlock();

    i32  ReadVar(int idx, int elemIndex) const;
    void WriteVar(int idx, int elemIndex, i32 value);

    // Collect a parenthesised command-call argument list `(a, b, ...)`; shared by
    // the value-position and statement-position command-call paths.
    std::vector<i32> CollectCallArgs();

    CompiledScript& cs_;
    std::vector<std::string> commands_;   // declared before lexer_ (init order)
    ScriptLexer lexer_;
    ScriptHost  host_;
    std::vector<LoopFrame> loopStack_;   // +2472 frame array
    int  braceDepth_ = 0;                // dword_62E8E0 — current { } nesting depth
    bool runnable_ = true;               // +164 bit0
    bool finished_ = false;
    u8   stmtMode_ = 0;                   // +2564
    i32  returnValue_ = 0;               // dword_62E8D0
    char lastTerm_ = 0;                  // terminator EvalExpression stopped on
    int  steps_ = 0;
    int  budget_ = 0;
};

} // namespace guild::sim
