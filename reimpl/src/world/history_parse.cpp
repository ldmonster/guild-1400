#include "world/history_parse.h"

#include <cstring>

// Faithful port of the dated-text formatter (inverse of VIBE_History_ParseDate
// 0x4fe2d4) and the three "target" Notify selectors (gilde.exe 0x535afc/0x535bb0/
// 0x535c68). The Notify functions all share the kind-6/7 gate; the voice-line bases
// (3960/3964/3967/3970/3974/3977/3980) and chronicle text ids (3953/3963/3973) are
// recovered exactly. The He broadcast + VIBE_Text_RenderFormattedMessage are mocked
// away (only the rule-relevant ids are returned).

namespace guild::world {

namespace {
void TwoDigits(char* p, int v) {
    p[0] = static_cast<char>('0' + (v / 10) % 10);
    p[1] = static_cast<char>('0' + v % 10);
}
} // namespace

char* HistoryFormatDate(int day, int month, int year, char* out) {
    if (day == 0)   day = 1;        // ParseDate's v40==0 -> 1 normalisation
    if (month == 0) month = 1;      // ParseDate's v39==0 -> 1 normalisation
    TwoDigits(out + 0, day % 100);
    out[2] = '.';
    TwoDigits(out + 3, month % 100);
    out[5] = '.';
    out[6] = static_cast<char>('0' + (year / 1000) % 10);
    out[7] = static_cast<char>('0' + (year / 100) % 10);
    out[8] = static_cast<char>('0' + (year / 10) % 10);
    out[9] = static_cast<char>('0' + year % 10);
    out[10] = '\0';
    return out;
}

bool HistoryRoundtripDate(const char* dateText, char* out, ParsedDate* parsed) {
    ParsedDate p;
    if (!HistoryParseDate(dateText, &p))
        return false;
    if (parsed)
        *parsed = p;
    HistoryFormatDate(p.day, p.month, p.year, out);
    return true;
}

// gilde.exe 0x535afc.
int HistoryNotifyTargetFoundTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 3953 : -1;
}

// gilde.exe 0x535bb0.
int HistoryNotifyTargetReachedATextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 3963 : -1;
}
int HistoryNotifyTargetReachedAVoiceBase(bool detained) {
    return detained ? 3967 : 3964;   // *((_BYTE*)v4 + 9) ? 3967 : 3964
}

// gilde.exe 0x535c68.
int HistoryNotifyTargetReachedBTextId(int recordKindByte) {
    return HistoryNotifyKindIsImportant(recordKindByte) ? 3973 : -1;
}
int HistoryNotifyTargetReachedBVoiceBase(bool detained) {
    return detained ? 3977 : 3974;   // *((_BYTE*)v4 + 9) ? 3977 : 3974
}

} // namespace guild::world
