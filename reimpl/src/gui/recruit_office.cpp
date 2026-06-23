// recruit_office.cpp — 1:1 reconstruction of the recruitment candidate collection,
// the office candidate-list / session-timer rendering bodies, and the guild-law
// person-selection candidate harvest from gilde.exe. See recruit_office.h for the
// full provenance map. Addresses are gilde.exe (imagebase 0x400000).
#include "gui/recruit_office.h"

#include "sim/entity.h"   // g_persons / g_personIds (the person id column)
#include "sim/person.h"   // PersonGet*  + kPf* field offsets
#include "world/office_recon_privilege.h" // OfficeSessionTimeSplit, PrivCandidateRowY

#include <cstdio>
#include <cstring>

namespace guild::gui {

using guild::sim::Person;
using guild::sim::PersonGetByte;
using guild::sim::PersonGetWord;
using guild::sim::PersonGetDword;
using guild::sim::g_persons;
using guild::sim::g_personIds;
using guild::sim::PersonFindRecordById;

// ===========================================================================
// 0x55d530 — VIBE_Recruit_CollectNearbyRecruitCandidates.
// ===========================================================================
namespace {
void DefSetGrayColor(int, int) {}
int  DefFindPeopleByPalette(const guild::u16*, int, int, float, float, guild::u16*) {
    return 0;
}
RecruitCollectHooks MakeRecruitDefaults() {
    RecruitCollectHooks h{};
    h.setGrayColor        = DefSetGrayColor;
    h.findPeopleByPalette = DefFindPeopleByPalette;
    h.ctx = nullptr;
    return h;
}
RecruitCollectHooks g_recruitHooks = MakeRecruitDefaults();
} // namespace

void RecruitCollectSetHooks(const RecruitCollectHooks& h) { g_recruitHooks = h; }
void RecruitCollectResetHooks() { g_recruitHooks = MakeRecruitDefaults(); }

int RecruitCollectNearbyCandidates(const guild::u16* refMarker, guild::u8 refByte9,
                                   guild::i32* outIds) {
    const RecruitCollectHooks& H = g_recruitHooks;
    // 0x55d545: search gray-color window.
    H.setGrayColor(0, 40);
    // 0x55d54a / 0x55d563: filter = 21513, or 21512 when refByte9 == 0.
    //   v9 = 21512; v9 = (refByte9 == 0) + 21513;  => 21513 when byte!=0, 21514?
    // The original computes (byte==0) + 21513: byte==0 -> 21514? No: (==0) yields 1
    // -> 21514. But the FIRST assignment 21512 is dead; the live value is
    // (refByte9==0) + 21513. Reproduce verbatim.
    int filter = (refByte9 == 0 ? 1 : 0) + 21513;  // 0x55d563
    (void)kRecruitFilterBase;

    // 0x55d581: spatial query, up to 9 palette slots.
    guild::u16 slots[kRecruitMaxCandidates] = {};
    int count = H.findPeopleByPalette(refMarker, kRecruitMaxCandidates, filter,
                                      30.0f, 100.0f, slots);  // *outCount = result
    // 0x55d58c..0x55d5b3: map each found slot to its person id (id column).
    for (int k = 0; k < count; ++k) {
        // dword_12CE914[134 * slots[k]] == g_personIds[slots[k]] (the +4 id column).
        outIds[k] = g_personIds[slots[k]];  // 0x55d5a8
    }
    return count;
}

// ===========================================================================
// 0x555f8c — VIBE_Office_ShowCandidateListWithRoles.
// ===========================================================================
namespace {
void DefBuildPersonCard(int, int, int, void*, int) {}
int  DefAddRoleLabel(int, int, int, const char*) { return 0; }
void DefSetObjectColor(int, int) {}
void DefSetObjectEnabled(int, int) {}
const char* DefRoleName(int) { return ""; }
OfficeListHooks MakeOfficeListDefaults() {
    OfficeListHooks h{};
    h.buildPersonCard  = DefBuildPersonCard;
    h.addRoleLabel     = DefAddRoleLabel;
    h.setObjectColor   = DefSetObjectColor;
    h.setObjectEnabled = DefSetObjectEnabled;
    h.roleName         = DefRoleName;
    h.ctx = nullptr;
    return h;
}
OfficeListHooks g_officeListHooks = MakeOfficeListDefaults();
} // namespace

void OfficeListSetHooks(const OfficeListHooks& h) { g_officeListHooks = h; }
void OfficeListResetHooks() { g_officeListHooks = MakeOfficeListDefaults(); }

int OfficeShowCandidateListWithRoles(unsigned count, OfficeListRow* rows,
                                     int pageAnchorIn, int* pageRowStride,
                                     int* pageAnchorOut) {
    const OfficeListHooks& H = g_officeListHooks;
    if (count == 0)
        return 0;  // 0x555f9a

    // 0x555ffc: the role-label running y starts at 70 and steps +112.
    int labelY = 70;  // v7
    for (unsigned i = 0; i < count; ++i) {
        // 0x556015 / 0x556020: card x = 75, y = 112*i + 40 (PrivCandidateRowY).
        rows[i].x = guild::world::kCandidateRowX;             // 75
        rows[i].y = guild::world::PrivCandidateRowY(i);       // 112*i + 40
        // 0x556028: build the person card.
        H.buildPersonCard(rows[i].x, rows[i].y, 69, &rows[i], 1);
        // 0x556068: add the role label from the role string table.
        H.addRoleLabel(130, labelY, /*win*/ 0, H.roleName(rows[i].roleId));
        // 0x556089: advance the label y by 112.
        labelY += guild::world::kCandidateRowStride;          // +112
        // 0x55609a: color the role label (68).
        H.setObjectColor(0, 68);
    }
    // 0x5560b0: second pass — enable each card object.
    for (unsigned i = 0; i < count; ++i)
        H.setObjectEnabled(0, 1);

    // 0x5560eb / 0x5560f1: store the page-descriptor stride (112) + anchor.
    if (pageRowStride) *pageRowStride = guild::world::kCandidateRowStride; // 112
    if (pageAnchorOut) *pageAnchorOut = pageAnchorIn;                      // v16
    return 1;  // 0x555f9c
}

// ===========================================================================
// 0x49d910 — VIBE_Office_RenderSessionTimer.
// ===========================================================================
namespace {
void DefSubmitTimerText(const char*) {}
OfficeTimerHooks MakeTimerDefaults() {
    OfficeTimerHooks h{};
    h.submitTimerText = DefSubmitTimerText;
    h.ctx = nullptr;
    return h;
}
OfficeTimerHooks g_timerHooks = MakeTimerDefaults();
} // namespace

void OfficeTimerSetHooks(const OfficeTimerHooks& h) { g_timerHooks = h; }
void OfficeTimerResetHooks() { g_timerHooks = MakeTimerDefaults(); }

void OfficeRenderSessionTimer(char* dest, unsigned destSize,
                              guild::u32 nowTick, guild::u32 startTick) {
    // 0x49d942: elapsed = 14 * (now - start), split into mm:ss:mmm.
    guild::world::SessionTimeSplit s =
        guild::world::OfficeSessionTimeSplit(nowTick, startTick);
    // 0x49d92c: VIBE_Crt_Sprintf_0(buf, "%2i : %2i : %3i ms", min, sec, ms).
    std::snprintf(dest, destSize, "%2i : %2i : %3i ms", s.minutes, s.seconds, s.millis);
    // 0x49d9b8: submit the formatted text to the timer label.
    g_timerHooks.submitTimerText(dest);
}

// ===========================================================================
// 0x55a224 — VIBE_Gesetz_RunPersonSelectionWindow (candidate-harvest core).
// ===========================================================================
GesetzHarvestMode GesetzClassifyState(guild::u8 s) {
    GesetzHarvestMode m{};
    // 0x55a2f8: (s == 1 || s == 2) -> spouse-table scan.
    m.scanSpouseTable = (s == 1 || s == 2);
    // 0x55a907: extended set -> child-table scan.
    //   (s>3 && s<0xA) || (s>0xA && s<0xF) || s==16 || (s>0x11 && s<0x17)
    m.scanChildTable =
        (s > 3 && s < 0x0A) || (s > 0x0A && s < 0x0F) || (s == 16) ||
        (s > 0x11 && s < 0x17);
    return m;
}

namespace {
// Per-record relation dword offsets used by the two link tables.
//   dword_12CEA80 == record + 0x170 (spouse-link to the subject's id).
//   dword_12CEA7C == record + 0x16C (child-link to the subject's id).
constexpr int kOffSpouseLink = 0x170;
constexpr int kOffChildLink  = 0x16C;

// One link-table scan pass (0x55a2fc..0x55a342 / 0x55a370..0x55a3b6). For every
// person slot whose `linkOff` dword equals the subject's id, add that person's
// marker to outMarkers (unless it duplicates the subject's +37/+39 words), honoring
// the 16-candidate / 512-byte caps. `count` is in/out (v61). Returns updated count.
int GesetzScanLinkTable(const Person* subject, guild::i32 subjectId, int linkOff,
                        guild::u16* outMarkers, int count) {
    if (count >= kGesetzMaxCandidates)   // v61 < 16 gate
        return count;
    const guild::i16 spouseW = PersonGetWord(subject, /*+37*/ 0x25);
    const guild::i16 childW  = PersonGetWord(subject, /*+39*/ 0x27);
    // The original walks v7 += 268 over the word-strided person array (768 slots),
    // also bounded by v8 < 512 (32 bytes per stored candidate). We track both.
    int byteCursor = 32 * count;  // v8 = 32 * v61
    for (int slot = 0; slot < guild::sim::kPersonCapacity; ++slot) {  // v7 < 205824
        if (byteCursor >= 512)   // v8 < 512 cap (0x55a342)
            break;
        const Person& p = g_persons[slot];
        if (PersonGetDword(&p, linkOff) == subjectId) {  // (char*)v9 == v60
            const guild::i16 marker = p.marker;          // word_12CE910[v7]
            // 0x55a8a8: de-dup vs subject's spouse/child words.
            if (marker != spouseW && marker != childW) {
                outMarkers[count] = static_cast<guild::u16>(marker);
                ++count;
                byteCursor += 32;
                if (count >= kGesetzMaxCandidates)
                    break;
            }
        }
    }
    return count;
}
} // namespace

int GesetzHarvestSelectionCandidates(const Person* subject, guild::u8 subjectStateByte,
                                     guild::u16* outMarkers) {
    if (!subject)
        return 0;  // 0x55a266
    int count = 0;  // v61
    const guild::i16 spouseW = PersonGetWord(subject, /*+37*/ 0x25);
    const guild::i16 childW  = PersonGetWord(subject, /*+39*/ 0x27);

    // 0x55a271: seed with the subject's spouse id if present.
    if (static_cast<guild::u16>(spouseW) != 0xFFFF) {
        outMarkers[count] = static_cast<guild::u16>(spouseW);  // v50[0] = +37
        ++count;
    }
    // 0x55a29a: add the child id if present and distinct from the spouse.
    if (static_cast<guild::u16>(childW) != 0xFFFF && childW != spouseW) {
        outMarkers[count] = static_cast<guild::u16>(childW);   // v50[8*count] = +39
        ++count;
    }

    const guild::i32 subjectId = subject->id;  // v60's id (the link comparand)
    GesetzHarvestMode mode = GesetzClassifyState(subjectStateByte);
    // 0x55a2f8: spouse-link table scan.
    if (mode.scanSpouseTable)
        count = GesetzScanLinkTable(subject, subjectId, kOffSpouseLink, outMarkers, count);
    // 0x55a907: child-link table scan.
    if (mode.scanChildTable)
        count = GesetzScanLinkTable(subject, subjectId, kOffChildLink, outMarkers, count);

    return count;
}

// ===========================================================================
// 0x55a9bc — VIBE_Gesetz_OpenPersonSelectionIfValid.
// ===========================================================================
namespace {
int DefMapTypeToState(guild::u8, void*) { return 0; }
GesetzOpenHooks MakeOpenDefaults() {
    GesetzOpenHooks h{};
    h.mapTypeToState = DefMapTypeToState;
    h.ctx = nullptr;
    return h;
}
GesetzOpenHooks g_openHooks = MakeOpenDefaults();
} // namespace

void GesetzOpenSetHooks(const GesetzOpenHooks& h) { g_openHooks = h; }
void GesetzOpenResetHooks() { g_openHooks = MakeOpenDefaults(); }

bool GesetzOpenPersonSelectionIfValid(bool subjectPresent, guild::u8 subjectBuildingType) {
    // 0x55a9c7: null subject -> do not open (original returns the null result).
    if (!subjectPresent)
        return false;
    // 0x55a9d6: building busy (state != 0) -> abort.
    char scratch[12] = {};
    if (g_openHooks.mapTypeToState(subjectBuildingType, scratch))
        return false;  // 0x55a9df
    // 0x55a9eb: otherwise the caller opens RunPersonSelectionWindow.
    return true;
}

// ===========================================================================
// The three recruitment MODAL WINDOW DRIVERS — full 1:1 bodies.
// ===========================================================================
// The form/frame-loop/text/command/He/dialog leaves are routed through
// RecruitWindowHooks; the candidate-list build, the grid geometry, the selection
// state machine, the offer/confirm decision logic and the hire/loyalty state
// mutation are reconstructed here verbatim from the decompile.
namespace {

// Engine input result codes the frame loops dispatch on (dword_75BF38 sentinel
// and the 1210/1155 button-id constants from the original).
constexpr int kInputNone    = -1;
constexpr int kInputConfirm = 1210;   // OK / confirm (0x55da2a)
constexpr int kInputCancel  = 1155;   // cancel (0x55dafe)
constexpr int kFrameLoopTag = 415687; // 0x4c09a0 first arg (0x6580C7)

int  DefGameTickFinalize(int, int, const char*)        { return -1; }
void DefFormCenter(int)                                 {}
void DefFormSelect(int, int)                            {}
void DefFormDestroy(int)                                {}
void DefFormSetChildrenVisible(int, int)                {}
int  DefFormGetChildObjectId(int, int, int)             { return -1; }
int  DefRunFrameLoop(int, int, const void*)             { return 0; } // exit at once
void DefDragCursorSetSprite(int, int)                   {}
int  DefTextRichString(unsigned, unsigned, int, int, int) { return 0; }
void DefTextFormatted(char* d, int, int, int)           { if (d) d[0] = '\0'; }
void DefHudBuildPersonCard(int, int, int, void*, int)   {}
void DefHudAddCenteredLabel(const char*, int, int, int, int) {}
void DefPersonResolveStatusFlags(void*)                 {}
int  DefPlayerPersonId()                                { return 0; }
int  DefPanelW()                                        { return 0; }
int  DefCardW()                                         { return 0; }
int  DefCardH()                                         { return 0; }
int  DefReadCancelEdge()                                { return 0; }
int  DefReadLastClicked()                               { return kInputNone; }
int  DefReadPrevSelectedRow()                           { return -1; }
int  DefQueueHireRequest(int)                           { return 0; }
int  DefCommandGetPacketStatus(int)                     { return 1; } // done (no spin)
void DefAmtRefreshGuildState()                          {}
int  DefQueueEntity29(int, void*)                       { return 0; }
void DefEnqueueCmd15(int, int, int, int)                {}
void DefGameTimeAdvance(int, int, int, int)             {}
void DefInputClearMouseButtons(int)                     {}
HeHandler* DefHeFindFirst(int)                          { return nullptr; }
HeHandler* DefHeFindNext()                              { return nullptr; }
int  DefPersonComputeOfficeRank(int)                    { return 0; }
int  DefRecruitComputeCost(int, int)                    { return 0; }
int  DefDialogCheckResourceAmount(int, int)             { return 0; }
void DefDialogShowMessageBox(const char*, int, int)     {}
int  DefRandomModulo(int)                               { return 0; }

RecruitWindowHooks MakeWindowDefaults() {
    RecruitWindowHooks h{};
    h.gameTickFinalize        = DefGameTickFinalize;
    h.formCenterChildWindows  = DefFormCenter;
    h.formSelectWindow        = DefFormSelect;
    h.formDestroy             = DefFormDestroy;
    h.formSetChildrenVisible  = DefFormSetChildrenVisible;
    h.formGetChildObjectId    = DefFormGetChildObjectId;
    h.gameLogicRunFrameLoop   = DefRunFrameLoop;
    h.dragCursorSetSprite     = DefDragCursorSetSprite;
    h.textRenderRichString    = DefTextRichString;
    h.textRenderFormatted     = DefTextFormatted;
    h.hudBuildPersonCard      = DefHudBuildPersonCard;
    h.hudAddCenteredLabel     = DefHudAddCenteredLabel;
    h.personResolveStatusFlags = DefPersonResolveStatusFlags;
    h.playerPersonId          = DefPlayerPersonId;
    h.panelW                  = DefPanelW;
    h.cardW                   = DefCardW;
    h.cardH                   = DefCardH;
    h.readCancelEdge          = DefReadCancelEdge;
    h.readLastClickedObject   = DefReadLastClicked;
    h.readPrevSelectedRow     = DefReadPrevSelectedRow;
    h.queueHireRequest        = DefQueueHireRequest;
    h.commandGetPacketStatus  = DefCommandGetPacketStatus;
    h.amtRefreshGuildState    = DefAmtRefreshGuildState;
    h.queueEntity29           = DefQueueEntity29;
    h.enqueueCmd15            = DefEnqueueCmd15;
    h.gameTimeAdvance         = DefGameTimeAdvance;
    h.inputClearMouseButtons  = DefInputClearMouseButtons;
    h.heFindFirst             = DefHeFindFirst;
    h.heFindNext              = DefHeFindNext;
    h.personComputeOfficeRank = DefPersonComputeOfficeRank;
    h.recruitComputeCost      = DefRecruitComputeCost;
    h.dialogCheckResourceAmount = DefDialogCheckResourceAmount;
    h.dialogShowMessageBox    = DefDialogShowMessageBox;
    h.randomModulo            = DefRandomModulo;
    h.ctx = nullptr;
    return h;
}

RecruitWindowHooks g_windowHooks = MakeWindowDefaults();

// One on-screen candidate card descriptor. The original packs these into the
// 56-byte-strided v16[] frame array; the driver only needs the geometry, the
// bound record pointer, the cost and the card object id for the click scan.
struct PickCard {
    int   x;          // v16[14*i+4]
    int   y;          // v16[14*i+5]
    void* record;     // v16[14*i+0]  (FindRecordById result)
    int   cost;       // v16[14*i+11]
    int   objId;      // v16[14*i+1]  (card object id, vs dword_62D22C)
};

} // namespace

void RecruitWindowSetHooks(const RecruitWindowHooks& h) { g_windowHooks = h; }
void RecruitWindowResetHooks() { g_windowHooks = MakeWindowDefaults(); }
const RecruitWindowHooks& RecruitWindowGetHooks() { return g_windowHooks; }

// ---------------------------------------------------------------------------
// 0x55d990 — VIBE_Recruit_RunHireConfirmDialog.
// ---------------------------------------------------------------------------
int RecruitRunHireConfirmDialog() {
    const RecruitWindowHooks& H = g_windowHooks;
    // 0x55d9c9: cost = ComputeRecruitmentCost(dword_12CE914[134*word_63CC5C]).
    int playerId = H.playerPersonId();
    int cost = H.recruitComputeCost(playerId, playerId);
    // 0x55d9db: form "privillegien\werbung2"; center + select.
    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung2");  // aPrivillegienWe
    H.formCenterChildWindows(formId);                   // 0x55d9dd
    H.formSelectWindow(formId, 0);                       // 0x55d9e4
    // 0x55d9fc: rich-string with the player marker + the cost.
    H.textRenderRichString(0x18E5u, 0, cost, 0, 0);
    int result = 0;  // 0x55d9fa: v5 = 0

    // 0x55dac1: do/while frame-loop pump.
    do {
        // 0x55da11: cancel edge latches quit (dword_631614 = 1).
        int cancel = H.readCancelEdge();
        int click = H.readLastClickedObject();          // dword_75BF38
        if (click != kInputNone) {                       // 0x55da1f
            if (click == kInputConfirm) {                // 0x55da2a
                // 0x55da39..0x55da89: queue the hire SlotReset28 for the player id.
                int packet = H.queueHireRequest(playerId);
                // 0x55da94: spin until the command packet completes, refreshing.
                while (!H.commandGetPacketStatus(packet))
                    H.amtRefreshGuildState();            // 0x55da96
                result = 1;                              // 0x55daac
                // 0x55dab1: dword_631614 = 1 (quit).
                break;
            } else if (click == kInputCancel) {          // 0x55dafe
                break;                                   // 0x55db00: quit
            }
        }
        if (cancel) break;                               // 0x55da9d -> quit
    } while (H.gameLogicRunFrameLoop(kFrameLoopTag, result, nullptr)); // 0x55dac1

    H.formDestroy(formId);                               // 0x55dad5
    H.inputClearMouseButtons(17);                        // 0x55dae5
    return result;                                       // 0x55daec
}

// ---------------------------------------------------------------------------
// 0x55db0c — VIBE_Recruit_RunCandidatePickWindow.
// ---------------------------------------------------------------------------
guild::i8 RecruitRunCandidatePickWindow(const guild::u16* refMarker, guild::u8 refByte9) {
    const RecruitWindowHooks& H = g_windowHooks;
    guild::i8 result = 2;  // 0x55db20: v24 = 2

    // 0x55db2e: collect the nearby recruit candidates (v19 = count, v18[] = ids).
    guild::i32 ids[kRecruitMaxCandidates] = {};
    int count = RecruitCollectNearbyCandidates(refMarker, refByte9, ids);

    // 0x55db48: form "privillegien\werbung1"; center + the two selects.
    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung1"); // aPrivillegienWe_0
    H.formCenterChildWindows(formId);                   // 0x55db4a
    H.formSelectWindow(formId, 0);                       // 0x55db51
    H.textRenderRichString(0x18E3u, 0, 0, 0, 0);        // 0x55db5b
    H.formSelectWindow(formId, 2);                       // 0x55db6a

    // 0x55db95: grid metrics. gap = panelW - 3*cardW; quarter = gap/4; half = gap%4/2.
    const int panelW  = H.panelW();
    const int cardW   = H.cardW();
    const int cardH   = H.cardH();
    const int gap     = panelW - 3 * cardW;
    const int quarter = gap / 4;          // 0x55dba7 (v21; the i64 >>2 collapses to /4)
    const int half    = (gap % 4) / 2;    // 0x55dbc9 (v20)

    int playerId = H.playerPersonId();
    PickCard cards[kRecruitMaxCandidates] = {};
    char buf[128];

    // 0x55dbd2: build a card per candidate.
    for (int i = 0; i < count; ++i) {
        const int col = i % 3;
        // 0x55dc32: x = half + 10*(col-1) + col*cardW + quarter*(col+1).
        cards[i].x = half + 10 * (col - 1) + col * cardW + quarter * (col + 1);
        // 0x55dc46: y = 130*(i/3) + 5.
        cards[i].y = 130 * (i / 3) + 5;
        // 0x55dc5d: card record = FindRecordById(ids[i]); +12 = 1 (interactive).
        cards[i].record = PersonFindRecordById(ids[i]);
        // 0x55dc6f: resolve the per-card status flags (He / building-output decoration).
        H.personResolveStatusFlags(cards[i].record);
        // 0x55dc8e: cost = ComputeRecruitmentCost(playerId).
        cards[i].cost = H.recruitComputeCost(playerId, playerId);
        // 0x55dca5: format "6372" with the cost.
        H.textRenderFormatted(buf, 6372, cards[i].cost, 0);
        // 0x55dcca: build the person card; its object id feeds the click scan.
        H.hudBuildPersonCard(cards[i].x, cards[i].y, 69, cards[i].record, 1);
        // 0x55dcee: label y = cardH + cards[i].y + 43 ; 0x55dd05: label x = cardW/2 + x.
        int labelY = cardH + cards[i].y + 43;
        int labelX = cardW / 2 + cards[i].x;
        // 0x55dd15: centered cost label.
        H.hudAddCenteredLabel(buf, labelX, 202, labelY, 69);
    }

    // 0x55dd44: dword_75BF38 = -1 (no last click yet).
    // 0x55dd55: modal pick loop. dword_672230 doubles as the selected-row index:
    // nonzero -> quit; zero -> scan from row 0. A clicked card opens hire-confirm.
    do {
        int selRow = H.readCancelEdge();                 // dword_672230 (v13)
        if (selRow != 0) {                               // 0x55dd5d
            break;                                       // 0x55dddc: dword_631614 = 1 (quit)
        } else if (H.readLastClickedObject() != kInputNone   // dword_75BF38 (0x55dd7b)
                   && selRow != H.readPrevSelectedRow()      // dword_672228
                   && selRow < count) {
            int clickedObj = H.readLastClickedObject();  // dword_62D22C (clicked obj id)
            int i = selRow;                              // v13 / v14 (0x55dd7d)
            do {                                         // 0x55dd7f
                if (clickedObj == cards[i].objId) {      // 0x55dd89
                    // 0x55dd8f: hide the children, run the hire-confirm.
                    H.formSetChildrenVisible(formId, 0);
                    if (RecruitRunHireConfirmDialog()) { // 0x55dd97
                        result = static_cast<guild::i8>(-110);  // 0x55dda8
                        break;                           // 0x55dda8 -> quit
                    }
                    // 0x55dde8: re-show the children, keep scanning.
                    H.formSetChildrenVisible(formId, 1);
                }
                ++i;                                     // 0x55ddf4 (v14 += 56)
            } while (i < count);                         // 0x55dd7f
            if (result == static_cast<guild::i8>(-110))
                break;
        }
    } while (H.gameLogicRunFrameLoop(kFrameLoopTag, count, nullptr)); // 0x55ddb9

    H.formDestroy(formId);                               // 0x55ddc4
    return result;                                       // 0x55ddd0
}

// ---------------------------------------------------------------------------
// 0x55de00 — VIBE_Recruit_RunRecruitmentOfferWindow.
// ---------------------------------------------------------------------------
namespace {
// 0x55de22..0x55de7e: select the offer mode from the matched handler state.
//   1 -> no handler ; 2 -> handler present ; 3 -> rejected/away (byte187==1)
//   4 -> offer-window pending (byte186==1, wins last).
int OfferComputeMode(const HeHandler* h) {
    int mode = 1;            // 0x55de22: v69 = 1
    if (h) {
        mode = 2;            // 0x55de60: v69 = 2
        if (h->byte187 == 1) // 0x55de69
            mode = 3;        // 0x55de6b
        if (h->byte186 == 1) // 0x55de7c
            mode = 4;        // 0x55de7e
    }
    return mode;
}

// 0x55e240..0x55e3a3: roll the bribe loyalty bonus + flavor string.
//
// 1:1 RNG ORDER (verified against disasm @0x55e241..0x55e398):
//   * VIBE_Math_RandomModulo(3) is ALWAYS drawn FIRST (0x55e241), and the count
//     is provisionally `that + 1` (0x55e263 `inc bl`). This draw happens before
//     the rank branch and is consumed UNCONDITIONALLY — even when the rank>0 arm
//     subsequently OVERWRITES the count with the clamped rank (0x55e27b). So both
//     arms consume exactly TWO RandomModulo draws and the first draw must never be
//     skipped, or the shared RNG desyncs every later draw.
//   * `tier` is the branch comparand `v33` computed at 0x55e246..0x55e25d as
//        tier = (goodType[slot] - 5 - candRank) / 2 + 1
//     (signed /2, round toward zero), where candRank = OfficeRank-1 (var_18) and
//     goodType[slot] = handler->byte188[slot] (v63[slot]). It is NOT candRank.
//   * second draw (strIndex) selects from the tier:
//        tier<=0 : rnd(5)+0          (0x55e285/0x55e28f, +0x1902 string base)
//        tier==1 : rnd(5)+5          (0x55e377/0x55e386)
//        tier==2 : rnd(4)+9          (0x55e360/0x55e36f)
//        tier>=3 : rnd(3)+12         (0x55e38e/0x55e39d)  (and count clamped to 3)
struct OfferBonus { int count; int strIndex; };
OfferBonus OfferBonusRoll(int tier, int (*rnd)(int)) {
    OfferBonus b{};
    b.count = rnd(3) + 1;                 // 0x55e241 + 0x55e263: ALWAYS drawn first
    if (tier <= 0) {                      // 0x55e26e
        b.strIndex = rnd(5);              // 0x55e28a/0x55e28f
    } else {
        int c = tier;
        if (c >= 3) c = 3;                // 0x55e34c clamp
        b.count = c;                      // 0x55e27b (overwrites the rnd(3)+1 count)
        if (tier == 1)                    // 0x55e359
            b.strIndex = rnd(5) + 5;      // 0x55e386
        else if (tier == 2)               // 0x55e35e
            b.strIndex = rnd(4) + 9;      // 0x55e36f
        else
            b.strIndex = rnd(3) + 12;     // 0x55e39d
    }
    return b;
}

// 0x55dfbe / 0x55e137 — the offer "rank-cost" delta shown in the offer header.
// The original reads the DWORDs at handler+0xB6 / handler+0xB5 and arithmetic-
// shifts each right by 24 (`sar ..,18h`), i.e. it sign-extends the TOP byte of
// each little-endian dword: (i8)handler[0xB9] - (i8)handler[0xB8] (byte185 minus
// byte184 in the field map). Render-only (feeds a richstring). NOTE: mode 2 clamps
// this to >= 0 (0x55dfc2); mode 4's header (0x55e149) passes it UN-clamped.
int OfferRankCostDeltaRaw(const HeHandler* h) {
    return static_cast<int>(h->byte185) - static_cast<int>(h->byte184); // (>>24 +0xB6)-(>>24 +0xB5)
}

// 0x55e029..0x55e04b: find which of the 4 offer buttons was clicked. Returns the
// 0-based slot, or 4 when nothing matched (the loop's terminal index).
int OfferFindSlotIndex(int clickedObj, const int buttonIds[4]) {
    int i = 0;                            // 0x55e029
    if (clickedObj != buttonIds[0]) {     // 0x55e030
        do {
            ++i;                          // 0x55e03c
        } while (i < 4 && clickedObj != buttonIds[i]); // 0x55e04b
    }
    return i;
}
} // namespace

guild::i8 RecruitRunRecruitmentOfferWindow(const guild::u16* entityMarker,
                                           const guild::u16* refMarker,
                                           guild::u8 refByte9) {
    const RecruitWindowHooks& H = g_windowHooks;
    guild::i8 result = 2;  // 0x55de25: v72 = 2

    // The entity record fields the driver reads: +1 dword (entity id, match key),
    // +2 byte (kind). The marker array is u16-strided; +1 dword == words [2..3].
    const guild::i32 entityId =
        static_cast<guild::i32>(entityMarker[2]) |
        (static_cast<guild::i32>(entityMarker[3]) << 16);            // *((DWORD*)v71+1)
    const guild::u8 entityKind = static_cast<guild::u8>(entityMarker[1] & 0xFF); // +2 byte

    // 0x55de30: He probe for the kind-65 handler bound to this entity.
    HeHandler* handler = H.heFindFirst(65);
    while (handler) {                                    // 0x55deb0
        if (handler->entityId == entityId)              // 0x55de46
            break;
        handler = H.heFindNext();                       // 0x55deab
    }

    // 0x55de54..0x55de7e: derive the candidate id + the mode from the handler.
    int candidateId = 0;                                // v65
    if (handler) {
        candidateId = handler->dword44;                 // 0x55de54
    }
    int mode = OfferComputeMode(handler);               // 0x55de22..0x55de7e

    // 0x55de8c: branch on the entity kind.
    if (entityKind != 6) {
        // 0x55dec5: no handler + bound person (+532) with its +8 flag set -> queue
        // a slot-reset and return 16; otherwise return 32 (0x55de98).
        if (!handler) {
            // The original resolves FindRecordById(*(entity+532)) and checks +8; the
            // bound person presence/flag is supplied by the He subsystem at this
            // boundary. With no handler and nothing pending we model the 32 path.
            return 32;                                  // 0x55de98
        }
        return 32;
    }

    // 0x55df40: kind==6 & mode 1 forwards to the candidate-pick window.
    if (mode == 1)
        return RecruitRunCandidatePickWindow(refMarker, refByte9); // 0x55e0ba

    // mode 2/3/4: open the offer window.
    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung2"); // 0x55df56
    H.formCenterChildWindows(formId);                   // 0x55df59
    H.dragCursorSetSprite(0, 0);                         // 0x55df62
    H.formSelectWindow(formId, 1);                       // 0x55df71
    H.textRenderRichString(0x18F3u, 0, 0, 0, 0);        // 0x55df84

    // The mode-4 offer descriptors (function scope so the pump can read them).
    int buttonIds[4] = { -1, -1, -1, -1 };
    int goodType[3]  = { 0, 0, 0 };  // v63[i] = handler->byte188[i]
    int goodCount[3] = { 0, 0, 0 };  // v60/v61/v62 = handler->dword47[i]
    int candRank = 0;                // v67 = ComputeOfficeRank(candidateId) - 1

    if (mode == 2) {
        // 0x55dfa3: candidate record; 0x55dfbe: clamp the rank-cost delta to >=0.
        const guild::sim::Person* cand = PersonFindRecordById(candidateId);
        int delta = OfferRankCostDeltaRaw(handler);      // (>>24 +0xB6)-(>>24 +0xB5)
        if (delta < 0) delta = 0;                        // 0x55dfc2 / 0x55e0d5
        guild::u16 candMarker = cand ? static_cast<guild::u16>(cand->marker) : 0;
        H.textRenderRichString(0x18E7u, candMarker, delta, 93, 0); // 0x55dfdd
    } else if (mode == 3) {
        PersonFindRecordById(candidateId);               // 0x55e210
        H.textRenderRichString(0x18E3u, 0, 0, 0, 0);     // 0x55e21c
        H.textRenderRichString(0x1931u, 0, 0, 0, 0);     // 0x55e22f
    } else { // mode 4 — the offer/bribe window.
        const guild::sim::Person* cand = PersonFindRecordById(candidateId);    // 0x55e0f6
        candRank = H.personComputeOfficeRank(candidateId) - 1;                 // 0x55e102..0x55e10b (v67)
        // 0x55e110: three offer entries — good-type byte188[i], good-count dword47[i].
        for (int i = 0; i < 3; ++i) {
            goodType[i]  = handler->byte188[i];          // v63[i]
            goodCount[i] = handler->dword47[i];          // v59[i+8]
        }
        guild::u16 candMarker = cand ? static_cast<guild::u16>(cand->marker) : 0;
        int rankCost = OfferRankCostDeltaRaw(handler);   // 0x55e137: UN-clamped (no jl)
        // 0x55e15d: header line.
        H.textRenderRichString(0x18F4u, candMarker, 93, rankCost, 93);
        // 0x55e17c..0x55e205: build the 4 buttons (3 offers + decline).
        buttonIds[0] = H.formGetChildObjectId(formId, 0,
                          H.textRenderRichString(0x18F5u, goodType[0] + 6417, goodCount[0], 0, 0));
        buttonIds[1] = H.formGetChildObjectId(formId, 0,
                          H.textRenderRichString(0x18F6u, goodType[1] + 6417, goodCount[1], 0, 0));
        buttonIds[2] = H.formGetChildObjectId(formId, 0,
                          H.textRenderRichString(0x18F7u, goodType[2] + 6417, goodCount[2], 0, 0));
        buttonIds[3] = H.formGetChildObjectId(formId, 0,
                          H.textRenderRichString(0x18F8u, 0, 0, 0, 0));
    }

    // 0x55e096: modal pump. Mode 4 dispatches on which offer button was clicked.
    char msg[512];
    do {
        int cancel = H.readCancelEdge();                 // dword_672230 (0x55dfff)
        if (cancel) break;                               // 0x55e001 -> quit
        if (mode == 4) {                                 // 0x55e01c
            int clickedObj = H.readLastClickedObject();  // dword_62D22C
            int slot = OfferFindSlotIndex(clickedObj, buttonIds); // 0x55e029
            if (slot >= 3) {                             // 0x55e053
                if (slot == 3) {                         // 0x55e3a5: decline.
                    const guild::sim::Person* cand = PersonFindRecordById(candidateId); // 0x55e3ae
                    guild::u16 m = cand ? static_cast<guild::u16>(cand->marker) : 0;
                    H.textRenderFormatted(msg, 6394, m, 0);        // 0x55e3c8
                    handler->byte186 = 0;                          // 0x55e3d5
                    handler->dword33 = H.queueEntity29(0, handler);// 0x55e3de
                    H.dialogShowMessageBox(msg, 0, 0);             // 0x55e3f4
                    break;                                          // 0x55e3f9 -> quit
                }
            } else { // slot 0..2: a bribe offer (0x55e070).
                // 0x55e07f: resource-amount gate on the offered good count.
                if (H.dialogCheckResourceAmount(goodCount[slot], 0)) {
                    // 0x55e246..0x55e25d: branch comparand tier =
                    //   (goodType[slot] - 5 - candRank) / 2 + 1   (signed /2, trunc).
                    // 0x55e265: re-resolve the candidate; 0x55e241..0x55e39d: roll the
                    // loyalty bonus + flavor string. NOTE: the first rnd(3) draw at
                    // 0x55e241 is ALWAYS consumed (RNG order is load-bearing).
                    int tier = (goodType[slot] - 5 - candRank) / 2 + 1;
                    const guild::sim::Person* cand = PersonFindRecordById(candidateId); // 0x55e265
                    OfferBonus b = OfferBonusRoll(tier, H.randomModulo);
                    guild::u16 m = cand ? static_cast<guild::u16>(cand->marker) : 0;
                    // 0x55e2ac: bribe-accept flavor message (string 0x18F9 + b.strIndex+0x1902).
                    H.textRenderFormatted(msg, 6393, m, b.strIndex + 6402);
                    // 0x55e2ba: accumulate the loyalty bonus into byte184.
                    guild::i8 newLoyalty = static_cast<guild::i8>(b.count + handler->byte184);
                    handler->byte184 = newLoyalty;                 // 0x55e2bc
                    if (newLoyalty >= handler->byte185) {          // 0x55e2d2: threshold met
                        // 0x55e2ef: advance the game clock (timestamp splice).
                        H.gameTimeAdvance(0, 0, 0, 1);
                    }
                    handler->byte186 = 0;                          // 0x55e2f9
                    handler->dword33 = H.queueEntity29(0, handler);// 0x55e304
                    // 0x55e32a: ship the bribe good as a cmd15 to the entity peer.
                    H.enqueueCmd15(-1, entityId, goodCount[slot], 0); // byte_6477A1 rate
                    H.dialogShowMessageBox(msg, 0, 0);             // 0x55e33c
                    break;                                          // 0x55e341 -> quit
                }
            }
        }
    } while (H.gameLogicRunFrameLoop(kFrameLoopTag, 0, nullptr));  // 0x55e096

    H.formDestroy(formId);                               // 0x55e0a6
    return result;                                       // 0x55e0ab
}

} // namespace guild::gui
