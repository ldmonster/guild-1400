#pragma once
// guild::gui — the tooltip CONTENT BUILDERS (the dispatch's draw path).
//
// gui/tooltip_dispatch.cpp reconstructs VIBE_Tooltip_DispatchByType @0x4f7424 (the
// per-frame lifecycle) and gui/tooltip_build.{h,cpp} the small pure cores.  This
// module reconstructs the BUILDERS the dispatcher fires — i.e. what the original
// actually puts on screen for a hovered subject — 1:1 from the disassembly:
//
//   VIBE_Tooltip_BuildObject   @0x4f7a10  object tooltip (Handelsgut/Waffen form)
//   VIBE_Tooltip_BuildUpgrade  @0x4f8154  upgrade tooltip (Objekte_Upgrades form)
//   VIBE_Tooltip_BuildBuilding @0x4f78e4  building tooltip (emission order; the
//                                         layout values reuse tooltip_build's
//                                         Tooltip_BuildingLayout / kBuildingColors)
//   VIBE_Tooltip_BuildPerson   @0x4f84ac  person tooltip (Personen form)
//   VIBE_Tooltip_BuildContact  @0x4f83e8  contact tooltip (emission order; the key
//                                         resolve reuses Tooltip_ResolveContact)
//   + the shared screen-edge clamp block (0x4f7aa5..0x4f7ae7 / 0x4f81c2..0x4f8204)
//
// NOT translated (out of the dispatch call tree / other clusters):
//   * VIBE_Tooltip_BuildPersonDetailed @0x4f882c — called only from
//     VIBE_MapView_PanelDispatcher @0x5441d0, not from the dispatch (rule 7).
//   * VIBE_Text_RenderRichString @0x59d6e8 — the rich-text renderer (0x22b2 bytes,
//     345 blocks; the text cluster's named gap).  Every RichString emission is
//     routed through the TooltipContentHost edge in the original argument order.
//   * the economy/person-table leaves each builder calls
//     (VIBE_Building_ComputeMarketPrice @0x58f3d0, LookupCachedMarketPrice
//     @0x58f6b8, VIBE_Object_ComputeMarketValue @0x594df0, Person_GetCashAmount
//     @0x58bc9c, Person_ComputeTotalWealth @0x591f7c, BuildingType_
//     ComputeRankWithinGroup @0x58a560, Person_FindRecordById @0x58bc6c,
//     He_FindFirstHandlerByFilter @0x4c63f8) — their RESULTS are supplied through
//     the env views below; the control flow that consumes them is byte-exact.
//
// Static data recovered byte-exact with get_bytes:
//   * kTooltipProductionTable — the 23-row x 21-byte producer table @0x6496A9
//     (the "which building produces this item" walk of BuildObject), including
//     the 3 trailing bytes the original's final unaligned int read touches.
//   * kObjWorkerWeightBits / kObjWorkerScale — dbl_6206D8 / dbl_6206E0.
//   * kUpgradePriceScale / kUpgradePriceBias — dbl_620728 / dbl_620730.

#include "gui/tooltip_build.h"   // kBuildingColors, BuildingTooltipLayout (REUSED)

#include <cstring>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Form asset names (gilde.exe .rdata).
// ---------------------------------------------------------------------------
inline constexpr const char* kFormTooltipTradegood = "ToolTip\\ToolTip_Objekte_Handelsgut"; // @0x62067c
inline constexpr const char* kFormTooltipWeapon    = "ToolTip\\ToolTip_Objekte_Waffen";     // @0x6206a0
inline constexpr const char* kFormTooltipUpgrade   = "ToolTip\\ToolTip_Objekte_Upgrades";   // @0x6206e8
inline constexpr const char* kFormTooltipBuilding  = "ToolTip\\ToolTip_Gebaeude";           // @0x620660
inline constexpr const char* kFormTooltipPerson    = "ToolTip\\ToolTip_Personen";           // @0x620768
inline constexpr const char* kFormTooltipContact   = "ToolTip\\ToolTip_Contact";            // @0x620744

// Rich-format strings (gilde.exe .rdata) the builders pass to RenderRichString.
inline constexpr const char* kFmtCountName   = "%i %s$N";    // @0x6206c0
inline constexpr const char* kFmtName        = "%s$N";       // @0x6206c8
inline constexpr const char* kFmtClear       = "$C";         // @0x6206d0
inline constexpr const char* kFmtPlusAmount  = "+%a %s$N";   // @0x62070c
inline constexpr const char* kFmtPercentName = "%i%% %s$N";  // @0x620718
inline constexpr const char* kFmtContactHead = "$Z$[%s$]";   // @0x62075c
inline constexpr const char* kFmtPersonHead  = "$Z$[%1N3$]"; // @0x620784
inline constexpr const char* kFmtIndent      = "$3>%s$A";    // @0x620790
inline constexpr const char* kFmtIndentName  = "$3>%1N1$A";  // @0x620798
inline constexpr const char* kFmtLabelColon  = "%s:";        // @0x6207a4

// ---------------------------------------------------------------------------
// Exact double constants (bit patterns read from .rdata with get_bytes).
// ---------------------------------------------------------------------------
inline constexpr unsigned long long kObjWorkerWeightBits = 0x3F70410441041010ull; // dbl_6206D8
inline double TooltipObjWorkerWeight() {                      // dbl_6206D8 (~1/252)
    double d; unsigned long long b = kObjWorkerWeightBits;
    std::memcpy(&d, &b, sizeof d); return d;
}
inline constexpr double kObjWorkerScale    = 0.25; // dbl_6206E0
inline constexpr double kUpgradePriceScale = 0.25; // dbl_620728
inline constexpr double kUpgradePriceBias  = 0.5;  // dbl_620730

// ---------------------------------------------------------------------------
// The producer table @0x6496A9 (23 rows x 21 bytes + the 3 bytes the last
// unaligned int read covers).  Row layout (as walked by 0x4f7d77..0x4f7dfb):
//   byte +3      : producer building code  (icon = code+1010, name = 14*code+1078)
//   words +4..+22: produced object ids (10 signed-word slots, step 2; the walk
//                  reads `(int at row+2k+2) >> 16` for k = 1..10, the last read
//                  crossing one byte into the next row exactly as the binary does)
// ---------------------------------------------------------------------------
inline constexpr int kProductionRows      = 23;  // cmp esi, 17h @0x4f7e15
inline constexpr int kProductionRowStride = 21;  // imul ..., 15h @0x4f7d5a
extern const u8 kTooltipProductionTable[kProductionRows * kProductionRowStride + 3];

// Tooltip text-template ids (the small first args of RenderRichString).
inline constexpr int kTipDurability   = 0x21; // "durability" line
inline constexpr int kTipPricePair    = 0x1F; // cached/current market price
inline constexpr int kTipUpgradePrice = 0x23; // scaled upgrade price
inline constexpr int kTipBaseValue    = 0x20; // record +34 value
inline constexpr int kTipValueRatio   = 0x22; // owner value/market ratio
inline constexpr int kTipIngredients  = 0x25; // "ingredients" header
inline constexpr int kTipProducers    = 0x26; // "produced by" header
inline constexpr int kTipUpgradeHead  = 0x24; // upgrade-effect header

// Object-name text-id arithmetic (window 1/2 captions).
inline constexpr int kObjNameTextBias = 2151; // 2*code + 2151 (name)
inline constexpr int kObjDescTextBias = 2152; // 2*code + 2152 (description)
inline constexpr int kObjIconBias     = 206;  // icon object id = code + 206
inline constexpr int kBldIconBias     = 1010; // building icon  = code + 1010
inline constexpr int kBldNameStride   = 14;   // building name  = 14*code + 1078
inline constexpr int kBldNameBias     = 1078;
inline constexpr int kUpgEffectBias   = 3203; // upgrade effect = 2*kind + 3203
inline constexpr int kMarketIconObj   = 1039; // the "+market" row icon (0x40F)
inline constexpr int kMarketTextId    = 1484; // the "+market" row name (0x5CC)

// Row geometry of the icon/text rows (windows 3/4 of the object tooltip).
inline constexpr int kTipRowBaseY  = 40; // first row y    (mov edi, 28h)
inline constexpr int kTipRowPitch  = 22; // per-row pitch  (add edi, 16h)
inline constexpr int kTipRowSlotIn = 0;  // ingredient icon x   (eax = 0)
inline constexpr int kTipRowSlotBy = 6;  // producer icon x     (eax = 6)

// Screen-edge clamp margin (the 0x4f7aa5 / 0x4f81c2 block).
inline constexpr int kTooltipScreenMargin = 16;

// gilde.exe 0x4f7aa5..0x4f7ae7 (and the identical 0x4f81c2..0x4f8204) — the
// tooltip on-screen clamp: when the form's anchor window overflows the right
// screen edge minus 16px, it is re-laid-out at x = screenW - 16 - winW (y kept).
// Returns true when the relayout fires; *clampedX receives the new x.
bool Tooltip_ClampToScreen(int winX, int winW, int screenW, int* clampedX);

// ---------------------------------------------------------------------------
// The runtime edge.  One virtual per VIBE_ call, invoked in the exact original
// order with the exact original arguments.  Defaults are inert.
// ---------------------------------------------------------------------------
struct TooltipContentHost {
    virtual ~TooltipContentHost() = default;
    // VIBE_GameTick_Finalize(0, 0, name) @0x41beb8 — load the ToolTip form.
    virtual int  LoadForm(const char* name) { (void)name; return -1; }
    // VIBE_Form_CenterChildWindows @0x41d6ac.
    virtual void CenterChildWindows(int form) { (void)form; }
    // Anchor-window geometry (dword_67EB80[238 * dword_676A64[171*form]]):
    // x = (int at +2)>>16, y = (int at +4)>>16, w = word at +8.  False = unknown
    // (no clamp test, like a zero-size window that never overflows).
    virtual bool WindowGeom(int form, int* x, int* y, int* w) {
        (void)form; (void)x; (void)y; (void)w; return false;
    }
    // VIBE_Widget_LayoutBounds @0x413220 — move the anchor window.
    virtual void MoveWindow(int x, int y) { (void)x; (void)y; }
    // VIBE_Form_SelectWindow(form, sub) @0x41e4cc (form==0 selects on current).
    virtual void SelectWindow(int form, int sub) { (void)form; (void)sub; }
    // VIBE_Object_AddToWindow(curWin, objId, 0, 0) @0x41ae10 — the header icon.
    virtual void AddIconObject(int objId) { (void)objId; }
    // VIBE_Object_AddAnimatedToWindow(curWin, y, x, objId, -2) @0x41af64.
    virtual void AddAnimatedObject(int objId, int x, int y) {
        (void)objId; (void)x; (void)y;
    }
    // VIBE_Text_RenderRichString @0x59d6e8 — the four recovered argument shapes.
    virtual void Text(int textId) { (void)textId; }
    virtual void TextArg(int templateId, int a0) { (void)templateId; (void)a0; }
    virtual void TextArg2(int templateId, int a0, int a1) {
        (void)templateId; (void)a0; (void)a1;
    }
    virtual void TextFmt(const char* fmt, int a0, int a1) {
        (void)fmt; (void)a0; (void)a1;
    }
    // VIBE_Object_AddTextLabel(0, y, curWin, "%s:"-formatted text) @0x41b288;
    // the label string is RenderFormattedMessage(buf, "%s:", text[textId]).
    virtual void TextLabel(int x, int y, int textId) {
        (void)x; (void)y; (void)textId;
    }
    // VIBE_Hud_BuildScaledTiledBar(100, y, skillRow, person, 1162) @0x4bd758.
    virtual void SkillBar(int cur, int y, int skillRow, int baseGfx) {
        (void)cur; (void)y; (void)skillRow; (void)baseGfx;
    }
    // VIBE_Hud_BuildPersonCardSimple(166, 5, statusCtx, curWin) @0x55433c.
    virtual void PersonCard(int x, int y) { (void)x; (void)y; }
};

// ---------------------------------------------------------------------------
// Modelled record views — exactly the fields each builder dereferences.
// ---------------------------------------------------------------------------

// The 65-byte object record @ dword_13CE27C + 65*code.
struct TooltipObjectView {
    u8  classByte = 0;                   // +0   (23/37 select the weapon rows)
    i32 baseValue = 0;                   // +34  (dword; the 0x20/0x22 line)
    u16 ingredientCount[4] = {0,0,0,0};  // +38/+40/+42/+44
    u16 ingredientItem[4]  = {0,0,0,0};  // +46/+48/+50/+52
    u8  durability = 0;                  // +64  (0 -> market-price branch)
};

// Runtime-state results the object/upgrade builders read (globals + economy
// leaves; each field cites its source).
struct TooltipObjectEnv {
    // VIBE_Avatar_LookupById(code) @0x4859b0 non-null with *(u16*)rec != 0
    // selects the Waffen form (0x4f7a51).
    bool isWeapon = false;
    // dword_631744 || dword_631748 || dword_11BC278 (the selected owner).
    bool hasOwner = false;
    // No-durability branch: ComputeMarketPrice(code,100) @0x58f3d0 and
    // LookupCachedMarketPrice(code, byte_6477A1) @0x58f6b8, both already
    // int-converted through VIBE_Coord_ConvertX (the fistp at 0x4f7f6a/0x4f7f8d).
    i32 priceNow = 0;
    i32 priceCached = 0;
    // Owner branch: VIBE_Object_ComputeMarketValue @0x594df0 over the owner's
    // collected person items (the 536-stride dword_12CEA7C walk), plus the
    // active-worker byte v14[+129] (Person_FindActiveByEntity @0x5920b0 or the
    // word_12CE910[536 * *(u16*)(owner+39)] fallback).
    double marketValue = 0.0;
    int    activeWorkerByte = 0;
    // dword_69FFBC >> 16 — the screen width the clamp tests against.
    int screenW = 0;
};

// gilde.exe 0x4f7a10 — VIBE_Tooltip_BuildObject (code@ax).
// Emits the full object tooltip through `host` in the original order; returns
// the loaded form id.  `code` is the 16-bit object id (e.g. 468).
int Tooltip_BuildObjectContent(i16 code, const TooltipObjectView& rec,
                               const TooltipObjectEnv& env,
                               TooltipContentHost& host);

// gilde.exe 0x4f7e32..0x4f80df (inside 0x4f7a10) — the weapon/tool "used by"
// building-code mapping.  Pure: object id -> up to two building codes.
//   449..451           -> {24,25}        452..454 -> {30}
//   464..466, 445..448 -> {32}           439..441, 458..460 -> {30}
//   461..463, 442..444 -> {31}           455 / others -> none
int Tooltip_WeaponUserCodes(int objectId, u8 out[2]);

// gilde.exe 0x4f7ebc..0x4f7ee5 — the "+market" row gate: id in 449..454 (the
// weapon set minus 455) suppresses it; otherwise class 23/37 emits it.
bool Tooltip_ObjectMarketRow(int objectId, u8 classByte);

// The owner-branch value line of 0x4f7a10 (0x4f7c27..0x4f7c75):
//   ratio = baseValue / (marketValue + workerByte * dbl_6206D8 * dbl_6206E0)
// int-converted exactly like the original fistp (truncation toward zero is the
// FPU default the binary runs with for fistp after ConvertX's load).
int Tooltip_ObjectValueRatio(i32 baseValue, double marketValue, int workerByte);

// ---------------------------------------------------------------------------
// Upgrade tooltip (gilde.exe 0x4f8154 — VIBE_Tooltip_BuildUpgrade).
// ---------------------------------------------------------------------------

// The owner's 589-byte building row @ dword_13CE294 + 589 * (*ownerClassByte):
// exactly the three parallel slot arrays the scan reads.
struct TooltipUpgradeOwnerView {
    bool present = false;       // owner record resolved (631744/631748/11BC278)
    u8   objClassGate = 0;      // *(u8*)objectRecord — 2/6 suppress the scan
    u16  slotWords[64] = {0};   // +35 + 2i  (HIBYTE &= 0x7F before compare)
    u8   kindBytes[64] = {0};   // +419 + i
    u8   valueBytes[64] = {0};  // +483 + i
};

struct TooltipUpgradeEnv {
    // (double)dword_63C744 * 0.25 + 0.5 scaled ComputeMarketPrice(code,100),
    // already int-converted through ConvertX (fistp @0x4f839d).
    i32 scaledPrice = 0;
    int screenW = 0;            // dword_69FFBC >> 16
};

// gilde.exe 0x4f8154 — VIBE_Tooltip_BuildUpgrade (code@ax).  Returns -1 when the
// object's class byte is 29 (the Tooltip_UpgradeApplies gate, REUSED), else the
// form id.  `objClassByte` is objectBase[65*code] (the gate + the 2/6 owner gate).
int Tooltip_BuildUpgradeContent(i16 code, u8 objClassByte,
                                const TooltipObjectView& rec,
                                const TooltipUpgradeOwnerView& owner,
                                const TooltipUpgradeEnv& env,
                                TooltipContentHost& host);

// ---------------------------------------------------------------------------
// Building tooltip (gilde.exe 0x4f78e4 — VIBE_Tooltip_BuildBuilding).
// The layout VALUES are tooltip_build's Tooltip_BuildingLayout; this emits them
// through the host in the recovered call order:
//   LoadForm(Gebaeude); Center; SelectWindow(0,1); RichString(0x27, 14c+1078, color);
//   SelectWindow(0,2); AddIcon(code+1010); RichString(14c+1079); SelectWindow(0,3);
//   RichString(0x2A, color); RichString(0x28, salePrice); RichString(0x29, +579).
// ---------------------------------------------------------------------------
inline constexpr int kTipBldTitle = 0x27; // title line (name + colour)
inline constexpr int kTipBldDesc  = 0x2A; // colour line
inline constexpr int kTipBldPrice = 0x28; // ComputeSalePrice line
inline constexpr int kTipBldExtra = 0x29; // record +579 line

int Tooltip_BuildBuildingContent(int code, u8 colorSelector, i32 extraField,
                                 i32 salePrice, TooltipContentHost& host);

// ---------------------------------------------------------------------------
// Person tooltip (gilde.exe 0x4f84ac — VIBE_Tooltip_BuildPerson).
// ---------------------------------------------------------------------------

// The person-record fields the builder dereferences (by original byte offset).
struct TooltipPersonView {
    u16 id = 0;            // +0    ($Z$[%1N3$] header arg, cash/wealth key)
    u8  female = 0;        // +9    (selects the female text bases)
    u8  classCode = 0;     // +12   (0x33 line: +1070)
    u8  religion = 0;      // +13   (0x37 line: +272 male / +279 female)
    u8  jobCode = 0;       // +356  (byte; 0x2C line: +294/+370, rank arg)
    u8  trait1 = 0;        // +358  (+525/+560)
    u8  trait2 = 0;        // +361  (+525/+560)
};

// Resolved runtime results (the economy/person-table leaves).
struct TooltipPersonEnv {
    // Person_ResolveStatusFlags @0x553ce8 != 0 -> Hud_BuildPersonCardSimple.
    bool statusCard = false;
    i32  cash = 0;          // Person_GetCashAmount @0x58bc9c via ConvertX
    i32  rank = 0;          // BuildingType_ComputeRankWithinGroup @0x58a560
    i32  wealth = 0;        // Person_ComputeTotalWealth @0x591f7c
    // Spouse line: name code of Person_FindRecordById(+92) when its byte +8 is
    // set, else -1.  Betrothed: the He_FindFirstHandlerByFilter(1,0,65) walk
    // matching handler[+0xAC] == person[+4]; -1 when unresolved.
    i32 spouseNameId = -1;
    i32 betrothedNameId = -1;
    // Children: Person_FindRecordById of the five ids at +104..+120; -1 = none.
    i32 childNameIds[5] = {-1, -1, -1, -1, -1};
};

// Text bases of the person tooltip (recovered immediates).
inline constexpr int kPersonJobBaseM      = 294;  // 0x126
inline constexpr int kPersonJobBaseF      = 370;  // 0x172
inline constexpr int kPersonTraitBaseM    = 525;  // 0x20D
inline constexpr int kPersonTraitBaseF    = 560;  // 0x230
inline constexpr int kPersonReligionBaseM = 272;  // 0x110
inline constexpr int kPersonReligionBaseF = 279;  // 0x117
inline constexpr int kPersonClassBase     = 1070; // 0x42E
inline constexpr int kPersonSkillTextBase = 4810; // 0x12CA + i
inline constexpr int kPersonSkillRows     = 5;
inline constexpr int kPersonSkillPitch    = 15;   // label y = 15*i, bar y = 2+15*i
inline constexpr int kPersonSkillBarGfx   = 1162; // 0x48A
inline constexpr int kPersonCardX         = 166;  // 0xA6
inline constexpr int kPersonCardY         = 5;
inline constexpr int kTipPersonCash       = 0x2B;
inline constexpr int kTipPersonJob        = 0x2C;
inline constexpr int kTipPersonTraitsHead = 0x2D;
inline constexpr int kTipPersonWealth     = 0x2E;
inline constexpr int kTipPersonFamilyHead = 0x2F;
inline constexpr int kTipPersonSpouse     = 0x30;
inline constexpr int kTipPersonBetrothed  = 0x31;
inline constexpr int kTipPersonUnmarried  = 0x32;
inline constexpr int kTipPersonClass      = 0x33;
inline constexpr int kTipPersonChildHead  = 0x34;
inline constexpr int kPersonNoChildText   = 0x35; // "$3>%s$A" arg (53)
inline constexpr int kTipPersonReligion   = 0x37;

// gilde.exe 0x4f84ac — VIBE_Tooltip_BuildPerson (record@eax).  Returns the form.
int Tooltip_BuildPersonContent(const TooltipPersonView& p,
                               const TooltipPersonEnv& env,
                               TooltipContentHost& host);

// ---------------------------------------------------------------------------
// Contact tooltip (gilde.exe 0x4f83e8 — VIBE_Tooltip_BuildContact).
// The key resolve is tooltip_build's Tooltip_ResolveContact (REUSED by the
// caller); this emits the found-branch in the recovered order:
//   LoadForm(Contact); Center; SelectWindow(0,1); TextFmt("$Z$[%s$]", idx);
//   SelectWindow(0,2); Text(idx+1).
// Returns the form id (`textIndex` must be the resolved index; the not-found
// branch never reaches here — the original returns -1 before loading the form).
// ---------------------------------------------------------------------------
int Tooltip_BuildContactContent(int textIndex, TooltipContentHost& host);

} // namespace guild::gui
