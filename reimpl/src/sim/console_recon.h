#pragma once
// ===========================================================================
// console_recon.{h,cpp} — the developer-console / CRT console-I/O primitive
// layer recovered from gilde.exe (VIBE_Console_* family).
//
// These are the low-level character primitives the developer console uses to
// read keystrokes and echo characters. In the original they wrap the Win32
// console API (CreateFileA("conin$"/"conout$"), ReadConsoleInputA,
// GetConsoleMode/SetConsoleMode, WriteConsoleA, SetConsoleCtrlHandler) — this
// is the MSVC CRT's `_getch`/`_putch`/`_kbhit`-style console layer.
//
// What is translated 1:1 here (pure logic, fully testable headless):
//   * VIBE_Console_IsValidKeyEvent  0x609090 — key-event filter predicate.
//   * VIBE_Console_ReadCharEvent    0x609c40 — the getch pushback state
//       machine (function-key two-byte sequence handling + repeat counts).
//   * VIBE_Console_GetCh            0x609d44 — ungetch slot + mode save/restore
//       orchestration and the overridable hook fast-paths.
//   * VIBE_Console_PutCh            0x609dc0 — single-char write orchestration.
//   * VIBE_Console_OpenConHandles   0x6090b8 — lazy handle open + caching.
//   * VIBE_Console_GetInputHandle   0x609128 / GetOutputHandle 0x609134.
//   * VIBE_Console_InstallCtrlHandler 0x60929c / RemoveCtrlHandler 0x6092cc —
//       SetConsoleCtrlHandler install/remove guard.
//
// COUPLED LEAVES → inert-default hooks (ConsoleHooks): the actual OS calls
// (open handle, read input record, write a char, get/set console mode, install
// the ctrl handler) and the CRT critical-section lock pair (off_64A910 /
// off_64A914) plus the three overridable function-pointer slots
// (dword_64AA50 / dword_64AA84 / dword_64AA8C). The default hooks are inert so
// the parse/dispatch logic is exercised by golden vectors. NOT a cheap
// analogue of engine logic (rule 8): the engine logic IS the state machine
// translated below; only the OS boundary is abstracted (Win32→hook, the
// pre-approved spirit of rule 4).
//
// gilde.exe is 32-bit x86 MSVC, imagebase 0x400000.
// ===========================================================================
#include "guild/common/types.h"

namespace guild::sim {

using guild::i32;
using guild::u8;
using guild::u16;
using guild::u32;

// ---------------------------------------------------------------------------
// One console key-input record. Mirrors the bytes the original reads via
// ReadConsoleInputA into an INPUT_RECORD; only the fields the logic touches are
// surfaced. Offsets (relative to the INPUT_RECORD base, as the decompile
// dereferences them):
//   +0x00 WORD  EventType            (*(_WORD*)a1)
//   +0x04 DWORD KeyEvent.bKeyDown    (*(_DWORD*)(a1+4))
//   +0x08 WORD  KeyEvent.wRepeatCount (Buffer.Event.KeyEvent.wRepeatCount)
//   +0x0A WORD  KeyEvent.wVirtualKeyCode (*(_WORD*)(a1+10))
//   +0x0C WORD  KeyEvent.wVirtualScanCode
//   +0x10 DWORD KeyEvent.dwControlKeyState (byte at +0x11 bit0 tested)
//   +0x0E BYTE  KeyEvent.uChar.AsciiChar
// gilde.exe INPUT_RECORD (KEY_EVENT subset).
struct ConsoleKeyRecord {
    u16 eventType;        // +0x00 ; KEY_EVENT == 1
    u32 keyDown;          // +0x04 ; bKeyDown
    u16 repeatCount;      // +0x08 ; wRepeatCount
    u16 virtualKeyCode;   // +0x0A ; wVirtualKeyCode
    u16 virtualScanCode;  // +0x0C ; wVirtualScanCode
    u8  asciiChar;        // +0x0E ; uChar.AsciiChar
    u8  controlKeyState;  // +0x11 low byte ; bit0 == enhanced/alt-ish flag
};

// ---------------------------------------------------------------------------
// Persistent console state, recovered from the gilde.exe .data globals. One
// instance models the process-global console block so the logic is testable
// without OS handles.
//   dword_64C124 = -1   input handle cache         (ConsoleState::inputHandle)
//   dword_64C128 = -1   output handle cache        (ConsoleState::outputHandle)
//   byte_64C194  =  0   ctrl-handler-installed flag (ConsoleState::ctrlInstalled)
//   dword_64C1E8 =  0   getch pushback phase        (ConsoleState::peekPhase)
//   dword_64A990 =  0   ungetch slot               (ConsoleState::ungetSlot)
//   dword_140AB00 = 0   pending scan/function byte (ConsoleState::pendingFnByte)
//   dword_140AB04 = 0   last char                  (ConsoleState::lastChar)
//   dword_140AB08 = 0   remaining repeat count     (ConsoleState::repeatLeft)
// gilde.exe console .data block.
struct ConsoleState {
    i32 inputHandle   = -1;   // dword_64C124
    i32 outputHandle  = -1;   // dword_64C128
    u8  ctrlInstalled = 0;    // byte_64C194
    i32 peekPhase     = 0;    // dword_64C1E8[0]
    i32 ungetSlot     = 0;    // dword_64A990
    i32 pendingFnByte = 0;    // dword_140AB00
    i32 lastChar      = 0;    // dword_140AB04
    i32 repeatLeft    = 0;    // dword_140AB08
};

// ---------------------------------------------------------------------------
// Inert-default OS boundary hooks. Each maps to one OS leaf the originals call.
// Defaults are pure / inert so the parse/dispatch logic is testable.
struct ConsoleHooks {
    // off_64A910 / off_64A914 — CRT console critical-section lock/unlock. Inert.
    void (*lock)()   = nullptr;
    void (*unlock)() = nullptr;

    // CreateFileA("conin$", GENERIC_READ ...) — open the console input handle.
    // Return value stored into dword_64C124. Default returns a sentinel != -1
    // so OpenConHandles stops retrying (models a successfully-opened handle).
    i32 (*openInput)()  = nullptr;
    // CreateFileA("conout$", GENERIC_WRITE ...) — open console output handle.
    i32 (*openOutput)() = nullptr;

    // SetConsoleCtrlHandler(HandlerRoutine, add) — returns nonzero on success.
    // gilde HandlerRoutine @0x6091fc raises VIBE_Signal SIGINT(4)/SIGBREAK(7).
    i32 (*setCtrlHandler)(i32 add) = nullptr;

    // GetConsoleMode(handle) → mode ; SetConsoleMode(handle, mode).
    u32  (*getConsoleMode)(i32 handle) = nullptr;
    void (*setConsoleMode)(i32 handle, u32 mode) = nullptr;

    // ReadConsoleInputA(handle, &rec) — fill `out` with one key record.
    // Returns false on failure (the original's ReadConsoleInputA==0 path →
    // ReadCharEvent returns -1). Default: returns false (no input available).
    bool (*readConsoleInput)(i32 handle, ConsoleKeyRecord* out) = nullptr;

    // WriteConsoleA(handle, &ch, 1) — emit one byte. Inert default.
    void (*writeConsoleChar)(i32 handle, u8 ch) = nullptr;

    // Overridable redirection slots (dword_64AA50 / dword_64AA84 / dword_64AA8C):
    //   getRedirect (dword_64AA84): if set, GetCh routes through it after calling
    //                               argSink(dword_64AA50) with `this`.
    //   putRedirect (dword_64AA8C): if set, PutCh routes through it after argSink.
    //   argSink     (dword_64AA50): receives the redirect argument.
    void (*argSink)(i32 arg) = nullptr;   // dword_64AA50
    i32  (*getRedirect)()    = nullptr;   // dword_64AA84
    void (*putRedirect)()    = nullptr;   // dword_64AA8C
};

// Process-global hook table + state accessors (single definition in the .cpp).
void                SetConsoleHooks(const ConsoleHooks* hooks);
const ConsoleHooks& GetConsoleHooks();
ConsoleState&       GetConsoleState();
void                ResetConsoleState();  // re-arm .data defaults (for tests).

// ===========================================================================
// Translated functions (1:1).
// ===========================================================================

// gilde.exe 0x609090 — VIBE_Console_IsValidKeyEvent(rec) (__usercall, eax=rec).
// Returns 1 iff: EventType==1 (KEY_EVENT) && bKeyDown != 0 && the virtual key
// is NOT one of VK_SHIFT(0x10)/VK_CONTROL(0x11)/VK_MENU(0x12). Else 0.
i32 Console_IsValidKeyEvent(const ConsoleKeyRecord& rec);

// gilde.exe 0x6090b8 — VIBE_Console_OpenConHandles().
// Lazily opens conin$/conout$ into the handle cache under the lock pair.
void Console_OpenConHandles();

// gilde.exe 0x609128 — VIBE_Console_GetInputHandle().
i32 Console_GetInputHandle();
// gilde.exe 0x609134 — VIBE_Console_GetOutputHandle().
i32 Console_GetOutputHandle();

// gilde.exe 0x60929c — VIBE_Console_InstallCtrlHandler().
// Installs HandlerRoutine once; returns the (u8) installed flag.
i32 Console_InstallCtrlHandler();
// gilde.exe 0x6092cc — VIBE_Console_RemoveCtrlHandler().
// Removes HandlerRoutine if installed and the OS call succeeds; returns
// (flag == 0), i.e. true when no handler is installed afterwards.
bool Console_RemoveCtrlHandler();

// gilde.exe 0x609c40 — VIBE_Console_ReadCharEvent(handle) (__usercall, eax=hnd).
// The getch pushback state machine. Returns the next character; for a function
// key it returns 0 first (and stashes the scan code for the next call), then
// the scan code; -1 on read failure.
i32 Console_ReadCharEvent(i32 handle);

// gilde.exe 0x609d44 — VIBE_Console_GetCh(this) (__thiscall).
// Returns the ungetch slot if armed, else routes through getRedirect, else
// reads one char with raw console mode (mode save/restore around ReadCharEvent).
i32 Console_GetCh(i32 thisArg = 0);

// gilde.exe 0x609dc0 — VIBE_Console_PutCh(ch, arg) (__usercall, eax=ch, ecx=arg).
// Writes one char: via putRedirect if armed, else WriteConsoleA. Returns ch.
i32 Console_PutCh(i32 ch, i32 arg = 0);

} // namespace guild::sim
