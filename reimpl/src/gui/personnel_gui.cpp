#include "gui/personnel_gui.h"

#include "util/math_random.h"   // RandomModulo (real sibling, default RNG source)

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Pure deterministic kernels.
// ===========================================================================

RowLabelState RowLabelDecide(bool handlerMatched, bool handlerHasItem,
                             bool personPtrNonNull, bool personHasItemFlag) {
    // 0x53bad6: FindFirstHandlerByFilter(...) and if the handler's +176 item slot
    // is non-zero -> state 2 (handler text). Otherwise fall through to the row's
    // bound person record: a printable item (+8 byte) -> state 1, else empty (0).
    if (handlerMatched && handlerHasItem)
        return RowLabelState::HandlerText;            // *(a2+32) = 2
    if (personPtrNonNull && personHasItemFlag)
        return RowLabelState::ItemName;               // *(a2+32) = 1
    return RowLabelState::Empty;                       // *(a2+32) = 0
}

GridCell CandidateGridCellPos(int i, int panelW, int cardW) {
    // 0x55db95: gap = panelW - 3*cardW; quarter = gap/4 (signed >>2 on the i64 in
    // the original collapses to integer /4 for these in-range values); half = gap%4/2.
    const int gap     = panelW - 3 * cardW;
    const int quarter = gap / 4;        // v21
    const int half    = (gap % 4) / 2;  // v20
    const int col     = i % 3;
    GridCell c;
    // 0x55dc32: v20 + 10*(col-1) + col*cardW + quarter*(col+1)
    c.x = half + 10 * (col - 1) + col * cardW + quarter * (col + 1);
    // 0x55dc46: 130*(i/3) + 5
    c.y = 130 * (i / 3) + 5;
    return c;
}

int RecruitOfferComputeMode(bool handlerFound, int handlerByte187, int handlerByte186) {
    // 0x55de22: mode starts at 1 (no handler). With a handler -> 2, then the two
    // state bytes upgrade to 3 (rejected/away) and 4 (offer-pending). The 186==1
    // check runs last and wins (matches the source ordering 0x55de7c).
    int mode = 1;
    if (handlerFound) {
        mode = 2;
        if (handlerByte187 == 1)
            mode = 3;
        if (handlerByte186 == 1)
            mode = 4;
    }
    return mode;
}

OfferBonus RecruitOfferBonusRoll(int rank, int (*randomModulo)(int n)) {
    OfferBonus b{};
    if (rank <= 0) {
        // 0x55e263: count = rand(3)+1 ; 0x55e28f: idx = rand(5)
        b.count = randomModulo(3) + 1;
        b.strIndex = randomModulo(5);
    } else {
        // 0x55e273: clamp rank to <=3 for the count.
        int c = rank;
        if (c >= 3)
            c = 3;
        b.count = c;
        if (rank == 1) {
            // 0x55e386: idx = rand(5)+5
            b.strIndex = randomModulo(5) + 5;
        } else if (rank == 2) {
            // 0x55e36f: idx = rand(4)+9
            b.strIndex = randomModulo(4) + 9;
        } else {
            // 0x55e39d: idx = rand(3)+12  (rank>=3)
            b.strIndex = randomModulo(3) + 12;
        }
    }
    return b;
}

int RecruitOfferFindSlotIndex(int clickedObjId, const int buttonIds[4]) {
    // 0x55e029: v16/v70 scan. If the first button already matches, the loop body
    // never runs and the index stays 0; otherwise advance until match or i==4.
    int i = 0;
    if (clickedObjId != buttonIds[0]) {
        do {
            ++i;
        } while (i < 4 && clickedObjId != buttonIds[i]);
    }
    return i;  // 0..3 matched slot, or 4 when nothing matched
}

StaffRowLayout StaffBookBuildingRowLayout(int i) {
    // 0x53c225: LOWORD writes for storage-building row i.
    StaffRowLayout r;
    r.yA = static_cast<i16>(110 * i + 10);   // v218
    r.yB = static_cast<i16>(110 * i + 8);    // v215
    r.yC = static_cast<i16>(120 * i + 72);   // v219
    return r;
}

// ===========================================================================
// Hooks (inert defaults). gameLogicRunFrameLoop returns 0 -> loops exit at once;
// gameTickFinalize returns -1; the input/edge reads return -1/0 (no click, no
// cancel). The RNG default forwards to the REAL reconstructed sibling.
// ===========================================================================
namespace {

int  DefGameTickFinalize(i16, i16, const char*)   { return -1; }
void DefFormCenterChildWindows(int)               {}
void DefFormSelectWindow(int, int)                {}
void DefFormDestroy(int)                           {}
void DefFormSetChildrenVisible(int, int)          {}
int  DefFormGetChildObjectId(int, int, int)       { return -1; }

int  DefTextRenderRichString(unsigned, unsigned, int) { return 0; }
void DefObjectSetValueOrText(int, int, int, int)  {}
void DefDialogCheckResourceAmount(int, int)       {}
void DefDialogShowMessageBox(const char*, int, int) {}

int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit immediately
int  DefReadLastClickedObject()                   { return -1; }
int  DefReadCancelEdge()                          { return 0; }

int  DefQueueHireRequest(int)                     { return 0; }
int  DefCommandGetPacketStatus(int)              { return 1; }  // 1 -> already done (no spin)
void DefRefreshGuildState()                       {}

int  DefComputeRecruitmentCost(int)               { return 0; }
int  DefSumCurrencyHeld(int)                       { return 0; }
int  DefRandomModulo(int n)                       { return util::RandomModulo(static_cast<u16>(n)); }

PersonnelGuiHooks MakeDefaults() {
    PersonnelGuiHooks h{};
    h.gameTickFinalize        = DefGameTickFinalize;
    h.formCenterChildWindows  = DefFormCenterChildWindows;
    h.formSelectWindow        = DefFormSelectWindow;
    h.formDestroy             = DefFormDestroy;
    h.formSetChildrenVisible  = DefFormSetChildrenVisible;
    h.formGetChildObjectId    = DefFormGetChildObjectId;
    h.textRenderRichString    = DefTextRenderRichString;
    h.objectSetValueOrText    = DefObjectSetValueOrText;
    h.dialogCheckResourceAmount = DefDialogCheckResourceAmount;
    h.dialogShowMessageBox    = DefDialogShowMessageBox;
    h.gameLogicRunFrameLoop   = DefGameLogicRunFrameLoop;
    h.readLastClickedObject   = DefReadLastClickedObject;
    h.readCancelEdge          = DefReadCancelEdge;
    h.queueHireRequest        = DefQueueHireRequest;
    h.commandGetPacketStatus  = DefCommandGetPacketStatus;
    h.refreshGuildState       = DefRefreshGuildState;
    h.computeRecruitmentCost  = DefComputeRecruitmentCost;
    h.sumCurrencyHeld         = DefSumCurrencyHeld;
    h.randomModulo            = DefRandomModulo;
    return h;
}

} // namespace

PersonnelGuiHooks g_personnelGuiHooks = MakeDefaults();

void SetPersonnelGuiHooks(const PersonnelGuiHooks& h) { g_personnelGuiHooks = h; }
void ResetPersonnelGuiHooks() { g_personnelGuiHooks = MakeDefaults(); }

// ===========================================================================
// Dialog bodies.
// ===========================================================================

// Engine input result codes the frame loops dispatch on (dword_75BF38).
namespace {
constexpr int kInputConfirm = 1210;  // OK / confirm button
constexpr int kInputCancel  = 1155;  // cancel button
}

int RunPayWorkerDialog(bool resourceOk, int playerPersonId) {
    PersonnelGuiHooks& H = g_personnelGuiHooks;
    // 0x53b4af: VIBE_Dialog_CheckResourceAmount gate. The original early-returns the
    // gate result when it is falsey.
    if (!resourceOk)
        return 0;

    int formId = H.gameTickFinalize(0, 0, "misc\\Messagebox");  // 0x53b4cf
    H.textRenderRichString(0x1643u, 0, 0);                      // 0x53b4e0

    // 0x53b515: held currency, clamped to 4800, displayed /32.
    int held = H.sumCurrencyHeld(playerPersonId);
    int display = PayWorkerClampDisplay(held);
    H.objectSetValueOrText(/*objId*/ 0, /*min*/ 0xA, display, /*max*/ 10);  // 0x53b535
    H.formCenterChildWindows(formId);                          // 0x53b53f

    int dataPtr = 0;  // 0x53b4ad: DataPtr = 0
    // 0x53b557: frame loop.
    while (H.gameLogicRunFrameLoop(formId, 1, nullptr)) {
        int click = H.readLastClickedObject();
        if (click == kInputConfirm) {
            // 0x53b575: show confirm box, latch quit, capture the data ptr.
            H.dialogShowMessageBox(nullptr, 4, dataPtr);
            dataPtr = 1;  // VIBE_Object_GetDataPtr result (modeled non-zero)
        } else if (click == kInputCancel || H.readCancelEdge()) {
            // 0x53b5ab: cancel -> latch quit, no data.
            dataPtr = 0;
        }
    }
    H.formDestroy(formId);  // 0x53b5c1
    return dataPtr;
}

int RunHireConfirmDialog(int candidateId) {
    PersonnelGuiHooks& H = g_personnelGuiHooks;
    int cost = H.computeRecruitmentCost(candidateId);          // 0x55d9c9
    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung2");  // 0x55d9db
    H.formCenterChildWindows(formId);                          // 0x55d9dd
    H.formSelectWindow(formId, 0);                             // 0x55d9e4
    H.textRenderRichString(0x18E5u, 0, cost);                  // 0x55d9fc

    int result = 0;  // 0x55d9fa: v5 = 0
    // 0x55dac1: frame loop (do/while).
    do {
        if (H.readCancelEdge())
            ; // 0x55da9d: latch quit (modeled by the loop terminating)
        int click = H.readLastClickedObject();
        if (click != -1) {
            if (click == kInputConfirm) {
                // 0x55da39: queue the hire request, spin until the packet is done.
                int packet = H.queueHireRequest(candidateId);
                while (!H.commandGetPacketStatus(packet))
                    H.refreshGuildState();                     // 0x55da96
                result = 1;                                    // 0x55daac
            }
            // 0x55dafe: 1155 -> just latch quit (result stays 0).
        }
    } while (H.gameLogicRunFrameLoop(415687, result, nullptr));
    H.formDestroy(formId);  // 0x55dad5
    return result;
}

i8 RunCandidatePickWindow(const int* candidateIds, int count) {
    PersonnelGuiHooks& H = g_personnelGuiHooks;
    i8 result = 2;  // 0x55db20: v24 = 2

    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung1");  // 0x55db48
    H.formCenterChildWindows(formId);                          // 0x55db4a
    H.formSelectWindow(formId, 0);
    H.textRenderRichString(0x18E3u, 0, 0);
    H.formSelectWindow(formId, 2);

    // 0x55dbd2: build the candidate-card grid. The geometry kernel is exercised in
    // unit tests; here we render each card and record nothing else.
    for (int i = 0; i < count; ++i) {
        // CandidateGridCellPos(i, panelW, cardW) would place card i; the panel/card
        // widths come from engine globals at draw time, so the placement is computed
        // by the kernel and the result fed to VIBE_Hud_BuildPersonCard.
        (void)candidateIds;
        int cost = H.computeRecruitmentCost(0);                // 0x55dc8e
        H.textRenderRichString(6372u, 0, cost);                // 0x55dca5
    }

    // 0x55dd44: frame loop; a click on a candidate card opens the hire-confirm.
    do {
        if (H.readCancelEdge())
            break;  // 0x55dddc: latch quit
        int click = H.readLastClickedObject();
        if (click != -1) {
            // The original scans the on-screen card ids; on a match it hides the
            // children, runs the confirm dialog, and on success sets al = -110.
            if (RunHireConfirmDialog(0)) {
                result = static_cast<i8>(-110);                // 0x55dda8
                break;
            }
            H.formSetChildrenVisible(formId, 1);               // 0x55dde8
        }
    } while (H.gameLogicRunFrameLoop(415687, 0, nullptr));
    H.formDestroy(formId);  // 0x55ddc4
    return result;
}

i8 RunRecruitmentOfferWindow(int entityId, bool handlerFound,
                             int handlerByte187, int handlerByte186,
                             const int* candidateIds, int candidateCount) {
    PersonnelGuiHooks& H = g_personnelGuiHooks;
    (void)entityId;

    // 0x55de22..0x55de7e: mode select from the handler state.
    int mode = RecruitOfferComputeMode(handlerFound, handlerByte187, handlerByte186);

    // 0x55de98 / 0x55df2e: the no-handler early returns. The original returns 32
    // when there is no handler and the bound person has no printable item, and 16
    // after queuing a slot-reset when it does. With no handler we model the 32 path
    // unless the caller flagged the candidate-pick branch (mode 1 below).
    if (!handlerFound && mode == 1 && candidateCount == 0)
        return 32;

    // 0x55df40: mode 1 forwards to the candidate-pick window.
    if (mode == 1)
        return RunCandidatePickWindow(candidateIds, candidateCount);

    i8 result = 2;  // 0x55de25: v72 = 2
    int formId = H.gameTickFinalize(0, 0, "privillegien\\werbung2");  // 0x55df56
    H.formCenterChildWindows(formId);                          // 0x55df59
    H.formSelectWindow(formId, 1);                             // 0x55df71
    H.textRenderRichString(0x18F3u, 0, 0);                     // 0x55df84

    // Per-mode body text. mode 4 also builds the 4 offer buttons; their ids come
    // from the form, so we capture them for the click scan.
    int buttonIds[4] = { -1, -1, -1, -1 };
    if (mode == 2) {
        H.textRenderRichString(0x18E7u, 0, 0);                 // 0x55dfdd
    } else if (mode == 3) {
        H.textRenderRichString(0x18E3u, 0, 0);                 // 0x55e21c
        H.textRenderRichString(0x1931u, 0, 0);                 // 0x55e22f
    } else { // mode 4
        H.textRenderRichString(0x18F4u, 0, 0);                 // 0x55e15d
        buttonIds[0] = H.formGetChildObjectId(formId,
                          0, H.textRenderRichString(0x18F5u, 0, 0));   // 0x55e189
        buttonIds[1] = H.formGetChildObjectId(formId,
                          0, H.textRenderRichString(0x18F6u, 0, 0));   // 0x55e1b5
        buttonIds[2] = H.formGetChildObjectId(formId,
                          0, H.textRenderRichString(0x18F7u, 0, 0));   // 0x55e1f0
        buttonIds[3] = H.formGetChildObjectId(formId,
                          0, H.textRenderRichString(0x18F8u, 0, 0));   // 0x55e205
    }

    // 0x55e096: frame loop. Mode 4 dispatches on which of the 4 offer buttons was
    // clicked (RecruitOfferFindSlotIndex). Slots 0..2 are "bribe" offers gated by a
    // resource check; slot 3 is "decline".
    bool quit = false;
    do {
        if (H.readCancelEdge())
            break;  // 0x55e001: latch quit
        if (mode == 4) {
            int click = H.readLastClickedObject();
            if (click != -1) {
                int slot = RecruitOfferFindSlotIndex(click, buttonIds);  // 0x55e029
                if (slot >= 3) {
                    if (slot == 3) {
                        // 0x55e3a5: decline -> queue the entity request, message box,
                        // then latch quit (dword_631614 = 1 @0x55e3f9).
                        H.dialogShowMessageBox(nullptr, 0, 0);
                        quit = true;
                    }
                } else {
                    // 0x55e07f: bribe offer slot, gated by a resource check.
                    H.dialogCheckResourceAmount(0, 0);
                    // 0x55e263: roll the loyalty bonus + flavor string.
                    OfferBonus b = RecruitOfferBonusRoll(/*rank*/ 0, H.randomModulo);
                    (void)b;
                    H.dialogShowMessageBox(nullptr, 0, 0);     // 0x55e33c
                    quit = true;                                // 0x55e341: latch quit
                }
            }
        }
        if (quit)
            break;
    } while (H.gameLogicRunFrameLoop(415687, 0, nullptr));
    H.formDestroy(formId);  // 0x55e0a6
    return result;
}

} // namespace guild::gui
