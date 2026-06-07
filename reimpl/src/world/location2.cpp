#include "world/location2.h"

namespace guild::world {

namespace {
int dlg(LocationDialog d) { return static_cast<int>(d); }
} // namespace

// gilde.exe 0x5239c0 — VIBE_Location_ChurchContactLoop.
// Original per-frame registration (then a matching else-if dispatch):
//   if (word_631758 & 0x200) {                         // shop open
//       v4 = Register("contact_PREDIGT", 22);          // sermon
//       v5 = Register("ob_WEIHWASSERBECKEN", 22);      // baptism
//   }
//   v7 = NPC+39;
//   if (v7 != word_63CC5C && v7 != -1) {               // foreign priest
//       v3 = QueryFind(...242) ? Register("ob_KIRCHE_BEICHTSTUHLRAUM",22) : 0;
//       v8 = Register("ob_SPENDENKAESTCHEN", 22);       // donation
//       v5 = Register("ob_WEIHWASSERBECKEN", 22);       // baptism (re-bound)
//   }
// We emit the items in registration order with the same gates; ContactDispatch
// then resolves a clicked slot to the dialog target exactly like the else-if.
std::vector<ContactMenuItem> ChurchContactMenu(bool shopOpen,
                                               bool npcServesOtherCity,
                                               bool confessionRoomPresent) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"contact_PREDIGT",     dlg(LocationDialog::ChurchSermon),  {}});
        items.push_back({"ob_WEIHWASSERBECKEN", dlg(LocationDialog::ChurchBaptism), {}});
    }
    if (npcServesOtherCity) {
        if (confessionRoomPresent)
            items.push_back({"ob_KIRCHE_BEICHTSTUHLRAUM", dlg(LocationDialog::ChurchConfession), {}});
        items.push_back({"ob_SPENDENKAESTCHEN", dlg(LocationDialog::ChurchDonation), {}});
        items.push_back({"ob_WEIHWASSERBECKEN", dlg(LocationDialog::ChurchBaptism),  {}});
    }
    return items;
}

// gilde.exe 0x521608 — VIBE_Location_ChurchGuildContactLoop.
//   if (VIBE_Amt_GetGuildEligibility(city office)) v1 = Register("contact_ZUNFTLEVEL1",22);
//   if (v1 == dword_631720) VIBE_Guild_ShowLevel1MenuDialog();
std::vector<ContactMenuItem> ChurchGuildContactMenu(bool guildEligible) {
    std::vector<ContactMenuItem> items;
    if (guildEligible)
        items.push_back({"contact_ZUNFTLEVEL1", dlg(LocationDialog::ChurchGuildLevel1), {}});
    return items;
}

// gilde.exe 0x522d70 — VIBE_Location_ChurchConfessionContactLoop.
//   if ((word_631758 & 0x200) == 0) v1 = Register("contact_BEICHTE",22);  // shop CLOSED
//   if (... && (word_631758 & 0x200) == 0 && v1 == clicked) ChurchConfessionDialog();
std::vector<ContactMenuItem> ChurchConfessionContactMenu(bool shopOpen) {
    std::vector<ContactMenuItem> items;
    if (!shopOpen)
        items.push_back({"contact_BEICHTE", dlg(LocationDialog::ChurchConfession), {}});
    return items;
}

// gilde.exe 0x523b64 — VIBE_Location_ChurchTop5ContactLoop.
//   Register("contact_TOP5",22); v4 = Register("contact_ABLASS",22);
//   dispatch: TOP5 -> Top5Dialog, ABLASS -> IndulgenceDialog.
std::vector<ContactMenuItem> ChurchTop5ContactMenu() {
    return {
        {"contact_TOP5",   dlg(LocationDialog::ChurchTop5),       {}},
        {"contact_ABLASS", dlg(LocationDialog::ChurchIndulgence), {}},
    };
}

// gilde.exe 0x524058 / 0x5252b0 — Idle loops: ResetEntries then spin; no items.
std::vector<ContactMenuItem> IdleContactMenu() {
    return {};
}

// gilde.exe 0x523cd0 — VIBE_Location_DungeonPlanContactLoop.
//   v3 = Register("ob_KERKERPLAN",22); if (v3 == clicked) DungeonPlanDialog();
std::vector<ContactMenuItem> DungeonPlanContactMenu() {
    return {{"ob_KERKERPLAN", dlg(LocationDialog::DungeonPlan), {}}};
}

// gilde.exe 0x523f8c — VIBE_Location_DungeonBribeContactLoop.
//   if (byte_12CEAC1[536*city]) v2 = Register("contact_BESTECHEN",16);
//   dispatch guarded by the same flag + NPC+39 != -1 -> DungeonBribeDialog.
std::vector<ContactMenuItem> DungeonBribeContactMenu(bool bribeAvailable) {
    std::vector<ContactMenuItem> items;
    if (bribeAvailable)
        items.push_back({"contact_BESTECHEN", dlg(LocationDialog::DungeonBribe), {}});
    return items;
}

// gilde.exe 0x5252cc — VIBE_Location_EquipStorageContactLoop.
// Registration order: (shop) AUSRUESTEN, REGENERATION ; (back) LAGER, TRANSPORT.
//   v2 = Register("contact_AUSRUESTEN",13);   // equip   (shop 0x200)
//   v3 = Register("contact_REGENERATION",23); // training(shop 0x200)
//   v1 = Register("contact_LAGER",14);        // storage (back 0x400)
//   v5 = Register("contact_TRANSPORT",21);    // transport(back 0x400)
// Dispatch resolves storage/transport/equip/training; ContactDispatch matches by
// slot id, so item->target order is independent of the dispatch source order.
std::vector<ContactMenuItem> EquipStorageContactMenu(bool shopOpen, bool backRoom) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"contact_AUSRUESTEN",   dlg(LocationDialog::ThievesGuildEquipment), {}});
        items.push_back({"contact_REGENERATION", dlg(LocationDialog::TrainingSelect),        {}});
    }
    if (backRoom) {
        items.push_back({"contact_LAGER",     dlg(LocationDialog::Storage),   {}});
        items.push_back({"contact_TRANSPORT", dlg(LocationDialog::Transport), {}});
    }
    return items;
}

// gilde.exe 0x5254c8 — VIBE_Location_TargetNightContactLoop.
//   if (shop) { v0 = Register("ob_ZIELSCHEIBE",12); v1 = Register("contact_NACHT",12); }
//   if (shop && clicked && (v0==clicked || v1==clicked)) RunTrainingSelect();
// Both items open the same panel.
std::vector<ContactMenuItem> TargetNightContactMenu(bool shopOpen) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"ob_ZIELSCHEIBE", dlg(LocationDialog::TrainingSelect), {}});
        items.push_back({"contact_NACHT",  dlg(LocationDialog::TrainingSelect), {}});
    }
    return items;
}

// gilde.exe 0x526e30 — VIBE_Location_GuardContactLoop.
//   if (shop) {
//     v9  = Register("contact_PATROL",22);    // patrol start
//     v2  = Register("ob_WACHPLAN",22);       // customs start
//     v4  = Register("ob_PERSONALBUCH",12);   // staff book
//     v10 = Register("ob_MEISTERBRIEF",22);   // master certificate
//     if (QueryFind(...327)) v3 = Register("ob_ZOLLKASSE",22);            // detain
//     else if (QueryFind(...328)) v3 = Register("ob_ZOLLKASSE_MIT_GEHEIMFACH",22); // detain
//   }
// Dispatch: staff/patrol/customs/detain/master-cert.
std::vector<ContactMenuItem> GuardContactMenu(bool shopOpen, int customsBox) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"contact_PATROL",   dlg(LocationDialog::GuardPatrolStart),  {}});
        items.push_back({"ob_WACHPLAN",      dlg(LocationDialog::GuardCustomsStart), {}});
        items.push_back({"ob_PERSONALBUCH",  dlg(LocationDialog::StaffBook),         {}});
        items.push_back({"ob_MEISTERBRIEF",  dlg(LocationDialog::MasterCertificate), {}});
        if (customsBox == 327)
            items.push_back({"ob_ZOLLKASSE", dlg(LocationDialog::GuardDetainStart),  {}});
        else if (customsBox == 328)
            items.push_back({"ob_ZOLLKASSE_MIT_GEHEIMFACH", dlg(LocationDialog::GuardDetainStart), {}});
    }
    return items;
}

// gilde.exe 0x526fc0 — VIBE_Location_GuardRaidContactLoop.
//   if (shop) {
//     v5 = Register("contact_LAGER",14);                 // storage
//     v6 = Register("contact_AUSRUESTEN",13);            // equip
//     v1 = Register("contact_RAZZIA",22);                // raid start
//     v2 = Register("contact_GEBAEUDE_AUSSPIONIEREN",23);// spy building start
//     v3 = Register("ub_INFORMATIONSPERGAMENT",12);      // information dialog
//     v0 = Register("contact_TRANSPORT",21);             // transport
//   }
std::vector<ContactMenuItem> GuardRaidContactMenu(bool shopOpen) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"contact_LAGER",                  dlg(LocationDialog::Storage),               {}});
        items.push_back({"contact_AUSRUESTEN",             dlg(LocationDialog::ThievesGuildEquipment), {}});
        items.push_back({"contact_RAZZIA",                 dlg(LocationDialog::GuardRaidStart),        {}});
        items.push_back({"contact_GEBAEUDE_AUSSPIONIEREN", dlg(LocationDialog::SpyBuildingStart),      {}});
        items.push_back({"ub_INFORMATIONSPERGAMENT",       dlg(LocationDialog::InformationDialog),     {}});
        items.push_back({"contact_TRANSPORT",              dlg(LocationDialog::Transport),             {}});
    }
    return items;
}

// gilde.exe 0x527120 — VIBE_Location_GuardTargetContactLoop.
//   if (shop) { v0=Register("ob_ZIELSCHEIBE",12); v1=Register("contact_NACHT",12);
//               v2=Register("contact_REGENERATION",23); }
//   click on any -> RunTrainingSelect().
std::vector<ContactMenuItem> GuardTargetContactMenu(bool shopOpen) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"ob_ZIELSCHEIBE",       dlg(LocationDialog::TrainingSelect), {}});
        items.push_back({"contact_NACHT",        dlg(LocationDialog::TrainingSelect), {}});
        items.push_back({"contact_REGENERATION", dlg(LocationDialog::TrainingSelect), {}});
    }
    return items;
}

// gilde.exe 0x5261bc — VIBE_Location_ThiefPrisonContactLoop.
//   if (shop) { v12=Register("contact_ENTFUEHREN",..); v14=Register("contact_LOESEGELD",..); }
//   if (hostage) {
//       a2 = hostage.player;
//       if (a2 == dword_12CE914[134*city]) {           // our prisoner
//           if (hostage+433) v13 = Register("contact_AUSBRECHEN",..);
//       }
//   }
//   dispatch: ENTFUEHREN->KidnapStart, LOESEGELD->RansomDialog, AUSBRECHEN->BreakoutDialog.
std::vector<ContactMenuItem> ThiefPrisonContactMenu(bool shopOpen,
                                                    bool hostageIsOurPrisoner,
                                                    bool breakoutFlag) {
    std::vector<ContactMenuItem> items;
    if (shopOpen) {
        items.push_back({"contact_ENTFUEHREN", dlg(LocationDialog::ThiefKidnapStart), {}});
        items.push_back({"contact_LOESEGELD",  dlg(LocationDialog::ThiefRansom),      {}});
    }
    if (hostageIsOurPrisoner && breakoutFlag)
        items.push_back({"contact_AUSBRECHEN", dlg(LocationDialog::ThiefBreakout), {}});
    return items;
}

// ===========================================================================
// Guard-action start gates. Common shape (e.g. 0x526554):
//   v1 = VIBE_Dialog_CheckActiveCharFlag();
//   if (!v1) { ...build config..., VIBE_MapView_PanelDispatcher(mode,&cfg,Dialog,global,0);
//              PlayerBar_Create/Destroy; Selection_ClearAll(); }
// When the active-char flag is set, the whole body is skipped (panel never opens).
// ===========================================================================

// gilde.exe 0x526554 — VIBE_Location_GuardPatrolStart.
//   PanelDispatcher(1, &cfg{v4[1]=1024,...}, GuardArrestDialog, dword_8C9084, 0).
GuardStartResult GuardPatrolStart(bool activeCharFlag) {
    GuardStartResult r;
    r.opened = !activeCharFlag;
    r.action.dispatchMode = 1;
    r.action.dialog = LocationDialog::GuardPatrolStart;  // opens via GuardArrestDialog
    r.action.hasConfigBlob = true;
    r.action.configFlag = 1024;  // v4[1] = 1024
    return r;
}

// gilde.exe 0x526824 — VIBE_Location_GuardRaidStart.
//   PanelDispatcher(1, &cfg{v6=1024,v7=65552,v8=7,v9=1689}, GuardRaidDialog, dword_8C9094, 0).
GuardStartResult GuardRaidStart(bool activeCharFlag) {
    GuardStartResult r;
    r.opened = !activeCharFlag;
    r.action.dispatchMode = 1;
    r.action.dialog = LocationDialog::GuardRaidStart;    // GuardRaidDialog
    r.action.hasConfigBlob = true;
    r.action.configFlag = 1024;  // v6 = 1024
    return r;
}

// gilde.exe 0x526af0 — VIBE_Location_GuardCustomsStart.
//   PanelDispatcher(4, 0, GuardCustomsDialog, dword_8C90A0, 0).  No config blob.
GuardStartResult GuardCustomsStart(bool activeCharFlag) {
    GuardStartResult r;
    r.opened = !activeCharFlag;
    r.action.dispatchMode = 4;
    r.action.dialog = LocationDialog::GuardCustomsStart; // GuardCustomsDialog
    r.action.hasConfigBlob = false;
    r.action.configFlag = 0;
    return r;
}

// gilde.exe 0x526db8 — VIBE_Location_GuardDetainStart.
//   cfg{v4[0]=1, v5=-1, v6=&unk_744300, v7=6, v8=1689};
//   PanelDispatcher(1, &cfg, GuardDetainDialog, dword_8C90AC, 0).
GuardStartResult GuardDetainStart(bool activeCharFlag) {
    GuardStartResult r;
    r.opened = !activeCharFlag;
    r.action.dispatchMode = 1;
    r.action.dialog = LocationDialog::GuardDetainStart;  // GuardDetainDialog
    r.action.hasConfigBlob = true;
    r.action.configFlag = 1;  // v4[0] = 1
    return r;
}

} // namespace guild::world
