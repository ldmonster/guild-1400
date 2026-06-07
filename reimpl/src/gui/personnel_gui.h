#pragma once
// guild::gui — personnel_gui: the staff/mercenary "book" and recruitment-offer
// dialog bodies from gilde.exe, translated 1:1. These are the VIBE_Personnel_* /
// VIBE_Recruit_* GUI dialogs that drive the personnel book, the per-worker pay
// dialog, the per-row populate/label state machine, and the recruitment flow
// (candidate pick grid, hire confirm, and the multi-mode offer window with its
// bribe-bonus RNG bracket).
//
// Functions recovered here (addr — original VIBE_ symbol):
//
//   0x53b478  VIBE_Personnel_RunPayWorkerDialog      pay-worker dialog: clamp held
//                                                    currency to 4800, display /32,
//                                                    frame-loop input dispatch.
//   0x53b744  VIBE_Personnel_PopulateBookRow         build one staff-book row's
//                                                    widgets: label/slider/relation
//                                                    bar + the row-state byte.
//   0x53babc  VIBE_Personnel_UpdateRowLabel          3-way row-label state machine
//                                                    (handler text / item name / empty).
//   0x55d990  VIBE_Recruit_RunHireConfirmDialog      hire-confirm: cost, frame-loop,
//                                                    1210->queue hire / 1155->cancel.
//   0x55db0c  VIBE_Recruit_RunCandidatePickWindow    3-column candidate-card grid +
//                                                    click->RunHireConfirmDialog.
//   0x55de00  VIBE_Recruit_RunRecruitmentOfferWindow handler->mode select, bribe-bonus
//                                                    RNG bracket, 4-button slot scan.
//
// The deterministic decision/layout KERNELS are exposed as pure free functions
// (PayWorkerClampDisplay, RowLabelDecide, CandidateGridCellPos,
// RecruitOfferComputeMode, RecruitOfferBonusRoll, RecruitOfferFindSlotIndex,
// StaffBookBuildingRowLayout) so they can be golden-tested without the GUI.
//
// All renderer / form / widget / command-queue / game-loop leaves are routed
// through an installable PersonnelGuiHooks struct with inert defaults defined in
// personnel_gui.cpp (the house pattern — GuiDialogs7Hooks / CutsceneMiscHooks).
// The reconstructed sibling util::RandomModulo (math_random.cpp 0x58b89c) is the
// bribe-bonus RNG source, forwarded through the randomModulo hook exactly as the
// live wiring (the integration test forwards it into the REAL util::RandomModulo).

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Frame-loop control latches reused from the engine (BSS, zero at load).
// These are the same globals the rest of the GUI uses; owned by gui_dialogs5.cpp
// (g_forceQuitLatch == dword_631614) and gui_dialogs7.cpp (dword_672230 edge,
// dword_75BF38 last-clicked id). personnel_gui reuses them via extern below.
// ===========================================================================

// ---------------------------------------------------------------------------
// Pure deterministic kernels (golden-testable; no GUI side effects).
// ---------------------------------------------------------------------------

// VIBE_Personnel_RunPayWorkerDialog inner math (0x53b515..0x53b535):
// the dialog clamps the player's held currency to 4800 then displays it divided
// by 32 (arithmetic shift right by 5, matching the signed >>5 in the original).
inline i32 PayWorkerClampDisplay(i32 heldCurrency) {
    i32 v = heldCurrency;
    if (v >= 4800)
        v = 4800;
    return v >> 5;
}

// VIBE_Personnel_UpdateRowLabel state classification (0x53bad6..).
// Decides which of three label states a staff-book row is in:
//   2 -> a matched handler with a non-zero "current item" slot (handler text)
//   1 -> an occupied person slot with a printable item   (item-name text)
//   0 -> empty                                            (placeholder text)
enum class RowLabelState : i32 { Empty = 0, ItemName = 1, HandlerText = 2 };

// handlerMatched: a handler was found by VIBE_He_FindFirstHandlerByFilter.
// handlerHasItem: that handler's +176 dword (the item-in-hand) is non-zero.
// personPtrNonNull: the row's bound person record pointer (a2+8) is non-null.
// personHasItemFlag: that person's +8 byte (printable item flag) is non-zero.
RowLabelState RowLabelDecide(bool handlerMatched, bool handlerHasItem,
                             bool personPtrNonNull, bool personHasItemFlag);

// VIBE_Recruit_RunCandidatePickWindow card-grid placement (0x55dc28..0x55dc46).
// Lays out candidate cards in a 3-column grid inside a panel of width panelW,
// where cardW is the per-card width (read from dword_62D204+117762 >> 16). i is
// the zero-based card index. Returns the card's top-left (x, y).
struct GridCell { i32 x; i32 y; };
GridCell CandidateGridCellPos(int i, int panelW, int cardW);

// VIBE_Recruit_RunRecruitmentOfferWindow mode select (0x55de4d..0x55de7e).
// Given a found handler (or none) and the handler's two state bytes, returns the
// offer-window mode:
//   1 -> no handler (route to the candidate-pick window)
//   2 -> handler found, default
//   3 -> handler[187] (the "rejected/away" byte) == 1
//   4 -> handler[186] (the "offer-pending" byte) == 1   (the interactive branch)
int RecruitOfferComputeMode(bool handlerFound, int handlerByte187, int handlerByte186);

// VIBE_Recruit_RunRecruitmentOfferWindow bribe-bonus bracket (0x55e263..0x55e39d).
// rank is the candidate's office-rank delta (v33). Picks a base bonus 'count' (the
// loyalty add, v32) and a flavor-string index (v35) using the supplied randomModulo
// (0..n-1). Mirrors the original branch ladder exactly:
//   rank<=0 : count=rand(3)+1,            idx=rand(5)
//   rank==1 : count=1,                    idx=rand(5)+5
//   rank==2 : count=2,                    idx=rand(4)+9
//   rank>=3 : count=3,                    idx=rand(3)+12
struct OfferBonus { int count; int strIndex; };
OfferBonus RecruitOfferBonusRoll(int rank, int (*randomModulo)(int n));

// VIBE_Recruit_RunRecruitmentOfferWindow clicked-button -> slot index (0x55e029..).
// Scans the 4 button object-ids for the clicked id; returns 0..3 (the slot) or the
// loop's terminal index (4) when no button matched.
int RecruitOfferFindSlotIndex(int clickedObjId, const int buttonIds[4]);

// VIBE_Personnel_RunStaffBook storage-building row layout kernel (0x53c225..0x53c24d).
// For row index i the book lays out an icon/label/value triple at fixed strides.
// Returns the three recovered Y/x16 sub-coordinates (matching LOWORD writes).
struct StaffRowLayout { i16 yA; i16 yB; i16 yC; };
StaffRowLayout StaffBookBuildingRowLayout(int i);

// ---------------------------------------------------------------------------
// Hooks (cross-module / renderer / command-queue / frame-loop leaves).
// Inert defaults make every dialog observable headless and terminate immediately.
// ---------------------------------------------------------------------------
struct PersonnelGuiHooks {
    // --- form / window plumbing ---
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName);  // VIBE_GameTick_Finalize 0x41beb8
    void (*formCenterChildWindows)(int formId);                    // VIBE_Form_CenterChildWindows 0x41d6ac
    void (*formSelectWindow)(int formId, int which);               // VIBE_Form_SelectWindow 0x41e4cc
    void (*formDestroy)(int formId);                               // VIBE_Form_Destroy 0x41da04
    void (*formSetChildrenVisible)(int formId, int visible);       // VIBE_Form_SetChildrenVisible 0x41d568
    int  (*formGetChildObjectId)(int formId, int a, int b);        // VIBE_Form_GetChildObjectId 0x41dea8

    // --- text / object widgets ---
    int  (*textRenderRichString)(unsigned msgId, unsigned a, int b); // VIBE_Text_RenderRichString 0x59d6e8
    void (*objectSetValueOrText)(int objId, int minV, int maxV, int val); // 0x41dfec
    void (*dialogCheckResourceAmount)(int amount, int kind);       // VIBE_Dialog_CheckResourceAmount 0x4ad62c
    void (*dialogShowMessageBox)(const char* text, int a, int b);  // VIBE_Dialog_ShowMessageBox 0x4ad6f0

    // --- frame loop / input ---
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* p);    // VIBE_GameLogic_RunFrameLoop 0x4c09a0 (0 -> exit)
    int  (*readLastClickedObject)();                               // dword_75BF38 (the input result code)
    int  (*readCancelEdge)();                                      // dword_672230 (right-click / cancel edge)

    // --- command queue ---
    int  (*queueHireRequest)(int candidateId);                     // VIBE_Command_QueueRequestSlotReset28 0x4948c8
    int  (*commandGetPacketStatus)(int packetId);                  // VIBE_Command_GetPacketStatusById 0x4939d4 (1 -> done)
    void (*refreshGuildState)();                                   // VIBE_Amt_RefreshGuildState 0x4becdc

    // --- sim leaves ---
    int  (*computeRecruitmentCost)(int candidateId);               // VIBE_Recruit_ComputeRecruitmentCost 0x55d674
    int  (*sumCurrencyHeld)(int personId);                         // VIBE_Person_SumCurrencyHeld 0x59152c
    int  (*randomModulo)(int n);                                   // VIBE_Math_RandomModulo 0x58b89c (real sibling)
};

extern PersonnelGuiHooks g_personnelGuiHooks;
void SetPersonnelGuiHooks(const PersonnelGuiHooks& h);  // install (e.g. from tests)
void ResetPersonnelGuiHooks();                          // restore inert defaults

// ---------------------------------------------------------------------------
// Dialog bodies (frame-loop plumbing routed through the hooks above).
// ---------------------------------------------------------------------------

// VIBE_Personnel_RunPayWorkerDialog 0x53b478.
// Returns the data-ptr result the original returns (0 unless a pay action was
// confirmed). resourceOk gates the whole dialog (VIBE_Dialog_CheckResourceAmount).
int RunPayWorkerDialog(bool resourceOk, int playerPersonId);

// VIBE_Recruit_RunHireConfirmDialog 0x55d990.
// Returns 1 if the player confirmed the hire (input 1210), else 0.
int RunHireConfirmDialog(int candidateId);

// VIBE_Recruit_RunCandidatePickWindow 0x55db0c.
// Returns the original's al result: 2 (no selection / closed) or -110 (0x92, a hire
// was confirmed for a candidate). candidates/count come from the recruit pool.
i8 RunCandidatePickWindow(const int* candidateIds, int count);

// VIBE_Recruit_RunRecruitmentOfferWindow 0x55de00.
// Drives the offer window for the recruit handle `entityId`. handlerFound and the
// two handler state bytes select the mode (see RecruitOfferComputeMode). For mode 1
// it forwards to RunCandidatePickWindow. Returns the original's al result code
// (2 normal exit, 32 / 16 the no-handler early-returns).
i8 RunRecruitmentOfferWindow(int entityId, bool handlerFound,
                             int handlerByte187, int handlerByte186,
                             const int* candidateIds, int candidateCount);

} // namespace guild::gui
