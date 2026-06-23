#pragma once
// =============================================================================
// guild::play — NATIVE "choose player" identity wizard.
//
//   VIBE_Menu_RunChoosePlayer @0x52ccd8 — the first character-spine screen (after the
//   perspective pick). Form "Menu\\CHOOSEPLAYER", a 6-page wizard:
//     0 Vorname (text)  1 Nachname (text)  2 Geschlecht (radio)  3 Glauben (radio)
//     4 Wappen (8 buttons)  5 confirm (auto-commit).
//   Page text is `_M0_PERSOENLICH_*` (Ваш герой / Имя / Фамилия / Пол: Мужской·Женский /
//   Вера: Католическая·Катарическая / Выберите Ваши цвета).
//
// This is the native (Vulkan/SDL) FRONT that drives the headless reconstruction
// gui::Menu_RunChoosePlayer (the page state machine) through ChoosePlayerRunHooks. The
// name pages consume the SDL text-input stream (shim::IPlatform::pollText, the WM_CHAR
// substitute) + Backspace; the radio/wappen pages are click-driven; Enter advances,
// ESC / back steps back (the reconstruction exits at page 0).
//
// DEFERRED (named): the exact form chrome + the real wappen sprites are rendered with the
// menu backdrop + decoded gfx 1342+i when present, else labelled cells.
// =============================================================================
#include "gui/chooseplayer_run.h"   // ChoosePlayerState

#include <cstddef>
#include <string>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

struct ChoosePlayerConfig {
    std::string gameDir;
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;
    int frameCapMs = 16;
    std::string seedFirstName = "Spieler";
    std::string seedFamilyName;
    int seedGender = 0, seedFaith = 0, seedWappen = 0;
};

struct ChoosePlayerScreenResult {
    bool confirmed = false;     // the wizard completed (page 5 commit)
    bool back      = false;     // exited from page 0 / window-close
    std::string firstName, familyName;
    int gender = 0, faith = 0, wappenIndex = 0;
    int framesPresented = 0;
    int pageReached = 0;
    bool quitByWindow = false;
    bool usedRealText = false;  // the real _M0_PERSOENLICH_* labels were loaded
};

// Apply one frame of text editing to `buf`: append the UTF-8 `typed` (filtered to
// printable ASCII/Latin-1), and if `backspaceEdge` pop the last byte. Capped at `maxLen`.
// Pure (no platform) so the edit logic is unit-testable. Returns the (possibly) new size.
std::size_t ApplyTextEdit(std::string& buf, const std::string& typed, bool backspaceEdge,
                          std::size_t maxLen = 31);

// Render + run the player wizard until it commits (confirmed) or backs out (cancel).
ChoosePlayerScreenResult RunChoosePlayerScreen(shim::IGraphicsDevice& device,
                                               shim::IPlatform& plat,
                                               const ChoosePlayerConfig& cfg);

} // namespace guild::play
