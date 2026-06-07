// guild::gui — hud_actionsn implementation (P6 / Wave 29 GUI coverage slice).
// 1:1 reconstructions of deferred VIBE_Hud_* modal-loop + contact-update leaves.

#include "gui/hud_actionsn.h"

#include <cstdint>
#include <cstring>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Original status-text key strings (verbatim from the decompiles' string refs).
// ---------------------------------------------------------------------------
namespace {
const char* const kObMeisterbrief = "ob_MEISTERBRIEF";    // aObMeisterbrief
const char* const kObPersonalbuch = "ob_PERSONALBUCH";    // aObPersonalbuch
const char* const kObSoeldnerbuch = "ob_SOELDNERBUCH";    // aObSoeldnerbuch
const char* const kUbStaenderWaff = "ub_STAENDER_WAFFEN"; // aUbStaenderWaff

constexpr int kFrameLoopMode = 425983; // RunFrameLoop arg the book loops pass
} // namespace

// ---------------------------------------------------------------------------
// Module-owned state.
// ---------------------------------------------------------------------------
int g_clickedStatusId = 0;   // dword_631720
int g_selectedObject  = 0;   // dword_631724 / dword_631748
std::uint16_t g_hudFlags = 0;// word_631758

// ===========================================================================
// Pure deterministic classifiers.
// ===========================================================================
bool Hud_IsTradeContactType(int typeByte) {
    int t = typeByte & 0xFF;
    return t == 32 || t == 31 || t == 30;   // exactly the original's 3-way compare
}

bool Hud_IsEnterableContactClass(int classByte) {
    int c = classByte & 0xFF;
    return c == kContactClassB || c == kContactClassA; // 6 || 2
}

// ===========================================================================
// Hooks (inert defaults). Frame loops terminate immediately; the sibling
// dialogs/world leaves are observable but inert.
// ===========================================================================
namespace {

int  DefRunFrameLoop(int, int, const void*) { return 0; } // exit loop at once
void DefStatusTextResetEntries() {}
int  DefStatusTextRegister(const char*, int) { return 0; }

void DefMeisterRunMasterCertificate() {}
void DefPersonnelRunStaffBook() {}
void DefPersonnelRunMercenaryBook() {}
void DefPanelRunThievesGuildEquipment() {}

void DefBuildingEnterScriptedLocation(int, int) {}
void DefSceneEnterBuildingInterior(int) {}
void DefSceneSetupBuildingAmbience(int) {}
void DefTradeRegisterEinkaufContact(int) {}

const HudActionsNHooks kDefaultHooks = {
    &DefRunFrameLoop, &DefStatusTextResetEntries, &DefStatusTextRegister,
    &DefMeisterRunMasterCertificate, &DefPersonnelRunStaffBook,
    &DefPersonnelRunMercenaryBook, &DefPanelRunThievesGuildEquipment,
    &DefBuildingEnterScriptedLocation, &DefSceneEnterBuildingInterior,
    &DefSceneSetupBuildingAmbience, &DefTradeRegisterEinkaufContact,
};

const HudActionsNHooks* g_hooks = &kDefaultHooks;

} // namespace

const HudActionsNHooks* SetHudActionsNHooks(const HudActionsNHooks* hooks) {
    const HudActionsNHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const HudActionsNHooks* HudActionsNHooks_Default() { return &kDefaultHooks; }
const HudActionsNHooks& HudActionsNHooksActive() { return *g_hooks; }

void ResetHudActionsN() {
    g_clickedStatusId = 0;
    g_selectedObject = 0;
    g_hudFlags = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x50ff00 — VIBE_Hud_RunMeisterPersonalBookLoop.
// ===========================================================================
int Hud_RunMeisterPersonalBookLoop(int a1) {
    const HudActionsNHooks& h = HudActionsNHooksActive();
    h.statusTextResetEntries();
    int result;
    while ((result = h.gameLogicRunFrameLoop(kFrameLoopMode, a1, reinterpret_cast<const void*>(12))) != 0) {
        int v2 = h.statusTextRegister(kObMeisterbrief, 12);
        a1 = 12; // the original reloads ebx with 12 between the two registers
        int v3 = h.statusTextRegister(kObPersonalbuch, 12);
        if (g_clickedStatusId) {
            if (v2 == g_clickedStatusId)
                h.meisterRunMasterCertificate();
            else if (v3 == g_clickedStatusId)
                h.personnelRunStaffBook();
        }
    }
    return result;
}

// ===========================================================================
// 0x50ff7c — VIBE_Hud_RunSoeldnerBookLoop.
// ===========================================================================
void Hud_RunSoeldnerBookLoop() {
    const HudActionsNHooks& h = HudActionsNHooksActive();
    h.statusTextResetEntries();
    int v5 = 0, v6 = 0;
    while (h.gameLogicRunFrameLoop(kFrameLoopMode, v6, kObSoeldnerbuch)) {
        if ((g_hudFlags & kHudFlagSoeldnerBook) != 0) {
            v5 = h.statusTextRegister(kObSoeldnerbuch, 12);
            v6 = h.statusTextRegister(kUbStaenderWaff, 13);
        }
        if (g_clickedStatusId) {
            if (v5 == g_clickedStatusId)
                h.personnelRunMercenaryBook();
            else if (v6 == g_clickedStatusId)
                h.panelRunThievesGuildEquipment();
        }
    }
}

// ===========================================================================
// 0x50ee60 — VIBE_Hud_UpdateSelectedObjectContact (GUI-owned slice).
// The original branches on several world handles (dword_11BC2F0, dword_11BC27C,
// dword_631738) to enter a building interior; those handles live outside this slice
// and are routed through the building/scene hooks. The deterministic GUI-owned tail
// classifies the selected object's type byte and, when it is a trade contact
// (30/31/32), registers an Einkauf contact. We faithfully reproduce that tail.
// ===========================================================================
void Hud_UpdateSelectedObjectContact(int a1) {
    (void)a1;
    const HudActionsNHooks& h = HudActionsNHooksActive();
    if (g_selectedObject == 0)
        return;
    // type byte == object record +0; modeled as the low byte of the handle so tests
    // can drive it deterministically (the real read is *(_BYTE*)dword_631748).
    int typeByte = g_selectedObject & 0xFF;
    if (Hud_IsTradeContactType(typeByte))
        h.tradeRegisterEinkaufContact(g_selectedObject);
}

} // namespace guild::gui
