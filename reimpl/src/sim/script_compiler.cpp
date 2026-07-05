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

// gilde.exe 0x443ff0 — VIBE_Script_EvaluateExpression (operator-precedence fold)
// over the live source lexer. Register map: acc == v3, operand == v2 (LOOP-
// CARRIED — tokens that load no operand fold the stale value), pendOp == v45
// (the pending fold operator; 2 == kOpOperand seeds), cur == v1 (this token's
// class-1 sub-code, 0 for operands). Every iteration ends in the LABEL_18 fold
// ladder followed by `v45 = v1` (LABEL_22); comparison operators return their
// boolean before the fold.
i32 ScriptExecutor::EvalExpression() {
    i32 acc = 0;             // v3
    i32 operand = 0;         // v2 — carried across iterations
    u8  pendOp = kOpOperand; // v45 = 2

    while (runnable_) {
        ScriptToken_t t = lexer_.Next();
        u8 cur = 0;          // v1

        if (t.cls == kTokSymbol) {
            cur = t.sub;
            switch (t.sub) {
                // Comparison / logical operators recurse for the RHS and return
                // the result immediately (the v1 ladder at 0x444459..0x4445af).
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
                // (kOpStop). A ';' additionally pops a pending single-statement
                // loop frame (0x443ff0 LABEL_23, the v1==10 block).
                case 10:        lastTerm_ = ';'; PopFrameAtSemicolon(); return acc;
                case 11:        lastTerm_ = ','; return acc;  // ',' arg separator
                case 13:        lastTerm_ = ')'; return acc;  // ')' group/call end
                case kOpStop:   lastTerm_ = ']'; return acc;  // ']' index end (28)
                case 12:
                    // '(' — parenthesised subexpression: recurse and fold the
                    // group's value as this iteration's operand (0x444447 ->
                    // 0x44455b: v1==12 -> v2 = EvaluateExpression(v43)).
                    operand = EvalExpression();
                    break;
                default:
                    // Any other operator (+ - * / | & ++ -- '[' ...) folds the
                    // stale operand via the pending op (usually a no-op), then
                    // becomes the new pending operator (LABEL_22: v45 = v1).
                    break;
            }
        } else switch (t.cls) {
            case kTokVariable: {
                // Variable read with an optional '[' index ']' subscript: the
                // original reads the next token and recurses for the index on
                // (1,27), else restores the cursor (0x44425a). The element index
                // is bounds-checked against the var record's +36 count — an out-
                // of-range index reports "Array index out of bounce" and
                // finishes the script (0x4442a0 region), returning 0.
                int elemIndex = 0;
                int save = lexer_.cursor();
                ScriptToken_t nx = lexer_.Next();
                if (nx.cls == kTokSymbol && nx.sub == 27) {   // '['
                    elemIndex = EvalExpression();             // up to ']' (sub 28)
                } else {
                    lexer_.setCursor(save);                   // no subscript: rewind
                }
                if (t.value >= 0 && t.value < (int)cs_.symbols.vars().size()) {
                    const ScriptVar& v = cs_.symbols.vars()[(std::size_t)t.value];
                    if (elemIndex > v.count - 1 || elemIndex < 0) {
                        finished_ = true;   // ReportError + VIBE_Script_Finish
                        return 0;
                    }
                }
                operand = ReadVar(t.value, elemIndex);
                break;
            }
            case kTokIntLit:   operand = t.value; break;   // class 5: v2 = value
            case kTokRawSym:
                // class 6 (float literal): the binary evaluator loads NO operand
                // (0x444331: cmp al,6 / jz -> fold ladder) — the fold uses the
                // stale v2. Float literals therefore contribute nothing here.
                break;
            case kTokStrLit:
                // class 7: v2 = &unk_7674E0 (pointer to the copied literal in
                // the shared string scratch). No flat address space here —
                // string operands read as 0 (host boundary).
                operand = 0;
                break;
            case kTokFuncCall: {                       // class 4: command as value
                // v2 = VIBE_Script_CallFunction(record) — collect the `(args)`
                // and invoke, the result is the operand.
                std::vector<i32> args = CollectCallArgs();
                operand = host_.invokeCommand ? host_.invokeCommand(t.text, args) : 0;
                break;
            }
            case kTokBlockEnd:
                // class 12 (end of source): fold below, then return
                // (LABEL_22 -> `if (v41[0] == 12) goto LABEL_23`).
                break;
            default:
                // class 3 (function name) / 8 (declaration) load no operand in
                // the binary (no case in the class switch); class 0 (unknown
                // symbol) reports + finishes there — tolerated here because the
                // standalone lexer resolves fewer names than the live engine
                // (include refs / not-yet-registered commands), so a faithful
                // kill would end scripts the binary would run.
                break;
        }

        switch (pendOp) {        // the LABEL_18 fold ladder over v45
            case kOpAdd: acc += operand; break;                    // v45 == 4
            case kOpSub: acc -= operand; break;                    // v45 == 6
            case kOpDiv:
                // v45 == 20: the binary divides unguarded (x86 #DE on 0); the
                // zero guard only avoids UB on inputs that would crash it.
                if (operand != 0) acc /= operand;
                break;
            case kOpMul: acc *= operand; break;                    // v45 == 21
            case kOpOr:  acc |= operand; break;                    // v45 == 24
            case kOpAnd: acc &= operand; break;                    // v45 == 25
            case kOpOperand: acc = operand; break;                 // v45 == 2 seed
            default: break;      // no pending operator: operand not folded
        }
        pendOp = cur;            // LABEL_22: v45 = v1
        if (t.cls == kTokBlockEnd) return acc;
    }
    return acc;
}

// gilde.exe 0x4416c4 — VIBE_Script_SkipBraceBlock(1): raw character scan from
// the cursor; '{' increments the depth, a '}' that drops it to <= 0 stops the
// scan with the cursor just PAST that '}'.
void ScriptExecutor::SkipBraceBlock() {
    int pos = lexer_.cursor();
    const std::string& s = cs_.source;
    int d = 0;
    while (pos < (int)s.size()) {
        if (s[pos] == '{') {
            ++d;
        } else if (s[pos] == '}' && --d <= 0) {
            ++pos;
            break;
        }
        ++pos;
    }
    lexer_.setCursor(pos);
}

// gilde.exe 0x441660 — VIBE_Script_SkipToSemicolon: scan to the next ';' and
// leave the cursor just past it.
void ScriptExecutor::SkipToSemicolon() {
    int pos = lexer_.cursor();
    const std::string& s = cs_.source;
    while (pos < (int)s.size() && s[pos] != ';') ++pos;
    if (pos < (int)s.size()) ++pos;
    lexer_.setCursor(pos);
}

// gilde.exe 0x444bd0 LABEL_6 / 0x443ff0 LABEL_23 — the ';' frame pop: when the
// frame count is > 1 and the top frame is a single-statement (noBrace) frame
// with a live restart cursor, pop it; a popped while frame (type 1) rewinds the
// cursor to its restart so the statement loop re-reads the `while` keyword.
void ScriptExecutor::PopFrameAtSemicolon() {
    if (frameCount_ > 1) {
        const LoopFrame& f = frames_[frameCount_ - 1];
        if (f.restartCursor && f.noBrace) {
            --frameCount_;
            if (frames_[frameCount_].type == 1)
                lexer_.setCursor(frames_[frameCount_].restartCursor);
        }
    }
}

// gilde.exe 0x44259c — VIBE_Script_ParseWhileLoop (type 1) and
// gilde.exe 0x4427b4 — VIBE_Script_ParseForLoop (type 2; IDA-misnamed — this is
// the `if` statement handler). Both: require '(' (1,12), evaluate the condition,
// build a frame slot, read the next token to classify a '{ }' body vs a single
// statement, and push (count capped at 8, "Too many looplevels").
//   while, false: skip the '{ } ' block (from the '{') or to the ';' — no push.
//   while, true : slot is memset AFTER the condition write (condition slot
//                 stays 0), type 1, restart recorded, pushed.
//   if,   any   : slot memset first, type 2 + restart + condition, pushed.
//   if,   false : skip the body but leave the cursor ON the closing '}' / ';'
//                 (the --cursor at 0x442958/0x442988) so the statement loop pops
//                 the frame through the normal '}' / ';' paths.
void ScriptExecutor::EnterLoop(u8 type, int restartCursor) {
    ScriptToken_t open = lexer_.Next();
    if (!(open.cls == kTokSymbol && open.sub == 12)) {
        // "Syntax Error: missing '('..." — ReportError only, no Finish.
        return;
    }
    i32 cond = EvalExpression();     // consumes through the matching ')'

    if (type == 1) {
        // ParseWhileLoop writes the condition into the un-pushed slot first —
        // visible to the else off-by-one read even when the loop is not taken.
        frames_[frameCount_].condition = cond;
        if (!cond) {
            int save = lexer_.cursor();
            ScriptToken_t nx = lexer_.Next();
            lexer_.setCursor(save);
            if (nx.cls == kTokSymbol && nx.sub == 8) SkipBraceBlock();
            else SkipToSemicolon();
            return;
        }
        frames_[frameCount_] = LoopFrame{};   // SetGrayColorThunk(0,12,slot)
        frames_[frameCount_].type = 1;
        frames_[frameCount_].restartCursor = restartCursor;
        int save = lexer_.cursor();
        ScriptToken_t nx = lexer_.Next();
        if (nx.cls == kTokSymbol && nx.sub == 8) {
            frames_[frameCount_].noBrace = 0;         // '{' consumed
        } else {
            frames_[frameCount_].noBrace = 1;
            lexer_.setCursor(save);                    // cursor restored
        }
        ++frameCount_;
        if (frameCount_ > 8) --frameCount_;            // "Too many looplevels!"
        return;
    }

    // type 2: the `if` handler (0x4427b4).
    frames_[frameCount_] = LoopFrame{};
    frames_[frameCount_].type = 2;
    frames_[frameCount_].restartCursor = restartCursor;
    frames_[frameCount_].condition = cond;
    int save = lexer_.cursor();
    ScriptToken_t nx = lexer_.Next();
    bool brace = (nx.cls == kTokSymbol && nx.sub == 8);
    if (brace) {
        frames_[frameCount_].noBrace = 0;              // '{' consumed
    } else {
        frames_[frameCount_].noBrace = 1;
        lexer_.setCursor(save);
    }
    ++frameCount_;
    if (frameCount_ > 8) --frameCount_;
    if (cond)
        return;                                        // body executes
    if (brace) {
        lexer_.setCursor(save);                        // back to the '{'
        SkipBraceBlock();                              // past the matching '}'
        int pos = lexer_.cursor();
        if (pos > 0 && cs_.source[pos - 1] == '}')
            lexer_.setCursor(pos - 1);                 // --cursor: leave ON '}'
    } else {
        SkipToSemicolon();
        int pos = lexer_.cursor();
        if (pos > 0 && cs_.source[pos - 1] == ';')
            lexer_.setCursor(pos - 1);                 // --cursor: leave ON ';'
    }
}

// gilde.exe 0x442ac8 — VIBE_Script_DispatchTokenBranch (the `else` keyword).
// Reads the condition of frame slot [count+1] — one ABOVE the slot the
// preceding '}' popped (an off-by-one in the original, preserved: 0x442ae7
// `cmp [edx + 12*(count+1) + 52], 0`). Slot contents persist after pops, so
// this sees stale frame data (or 0 for never-written slots — the deterministic
// stand-in for the original's uninitialised heap). Condition zero -> return,
// the else body executes; nonzero -> skip: SkipBraceBlock only when the next
// token is the keyword `else` (10,4), otherwise SkipToSemicolon (one statement).
void ScriptExecutor::DispatchBranch() {
    if (frames_[frameCount_ + 1].condition == 0)
        return;                                        // body executes
    ScriptToken_t nx = lexer_.Next();
    if (nx.cls == kTokKeyword && nx.sub == 4) SkipBraceBlock();
    else SkipToSemicolon();
}

// gilde.exe 0x444bd0 — VIBE_Script_ExecuteStatement. One statement: class-1
// tokens loop at the top ('}' pops a frame, ';' pops a pending single-statement
// frame); the first non-symbol token dispatches by class, keywords by sub-code.
bool ScriptExecutor::ExecStatement() {
    if (++steps_ > budget_) { finished_ = true; return false; }
    int stmtStart = lexer_.cursor();   // keyword restart anchor (cursor-strlen(kw))
    ScriptToken_t t = lexer_.Next();

    switch (t.cls) {
        case kTokSymbol:
            if (t.sub == 9) {
                // '}' — block pop (the LOBYTE(v21[0]) == 9 branch): with no
                // frame, the binary runs HandleExitKeyword / Finish (function
                // return — the host owns call frames here). Otherwise pop the
                // top frame; a popped while frame rewinds to its restart cursor
                // so the `while` keyword is re-dispatched (the re-test).
                if (frameCount_ > 0) {
                    --frameCount_;
                    const LoopFrame& f = frames_[frameCount_];
                    if (f.restartCursor && f.type == 1)
                        lexer_.setCursor(f.restartCursor);
                }
                return true;
            }
            if (t.sub == 10) {
                // ';' — LABEL_6: pops a pending single-statement frame.
                PopFrameAtSemicolon();
                return true;
            }
            return true;  // '{' and other operators: no-op at statement position

        case kTokVariable: {
            // gilde.exe 0x4445bc — VIBE_Script_AssignVariable. The token after
            // the variable must be class 1; an optional '[' index ']' subscript
            // (sub 27) precedes the operator. Operators by sub-code: 3 == '++',
            // 5 == '--', 2 == '=' (anything else: no-op return). There is NO
            // compound '+='/'-=' in this VM. Trailing ';' is NOT consumed here —
            // the statement loop reads it (and runs the ';' frame pop).
            int elemIndex = 0;
            ScriptToken_t op = lexer_.Next();
            if (op.cls != kTokSymbol) return true;
            if (op.sub == 27) {                           // '['
                elemIndex = EvalExpression();             // up to ']'
                op = lexer_.Next();                       // the assignment operator
                if (op.cls != kTokSymbol) return true;
            }
            if (op.sub == 3) {                                    // '++'
                WriteVar(t.value, elemIndex, ReadVar(t.value, elemIndex) + 1);
            } else if (op.sub == 5) {                             // '--'
                WriteVar(t.value, elemIndex, ReadVar(t.value, elemIndex) - 1);
            } else if (op.sub == 2) {                             // '=' store rhs
                i32 rhs = EvalExpression();
                WriteVar(t.value, elemIndex, rhs);
            }
            return true;
        }

        case kTokFuncCall: {
            // class 4 — VIBE_Script_CallFunction: collect the '(args)' and
            // invoke. The trailing ';' is left for the statement loop.
            std::vector<i32> args = CollectCallArgs();
            if (host_.invokeCommand) host_.invokeCommand(t.text, args);
            return true;
        }

        case kTokFuncDef:
            // class 3 — VIBE_Script_EnterFunction: user function call (the host
            // owns the call-record push / parameter binding / body run).
            if (host_.callUserFunction) host_.callUserFunction(t.value);
            SkipToSemicolon();
            return true;

        case kTokDeclare: {
            // gilde.exe 0x442d88 — VIBE_Script_ParseDeclaration (runtime form).
            // NextToken must be an UNRESOLVED name (class 0) or the declaration
            // aborts. Then by delimiter sub-code:
            //   10 ';'  plain scalar: define if not present.
            //   2  '='  scalar: define if not present; if the initialiser token
            //           is an int literal (class 5) store it — anything else is
            //           dropped (the binary only stores class-5 initialisers).
            //           The trailing ';' stays for the statement loop.
            //   27 '['  array: class-5 size (else "Illegal array size" -> 1),
            //           then ']' and ';' are consumed.
            //   12 '('  function definition: the record was registered by the
            //           compile pass; the binary restores the cursor to before
            //           the '(' and lets the signature tokens flow as no-ops.
            u8 declType = t.sub;
            ScriptToken_t nameTok = lexer_.Next();
            if (nameTok.cls != kTokUnknown) return true;
            int beforeDelim = lexer_.cursor();
            ScriptToken_t d = lexer_.Next();
            if (d.cls != kTokSymbol) return true;
            if (d.sub == 10) {                                   // ';'
                if (cs_.symbols.LookupVariable(nameTok.text) < 0)
                    cs_.symbols.DefineVariable(nameTok.text, declType, 1);
                return true;
            }
            if (d.sub == 2) {                                    // '='
                int vi = cs_.symbols.LookupVariable(nameTok.text);
                if (vi < 0)
                    vi = cs_.symbols.DefineVariable(nameTok.text, declType, 1);
                ScriptToken_t init = lexer_.Next();
                if (init.cls == kTokIntLit) WriteVar(vi, 0, init.value);
                return true;
            }
            if (d.sub == 27) {                                   // '[' size ']'
                ScriptToken_t sz = lexer_.Next();
                int n = (sz.cls == kTokIntLit) ? sz.value : 1;
                if (cs_.symbols.LookupVariable(nameTok.text) < 0)
                    cs_.symbols.DefineVariable(nameTok.text, declType, n);
                lexer_.Next();                                   // ']' (sub 28)
                lexer_.Next();                                   // ';' (sub 10)
                return true;
            }
            if (d.sub == 12)                                     // '(' func def
                lexer_.setCursor(beforeDelim);
            return true;
        }

        case kTokKeyword:
            // Class-10 switch on the keyword sub-code (byte_767450 stride 16):
            //   1 while  -> ParseWhileLoop (0x44259c)
            //   2 if     -> "ParseForLoop" (0x4427b4, IDA-misnamed if handler)
            //   3 return -> EvaluateExpression + HandleExitKeyword/Finish
            //   4 else   -> DispatchTokenBranch (0x442ac8)
            //   5 #include-> ParseInclude
            //   6/7/8    -> single/normal/multi step (ctx+2564)
            // script_vm.h's enum names are mislabeled vs the binary spellings:
            // kKwFor==2 is actually `if`; kKwIf==4 is the `else`/branch code. We
            // dispatch by the binary sub-code, not by the (mislabeled) name.
            switch (t.sub) {
                case kKwWhile: EnterLoop(1, stmtStart); return true;  // 1: while
                case kKwFor:   EnterLoop(2, stmtStart); return true;  // 2: if
                case kKwReturn:                              // 3: return
                    returnValue_ = EvalExpression();
                    finished_ = true;
                    return false;
                case kKwIf:                                  // 4: else / branch
                    DispatchBranch();
                    return true;
                case kKwInclude: SkipToSemicolon(); return true;  // 5: #include
                case kKwModeA:    stmtMode_ = 1; return true;
                case kKwModeB:    stmtMode_ = 0; return true;
                case kKwModeLoop: stmtMode_ = 2; return true;
                default: finished_ = true; return false;  // "Keyword is not surported!"
            }

        case kTokBlockEnd:
            // class 12: clear the runnable bit (+164 &= ~1) and stop.
            runnable_ = false;
            return false;

        default:   // class 0 unknown symbol / literals in statement position
            // The binary reports "Unknown symbol" + Finish; tolerated here
            // because the standalone lexer resolves fewer names than the live
            // engine (see EvalExpression's class-0 note).
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
