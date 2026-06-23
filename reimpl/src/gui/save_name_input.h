#pragma once
// guild::gui — the save-name text-entry modal: the INPUT / CONFIRM state logic.
//
// gilde.exe 0x56a700 — VIBE_Menu_RunSaveNameInput(parentWin@eax, parent@edx, buf@ebx)
//   (__usercall, eax = confirmed?). Registers one text-input field (color 66) seeded
//   with the current `buf` text, focuses it, sizes it (width 272, the +8 / +120 sprite
//   region), and runs the modal frame loop.  When the user presses ENTER (key 28) the
//   field's text is copied back into `buf` and the dialog closes returning 1; if the
//   loop ends any other way (cancel / window-close) it returns 0 and `buf` is unchanged.
//
// This is the text-entry *state machine* — the part that mutates the name buffer and
// reports confirm vs cancel.  The field-widget creation, focus, sizing sprites and the
// modal frame loop are the SDL/Vulkan widget boundary (rules 3-4); they are routed
// through SaveNameInputHooks so the input logic is testable in isolation.

#include "guild/common/types.h"
#include <string>

namespace guild::gui {

// gilde.exe — the keycode for ENTER (byte_67225C == 28 == confirm).
inline constexpr int kKeyEnter = 28;
// gilde.exe 0x56a76c — the field width the dialog forces (dword_6951D8 := 272).
inline constexpr int kSaveNameFieldWidth = 272;
// gilde.exe 0x56a75b — the field x-origin region (dword_695090 := 128).
inline constexpr int kSaveNameFieldX = 128;
// gilde.exe 0x56a71e — the input field's color (Object_SetColor(field, 66)).
inline constexpr int kSaveNameFieldColor = 66;

// The modal step the host drives each frame; the input logic asks it three things.
struct SaveNameInputHooks {
    // Pump one frame: returns false when the modal loop should end (RunFrameLoop == 0 /
    // window closed). Default: ends immediately (no frames).
    bool (*pumpFrame)() = nullptr;
    // The last key the frame produced (byte_67225C). Default: 0 (none).
    int (*lastKey)() = nullptr;
    // The current text in the input field (Object_GetDataPtr). Default: "".
    std::string (*fieldText)() = nullptr;
    // True when the host requested the loop close for a non-confirm reason
    // (dword_672230 != 0 || !dword_62D328). Default: false.
    bool (*cancelRequested)() = nullptr;
};

void SetSaveNameInputHooks(const SaveNameInputHooks& hooks);
const SaveNameInputHooks& GetSaveNameInputHooks() ;

// gilde.exe 0x56a700 — VIBE_Menu_RunSaveNameInput. Runs the modal loop; on ENTER copies
// the field text into `buf` and returns true (confirmed). Returns false (cancel) leaving
// `buf` unchanged. `initialText` seeds the field (the original passes `buf` in via ebx).
bool Menu_RunSaveNameInput(std::string& buf);

} // namespace guild::gui
