#include "sim/script_compiler.h"
#include <cstring>
#include <cstdlib>

namespace guild::sim {

// ===========================================================================
// gilde.exe 0x4435d0 — VIBE_Script_CompileBlock (declaration / brace pass)
//
// The original allocates the var/func tables, then loops NextToken:
//   * class 8 (type keyword) at brace-depth 0 -> ParseDeclaration_42d88, which
//     reads the name and either declares a variable ('=' / '[' / ';') or, for a
//     function signature '(' ... ')' '{', registers a function and stores the
//     body source pointer (+32) via ParseSymbolName.
//   * class 1 sub 8 ('{') -> ++depth;  sub 9 ('}') -> --depth.
//   * include keyword -> ParseInclude.
//   * loop until class 12 (end). Brace imbalance -> syntax error.
//
// We compile in one lexing pass. Because the lexer classifies a name as a
// variable only *after* it is declared, we register declarations as we see them,
// which matches the original (declarations precede uses in .esc scripts).
// ===========================================================================

// Read an identifier lexeme directly from source at `pos` (raw, unclassified):
// the run of non-space, non-operator characters. Returns the lexeme and the
// cursor just past it. Mirrors NextToken's lexeme extraction but without the
// symbol-table classification (used for names being *declared*).
static std::string RawIdent(const std::string& s, int& pos) {
    while (pos < (int)s.size() &&
           (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
    int start = pos;
    while (pos < (int)s.size()) {
        char c = s[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        int probe = 0;
        if (MatchOperatorPublic(s, pos, probe)) break;
        ++pos;
    }
    return s.substr(start, pos - start);
}

// Skip whitespace; return the next char (or 0 at end) without consuming.
static char PeekNonSpace(const std::string& s, int& pos) {
    while (pos < (int)s.size() &&
           (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
    return pos < (int)s.size() ? s[pos] : 0;
}

CompiledScript CompileScript(const std::string& source,
                             const std::vector<std::string>& commandNames) {
    CompiledScript cs;
    cs.source = source;

    const std::string& s = cs.source;
    int pos = 0;
    int depth = 0;
    (void)commandNames;

    auto isTypeWord = [](const std::string& w) -> u8 {
        if (w == "int") return kVarInt;
        if (w == "float") return kVarFloat;
        if (w == "void") return kVarVoid;
        if (w == "string") return kVarString;
        if (w == "byte" || w == "char") return kVarByte;
        return 0;
    };

    while (pos < (int)s.size()) {
        char c = PeekNonSpace(s, pos);
        if (c == 0) break;
        if (c == '{') { ++depth; ++pos; continue; }
        if (c == '}') { --depth; ++pos; if (depth < 0) { cs.error = "Syntax Error: too many '}'"; return cs; } continue; }

        // A leading identifier: could be a type keyword (declaration) only at the
        // top level (depth 0). Inside a function body, statements are executed,
        // not compiled, so we just skip to the next statement boundary.
        int save = pos;
        std::string word = RawIdent(s, pos);
        if (word.empty()) { ++pos; continue; }

        u8 ty = isTypeWord(word);
        if (ty && depth == 0) {
            // Declaration: read the name.
            std::string name = RawIdent(s, pos);
            char nxt = PeekNonSpace(s, pos);
            if (nxt == '(') {
                // Function definition: <type> name ( params ) { body }
                ++pos; // consume '('
                std::vector<u8> ptypes;
                std::vector<std::string> pnames;
                // params: zero or more "type name" separated, until ')'
                while (true) {
                    char pk = PeekNonSpace(s, pos);
                    if (pk == ')' || pk == 0) { if (pk == ')') ++pos; break; }
                    std::string pty = RawIdent(s, pos);
                    u8 pt = isTypeWord(pty);
                    std::string pn = RawIdent(s, pos);
                    if (pt) { ptypes.push_back(pt); pnames.push_back(pn); }
                    char sep = PeekNonSpace(s, pos);
                    if (sep == ',') ++pos;
                }
                // body opens at the next '{'
                char b = PeekNonSpace(s, pos);
                int body = -1;
                if (b == '{') body = pos;
                int fi = cs.symbols.DefineFunction(name, body);
                cs.symbols.funcs()[fi].paramTypes = ptypes;
                cs.symbols.funcs()[fi].paramNames = pnames;
                // skip the body block to balance braces
                if (b == '{') {
                    ++pos; int d = 1;
                    while (pos < (int)s.size() && d > 0) {
                        if (s[pos] == '{') ++d;
                        else if (s[pos] == '}') --d;
                        ++pos;
                    }
                    if (d > 0) { cs.error = "Syntax Error: Wrong braces-count"; return cs; }
                }
            } else if (nxt == '[') {
                // array declaration: <type> name [ size ]
                ++pos;
                std::string num = RawIdent(s, pos);
                int sz = num.empty() ? 1 : (int)strtol(num.c_str(), nullptr, 10);
                if (sz < 1) sz = 1;
                cs.symbols.DefineVariable(name, ty, sz);
                if (PeekNonSpace(s, pos) == ']') ++pos;
                if (PeekNonSpace(s, pos) == ';') ++pos;
            } else {
                // scalar declaration (optionally with '= value;')
                cs.symbols.DefineVariable(name, ty, 1);
                // consume up to ';'
                while (pos < (int)s.size() && s[pos] != ';' && s[pos] != '\n') ++pos;
                if (pos < (int)s.size() && s[pos] == ';') ++pos;
            }
            continue;
        }

        // Not a top-level declaration (or inside a function body): `word` has
        // already been consumed; just continue. Brace depth is tracked solely by
        // the explicit '{' / '}' checks above, exactly as CompileBlock does
        // (it only special-cases declarations and braces; all other tokens are
        // advanced over by NextToken). `save` is retained only for declarations.
        (void)save;
    }

    if (depth != 0) { cs.error = "Syntax Error: Wrong braces-count"; return cs; }
    cs.ok = true;
    return cs;
}

// ===========================================================================
// ScriptExecutor — the source-text-driven statement interpreter.
// ===========================================================================

i32 ScriptExecutor::ReadVar(int idx, int elemIndex) const {
    if (idx < 0 || idx >= (int)cs_.symbols.vars().size()) return 0;
    const ScriptVar& v = cs_.symbols.vars()[idx];
    int cell = v.storage + elemIndex;
    const auto& st = cs_.symbols.storage();
    return (cell >= 0 && cell < (int)st.size()) ? st[cell] : 0;
}

void ScriptExecutor::WriteVar(int idx, int elemIndex, i32 value) {
    if (idx < 0 || idx >= (int)cs_.symbols.vars().size()) return;
    const ScriptVar& v = cs_.symbols.vars()[idx];
    int cell = v.storage + elemIndex;
    auto& st = cs_.symbols.storage();
    if (cell >= 0 && cell < (int)st.size()) st[cell] = value;
}

// Collect a command call's parenthesised argument list (the `(a, b, ...)` after
// a kTokFuncCall name), evaluating each argument expression. Shared by the value-
// position (EvalExpression) and statement-position (ExecStatement) call paths so
// both pass the full evaluated arg list to invokeCommand. Leaves the cursor just
// past the closing ')'. A call with no '(' yields an empty arg list (the cursor
// is untouched). Mirrors VIBE_Script_InvokeCommand's argument marshalling.
std::vector<i32> ScriptExecutor::CollectCallArgs() {
    std::vector<i32> args;
    int look = lexer_.cursor();
    char b = PeekNonSpace(cs_.source, look);
    if (b == '(') {
        lexer_.Next();                   // consume '('
        while (true) {
            int before = lexer_.cursor();
            char pk = PeekNonSpace(cs_.source, before);
            if (pk == ')' || pk == 0) { lexer_.setCursor(before + (pk==')'?1:0)); break; }
            lastTerm_ = 0;
            i32 a = EvalExpression();
            args.push_back(a);
            // EvalExpression consumes its own terminator (')' or ','); continue
            // the arg list only on a comma.
            if (lastTerm_ == ',') continue;
            break;
        }
    }
    return args;
}

// gilde.exe 0x443ff0 — VIBE_Script_EvaluateExpression (operator-precedence fold).
// Mirrors ScriptVm::EvalExpression but over the live source lexer: accumulator
// folding for +/-/*///|/&, comparison/bitwise ops short-circuit-return.
i32 ScriptExecutor::EvalExpression() {
    i32 acc = 0;
    u8 pendOp = kOpOperand;

    while (runnable_) {
        ScriptToken_t t = lexer_.Next();

        if (t.cls == kTokSymbol) {
            switch (t.sub) {
                case kOpEq:     return acc == EvalExpression();
                case kOpNe:     return acc != EvalExpression();
                case kOpLe:     return acc <= EvalExpression();
                case kOpLt:     return acc <  EvalExpression();
                case kOpGe:     return acc >= EvalExpression();
                case kOpGt:     return acc >  EvalExpression();
                case kOpBitOr:  return acc |  EvalExpression();
                case kOpBitAnd: return acc &  EvalExpression();
                // Expression terminators, by recovered operator sub-code
                // (byte_767958 stride 5): ';'==10, ','==11, ')'==13, ']'==28
                // (kOpStop). EvaluateExpression's loop is bounded by these /
                // class-12 end; each ends the current fold and records which
                // delimiter we stopped on (CollectCallArgs continues on ',').
                case 10:        lastTerm_ = ';'; return acc;  // ';' statement sep
                case 11:        lastTerm_ = ','; return acc;  // ',' arg separator
                case 13:        lastTerm_ = ')'; return acc;  // ')' group/call end
                case kOpStop:   lastTerm_ = ']'; return acc;  // ']' index end (28)
                case kOpAdd: case kOpSub: case kOpDiv:
                case kOpMul: case kOpOr:  case kOpAnd:
                    pendOp = t.sub;
                    continue;
                default:
                    continue;
            }
        }

        i32 operand = 0;
        switch (t.cls) {
            case kTokVariable: {
                // Variable read with an optional '[' index ']' subscript. The
                // lexer emits `name` `[` `idx` `]` as separate tokens (bracket ==
                // operator sub-code 27/28, shared with paren), exactly as the
                // LHS AssignVariable path already consumes them; here on the read
                // side EvaluateExpression resolves the element via storage+index.
                // Without this, `[`, idx and `]` would re-enter the fold and
                // corrupt the accumulator (so array reads in argument position —
                // SetEmitterSize(emitter[0], ...) — would pass a wrong handle).
                int elemIndex = 0;
                int save = lexer_.cursor();
                ScriptToken_t nx = lexer_.Next();
                if (nx.cls == kTokSymbol && nx.sub == 27) {   // '['
                    elemIndex = EvalExpression();             // up to ']' (sub 28)
                } else {
                    lexer_.setCursor(save);                   // no subscript: rewind
                }
                operand = ReadVar(t.value, elemIndex);
                break;
            }
            case kTokIntLit:   operand = t.value; break;
            case kTokRawSym:   operand = t.value; break;
            case kTokStrLit:   operand = 0; break;
            case kTokFuncCall: {                       // command used as value
                // A command invoked in VALUE position (e.g.
                // `emitter[0]=CreateEmitter(1,0,...)`) must collect its `(args)`
                // exactly like the statement-position path, else the args leak
                // back into the fold and the command runs argument-less. Mirrors
                // VIBE_Script_EvaluateExpression's class-4 sub-call handling.
                std::vector<i32> args = CollectCallArgs();
                operand = host_.invokeCommand ? host_.invokeCommand(t.text, args) : 0;
                break;
            }
            case kTokBlockEnd: return acc;
            default: return acc;
        }

        switch (pendOp) {
            case kOpAdd: acc += operand; break;
            case kOpSub: acc -= operand; break;
            case kOpDiv: if (operand) acc /= operand; break;
            case kOpMul: acc *= operand; break;
            case kOpOr:  acc |= operand; break;
            case kOpAnd: acc &= operand; break;
            default:     acc = operand; break;
        }
        pendOp = kOpOperand;
    }
    return acc;
}

void ScriptExecutor::SkipBraceBlock() {
    // Advance the lexer's cursor over a balanced { } block.
    int pos = lexer_.cursor();
    const std::string& s = cs_.source;
    while (pos < (int)s.size() && s[pos] != '{') ++pos;
    if (pos >= (int)s.size()) { lexer_.setCursor(pos); return; }
    int d = 0;
    while (pos < (int)s.size()) {
        if (s[pos] == '{') ++d;
        else if (s[pos] == '}') { --d; if (d == 0) { ++pos; break; } }
        ++pos;
    }
    lexer_.setCursor(pos);
}

void ScriptExecutor::SkipToSemicolon() {
    int pos = lexer_.cursor();
    const std::string& s = cs_.source;
    while (pos < (int)s.size() && s[pos] != ';') ++pos;
    if (pos < (int)s.size()) ++pos;
    lexer_.setCursor(pos);
}

// gilde.exe 0x44259c (while, type 1) / 0x4427b4 (for, type 2).
// The original reads '(' (sub 12 in its table), evaluates the condition, records
// a loop frame (restart cursor, condition, type, has-brace), and on a false
// condition skips the body. On a true condition it leaves the cursor at the body
// start and pushes the frame so the closing '}'/'; rewinds to restartCursor.
// Advance the cursor past the next '{' and bump the brace-nesting depth. Mirrors
// the original's "enter block" transition (dword_62E8E0 ++ when NextToken yields
// a '{' / class-1 sub-8). Returns true if a '{' was consumed.
bool ScriptExecutor::StepIntoBlock() {
    int pos = lexer_.cursor();
    while (pos < (int)cs_.source.size() && cs_.source[pos] != '{') ++pos;
    if (pos >= (int)cs_.source.size()) { lexer_.setCursor(pos); return false; }
    ++pos;                       // consume '{'
    lexer_.setCursor(pos);
    ++braceDepth_;               // dword_62E8E0 ++
    return true;
}

void ScriptExecutor::EnterLoop(u8 type) {
    // Expect '(' then condition expression up to ')'.
    int restart = lexer_.cursor();   // re-evaluate the condition from here
    ScriptToken_t open = lexer_.Next();
    (void)open;                      // '(' (operator sub 27)
    i32 cond = EvalExpression();

    LoopFrame f;
    f.restartCursor = restart;
    f.condition = cond;
    f.type = type;

    // Peek whether a '{' block follows.
    int look = lexer_.cursor();
    char b = PeekNonSpace(cs_.source, look);
    f.hasBrace = (b == '{') ? 1 : 0;

    if (!cond) {
        // false: skip the body (no depth change — we never entered it).
        if (f.hasBrace) SkipBraceBlock();
        else SkipToSemicolon();
        return;
    }
    if (f.hasBrace) {
        StepIntoBlock();             // consume '{', braceDepth_ ++
        f.bodyDepth = braceDepth_;   // body runs at this depth; matching '}' here
    } else {
        f.bodyDepth = braceDepth_ + 1;  // single-stmt body: a virtual one-deeper
    }
    loopStack_.push_back(f);
}

// gilde.exe 0x442ac8 — VIBE_Script_DispatchTokenBranch (the `if` body executes
// only when the loop frame's condition slot is set; otherwise the braced block
// or statement is skipped). We model `if (cond) <stmt-or-block>` directly: the
// condition was just evaluated by ExecStatement; on false, skip.
void ScriptExecutor::DispatchBranch() {
    int look = lexer_.cursor();
    char b = PeekNonSpace(cs_.source, look);
    if (b == '{') SkipBraceBlock();
    else SkipToSemicolon();
}

bool ScriptExecutor::ExecStatement() {
    if (++steps_ > budget_) { finished_ = true; return false; }
    ScriptToken_t t = lexer_.Next();

    switch (t.cls) {
        case kTokSymbol:
            // block markers: 8 '{', 9 '}', 12 end, 13 ';'
            if (t.sub == 8) {        // '{' — enter a (bare) block: deepen nesting
                ++braceDepth_;       // dword_62E8E0 ++
                return true;
            }
            if (t.sub == 9) {        // '}' — leave a block: shallow the nesting
                if (braceDepth_ > 0) --braceDepth_;   // dword_62E8E0 --
                // A loop's closing brace is the one that brings the depth back to
                // (frame.bodyDepth - 1). An inner block's '}' (e.g. a nested if)
                // lands at a deeper level and must NOT touch the loop frame — that
                // was the old bug that prematurely re-tested the while.
                if (!loopStack_.empty() &&
                    braceDepth_ + 1 == loopStack_.back().bodyDepth) {
                    LoopFrame f = loopStack_.back();
                    loopStack_.pop_back();
                    if (f.type == 1) {       // while: re-test from restartCursor
                        lexer_.setCursor(f.restartCursor);
                        EnterLoop(1);        // re-evaluate the condition + re-enter
                    }
                    // for/once (type 2): already popped; fall through past block.
                }
                return true;
            }
            return true;  // ';', operators: no-op at statement position

        case kTokVariable: {
            // gilde.exe 0x4445bc — VIBE_Script_AssignVariable. A variable in
            // statement position is an assignment. The recovered assignment
            // operators are exactly: '=' (store), '++' (increment), '--'
            // (decrement) — there is NO compound '+='/'-=' in this VM. An optional
            // '[' index ']' subscript precedes the operator (array element).
            int elemIndex = 0;
            ScriptToken_t op = lexer_.Next();
            if (op.cls == kTokSymbol && op.sub == 27) {   // '['
                elemIndex = EvalExpression();             // up to ']'
                op = lexer_.Next();                       // the assignment operator
            }
            if (op.cls == kTokSymbol && op.text == "++") {        // ++ : increment
                WriteVar(t.value, elemIndex, ReadVar(t.value, elemIndex) + 1);
                int p = lexer_.cursor(); char sc = PeekNonSpace(cs_.source, p);
                if (sc == ';') lexer_.setCursor(p + 1);
            } else if (op.cls == kTokSymbol && op.text == "--") { // -- : decrement
                WriteVar(t.value, elemIndex, ReadVar(t.value, elemIndex) - 1);
                int p = lexer_.cursor(); char sc = PeekNonSpace(cs_.source, p);
                if (sc == ';') lexer_.setCursor(p + 1);
            } else {                                              // '=' : store rhs
                // op is '=' (sub-code 2); evaluate the right-hand expression up to
                // the ';' / terminator and store it.
                i32 rhs = EvalExpression();
                WriteVar(t.value, elemIndex, rhs);
            }
            return true;
        }

        case kTokFuncCall: {
            // command call: collect args up to the closing ')'/';'
            std::vector<i32> args = CollectCallArgs();
            if (host_.invokeCommand) host_.invokeCommand(t.text, args);
            // consume trailing ';'
            int p = lexer_.cursor();
            char sc = PeekNonSpace(cs_.source, p);
            if (sc == ';') lexer_.setCursor(p + 1);
            return true;
        }

        case kTokFuncDef:
            // user function name at statement position: call it (EnterFunction).
            if (host_.callUserFunction) host_.callUserFunction(t.value);
            SkipToSemicolon();
            return true;

        case kTokDeclare:
            // a local declaration encountered during execution: skip to ';'.
            SkipToSemicolon();
            return true;

        case kTokKeyword:
            // gilde.exe VIBE_Script_ExecuteStatement (0x444bd0) class-10 switch on
            // the keyword sub-code (byte_767450 stride 16, subcode == offset/16):
            //   1 while  -> ParseWhileLoop (0x44259c)
            //   2 if     -> "ParseForLoop" (0x4427b4, IDA-misnamed; THIS is the
            //              if-statement handler: reads '(' cond ')', then a '{'
            //              braced body or a single statement; skips it when false)
            //   3 return -> EvaluateExpression + Finish
            //   4 else   -> DispatchTokenBranch (0x442ac8, the branch dispatcher)
            //   5 #include-> ParseInclude
            //   6/7/8    -> single/normal/multi step (ctx+2564)
            // script_vm.h's enum names are mislabeled vs the binary spellings:
            // kKwFor==2 is actually `if`; kKwIf==4 is the `else`/branch code. We
            // dispatch by the binary sub-code, not by the (mislabeled) name.
            switch (t.sub) {
                case kKwWhile:  EnterLoop(1); return true;   // 1: while
                case kKwFor: {                               // 2: if-statement
                    // if ( cond ) <stmt/block>   (0x4427b4)
                    lexer_.Next();                     // '(' (sub 12)
                    i32 cond = EvalExpression();       // up to ')'
                    if (!cond) {
                        DispatchBranch();              // skip the braced body / stmt
                    } else {
                        // true: step into the body. A braced body deepens the brace
                        // nesting (so its '}' returns to THIS depth, not the
                        // enclosing loop's). A single-statement body just runs next.
                        int look = lexer_.cursor();
                        char b = PeekNonSpace(cs_.source, look);
                        if (b == '{') StepIntoBlock();
                    }
                    return true;
                }
                case kKwReturn:                              // 3: return
                    returnValue_ = EvalExpression();
                    finished_ = true;
                    return false;
                case kKwIf:                                  // 4: else / branch
                    // DispatchTokenBranch (0x442ac8): skip the following braced
                    // block or single statement (the else/branch dispatch path).
                    DispatchBranch();
                    return true;
                case kKwInclude: SkipToSemicolon(); return true;  // 5: #include
                case kKwModeA:    stmtMode_ = 1; return true;
                case kKwModeB:    stmtMode_ = 0; return true;
                case kKwModeLoop: stmtMode_ = 2; return true;
                default: finished_ = true; return false;
            }

        case kTokBlockEnd:
            runnable_ = false;
            return false;

        default:   // unknown symbol
            // tolerate stray tokens (skip) rather than aborting the whole run
            return true;
    }
}

i32 ScriptExecutor::Run(int entryCursor, int stepBudget) {
    budget_ = stepBudget;
    steps_ = 0;
    lexer_.setCursor(entryCursor);
    runnable_ = true;
    finished_ = false;
    while (runnable_ && !finished_ && !lexer_.atEnd()) {
        if (!ExecStatement()) break;
    }
    return returnValue_;
}

} // namespace guild::sim
