#pragma once
// The .esc scripting VM core for the Guild simulation (gilde.exe).
//
// Namespace: guild::sim  (the scripting engine is bound into the sim cluster:
//   VIBE_Script_StepAllActive is driven from the per-frame loop, mask 0x200;
//   the VIBE_Script_* prefix lives at 0x43c650..0x445d7c. We keep it under
//   guild::sim alongside cutscene.{h,cpp} which shares its runtime.)
//
// The original is NOT a classic register/bytecode VM: the ".esc" engine is a
// tree-walking interpreter over tokenised script *source text*. Each per-script
// runtime "context" (2584 bytes) holds the source cursor, the variable/symbol
// tables, the call/return stack and the registered-command argument scratch.
// StepAllActive scans a fixed table of 128 contexts each frame and steps the
// runnable ones; Step tokenises and executes statements until the context
// blocks (on a command) or yields.
//
// What this file recovers byte-for-byte:
//   * the per-script ScriptContext layout (stride 2584, the fields the stepper,
//     statement executor, expression evaluator and command-invoker touch),
//   * the registered-command record layout (stride 52, 256 slots),
//   * the token-class and operator "opcode" tables (the instruction set),
//   * the context-table iteration (StepAllActive / CountActive / FreeFinished).
//
// What it translates fully as the "VM core + representative opcodes":
//   * the operator-precedence expression evaluator (EvalExpression) reproducing
//     VIBE_Script_EvaluateExpression's arithmetic/comparison/bitwise semantics,
//   * the statement dispatcher (ExecStatement) reproducing
//     VIBE_Script_ExecuteStatement's var-set / branch / call / return handling,
//   * the registered-command invocation ABI (InvokeCommand) reproducing
//     VIBE_Script_InvokeCommand's arg-count switch and by-pointer argument
//     passing.  Command bodies and host-engine leaves are forward-declared as
//     mockable hooks (see ScriptHost) — the brief's "emit commands / call
//     leaves -> forward-declare/mock".
//
// Deferred (deeply-coupled, listed in the module report, not translated here):
//   the source-text lexer VIBE_Script_NextToken (0x441974), the compiler pass
//   (CompileBlock/EnterFunction/ParseWhileLoop/ParseForLoop/...), the live
//   variable storage walk VIBE_Script_GetVariableAddress (0x4459bc, needs the
//   compiled symbol table), and Universe_SwitchActiveSlot scene swaps in Step.
#include "guild/common/types.h"
#include <vector>
#include <string>
#include <functional>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Token classes  (VIBE_Script_NextToken writes the class to result[0]; the
// statement/expression dispatchers switch on it). Recovered from the switch
// arms of VIBE_Script_ExecuteStatement (0x444bd0) and
// VIBE_Script_EvaluateExpression (0x443ff0).
// ---------------------------------------------------------------------------
enum ScriptToken : u8 {
    kTokUnknown   = 0,   // unresolved symbol -> "Unknown symbol" error
    kTokSymbol    = 1,   // operator / keyword-bearing symbol (sub-code in arg)
    kTokVariable  = 2,   // (array) variable reference
    kTokFuncDef   = 3,   // function definition / block enter (ExecuteStatement)
    kTokFuncCall  = 4,   // user-function call
    kTokIntLit    = 5,   // integer literal (value in arg)
    kTokRawSym    = 6,   // bare symbol (evaluator: no-op operand)
    kTokStrLit    = 7,   // string literal (copied to scratch)
    kTokDeclare   = 8,   // declaration statement
    kTokKeyword   = 10,  // 0xA: control keyword (sub-code in arg, see below)
    kTokLabel     = 11,  // 0xB: label / goto target (sets pending-label)
    kTokBlockEnd  = 12,  // 0xC: end of block/statement -> stop
};

// Keyword sub-codes (the low byte of the arg word when class == kTokKeyword).
// Recovered from the inner switch of VIBE_Script_ExecuteStatement.
enum ScriptKeyword : u8 {
    kKwWhile    = 1,
    kKwFor      = 2,
    kKwReturn   = 3,
    kKwIf       = 4,   // dispatch/branch
    kKwInclude  = 5,
    kKwModeA    = 6,   // set ctx.stmtMode = 1  (+2564)
    kKwModeB    = 7,   // set ctx.stmtMode = 0
    kKwModeLoop = 8,   // set ctx.stmtMode = 2  (re-run statement while active)
    kKwExit     = 9,   // (symbol class 1, sub-code 9) exit/return-from-function
    kKwBlockPop = 10,  // (symbol class 1, sub-code 10) pop block scope
};

// ---------------------------------------------------------------------------
// Operator "opcodes" — the instruction set of the expression evaluator.
// VIBE_Script_EvaluateExpression keeps a "current operator" (v1, set from the
// symbol sub-code) and a "pending accumulator operator" (v45). Comparison ops
// short-circuit-return a bool; accumulator ops fold the operand into v3.
// Codes recovered verbatim from the evaluator's two operator dispatch ladders.
// ---------------------------------------------------------------------------
enum ScriptOp : u8 {
    kOpOperand  = 2,    // start-of-expression / push operand (v1==2 / v45==2)
    // accumulator (binary fold) operators (v45 ladder):
    kOpAdd      = 4,    // v3 += operand
    kOpSub      = 6,    // v3 -= operand
    kOpDiv      = 20,   // 0x14  v3 /= operand
    kOpMul      = 21,   // 0x15  v3 *= operand
    kOpOr       = 24,   // 0x18  v3 |= operand
    kOpAnd      = 25,   // 0x19  v3 &= operand
    // comparison operators (v1 ladder) — each returns the bool immediately:
    kOpEq       = 1,    // ==
    kOpNe       = 7,    // !=
    kOpLe       = 16,   // 0x10  <=
    kOpLt       = 17,   // 0x11  <
    kOpGe       = 18,   // 0x12  >=
    kOpGt       = 19,   // 0x13  >
    kOpBitOr    = 22,   // 0x16  |  (returns v3 | operand)
    kOpBitAnd   = 23,   // 0x17  &  (returns v3 & operand)
    kOpLabelRef = 11,   // 0xB   label reference (pushes label id as operand)
    kOpStop     = 28,   // 0x1C  expression terminator (returns accumulator)
};

// ---------------------------------------------------------------------------
// Per-script runtime context  (gilde.exe table base dword_62E8A4 @0x62E8A4)
//   stride 2584 bytes, 128 slots (330752 / 2584). Heap-allocated (cold IDB 0).
// Only the fields the stepper / statement-exec / evaluator / command-invoker
// address are named; the rest is the source buffer, compiled symbol/function
// tables and command-arg scratch, left as padding with their offsets noted.
//   +0    (byte)  in-use / "loaded" flag (CountActive, FreeFinished gate)
//   +128  (dword) script handle/id; -1 == free context (Step gate, +132 owner)
//   +132  (dword) owning scene/caller id; -1 or == dword_62E8D4 -> skip-run
//   +152  (dword) source cursor (char*) — the instruction pointer
//   +156  (dword) source length / token base
//   +160  (dword) source buffer base
//   +164  (byte)  run-flags: bit0 = runnable, bit1 = suspended
//   +2472 (dword) block-scope stack base (compiled scope frames, 12 B each)
//   +2524 (dword) pending command record ptr (set by call; cleared by invoke)
//   +2528 (dword) command-blocked flag (nonzero -> InvokeCommand re-arms)
//   +2532 (dword) per-arg scratch ptrs (freed for type-6 string args)
//   +2536..+2560  command argument value slots (5 dwords, by-ptr ABI)
//   +2564 (byte)  statement mode (0/1/2; 2 == re-run current statement)
//   +2572 (dword) blocked-on-scene flag (gates Step's scene swap)
//   +2576 (dword) scene-slot id ; +2580 scene-context ptr
// ---------------------------------------------------------------------------
constexpr int kScriptContextStride = 2584;
constexpr int kScriptContextCount  = 128;     // 330752 / 2584

enum ScriptCtxField : int {
    kScInUse        = 0,
    kScHandle       = 128,
    kScOwner        = 132,
    kScCursor       = 152,
    kScSrcLen       = 156,
    kScSrcBase      = 160,
    kScRunFlags     = 164,   // bit0 runnable, bit1 suspended
    kScScopeBase    = 2472,
    kScPendingCmd   = 2524,
    kScCmdBlocked   = 2528,
    kScArgScratch   = 2532,
    kScArg0         = 2536,   // arg slots stride 4
    kScStmtMode     = 2564,
    kScSceneBlocked = 2572,
    kScSceneSlot    = 2576,
    kScSceneCtx     = 2580,
};
constexpr u8 kRunFlagRunnable  = 0x01;  // ctx[+164] & 1
constexpr u8 kRunFlagSuspended = 0x02;  // ctx[+164] & 2

// ---------------------------------------------------------------------------
// Registered-command record  (gilde.exe table base dword_62E8AC @0x62E8AC)
//   stride 52, 256 slots (13312 / 52). Built by VIBE_Script_ImportCommand
//   (0x445bc8); looked up by name (UTF-16 StrCmp) in FindCommandByName.
//   +0    name (UTF-16, <=31 chars) — also the StrCmp key region
//   +32   (dword) argument count (0..7)
//   +36   (byte[8]) per-argument type codes (type 6 == owned string -> freed)
//   +44   (dword) command function pointer
//   +48   (byte)  command kind/category
// VIBE_Script_InvokeCommand switches on the arg count and calls fn(&arg0,
// &arg1, ...) with pointers into the context's arg-value slots.
// ---------------------------------------------------------------------------
constexpr int kCommandStride   = 52;
constexpr int kCommandCapacity = 256;       // 13312 / 52
constexpr int kMaxCommandArgs  = 7;
constexpr u8  kArgTypeOwnedStr = 6;         // +36 type-6 arg is heap-freed

// ---------------------------------------------------------------------------
// Token program model (for the translated VM core / tests).
// The original lexes from source text; here a script is a flat token stream so
// the evaluator/statement dispatcher can be exercised 1:1 without porting the
// 431-instruction lexer. A Token carries the recovered class + arg payload.
// ---------------------------------------------------------------------------
struct ScriptToken_t {
    u8  cls;      // ScriptToken class
    u8  sub;      // operator code / keyword sub-code (symbol/keyword classes)
    i32 value;    // int literal value / variable index / function id / label id
    std::string text; // string-literal payload (kTokStrLit) or var name
};

// Variable cell: the evaluator reads/writes 32-bit cells by index (the original
// resolves an address via GetVariableAddress over the compiled symbol table).
struct ScriptVarStore {
    std::vector<i32> cells;
    i32 Get(int idx) const { return (idx >= 0 && idx < (int)cells.size()) ? cells[idx] : 0; }
    void Set(int idx, i32 v) {
        if (idx < 0) return;
        if (idx >= (int)cells.size()) cells.resize(idx + 1, 0);
        cells[idx] = v;
    }
};

// Host hooks — the "emit command / call leaf" boundary. The original calls a
// registered C function (cmd record +44) with by-pointer arg slots; we model
// that as a callback so command bodies and scene/host leaves are mockable.
struct ScriptHost {
    // Invoke a named command with its (already-evaluated) integer arguments,
    // mirroring VIBE_Script_InvokeCommand's by-pointer ABI. Returns the command
    // result. args may be mutated in place (out-parameters).
    std::function<i32(const std::string& name, std::vector<i32>& args)> invokeCommand;
    // Call a user-defined script function by id (mirrors VIBE_Script_CallFunction
    // leaf); returns its result. Deferred body -> mocked in tests.
    std::function<i32(i32 funcId)> callUserFunction;
};

// A self-contained interpreter over a token program implementing the recovered
// VM core semantics. One VmContext mirrors one ScriptContext's evaluation state
// (cursor + variable store + run flags + statement mode).
class ScriptVm {
public:
    ScriptVm(const std::vector<ScriptToken_t>& program, ScriptHost host)
        : prog_(program), host_(std::move(host)) {}

    ScriptVarStore& vars() { return vars_; }
    int cursor() const { return ip_; }
    void setCursor(int ip) { ip_ = ip; }

    // gilde.exe 0x443ff0 — VIBE_Script_EvaluateExpression(stopOp@<eax>)
    // Operator-precedence fold: reads operands/operators from the token stream
    // starting at the cursor; accumulates with the pending accumulator operator
    // (kOpAdd/kOpSub/kOpMul/kOpDiv/kOpOr/kOpAnd); a comparison operator
    // (kOpEq..kOpBitAnd) returns its boolean result immediately. stopOp is the
    // operator code at which a nested evaluation stops (0 == run to terminator).
    // Returns the resulting integer.
    i32 EvalExpression(u8 stopOp);

    // gilde.exe 0x444bd0 — VIBE_Script_ExecuteStatement(ctx@<eax>)
    // Reads the next statement token and dispatches: variable-assign (class 2),
    // function-call (class 4), function-def/enter (class 3), declaration
    // (class 8), label (class 0xB), or keyword (class 10: while/for/return/if/
    // include/mode). Returns 0 on a normal statement, propagating block-end.
    // Returns false when the script should stop (block end / exit / error).
    bool ExecStatement();

    // gilde.exe 0x444f4c — VIBE_Script_InvokeCommand: resolve the named command
    // and call its body via the host with by-pointer arg slots (arg count 0..7).
    i32 InvokeCommand(const std::string& name, std::vector<i32> args);

    // Run statements until the program ends or a stop is hit. Mirrors the
    // do/while loop in VIBE_Script_Step (run while stmtMode==2 && runnable).
    void Run();

    // Return value stash (VIBE_Script_HandleExitKeyword path; dword_62E8D0).
    i32 returnValue() const { return returnValue_; }
    bool finished() const { return finished_; }

private:
    const ScriptToken_t& peek() const;
    const ScriptToken_t& next();
    bool atEnd() const { return ip_ >= (int)prog_.size(); }

    const std::vector<ScriptToken_t>& prog_;
    ScriptHost host_;
    ScriptVarStore vars_;
    int ip_ = 0;
    u8  stmtMode_ = 0;     // ctx +2564
    bool runnable_ = true; // ctx +164 bit0
    bool finished_ = false;
    i32 returnValue_ = 0;
    i32 pendingLabel_ = 0; // dword_62E8C8
};

// ---------------------------------------------------------------------------
// Script context-table stepping (faithful to the originals; Step body is a
// host hook because it pulls in the lexer + scene swaps).
// ---------------------------------------------------------------------------

// A minimal mirror of the on-disk context fields the table scanners read.
struct ScriptSlot {
    u8  inUse   = 0;    // +0
    i32 handle  = -1;   // +128 (-1 == free)
    u8  runFlags = 0;   // +164 (bit0 runnable)
};

// gilde.exe 0x445250 — VIBE_Script_StepAllActive(curScene@<ebp>)
//   Scans all 128 contexts; for each with run-flag bit0 set, records it as the
//   "current" context (dword_62E8A8) and calls VIBE_Script_Step. step() is the
//   per-context stepper hook (mocked/host-provided). Returns last step result.
int StepAllActive(std::vector<ScriptSlot>& slots,
                  const std::function<int(ScriptSlot&)>& step);

// gilde.exe 0x445d40 — VIBE_Script_CountActive: count contexts that are in use
//   (+0 nonzero) OR runnable (+164 bit0 set).
int CountActive(const std::vector<ScriptSlot>& slots);

// gilde.exe 0x445370 — VIBE_Script_FreeFinished: destroy every context that is
//   in use (+0) or has a live handle (+128 != -1). destroy() is the teardown
//   hook (VIBE_Script_DestroyContext). Returns count destroyed.
int FreeFinished(std::vector<ScriptSlot>& slots,
                 const std::function<void(ScriptSlot&)>& destroy);

} // namespace guild::sim
