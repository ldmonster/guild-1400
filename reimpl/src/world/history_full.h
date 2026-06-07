#pragma once
// History / Chronicle — the chronicle TEXT-FILE parser cores plus the recovered
// substitution tables. Complements world/history.h (ParseDate / scan classifier /
// first Notify batch), world/history_chronicle.h (plague/attack/wander selectors +
// in-memory chronicle) and world/history_parse.h (date formatter + target Notify).
//
// Recovered here from gilde.exe:
//   * VIBE_History_ParseTextFirstPass 0x4fd220 — the chronicle label SYNTAX
//     VALIDATOR ('[' .. ']' bracket pairing with a leading '#' escape marker).
//   * VIBE_History_ParseContext      0x4fd44c — the placeholder-token classifier
//     ('_NEW'/'_SET'/'_USE' prefix table + the 15 role-name replacement table).
//   * VIBE_History_ScanNextEventForward 0x4fe694 — the dated forward-scan driver
//     (its per-entry classification already in history.h; here the full walk with
//     the emit/stop/continue codes 1/2/4/6/3 the original returns).
//
// The label string table (dword_8C36B0), the per-group slot table (dword_122DAE0),
// the He broadcast + VIBE_Text_RenderFormattedMessage and the .esc replacement
// dispatch (funcs_4FD5D2) are engine leaves; the recoverable cores are the syntax
// rules + the data tables, recovered byte-for-byte.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Substitution prefix table  (gilde.exe dword_6343B8, stride 5 bytes).
// ===========================================================================
// ParseContext matches the first dword of a token against these 5-byte-stride
// prefixes; index 0..2 (NEW/USE/REL) select the substitution MODE (the v16 code).
// Bytes recovered via get_bytes @0x6343B8.
extern const char* const kHistoryPrefixTable[5];  // "_NEW","_USE","_REL","_SET","_USE"
constexpr int kHistoryPrefixCount = 5;

// ParseCommandlineFirstPass keys group resets on these two prefixes (dword_6343C7
// "_SET", dword_6343CC "_USE").
constexpr const char* kHistorySetPrefix = "_SET";
constexpr const char* kHistoryUsePrefix = "_USE";

// ===========================================================================
// Role-name replacement table  (gilde.exe aBuergermeister_3, 15 entries x 64B).
// ===========================================================================
// ParseContext matches the token body against these names (64-byte stride, memcmp
// over strlen) to pick the replacement role index (0..14). Recovered byte-for-byte
// via get_bytes @0x633FF8.
extern const char* const kHistoryRoleNames[15];
constexpr int kHistoryRoleCount = 15;
constexpr int kHistoryRoleStride = 64;   // 64-byte stride in the binary

// Looks up a role-name in the table; returns its index (0..14) or -1 (the original
// stops at index 15 == not found). Matches the leading-prefix memcmp the original
// uses (a token that starts with a role name matches it).
int HistoryRoleNameIndex(const char* token);

// ===========================================================================
// FirstPass syntax validator  (gilde.exe 0x4fd220).
// ===========================================================================
// Validates a chronicle label's bracket grammar: a '[' starts a region, a '#'
// inside it marks the escape split, a ']' closes it (and the region between '#' and
// ']' is collapsed). A bare '#' before any '[' or an unbalanced/duplicate marker is
// a syntax error. Returns true when the label is well-formed (the original's
// "Syntax Error" gate passes), false otherwise. Pure string logic — the label
// string itself is supplied by the caller (the engine resolves it from the table).
bool HistoryParseTextFirstPassValid(const char* label);

// ===========================================================================
// Substitution-token classification  (gilde.exe 0x4fd44c, recoverable portion).
// ===========================================================================
// The mode code v16 ParseContext derives from the token's leading prefix, by
// comparing the token's first dword against the first THREE prefix-table entries
// (offsets 0/5/10 == "_NEW"/"_USE"/"_REL"):
//   prefix "_NEW" (index 0) -> mode 0  (define / new slot)
//   prefix "_USE" (index 1) -> mode 1  (use existing slot)
//   prefix "_REL" (index 2) -> mode 2  (relative reference)
//   no match               -> mode 4  (literal role-name replacement)
// (The "_SET"/"_USE" entries at indices 3/4 are used by ParseCommandlineFirstPass,
//  not by ParseContext's 3-entry mode scan.)
enum class HistoryTokenMode : int {
    kNew     = 0,
    kUse     = 1,
    kRel     = 2,
    kLiteral = 4,   // v16 default == 4 (no prefix matched)
};
HistoryTokenMode HistoryClassifyTokenMode(const char* token);

// The slot reference the token carries (the digit after the prefix). Valid slots
// are 0..7 (the original's `v13 < 8` gate); returns -1 for an out-of-range slot.
int HistoryTokenSlot(const char* token, HistoryTokenMode mode);

// ===========================================================================
// Forward event-scan driver  (gilde.exe 0x4fe694).
// ===========================================================================
// The scanner returns one of these codes per call (the original's `result`):
enum class HistoryScanCode : int {
    kEmit       = 1,  // emitted an entry dated to currentDay-1 with non-empty text
    kStop       = 2,  // reached an entry dated on/after currentDay
    kEndOfFile  = 3,  // scan inactive / no more entries
    kParseError = 4,  // a date label failed to parse
    kEmitEmpty  = 6,  // matched the day window but the text was empty
};
// Classifies one scan step: `parseOk` is whether the date label parsed, `textEmpty`
// whether the resolved text was empty, given the day diff (currentDay - entryDay).
// Mirrors the per-entry branch in ScanNextEventForward.
HistoryScanCode HistoryScanStep(bool parseOk, i32 dayDiff, bool textEmpty);

} // namespace guild::world
