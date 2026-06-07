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
                case kOpStop:   lastTerm_ = ')'; return acc;  // ')' / ']' (28)
                case 29:        lastTerm_ = ','; return acc;  // ',' separator
                case 13:        lastTerm_ = ';'; return acc;  // ';'
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
            case kTokVariable: operand = ReadVar(t.value, 0); break;
            case kTokIntLit:   operand = t.value; break;
            case kTokRawSym:   operand = t.value; break;
            case kTokStrLit:   operand = 0; break;
            case kTokFuncCall: {                       // command used as value
                std::vector<i32> args;
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
        // false: skip the body.
        if (f.hasBrace) SkipBraceBlock();
        else SkipToSemicolon();
        return;
    }
    loopStack_.push_back(f);
    if (f.hasBrace) {
        // step into the block: consume the '{'
        int pos = lexer_.cursor();
        while (pos < (int)cs_.source.size() && cs_.source[pos] != '{') ++pos;
        if (pos < (int)cs_.source.size()) ++pos;
        lexer_.setCursor(pos);
    }
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
            if (t.sub == 9) {        // '}' — close a loop body: rewind if frame
                if (!loopStack_.empty()) {
                    LoopFrame f = loopStack_.back();
                    if (f.type == 1) {       // while: re-test from restartCursor
                        loopStack_.pop_back();
                        lexer_.setCursor(f.restartCursor);
                        // re-enter the loop keyword path: emulate by re-eval cond
                        int restart = lexer_.cursor();
                        ScriptToken_t open = lexer_.Next(); (void)open;
                        i32 cond = EvalExpression();
                        int look = lexer_.cursor();
                        char b = PeekNonSpace(cs_.source, look);
                        if (cond) {
                            LoopFrame nf; nf.restartCursor = restart; nf.condition = cond;
                            nf.type = 1; nf.hasBrace = (b=='{');
                            loopStack_.push_back(nf);
                            int pos = lexer_.cursor();
                            while (pos < (int)cs_.source.size() && cs_.source[pos] != '{') ++pos;
                            if (pos < (int)cs_.source.size()) ++pos;
                            lexer_.setCursor(pos);
                        } else {
                            if (b == '{') SkipBraceBlock(); else SkipToSemicolon();
                        }
                    } else {                 // for/once: fall through past block
                        loopStack_.pop_back();
                    }
                }
                return true;
            }
            return true;  // '{', ';', operators: no-op at statement position

        case kTokVariable: {
            // assignment: name '=' expr ';'  (AssignVariable). Handle optional
            // '[' index ']' before '='.
            int elemIndex = 0;
            ScriptToken_t op = lexer_.Next();
            if (op.cls == kTokSymbol && op.sub == 27) {   // '['
                elemIndex = EvalExpression();             // up to ']'
                op = lexer_.Next();                       // '=' expected
            }
            // op should be '=' — the original encodes assign via sub-code 2/27.
            i32 rhs = EvalExpression();
            WriteVar(t.value, elemIndex, rhs);
            return true;
        }

        case kTokFuncCall: {
            // command call: collect args up to the closing ')'/';'
            std::vector<i32> args;
            int look = lexer_.cursor();
            char b = PeekNonSpace(cs_.source, look);
            if (b == '(') {
                lexer_.Next();                   // consume '('
                // read comma/space-separated argument expressions until ')'
                while (true) {
                    int before = lexer_.cursor();
                    char pk = PeekNonSpace(cs_.source, before);
                    if (pk == ')' || pk == 0) { lexer_.setCursor(before + (pk==')'?1:0)); break; }
                    lastTerm_ = 0;
                    i32 a = EvalExpression();
                    args.push_back(a);
                    // EvalExpression consumes its own terminator (')' or ',').
                    // Continue the arg list only on a comma.
                    if (lastTerm_ == ',') continue;
                    break;
                }
            }
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
            switch (t.sub) {
                case kKwWhile:  EnterLoop(1); return true;
                case kKwFor:    EnterLoop(2); return true;
                case kKwReturn:
                    returnValue_ = EvalExpression();
                    finished_ = true;
                    return false;
                case kKwIf: {
                    // if ( cond ) <stmt/block>
                    lexer_.Next();                     // '('
                    i32 cond = EvalExpression();       // up to ')'
                    if (!cond) DispatchBranch();
                    else {
                        int look = lexer_.cursor();
                        char b = PeekNonSpace(cs_.source, look);
                        if (b == '{') { int pos=lexer_.cursor(); while(pos<(int)cs_.source.size()&&cs_.source[pos]!='{')++pos; if(pos<(int)cs_.source.size())++pos; lexer_.setCursor(pos); }
                    }
                    return true;
                }
                case kKwInclude: SkipToSemicolon(); return true;
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
