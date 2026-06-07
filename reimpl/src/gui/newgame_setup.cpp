#include "gui/newgame_setup.h"

#include <cstring>

namespace guild::gui {

// ---------------------------------------------------------------------------
// CHOOSEPROFESSION grid geometry — gilde.exe 0x52c5b8:
//   VIBE_Object_AddToWindow(win, 80*(i/3)+100, 96*(i%3)+100, beruf[i]+1349)
// (AddToWindow takes y then x; gfx = berufByte + 1349).
// ---------------------------------------------------------------------------
int Profession_ButtonX(int index) {
    if (index < 0 || index >= kProfessionCount) return -1;
    return kProfessionCellW * (index % kProfessionColumns) + kProfessionOriginX;
}

int Profession_ButtonY(int index) {
    if (index < 0 || index >= kProfessionCount) return -1;
    return kProfessionCellH * (index / kProfessionColumns) + kProfessionOriginY;
}

int Profession_ButtonGfx(int berufByte) {
    return berufByte + kProfessionGfxBase;
}

// ---------------------------------------------------------------------------
// CHOOSEPLAYER wappen grid — gilde.exe 0x52cf3f:
//   columns = (windowWidthPx - 32) / 48
//   x = 48*(i%columns)+80,  y = 48*(i/columns)+32,  gfx/id = 1342 + i
// ---------------------------------------------------------------------------
int Wappen_ColumnCount(int windowWidthPx) {
    int c = (windowWidthPx - kWappenMarginPx) / kWappenCellPx;
    return c > 0 ? c : 1; // guard against degenerate windows (the original assumes >0)
}

int Wappen_ButtonX(int index, int columns) {
    if (columns <= 0) columns = 1;
    return kWappenCellPx * (index % columns) + kWappenOriginX;
}

int Wappen_ButtonY(int index, int columns) {
    if (columns <= 0) columns = 1;
    return kWappenCellPx * (index / columns) + kWappenOriginY;
}

// ---------------------------------------------------------------------------
// RunChooseCity model bits.
// ---------------------------------------------------------------------------
const char* ChooseCity_Extension(bool network) {
    // 0x52e749: if (v73) v5 = ".NET"; else v5 = ".CTY";
    return network ? kCityExtNet : kCityExtLocal;
}

bool ChooseCity_IsConfirm(int clickedId, int enterKey) {
    // 0x52ed4e: if (dword_75BF38 == 1210 || byte_67225C == 28)
    return clickedId == 1210 || enterKey == 28;
}

bool ChooseCity_IsCityObject(const std::string& objectName) {
    // 0x52ea33: VIBE_Util_StrncmpN(name, "stadt_", 6) == 0
    const std::size_t n = std::strlen(kCityNamePrefix);
    if (objectName.size() < n) return false;
    return std::memcmp(objectName.data(), kCityNamePrefix, n) == 0;
}

// ---------------------------------------------------------------------------
// RunChooseHistory button -> flag.
//   slot 0 (v32) -> History_SetActiveFlag(1)
//   slot 1 (v33) -> History_SetActiveFlag(2)
//   slot 2 (v34) -> History_SetActiveFlag(0)
//   slot 3       -> id 1155 (Cancel)
// ---------------------------------------------------------------------------
int ChooseHistory_FlagForButton(int buttonSlot) {
    switch (buttonSlot) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 0;
        default: return -1; // Cancel / out of range
    }
}

// ---------------------------------------------------------------------------
// ChooseProfession widget id -> profession byte.
//   The build loop creates buttons with widget +8 id == beruf[i] + 1349; the click loop
//   (0x52c693) scans for `(beruf[k]>>... )+1349 == dword_75BF38`.  Here the picked widget
//   id maps directly back to a profession byte (id - 1349) and we validate it appears in
//   the beruf table (the 8 ids the grid actually built).  `berufTable` may be null to
//   skip the table check (then any id in [1349, 1349+255] yields id-1349).
// ---------------------------------------------------------------------------
int ChooseProfession_ByteForWidgetId(int widgetId, const int* berufTable) {
    int b = widgetId - kProfessionGfxBase;
    if (b < 0 || b > 0xFF) return -1;
    if (berufTable) {
        for (int i = 0; i < kProfessionCount; ++i)
            if (berufTable[i] == b) return b;
        return -1;
    }
    return b;
}

// ---------------------------------------------------------------------------
// Sink.
// ---------------------------------------------------------------------------
namespace {
NewGameSink g_defaultSink;
NewGameSink* g_sink = &g_defaultSink;
} // namespace

void NewGame_SetSink(NewGameSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// ---------------------------------------------------------------------------
// Flow steps.
// ---------------------------------------------------------------------------
bool NewGame_ApplyCity(NewGameParams& p, const std::string& cityName,
                       const std::string& cityFile, bool network) {
    p.cityName = cityName;
    p.cityFile = cityFile;
    p.network = network;
    return true; // RunChooseCity returns v77 = 1 on confirm
}

bool NewGame_ApplyHistory(NewGameParams& p, int buttonSlot) {
    int flag = ChooseHistory_FlagForButton(buttonSlot);
    if (flag < 0) return false; // Cancel (id 1155) -> word_63C740 = 0, return 0
    p.historyFlag = flag;
    g_sink->SetHistoryFlag(flag); // VIBE_History_SetActiveFlag(flag)
    return true;
}

void NewGame_ApplyPlayer(NewGameParams& p, const std::string& firstName,
                         const std::string& familyName, int wappenIndex, int gender,
                         int faith) {
    // The RunChoosePlayer commit writes these to [Network] + the globals; we record them.
    p.firstName = firstName;       // String (Vorname)
    p.familyName = familyName;      // byte_122F4CA (Nachname)
    p.wappen = wappenIndex;         // dword_122F4A4 == 1342 + index; we keep the index
    p.gender = gender;              // byte_122F4A8
    p.faith = faith;                // byte_122F4A9
}

bool NewGame_ApplyProfession(NewGameParams& p, int berufByte) {
    if (berufByte < 0) return false;
    p.profession = berufByte;
    // 0x52c6a9: HIBYTE(dword_122F4A0) = BuildingType_ComputeVariantIndex(berufByte, 1)
    p.professionVariant = g_sink->ComputeProfessionVariant(berufByte);
    return true; // ChooseProfession returns v23 = 1
}

void NewGame_Commit(NewGameParams& p) {
    // RunChooseHistory tail (0x52da92): word_63C740 |= 8; ChooseProfession commit
    // (0x52da1a / 0x52c6b3): dword_122F528 = 1555; dword_631614 = 1.  EnterChooseCity
    // success (0x52a359): word_63C740 |= 1.
    p.sessionFlags |= kSessionNewGame | kSessionHistory;
    if (p.network) p.sessionFlags |= kSessionNetwork;
    p.started = true;
    g_sink->StartSession(p);
}

} // namespace guild::gui
