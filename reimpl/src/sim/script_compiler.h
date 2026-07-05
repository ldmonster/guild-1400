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

// A 12-byte loop frame slot on the per-call block-scope array (the 8 frames at
// +48 of the 36-dword call record reached via ctx+2472). Recovered 1:1 from
// ParseWhileLoop (0x44259c) / ParseForLoop (0x4427b4) / ExecuteStatement
// (0x444bd0) / EvaluateExpression (0x443ff0 LABEL_23):
//   slot i lives at record + 12*i + 48; a push writes slot[count] then ++count;
//   every '}' pops the top slot (count--); a ';' pops a pending single-statement
//   (noBrace) slot when count > 1. Popped slots are NOT cleared — the `else`
//   dispatcher (0x442ac8) reads slot[count+1].condition, one ABOVE the popped
//   slot (an original off-by-one, preserved; see DispatchBranch).
struct LoopFrame {
    int restartCursor = 0;   // +0 (=+48) source cursor of the loop keyword
    int condition     = 0;   // +4 (=+52) last evaluated condition (while-true
                             //     frames keep 0 here: 0x44259c memsets the slot
                             //     AFTER the condition write, if-frames after)
    u8  type          = 0;   // +8 (=+56) 1 = while, 2 = if ("for" is a misname)
    u8  noBrace       = 0;   // +9 (=+57) 1 = single-statement body (no '{ }')
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

    // gilde.exe 0x44259c (type 1, while) / 0x4427b4 (type 2, if) — evaluate the
    // '(' condition ')', push a loop frame, and on a false condition skip the
    // body (leaving the cursor ON the closing '}' / ';' for the if form so the
    // statement loop pops the frame). restartCursor = source offset of the
    // keyword itself (the binary stores cursor - strlen(keyword)).
    void EnterLoop(u8 type, int restartCursor);
    // gilde.exe 0x442ac8 — VIBE_Script_DispatchTokenBranch (the `else` keyword).
    void DispatchBranch();
    // gilde.exe 0x444bd0 LABEL_6 / 0x443ff0 LABEL_23 — a ';' pops a pending
    // single-statement (noBrace) frame when count > 1; a popped while frame
    // rewinds the cursor to its restart for the re-test.
    void PopFrameAtSemicolon();

    // Skip a `{ ... }` block (SkipBraceBlock 0x4416c4) or to ';' (SkipToSemicolon
    // 0x441660) from the current cursor.
    void SkipBraceBlock();
    void SkipToSemicolon();

    i32  ReadVar(int idx, int elemIndex) const;
    void WriteVar(int idx, int elemIndex, i32 value);

    // Collect a parenthesised command-call argument list `(a, b, ...)`; shared by
    // the value-position and statement-position command-call paths.
    std::vector<i32> CollectCallArgs();

    CompiledScript& cs_;
    std::vector<std::string> commands_;   // declared before lexer_ (init order)
    ScriptLexer lexer_;
    ScriptHost  host_;
    // The block-scope frame slots of the current call record (8 usable, cap
    // enforced as in the binary; slot [9] is readable by the else off-by-one).
    // Zero-initialised — the deterministic stand-in for the original's
    // uninitialised heap slots (the else read of a never-written slot).
    LoopFrame frames_[16];
    int  frameCount_ = 0;                // call record +8
    bool runnable_ = true;               // +164 bit0
    bool finished_ = false;
    u8   stmtMode_ = 0;                   // +2564
    i32  returnValue_ = 0;               // dword_62E8D0
    char lastTerm_ = 0;                  // terminator EvalExpression stopped on
    int  steps_ = 0;
    int  budget_ = 0;
};

} // namespace guild::sim
