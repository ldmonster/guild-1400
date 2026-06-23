#pragma once
// guild::gui — recruit_office: 1:1 reconstruction of the recruitment candidate
// COLLECTION + the office candidate-list / session-timer rendering bodies + the
// guild-law (Gesetz) person-selection candidate-harvest logic from gilde.exe.
//
// This module reconstructs the GAME-LOGIC bodies that surround the recruitment /
// office / law dialogs (the dialog frame-loop shells themselves live in
// gui/personnel_gui.cpp, world/office_recon_privilege.h and world/office_law3.h;
// the deterministic kernels there are reused via include/extern — no ODR clash).
//
// Functions recovered here (addr — original VIBE_ symbol):
//
//   0x55d530  VIBE_Recruit_CollectNearbyRecruitCandidates
//             spatial query (FindPeopleByPalette) -> map each found palette index
//             through the person-id column (dword_12CE914[134*idx]) into outIds[].
//   0x555f8c  VIBE_Office_ShowCandidateListWithRoles
//             build the office candidate card list: per-row x=75 / y=112*i+40,
//             role label, enable pass, and the page-descriptor stride/anchor write.
//   0x49d910  VIBE_Office_RenderSessionTimer
//             format the 14x-scaled tick delta as "%2i : %2i : %3i ms" and submit.
//   0x55a224  VIBE_Gesetz_RunPersonSelectionWindow (candidate-harvest core)
//             gather the eligible person ids for the law person-selection window
//             from the subject's spouse/child ids + the two relation tables
//             (spouse-link dword_12CEA80, child-link dword_12CEA7C), filtered by
//             the subject's building-type state, with de-duplication vs the two
//             already-listed ids and the 16-candidate / 512-byte caps.
//   0x55a9bc  VIBE_Gesetz_OpenPersonSelectionIfValid
//             validity gate that opens the selection window when the subject's
//             building type is not "busy".
//
// The renderer / form / HUD leaves are routed through installable hook structs
// with inert defaults (the house pattern). The DETERMINISTIC selection/collection
// logic is reconstructed in full against the real Person array.

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::gui {

// ===========================================================================
// 0x55d530 — VIBE_Recruit_CollectNearbyRecruitCandidates.
// ===========================================================================
// The original:
//   1. sets the search gray-color window (0,40) ; 0x55d545
//   2. builds the palette filter desc: v9 = 21513 normally, or 21512 if the
//      reference record's +9 byte (gender/married flag) is zero ; 0x55d54a/0x55d563
//   3. FindPeopleByPalette(ref, maxPeople=9, filter=&v9, 30.0, 100.0, &tmp16) ;
//      0x55d581  -> writes *outCount and a tmp array of up to 9 palette indices
//   4. for each found index, outIds[k] = dword_12CE914[134*tmpIdx] (the person id
//      column of that slot) ; 0x55d5a8
//
// `referenceRec` is the recruiter Person record (the original's eax/v4). The
// spatial query leaf is reconstructed in sim/pathfind_map.cpp
// (ObjectSearchFindPeopleByPalette); we forward to it through a hook so the
// collection is testable headless. `outIds` must hold at least 9 entries.
struct RecruitCollectHooks {
    void (*setGrayColor)(int value, int kind);          // VIBE_Light_SetGrayColorThunk 0x5c6af0
    // VIBE_ObjectSearch_FindPeopleByPalette 0x47b008. Writes up to maxPeople palette
    // indices into outSlots[]; returns the count.
    int  (*findPeopleByPalette)(const guild::u16* refMarker, int maxPeople, int filter,
                                float minR, float maxR, guild::u16* outSlots);
    void* ctx;
};
void RecruitCollectSetHooks(const RecruitCollectHooks& h);
void RecruitCollectResetHooks();

// Returns the number of candidates collected (== *outCount). outIds receives that
// many person ids (mapped from palette slots through the id column). refByte9 is
// the reference record's +9 byte (selects the 21512/21513 filter constant).
constexpr int kRecruitMaxCandidates = 9;     // a2 = 9
constexpr int kRecruitFilterBase    = 21512; // 0x5408
int RecruitCollectNearbyCandidates(const guild::u16* refMarker, guild::u8 refByte9,
                                   guild::i32* outIds);

// ===========================================================================
// 0x555f8c — VIBE_Office_ShowCandidateListWithRoles (candidate card list build).
// ===========================================================================
// Pure record-side effect of the row-build loop, extracted so it can be golden-
// pinned. For each of `count` rows the original writes the card x/y into the row
// descriptor (stride 56 bytes per row), builds a person card, adds a role label
// from a role string table, marks the row interactive, and (in a second pass)
// enables the card object. Finally it stores the per-page row stride (112) and the
// page anchor into the page-descriptor table.
//
// Here we reconstruct the deterministic per-row geometry write into a caller-
// supplied row array; the HUD/object leaves are routed through a hook.
struct OfficeListRow {
    int x;        // descriptor +16  (always 75)
    int y;        // descriptor +20  (112*i + 40)
    int roleId;   // descriptor +44  (role index, supplied by the caller's page)
};
struct OfficeListHooks {
    void (*buildPersonCard)(int x, int y, int color, void* rowPtr, int flag); // 0x553f30
    int  (*addRoleLabel)(int x, int y, int win, const char* roleName);        // 0x41b288
    void (*setObjectColor)(int objId, int color);                             // 0x41e614
    void (*setObjectEnabled)(int objId, int enabled);                         // 0x41e318
    const char* (*roleName)(int roleId);  // dword_8C8140[roleId] string table
    void* ctx;
};
void OfficeListSetHooks(const OfficeListHooks& h);
void OfficeListResetHooks();

// Returns 1 when rows were built (count != 0), 0 otherwise (the original's 0x555f9a
// early return). Fills rows[0..count-1] with the per-row x/y; pageRowStride and
// pageAnchor receive the values written to dword_67EDE4/dword_67EDC8.
int OfficeShowCandidateListWithRoles(unsigned count, OfficeListRow* rows,
                                     int pageAnchorIn, int* pageRowStride,
                                     int* pageAnchorOut);

// ===========================================================================
// 0x49d910 — VIBE_Office_RenderSessionTimer.
// ===========================================================================
// Formats "%2i : %2i : %3i ms" from the 14x-scaled tick delta and submits it to
// the on-screen timer label. The split math is OfficeSessionTimeSplit (reused from
// world/office_recon_privilege.h). Here we expose the exact formatted string so it
// can be golden-pinned; the actual label submit is routed through a hook.
struct OfficeTimerHooks {
    void (*submitTimerText)(const char* text);  // VIBE_Animation_Apply label submit
    void* ctx;
};
void OfficeTimerSetHooks(const OfficeTimerHooks& h);
void OfficeTimerResetHooks();

// Writes the formatted timer string into `dest` (must hold >= 32 bytes) and submits
// it via the hook. nowTick/startTick are dword_62EB38 / dword_6315D8.
void OfficeRenderSessionTimer(char* dest, unsigned destSize,
                              guild::u32 nowTick, guild::u32 startTick);

// ===========================================================================
// 0x55a224 — VIBE_Gesetz_RunPersonSelectionWindow (candidate-harvest core).
// ===========================================================================
// The selection window gathers up to 16 eligible person ids (markers) for the
// subject record. The harvest is deterministic and load-bearing:
//   * seed with the subject's spouse id (+37 word) if != 0xFFFF       (0x55a877)
//   * add the subject's child id (+39 word) if != 0xFFFF and != spouse (0x55a2ab)
//   * if the subject's building-type state byte is 1 or 2: scan the spouse-link
//     table (dword_12CEA80, record+0x170) for every person bound to the subject,
//     adding each marker not already equal to the subject's +37/+39 words (0x55a342)
//   * if the state byte is in the "extended" set (3<s<10, 10<s<15, ==16,
//     17<s<23): same scan over the child-link table (dword_12CEA7C, record+0x16C)
//   * caps: at most 16 candidates and at most 512 bytes of the 32-byte-strided
//     candidate buffer.
// Returns the harvested marker ids in `outMarkers` (>= 16 entries) and the count.
//
// `subjectStateByte` is *v57 (the building-type state byte at
// dword_13CE294 + 589*subjectType). The two link tables are modeled as the live
// Person array's relation slot 0x170 (spouse) / 0x16C (child) holding the
// subject's record id when a person is bound to the subject.
int GesetzHarvestSelectionCandidates(const guild::sim::Person* subject,
                                     guild::u8 subjectStateByte,
                                     guild::u16* outMarkers);
constexpr int kGesetzMaxCandidates = 16; // v61 < 16 cap

// Classifies the subject's state byte into the harvest mode.
//   0 -> none ; 1 -> spouse-table scan ; 2 -> child-table scan.
// (A subject may take BOTH: state 1/2 always does spouse; the extended set adds
// child. The two passes are independent, mirroring the two `if` blocks.)
struct GesetzHarvestMode { bool scanSpouseTable; bool scanChildTable; };
GesetzHarvestMode GesetzClassifyState(guild::u8 stateByte);

// ===========================================================================
// 0x55a9bc — VIBE_Gesetz_OpenPersonSelectionIfValid.
// ===========================================================================
// Validity gate: returns false (do-not-open) when the subject is null or its
// building type maps to a "busy" state; otherwise the caller opens the selection
// window. The building-type->state map is routed through a hook (the real leaf is
// VIBE_Building_MapTypeToState @0x592a5c).
struct GesetzOpenHooks {
    int (*mapTypeToState)(guild::u8 buildingType, void* outCtx); // 0x592a5c (nonzero=busy)
    void* ctx;
};
void GesetzOpenSetHooks(const GesetzOpenHooks& h);
void GesetzOpenResetHooks();

// subjectBuildingType is *(_BYTE*)subject (the subject record's +0 byte). Returns
// true when the selection window should open (subject present AND not busy).
bool GesetzOpenPersonSelectionIfValid(bool subjectPresent, guild::u8 subjectBuildingType);

// ===========================================================================
// The three recruitment MODAL WINDOW DRIVERS (full 1:1 bodies).
// ===========================================================================
//   0x55d990  VIBE_Recruit_RunHireConfirmDialog       — cost display, OK/cancel
//                                                        frame loop, hire packet.
//   0x55db0c  VIBE_Recruit_RunCandidatePickWindow     — collect nearby candidates,
//                                                        build the 3-column card
//                                                        grid, click->hire-confirm.
//   0x55de00  VIBE_Recruit_RunRecruitmentOfferWindow  — He-handler mode select,
//                                                        per-mode body, bribe-bonus
//                                                        roll + loyalty mutation,
//                                                        4-button slot dispatch.
//
// The form/frame-loop/text/command/He/dialog leaves are the SDL/Vulkan + engine-
// subsystem boundary; they are routed through RecruitWindowHooks (inert defaults,
// the house pattern). The candidate-list build, the grid geometry, the selection
// state machine, the offer/confirm decision logic and the hire/loyalty state
// mutation are reconstructed in full from the decompile (addresses inline).
//
// An opaque He-handler record is modeled by HeHandler: the driver only ever reads
// the specific fields the original reads (dword 43/44/47, byte 181/182/184/185/
// 186/187/188), so the accessor surface is exactly those.

// One He "Handlung" handler record (332 bytes in the original, byte_11D6040 +
// 332*i). The window only touches the fields below; everything else is opaque.
struct HeHandler {
    guild::i32 entityId;     // +0x04 dword[1]  (== *((DWORD*)v2+1) match key)
    guild::i32 dword43;      // +0xAC dword[43] (entity id checked vs player id)
    guild::i32 dword44;      // +0xB0 dword[44] (candidate person id; v65)
    guild::i32 dword47[3];   // +0xBC dword[47] (per-offer "good count" v59[8..10])
    guild::i32 dword33;      // +0x84 dword[33] (written: queued request id)
    guild::i8  byte181;      // +0xB5 byte[181] (rank-cost-low; >>24 in orig wealth)
    guild::i8  byte182;      // +0xB6 byte[182] (rank-cost-high; >>24)
    guild::i8  byte184;      // +0xB8 byte[184] (accumulated loyalty)
    guild::i8  byte185;      // +0xB9 byte[185] (loyalty threshold)
    guild::i8  byte186;      // +0xBA byte[186] (offer-pending flag)
    guild::i8  byte187;      // +0xBB byte[187] (rejected/away flag)
    guild::i8  byte188[3];   // +0xBC byte[188] (per-offer "good type" v63[i])
};

// All boundary leaves the three drivers call. Inert defaults make the drivers run
// to completion headless (the frame loop returns 0 -> exits at once; the He probe
// finds nothing; the cost hook returns 0). The deterministic GAME LOGIC is NOT in
// the hooks — it is in the driver bodies below.
struct RecruitWindowHooks {
    // --- form / frame-loop plumbing (SDL/Vulkan boundary) ---
    int  (*gameTickFinalize)(int a, int b, const char* form); // 0x41beb8 (->formId)
    void (*formCenterChildWindows)(int formId);               // 0x41d6ac
    void (*formSelectWindow)(int formId, int which);          // 0x41e4cc
    void (*formDestroy)(int formId);                          // 0x41da04
    void (*formSetChildrenVisible)(int formId, int on);       // 0x41d568
    int  (*formGetChildObjectId)(int formId, int a, int b);   // 0x41dea8
    int  (*gameLogicRunFrameLoop)(int tag, int a, const void* p); // 0x4c09a0
    void (*dragCursorSetSprite)(int a, int b);                // 0x41fcbc

    // --- text / HUD card build ---
    int  (*textRenderRichString)(unsigned id, unsigned a, int b, int c, int d); // 0x59d6e8
    void (*textRenderFormatted)(char* dst, int id, int a, int b);               // 0x59f99c
    void (*hudBuildPersonCard)(int x, int y, int color, void* rec, int flag);   // 0x553f30
    void (*hudAddCenteredLabel)(const char* txt, int x, int y, int z, int col); // 0x552778
    void (*personResolveStatusFlags)(void* card);             // 0x553ce8

    // --- engine globals the windows read at draw time ---
    int  (*playerPersonId)();   // dword_12CE914[134*word_63CC5C]
    int  (*panelW)();           // *(int*)(dword_62D298+6) >> 16   (panel width)
    int  (*cardW)();            // *(int*)(dword_62D204+117762) >> 16 (card width)
    int  (*cardH)();            // *(int*)(dword_62D204+117764) >> 16 (card height)

    // --- input edges (engine BSS latches dword_672230 / dword_672228 / dword_62D22C) ---
    int  (*readCancelEdge)();        // dword_672230 (nonzero -> quit)
    int  (*readLastClickedObject)(); // dword_62D22C (clicked object id)
    int  (*readPrevSelectedRow)();   // dword_672228 (suppress re-trigger)

    // --- command queue / hire (engine command subsystem boundary) ---
    int  (*queueHireRequest)(int candidateId);     // 0x4948c8 SlotReset28
    int  (*commandGetPacketStatus)(int packet);    // 0x4939d4 (nonzero -> done)
    void (*amtRefreshGuildState)();                // 0x4becdc
    int  (*queueEntity29)(int a, void* handler);   // 0x4949c4
    void (*enqueueCmd15)(int a, int peer, int good, int rate); // 0x494604
    void (*gameTimeAdvance)(int a, int b, int c, int d);       // 0x583150
    void (*inputClearMouseButtons)(int mask);      // 0x40dca8

    // --- He handler table probe + per-person valuations (engine subsystem) ---
    // FindFirstHandlerByFilter(1,0,kind) then FindNextMatchingHandler iteration:
    // returns the first handler with kind==65 whose entityId matches, or null.
    HeHandler* (*heFindFirst)(int kind);   // 0x4c63f8
    HeHandler* (*heFindNext)();            // 0x4c6278
    int  (*personComputeOfficeRank)(int candidatePersonId); // 0x58bccc
    int  (*recruitComputeCost)(int recruiterId, int candidateId); // 0x55d674
    int  (*dialogCheckResourceAmount)(int good, int byte);  // 0x4ad62c
    void (*dialogShowMessageBox)(const char* txt, int a, int b); // 0x4ad6f0
    int  (*randomModulo)(int n);                            // 0x58b89c

    void* ctx;
};
void RecruitWindowSetHooks(const RecruitWindowHooks& h);
void RecruitWindowResetHooks();
const RecruitWindowHooks& RecruitWindowGetHooks();

// ---------------------------------------------------------------------------
// 0x55d990 — VIBE_Recruit_RunHireConfirmDialog.
// ---------------------------------------------------------------------------
// `candidateId` is the candidate's person id (the original reads it from
// dword_12CE914[134*word_63CC5C] — i.e. the player slot — for the cost AND for the
// hire packet; both refer to the player record in the original). Returns 1 when the
// player confirmed and the hire packet completed, 0 on cancel.
int RecruitRunHireConfirmDialog();

// ---------------------------------------------------------------------------
// 0x55db0c — VIBE_Recruit_RunCandidatePickWindow.
// ---------------------------------------------------------------------------
// Collects the nearby recruit candidates (RecruitCollectNearbyCandidates), lays
// them out in a 3-column card grid, and runs the modal pick loop: a click on a
// candidate card runs the hire-confirm dialog; on a successful hire it returns
// -110, otherwise 2.
//   refMarker / refByte9: the recruiter record marker + its +9 byte (forwarded to
//   the collect leaf). Returns the result byte (2 default, -110 on hire).
guild::i8 RecruitRunCandidatePickWindow(const guild::u16* refMarker, guild::u8 refByte9);

// ---------------------------------------------------------------------------
// 0x55de00 — VIBE_Recruit_RunRecruitmentOfferWindow.
// ---------------------------------------------------------------------------
// The recruitment "offer" entry. `entityMarker` is the clicked entity record
// (a1/v71): its +1 dword is the entity id (handler match key), +2 byte is the kind.
//   * Finds the He handler (kind 65) bound to this entity; derives the offer MODE
//     (1 no-handler / 2 pending / 3 rejected / 4 offer-window).
//   * kind!=6  -> if no handler and the bound person (+532) has its +8 flag set,
//                 queues a slot-reset (returns 16); else returns 32.
//   * kind==6 & mode 1 -> forwards to RunCandidatePickWindow.
//   * kind==6 & mode 2/3/4 -> opens the offer window; mode 4 runs the 4-button
//     bribe/decline dispatch with the loyalty-bonus roll + GameTime advance.
// Returns the result byte (2/16/32/-110 etc., matching the original al).
guild::i8 RecruitRunRecruitmentOfferWindow(const guild::u16* entityMarker,
                                           const guild::u16* refMarker,
                                           guild::u8 refByte9);

} // namespace guild::gui
