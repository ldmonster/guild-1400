#pragma once
// Location interaction FSMs, batch 2 — the per-building/per-NPC contact-menu
// loops and the guard-action "start" gates recovered from gilde.exe
// VIBE_Location_*. Each contact loop is the same small frame-loop FSM already
// abstracted in world/location.{h,cpp}: ResetEntries, then every frame
// (re)register a gated set of menu items and, when one is clicked
// (dword_631720), dispatch to its dialog. This file recovers the *concrete
// menus* (strings, icon widths, gates, dispatch targets, registration order)
// for the loops not covered by location.cpp, plus the guard-action start gates.
//
// Functions recovered here (address -> what is modeled):
//   VIBE_Location_ChurchContactLoop          0x5239c0  (ChurchContactMenu)
//   VIBE_Location_ChurchGuildContactLoop     0x521608  (ChurchGuildContactMenu)
//   VIBE_Location_ChurchConfessionContactLoop0x522d70  (ChurchConfessionContactMenu)
//   VIBE_Location_ChurchTop5ContactLoop      0x523b64  (ChurchTop5ContactMenu)
//   VIBE_Location_IdleContactLoop            0x524058  (IdleContactMenu)
//   VIBE_Location_IdleContactLoopAlt         0x5252b0  (IdleContactMenu, byte-id twin)
//   VIBE_Location_DungeonPlanContactLoop     0x523cd0  (DungeonPlanContactMenu)
//   VIBE_Location_DungeonBribeContactLoop    0x523f8c  (DungeonBribeContactMenu)
//   VIBE_Location_EquipStorageContactLoop    0x5252cc  (EquipStorageContactMenu)
//   VIBE_Location_TargetNightContactLoop     0x5254c8  (TargetNightContactMenu)
//   VIBE_Location_GuardContactLoop           0x526e30  (GuardContactMenu)
//   VIBE_Location_GuardRaidContactLoop       0x526fc0  (GuardRaidContactMenu)
//   VIBE_Location_GuardTargetContactLoop     0x527120  (GuardTargetContactMenu)
//   VIBE_Location_ThiefPrisonContactLoop     0x5261bc  (ThiefPrisonContactMenu)
//   VIBE_Location_GuardPatrolStart           0x526554  (GuardActionStartDecision)
//   VIBE_Location_GuardRaidStart             0x526824  (GuardActionStartDecision)
//   VIBE_Location_GuardCustomsStart          0x526af0  (GuardActionStartDecision)
//   VIBE_Location_GuardDetainStart           0x526db8  (GuardActionStartDecision)
#include <vector>

#include "guild/common/types.h"
#include "world/location.h"

namespace guild::world {

// ===========================================================================
// Dispatch target ids. Each contact loop's else-if chain calls a specific
// dialog/start function; we tag every menu item with the integer id of the
// dialog it opens so a test can verify a click resolves to the right target.
// (location.cpp already defines ProductionTarget / ThiefGuildTarget.)
// ===========================================================================
enum class LocationDialog {
    None = 0,
    // Church family (0x5239c0 / 0x521608 / 0x522d70 / 0x523b64)
    ChurchSermon,           // VIBE_Location_ChurchSermonDialog       0x522dd8
    ChurchBaptism,          // VIBE_Location_ChurchBaptismDialog      0x5220c4
    ChurchConfession,       // VIBE_Location_ChurchConfessionDialog   0x522ae8
    ChurchDonation,         // VIBE_Location_ChurchDonationDialog     0x521674
    ChurchGuildLevel1,      // VIBE_Guild_ShowLevel1MenuDialog        0x520ba8
    ChurchTop5,             // VIBE_Location_ChurchTop5Dialog         0x522780
    ChurchIndulgence,       // VIBE_Location_ChurchIndulgenceDialog   0x521bac
    // Dungeon (0x523cd0 / 0x523f8c)
    DungeonPlan,            // VIBE_Location_DungeonPlanDialog        0x523bdc
    DungeonBribe,           // VIBE_Location_DungeonBribeDialog       0x523d1c
    // Equipment storage (0x5252cc)
    Storage,                // VIBE_StorageDialog_Options             0x5461b0
    Transport,              // VIBE_TradeTransport_OpenPanelMode1     0x54011c
    ThievesGuildEquipment,  // VIBE_Panel_RunThievesGuildEquipment    0x550310
    TrainingSelect,         // VIBE_Panel_RunTrainingSelect           0x550adc
    // Guard (0x526e30 / 0x526fc0 / 0x527120)
    StaffBook,              // VIBE_Personnel_RunStaffBook            0x53bccc
    MasterCertificate,      // VIBE_Meister_RunMasterCertificateDialog0x558e58
    GuardPatrolStart,       // VIBE_Location_GuardPatrolStart         0x526554
    GuardCustomsStart,      // VIBE_Location_GuardCustomsStart        0x526af0
    GuardDetainStart,       // VIBE_Location_GuardDetainStart         0x526db8
    GuardRaidStart,         // VIBE_Location_GuardRaidStart           0x526824
    SpyBuildingStart,       // VIBE_Location_ThiefSpyBuildingStart    0x524740
    InformationDialog,      // VIBE_Location_ThiefInformationDialog   0x52481c
    // Thief prison (0x5261bc)
    ThiefKidnapStart,       // VIBE_Location_ThiefKidnapStart         0x5258bc
    ThiefRansom,            // VIBE_Location_ThiefRansomDialog        0x5259f8
    ThiefBreakout,          // VIBE_Location_ThiefBreakoutDialog      0x525e94
};

// ===========================================================================
// Per-loop inputs. The originals read these from globals/NPC fields each frame:
//   word_631758  -> panel flags (kPanelShopOpen 0x200, kPanelBackRoom 0x400)
//   NPC+39 (i16) -> NPC city id; compared against word_63CC5C (current city)
//   word_63CC5C  -> current city id
// We pass them as plain parameters so the menu builder is pure and testable.
// ===========================================================================

// gilde.exe 0x5239c0 — VIBE_Location_ChurchContactLoop.
// Two gated groups, registered (and dispatched) in this order:
//   if shopOpen (0x200):  contact_PREDIGT (sermon), ob_WEIHWASSERBECKEN (baptism)
//   if npc is a "foreign" priest (NPC+39 != current city AND != -1):
//        [if confession room object present] ob_KIRCHE_BEICHTSTUHLRAUM (confession),
//        ob_SPENDENKAESTCHEN (donation), ob_WEIHWASSERBECKEN (baptism)
// `npcServesOtherCity` == (NPC+39 != word_63CC5C && NPC+39 != -1).
// `confessionRoomPresent` == VIBE_GameObject_QueryFind(...,242) != 0.
std::vector<ContactMenuItem> ChurchContactMenu(bool shopOpen,
                                               bool npcServesOtherCity,
                                               bool confessionRoomPresent);

// gilde.exe 0x521608 — VIBE_Location_ChurchGuildContactLoop.
// One item, gated by guild eligibility for the current city's office record:
//   if VIBE_Amt_GetGuildEligibility(city office): contact_ZUNFTLEVEL1 -> level-1 menu
std::vector<ContactMenuItem> ChurchGuildContactMenu(bool guildEligible);

// gilde.exe 0x522d70 — VIBE_Location_ChurchConfessionContactLoop.
// One item, offered only when the shop is CLOSED ((word_631758 & 0x200) == 0):
//   contact_BEICHTE -> confession dialog
std::vector<ContactMenuItem> ChurchConfessionContactMenu(bool shopOpen);

// gilde.exe 0x523b64 — VIBE_Location_ChurchTop5ContactLoop.
// Two items always offered: contact_TOP5 (top5), contact_ABLASS (indulgence).
std::vector<ContactMenuItem> ChurchTop5ContactMenu();

// gilde.exe 0x524058 / 0x5252b0 — Idle contact loops. No items ever registered
// (ResetEntries then spin RunFrameLoop). Returns an empty menu.
std::vector<ContactMenuItem> IdleContactMenu();

// gilde.exe 0x523cd0 — VIBE_Location_DungeonPlanContactLoop.
// One item always offered: ob_KERKERPLAN -> dungeon plan dialog.
std::vector<ContactMenuItem> DungeonPlanContactMenu();

// gilde.exe 0x523f8c — VIBE_Location_DungeonBribeContactLoop.
// One item, gated on the current city's "bribe available" flag
// (byte_12CEAC1 stride 536): contact_BESTECHEN -> bribe dialog (icon width 16).
std::vector<ContactMenuItem> DungeonBribeContactMenu(bool bribeAvailable);

// gilde.exe 0x5252cc — VIBE_Location_EquipStorageContactLoop.
// shopOpen (0x200): contact_AUSRUESTEN (equip, w13), contact_REGENERATION (training, w23)
// backRoom (0x400): contact_LAGER (storage, w14),   contact_TRANSPORT (transport, w21)
// Dispatch order in the original: storage, transport, equip, training.
std::vector<ContactMenuItem> EquipStorageContactMenu(bool shopOpen, bool backRoom);

// gilde.exe 0x5254c8 — VIBE_Location_TargetNightContactLoop.
// shopOpen (0x200): ob_ZIELSCHEIBE (w12), contact_NACHT (w12); both dispatch to
// the training-select panel (a click on EITHER opens it).
std::vector<ContactMenuItem> TargetNightContactMenu(bool shopOpen);

// gilde.exe 0x526e30 — VIBE_Location_GuardContactLoop.
// shopOpen (0x200), registration order:
//   contact_PATROL (patrol start, w22), ob_WACHPLAN (customs start, w22),
//   ob_PERSONALBUCH (staff book, w12), ob_MEISTERBRIEF (master cert, w22),
//   then ONE customs-box item gated by which box object is present:
//     if QueryFind(...327): ob_ZOLLKASSE                -> detain start (w22)
//     else if QueryFind(...328): ob_ZOLLKASSE_MIT_GEHEIMFACH -> detain start (w22)
// `customsBox`: 0 none, 327 plain, 328 secret-compartment.
std::vector<ContactMenuItem> GuardContactMenu(bool shopOpen, int customsBox);

// gilde.exe 0x526fc0 — VIBE_Location_GuardRaidContactLoop.
// shopOpen (0x200), registration order:
//   contact_LAGER (storage, w14), contact_AUSRUESTEN (equip, w13),
//   contact_RAZZIA (raid start, w22), contact_GEBAEUDE_AUSSPIONIEREN (spy, w23),
//   ub_INFORMATIONSPERGAMENT (information, w12), contact_TRANSPORT (transport, w21)
std::vector<ContactMenuItem> GuardRaidContactMenu(bool shopOpen);

// gilde.exe 0x527120 — VIBE_Location_GuardTargetContactLoop.
// shopOpen (0x200): ob_ZIELSCHEIBE (w12), contact_NACHT (w12),
// contact_REGENERATION (w23); a click on ANY opens the training-select panel.
std::vector<ContactMenuItem> GuardTargetContactMenu(bool shopOpen);

// gilde.exe 0x5261bc — VIBE_Location_ThiefPrisonContactLoop.
// shopOpen (0x200): contact_ENTFUEHREN (kidnap start), contact_LOESEGELD (ransom);
// plus contact_AUSBRECHEN (breakout) when the held prisoner belongs to the
// current city's player AND the prisoner's +433 "can break out" flag is set.
// `hostageIsOurPrisoner` == (hostage.player == current-city player) ;
// `breakoutFlag` == hostage+433 != 0.
std::vector<ContactMenuItem> ThiefPrisonContactMenu(bool shopOpen,
                                                    bool hostageIsOurPrisoner,
                                                    bool breakoutFlag);

// ===========================================================================
// Guard-action start gates (0x526554 / 0x526824 / 0x526af0 / 0x526db8).
// All four share the shape:
//   if (!VIBE_Dialog_CheckActiveCharFlag()) { build a MapView panel config and
//      VIBE_MapView_PanelDispatcher(mode, &cfg, <Dialog>, <panelGlobal>, 0); }
// The recovered, byte-identical pieces are: the active-char-flag gate, the
// PanelDispatcher mode, and the dialog that gets dispatched. The panel config
// blobs differ per action; the bytes that matter for behavior are captured.
// ===========================================================================
struct GuardActionStart {
    int  dispatchMode = 0;            // VIBE_MapView_PanelDispatcher arg1
    LocationDialog dialog = LocationDialog::None; // dispatched dialog
    bool hasConfigBlob = false;       // builds a v4/v5 panel-config struct
    int  configFlag = 0;              // the leading config dword (1024 / -1 ...)
};

// `activeCharFlag` == VIBE_Dialog_CheckActiveCharFlag() != 0. When true the
// action is suppressed (the panel never opens) and `opened` is false.
struct GuardStartResult {
    bool opened = false;              // did the panel open (gate passed)?
    GuardActionStart action;          // the panel/dialog that WOULD open
};

GuardStartResult GuardPatrolStart(bool activeCharFlag);   // 0x526554
GuardStartResult GuardRaidStart(bool activeCharFlag);     // 0x526824
GuardStartResult GuardCustomsStart(bool activeCharFlag);  // 0x526af0
GuardStartResult GuardDetainStart(bool activeCharFlag);   // 0x526db8

} // namespace guild::world
