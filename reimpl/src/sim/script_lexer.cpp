#include "sim/script_lexer.h"
#include <cstdlib>

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
// Reconstructed operator table (byte_767958, stride 5). Row index (1-based) ==
// the sub-code NextToken returns in result[4]. Sub-codes recovered from the
// expression evaluator (ScriptOp) and the statement/declaration parsers:
//   1  ==     4  +      6  -      7  !=     8  {      9  }
//  11  :(label)        12  end   13  ;      14  "(string opener)
//  16  <=    17  <     18  >=    19  >      20  /     21  *
//  22  |     23  &     24  ||    25  &&     27  [ / ( 28  ] / )
// (Single-table entries are listed once; bracket/paren share a sub-code class.)
// ===========================================================================
const std::vector<OperatorEntry> kOperatorTable = {
    {"==", kOpEq},      // 1
    {"=",  2},          // 2  assignment ('=' ; AssignVariable v18==2 path)
    {"+",  kOpAdd},     // 4
    {"-",  kOpSub},     // 6
    {"!=", kOpNe},      // 7
    {"{",  8},          // 8  block-open
    {"}",  9},          // 9  block-close
    {":",  kOpLabelRef},// 11 label
    {";",  13},         // 13 statement separator
    {"\"", 14},         // 14 string-literal opener
    {"<=", kOpLe},      // 16
    {"<",  kOpLt},      // 17
    {">=", kOpGe},      // 18
    {">",  kOpGt},      // 19
    {"/",  kOpDiv},     // 20
    {"*",  kOpMul},     // 21
    {"|",  kOpBitOr},   // 22
    {"&",  kOpBitAnd},  // 23
    {"||", kOpOr},      // 24
    {"&&", kOpAnd},     // 25
    {"(",  27},         // 27 open paren / array-index open
    {")",  28},         // 28 close paren / array-index close
    {",",  29},         // arg separator (own sub-code; terminates an expression)
};

// ===========================================================================
// Reconstructed keyword table (byte_767450, stride 16). Sub-codes recovered from
// ExecuteStatement's class-10 switch: 1 while, 2 for, 3 return, 4 if, 5 include,
// 6/7/8 mode keywords. (byte_767460 / byte_767470 hold "while"/"for" spellings,
// strlen'd by the loop parsers.)
// ===========================================================================
const std::vector<KeywordEntry> kKeywordTable = {
    {"while",   kKwWhile},    // 1
    {"for",     kKwFor},      // 2
    {"return",  kKwReturn},   // 3
    {"if",      kKwIf},       // 4
    {"include", kKwInclude},  // 5
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
    if (const OperatorEntry* op = MatchOperator(src_, cursor_, oplen)) {
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
        if (MatchOperator(src_, cursor_, probe)) break;
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

    // 3. numeric literal (digit-class first char).
    if (IsDigitClass((u8)lexeme[0])) {
        if (lexeme.find('.') != std::string::npos) {
            tok.cls = kTokRawSym;            // class 6 (float operand)
            tok.value = (i32)std::strtod(lexeme.c_str(), nullptr);
        } else {
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
