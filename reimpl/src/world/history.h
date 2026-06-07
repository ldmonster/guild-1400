#pragma once
// History / Chronicle — the dated event-log subsystem. Faithful 1:1 port of the
// recoverable core of the VIBE_History_* family (gilde.exe):
//   * the dated-text parser/formatter  VIBE_History_ParseDate 0x4fe2d4
//   * the chronological forward scan     VIBE_History_ScanNextEventForward 0x4fe694
//   * the Notify dispatch gate           VIBE_History_Notify* (~25 fns @0x535780+)
//
// The GUI / file-IO / network-broadcast plumbing (text rendering, He message
// send, chronicle file load) is left to the engine; this module exposes the
// data-rules those functions are built on, with the engine boundary mocked.
#include <cstdint>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Dated-text parsing  (gilde.exe VIBE_History_ParseDate 0x4fe2d4)
// ===========================================================================
// The chronicle stores each entry under a "<<DATE>>" label whose text is a
// 10-character "DD.MM.YYYY" string. ParseDate splits it into day/month/year via
// VIBE_Util_ParseInt over fixed substrings, normalises a zero day/month to 1,
// and reports the year as an offset from 1400 (the campaign start year).
struct ParsedDate {
    i32 yearOffset;  // *v69 = year - 1400 (output to the caller's record)
    i32 day;         // 1..31, with 0 normalised to 1
    i32 month;       // 1..12, with 0 normalised to 1
    i32 year;        // absolute year (e.g. 1400)
    // mode mirrors the original's v72:
    //   0 = full "DD.MM.YYYY", 1 = day was 0 (month/year only),
    //   2 = month was 0 (year only).
    int mode;
};

constexpr int kChronicleBaseYear = 1400;  // dword: *v69 = year - 1400

// Parses a 10-char "DD.MM.YYYY" date string. Returns true on success (string is
// exactly 10 chars long, as the original requires), filling `out`. On failure
// returns false and leaves `out` untouched (mirrors the strlen(v64)==10 gate).
bool HistoryParseDate(const char* dateText, ParsedDate* out);

// ===========================================================================
// Chronological forward scan  (gilde.exe VIBE_History_ScanNextEventForward)
// ===========================================================================
// The scanner walks chronicle date-labels in order looking for the entry whose
// stored day is exactly one before the current game day. The original encodes
// the per-entry comparison as (currentDay - parsedDay):
//   == 1 -> this is "yesterday's" entry, emit it          (result 1)
//   <  1 -> we've passed the current day, stop             (result 2)
//   >  1 -> older entry, keep scanning                     (result 0/continue)
// We expose the pure comparison so the walk can be driven and tested without the
// chronicle file buffer.
enum class HistoryScanResult : int {
    kEmit     = 1,  // entry matches the current day window
    kStop     = 2,  // scanned past the current day
    kContinue = 0,  // older entry, advance to the next label
};
HistoryScanResult HistoryClassifyEntry(i32 currentDay, i32 entryDay);

// ===========================================================================
// Notify dispatch gate  (gilde.exe VIBE_History_Notify* family)
// ===========================================================================
// Each Notify function reads the target person-record's kind byte (record+2) and
// only logs/broadcasts when the kind is 6 or 7 (the two "important"/player kinds
// the chronicle records). This predicate is the shared gate.
bool HistoryNotifyKindIsImportant(int recordKindByte);

// VIBE_History_NotifyArrestTarget 0x535780 — selects the chronicle message id
// for an arrest. With flag==0 the base text id 7313 is used; with flag!=0 the id
// depends on the current game day: 7314 when day < 117, else 7315. Returns the
// selected text id, or -1 when the target kind is not important.
int HistoryNotifyArrestTextId(int recordKindByte, bool detained, i32 currentDay);

// ---------------------------------------------------------------------------
// The remaining kind-gated Notify text-id selectors. Each returns the chronicle
// text id the original would render via VIBE_Text_RenderFormattedMessage, or -1
// when the kind gate fails. The gate is HistoryNotifyKindIsImportant unless
// noted. (The He message broadcast + formatting are the engine's; the text-id
// selection is the recoverable rule.)
// ---------------------------------------------------------------------------
// 0x5357f8 VIBE_History_NotifyUseItemEvent       -> 7316  (kind 6/7)
int HistoryNotifyUseItemTextId(int recordKindByte);
// 0x535844 VIBE_History_NotifyBuildingLinkRemoved-> 7318  (kind 6/7)
int HistoryNotifyBuildingLinkRemovedTextId(int recordKindByte);
// 0x535890 VIBE_History_NotifyOfficeTransfer     -> 7317  (kind 6/7)
int HistoryNotifyOfficeTransferTextId(int recordKindByte);
// 0x535d88 VIBE_History_NotifyCrimeAdded         -> 7328  (kind 6/7)
int HistoryNotifyCrimeAddedTextId(int recordKindByte);
// 0x536070 VIBE_History_NotifyRivalEvent         -> 4954  (UNGATED, always)
int HistoryNotifyRivalEventTextId();

// 0x535d20 VIBE_History_NotifyLawChangeToMaster — gated NOT on kind but on the
// target id differing from the local player id. Returns 7327 when
// targetId != localPlayerId, else -1 (the early-out returning the id unchanged).
int HistoryNotifyLawChangeTextId(int targetId, int localPlayerId);

// 0x535f64 VIBE_History_NotifyOfficeSwap — three-way: if the first party is kind
// 6 -> 6604; else if the second party is kind 6 -> 6604; else if the subject is
// NOT kind 6 -> 6605; else -1 (no notification). Returns the selected id.
int HistoryNotifyOfficeSwapTextId(int subjectKindByte, int partyAKindByte,
                                  int partyBKindByte);

} // namespace guild::world
