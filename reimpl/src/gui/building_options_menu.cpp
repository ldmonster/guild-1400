#include "gui/building_options_menu.h"

#include <initializer_list>

namespace guild::gui {

namespace {

// Child-object id base. As in building_dialog.cpp, only the relative offsets of the
// fetched ids matter for the wiring; we synthesize a deterministic base so each
// entry gets a stable, distinct object id for the dispatch tests.
constexpr int kMenuObjBase = 3000;

// Append an active entry (with a distinct object id) and bind it to an action.
void AddEntry(BuildingMenuLayout& l, int textId, BuildingMenuAction action,
              bool enabled = true) {
    if (l.entryCount >= kMenuMaxEntries)
        return;
    BuildingMenuEntry& e = l.entries[l.entryCount];
    e.objId = kMenuObjBase + l.entryCount;  // GetChildObjectId result (stable)
    e.textId = textId;
    e.action = action;
    e.enabled = enabled;
    ++l.entryCount;
}

// Find the entry bound to an action (to apply a late SetEnabled gate).
BuildingMenuEntry* FindByAction(BuildingMenuLayout& l, BuildingMenuAction a) {
    for (int i = 0; i < l.entryCount; ++i)
        if (l.entries[i].action == a)
            return &l.entries[i];
    return nullptr;
}

}  // namespace

// ===========================================================================
// OptionsMenu — DATA/LAYOUT.
// ===========================================================================
//
// gilde.exe 0x54c608 — VIBE_BuildingDialog_OptionsMenu.
//   form = "Panel\gebaeude_optionen_pergament"; SelectWindow(form,1).
//   The entry set is rebuilt each time the building changes; the gates below are
//   transcribed from the rebuild block (v3 != 0). cat = MapTypeToCategory; *v97 is
//   the building-type-record state byte; v97[33] the room count.
BuildingMenuLayout BuildingOptionsMenu_Build(const BuildingMenuState& s, int roomWorth) {
    BuildingMenuLayout l{};
    l.form = kFormOptionsPergament;
    l.loopForm = kLoopFormOptionsMenu;  // RunFrameLoop(415687)
    l.displayWorth = roomWorth;         // ComputeRoomWorth(building)

    const bool ownOrCity = s.isOwn || s.isCity;

    // --- Block A: foreign building (neither own nor city). ------------------
    //   if (!isCity && !isOwn): RenderRichString(5043 title); when cat!=3 && cat!=5
    //   && *flag!=1: a buyHandler -> 5053 (buy gate); else if (!isProduction ||
    //   *flag==16) -> 5056 (sell-land), disabled when flag91&2.
    if (!s.isCity && !s.isOwn) {
        if (s.category != 3 && s.category != 5 && s.stateByte != 1) {
            if (s.hasBuyHandler) {
                AddEntry(l, kMenuTextBuyBuilding, BuildingMenuAction::kBuyBuilding);
            } else if (!s.isProduction || s.stateByte == 16) {
                // SetEnabled(v80, 0) when (building[91] & 2) -> sell-land disabled.
                bool enabled = (s.flag91 & 2) == 0;
                AddEntry(l, kMenuTextSellLand, BuildingMenuAction::kSellLand, enabled);
            }
        }
    }

    // --- Block B: own building OR city/plot building. -----------------------
    if (ownOrCity) {
        if (s.isOwn) {
            // 5044 rename edit field (Object_SetButtonCallback 16).
            AddEntry(l, kMenuTextRename, BuildingMenuAction::kRenameField);
        }
        // Sell row: cat==2 -> demolish-room (5058); flag90&2 -> confirm-sell (5050);
        //           else -> sell-preview (5047).
        if (s.category == 2) {
            AddEntry(l, kMenuTextDemolishRoom, BuildingMenuAction::kDemolishRoom);
        } else if ((s.flag90 & 2) != 0) {
            AddEntry(l, kMenuTextConfirmSell, BuildingMenuAction::kConfirmSell);
        } else {
            AddEntry(l, kMenuTextSellPreview, BuildingMenuAction::kSellPreview);
        }
        // Renovate (5048) is always present in block B.
        AddEntry(l, kMenuTextRenovate, BuildingMenuAction::kRenovate);
        // Tear-down (5049): only when (flag90 & 0x18)==0 && cat!=2 && *flag!=16.
        if ((s.flag90 & 0x18) == 0 && s.category != 2 && s.stateByte != 16) {
            AddEntry(l, kMenuTextTearDown, BuildingMenuAction::kTearDown);
        }
        // Open upgrade window (5055): when *flag != 3.
        if (s.stateByte != 3) {
            AddEntry(l, kMenuTextOpenUpgradeWin, BuildingMenuAction::kOpenUpgradeWindow);
        }
        // Expand room (5051): when room count (v97[33]) > 1.
        if (s.roomCount > 1) {
            AddEntry(l, kMenuTextExpandRoom, BuildingMenuAction::kExpandRoom);
        }
        // Upgrade (5054): when cat != 2.
        if (s.category != 2) {
            AddEntry(l, kMenuTextUpgrade, BuildingMenuAction::kUpgrade);
        }
        // Extinguish fire (5059): when a parent/abort handler (v87) is pending.
        if (s.hasParentHandler) {
            AddEntry(l, kMenuTextExtinguish, BuildingMenuAction::kExtinguishFire);
        }
    }

    // --- City link entry: when isCity and *flag==2 (and not the player root). ---
    //   RenderRichString(5052) -> QueueRequestPair57.
    if (s.isCity && s.stateByte == 2) {
        AddEntry(l, kMenuTextRequestPair, BuildingMenuAction::kRequestPair57);
    }

    // --- Debug/test build entry: dword_63C7B8 -> 6068 QueueRequestQuad56. ---
    if (s.debugBuild) {
        AddEntry(l, kMenuTextRequestQuad, BuildingMenuAction::kRequestQuad56);
    }

    // Late SetEnabled gates: building[90] & 4 disables the sell/renovate/teardown/
    // sell-preview group; building[90] & 0x40 disables the upgrade entry.
    if ((s.flag90 & 4) != 0) {
        for (BuildingMenuAction a : {BuildingMenuAction::kSellPreview,
                                     BuildingMenuAction::kConfirmSell,
                                     BuildingMenuAction::kRenovate,
                                     BuildingMenuAction::kTearDown,
                                     BuildingMenuAction::kSellLand}) {
            if (BuildingMenuEntry* e = FindByAction(l, a))
                e->enabled = false;
        }
    }
    if (BuildingMenuEntry* up = FindByAction(l, BuildingMenuAction::kUpgrade)) {
        // (flag90 & 0x40) -> SetEnabled(v95, 0); else SetEnabled(v95, 1).
        up->enabled = (s.flag90 & 0x40) == 0;
    }
    return l;
}

// ===========================================================================
// OptionsMenuAlt — DATA/LAYOUT (the trimmed hub).
// ===========================================================================
//
// gilde.exe 0x54d5b4 — VIBE_BuildingDialog_OptionsMenuAlt.
//   Same form. Renders 5043 (foreign title) / 5041 (own title) then builds:
//     ChildObjectId <- 5048 (Renovate);
//     if (*flag != 3)  v41 <- 5055 (OpenUpgradeWindow);
//     if (v39[33] > 1) v36 <- 5051 (ExpandRoom);
//     v3 <- 5054 (Upgrade).
//   Late gates: building[90]&4 disables OpenUpgradeWindow + Renovate; building[90]&0x40
//   disables Upgrade (toggle).
BuildingMenuLayout BuildingOptionsMenu_BuildAlt(const BuildingMenuState& s, int roomWorth) {
    BuildingMenuLayout l{};
    l.form = kFormOptionsPergament;
    l.loopForm = kLoopFormOptionsMenuAlt;  // RunFrameLoop(423879)
    l.displayWorth = roomWorth;

    AddEntry(l, kMenuTextRenovate, BuildingMenuAction::kRenovate);  // ChildObjectId
    if (s.stateByte != 3) {
        AddEntry(l, kMenuTextOpenUpgradeWin, BuildingMenuAction::kOpenUpgradeWindow);
    }
    if (s.roomCount > 1) {
        AddEntry(l, kMenuTextExpandRoom, BuildingMenuAction::kExpandRoom);
    }
    AddEntry(l, kMenuTextUpgrade, BuildingMenuAction::kUpgrade);

    if ((s.flag90 & 4) != 0) {
        if (BuildingMenuEntry* e = FindByAction(l, BuildingMenuAction::kOpenUpgradeWindow))
            e->enabled = false;
        if (BuildingMenuEntry* e = FindByAction(l, BuildingMenuAction::kRenovate))
            e->enabled = false;
    }
    if (BuildingMenuEntry* up = FindByAction(l, BuildingMenuAction::kUpgrade)) {
        up->enabled = (s.flag90 & 0x40) == 0;
    }
    return l;
}

// ===========================================================================
// Dispatch — WIRING.
// ===========================================================================
//
// The per-frame loop ignores clicks while no widget is hit (dword_75BF38 == -1) and
// suppresses the sub-dialogs when the right-button flag (word_63C740 & 0x80) is set;
// here `clickedId` carries the live last-click id (-1 == none) and the right-button
// suppression is left to the caller (matching how building_dialog wires 1155).
BuildingMenuAction BuildingOptionsMenu_Dispatch(const BuildingMenuLayout& l,
                                                int clickedId, int clickedObj) {
    if (clickedId == -1)
        return BuildingMenuAction::kNone;
    for (int i = 0; i < l.entryCount; ++i) {
        const BuildingMenuEntry& e = l.entries[i];
        if (e.objId != clickedObj)
            continue;
        if (!e.enabled)
            return BuildingMenuAction::kNone;  // disabled object swallows the click
        return e.action;
    }
    return BuildingMenuAction::kNone;
}

}  // namespace guild::gui
