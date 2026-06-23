#pragma once
// guild::world — VIBE_Location_* interaction rule-cores (recon batch 3).
//
// Reconstructed 1:1 from gilde.exe.  This module covers eight location/building
// interaction dialogs from the VIBE_Location_* family that were not yet present
// in the tree (the rest of the family — thief guild, church, guard, residence
// master/mistress-visit, tavern cards/rules/dark-corner-buy — already live in
// location*.cpp, location_church/residence/tavern/thief.cpp,
// tavern_cards_location_recon.* and townhall_location_recon.*).
//
//   VIBE_Location_BriberyMenu            0x512a6c — bribe-rival-officials menu.
//   VIBE_Location_TradeTransport         0x513568 — trade transport (import/export) panel.
//   VIBE_Location_TradeSearchExport      0x513c60 — "search to export" goods aggregation.
//   VIBE_Location_TradeSearchImport      0x5142ec — "search to import" goods aggregation.
//   VIBE_Location_ResidenceMistress      0x515524 — mistress list / take-a-mistress.
//   VIBE_Location_TavernStammtischLeave  0x517724 — leave the regulars' table.
//   VIBE_Location_TavernStammtischJoin   0x517a58 — join the regulars' table.
//   VIBE_Location_TavernDarkCornerBrowse 0x51816c — dark-corner informant browse.
//
// Each original is a modal dialog loop of the canonical shape
//     form = GameTick_Finalize(0,0,"locations\\...");   // open the .frm window
//     do { ... } while (GameLogic_RunFrameLoop(sel, ...));  // per-frame UI pump
//     Form_Destroy(form);
// The modal frame loop, the localized rich-text engine, the 3D-overlay/person-card
// renderer, the live person/handler arrays and the command queue all live in the
// io/sim/render clusters; they are modelled here as inert-default hooks so the pure
// rule kernels (eligibility gates, data aggregation, slot-list build, person-card
// geometry, the price/decay math) are translated and testable 1:1.
//
// Recovered table/string constants are spelled out below.  Asset/icon-id globals are
// runtime handles (zero at load) and are passed straight through to the render leaves,
// so they are not logic and are omitted.

#include "guild/common/types.h"

namespace guild::world {

using f32 = float;

namespace loc3 {

// VIBE_GameLogic_RunFrameLoop window selectors.
constexpr int kDialogForm  = 423879; // 0x67887 — modal dialog
constexpr int kContactForm = 425983; // 0x67FFF — contact menu (ResidenceMistress)
constexpr int kBlockingSel = 131079; // 0x20007 — blocking "wait for packet" pump

// dword_75BF38 click sentinels.
constexpr int kClickNone    = -1;    // no UI element clicked this frame
constexpr int kClickConfirm = 1210;  // the confirm/"buy" button id

// ===========================================================================
// VIBE_Location_BriberyMenu  0x512a6c  (misc\Bestechung)
// __usercall(edx=target, sil=flag)
//
// Gate / capacity rule core (no analogue substituted — this is the genuine logic):
//   * target must be non-null and target+39 (the building-type word) != 0xFFFF, else the
//     "can't bribe here" message (id 5791) is shown and the menu never opens.
//   * IsAnimalTargetBusy(target) must be true to proceed (the original returns early via
//     the 5791 path when it is false).
//   * Count rival officials already attached: iterate the *other* office's persons
//     (QueryFind kind 1, sub 202) and count those whose key (+21) != target's key (+1).
//   * Count "free" handlers: FindFirstHandlerByFilter(2,2,office,0,64); a handler counts
//     as free when its key (+43*4) matches none of the collected rivals' keys (+42*4).
//   * total = freeHandlers + rivals.  If total >= 2 * capacity_byte (building record
//     byte at +583, 589-byte stride per building-type), abort with message 5641 (total).
//   * Otherwise the bribe-slot list is built (see kBribeSlotCap below) and on confirm
//     a slot-reset command (handler 0x40 == 64) is queued.
// ===========================================================================
constexpr int kBribeBuildingTypeWord = 0xFFFF; // target+39 sentinel that blocks the menu
constexpr int kBribeMsgCantHere      = 5791;   // VIBE_Text_RenderFormattedMessage id
constexpr int kBribeMsgTooMany       = 5641;   // shown when total >= 2*capacity
constexpr int kBribeHandlerOp        = 64;     // handler filter / command opcode (0x40)
constexpr int kBribeRivalSubtype     = 202;    // QueryFind subtype for rival officials
constexpr int kBuildingRecStride     = 589;    // per-building-type record stride (bytes)
constexpr int kBuildingCapByteOff    = 583;    // capacity byte offset within the record
constexpr int kBribeSlotCap          = 8;      // slot list capped at 8 entries
constexpr int kSlotTableStride       = 134;    // dword_12CE914 stride per building-type
constexpr int kSlotTableLimit        = 102912; // loop bound (134 * 768)

// Inputs the bribery gate consults (all from live arrays / the target record).
struct BriberyDeps {
    virtual ~BriberyDeps() = default;
    virtual bool TargetValid()        { return false; } // target && target+39 != 0xFFFF
    virtual bool AnimalTargetBusy()   { return false; } // VIBE_CharAction_IsAnimalTargetBusy
    virtual int  RivalOfficialCount() { return 0; }     // distinct rivals attached
    virtual int  FreeHandlerCount()   { return 0; }     // handlers not matching a rival
    virtual int  CapacityByte()       { return 0; }     // building record byte +583
};

enum class BriberyResult {
    CantBribeHere, // target invalid OR not busy -> message 5791, menu not opened
    TooMany,       // total >= 2*capacity -> message 5641
    MenuOpened,    // gate passed -> the bribe menu opens
};

// Run the bribery gate (the head of 0x512a6c, up to the form open).  Returns which of the
// three outcomes the original takes; on MenuOpened the modal menu would open.
BriberyResult Bribery_Gate(BriberyDeps& deps);

// The capacity comparison used by the gate: total officials must stay under 2*capacity.
// 1:1 with `if (v37 + v5 >= 2 * capacity_byte)`.
inline bool Bribery_OverCapacity(int total, int capacityByte) {
    return total >= 2 * capacityByte;
}

// ===========================================================================
// VIBE_Location_TradeTransport  0x513568  (Handel\Handel_Transport)
// __usercall(eax=obj, ebx=...)
//
// Pure rule cores:
//   * the panel only opens when *obj == 71 (the building's type byte gate).
//   * import/export toggle: a state flag `dir` (1 = export branch, 0 = import).  When the
//     active row toggles, the panel rebuilds: export uses object subtype 475 and rich
//     string 0x1854; import uses subtype 476 and rich string 0x1855.  Clicking the export
//     row sets dir=1, the import row sets dir=0.
//   * the slot enable bits: for the two toggle rows (export row v25, import row v26) the
//     "selected" row gets enable=dir and (==dir) and the other gets the complement.  The
//     two writes at +56/+76 are: export-row = dir, import-row = (dir==0).
//   * a smooth bar fill: factor = min(elapsedTicks * dbl_621918, 0.25) clamped, i.e.
//     `f = elapsed * rate; if (f >= cap) f = 0.25 else f = f`.  Stored as a float at
//     [+229] of the panel record.  (dbl_621918 = the per-tick rate, dbl_621920 = cap.)
// ===========================================================================
constexpr int  kTransportTypeByte    = 71;    // *obj gate
constexpr int  kTransportSubExport   = 475;   // QueryFind subtype, export branch
constexpr int  kTransportSubImport   = 476;   // QueryFind subtype, import branch
constexpr u32  kTransportTextExport  = 0x1854;// rich-string id, export
constexpr u32  kTransportTextImport  = 0x1855;// rich-string id, import

// dbl_621918 / dbl_621920 — the bar-fill rate and clamp.  Recovered as the IEEE-754
// doubles below (see .cpp for the exact 8-byte values from get_bytes).
extern const double kTransportBarRate; // dbl_621918
extern const double kTransportBarCap;  // dbl_621920 (== 0.25 source constant)

// The bar-fill computation (1:1 with the v20/v21/v22 block of 0x513568).
// elapsed = current_tick - start_tick.  Returns the clamped fill factor.
f32 Transport_BarFill(int elapsedTicks);

// Which (subtype, text id) the panel rebuild uses for a direction.  dir!=0 -> export.
struct TransportRebuild { int subtype; u32 textId; };
TransportRebuild Transport_RebuildFor(int dir);

// The two row-enable bits written on rebuild.  Returns {exportRowEnable, importRowEnable}.
// 1:1 with the +56/+76 writes: export-row = dir, import-row = (dir == 0).
struct TransportRowEnable { int exportRow; int importRow; };
TransportRowEnable Transport_RowEnable(int dir);

// ===========================================================================
// VIBE_Location_TradeSearchExport 0x513c60  (HANDEL\HANDEL_SUCHEN)
// VIBE_Location_TradeSearchImport 0x5142ec  (HANDEL\HANDEL_SUCHEN)
// __usercall(eax=obj, edx=arg)
//
// The genuine kernel both share is a HANDLER AGGREGATION by good-type.  Three good-type
// ids are fixed per direction:
//   export: dword_507F90 = {452, 453, 454}   (0x1C4..0x1C6)
//   import: dword_507F9C = {449, 450, 451}   (0x1C1..0x1C3)
// For each matching handler (FindFirstHandlerByFilter(2,0,reason,3,playerKey), reason=20
// for export, 21 for import) the original reads the handler's good-type from its +43 dword
// (>> 16) and, if it equals one of the three good-type ids at index i, accumulates the
// handler's byte at +172 into a per-good total[i].  The accumulate uses the compiler's
// `LOBYTE(x ^= ...)` idiom which is *exactly* `total[i] += handler_byte172` (the XOR is on
// the matched-equal values so the high bytes cancel; only the low byte — the +172 byte —
// survives).  Export differs from import only in: the slot-count cap (export caps the row
// count at `kExportRowCap = (typeByte==8 || capByte<2) ? 2 : 3`; import is fixed 3 rows)
// and the message ids (export 5134/5135, import 5142/5143; "%s" for the zero case).
//
// The message-id selection is the only branchy per-row decision and is translated as a
// predicate: amount==1 -> "one" id; amount>1 -> "many" id; amount<=0 (==0) -> bare "%s".
// ===========================================================================

// gilde.exe good-type id triples.
constexpr int kSearchExportGoods[3] = {452, 453, 454}; // dword_507F90 {0x1C4,0x1C5,0x1C6}
constexpr int kSearchImportGoods[3] = {449, 450, 451}; // dword_507F9C {0x1C1,0x1C2,0x1C3}

// Filter "reason" arg to FindFirstHandlerByFilter (2,0,reason,3,key).
constexpr int kSearchReasonExport = 20;
constexpr int kSearchReasonImport = 21;

// Per-row message ids.
constexpr int kExportMsgOne  = 5134; // amount == 1
constexpr int kExportMsgMany = 5135; // amount  > 1
constexpr int kImportMsgOne  = 5142;
constexpr int kImportMsgMany = 5143;

// The export row-count gate: typeByte (building +0) == 8, OR capacity byte (+583) < 2 -> 2
// rows; otherwise 3.  Import is always 3.  1:1 with the v70 assignment of 0x513c60.
int Search_ExportRowCount(int buildingTypeByte, int capacityByte);
constexpr int kSearchImportRowCount = 3;

// One handler observation: its good-type id and the +172 byte it contributes.
struct SearchHandler { int goodType; int weightByte; };

// Aggregate handlers into per-good totals over a 3-good-type id table.  Returns the three
// totals in `out[0..2]`.  1:1 with both functions' aggregation loop (the LOBYTE-XOR
// accumulate reduces to `total[i] += weightByte` for the matched index).
void Search_Aggregate(const int goodTypes[3], const SearchHandler* handlers, int count,
                      int out[3]);

// Per-row message id (export or import variant).  amount==1 -> msgOne, >1 -> msgMany,
// otherwise 0 (the bare "%s" path, no count).  Returns 0 for the no-count case.
int Search_RowMessageId(int amount, int msgOne, int msgMany);

// ===========================================================================
// VIBE_Location_ResidenceMistress  0x515524  (locations\wohnsitz\geliebte)
// __usercall(eax=obj)
//
// Pure rule cores:
//   * Candidate gather: iterate handlers (FindFirstHandlerByFilter(1,0,111)); for each
//     whose key (+43 dword) matches the active player's slot key (dword_12CE914[+536*slot]),
//     record up to 16 candidates: the candidate's person key (+1 dword), an "enabled"
//     flag = (handler +28 dword == 0), and the candidate's id (+44 dword).
//   * The "take a mistress" action (ChildObjectId) is ENABLED only when fewer than 4
//     mistresses are recorded: `enabled = (count < 4)`.  1:1 with the v10<4 branch.
//   * Header rich-string: gender flag byte dword_12CE919[+134*slot] != 0 -> id 0x165E,
//     else 0x165D (the "your mistress(es)" vs "your lover(s)" header).
//   * On confirming "take a mistress": a slot-reset command with handler 111 is queued.
// ===========================================================================
constexpr int kMistressHandlerOp   = 111;   // FindFirstHandlerByFilter / command opcode
constexpr int kMistressMaxRecorded = 16;    // candidate array cap
constexpr int kMistressTakeLimit   = 4;     // "take a mistress" enabled while count < 4
constexpr u32 kMistressHdrMale     = 0x165E;// gender flag set
constexpr u32 kMistressHdrFemale   = 0x165D;// gender flag clear
constexpr int kMistressGenderByteStride = 134; // dword_12CE919 stride

// "Take a mistress" button enabled?  1:1 with `count < 4 ? enabled : disabled`.
inline bool Mistress_TakeEnabled(int recordedCount) {
    return recordedCount < kMistressTakeLimit;
}

// Header rich-string id by gender flag.  nonzero flag -> male header (0x165E).
inline u32 Mistress_HeaderText(int genderFlag) {
    return genderFlag ? kMistressHdrMale : kMistressHdrFemale;
}

// ===========================================================================
// VIBE_Location_TavernStammtischJoin  0x517a58  (locations\Wirtshaus\Stammtisch)
// VIBE_Location_TavernStammtischLeave 0x517724  (locations\Wirtshaus\Stammtisch)
//
// Shared person-card geometry (the regulars'-table layout, identical in both):
//   width      w = window2.width - 2 * cardWidth        (62D298+6>>16 minus 2*62D204-relative)
//   colWidth   cw = w / 3
//   center     c  = (w % 3) / 2
//   for member i (0-based):
//     x = c + cw*(i%2 + 1) + 10*(i%2 - 1) + cardWidth*(i%2)
//     y = 130 * (i/2) + 100
//   (cardWidth is the +117762 window's width>>16; modelled as a parameter.)
//
// JOIN eligibility state machine (v38), evaluated each frame after the member gather:
//   start state = 1 (can join).
//   if (memberCount == 4 && !playerAtTable)            -> state 2 (table full).
//   else if (memberCount < 4 && !playerAtTable):
//        scan all players' shops (QueryFind subtype 301): if any seated person is the
//        active player's key -> state 4 (already a member elsewhere); else stays 3
//        (an opening exists, "ask to join").
//   the join command (op "stammtisch join H-Plr") is only queued in state 3 on confirm;
//   state 1 with the leave hotkey queues "stammtisch leave H-Plr".
//   playerAtTable = any seated member's person key == active player's key (set during gather).
//
// LEAVE is the same gather/geometry; its only action is the leave command on hotkey 37
// when memberCount>0, plus the hotkey-48 "swap to other building" toggle.
// ===========================================================================
constexpr int kStammtischSeats     = 4;     // table holds up to 4 regulars
constexpr int kStammtischCardYStep = 130;   // y = 130*(i/2) + 100
constexpr int kStammtischCardYBase = 100;
constexpr int kStammtischColXOff   = 10;    // the 10*(i%2 - 1) term
constexpr int kHotkeyLeave         = 37;    // byte_67225C — leave / action hotkey
constexpr int kHotkeySwapBuilding  = 48;    // byte_67225C — toggle to twin building
constexpr int kStammtischShopSub   = 301;   // QueryFind subtype scanned in Join

// Join state values (the v38 state machine of 0x517a58).
enum class StammtischState {
    CanJoin = 1,        // an opening + not seated -> "ask to join"
    Full = 2,           // 4 seated and player not among them -> table full
    AlreadyMember = 3,  // <4 seated, not here, and not a member elsewhere -> join offer
    MemberElsewhere = 4 // the player already sits at another building's table
};

// One regulars'-table card's geometry (x,y) — the only out-of-pixel-buffer thing here.
struct StammtischCard { int x; int y; };

// Compute the i-th card's geometry.  `windowWidth` is the win-2 inner width (62D298+6>>16),
// `cardWidth` is the per-card width (62D204+117762>>16).  1:1 with the layout block shared
// by both functions.
StammtischCard Stammtisch_CardGeometry(int i, int windowWidth, int cardWidth);

// Compute the JOIN state.  `memberCount` is seated regulars (0..4), `playerAtTable` is true
// when one seated member is the active player, `memberElsewhere` is true when the scan of
// other shops found the player seated at another table.  1:1 with the v38 assignments.
StammtischState Stammtisch_JoinState(int memberCount, bool playerAtTable, bool memberElsewhere);

// ===========================================================================
// VIBE_Location_TavernDarkCornerBrowse  0x51816c  (locations\Wirtshaus\DUNKLEECKE)
// __usercall(eax=obj, edx=arg2, ebx=informants)
//
// Three informant slots are scored each frame: for each of three informant ids (ebx[0..2])
// VIBE_He_ComputeEntityScore(id, playerRecord, &spriteId, &price, scratch) returns nonzero
// when the slot is filled.  The pure decision logic recovered:
//   * a slot's previous good-type cache (v38[i]); when it changes the panel is rebuilt.
//   * filled slot: store sprite & price; row text id 5300 with arg
//        (5 * (someByte>>24) + 4145, price).
//   * empty slot (was != -1, now -1): clear to (sprite=0, type=-1, price=0); row text 5301.
//   * the "hire" button (ChildObjectId) is OFFERED only when the player can afford it:
//     the gate is `qword_13CE852 (player wealth) > informants_record+40 (the asking price)`.
//     If a hire-block handler matches (FindFirstHandlerByFilter(1,0,116) keyed to player)
//     the panel instead shows the "already hired / blocked" text 0x14B7.
//   * on confirm a slot-reset command with handler 116 is queued.
// ===========================================================================
constexpr int kDarkCornerHandlerOp = 116;   // FindFirstHandlerByFilter / command opcode
constexpr int kDarkCornerSlots     = 3;
constexpr int kDarkCornerMsgFilled = 5300;  // row text, slot filled
constexpr int kDarkCornerMsgEmpty  = 5301;  // row text, slot empty
constexpr u32 kDarkCornerTextBlocked = 0x14B7; // "already hired" (handler matched)
constexpr u32 kDarkCornerTextHire    = 0x14B6; // the hire button (affordable)
constexpr int kDarkCornerRowTextBase = 4145;   // 5*(byte>>24) + 4145

// Hire button offered?  Offered only when no block-handler matched AND the player can
// afford it.  1:1 with: blocked -> text 0x14B7; else if (wealth > price) -> hire button.
// Returns: 0 = no offer (too poor), 1 = hire button shown, 2 = blocked.
enum class DarkCornerOffer { TooPoor = 0, HireButton = 1, Blocked = 2 };
DarkCornerOffer DarkCorner_HireOffer(bool blockHandlerMatched, long long playerWealth, int askingPrice);

// The row text base used for a filled slot's label arg: 5 * (statusByte >> 24) + 4145.
inline int DarkCorner_RowTextArg(int statusByte) {
    return 5 * (statusByte >> 24) + kDarkCornerRowTextBase;
}

} // namespace loc3
} // namespace guild::world
