#include "sim/script_lexer.h"
#include <cstdlib>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// gilde.exe byte_64A208 @0x64A208 — 256-byte character class table (verbatim).
// Recovered via get_bytes. Bit 0x20 of byte_64A208[c+1] marks `c` as a
// digit-class character (used by NextToken to split int/float literals from
// identifiers). Other bits encode whitespace/alpha classes used elsewhere.
// ===========================================================================
const u8 kCharClass[256] = {
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x03,0x03,0x03,0x03,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x0a,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x0c,0x58,0x58,0x58,0x58,0x58,0x58,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x98,0x98,0x98,0x98,0x98,0x98,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x0c,0x0c,0x0c,0x0c,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

// gilde.exe NextToken digit test: byte_64A208[(unsigned __int8)(c + 1)] & 0x20.
bool IsDigitClass(u8 c) {
    return (kCharClass[(u8)(c + 1)] & 0x20) != 0;
}

// ===========================================================================
// gilde.exe operator table byte_767958 (stride 5). Row index (1-based) == the
// sub-code NextToken returns in result[4] (the v56 counter). The table is
// runtime-populated in ConsoleParseLine @0x4453a4: each operator spelling
// (asc_6184C0.. / asc_618014.. / asc_6183D4) is copied to a FIXED destination
// address unk_7679XX, and the destination's offset from base 0x767958 divided by
// the stride (5) yields the row index == sub-code.  Recovered destination map
// (addr -> (addr-0x767958)/5 == sub-code):
//   ==  0x76795D=1   =   0x767962=2   ++  0x767967=3   +   0x76796C=4
//   --  0x767971=5   -   0x767976=6   !=  0x76797B=7   {   0x767980=8
//   }   0x767985=9   ;   0x76798A=10  ,   0x76798F=11  (   0x767994=12
//   )   0x767999=13  "   0x76799E=14  //  0x7679A3=15  <=  0x7679A8=16
//   <   0x7679AD=17  >=  0x7679B2=18  >   0x7679B7=19  /   0x7679BC=20
//   *   0x7679C1=21  ||  0x7679C6=22  &&  0x7679CB=23  |   0x7679D0=24
//   &   0x7679D5=25  (space) 0x7679DA=26  [ 0x7679DF=27  ] 0x7679E4=28
//   .   0x7679E9=29
// The expression-evaluator's binary-op semantics (VIBE_Script_EvaluateExpression
// @0x443ff0) confirm 22 returns a|b, 23 returns a&b, 24 does acc|=b, 25 acc&=b.
// (script_vm.h's kOp* names are mislabeled vs these spellings but their numeric
// VALUES match the row indices below; we use the binary's row indices verbatim.)
// ===========================================================================
const std::vector<OperatorEntry> kOperatorTable = {
    {"==", 1},          // 1   asc_6184DC -> unk_76795D
    {"=",  2},          // 2   asc_6184D8 -> unk_767962 (assignment)
    {"++", 3},          // 3   asc_6184D4 -> unk_767967 (post-increment)
    {"+",  4},          // 4   asc_6184CC -> unk_76796C  (== kOpAdd)
    {"--", 5},          // 5   asc_6184D0 -> unk_767971 (post-decrement)
    {"-",  6},          // 6   asc_6184C8 -> unk_767976  (== kOpSub)
    {"!=", 7},          // 7   asc_6184E0 -> unk_76797B  (== kOpNe)
    {"{",  8},          // 8   asc_618014 -> unk_767980
    {"}",  9},          // 9   asc_618018 -> unk_767985
    {";",  10},         // 10  asc_6184F8 -> unk_76798A (statement separator)
    {",",  11},         // 11  asc_6184FC -> unk_76798F (arg separator)
    {"(",  12},         // 12  asc_6184EC -> unk_767994
    {")",  13},         // 13  asc_6184F0 -> unk_767999
    {"\"", 14},         // 14  asc_6184F4 -> unk_76799E (string-literal opener)
    {"//", 15},         // 15  asc_618500 -> unk_7679A3 (line-comment marker)
    {"<=", 16},         // 16  asc_618514 -> unk_7679A8  (== kOpLe)
    {"<",  17},         // 17  asc_618504 -> unk_7679AD  (== kOpLt)
    {">=", 18},         // 18  asc_61851C -> unk_7679B2  (== kOpGe)
    {">",  19},         // 19  asc_618518 -> unk_7679B7  (== kOpGt)
    {"/",  20},         // 20  asc_618520 -> unk_7679BC  (== kOpDiv)
    {"*",  21},         // 21  asc_618524 -> unk_7679C1  (== kOpMul)
    {"||", 22},         // 22  asc_618510 -> unk_7679C6  (eval: returns a|b)
    {"&&", 23},         // 23  asc_6183D4 -> unk_7679CB  (eval: returns a&b)
    {"|",  24},         // 24  asc_618508 -> unk_7679D0  (eval: acc |= b)
    {"&",  25},         // 25  asc_61850C -> unk_7679D5  (eval: acc &= b)
    // 26 == " " (space); never reached here — leading spaces are skipped before
    // operator matching, exactly as NextToken's `*v3 == 32` loop does.
    {"[",  27},         // 27  asc_6184E4 -> unk_7679DF (array-index open)
    {"]",  28},         // 28  asc_6184E8 -> unk_7679E4 (array-index close)
    {".",  29},         // 29  asc_6184C4 -> unk_7679E9 (decimal point / member)
};

// ===========================================================================
// gilde.exe keyword table byte_767450 (stride 16). NextToken scans rows starting
// at offset 16 (v8=16, v56=1, while v8<144 -> 8 rows), so sub-code == (offset/16).
// Populated in ConsoleParseLine @0x4453a4 at FIXED destinations; recovered map
// (dest -> offset from 0x767450 / 16 == sub-code):
//   while      byte_767460=0x767460  off 16  -> 1
//   if         byte_767470=0x767470  off 32  -> 2
//   return     unk_767480 =0x767480  off 48  -> 3
//   else       unk_767490 =0x767490  off 64  -> 4
//   #include   unk_7674A0 =0x7674A0  off 80  -> 5
//   #singlestep unk_7674B0=0x7674B0  off 96  -> 6
//   #normalstep unk_7674C0=0x7674C0  off 112 -> 7
//   #multistep  unk_7674D0=0x7674D0  off 128 -> 8
// (`do` is copied to byte_767450 at offset 0 but NEVER matched, since the scan
// starts at offset 16.)  The executor's class-10 switch (ExecuteStatement
// @0x444bd0) dispatches these exact codes: 1->ParseWhileLoop, 2->the if-stmt
// handler (0x4427b4, IDA-misnamed "ParseForLoop"; it reads byte_767470=="if",
// expects '(' then '{'), 3->return-expr, 4->DispatchTokenBranch (if/else branch),
// 5->ParseInclude, 6/7/8-> single/normal/multi step modes (ctx+2564).
// NOTE: script_vm.h's kKw* names are mislabeled (kKwFor=2 is actually `if`;
// kKwIf=4 is the branch/`else` code; there is no `for` keyword and `include`
// is spelled `#include`). We encode the BINARY's spellings + row indices here.
// ===========================================================================
const std::vector<KeywordEntry> kKeywordTable = {
    {"while",       1},    // 1
    {"if",          2},    // 2  (if-statement handler @0x4427b4)
    {"return",      3},    // 3
    {"else",        4},    // 4  (branch dispatch @0x442ac8)
    {"#include",    5},    // 5
    {"#singlestep", 6},    // 6  ctx+2564 = 1
    {"#normalstep", 7},    // 7  ctx+2564 = 0
    {"#multistep",  8},    // 8  ctx+2564 = 2
};

// gilde.exe 0x441788 — VIBE_Script_ParseTypeKeyword (declaration type words).
u8 ScriptLexer::ClassifyTypeKeyword(const std::string& w) {
    if (w == "int")    return kVarInt;    // 1
    if (w == "float")  return kVarFloat;  // 7
    if (w == "void")   return kVarVoid;   // 5
    if (w == "string") return kVarString; // 6
    if (w == "{")      return 8;          // asc_618014
    if (w == "}")      return 9;          // asc_618018
    if (w == "byte" || w == "char") return kVarByte; // 2
    return 0;
}

// Try to match an operator-table row starting at src_[pos]; returns the matched
// row (longest spelling first, mirroring the table-walk ordering in NextToken,
// which finds the operator that terminates the current token).
static const OperatorEntry* MatchOperator(const std::string& s, int pos, int& len) {
    const OperatorEntry* best = nullptr;
    int bestLen = 0;
    for (const auto& op : kOperatorTable) {
        int ol = (int)std::strlen(op.text);
        if (ol > bestLen && pos + ol <= (int)s.size() &&
            s.compare(pos, ol, op.text) == 0) {
            best = &op;
            bestLen = ol;
        }
    }
    len = bestLen;
    return best;
}

const OperatorEntry* MatchOperatorPublic(const std::string& s, int pos, int& len) {
    return MatchOperator(s, pos, len);
}

// gilde.exe NextToken token-terminator test (the v7=5 walk at 0x441a72). An
// operator string at s[pos] terminates the current token UNLESS it is the '.'
// row (sub-code 29, table offset 145) AND the preceding char is a digit-class
// char — exactly the `v7 == 145 && (byte_64A208[(u8)(*(v6-1)+1)] & 0x20)`
// exception at 0x441a9e, which keeps `3.14` a single numeric lexeme while still
// splitting `a.b`. Returns the terminating operator (or nullptr) and its length.
static const OperatorEntry* MatchSeparator(const std::string& s, int pos, int& len) {
    const OperatorEntry* op = MatchOperator(s, pos, len);
    if (op && op->subcode == 29 && pos > 0 &&
        IsDigitClass(static_cast<u8>(s[pos - 1]))) {
        len = 0;
        return nullptr;   // '.' after a digit is part of the number, not a break
    }
    return op;
}

// ===========================================================================
// gilde.exe 0x441974 — VIBE_Script_NextToken
// ===========================================================================
ScriptToken_t ScriptLexer::Next() {
    ScriptToken_t tok{kTokBlockEnd, 0, 0, ""};

    // Skip leading spaces (the `*v3 == 32` loop).
    while (cursor_ < (int)src_.size() &&
           (src_[cursor_] == ' ' || src_[cursor_] == '\t' ||
            src_[cursor_] == '\n' || src_[cursor_] == '\r')) {
        ++cursor_;
    }
    if (cursor_ >= (int)src_.size()) {
        tok.cls = kTokBlockEnd;  // end of source -> class 12
        return tok;
    }

    // If an operator/separator string starts exactly here, that *is* the token
    // (NextToken emits operators as their own tokens). String opener -> scan to
    // the closing quote and emit a string literal (class 7).
    int oplen = 0;
    if (const OperatorEntry* op = MatchSeparator(src_, cursor_, oplen)) {
        if (op->subcode == 14) {                 // string-literal opener '"'
            int start = cursor_ + 1;
            int end = start;
            while (end < (int)src_.size() && src_[end] != '"') ++end;
            tok.cls  = kTokStrLit;               // class 7
            tok.text = src_.substr(start, end - start);
            tok.value = 0;
            cursor_ = (end < (int)src_.size()) ? end + 1 : end;
            return tok;
        }
        tok.cls   = kTokSymbol;                  // class 1
        tok.sub   = op->subcode;
        tok.value = 0;
        tok.text  = op->text;
        cursor_ += oplen;
        return tok;
    }

    // Otherwise read the longest lexeme up to the next operator/space.
    int start = cursor_;
    while (cursor_ < (int)src_.size()) {
        char c = src_[cursor_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        int probe = 0;
        if (MatchSeparator(src_, cursor_, probe)) break;
        ++cursor_;
    }
    std::string lexeme = src_.substr(start, cursor_ - start);
    if (lexeme.empty()) { tok.cls = kTokBlockEnd; return tok; }

    // ---- classification chain (NextToken's exact priority order) ----

    // 1. keyword table -> class 10.
    for (const auto& kw : kKeywordTable) {
        if (lexeme == kw.text) {
            tok.cls = kTokKeyword;  // 10
            tok.sub = kw.subcode;
            tok.text = lexeme;
            return tok;
        }
    }

    // 3. numeric literal (digit-class first char).  gilde.exe NextToken (0x441b88):
    //    the lexeme is a number iff byte_64A208[(u8)(lexeme[0]+1)] & 0x20; it is a
    //    FLOAT (class 6) iff it contains '.', else an INT (class 5).
    if (IsDigitClass((u8)lexeme[0])) {
        if (lexeme.find('.') != std::string::npos) {
            // class 6 (float operand). Original: *((float*)out+1) =
            // (float)StrToDouble(lexeme) — store the 32-bit float BIT PATTERN in
            // the value word so float-typed consumers read it back as a float
            // (0x441bbb/0x441bc0/0x441bcc), not a truncated int.
            tok.cls = kTokRawSym;            // class 6
            float f = static_cast<float>(std::strtod(lexeme.c_str(), nullptr));
            std::memcpy(&tok.value, &f, sizeof(f));
        } else {
            // class 5: *(out+4) = ParseInt(lexeme) (base-10, 0x441e53/0x441e58).
            tok.cls = kTokIntLit;            // class 5
            tok.value = (i32)std::strtol(lexeme.c_str(), nullptr, 10);
        }
        tok.text = lexeme;
        return tok;
    }

    // 4. variable.
    if (syms_) {
        int vi = syms_->LookupVariable(lexeme);
        if (vi >= 0) {
            tok.cls = kTokVariable;          // class 2
            tok.value = vi;
            tok.text = lexeme;
            return tok;
        }
    }

    // 5. type keyword -> declaration (class 8).
    if (u8 ty = ClassifyTypeKeyword(lexeme)) {
        tok.cls = kTokDeclare;               // class 8
        tok.sub = ty;
        tok.value = ty;
        tok.text = lexeme;
        return tok;
    }

    // 6. function.
    if (syms_) {
        int fi = syms_->LookupFunction(lexeme);
        if (fi >= 0) {
            tok.cls = kTokFuncDef;           // class 3
            tok.value = fi;
            tok.text = lexeme;
            return tok;
        }
    }

    // 7. registered command.
    if (commands_) {
        for (int i = 0; i < (int)commands_->size(); ++i) {
            if ((*commands_)[i] == lexeme) {
                tok.cls = kTokFuncCall;      // class 4
                tok.value = i;
                tok.text = lexeme;
                return tok;
            }
        }
    }

    // 8. include-ref -> class 11 (kTokLabel).  gilde.exe NextToken @0x441ee5 calls
    //    VIBE_Script_LookupInclude(ctx, lexeme) here, before falling through to
    //    class 0.  BOUNDARY: that lookup walks the live context's include-slot
    //    table (ctx+2492, see LookupInclude @0x4415bc in script_console.cpp); this
    //    standalone ScriptLexer carries no runtime context, so the class-11 step is
    //    not reachable here.  It is exercised end-to-end through the executor path
    //    (script_console.cpp LookupInclude), not the standalone tokenizer.

    // 9. unknown symbol — copy the lexeme text out (class 0).
    tok.cls = kTokUnknown;
    tok.value = 0;
    tok.text = lexeme;
    return tok;
}

std::vector<ScriptToken_t> ScriptLexer::Tokenize() {
    std::vector<ScriptToken_t> out;
    int guard = 0;
    while (!atEnd() && guard++ < 1000000) {
        ScriptToken_t t = Next();
        out.push_back(t);
        if (t.cls == kTokBlockEnd) return out;
    }
    out.push_back({kTokBlockEnd, 0, 0, ""});
    return out;
}

} // namespace guild::sim
