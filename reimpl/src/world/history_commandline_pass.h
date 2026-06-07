#pragma once
// History / Chronicle — the COMMANDLINE second-pass parser and the .esc-style
// "FEST/BELAGERUNG/AUFSTAND/..." event-command dispatch it drives.
//
// This module completes the chronicle label pipeline begun in
// world/history_full.{h,cpp} (FirstPass syntax validator + ParseContext token
// classifier) and world/history_second_pass.{h,cpp} (bracket collapser + pass
// orchestrator). Recovered 1:1 from gilde.exe:
//
//   * VIBE_History_ParseCommandlineSecondPass 0x4fd8ac — the COMMANDLINE 2nd
//     pass: load a label's text, optionally resolve a leading "_SET<n>"/"_USE<n>"
//     GROUP reference (validated against the 4-slot group table), then split the
//     remaining body on spaces and, for each whitespace-delimited token, match it
//     against the 27-entry command-keyword table (aFest) and dispatch the matching
//     entry's handler (funcs_4FDAA5) with (nameLen, argText). Returns 1 on success.
//
//   * VIBE_History_ParseCommandlineFirstPass 0x4fd6ac — the matching FIRST pass:
//     load the label text, classify the leading GROUP prefix ("_SET"==reset /
//     "_USE"==reuse), validate the 0..3 group index, optionally reset the group
//     slot, and return the group-table row pointer. Recovered here as a pure
//     classification (the group-table row + reset are caller leaves).
//
// The 27 command-keyword strings (aFest @0x633938, 64-byte stride) are recovered
// byte-for-byte. The 27 dispatch handlers (funcs_4FDAA5 @0x634414) each enqueue a
// game action / mutate engine state (siege, uprising, fire, treasury edits, ...)
// and are deeply engine-coupled leaves; they are exposed here as a host hook so
// the parser's tokenize+match+dispatch control flow is exercised 1:1 while the
// command bodies stay mockable. The label-string table (dword_8C36B0) and the
// group table (dword_122DAE0) are likewise caller-supplied.
#include "guild/common/types.h"

#include <string>
#include <vector>
#include <functional>

namespace guild::world {

// ===========================================================================
// Command-keyword table  (gilde.exe aFest @0x633938, 64-byte stride, 27 rows).
// ===========================================================================
// ParseCommandlineSecondPass matches each whitespace-delimited body token against
// these names (memcmp over strlen(name)); a match at index i dispatches handler i.
// Index 0 ("FEST") is the canonical first entry; the table is recovered verbatim.
extern const char* const kHistoryCommandNames[27];
constexpr int kHistoryCommandCount  = 27;
constexpr int kHistoryCommandStride = 64;   // 64-byte stride in the binary

// Matches a token against the command-keyword table the way the original does:
// scans rows 0..26, returns the first index whose name is a leading prefix of the
// token (memcmp over strlen(name) == 0), or 27 (== not found, the original's
// `>= 27` sentinel). The original keys the dispatch on the byte AT the high byte
// of an int slot it initialises to the index; this helper reproduces that scan.
int HistoryCommandIndex(const char* token);

// ===========================================================================
// GROUP-reference classification  (the leading "_SET<n>" / "_USE<n>" prefix).
// ===========================================================================
// Both passes special-case a label whose first 4 body chars (the dword AT
// label+1, after the leading marker char) equal "_SET" (dword_6343C7) or "_USE"
// (dword_6343CC). When present, the next char (label+6) is a single decimal digit
// naming a group slot (0..3). These are the recovered prefix dwords.
constexpr const char* kHistoryGroupSetPrefix = "_SET";   // dword_6343C7
constexpr const char* kHistoryGroupUsePrefix = "_USE";   // dword_6343CC
constexpr int kHistoryGroupCount = 4;                    // valid slots 0..3

// The group classification of a loaded label body (the first-pass / second-pass
// shared head). `prefix4` is the 4 chars at body+1; `slotDigit` is the char at
// body+6 (only meaningful when a group prefix matched).
enum class HistoryGroupKind : int {
    kNone = 0,   // no group prefix -> body parsed verbatim
    kSet  = 1,   // "_SET<n>" -> reset group slot n then use it
    kUse  = 2,   // "_USE<n>" -> reuse group slot n
};

struct HistoryGroupRef {
    HistoryGroupKind kind = HistoryGroupKind::kNone;
    int slot = -1;          // 0..3 (the parsed group index), or -1
    bool valid = true;      // false on an out-of-range slot (>= 4)
};

// Classify a loaded label's leading 5-byte head. `head` must point at the start
// of the label text buffer (the marker char at [0], the 4-char prefix at [1..4],
// the slot digit at [6]). Mirrors the `v29 == dword_6343C7 || == dword_6343CC`
// test plus the `VIBE_Util_ParseInt(slotDigit)` / `>= 4` gate.
HistoryGroupRef HistoryClassifyGroupRef(const char* head);

// ===========================================================================
// Commandline second pass  (gilde.exe 0x4fd8ac — VIBE_History_ParseCommandlineSecondPass).
// ===========================================================================
// The dispatch hook: invoked once per matched command token, mirroring the
// original's `funcs_4FDAA5[idx](nameLen, argText)` indirect call. `index` is the
// matched command index (0..26), `name` its keyword, and `argText` the bytes of
// the token AFTER the keyword (the original passes `&arg[nameLen]`). `groupSlot`
// is the resolved group slot (or -1 for a non-group label) so handlers that mutate
// a group's roster get the original's group context.
using HistoryCommandDispatch =
    std::function<void(int index, const std::string& name,
                       const std::string& argText, int groupSlot)>;

// The result codes the original returns.
enum class HistoryCmdlineResult : int {
    kOk       = 1,   // parsed (and dispatched any matched commands)
    kDisabled = 0,   // dword_B537B4 gate off / empty label / bad group
};

// Parse `labelText` as the body of the commandline second pass and dispatch each
// matched command through `dispatch`. `groupTableSlotPopulated` mirrors the
// original's `*group != 0` gate after resolving a "_USE"/"_SET" group reference:
// when a group prefix is present the original aborts (returns 0) if the group's
// table row is empty; pass the predicate that answers "is group slot n populated?"
// (or null to treat all groups as populated). `enabled` is the dword_B537B4 gate.
//
// Faithful behaviour:
//   * !enabled                  -> kDisabled.
//   * group prefix, bad slot    -> kDisabled (the "Invalid GROUP reference" path).
//   * group prefix, empty slot  -> kDisabled (`if (*v35) ... return 0`).
//   * otherwise tokenize the body on spaces; for each token, HistoryCommandIndex;
//     if matched (< 27), dispatch(index, name, argText, groupSlot). Returns kOk.
HistoryCmdlineResult HistoryParseCommandlineSecondPass(
    const std::string& labelText,
    const HistoryCommandDispatch& dispatch,
    const std::function<bool(int slot)>& groupTableSlotPopulated = nullptr,
    bool enabled = true);

} // namespace guild::world
