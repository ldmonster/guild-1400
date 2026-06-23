#pragma once
// gilde.exe — MSVC C-runtime exception→signal action table (_XcptActTab) and
// the two thin wrappers the signal subsystem installs as runtime hooks.
// namespace guild::crt.
//
// This is the CRT layer that translates a structured/FP hardware exception code
// into a C signal (SIGFPE/SIGILL/SIGSEGV) and dispatches the registered C
// handler. It is distinct from the signal()/raise() table modelled by
// SignalTable in signal.{h,cpp}; this file adds:
//
//   VIBE_Signal_GetHandlerIfMatched @0x6091e0 — runtime hook (dword_64ADDC):
//       if (GetDispositionSlot(sig) == GetHandlerSlot? ) ... (see .cpp)
//   VIBE_Signal_ResetCtrlState      @0x6094c0 — runtime hook: if a console ctrl
//       handler is still required, remove it and clear SIGILL/SIGSEGV slots.
//   VIBE_Signal_InstallTermHooks    @0x6094f0 — install the init + reset hooks
//       (off_64A94C / off_64A950).
//   VIBE_Signal_FindAction          @0x14261de — linear scan of _XcptActTab for
//       an entry whose exception code == `code`.
//   VIBE_Signal_Raise_2609d         @0x142609d — dispatch path: look up the
//       action for the raised C++/SEH code and invoke the C handler.
//
// Recovered table geometry (get_bytes @0x1455008, 0x1455080):
//   _XcptActTab[10] of { i32 xcptCode; i32 sigAction; Handler fn; }  (12 bytes)
//   dword_1455088 = 10   (entry count)
//   dword_1455080 = 3, dword_1455084 = 7  (the SIGFPE per-code clear region:
//       zero unk_1455010[3..9].field0 with 12-byte stride before dispatch)
//   dword_145508C = _fpecode (current SIGFPE sub-code), default 0xFFFFFFFF? — the
//       table seeds it to -1; Raise sets/restores it around the FPE handler.
#include "guild/common/types.h"
#include "crt/signal.h"
#include <cstdint>

namespace guild::crt {

using XcptHandler = void (*)(int);

// _XcptActTab entry — 12 bytes in the original (i32, i32, ptr).
struct XcptAction {
    i32 code;                 // +0  structured/FP exception code (0xCxxxxxxx)
    i32 action;               // +4  the C signal to deliver (0/4/8 = -/SIGILL/SIGFPE)
    XcptHandler handler = nullptr; // +8 currently-registered C handler for `code`
};

// The 10-entry action table, seeded with the original's recovered codes. The
// handler column starts null (SIG_DFL) and is mutated by Raise/signal exactly
// as the original mutates _XcptActTab[i].handler.
class XcptActionTable {
public:
    static constexpr int kCount = 10;        // dword_1455088
    static constexpr int kFpeClearStart = 3; // dword_1455080
    static constexpr int kFpeClearCount = 7; // dword_1455084

    XcptActionTable();

    // VIBE_Signal_FindAction @0x14261de — return the entry matching `code`, or
    // null. (The original scans 12-byte strides; entry 0 is checked first.)
    XcptAction* FindAction(i32 code);

    // VIBE_Signal_Raise_2609d @0x142609d — dispatch the C handler registered for
    // structured exception `code` (a2 = the OS exception-pointers arg passed to
    // the handler context). Returns:
    //   * defaultRaise(a2)  if no action / no handler is installed,
    //   * 1                 if the handler is SIG_ACK(5) (cleared, "handled"),
    //   * -1                otherwise (handler invoked, or IGN passthrough).
    int Raise(i32 code, i32 a2);

    // --- injected leaves (default inert) -----------------------------------
    // dword_1467798 — the "no C handler" default action (the SEH continue-search
    // / __FPECode passthrough). Returns 0 by default.
    using DefaultRaiseFn = int (*)(i32 a2);
    void set_default_raise(DefaultRaiseFn fn) { defaultRaise_ = fn; }

    // --- inspectable state --------------------------------------------------
    void set_handler(i32 code, XcptHandler h) { if (auto* a = FindAction(code)) a->handler = h; }
    i32  fpecode() const { return fpecode_; }
    const XcptAction& entry(int i) const { return tab_[i]; }

private:
    XcptAction tab_[kCount];
    i32 fpecode_ = 0;            // dword_145508C (_fpecode)
    DefaultRaiseFn defaultRaise_ = nullptr; // dword_1467798
};

// VIBE_Signal_GetHandlerIfMatched @0x6091e0 — runtime dispatch hook
// (dword_64ADDC). If the signal's disposition slot equals its handler slot
// (the original compares GetDispositionSlot(sig) against the value in edx left
// by the call, which is the same slot read) it returns GetHandlerSlot(sig),
// else 0. Faithful translation against the existing SignalTable.
std::uintptr_t Signal_GetHandlerIfMatched(SignalTable& t, int sig);

// VIBE_Signal_ResetCtrlState @0x6094c0 — runtime reset hook (dword_64ADE0 sibling).
//   if (NeedsCtrlHandler()) { Console_RemoveCtrlHandler();
//                             SetHandlerSlot(4, SIG_RUNNING);
//                             return SetHandlerSlot(7, SIG_RUNNING); }
//   return 0;
// Console_RemoveCtrlHandler is an OS leaf (injected, inert by default).
int Signal_ResetCtrlState(SignalTable& t, void (*removeCtrlHandler)() = nullptr);

// VIBE_Signal_InstallTermHooks @0x6094f0 — store the init + reset hook pointers.
// Modelled as setting the two function-pointer outputs.
struct TermHooks {
    int (*initHandlerTable)() = nullptr;  // off_64A94C = VIBE_Signal_InitHandlerTable
    int (*resetCtrlState)()   = nullptr;  // off_64A950 = VIBE_Signal_ResetCtrlState
};
void Signal_InstallTermHooks(TermHooks& hooks,
                             int (*initHandlerTable)(),
                             int (*resetCtrlState)());

} // namespace guild::crt
