#pragma once
// Office / Privilege / Gesetz law-flow cores — wave-14 slice (file stem office_law3).
// Faithful 1:1 ports of a set of VIBE_Office_* / VIBE_Gesetz_* functions from
// gilde.exe that the earlier office.cpp / office_assign.cpp / law*.cpp / gesetz_flow.cpp
// passes left untranslated. These are: the office-holder TABLE INITIALIZER, the
// staff role-template TABLE SCAN, the promote-and-await-result FLOW glue, the law
// VIOLATION-DESCRIPTION text-id selectors (the Low/Mid/High family + dispatcher),
// and three small law-book MODAL dialog leaves.
//
// House style (see CONVENTIONS.md / law_text.h): functions that bottom out in a
// GUI / network / sim leaf we do not own route through an installable hooks struct
// whose DEFAULT implementation is inert and defined in office_law3.cpp. Functions
// that compute a deterministic id/result return that result so it is testable.
//
// Translated functions (addr — VIBE name):
//   0x47de78 VIBE_Office_InitTable                  -> OfficeInitHolderTable
//   0x57c184 VIBE_Office_FindRoleTemplate           -> OfficeFindRoleTemplate
//   0x562c9c VIBE_Office_AwaitPromoteResult         -> OfficeAwaitPromoteResult
//   0x4c2e74 VIBE_Gesetz_FormatDescriptionLow       -> GesetzFormatDescriptionLow
//   0x4c2f58 VIBE_Gesetz_FormatDescriptionMid       -> GesetzFormatDescriptionMid
//   0x4c3020 VIBE_Gesetz_FormatDescriptionHigh      -> GesetzFormatDescriptionHigh
//   0x4c3278 VIBE_Gesetz_FormatDescription          -> GesetzFormatDescription
//   0x55a9bc VIBE_Gesetz_OpenPersonSelectionIfValid -> GesetzOpenPersonSelectionIfValid
//   0x558bbc VIBE_Gesetz_ShowApplicationErrorDialog -> GesetzShowApplicationErrorDialog
//   0x558b30 VIBE_Gesetz_ShowLawBookInfoDialog      -> GesetzShowLawBookInfoDialog
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// 0x47de78 — VIBE_Office_InitTable.
// ===========================================================================
// Clears the 888-byte office-holder table (byte_B59848, 37 records x 24 B) to 0,
// then for each of the 37 records writes the holder-id byte = recordIndex and the
// two id-dwords (+4 city, +20 secondary) = -1. Finally it stamps a 37-entry
// office-TYPE seed into the `type` byte (+8) of each record (byte_B59850 family) so
// the table is born already mapped to the static office layout. Returns 7709
// (0x1E1D, the original's tail constant — an unused EAX residue, preserved).
//
// The seed value written into record r's type byte is kOfficeTypeSeed[r].
constexpr int kOfficeInitRecordCount = 37;   // the do/while runs while v3 < 37
constexpr int kOfficeInitRecordStride = 24;  // 6 dwords per record

// A record view matching the holder table the initializer writes (subset of the
// full OfficeHolder in law_types.h; this slice only sets holder/city/type/secondary).
struct OfficeInitRecord {
    u8  holder;     // +0  set to record index 0..36
    u8  pad1[3];
    i32 city;       // +4  set to -1
    u8  type;       // +8  set to kOfficeTypeSeed[r]
    u8  pad9[11];   // +9  fill to the secondary dword at +20
    i32 secondary;  // +20 set to -1
};
static_assert(sizeof(OfficeInitRecord) == kOfficeInitRecordStride,
              "OfficeInitRecord must be 24 bytes");

// The 37 office-type seed bytes, in record order, recovered verbatim from the
// byte_B5985x.. assignment block of VIBE_Office_InitTable.
extern const u8 kOfficeTypeSeed[kOfficeInitRecordCount];

// Initializes `table` (>= 37 records) exactly as the original. Returns 7709.
int OfficeInitHolderTable(OfficeInitRecord* table);

// ===========================================================================
// 0x57c184 — VIBE_Office_FindRoleTemplate.
// ===========================================================================
// The original picks one of two static 76-slot x 40-byte role-template tables by
// `which` (0 -> unk_63E338, 1 -> unk_63EF18; any other -> not found), then scans
// for the first record whose +0 id dword equals `roleId`, stopping at a zero id or
// after 76 entries, and returns &record. We return the record INDEX (portable
// equivalent; the caller uses the model id at record +36). Special cases:
//   roleId == 0 || roleId >= 76  -> kRoleNotFound (the original returns null/0).
//   which not in {0,1}           -> kRoleNotFound.
//   no match before the table's zero terminator / 76th slot -> returns the index
//     of that stop slot (the original returns &v4[10*v3], i.e. the end-of-scan
//     record), NOT not-found. Faithfully preserved.
constexpr int kRoleTemplateSlots = 76;
constexpr int kRoleNotFound = -1;

// The id columns (record +0 dword) of the two tables, recovered via get_bytes.
// Table A (which==0) has 75 live entries then a 0 terminator; table B (which==1)
// has 69. Lookups past the live count return the terminator index per the original.
extern const i32 kRoleTableA[kRoleTemplateSlots];
extern const i32 kRoleTableB[kRoleTemplateSlots];

// `which`: 0 == journeyman table (A), 1 == master table (B).
int OfficeFindRoleTemplate(u8 which, u8 roleId);

// ===========================================================================
// 0x562c9c — VIBE_Office_AwaitPromoteResult.
// ===========================================================================
// Drives a promotion: calls TryPromoteCharacter(person, fromCity, toCity); if it
// returns -1 (no command issued) -> false. If the promoted person's type byte
// (record +2) is not 6 (guild head) -> true immediately. Otherwise it spins
// pumping the guild-state refresh until the issued command's packet status is
// non-zero, then returns (status != 2) — i.e. true unless the command failed (2).
//
// The three engine leaves (promote, refresh, packet-status) are not owned here, so
// they route through OfficeFlowHooks. The default hooks are inert and deterministic
// (see office_law3.cpp): promote returns -1, so the default outcome is false.
struct OfficeFlowHooks {
    // VIBE_Office_TryPromoteCharacter @0x47ebd4 — returns a command id, or -1.
    i32 (*tryPromote)(i32 person, i32 fromCity, i32 toCity, void* ctx);
    // The promoted person's type byte (record +2). Used to decide whether to wait.
    u8  (*personType)(i32 person, void* ctx);
    // VIBE_Amt_RefreshGuildState @0x4becdc — pump one frame of guild state.
    void (*refresh)(void* ctx);
    // VIBE_Command_GetPacketStatusById @0x4939d4 — 0 == pending, else final status.
    int (*packetStatus)(i32 commandId, void* ctx);
    void* ctx;
};
// Installs the flow hooks for AwaitPromoteResult. Passing a null member keeps the
// inert default for that member. Reset to all-default with OfficeFlowHooksReset().
void OfficeSetFlowHooks(const OfficeFlowHooks& hooks);
void OfficeFlowHooksReset();

// Returns true on a successful promotion outcome (see above).
bool OfficeAwaitPromoteResult(i32 person, i32 fromCity, i32 toCity);

// ===========================================================================
// Gesetz violation-description text-id selectors (0x4c2e74 .. 0x4c3278).
// ===========================================================================
// These pick a localized message id for a law-violation explanation, resolve a
// person portrait/name id, and render it through VIBE_Text_RenderFormattedMessage
// @0x59f99c. The render leaf is a hook; the DETERMINISTIC result is the selected
// format text id and the result code, which the ports compute and return.
//
// Shared inputs (recovered from the __userpurge frames; the dispatcher repacks the
// same blob into each leaf):
//   op       : the law op-class byte (drives which message id is chosen).
//   value    : the tested numeric value (rendered as the %d arg; "a3" in the dump).
//   subjectId: the offender person id (the leaf resolves it to a portrait id).
//   subValue : a secondary value (Low op==2 uses it as a base id; "*(&a8+1)").
//   lowFlag  : the original's `a12` — `flag = (lowFlag <= 1)` is added as a 0/1
//              offset to the chosen base id (singular vs. plural phrasing).
//
// The chosen base ids are recovered verbatim from the binary.
struct GesetzDescResult {
    bool valid = false;     // 1 == a message id was selected & rendered, 0 == fallback
    int  textId = 0;        // the selected (already +flag) format message id
    bool usedFallback = false; // op out of range -> the "Hm, das ist doch nicht
                               // verboten" literal was copied; valid stays false.
};

// The portrait/name-id resolver (VIBE_Person_FindRecordById @0x58bc6c -> *rec, the
// person's word +0). Default returns kNoPortrait (-2), matching the original's
// "record == null -> v16 = -2" path. Tests install a model.
constexpr int kNoPortrait = -2;
using PortraitIdFn = int (*)(i32 personId, void* ctx);
// The format render leaf (VIBE_Text_RenderFormattedMessage). dest is the caller
// scratch buffer; textId is the selected id; the remaining args are opaque. Inert
// by default; tests install a recorder.
using DescRenderFn = void (*)(char* dest, int textId, void* ctx);
void GesetzSetDescHooks(PortraitIdFn portrait, DescRenderFn render, void* ctx);
void GesetzDescHooksReset();

// 0x4c2e74 — VIBE_Gesetz_FormatDescriptionLow  (op-class 0..7; handles 2,3,4).
//   op==2: base 4158; value-arg id = (value >= 117 ? subValue+4143 : subValue+4141)
//   op==3: base 4163
//   op==4: base 4168
//   else : copy fallback, valid=false.
GesetzDescResult GesetzFormatDescriptionLow(char* dest, u8 op, int value,
                                            i32 subjectId, int subValue,
                                            int lowFlag);

// 0x4c2f58 — VIBE_Gesetz_FormatDescriptionMid  (op-class 8..15; handles 13,15).
//   op==13: base 4213 ; op==15: base 4223 ; else fallback.
GesetzDescResult GesetzFormatDescriptionMid(char* dest, u8 op, int value,
                                            i32 subjectId, int lowFlag);

// 0x4c3020 — VIBE_Gesetz_FormatDescriptionHigh (op-class 16..25).
//   base id table: op16->4228 op17->4233 op18->4238 op19->4243 op20->4248
//                  op21->4253 op22->4258 op23->4263 op24->4268 op25->4273.
//   (the original always returns 0 here; valid reports whether op was in 16..25.)
GesetzDescResult GesetzFormatDescriptionHigh(char* dest, u8 op, int value,
                                             i32 subjectId, int lowFlag);

// 0x4c3278 — VIBE_Gesetz_FormatDescription dispatcher.
//   op < 8  -> Low ;  op < 16 -> Mid ;  op < 26 -> High ;  else valid=false.
GesetzDescResult GesetzFormatDescription(char* dest, u8 op, int value,
                                         i32 subjectId, int subValue,
                                         int lowFlag);

// ===========================================================================
// Law-book modal dialog leaves (0x55a9bc / 0x558bbc / 0x558b30).
// ===========================================================================
// Tiny GUI flow helpers. The form/text/frame-loop engine is not owned here; it
// routes through GesetzUiHooks. The deterministic CONTROL FLOW (which dialog form
// is opened, which text id is shown, the modal-loop bookkeeping) is preserved.
struct GesetzUiHooks {
    // VIBE_Building_MapTypeToState @0x592a5c — returns nonzero (busy) to abort the
    // selection window. Default returns 0 (available) so selection proceeds.
    int (*mapTypeToState)(u8 buildingType, void* ctx);
    // VIBE_Gesetz_RunPersonSelectionWindow @0x55a224 — returns a selected person id
    // (or 0). Default returns 0.
    i32 (*runPersonSelection)(i32 subject, const char* caption, i32 arg, void* ctx);
    // Opens a modal dialog by resource name and pumps it once. Returns the dialog's
    // result code. Default returns 0. (Models VIBE_GameTick_Finalize + the
    // RunFrameLoop modal pump + VIBE_Form_Destroy collapsed into one observable.)
    int (*showModal)(const char* resource, int primaryTextId, int secondaryTextId,
                     void* ctx);
    void* ctx;
};
void GesetzSetUiHooks(const GesetzUiHooks& hooks);
void GesetzUiHooksReset();

// 0x55a9bc — VIBE_Gesetz_OpenPersonSelectionIfValid.
//   subject == 0            -> returns 0 (the original returns the null `result`).
//   building busy (state!=0) -> returns 0 (selection aborted).
//   otherwise               -> returns RunPersonSelectionWindow(subject, caption, arg).
// `buildingType` is *(_BYTE*)subject in the original (the subject record's +0 byte).
i32 GesetzOpenPersonSelectionIfValid(i32 subject, u8 buildingType,
                                     const char* caption, i32 arg);

// 0x558bbc — VIBE_Gesetz_ShowApplicationErrorDialog.
// Opens "Gesetze\\Antrag_Fehler" and shows `errorTextId`, or a default literal id
// (kDefaultErrorTextId) when errorTextId == 0. Returns the modal result.
constexpr int kDefaultErrorTextId = 0;  // the original uses the aCeingabefehler string ptr
i32 GesetzShowApplicationErrorDialog(int errorTextId);

// 0x558b30 — VIBE_Gesetz_ShowLawBookInfoDialog.
// Opens "Gesetze\\Gesetzbuch_Info" and shows the record's two text ids (record +1
// and record +2 in the original). Returns the modal result.
i32 GesetzShowLawBookInfoDialog(int recordBaseTextId);

} // namespace guild::world
