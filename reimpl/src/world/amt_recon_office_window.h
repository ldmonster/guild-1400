#pragma once
// amt_recon_office_window.h — pure decision/layout logic of the political-office
// (Amt) window cluster of gilde.exe, reconstructed 1:1 from the Hex-Rays decompile.
//
// The functions in this cluster are, in the original, GUI frame-loop dialogs that
// build a Form (VIBE_GameTick_Finalize "special\amt1"/"special\amt3"), spin
// VIBE_GameLogic_RunFrameLoop, and on click open a sub-session. Those subsystems
// (Form/HUD/Vulkan, the command queue, the live word_12CE910 person array, the
// VIBE_He_* handler iterator) are coupled leaves; per the project rules they are
// surfaced here as inert-default hooks. What is faithfully reconstructed here are
// the *pure rules* buried inside those dialogs:
//
//   * the candidate-role dispatch table (funcs_557292 @0x63d584) — 5 fixed records
//     each = { collector-fn, renderer-fn, list-index }, count = dword_5526AC = 8;
//   * the office-overview slot table (dword_5526B0 @0x5526b0) — the 8 office-type
//     ids shown per overview slot;
//   * VIBE_Office_PrepareCandidatePage (0x555eb4) control flow;
//   * VIBE_Office_ApplyCandidateRatingBars (0x556ba0) bar pairing rule;
//   * VIBE_Amt_RunCandidateSelectionWindow (0x556c40) state reset / index clamp /
//     find-first-nonempty-list / per-row vertical layout distribution;
//   * VIBE_Amt_RunOfficeOverviewWindow (0x5575c8) per-slot occupied gating and
//     the active-slot match-find;
//   * VIBE_Amt_HasOccupiedOffice (0x480cb4) occupied scan;
//   * VIBE_Amt_OpenOfficeWindow (0x5546a0) descriptor build.
//
// Reused (NOT redefined here):
//   * VIBE_Person_EvaluateCandidateEligibility (0x5596f8) — already reconstructed as
//     guild::sim::PersonEvaluateCandidateEligibility (src/sim/recruit_cost.cpp).
//   * VIBE_Person_ComputeOfficeRank (0x58bccc) — guild::sim::PersonComputeOfficeRank.
//   * Candidate-row Y math (PrivCandidateRowY/kCandidateRowStride) already lives in
//     world/office_recon_privilege.h — the *card-list* renderer uses that 112-stride;
//     the *button-grid* layout below (RunCandidateSelectionWindow) is a DIFFERENT,
//     evenly-distributed layout and is reconstructed here.
//
// Deferred (UI/render/network leaves), surfaced as hooks below with reasons:
//   Form build/select/destroy, HUD label/card build, rich-string render, the
//   RunFrameLoop modal spin, the network command queue, the live person array, and
//   the VIBE_He_* handler iterator used by the role collectors.

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Candidate-role dispatch table  (funcs_557292 @0x63d584 / funcs_5572C9 @0x63d588)
// ===========================================================================
//
// gilde.exe 0x63d584 — five fixed 5-DWORD records. The base symbol funcs_557292
// points at field +0 (the collector fn); funcs_5572C9 points at field +1 (the
// renderer fn) of the SAME records (it is the table address + 4). Layout of each
// 20-byte record (verified via get_bytes @0x63d584):
//     +0  collector  (VIBE_..Collect..  builds the candidate list, returns count)
//     +4  renderer   (VIBE_Office_ShowCandidate..  lays out the page)
//     +8  listIndex  (1..5 — the form sub-window the page renders into)
//     +12 0
//     +16 -1
//
// Recovered bytes -> records:
//   rec0: 0x554e34 VIBE_Person_CollectRelatedNpcs,      0x555f8c ShowCandidateListWithRoles,      1
//   rec1: 0x515150 (collector),                          0x556108 ShowCandidateCardListVariantA,   2
//   rec2: 0x553778 (collector),                          0x55623c ShowCandidateCardListVariantB,   3
//   rec3: 0x5555c8 (collector),                          0x556370 ShowCandidateCardListVariantC,   4
//   rec4: 0x5556c4 (collector),                          0x5564ac ShowCandidateListWithSkillLabels,5
struct AmtRoleEntry {
    u32 collectorAddr;  // +0  gilde.exe address of the list-collector fn
    u32 rendererAddr;   // +4  gilde.exe address of the page-renderer fn
    i32 listIndex;      // +8  form sub-window index 1..5
};

// gilde.exe dword_5526AC @0x5526ac == 8 — the candidate-role count the windows
// iterate (NOTE: the funcs table only defines 5 records; the window loop uses
// dword_5526AC as the live count, so callers that drive 8 must supply the extra
// entries from runtime state — here we expose both the count and the 5 fixed rows).
constexpr int kAmtCandidateRoleCount = 8; // dword_5526AC

// The five statically-known role records (funcs_557292).
constexpr int kAmtRoleTableRows = 5;
extern const AmtRoleEntry kAmtRoleTable[kAmtRoleTableRows];

// gilde.exe dword_5526B0 @0x5526b0 — the office-type id shown in each of the 8
// overview slots (read as bytes, used as HIBYTE arg to VIBE_Panel_RunOfficeSession
// and to index the label table dword_8C7288). Recovered bytes:
//   00 06 04 05 01 02 03 07
constexpr int kAmtOverviewSlotCount = 8;
extern const u8 kAmtOverviewSlotOfficeType[kAmtOverviewSlotCount];

// ===========================================================================
// VIBE_Office_PrepareCandidatePage  (gilde.exe 0x555eb4)
// ===========================================================================
//
// __usercall (eax=form, edx=primaryWin, ebx=scrollWin). Selects the primary
// window, clears its rich text ("$C"), removes the page children, and — unless
// scrollWin == -1 — resets the page child count, selects the scroll window,
// clears it, creates the scroll buttons, and re-selects the primary window.
//
// The Form/Text/Window leaves are hooks; the pure result this returns is captured
// here: a small descriptor saying whether the scroll sub-window was set up and the
// two window indices that were touched, plus the function's integer return value
// (always 0 unless scrollWin == -1, where it returns an uninitialized ecx — modelled
// as 0 since the only caller stores it then overwrites the page descriptor).
struct PreparePageResult {
    i32 primaryWin;     // a2
    i32 scrollWin;      // a3 (-1 => no scroll buttons)
    bool builtScroll;   // a3 != -1
    i32 returnValue;    // 0 (faithful for the wired call sites)
};
PreparePageResult OfficePrepareCandidatePage(i32 primaryWin, i32 scrollWin);

// ===========================================================================
// VIBE_Office_ApplyCandidateRatingBars  (gilde.exe 0x556ba0)
// ===========================================================================
//
// __usercall (al ret; eax=headPerson, edx=count, ebx=entries). For each of `count`
// entries (stride 14 DWORDs) whose slot is populated (entry[0]!=0, the pointed
// person's first word != 0xFFFF, entry[2] != -1) it sets the rating bar:
//   - if BOTH the head person's category byte (head+2) and the entry person's
//     category byte (person+2) are in {6,7}  -> bar value forced to 100 (a "max"
//     rating: VIBE_Object_SetValueOrText(entry[2], 0,100, 0,..)).
//   - otherwise -> bar value = favorability(head, entryPerson, 1).
// Head==null or head[0]==0xFFFF short-circuits (no bars).
//
// This reconstructs the *selection rule* (which formula a row uses); the favorability
// computation and the SetValueOrText/Coord leaves are hooks. We expose the per-row
// decision: returns, for one row, whether it is rendered and which mode it uses.
enum class RatingBarMode { Skip, MaxValue, Favorability };

// gilde.exe 0x556bcf/0x556c3b — category-pair test: both in {6,7}.
inline bool RatingBarBothNobility(u8 headCat, u8 entryCat) {
    bool h = (headCat == 6 || headCat == 7);   /*0x556bcf*/
    bool e = (entryCat == 6 || entryCat == 7); /*0x556c3b*/
    return h && e;
}

// One row of the rating-bar pass. `populated` mirrors the original guard
// (entry[0]!=0 && *(person)!=0xFFFF && entry[2]!=-1).
RatingBarMode RatingBarRowMode(bool headValid, bool populated,
                               u8 headCat, u8 entryCat);

// ===========================================================================
// VIBE_Amt_RunCandidateSelectionWindow  paging/index core (gilde.exe 0x556c40)
// ===========================================================================
//
// The window keeps a current role index dword_63D618 and, per role, a per-role
// row count dword_63D57C[5*i] (filled by the collector fn). The pure controller
// rules, extracted from the decompile:

// gilde.exe 0x556c9b — if the caller passes an explicit start index in [0,count)
// it becomes the current index; then 0x556cae clamps an out-of-range index to 0.
inline i32 AmtClampStartIndex(i32 current, i32 requested, int count) {
    if (requested > -1 && requested < count) /*0x556c9b*/
        current = requested;                  /*0x556c9d*/
    if (static_cast<u32>(current) >= static_cast<u32>(count)) /*0x556cae*/
        current = 0;                          /*0x556cb2*/
    return current;
}

// gilde.exe 0x556d2b / 0x556fe4 — "find first non-empty role". If the role at the
// current index has zero rows, set index = count then scan rows 0..count-1 and stop
// at the first non-empty role; if NONE is non-empty the index stays == count (which
// the caller later treats as "no list" and forces back to 0 at 0x556f75).
//   rowCounts[i] is dword_63D57C[5*i].
// Returns the (possibly updated) current index.
i32 AmtFindFirstNonEmptyRole(i32 current, const i32* rowCounts, int count);

// gilde.exe 0x556f6d — at the top of each frame the selected list-row count is read
// only when the index is in range; otherwise the index is reset to 0.
//   if (index < count)  selectedCount = rowCounts[index];  else { index = 0; ... }
struct SelectedRoleState { i32 index; i32 selectedCount; bool inRange; };
SelectedRoleState AmtSelectRoleAtFrameTop(i32 index, const i32* rowCounts, int count);

// ---------------------------------------------------------------------------
// Per-role-button vertical layout (gilde.exe 0x556df1 .. 0x556ee2).
// ---------------------------------------------------------------------------
// The role buttons are evenly distributed down the list sub-window. With the
// sub-window inner height H (the original reads it from the form geometry) the
// layout computes, ONCE per build:
//     span  = H + 1 - 33*count                 ; 0x556df1 (33 = button height)
//     rem   = span % (count - 1)                ; 0x556e10
//     step  = span / (count - 1)                ; 0x556e1e (truncating int div)
//     y     = 0                                 ; first button at y=0 (0x556e2d v62)
//   for each button i:
//     place button at current y
//     y += (rem-- >= 0 ? 1 : 0) + step + 33     ; 0x556ee2  (distributes remainder)
// i.e. the leftover pixels (rem) are spread one-per-row over the first `rem+1` rows.
// NOTE the original decrements `rem` BEFORE the >=0 test, so the bump applies while
// the pre-decrement value is >= 0 — reproduced exactly below.
//
// Returns the Y coordinate of each of `count` buttons given inner height H.
// `count` must be >= 2 (the original divides by count-1; count==8 in practice).
void AmtComputeRoleButtonYs(int innerHeight, int count, i32* outYs);

// gilde.exe 0x556df1 — span numerator helper (exposed for golden tests).
inline i32 AmtRoleButtonSpan(int innerHeight, int count) {
    return innerHeight + 1 - 33 * count; /*0x556df1*/
}
constexpr int kAmtRoleButtonHeight = 33;

// ===========================================================================
// VIBE_Amt_RunOfficeOverviewWindow  per-slot core (gilde.exe 0x5575c8)
// ===========================================================================
//
// The overview shows 8 office slots. Slots 1..6 (the central column) use one label
// placement, slots 0 and 7 (top/bottom) use another (0x5577ce: the alt branch is
// taken when slotIndex <= 0 OR slotIndex >= 7). The office-type label for slot i is
// dword_8C7288[kAmtOverviewSlotOfficeType[i]].
inline bool AmtOverviewSlotUsesAltLayout(int slotIndex) {
    return slotIndex <= 0 || slotIndex >= 7; /*0x5577ce*/
}

// gilde.exe 0x55784c — the "apply for office" extra slot (v32[0]) is enabled (state
// 1 / flag 3) iff there is at least one occupied office, else disabled (state 0 /
// flag 0). `hasOccupied` is the VIBE_Amt_HasOccupiedOffice result.
struct OverviewApplySlotState { i32 enabledFlag; u8 styleByte; };
inline OverviewApplySlotState AmtOverviewApplySlotState(bool hasOccupied) {
    return hasOccupied ? OverviewApplySlotState{1, 3}   /*0x557936/0x557948*/
                       : OverviewApplySlotState{0, 0};  /*0x557866/0x55787c*/
}

// gilde.exe 0x55796b..0x557980 — on a click, find which of the 8 office-slot objects
// matches the clicked object id. Returns the slot index 0..7, or 8 ("not found",
// the original's loop terminal) which then routes to the apply-slot / no-op branch.
// `slotObjectIds` is v31[0..7]; `clickedObjId` is dword_62D22C.
int AmtOverviewFindClickedSlot(const i32* slotObjectIds, int slotCount,
                               i32 clickedObjId);

// ===========================================================================
// VIBE_Amt_HasOccupiedOffice  (gilde.exe 0x480cb4)
// ===========================================================================
//
// Scans the 7 council-office records (indices 215..209 stepping -1 in `v0`; the
// byte/dword tables are indexed by v1 = 216,210,...). A record counts as "occupied"
// when:
//   - its office-type high byte (dword_62EC8E[3*officeTypeByte] HIBYTE) == 7, AND
//   - its holder id (dword_B5984C[v1]) != -1, AND
//   - that holder id resolves to a live person record.
// Returns 1 on the first occupied office, else 0.
//
// The live tables are process state; we reconstruct the loop against an injected
// view so it is testable. `slot` carries the three inputs per office.
struct OfficeOccupancySlot {
    int officeTypeHiByte; // HIBYTE(dword_62EC8E[3*type])
    i32 holderId;         // dword_B5984C[v1]   (-1 == vacant)
    bool holderResolves;  // VIBE_Person_FindRecordById(holderId) != null
};
// gilde.exe 0x480cd3..0x480ce2 — per-slot occupied test.
inline bool AmtOfficeSlotOccupied(const OfficeOccupancySlot& s) {
    return s.officeTypeHiByte == 7 && s.holderId != -1 && s.holderResolves;
}
// Faithful scan over the council offices in original order. Returns 1/0.
int AmtHasOccupiedOffice(const OfficeOccupancySlot* slots, int count);

// ===========================================================================
// VIBE_Amt_OpenOfficeWindow  (gilde.exe 0x5546a0)
// ===========================================================================
//
// Builds a 3-DWORD descriptor on the stack { ?, 516, ? } with a trailing byte 6 and
// forwards to VIBE_Amt_RunCandidateSelectionWindow(&desc, a2, 0, 0). The leading and
// trailing fields are: [1]=516 (the session kind), byteAt+0xC = 6 (the office
// category). The candidate window + the gray-color thunk are hooks. We expose the
// descriptor the original constructs.
struct OpenOfficeDescriptor {
    i32 word1;        // v5[1] = 516
    u8  categoryByte; // v6   = 6
};
inline OpenOfficeDescriptor AmtBuildOpenOfficeDescriptor() {
    return OpenOfficeDescriptor{516, 6}; /*0x5546c8 / 0x5546cc*/
}

// ===========================================================================
// Inert-default hooks (UI / render / network / live-array leaves)
// ===========================================================================
// These are intentionally no-ops / identity in the headless build; the real backends
// (Form/HUD/Vulkan, command queue, person array) wire them in the wired build.
struct AmtWindowHooks {
    // Form lifecycle.
    void* (*formBuild)(const char* asset) = nullptr;
    void  (*formCenter)(void* form) = nullptr;
    void  (*formSelectWindow)(void* form, int win) = nullptr;
    void  (*formSetObjectsVisible)(void* form, int visible) = nullptr;
    void  (*formDestroy)(void* form) = nullptr;
    // Frame loop: returns nonzero to keep spinning.
    int   (*runFrameLoop)(int selector, int a, void* b) = nullptr;
};

} // namespace guild::world
