#pragma once
// guild::gui — gui_dialogs8: the last untranslated slice of the VIBE_Tooltip_* builder
// family, translated 1:1 from gilde.exe. Wave 14's tooltip_build.{h,cpp} recovered the
// pure leaf cores of BuildContact / BuildBuilding / BuildUpgrade, and tooltip.cpp /
// tooltip_dispatch.cpp recovered the DispatchByType classifier. This file recovers the
// three remaining builders proper — the ones that drive the form/text runtime and the
// person/object databases:
//
//   0x4f7a10  VIBE_Tooltip_BuildObject          object/trade-good tooltip (price + crafters
//                                               + animated material/level rows).
//   0x4f84ac  VIBE_Tooltip_BuildPerson          person tooltip (cash, title, rank, spouse,
//                                               talent, wealth, employer, children, stat bars).
//   0x4f882c  VIBE_Tooltip_BuildPersonDetailed  the person tooltip wrapped in a modal idle
//                                               loop (class-byte gate at +2, extra panel,
//                                               drag begin, Form_Destroy on exit).
//
// All three are dominated by the retained-mode form/text runtime and the world/economy
// databases (price evaluation, person-record walks). Those leaves live in other clusters
// and are routed through an installable GuiDialogs8Hooks struct with inert defaults
// defined in gui_dialogs8.cpp — the house pattern (GuiDialogs7Hooks / CutsceneMiscHooks).
// Tests install their own hooks. The already-reconstructed siblings Form_SelectWindow /
// Form_CenterChildWindows / Form_Destroy are REUSED directly via their real headers (not
// hooked), and BuildPerson's rank value forwards into the REAL reconstructed
// sim::BuildingType_ComputeRankWithinGroup (building_type.cpp 0x58a560) exactly as the
// live wiring does.
//
// The GUI-OWNED deterministic core of each builder — the text-index arguments it computes
// from the person record (title/rank/spouse/talent text ids, the wealth/guild ids, the
// five stat-bar label ids and y-positions) and the modal class-byte gate — is reproduced
// byte-for-byte and exposed as pure helpers so it is golden-vector testable headless.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Person tooltip record offsets (byte offsets into the person record `a1`).
// Recovered from the BuildPerson / BuildPersonDetailed decompiles.
// ===========================================================================
//   +0    : id word (rendered as "$Z1n3", arg *a1)
//   +2    : detail-gate byte (BuildPersonDetailed: early-out when < 10)
//   +9    : gender/role byte (selects every "male/female" text-id base)
//   +13   : talent-level byte (id 0x37 = +13 byte + base)
//   +353  : packed dword; high byte = building-group code (-> rank); >>24 also a title delta
//   +358  : spouse/title field A  (0 -> not present)
//   +361  : spouse/title field B  (0 -> not present)
inline constexpr int kPersonDetailGateByte = 2;
inline constexpr int kPersonGenderByte     = 9;
inline constexpr int kPersonTalentByte      = 13;
inline constexpr int kPersonPackedDword     = 353;
inline constexpr int kPersonSpouseA         = 358;
inline constexpr int kPersonSpouseB         = 361;

// BuildPersonDetailed early-out: only fires the modal tooltip when record[+2] < 10.
inline constexpr int kPersonDetailGateMax   = 10;

// Text-id base constants (recovered verbatim from the decompile).
inline constexpr int kTitleBaseMale     = 370;  // record[+9] != 0
inline constexpr int kTitleBaseFemale   = 294;  // record[+9] == 0
inline constexpr int kSpouseBaseMale    = 560;  // record[+9] != 0
inline constexpr int kSpouseBaseFemale  = 525;  // record[+9] == 0
inline constexpr int kSpouseNoneTextId  = 0x20D; // 525 fallback when neither field set
inline constexpr int kTalentBaseMale    = 279;  // record[+9] != 0
inline constexpr int kTalentBaseFemale  = 272;  // record[+9] == 0
inline constexpr int kGuildTextBias     = 1070; // (record[+9]>>24) + 1070  (id 0x33)

// The five-stat bar block (id 3 window).
inline constexpr int kStatBarCount      = 5;
inline constexpr int kStatBarLabelBase  = 4810; // first label string-index ("%s:")
inline constexpr int kStatBarYStart     = 2;    // first bar y
inline constexpr int kStatBarYStep      = 15;   // per-row y step
inline constexpr int kStatBarWidthArg   = 100;  // Hud_BuildScaledTiledBar width arg
inline constexpr int kStatBarKindArg    = 1162; // Hud_BuildScaledTiledBar kind arg

// ---------------------------------------------------------------------------
// Pure deterministic core helpers (GUI-owned arithmetic, golden-vector testable).
// ---------------------------------------------------------------------------

// gilde.exe 0x4f854b/0x4f8568 — the id-0x2C "title" argument:
//   (record[+353] >> 24) + (record[+9] ? 370 : 294)
int Person_TitleTextId(const u8* record);

// gilde.exe 0x4f8588.. — the id-0x2D/$3>%s$A "spouse/title" rendering decision.
struct PersonSpouseLayout {
    bool hasAny   = false;  // record[+358] || record[+361]
    bool showA    = false;  // record[+358] != 0
    bool showB    = false;  // record[+361] != 0
    int  textIdA  = 0;      // record[+358] + (record[+9] ? 560 : 525)
    int  textIdB  = 0;      // record[+361] + (record[+9] ? 560 : 525)
    int  noneTextId = kSpouseNoneTextId; // rendered when !hasAny
};
PersonSpouseLayout Person_SpouseLayout(const u8* record);

// gilde.exe 0x4f85e8/0x4f8601 — the id-0x37 "talent" argument:
//   record[+13] + (record[+9] ? 279 : 272)
int Person_TalentTextId(const u8* record);

// gilde.exe 0x4f8663 — the id-0x33 "guild" argument: (record[+353_signed]>>24) + 1070.
// (The original re-reads the dword at record+9 and arithmetic-shifts right 24; that is
// the signed high byte of the dword that *starts* at +9.)
int Person_GuildTextId(const u8* record);

// gilde.exe 0x4f86be.. — one stat-bar row's layout (label string index, bar y, args).
struct StatBarRow {
    int labelTextId; // kStatBarLabelBase + i
    int barY;        // kStatBarYStart + kStatBarYStep * i
    int width;       // kStatBarWidthArg
    int kind;        // kStatBarKindArg
};
// row in [0, kStatBarCount).
StatBarRow Person_StatBarRow(int row);

// gilde.exe 0x4f8840 — BuildPersonDetailed's modal gate: record[+2] < 10.
bool Person_DetailGateOpen(const u8* record);

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults. Signatures mirror the
// raw decompiled ABI so each call site translates 1:1.
// ===========================================================================
struct GuiDialogs8Hooks {
    // --- form / text runtime ----------------------------------------------
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName); // VIBE_GameTick_Finalize @0x41beb8
    int  (*textRenderRichString)(unsigned id, unsigned arg);      // VIBE_Text_RenderRichString @0x59d6e8
    int  (*textRenderFormatted)(char* out, const char* fmt, const char* arg); // VIBE_Text_RenderFormattedMessage
    int  (*objectAddTextLabel)(int a, i16 b, int win, const char* text);      // VIBE_Object_AddTextLabel
    int  (*objectAddToWindow)(int win, int obj);                  // VIBE_Object_AddToWindow @0x41ae10
    int  (*objectAddAnimatedToWindow)(int win, int x, int y, int obj, int z); // VIBE_Object_AddAnimatedToWindow
    void (*lightSetGrayThunk)(int a, int b, int descPtr);         // VIBE_Light_SetGrayColorThunk @0x5c6af0
    double (*coordConvertX)(double v);                            // VIBE_Coord_ConvertX @0x5c6b08

    // --- person / economy database (other clusters) ------------------------
    int  (*personResolveStatusFlags)(void* ctx);                  // VIBE_Person_ResolveStatusFlags
    int  (*hudBuildPersonCardSimple)(int a, int b, void* ctx);    // VIBE_Hud_BuildPersonCardSimple
    double (*personGetCashAmount)(int id);                        // VIBE_Person_GetCashAmount
    int  (*buildingTypeRankWithinGroup)(u8 groupCode);            // VIBE_BuildingType_ComputeRankWithinGroup (real sibling, hookable)
    int  (*personComputeTotalWealth)(int id, const i16* rec);     // VIBE_Person_ComputeTotalWealth
    const i16* (*personFindRecordById)(int id);                   // VIBE_Person_FindRecordById
    const u8*  (*heFindFirstHandler)(int a, int b, int c);        // VIBE_He_FindFirstHandlerByFilter
    const u8*  (*heFindNextHandler)();                            // VIBE_He_FindNextMatchingHandler
    int  (*hudBuildScaledTiledBar)(int w, int y, int row, int rec, int kind); // VIBE_Hud_BuildScaledTiledBar

    // --- object tooltip economy leaves -------------------------------------
    const i16* (*avatarLookupById)(i16 id);                       // VIBE_Avatar_LookupById @0x4859b0
    int  (*widgetLayoutBounds)(int a, int b, int c);              // VIBE_Widget_LayoutBounds @0x413220
    double (*buildingComputeMarketPrice)(i16 code, unsigned pct); // VIBE_Building_ComputeMarketPrice
    double (*buildingLookupCachedMarketPrice)(i16 code, u8 q);    // VIBE_Building_LookupCachedMarketPrice
    int  (*buildingSumWorkstation)(const char* p, int a, int b);  // VIBE_Building_SumWorkstationByCategory
    const i16* (*personFindActiveByEntity)(const i16* ent);       // VIBE_Person_FindActiveByEntity
    float (*objectComputeMarketValue)(const char* p, int n, const int* ids); // VIBE_Object_ComputeMarketValue

    // --- the modal idle loop (BuildPersonDetailed tail) --------------------
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* self); // VIBE_GameLogic_RunFrameLoop @0x4c09a0
    void (*dragSlotBeginDragText)(void* self);                     // VIBE_DragSlot_BeginDragText
    int  (*readMouseRelease)();                                    // dword_672230 (cancel edge)
};

// Install a hooks struct (nullptr restores the inert defaults). Returns previous.
const GuiDialogs8Hooks* SetGuiDialogs8Hooks(const GuiDialogs8Hooks* hooks);
const GuiDialogs8Hooks* GuiDialogs8Hooks_Default();
const GuiDialogs8Hooks& GuiDialogs8HooksActive();

// Reset module-owned state + restore default hooks (deterministic test start).
void ResetGuiDialogs8();

// ===========================================================================
// Recovered builders (1:1 with the originals).
// ===========================================================================

// gilde.exe 0x4f84ac — VIBE_Tooltip_BuildPerson (record@eax).
// Builds the "person" tooltip form: title bar (id $Z1n3 = *record), the person card
// (when ResolveStatusFlags succeeds), cash (id 0x2B), title (id 0x2C, rank from the
// REAL BuildingType_ComputeRankWithinGroup), spouse/title rows (id 0x2D / $3>%s$A),
// talent (id 0x37), wealth (id 0x2E), employer (id 0x30/0x31/0x32 via the person-record
// walk), guild (id 0x33), children rows, and the five stat bars. Returns the form id.
int Tooltip_BuildPerson(const i16* record);

// gilde.exe 0x4f882c — VIBE_Tooltip_BuildPersonDetailed (record@eax).
// Early-outs (returns `record` unchanged) when record[+2] >= 10. Otherwise builds the
// same body as BuildPerson plus an extra Light_SetGrayColorThunk panel, then spins the
// modal idle loop (forcing the force-quit latch on the cancel edge) until it exits,
// begins the drag text, and destroys the form. Returns the form id when the gate is
// open, or 0 when closed (the original returns `result@eax` unchanged — a pointer; we
// model the gate-closed case as "no form built" = 0 so the ABI is host-pointer-safe).
int Tooltip_BuildPersonDetailed(const i16* record);

// gilde.exe 0x4f7a10 — VIBE_Tooltip_BuildObject (objectId@ax).
// Builds the "object / trade good" tooltip: picks the weapon vs. trade-good .form by the
// avatar lookup, clamps the layout bounds to the screen, renders the name/price (either a
// flat price or the buy/sell market pair), the owner's profit row when a crafter is found,
// then the animated material rows and the level/quality rows. Returns the form id.
int Tooltip_BuildObject(i16 objectId);

} // namespace guild::gui
