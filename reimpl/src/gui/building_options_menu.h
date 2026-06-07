#pragma once
// guild::gui — the building OPTIONS MENU (the building "hub" dialog).
//
//   VIBE_BuildingDialog_OptionsMenu @gilde.exe 0x54c608 (0xfaa bytes) — the full
//     building-action hub. Form = "Panel\gebaeude_optionen_pergament". It opens a
//     menu of action buttons over a building (sell / renovate / upgrade / expand /
//     buy / demolish / extinguish / tech-effects / rename / ...), each gated on the
//     building's state, then dispatches the clicked button into the matching
//     sub-dialog (VIBE_BuildingDialog_*). RunFrameLoop selector = 415687.
//
//   VIBE_BuildingDialog_OptionsMenuAlt @gilde.exe 0x54d5b4 — a trimmed variant of the
//     same hub (same form) that builds only the renovate / open-upgrade-window /
//     expand-room / upgrade buttons. RunFrameLoop selector = 423879.
//
// Each menu entry is a RenderRichString line whose child-object id is fetched with
// VIBE_Form_GetChildObjectId; the per-frame loop compares dword_62D22C (the clicked
// object) against each fetched id and, when it matches and the right-button flag
// (word_63C740 & 0x80) is clear, hides the hub (Form_SetObjectsVisible(form,0)),
// runs the sub-dialog, then re-shows the hub. We recover the text-id -> action map
// and the per-entry visibility gates byte-for-byte, and expose a builder that returns
// the active entry set for a synthetic building, plus a dispatcher that maps a clicked
// entry to its action (sub-dialog) enum.
//
// The frame loop, text/glyph engine and command codec live in other clusters and are
// forward-declared; the sub-dialog builders are in building_dialog.{h,cpp} and
// building_upgrade_dialog.{h,cpp}.

#include "gui/building_dialog.h"

namespace guild::gui {

// Recovered .form resource name (shared by both hub variants).
inline constexpr const char* kFormOptionsPergament =
    "Panel\\gebaeude_optionen_pergament";

// Recovered RunFrameLoop selectors.
//   OptionsMenu uses the panel selector (415687); OptionsMenuAlt the confirm one.
inline constexpr int kLoopFormOptionsMenu    = 415687;
inline constexpr int kLoopFormOptionsMenuAlt = 423879;

// ---------------------------------------------------------------------------
// The actions a hub entry dispatches to. Each names the sub-dialog / command the
// original routes to from the per-frame button match.
// ---------------------------------------------------------------------------
enum class BuildingMenuAction {
    kNone = 0,
    kOpenUpgradeWindow,  // VIBE_Building_OpenUpgradeWindow(building,253,...)   (text 5055)
    kSellPreview,        // VIBE_BuildingDialog_SellPreview                     (text 5047)
    kRenovate,           // VIBE_BuildingDialog_Renovate                        (text 5048)
    kSellLand,           // VIBE_BuildingDialog_SellLand                        (text 5056)
    kConfirmSell,        // VIBE_BuildingDialog_ConfirmSell                     (text 5050)
    kExpandRoom,         // VIBE_BuildingDialog_ExpandRoom                      (text 5051)
    kUpgrade,            // VIBE_BuildingDialog_Upgrade                         (text 5054)
    kExtinguishFire,     // VIBE_BuildingDialog_ExtinguishFire                  (text 5059)
    kTechEffects,        // VIBE_BuildingDialog_ShowTechEffects                 (text 5046/tech)
    kDemolishRoom,       // 5110 demolish-room command bracket                  (text 5058)
    kTearDown,           // 5082 messagebox -> Reset28 building tear-down        (text 5049)
    kRequestPair57,      // VIBE_Command_QueueRequestPair57 (link parent)       (text 5052)
    kBuyBuilding,        // VIBE_Building_EnqueueBuyBuilding gate                (text 5053)
    kRequestQuad56,      // VIBE_Command_QueueRequestQuad56                     (text 6068)
    kRenameField,        // edit-name field (Object_SetButtonCallback 16)       (text 5044)
};

// ---------------------------------------------------------------------------
// Recovered text ids for the hub entries (RenderRichString arg).
// ---------------------------------------------------------------------------
inline constexpr int kMenuTextUpgradeTitleA = 5043; // upgrade-level title block (variant A)
inline constexpr int kMenuTextUpgradeTitleB = 5041; // upgrade-level title block (variant B)
inline constexpr int kMenuTextHandlerLine   = 5045; // in-progress handler line (when *flag==1)
inline constexpr int kMenuTextRename        = 5044; // rename edit field
inline constexpr int kMenuTextSellPreview   = 5047; // -> SellPreview
inline constexpr int kMenuTextRenovate      = 5048; // -> Renovate
inline constexpr int kMenuTextTearDown      = 5049; // -> tear-down (messagebox 5082)
inline constexpr int kMenuTextConfirmSell   = 5050; // -> ConfirmSell
inline constexpr int kMenuTextExpandRoom    = 5051; // -> ExpandRoom
inline constexpr int kMenuTextRequestPair   = 5052; // -> QueueRequestPair57
inline constexpr int kMenuTextBuyBuilding   = 5053; // -> buy-building gate
inline constexpr int kMenuTextUpgrade       = 5054; // -> Upgrade
inline constexpr int kMenuTextOpenUpgradeWin = 5055; // -> OpenUpgradeWindow
inline constexpr int kMenuTextSellLand      = 5056; // -> SellLand
inline constexpr int kMenuTextDemolishRoom  = 5058; // -> demolish-room (5110)
inline constexpr int kMenuTextExtinguish    = 5059; // -> ExtinguishFire
inline constexpr int kMenuTextRequestQuad   = 6068; // -> QueueRequestQuad56

// ---------------------------------------------------------------------------
// Synthetic building state for the hub (the gate inputs the original reads off the
// building record at building+90/+91/+101.., the type word at +37/+39, and globals).
// ---------------------------------------------------------------------------
struct BuildingMenuState {
    int  handle = 0;          // building+1 entity id
    u8   category = 0;        // VIBE_Building_MapTypeToCategory(*building)  (v101)
    int  flag90 = 0;          // building[90] bitfield (0x02 occupied,0x04 disabled,
                              //   0x08/0x10 no-renovate, 0x18 mask, 0x40 enable-toggle)
    int  flag91 = 0;          // building[91] bitfield (0x02 sell-land disable,0x04 abort)
    int  stateByte = 0;       // *v97 = building-type-record byte (1 in-progress,2,3,16)
    int  roomCount = 0;       // v97[33] room count (>1 enables ExpandRoom)
    bool isOwn = false;       // building+37 word == own faction (word_63CC5C)
    bool isCity = false;      // building+39 word == own faction (city/plot building)
    bool isProduction = false;// VIBE_Building_IsProductionType(building)
    bool hasBuyHandler = false;// FirstHandlerByFilter (a pending buy handler) found (v6)
    bool hasParentHandler = false; // v87 (a pending link handler) found
    bool debugBuild = false;  // global dword_63C7B8 (debug/test build flag)
};

// One active hub entry — a fetched child-object id bound to an action + its text id.
struct BuildingMenuEntry {
    int objId = -1;                 // VIBE_Form_GetChildObjectId result
    int textId = 0;                 // the RenderRichString id
    BuildingMenuAction action = BuildingMenuAction::kNone;
    bool enabled = true;            // VIBE_Object_SetEnabled(obj,0) gates
};

inline constexpr int kMenuMaxEntries = 16;

struct BuildingMenuLayout {
    const char* form = nullptr;
    int loopForm = 0;
    int displayWorth = 0;           // ComputeRoomWorth(building) (the title cost)
    int entryCount = 0;
    BuildingMenuEntry entries[kMenuMaxEntries];
};

// ---------------------------------------------------------------------------
// Builders (DATA/LAYOUT) — return the active entry set for a synthetic building.
// ---------------------------------------------------------------------------

// gilde.exe 0x54c608 — VIBE_BuildingDialog_OptionsMenu.
BuildingMenuLayout BuildingOptionsMenu_Build(const BuildingMenuState& s, int roomWorth);

// gilde.exe 0x54d5b4 — VIBE_BuildingDialog_OptionsMenuAlt (trimmed hub).
BuildingMenuLayout BuildingOptionsMenu_BuildAlt(const BuildingMenuState& s, int roomWorth);

// ---------------------------------------------------------------------------
// Dispatch (WIRING) — map a clicked object to its action. `clickedId` is the live
// dword_75BF38 (-1 == no click); `clickedObj` is dword_62D22C. Returns the action of
// the matched, enabled entry (kNone if nothing matched).
// ---------------------------------------------------------------------------
BuildingMenuAction BuildingOptionsMenu_Dispatch(const BuildingMenuLayout& l,
                                                int clickedId, int clickedObj);

}  // namespace guild::gui
