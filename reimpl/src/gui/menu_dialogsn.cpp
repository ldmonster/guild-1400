// guild::gui — menu_dialogsn implementation (P6 / Wave 29 GUI coverage slice).
// 1:1 reconstructions of deferred VIBE_Menu_ChoosePlayerCount / ShowMissionWarning.

#include "gui/menu_dialogsn.h"

#include <cstdint>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Form names + format strings (verbatim from the decompiles' string refs).
// ---------------------------------------------------------------------------
namespace {
const char* const kFormChoosePlayerCount = "Menu\\CHOOSE_PLAYERCOUNT"; // aMenuChoosePlay
const char* const kFormGetMissionWarning = "Menu\\GET_MISSION_WARNING"; // aMenuGetMission
} // namespace

// ---------------------------------------------------------------------------
// Module-owned state.
// ---------------------------------------------------------------------------
std::uint8_t g_playerCount = 0;     // byte_63CC1D
int g_lastMenuAction = 0;           // dword_75BF38
std::uint8_t g_missionTypeSeed = 0; // byte_63C8F4

// dword_631614 — transition/quit latch (modeled locally per house convention).
static int g_menuQuitLatch = 0;

// ===========================================================================
// Pure deterministic cores.
// ===========================================================================
std::uint8_t Menu_ClampPlayerCount(std::uint8_t current) {
    // Original: if (byte_63CC1D >= 2) v4 = byte_63CC1D; else v4 = 2; (unsigned compare)
    return current >= kMinPlayerCount ? current : kMinPlayerCount;
}

bool Menu_ShouldShowMissionWarning(std::uint8_t playerCount) {
    return playerCount > 1u;   // (unsigned __int8)byte_63CC1D > 1u
}

// ===========================================================================
// Hooks (inert defaults). Frame loop returns 0 -> the modal exits immediately.
// ===========================================================================
namespace {

int  DefGameTickFinalize(i16, i16, const char*) { return -1; }
int  DefFormCenterChildWindows(int) { return 0; }
int  DefFormGetChildObjectId(int, int, int) { return -1; }
void DefObjectSetValueOrText(int, int, int, int) {}
void DefDragCursorSetSprite(int, int) {}
int  DefFormSelectWindow(int, int) { return 1; }
int  DefTextRenderRichString(unsigned, unsigned) { return 0; }
int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; }
int  DefObjectGetDataPtr(int) { return 0; }
void DefFormSetObjectsVisible(int, int) {}
void DefFormDestroy(int) {}
int  DefReadMouseRelease() { return 0; }
int  DefMissionPickRandomByType(std::uint8_t) { return 0; }
void DefMissionRunFailureDialog(int, int) {}

const MenuDialogsNHooks kDefaultHooks = {
    &DefGameTickFinalize, &DefFormCenterChildWindows, &DefFormGetChildObjectId,
    &DefObjectSetValueOrText, &DefDragCursorSetSprite, &DefFormSelectWindow,
    &DefTextRenderRichString, &DefGameLogicRunFrameLoop, &DefObjectGetDataPtr,
    &DefFormSetObjectsVisible, &DefFormDestroy, &DefReadMouseRelease,
    &DefMissionPickRandomByType, &DefMissionRunFailureDialog,
};

const MenuDialogsNHooks* g_hooks = &kDefaultHooks;

} // namespace

const MenuDialogsNHooks* SetMenuDialogsNHooks(const MenuDialogsNHooks* hooks) {
    const MenuDialogsNHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const MenuDialogsNHooks* MenuDialogsNHooks_Default() { return &kDefaultHooks; }
const MenuDialogsNHooks& MenuDialogsNHooksActive() { return *g_hooks; }

void ResetMenuDialogsN() {
    g_playerCount = 0;
    g_lastMenuAction = 0;
    g_missionTypeSeed = 0;
    g_menuQuitLatch = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x528c84 — VIBE_Menu_ChoosePlayerCount.
// ===========================================================================
int Menu_ChoosePlayerCount() {
    const MenuDialogsNHooks& h = MenuDialogsNHooksActive();
    int form = h.gameTickFinalize(0, 0, kFormChoosePlayerCount);
    h.formCenterChildWindows(form);
    int childObj = h.formGetChildObjectId(form, 0, 0);
    int okPressed = 0;

    g_playerCount = Menu_ClampPlayerCount(g_playerCount);
    // VIBE_Object_SetValueOrText(childObj, 2, 8, count): seed the spinner range/value.
    h.objectSetValueOrText(childObj, 2, 8, g_playerCount);
    h.dragCursorSetSprite(0, 0);
    h.formSelectWindow(form, 1);
    h.textRenderRichString(0x16C2u, 0);
    h.formSelectWindow(form, 2);
    h.textRenderRichString(0x7Du, 0x7Eu); // "%s$A%s", 0x7D, 0x7E

    while (h.gameLogicRunFrameLoop(147591, childObj, reinterpret_cast<const void*>(1))) {
        if (h.readMouseRelease())
            g_menuQuitLatch = 1;
        if (g_lastMenuAction == kActionCancel)
            g_menuQuitLatch = 1;
        if (g_lastMenuAction == kActionOk) {
            g_playerCount = static_cast<std::uint8_t>(h.objectGetDataPtr(childObj));
            okPressed = 1;
            h.formSetObjectsVisible(form, 0);
            g_menuQuitLatch = 1;
        }
    }
    if (form != -1)
        h.formDestroy(form);
    return okPressed;
}

// ===========================================================================
// 0x52c6c0 — VIBE_Menu_ShowMissionWarning.
// ===========================================================================
int Menu_ShowMissionWarning() {
    const MenuDialogsNHooks& h = MenuDialogsNHooksActive();
    int mission = h.missionPickRandomByType(g_missionTypeSeed);

    if (Menu_ShouldShowMissionWarning(g_playerCount)) {
        int form = h.gameTickFinalize(0, 0, kFormGetMissionWarning);
        h.formCenterChildWindows(form);
        h.formSelectWindow(form, 0);
        h.textRenderRichString(0x16E9u, 0);
        h.textRenderRichString(0x16EAu, 0);
        while (h.gameLogicRunFrameLoop(198, 0, reinterpret_cast<const void*>(1))) {
            if (g_lastMenuAction == kActionOk)
                g_menuQuitLatch = 1;
            if (g_lastMenuAction == kActionCancel)
                g_menuQuitLatch = 1;
            if (h.readMouseRelease())
                g_menuQuitLatch = 1;
        }
        if (form != -1)
            h.formDestroy(form);
    }

    if (mission) {
        h.missionRunFailureDialog(mission, 198);
        return mission; // dword_122F4EC = v4 (the failure-dialog result); modeled as the
                        // mission id (the original returns the same al value it stored).
    }
    return 0;
}

} // namespace guild::gui
