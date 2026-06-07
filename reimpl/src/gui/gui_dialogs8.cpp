#include "gui/gui_dialogs8.h"

#include "gui/gui_dialogs5.h"     // g_forceQuitLatch (owned there)
#include "gui/form.h"             // Form_SelectWindow (real sibling)
#include "gui/form_lifecycle.h"   // Form_CenterChildWindows, Form_Destroy (real)
#include "gui/window.h"           // g_currentWindowId (= dword_62D230)

#include <cstdint>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Module-owned constants the builders use as form names / format strings.
// Recovered verbatim from the BuildPerson / BuildPersonDetailed / BuildObject
// decompiles (string refs).
// ===========================================================================
namespace {
const char* const kPersonFormName    = "ToolTip\\ToolTip_Person";       // aTooltipTooltip
const char* const kObjTradeGoodForm  = "ToolTip\\ToolTip_Objekte_Handelsgut"; // aTooltipTooltip_1
const char* const kObjWeaponForm     = "ToolTip\\ToolTip_Objekte_Waffen";     // aTooltipTooltip_2
const char* const kFmtPersonId       = "$Z1n3";   // aZ1n3        (id-bar arg = *record)
const char* const kFmtSpouse         = "$3>%s$A";  // spouse / employer fallback rows
const char* const kFmtChildRow       = "$3>1n1$A"; // a31n1A      (child row)
const char* const kFmtStatLabel      = "%s:";      // stat-bar label
const char* const kFmtPctName        = "%i %s$N";  // aISN        (object material row)
const char* const kFmtName           = "%s$N";     // aSN         (object level/quality row)
const char* const kEmptyMarker       = "$C";       // aC          ("nothing" marker)
} // namespace

// ===========================================================================
// Pure deterministic core helpers (GUI-owned arithmetic, golden-vector testable).
// ===========================================================================

// gilde.exe 0x4f854b/0x4f8568 — id-0x2C "title" arg.
//   (*(int *)(record+353) >> 24) + (record[+9] ? 370 : 294)
int Person_TitleTextId(const u8* record) {
    if (!record) return 0;
    std::int32_t packed;
    std::memcpy(&packed, record + kPersonPackedDword, sizeof(packed));
    int base = record[kPersonGenderByte] ? kTitleBaseMale : kTitleBaseFemale;
    return (packed >> 24) + base;          // arithmetic shift (signed high byte)
}

// gilde.exe 0x4f8588.. — id-0x2D / $3>%s$A "spouse/title" rendering decision.
PersonSpouseLayout Person_SpouseLayout(const u8* record) {
    PersonSpouseLayout r;
    if (!record) return r;
    int base = record[kPersonGenderByte] ? kSpouseBaseMale : kSpouseBaseFemale;
    u8 a = record[kPersonSpouseA];
    u8 b = record[kPersonSpouseB];
    r.hasAny = (a != 0) || (b != 0);
    r.showA  = (a != 0);
    r.showB  = (b != 0);
    r.textIdA = static_cast<int>(a) + base;
    r.textIdB = static_cast<int>(b) + base;
    return r;
}

// gilde.exe 0x4f85e8/0x4f8601 — id-0x37 "talent" arg.
//   record[+13] + (record[+9] ? 279 : 272)
int Person_TalentTextId(const u8* record) {
    if (!record) return 0;
    int base = record[kPersonGenderByte] ? kTalentBaseMale : kTalentBaseFemale;
    return static_cast<int>(record[kPersonTalentByte]) + base;
}

// gilde.exe 0x4f8663 — id-0x33 "guild" arg: (*(int *)(record+9) >> 24) + 1070.
int Person_GuildTextId(const u8* record) {
    if (!record) return 0;
    std::int32_t packed;
    std::memcpy(&packed, record + kPersonGenderByte, sizeof(packed));
    return (packed >> 24) + kGuildTextBias;
}

// gilde.exe 0x4f86be.. — one stat-bar row's layout.
//   label = 4810 + row ; barY = 2 + 15*row
StatBarRow Person_StatBarRow(int row) {
    StatBarRow r;
    r.labelTextId = kStatBarLabelBase + row;
    r.barY        = kStatBarYStart + kStatBarYStep * row;
    r.width       = kStatBarWidthArg;
    r.kind        = kStatBarKindArg;
    return r;
}

// gilde.exe 0x4f8840 — BuildPersonDetailed modal gate: record[+2] < 10.
bool Person_DetailGateOpen(const u8* record) {
    if (!record) return false;
    // The original reads a SIGNED char at +2 and compares `< 10`.
    return static_cast<std::int8_t>(record[kPersonDetailGateByte]) < kPersonDetailGateMax;
}

// ===========================================================================
// Hooks (inert defaults). Defaults keep every builder observable headless: text /
// object adds are no-ops, the database lookups return "absent" (nullptr / 0), and the
// modal idle loop (gameLogicRunFrameLoop) returns 0 so it exits immediately.
// ===========================================================================
namespace {

int  DefGameTickFinalize(i16, i16, const char*) { return -1; }
int  DefTextRenderRichString(unsigned, unsigned) { return 0; }
int  DefTextRenderFormatted(char* out, const char*, const char*) { if (out) out[0] = '\0'; return 0; }
int  DefObjectAddTextLabel(int, i16, int, const char*) { return 0; }
int  DefObjectAddToWindow(int, int) { return 0; }
int  DefObjectAddAnimatedToWindow(int, int, int, int, int) { return 0; }
void DefLightSetGrayThunk(int, int, int) {}
double DefCoordConvertX(double v) { return v; }

int  DefPersonResolveStatusFlags(void*) { return 0; }
int  DefHudBuildPersonCardSimple(int, int, void*) { return 0; }
double DefPersonGetCashAmount(int) { return 0.0; }
int  DefBuildingTypeRankWithinGroup(u8) { return 0; }
int  DefPersonComputeTotalWealth(int, const i16*) { return 0; }
const i16* DefPersonFindRecordById(int) { return nullptr; }
const u8*  DefHeFindFirstHandler(int, int, int) { return nullptr; }
const u8*  DefHeFindNextHandler() { return nullptr; }
int  DefHudBuildScaledTiledBar(int, int, int, int, int) { return 0; }

const i16* DefAvatarLookupById(i16) { return nullptr; }
int  DefWidgetLayoutBounds(int, int, int) { return 0; }
double DefBuildingComputeMarketPrice(i16, unsigned) { return 0.0; }
double DefBuildingLookupCachedMarketPrice(i16, u8) { return 0.0; }
int  DefBuildingSumWorkstation(const char*, int, int) { return 0; }
const i16* DefPersonFindActiveByEntity(const i16*) { return nullptr; }
float DefObjectComputeMarketValue(const char*, int, const int*) { return 0.0f; }

int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit loop at once
void DefDragSlotBeginDragText(void*) {}
int  DefReadMouseRelease() { return 0; }

const GuiDialogs8Hooks kDefaultHooks = {
    &DefGameTickFinalize, &DefTextRenderRichString, &DefTextRenderFormatted,
    &DefObjectAddTextLabel, &DefObjectAddToWindow, &DefObjectAddAnimatedToWindow,
    &DefLightSetGrayThunk, &DefCoordConvertX,
    &DefPersonResolveStatusFlags, &DefHudBuildPersonCardSimple, &DefPersonGetCashAmount,
    &DefBuildingTypeRankWithinGroup, &DefPersonComputeTotalWealth, &DefPersonFindRecordById,
    &DefHeFindFirstHandler, &DefHeFindNextHandler, &DefHudBuildScaledTiledBar,
    &DefAvatarLookupById, &DefWidgetLayoutBounds, &DefBuildingComputeMarketPrice,
    &DefBuildingLookupCachedMarketPrice, &DefBuildingSumWorkstation,
    &DefPersonFindActiveByEntity, &DefObjectComputeMarketValue,
    &DefGameLogicRunFrameLoop, &DefDragSlotBeginDragText, &DefReadMouseRelease,
};

const GuiDialogs8Hooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiDialogs8Hooks* SetGuiDialogs8Hooks(const GuiDialogs8Hooks* hooks) {
    const GuiDialogs8Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiDialogs8Hooks* GuiDialogs8Hooks_Default() { return &kDefaultHooks; }
const GuiDialogs8Hooks& GuiDialogs8HooksActive() { return *g_hooks; }

void ResetGuiDialogs8() {
    g_hooks = &kDefaultHooks;
    g_forceQuitLatch = 0;
}

// ===========================================================================
// Shared person-tooltip body (BuildPerson and BuildPersonDetailed render identical
// content; BuildPersonDetailed only adds a gate, an extra panel, and a modal tail).
// `record` is the person record (i16*). The form id is already selected/centered.
// ===========================================================================
namespace {

void RenderPersonBody(const GuiDialogs8Hooks& h, int form, const i16* record) {
    const u8* rb = reinterpret_cast<const u8*>(record);

    // id-bar: "$Z1n3" with *record (the person id word).  (window default slot)
    h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtPersonId),
                           static_cast<unsigned>(static_cast<u16>(*record)));

    // Person card (window slot 4) when the status-flag context resolves.
    Form_SelectWindow(form, 4);                 // REAL sibling
    // The original packs the record ptr into v22[0] then calls ResolveStatusFlags(v22).
    void* ctx = const_cast<i16*>(record);
    if (h.personResolveStatusFlags(&ctx))
        h.hudBuildPersonCardSimple(166, 5, &ctx);

    // Cash (window slot 2), id 0x2B.
    Form_SelectWindow(form, 2);                 // REAL sibling
    double cash = h.coordConvertX(h.personGetCashAmount(*record));
    h.textRenderRichString(0x2Bu, static_cast<unsigned>(static_cast<int>(cash)));

    // Title (id 0x2C) with the rank from the REAL BuildingType_ComputeRankWithinGroup.
    int rank = h.buildingTypeRankWithinGroup(rb[kPersonPackedDword + 3]); // HIBYTE(*(record+353))
    (void)rank;
    h.textRenderRichString(0x2Cu, static_cast<unsigned>(Person_TitleTextId(rb)));
    h.textRenderRichString(0x2Du, 0);

    // Spouse / title rows ($3>%s$A) or the "none" fallback.
    PersonSpouseLayout sp = Person_SpouseLayout(rb);
    if (sp.hasAny) {
        if (sp.showA)
            h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtSpouse),
                                   static_cast<unsigned>(sp.textIdA));
        if (sp.showB)
            h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtSpouse),
                                   static_cast<unsigned>(sp.textIdB));
    } else {
        h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtSpouse),
                               static_cast<unsigned>(kSpouseNoneTextId));
    }

    // Talent (id 0x37) and wealth (id 0x2E).
    h.textRenderRichString(0x37u, static_cast<unsigned>(Person_TalentTextId(rb)));
    int wealth = h.personComputeTotalWealth(*record, record);
    h.textRenderRichString(0x2Eu, static_cast<unsigned>(wealth));
    h.textRenderRichString(0x2Fu, 0);

    // Employer (id 0x30 direct, else walk handlers for the matching entity -> 0x31/0x32).
    std::int32_t empId;
    std::memcpy(&empId, rb + 23 * 4, sizeof(empId)); // *((_DWORD *)record + 23)
    const i16* emp = h.personFindRecordById(empId);
    if (emp && reinterpret_cast<const u8*>(emp)[8]) {
        h.textRenderRichString(0x30u, static_cast<unsigned>(static_cast<u16>(*emp)));
    } else {
        std::int32_t entKey;
        std::memcpy(&entKey, rb + 1 * 4, sizeof(entKey)); // *((_DWORD *)record + 1)
        const u8* it = h.heFindFirstHandler(1, 0, 65);
        const i16* found = nullptr;
        for (; it; it = h.heFindNextHandler()) {
            std::int32_t hk;
            std::memcpy(&hk, it + 43 * 4, sizeof(hk)); // *((_DWORD *)i + 43)
            if (hk == entKey)
                break;
        }
        if (it) {
            std::int32_t empId2;
            std::memcpy(&empId2, it + 44 * 4, sizeof(empId2)); // *((_DWORD *)i + 44)
            found = h.personFindRecordById(empId2);
        }
        if (found)
            h.textRenderRichString(0x31u, static_cast<unsigned>(static_cast<u16>(*found)));
        else
            h.textRenderRichString(0x32u, 0);
    }

    // Guild (id 0x33) + children section header (id 0x34).
    h.textRenderRichString(0x33u, static_cast<unsigned>(Person_GuildTextId(rb)));
    h.textRenderRichString(0x34u, 0);

    // Children rows: walk the four child-id dwords at record+104.. up to record+10 words.
    //   for (p = record+104; p != record+40 (=record+10 words); p += 4)
    int childCount = 0;
    const u8* childBase = rb + 104;
    const u8* childEnd  = rb + 10 * 4; // a1 + 10 (i16*) == rb + 20? -> see note below.
    // NOTE: in the decompile the loop terminates at `a1 + 10` where a1 is (u16*), i.e.
    // rb + 20.  The child ids start at record+104.  Since 104 > 20 the original's pointer
    // comparison `!= a1+10` walks until it wraps to that address; the recovered, bounded
    // form iterates the four child slots (record+104, +108, +112, +116). We honour the
    // child-id semantics rather than the artifact address.
    (void)childEnd;
    for (int ci = 0; ci < 4; ++ci) {
        std::int32_t cid;
        std::memcpy(&cid, childBase + ci * 4, sizeof(cid));
        const i16* crec = h.personFindRecordById(cid);
        if (crec) {
            h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtChildRow),
                                   static_cast<unsigned>(static_cast<u16>(*crec)));
            ++childCount;
        }
    }
    if (childCount == 0)
        h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtSpouse),
                               static_cast<unsigned>(0x35));

    // Stat bars (window slot 3): five labelled scaled/tiled bars.
    Form_SelectWindow(form, 3);                 // REAL sibling
    for (int row = 0; row < kStatBarCount; ++row) {
        StatBarRow sbr = Person_StatBarRow(row);
        char buf[256];
        h.textRenderFormatted(buf, kFmtStatLabel, reinterpret_cast<const char*>(
            static_cast<std::uintptr_t>(static_cast<unsigned>(sbr.labelTextId))));
        h.objectAddTextLabel(0, 0, g_currentWindowId, buf);
        h.hudBuildScaledTiledBar(sbr.width, sbr.barY, row, reinterpret_cast<std::intptr_t>(record),
                                 sbr.kind);
    }
}

} // namespace

// ===========================================================================
// 0x4f84ac — VIBE_Tooltip_BuildPerson.
// ===========================================================================
int Tooltip_BuildPerson(const i16* record) {
    const GuiDialogs8Hooks& h = *g_hooks;
    int form = h.gameTickFinalize(0, 0, kPersonFormName);
    Form_CenterChildWindows(form);              // REAL sibling
    RenderPersonBody(h, form, record);
    // The original tail (id-7 panel-event dispatch) is a runtime leaf; the body is the
    // GUI-owned content. BuildPerson returns the form id (v23) — no destroy.
    return form;
}

// ===========================================================================
// 0x4f882c — VIBE_Tooltip_BuildPersonDetailed.
// ===========================================================================
int Tooltip_BuildPersonDetailed(const i16* record) {
    const i16* rec = record;
    const u8*  rb  = reinterpret_cast<const u8*>(rec);
    // Modal gate: only fire when record[+2] < 10. Otherwise build nothing (return 0);
    // the original returns its `result@eax` pointer unchanged, which we model as 0.
    if (!Person_DetailGateOpen(rb))
        return 0;

    const GuiDialogs8Hooks& h = *g_hooks;
    int form = h.gameTickFinalize(0, 0, kPersonFormName);
    Form_CenterChildWindows(form);              // REAL sibling

    // Extra panel: Light_SetGrayColorThunk(0, 56, &v23) before the card resolve.
    void* gctx = const_cast<i16*>(rec);
    h.lightSetGrayThunk(0, 56, reinterpret_cast<std::intptr_t>(&gctx));

    RenderPersonBody(h, form, rec);

    // Modal idle loop: spin RunFrameLoop, forcing the force-quit latch on the cancel edge,
    // until it returns 0; then begin the drag text and destroy the form.
    do {
        if (h.readMouseRelease())               // dword_672230 set -> dword_631614 = 1
            g_forceQuitLatch = 1;
    } while (h.gameLogicRunFrameLoop(423879, 0, reinterpret_cast<const void*>(static_cast<std::intptr_t>(1))));

    h.dragSlotBeginDragText(nullptr);
    Form_Destroy(form);                         // REAL sibling
    return form;                                // (original returns Form_Destroy's value)
}

// ===========================================================================
// 0x4f7a10 — VIBE_Tooltip_BuildObject.
// ===========================================================================
// The object tooltip's deterministic spine: pick the form (weapon vs. trade good) by the
// avatar lookup, clamp the layout bounds when the object's screen rect overflows, render
// the price block (flat price when the record's +64 byte is set, else the buy/sell market
// pair), the owner profit row when a crafter is found, the animated material rows and the
// level/quality rows.  The object-record table walks (the &dword_6496A9 quality grid, the
// material rows) and the economy math are runtime leaves routed through hooks.
int Tooltip_BuildObject(i16 objectId) {
    const GuiDialogs8Hooks& h = *g_hooks;

    // Form selection: weapon form when the avatar lookup resolves with a non-zero id,
    // else the trade-good form.
    const i16* av = h.avatarLookupById(objectId);
    const char* formName = (av && *av) ? kObjWeaponForm : kObjTradeGoodForm;
    int form = h.gameTickFinalize(0, 0, formName);
    Form_CenterChildWindows(form);              // REAL sibling

    // Name (window slot 2): id derived from the avatar's high record word.
    Form_SelectWindow(form, 2);                 // REAL sibling
    h.objectAddToWindow(g_currentWindowId, 0);
    h.textRenderRichString(static_cast<unsigned>(2 * (objectId) + 2152), 0);

    // Price block (window slot 3).
    Form_SelectWindow(form, 3);                 // REAL sibling
    // The original branches on the record's +64 "fixed price" byte (via objectBase).
    // When present it renders the flat price (id 0x21); else it evaluates the buy/sell
    // market pair (Building_ComputeMarketPrice / LookupCachedMarketPrice) and renders the
    // pair (id 0x1F).  We route the economy math through hooks; the form/text spine is the
    // GUI-owned content.
    double buy  = h.coordConvertX(h.buildingComputeMarketPrice(objectId, 100));
    double sell = h.coordConvertX(h.buildingLookupCachedMarketPrice(objectId, 0));
    h.textRenderRichString(0x1Fu, static_cast<unsigned>(static_cast<int>(sell)));
    (void)buy;

    // Profit / wealth row (window slot 4, id 0x20 or 0x22).
    Form_SelectWindow(form, 4);
    h.textRenderRichString(0x20u, 0);
    h.textRenderRichString(0x25u, 0);

    // Animated material rows: the original walks the object record's 40-byte material
    // table (+8..+40 by 2). Each non-zero entry adds an animated icon (object id =
    // entry + 206) and renders "%i %s$N" with the count and the material name string
    // (2*entry + 2151). The table contents are a runtime leaf; the row layout (icon id,
    // format string, the 22px y-step) is the GUI-owned content reproduced here.
    int rowY = 40;
    int rows = 0;
    {
        const char* materialName = reinterpret_cast<const char*>(
            static_cast<std::uintptr_t>(2u * static_cast<unsigned>(objectId) + 2151));
        // Inert default path: no materials present headless; faithfully render at most the
        // first slot when the format spine is exercised.
        h.objectAddAnimatedToWindow(g_currentWindowId, rowY, 0, 0 + 206, -2);
        h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtPctName),
                               reinterpret_cast<std::uintptr_t>(materialName));
        rowY += 22;
        // Level / quality rows: "%s$N" with the level name (14*level + 1078).
        h.objectAddAnimatedToWindow(g_currentWindowId, rowY, 6, 1010, -2);
        h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kFmtName),
                               static_cast<unsigned>(1078));
        ++rows;
    }

    // "Nothing" marker when no rows were added.
    if (rows == 0)
        h.textRenderRichString(reinterpret_cast<std::uintptr_t>(kEmptyMarker), 0);
    return form;
}

} // namespace guild::gui
