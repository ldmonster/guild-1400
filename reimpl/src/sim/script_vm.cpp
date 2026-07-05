#include "sim/script_vm.h"

namespace guild::sim {

// ===========================================================================
// Token-stream helpers
// ===========================================================================
static const ScriptToken_t kEndToken{kTokBlockEnd, 0, 0, ""};

const ScriptToken_t& ScriptVm::peek() const {
    return atEnd() ? kEndToken : prog_[ip_];
}
const ScriptToken_t& ScriptVm::next() {
    if (atEnd()) return kEndToken;
    return prog_[ip_++];
}

// ===========================================================================
// gilde.exe 0x443ff0 — VIBE_Script_EvaluateExpression
//
// Faithful transcription of the original operator-precedence fold:
//   * v3  : accumulator (returned for the |/& terminator path and at kOpStop)
//   * v45 : pending accumulator operator (folds the *next* operand into v3)
//   * v1  : the operator that follows the current operand
// On reading an operand the original immediately classifies the following
// operator (v1). A comparison operator (kOpEq..kOpGt / kOpBitAnd) recurses to
// evaluate the RHS and returns the boolean result. The accumulator operators
// (kOpAdd/kOpSub/kOpDiv/kOpMul/kOpOr/kOpAnd) fold the *previous* operand into v3
// using v45, then promote v1 into v45. A kTokBlockEnd ends the expression.
//
// We model the source-text "find logical operator" pre-scan (&&/||) by honoring
// the kTokKeyword/sub-code structure; the host's compiled `value` already gives
// the operand, so the recursion structure is preserved 1:1 over tokens.
// ===========================================================================
i32 ScriptVm::EvalExpression(u8 stopOp) {
    (void)stopOp;
    i32 acc = 0;             // v3 — the running accumulator
    i32 operand = 0;         // v2 — LOOP-CARRIED: no-operand tokens fold stale v2
    u8  pendOp = kOpOperand; // v45 — operator that folds the *next* operand
    pendingLabel_ = 0;       // dword_62E8C8 = 0 at entry/exit

    while (runnable_ && !atEnd()) {
        const ScriptToken_t& t = next();
        u8 cur = 0;          // v1 — this token's class-1 sub-code (0 for operands)

        // ---- operator tokens (class 1, operator sub-code) ----
        if (t.cls == kTokSymbol) {
            pendingLabel_ = 0;
            cur = t.sub;
            // Comparison/bitwise operators evaluate the RHS and return the
            // result immediately (the original recurses into EvaluateExpression).
            switch (t.sub) {
                case kOpEq:     return acc == EvalExpression(0);
                case kOpNe:     return acc != EvalExpression(0);
                case kOpLe:     return acc <= EvalExpression(0);
                case kOpLt:     return acc <  EvalExpression(0);
                case kOpGe:     return acc >= EvalExpression(0);
                case kOpGt:     return acc >  EvalExpression(0);
                case kOpBitOr:  return acc | EvalExpression(0);
                case kOpBitAnd: return acc & EvalExpression(0);
                // Terminators: ';' == 10 / ',' == 11 exit at the loop top,
                // ')' == 13 and ']' == 28 (kOpStop) return in the v1 ladder.
                case 10: case 11: case 13:
                case kOpStop:   return acc;
                case 12:
                    // '(' — parenthesised subexpression: recurse and fold the
                    // group's value as the operand (0x444447 -> 0x44455b).
                    operand = EvalExpression(0);
                    break;
                default:
                    // any other operator folds the stale operand (usually a
                    // no-op) and becomes the pending op (LABEL_22: v45 = v1).
                    break;
            }
        } else switch (t.cls) {
            // ---- operand tokens: load v2, fold with the pending operator ----
            case kTokFuncCall:  // class 4
                operand = host_.callUserFunction ? host_.callUserFunction(t.value) : 0;
                break;
            case kTokVariable:  // class 2
                operand = vars_.Get(t.value);
                break;
            case kTokIntLit:    // class 5
                operand = t.value;
                break;
            case kTokStrLit:    // class 7 (binary: v2 = &unk_7674E0 scratch ptr)
                operand = t.value;
                break;
            case kTokRawSym:
                // class 6 (float literal): the binary loads NO operand
                // (0x444331: cmp al,6 / jz -> fold) — the stale v2 folds.
                break;
            case kTokLabel:
                // class 0xB (0x444350): dword_62E8C8 = value, then v1 = 2 and
                // LABEL_21 seeds acc from the STALE operand; v45 becomes 2.
                pendingLabel_ = t.value;
                acc = operand;
                pendOp = kOpOperand;
                continue;
            case kTokBlockEnd:  // class 0xC: fold below, then return
                break;
            default:            // class 0: unknown -> "Unknown symbol", Finish
                finished_ = true;
                return acc;
        }

        switch (pendOp) {        // the LABEL_18 fold ladder over v45
            case kOpAdd: acc += operand; break;
            case kOpSub: acc -= operand; break;
            case kOpDiv:
                // the binary divides unguarded (x86 #DE on 0); the zero guard
                // only avoids UB on inputs that would crash it.
                if (operand != 0) acc /= operand;
                break;
            case kOpMul: acc *= operand; break;
            case kOpOr:  acc |= operand; break;
            case kOpAnd: acc &= operand; break;
            case kOpOperand: acc = operand; break;   // v45 == 2: seed
            default: break;      // no pending operator: operand not folded
        }
        pendOp = cur;            // LABEL_22: v45 = v1
        if (t.cls == kTokBlockEnd) return acc;   // v41[0] == 12 -> LABEL_23
    }
    return acc;
}

// ===========================================================================
// gilde.exe 0x444f4c — VIBE_Script_InvokeCommand
// Resolve the command by name (the compiled +2524 record in the original) and
// call its body via the host with by-pointer argument slots. The original
// switches on arg count 0..7 and passes pointers into the context arg slots;
// here the host receives the arg vector by reference so out-params work, and
// type-6 (owned-string) cleanup is the host's concern (mirrors the free loop).
// ===========================================================================
i32 ScriptVm::InvokeCommand(const std::string& name, std::vector<i32> args) {
    if (!host_.invokeCommand) return 0;
    return host_.invokeCommand(name, args);
}

// ===========================================================================
// gilde.exe 0x444bd0 — VIBE_Script_ExecuteStatement
// Reads the leading statement token and dispatches one statement. Returns true
// to keep running, false to stop (block end / exit / error). Mirrors the
// outer/inner switch ladders of the original.
// ===========================================================================
bool ScriptVm::ExecStatement() {
    pendingLabel_ = 0;       // dword_62E8C8 = 0
    const ScriptToken_t& t = next();

    switch (t.cls) {
        case kTokSymbol:
            // class 1 with sub-code 9 == exit-from-function, 10 == block pop.
            if (t.sub == kKwExit) { finished_ = true; return false; }
            if (t.sub == kKwBlockPop) return true;
            // any other bare symbol is consumed (operator without expression)
            return true;

        case kTokVariable: {
            // assignment: VIBE_Script_AssignVariable — evaluate RHS into the cell.
            // The original parses '= <expr>'; here the RHS expression follows.
            i32 rhs = EvalExpression(0);
            vars_.Set(t.value, rhs);
            return true;
        }

        case kTokFuncCall:
            // VIBE_Script_CallFunction leaf.
            if (host_.callUserFunction) host_.callUserFunction(t.value);
            return true;

        case kTokFuncDef:
            // VIBE_Script_EnterFunction — block enter; compiled scope handled by
            // the deferred compiler. Treated as a no-op scope marker here.
            return true;

        case kTokDeclare:
            // VIBE_Script_ParseDeclaration — declare a local; ensure the cell
            // exists (value already zero-initialised by the store).
            vars_.Set(t.value, vars_.Get(t.value));
            return true;

        case kTokLabel:
            // class 0xB: set pending-label and advance (original: ++cursor).
            pendingLabel_ = t.value;
            return true;

        case kTokBlockEnd:
            // class 0xC: clear runnable bit and stop this statement run.
            runnable_ = false;
            return false;

        case kTokKeyword:
            switch (t.sub) {
                case kKwReturn: {
                    // case 3: evaluate, stash return value, finish.
                    returnValue_ = EvalExpression(0);
                    finished_ = true;
                    return false;
                }
                case kKwIf: {
                    // case 4: VIBE_Script_DispatchTokenBranch. Evaluate the
                    // condition; on false, skip the guarded statement (the next
                    // token), mirroring the branch dispatcher's skip.
                    i32 cond = EvalExpression(0);
                    if (!cond && !atEnd()) (void)next();   // skip guarded stmt
                    return true;
                }
                case kKwWhile:
                case kKwFor:
                    // Loop forms compile to a re-runnable statement; modeled as
                    // mode-loop so Run() re-enters. (Full loop compile deferred.)
                    stmtMode_ = 2;
                    return true;
                case kKwInclude:
                    return true;  // VIBE_Script_ParseInclude (compile-time)
                case kKwModeA:    stmtMode_ = 1; return true;
                case kKwModeB:    stmtMode_ = 0; return true;
                case kKwModeLoop: stmtMode_ = 2; return true;
                default:
                    // "Keyword is not surported!" path -> stop.
                    finished_ = true;
                    return false;
            }

        default:
            // Unknown symbol -> error/stop (ReportError + Finish).
            finished_ = true;
            return false;
    }
}

// ===========================================================================
// Statement run loop (the do/while in VIBE_Script_Step): execute statements
// while runnable and not finished. stmtMode==2 re-runs (loop) — bounded here by
// program length since the synthetic stream is finite.
// ===========================================================================
void ScriptVm::Run() {
    while (runnable_ && !finished_ && !atEnd()) {
        if (!ExecStatement()) break;
    }
}

// ===========================================================================
// Context-table scanners (faithful 1:1 over the 128-slot table).
// ===========================================================================

// gilde.exe 0x445250 — VIBE_Script_StepAllActive
int StepAllActive(std::vector<ScriptSlot>& slots,
                  const std::function<int(ScriptSlot&)>& step) {
    int result = 0;
    for (auto& s : slots) {
        if (s.runFlags & kRunFlagRunnable) {     // (+164 & 1)
            if (step) result = step(s);
        }
    }
    return result;
}

// gilde.exe 0x445d40 — VIBE_Script_CountActive
int CountActive(const std::vector<ScriptSlot>& slots) {
    int n = 0;
    for (const auto& s : slots) {
        if (s.inUse || (s.runFlags & kRunFlagRunnable)) ++n;  // (+0) || (+164&1)
    }
    return n;
}

// gilde.exe 0x445370 — VIBE_Script_FreeFinished
int FreeFinished(std::vector<ScriptSlot>& slots,
                 const std::function<void(ScriptSlot&)>& destroy) {
    int n = 0;
    for (auto& s : slots) {
        if (s.inUse || s.handle != -1) {          // (+0) || (+128 != -1)
            if (destroy) destroy(s);
            ++n;
        }
    }
    return n;
}

} // namespace guild::sim
