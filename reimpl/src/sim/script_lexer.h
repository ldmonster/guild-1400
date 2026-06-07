#pragma once
// The .esc source-text lexer for the Guild scripting engine (gilde.exe).
//
// Namespace: guild::sim. Re-implements VIBE_Script_NextToken (0x441974), the
// tokenizer the compiler and the tree-walking executor both pull from.
//
// Recovered behaviour (from the 431-instruction NextToken disassembly):
//   * Leading spaces are skipped (the `*v3 == 32` loop).
//   * A token is the longest run up to the next operator/separator string in the
//     operator table (byte_767958, stride 5) OR, at the start, a multi-char
//     keyword in the keyword table (byte_767450, stride 16).
//   * The extracted lexeme is then classified, in this exact priority order:
//       1. keyword table  (byte_767450, stride 16) -> class 10 (kTokKeyword),
//          sub-code = 1-based row index.
//       2. operator table (byte_767958, stride 5)  -> class 1 (kTokSymbol),
//          sub-code = 1-based row index. (Sub-code 14 == a string-literal opener
//          '"': NextToken then scans to the closing quote -> class 7.)
//       3. if the first char is a digit-class char (byte_64A208[c+1] & 0x20):
//          contains '.' -> class 6 (float, kTokRawSym carries the value) else
//          class 5 (int literal).
//       4. LookupVariable  -> class 2 (kTokVariable), arg = var index.
//       5. ParseTypeKeyword-> class 8 (kTokDeclare),  arg = type code.
//       6. LookupFunction  -> class 3 (kTokFuncDef),  arg = func index.
//       7. FindCommandByName-> class 4 (kTokFuncCall),arg = command index.
//       8. LookupInclude   -> class 11 (kTokLabel),   arg = include index.
//       9. otherwise class 0 (unknown) and the lexeme text is copied out.
//   * End of source -> class 12 (kTokBlockEnd).
//
// The operator/keyword string tables (byte_767958 / byte_767450) read as zeros
// in the cold IDB — they are populated at engine init. We reconstruct them here
// from the recovered sub-codes (ScriptOp / ScriptKeyword in script_vm.h) so the
// table *structure* (stride 5 / stride 16, row index == sub-code) is byte-exact
// and the lexeme->token mapping matches NextToken.
//
// The character-class table byte_64A208 IS real data (256 bytes, see .cpp); it
// is reproduced verbatim and used for the digit-class test exactly as NextToken
// does (`byte_64A208[(u8)(c + 1)] & 0x20`).
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include "sim/script_symbols.h"
#include <string>
#include <vector>

namespace guild::sim {

// gilde.exe byte_64A208 @0x64A208 — 256-byte character class table (real data).
// Bit 0x20 of byte_64A208[c+1] marks `c` as a digit-class start character.
extern const u8 kCharClass[256];

// True iff `c` begins a numeric literal, per NextToken's digit test.
bool IsDigitClass(u8 c);

// One operator-table row (byte_767958 stride 5): the operator spelling (<=4
// chars + NUL) and its recovered sub-code (== 1-based table row index).
struct OperatorEntry { const char* text; u8 subcode; };
// One keyword-table row (byte_767450 stride 16): keyword spelling + sub-code.
struct KeywordEntry  { const char* text; u8 subcode; };

// The reconstructed operator table (byte_767958). Row index i (1-based) maps to
// sub-code i, matching NextToken which returns v56 (the 1-based row counter).
extern const std::vector<OperatorEntry> kOperatorTable;
// The reconstructed keyword table (byte_767450).
extern const std::vector<KeywordEntry>  kKeywordTable;

// Match an operator-table row starting at s[pos] (longest spelling wins, as in
// NextToken's table walk). Returns the matched row or nullptr; `len` = match
// length. Shared with the compiler's raw-identifier scan.
const OperatorEntry* MatchOperatorPublic(const std::string& s, int pos, int& len);

// The lexer needs the compiled symbol table (to classes 2/3) and the registered
// commands (class 4) to classify a bare identifier — exactly NextToken's chain
// of LookupVariable/LookupFunction/FindCommandByName. Commands are supplied as a
// name list (the host's registered-command set); index+1 is the returned arg.
class ScriptLexer {
public:
    ScriptLexer(const std::string& source,
                const ScriptSymbols* symbols,
                const std::vector<std::string>* commandNames)
        : src_(source), syms_(symbols), commands_(commandNames) {}

    // gilde.exe 0x441974 — VIBE_Script_NextToken. Reads one token at the cursor,
    // advances the cursor past it, and returns the token (class + sub/value/text).
    ScriptToken_t Next();

    int  cursor() const { return cursor_; }
    void setCursor(int c) { cursor_ = c; }
    bool atEnd() const { return cursor_ >= (int)src_.size(); }

    // Tokenize the whole source into a flat token stream (terminated by a single
    // kTokBlockEnd). Convenience for tests / the compiler front-end.
    std::vector<ScriptToken_t> Tokenize();

private:
    // ParseTypeKeyword (0x441788): "int"/"byte"/"char"/"float"/"string"/"void"/
    // "{"/"}" -> the recovered type/brace codes; 0 if not a type keyword.
    static u8 ClassifyTypeKeyword(const std::string& lexeme);

    std::string src_;   // owned copy of the source buffer (durable cursor base)
    const ScriptSymbols* syms_;
    const std::vector<std::string>* commands_;
    int cursor_ = 0;
};

} // namespace guild::sim
