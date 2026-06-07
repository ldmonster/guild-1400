#include "world/history.h"

#include <cstring>

// Faithful 1:1 port of the recoverable VIBE_History_* data rules.

namespace guild::world {

namespace {
// Mirror of VIBE_Util_ParseInt over a fixed-width digit substring: parses the
// leading decimal digits, stopping at the first non-digit. Matches the engine
// helper closely enough for the day/month/year fields (plain unsigned decimal).
i32 ParseIntField(const char* p, int maxLen) {
    i32 v = 0;
    for (int i = 0; i < maxLen && p[i] >= '0' && p[i] <= '9'; ++i)
        v = v * 10 + (p[i] - '0');
    return v;
}
} // namespace

// gilde.exe 0x4fe2d4 — VIBE_History_ParseDate (parse portion).
bool HistoryParseDate(const char* dateText, ParsedDate* out) {
    if (!dateText)
        return false;
    if (std::strlen(dateText) != 10)        // strlen(v64) == 10 gate
        return false;

    // "DD.MM.YYYY": day = chars[0:2], month = chars[3:5], year = chars[6:10].
    i32 day   = ParseIntField(dateText + 0, 2);
    i32 month = ParseIntField(dateText + 3, 2);
    i32 year  = ParseIntField(dateText + 6, 4);

    int mode = 0;                            // v72
    if (day == 0) {                          // if (!v40) { v40 = 1; v72 = 1; }
        day  = 1;
        mode = 1;
    }
    if (month == 0) {                        // if (!v39) { v39 = 1; v72 = 2; }
        month = 1;
        mode  = 2;
    }

    out->year       = year;
    out->day        = day;
    out->month      = month;
    out->yearOffset = year - kChronicleBaseYear;   // *v69 = v38 - 1400
    out->mode       = mode;
    return true;
}

// gilde.exe 0x4fe694 — VIBE_History_ScanNextEventForward (classification core).
HistoryScanResult HistoryClassifyEntry(i32 currentDay, i32 entryDay) {
    i32 diff = currentDay - entryDay;        // qword_13CE852 - v8[0]
    if (diff == 1)
        return HistoryScanResult::kEmit;     // exactly the prior-day window
    if (diff <= 1)                           // (int) <= 1 (and != 1) -> stop
        return HistoryScanResult::kStop;
    return HistoryScanResult::kContinue;     // older -> keep scanning
}

// gilde.exe VIBE_History_Notify* — shared target-kind gate.
bool HistoryNotifyKindIsImportant(int recordKindByte) {
    return recordKindByte == 6 || recordKindByte == 7;
}

// gilde.exe 0x535780 — VIBE_History_NotifyArrestTarget (text-id selection).
int HistoryNotifyArrestTextId(int recordKindByte, bool detained, i32 currentDay) {
    if (!HistoryNotifyKindIsImportant(recordKindByte))
        return -1;
    if (!detained)
        return 7313;                         // *(a2+12) == 0 -> base id
    return currentDay >= 117 ? 7315 : 7314;  // qword_13CE852 >= 117 ? 7315 : 7314
}

// gilde.exe 0x5357f8 — VIBE_History_NotifyUseItemEvent.
int HistoryNotifyUseItemTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7316 : -1;
}

// gilde.exe 0x535844 — VIBE_History_NotifyBuildingLinkRemoved.
int HistoryNotifyBuildingLinkRemovedTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7318 : -1;
}

// gilde.exe 0x535890 — VIBE_History_NotifyOfficeTransfer.
int HistoryNotifyOfficeTransferTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7317 : -1;
}

// gilde.exe 0x535d88 — VIBE_History_NotifyCrimeAdded.
int HistoryNotifyCrimeAddedTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7328 : -1;
}

// gilde.exe 0x536070 — VIBE_History_NotifyRivalEvent (ungated).
int HistoryNotifyRivalEventTextId() {
    return 4954;
}

// gilde.exe 0x535d20 — VIBE_History_NotifyLawChangeToMaster.
//   if ( *a1 != (u16)word_63CC5C ) render 7327; else early-out.
int HistoryNotifyLawChangeTextId(int targetId, int localPlayerId) {
    return targetId != localPlayerId ? 7327 : -1;
}

// gilde.exe 0x535f64 — VIBE_History_NotifyOfficeSwap.
int HistoryNotifyOfficeSwapTextId(int subjectKindByte, int partyAKindByte,
                                  int partyBKindByte) {
    if (partyAKindByte == 6)            // *((_BYTE*)a2 + 2) == 6
        return 6604;
    if (partyBKindByte == 6)            // *((_BYTE*)a3 + 2) == 6
        return 6604;
    if (subjectKindByte != 6)           // *((_BYTE*)result + 2) != 6
        return 6605;
    return -1;
}

} // namespace guild::world
