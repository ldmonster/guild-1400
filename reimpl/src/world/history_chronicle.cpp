#include "world/history_chronicle.h"

#include "world/history.h"   // HistoryNotifyKindIsImportant, HistoryClassifyEntry
#include "crt/rand.h"        // crt::RandNext (the LCG behind VIBE_Math_RandomModulo)

// Faithful port of the remaining VIBE_History_Notify* selectors, the plague RNG
// selectors, and an in-memory chronicle add/scan/format. The plague selectors use
// VIBE_Math_RandomModulo(n) == (int)VIBE_Util_RandNext() % n (gilde.exe 0x58b89c),
// i.e. our crt::RandNext() % n — reproduced exactly so seeded draws match.

namespace guild::world {

namespace {
// VIBE_Math_RandomModulo (gilde.exe 0x58b89c): RandNext()%n, n==0 -> 0.
int RandomModulo(int n) {
    if (n == 0)
        return 0;
    return crt::RandNext() % n;
}
} // namespace

// gilde.exe 0x5358e0.
int ChronicleWanderATextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7319 : -1;
}

// gilde.exe 0x535928.
int ChronicleWanderBTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 7320 : -1;
}

// gilde.exe 0x535970 — gated on kind != 7 per party.
int ChronicleWanderPairLines(int selfKindByte, int otherKindByte,
                             int* selfEmitId, int* otherEmitId) {
    int lines = 0;
    int selfId = -1, otherId = -1;
    if (selfKindByte != 7) {           // *((_BYTE*)result + 2) != 7
        selfId = 7321;
        ++lines;
    }
    if (otherKindByte != 7) {          // *(_BYTE*)(a2 + 2) != 7
        otherId = 7322;
        ++lines;
    }
    if (selfEmitId)  *selfEmitId  = selfId;
    if (otherEmitId) *otherEmitId = otherId;
    return lines;
}

// gilde.exe 0x535a04.
int ChronicleAttackDefenderTextId(int defenderKindByte) {
    return HistoryNotifyKindIsImportant(defenderKindByte) ? 7323 : -1;
}

int ChronicleAttackSuffixTextId(bool attackerHasFlag) {
    // (*((_BYTE*)a1 + 9) != 0) + 7325
    return (attackerHasFlag ? 1 : 0) + 7325;
}

// gilde.exe 0x535dd8 — RandomModulo(3) -> 7329 + r.
int ChroniclePlagueOutbreakTextId() {
    int r = RandomModulo(3);                 // v2 = VIBE_Math_RandomModulo(3u)
    return r + 7329;                         // v2 + 7329
}

// gilde.exe 0x535e64 — RandomModulo(2) drawn before the gate; 7332 + r when kind 6/7.
int ChroniclePlagueSpreadStepTextId(int recordKindByte) {
    int r = RandomModulo(2);                 // result = VIBE_Math_RandomModulo(2u)
    if (!HistoryNotifyKindIsImportant(recordKindByte))
        return -1;                           // gate fails after the draw
    return r + 7332;                         // v5 + 7332
}

// gilde.exe 0x535edc — RandomModulo(2) -> 7335 + r.
int ChroniclePlagueSpreadTextId() {
    int r = RandomModulo(2);                 // v2 = VIBE_Math_RandomModulo(2u)
    return r + 7335;                         // v2 + 7335
}

// ===========================================================================
// In-memory chronicle.
// ===========================================================================
int Chronicle::Add(const ChronicleEntry& e) {
    if (count_ >= kChronicleCapacity)
        return -1;
    // Insertion-sort by day (stable for equal days: new entry goes after equals,
    // preserving stored/insertion order the file scanner relies on).
    int i = count_;
    while (i > 0 && entries_[i - 1].day > e.day) {
        entries_[i] = entries_[i - 1];
        --i;
    }
    entries_[i] = e;
    ++count_;
    return i;
}

int Chronicle::ScanNextForward(i32 currentDay) const {
    for (int i = 0; i < count_; ++i) {
        HistoryScanResult r = HistoryClassifyEntry(currentDay, entries_[i].day);
        if (r == HistoryScanResult::kEmit)
            return i;                         // dated to currentDay - 1
        if (r == HistoryScanResult::kStop)
            return -1;                        // reached an entry on/after today
        // kContinue: older entry, keep scanning.
    }
    return -1;
}

char* Chronicle::FormatDate(int i, char* out) const {
    const ChronicleEntry& e = entries_[i];
    // Inverse of ParseDate's fixed-width "DD.MM.YYYY": two digits, dot, two
    // digits, dot, four digits. Day/month clamped to two digits, year to four.
    auto two = [](char* p, int v) {
        p[0] = static_cast<char>('0' + (v / 10) % 10);
        p[1] = static_cast<char>('0' + v % 10);
    };
    int dayOfMonth = e.day % 100;             // synthetic: keep two digits
    if (dayOfMonth == 0) dayOfMonth = 1;
    two(out + 0, dayOfMonth);
    out[2] = '.';
    two(out + 3, e.month);
    out[5] = '.';
    int y = e.year;
    out[6] = static_cast<char>('0' + (y / 1000) % 10);
    out[7] = static_cast<char>('0' + (y / 100) % 10);
    out[8] = static_cast<char>('0' + (y / 10) % 10);
    out[9] = static_cast<char>('0' + y % 10);
    out[10] = '\0';
    return out;
}

} // namespace guild::world
