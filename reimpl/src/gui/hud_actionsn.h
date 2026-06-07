#pragma once
// guild::gui — hud_actionsn: a P6 (Wave 29) coverage slice closing genuinely-DEFERRED
// deterministic leaves of the VIBE_Hud_* family, translated 1:1 from gilde.exe.
// All verified absent from src/ + include/ (address AND VIBE_ name) before work.
//
//   0x50ff00  VIBE_Hud_RunMeisterPersonalBookLoop  meister-brief + personnel-book modal
//                                                  loop: register two status entries,
//                                                  dispatch the clicked one.
//   0x50ff7c  VIBE_Hud_RunSoeldnerBookLoop         mercenary-book + weapon-rack modal
//                                                  loop (gated on a hud-flag bit 0x200).
//   0x50ee60  VIBE_Hud_UpdateSelectedObjectContact 3D-overlay contact updater: classifies
//                                                  the selected object and enters its
//                                                  interior / registers a trade contact.
//
// The runtime/world leaves the originals call (frame loop, status-text register, the
// building/scene/trade engine) are routed through an installable HudActionsNHooks
// struct with inert defaults in hud_actionsn.cpp. The GUI-OWNED deterministic cores —
// the status-entry-id dispatch and the object-type classifier — are reproduced
// byte-for-byte and exposed as pure helpers so they are golden-vector testable.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Selected-object / clicked-entry state (originals: dword_631720, dword_631724,
// word_631758 hud flags). Modeled module-local + settable for tests.
// ---------------------------------------------------------------------------
extern int g_clickedStatusId;   // dword_631720 — the status-text id last clicked
extern int g_selectedObject;    // dword_631724 / dword_631748 — selected object record
extern std::uint16_t g_hudFlags;// word_631758 — hud mode flag bits

// Object-type byte classification (read at object record +0).
//   30, 31, 32 -> a trade-contact building (Einkauf contact registered).
inline constexpr int kObjTypeTradeLo = 30;
inline constexpr int kObjTypeTradeHi = 32;
// Contact-class byte (object class table +0) gating interior entry: 2 or 6.
inline constexpr int kContactClassA = 2;
inline constexpr int kContactClassB = 6;
// Soeldner-book loop gate bit in word_631758.
inline constexpr std::uint16_t kHudFlagSoeldnerBook = 0x200;

// gilde.exe 0x50f01c region — pure object-type -> "is trade contact" classifier.
// True when the object's type byte is in [30,32]. (The original guards on a non-null
// object record first.)
bool Hud_IsTradeContactType(int typeByte);

// gilde.exe 0x50ee84 region — contact-class gate: a building interior is entered when
// the class byte is 2 or 6.
bool Hud_IsEnterableContactClass(int classByte);

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults.
// ===========================================================================
struct HudActionsNHooks {
    // --- modal loop runtime ------------------------------------------------
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* p); // VIBE_GameLogic_RunFrameLoop @0x4c09a0
    void (*statusTextResetEntries)();                           // VIBE_StatusText_ResetEntries @0x4bcc4c
    int  (*statusTextRegister)(const char* key, int tag);       // VIBE_StatusText_Register @0x4bcc80

    // --- meister/personnel book leaves -------------------------------------
    void (*meisterRunMasterCertificate)();                      // VIBE_Meister_RunMasterCertificateDialog @0x558e58
    void (*personnelRunStaffBook)();                            // VIBE_Personnel_RunStaffBook @0x53bccc
    void (*personnelRunMercenaryBook)();                        // VIBE_Personnel_RunMercenaryBook @0x53dc30
    void (*panelRunThievesGuildEquipment)();                    // VIBE_Panel_RunThievesGuildEquipment @0x550310

    // --- contact-updater world leaves --------------------------------------
    void (*buildingEnterScriptedLocation)(int objRecord, int a2); // VIBE_Building_EnterScriptedLocation @0x51ed98
    void (*sceneEnterBuildingInterior)(int objRecord);          // VIBE_Scene_EnterBuildingInterior @0x5066b8
    void (*sceneSetupBuildingAmbience)(int objRecord);          // VIBE_Scene_SetupBuildingAmbience @0x5069c0
    void (*tradeRegisterEinkaufContact)(int objRecord);         // VIBE_Trade_RegisterEinkaufContact @...
};

const HudActionsNHooks* SetHudActionsNHooks(const HudActionsNHooks* hooks);
const HudActionsNHooks* HudActionsNHooks_Default();
const HudActionsNHooks& HudActionsNHooksActive();
void ResetHudActionsN();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// gilde.exe 0x50ff00 — VIBE_Hud_RunMeisterPersonalBookLoop (a1@ebx).
//   StatusText_ResetEntries();
//   while (RunFrameLoop(425983, a1, 12)) {
//     v2 = StatusText_Register("ob_MEISTERBRIEF", 12);
//     v3 = StatusText_Register("ob_PERSONALBUCH", 12);
//     if (g_clickedStatusId) {
//       if (v2 == g_clickedStatusId) Meister_RunMasterCertificate();
//       else if (v3 == g_clickedStatusId) Personnel_RunStaffBook();
//     }
//   }
//   return result;
int Hud_RunMeisterPersonalBookLoop(int a1);

// gilde.exe 0x50ff7c — VIBE_Hud_RunSoeldnerBookLoop.
//   StatusText_ResetEntries(); v5=v6=0;
//   while (RunFrameLoop(425983, v6, "ob_SOELDNERBUCH")) {
//     if (g_hudFlags & 0x200) {
//       v5 = StatusText_Register("ob_SOELDNERBUCH", 12);
//       v6 = StatusText_Register("ub_STAENDER_WAFFEN", 13);
//     }
//     if (g_clickedStatusId) {
//       if (v5 == g_clickedStatusId) Personnel_RunMercenaryBook();
//       else if (v6 == g_clickedStatusId) Panel_RunThievesGuildEquipment();
//     }
//   }
void Hud_RunSoeldnerBookLoop();

// gilde.exe 0x50ee60 — VIBE_Hud_UpdateSelectedObjectContact (a1@ebx).
// The deterministic dispatcher: for the selected object record `g_selectedObject`,
// when its type byte is a trade contact (30/31/32) registers an Einkauf trade
// contact; the building/scene interior entry leaves are routed through hooks.
// Simplified to the GUI-owned classification + the trade-contact branch (the
// scene/building gating depends on world handles outside this slice and is hooked).
void Hud_UpdateSelectedObjectContact(int a1);

} // namespace guild::gui
