#pragma once
// History / Chronicle — the remaining VIBE_History_Notify* text-id selectors, the
// plague-event RNG selectors, and an in-memory chronicle add / chronological scan
// / dated-text formatter. This complements world/history.h (which already covers
// ParseDate, the ScanNextEventForward classifier and the first batch of Notify
// gates); here we recover the rest of the Notify family and the chronicle-entry
// add/format/scan rules.
//
// All Notify functions share the same shape: a target-kind gate (the person
// record's kind byte at record+2 must be 6 or 7 — "important"/player kinds; one
// variant gates on != 7 instead), then a text id is selected (some offset by a
// crt::RandNext draw). The He message broadcast + VIBE_Text_RenderFormattedMessage
// are the engine's; the recoverable rule is the gate + the selected text id.
#include "guild/common/types.h"

namespace guild::world {

// gilde.exe 0x5358e0 VIBE_History_NotifyWanderEventA  -> 7319 (kind 6/7)
int ChronicleWanderATextId(int recordKindByte);
// gilde.exe 0x535928 VIBE_History_NotifyWanderEventB  -> 7320 (kind 6/7)
int ChronicleWanderBTextId(int recordKindByte);

// gilde.exe 0x535970 VIBE_History_NotifyWanderPairEvent — two-party, gated on
// kind != 7 for each party (the "self" party at +2, the other at a2+2):
//   selfKind != 7  -> emit 7321 for self
//   otherKind != 7 -> emit 7322 for other
// Returns the count of lines that would be emitted (0..2). selfEmitId / otherEmitId
// (optional) receive the per-party text ids (-1 when that party's line is skipped).
int ChronicleWanderPairLines(int selfKindByte, int otherKindByte,
                             int* selfEmitId, int* otherEmitId);

// gilde.exe 0x535a04 VIBE_History_BroadcastAttackEvent — the attacker/defender
// line is gated on the defender kind (6/7) -> 7323; the broadcast summary line
// (7324) is always emitted, with the suffix-id depending on a flag byte at
// attacker+9: present -> 7325, else the base 7324 group. Returns the defender
// text id (7323) or -1 when the defender kind is not important.
int ChronicleAttackDefenderTextId(int defenderKindByte);
// The broadcast-summary suffix id: (attackerHasFlag != 0) + 7325 == 7325 or 7326.
int ChronicleAttackSuffixTextId(bool attackerHasFlag);

// gilde.exe 0x535dd8 VIBE_History_BroadcastPlagueOutbreak — outbreak picks a
// random voice line 0..2 and a chronicle text id 7329 + r (r = RandNext()%3).
// Returns the selected text id (advances the global crt LCG exactly once).
int ChroniclePlagueOutbreakTextId();

// gilde.exe 0x535e64 VIBE_History_NotifyPlagueSpreadStep — gated on kind 6/7;
// picks r = RandNext()%2, text id 7332 + r (the "_0%i" voice line uses r+3).
// Returns the text id, or -1 when the kind is not important. Advances the LCG once
// (the RandNext draw happens before the gate test in the original, so the draw is
// consumed regardless — reproduced here).
int ChroniclePlagueSpreadStepTextId(int recordKindByte);

// gilde.exe 0x535edc VIBE_History_BroadcastPlagueSpread — picks r = RandNext()%2,
// text id 7335 + r (voice line uses r+18). Returns the selected text id (advances
// the LCG once).
int ChroniclePlagueSpreadTextId();

// ===========================================================================
// In-memory chronicle  (the dated event log the parser/scanner operate on).
// ===========================================================================
// The shipped chronicle is a text file of "<<DD.MM.YYYY>>" labels followed by the
// event text; VIBE_History_ParseDate splits the date and ScanNextEventForward
// walks entries in stored order. We model the parsed form as a list of dated
// entries and recover the add / chronological-scan / dated-text-format rules so
// they can be exercised without the file buffer.
struct ChronicleEntry {
    i32         day;        // absolute game day the entry is dated to
    int         month;      // 1..12 (for the DD.MM.YYYY format)
    int         year;       // absolute year (>= kChronicleBaseYear)
    int         textId;     // chronicle message id (the Notify selector output)
    const char* text;       // optional rendered text (engine-formatted; may be null)
};

constexpr int kChronicleCapacity = 256;

class Chronicle {
public:
    Chronicle() : count_(0) {}

    void Clear() { count_ = 0; }
    int  Count() const { return count_; }
    // Bound the index to the populated range (the engine only ever reads loaded
    // entries); an out-of-range index clamps to a valid slot rather than reading
    // past the live entries. entries_[0] is always a valid object (the array is a
    // fixed member), so a clamp to 0 on an empty/oob index is well-defined.
    const ChronicleEntry& At(int i) const {
        if (i < 0) i = 0;
        else if (i >= count_) i = (count_ > 0) ? count_ - 1 : 0;
        return entries_[i];
    }

    // Appends an entry. Entries are kept in chronological (ascending day) order by
    // insertion-sort on the day key, matching the file's stored order the scanner
    // assumes. Returns the insert index, or -1 if the chronicle is full.
    int Add(const ChronicleEntry& e);

    // gilde.exe ScanNextEventForward driver: returns the index of the first entry
    // dated to (currentDay - 1) — the "yesterday" window the original emits — or
    // -1 if none. Uses HistoryClassifyEntry (world/history.h) for the per-entry
    // decision, stopping once an entry is dated on/after currentDay.
    int ScanNextForward(i32 currentDay) const;

    // Formats entry `i`'s date as the chronicle's "DD.MM.YYYY" label into `out`
    // (which must hold at least 11 bytes). Returns out. Mirrors the inverse of
    // ParseDate's fixed-width fields.
    char* FormatDate(int i, char* out) const;

private:
    ChronicleEntry entries_[kChronicleCapacity];
    int            count_;
};

} // namespace guild::world
