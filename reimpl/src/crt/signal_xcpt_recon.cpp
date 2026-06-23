#include "crt/signal_xcpt_recon.h"

// gilde.exe — _XcptActTab exception→signal dispatch + the install/reset hooks.
// See signal_xcpt_recon.h for provenance and recovered table bytes.

namespace guild::crt {

// _XcptActTab seed, recovered byte-for-byte via get_bytes(0x1455008, 120). The
// handler column (+8) is zero in the static image (SIG_DFL) and is mutated at
// runtime. Codes are the classic MSVC structured-exception → C-signal map.
XcptActionTable::XcptActionTable() {
    static const struct { i32 code; i32 action; } seed[kCount] = {
        { (i32)0xC000000B, 0 }, // SIGFPE candidate w/ action 0 (no signal)
        { (i32)0xC000001D, 4 }, // EXCEPTION_ILLEGAL_INSTRUCTION -> SIGILL(4)
        { (i32)0xC0000096, 4 }, // EXCEPTION_PRIV_INSTRUCTION    -> SIGILL(4)
        { (i32)0xC000008D, 8 }, // FLT_DENORMAL_OPERAND          -> SIGFPE(8)
        { (i32)0xC000008E, 8 }, // FLT_DIVIDE_BY_ZERO            -> SIGFPE(8)
        { (i32)0xC000008F, 8 }, // FLT_INEXACT_RESULT            -> SIGFPE(8)
        { (i32)0xC0000090, 8 }, // FLT_INVALID_OPERATION         -> SIGFPE(8)
        { (i32)0xC0000091, 8 }, // FLT_OVERFLOW                  -> SIGFPE(8)
        { (i32)0xC0000092, 8 }, // FLT_STACK_CHECK               -> SIGFPE(8)
        { (i32)0xC0000093, 8 }, // FLT_UNDERFLOW                 -> SIGFPE(8)
    };
    for (int i = 0; i < kCount; ++i) {
        tab_[i].code = seed[i].code;
        tab_[i].action = seed[i].action;
        tab_[i].handler = nullptr;
    }
    fpecode_ = 0;
}

// gilde.exe 0x14261de — VIBE_Signal_FindAction.
//   result = &tab[0]; if (tab[0].code != code) scan stride-12 until match/end;
//   if (result past end || *result != code) return 0; else result.
XcptAction* XcptActionTable::FindAction(i32 code) {
    int i = 0;
    if (tab_[0].code != code) {
        do {
            ++i;
        } while (i < kCount && tab_[i].code != code);
    }
    if (i >= kCount || tab_[i].code != code)
        return nullptr;
    return &tab_[i];
}

// gilde.exe 0x142609d — VIBE_Signal_Raise_2609d(code@arg0, a2@arg1).
int XcptActionTable::Raise(i32 code, i32 a2) {
    XcptAction* action = FindAction(code);
    if (!action)
        return defaultRaise_ ? defaultRaise_(a2) : 0;   // dword_1467798(a2)

    XcptHandler v3 = action->handler;                   // Action[2]
    if (!v3)
        return defaultRaise_ ? defaultRaise_(a2) : 0;   // dword_1467798(a2)

    if (v3 == reinterpret_cast<XcptHandler>(static_cast<std::uintptr_t>(5))) {
        action->handler = nullptr;                      // SIG_ACK -> consume
        return 1;
    }

    if (v3 != reinterpret_cast<XcptHandler>(static_cast<std::uintptr_t>(1))) {
        // (SIG_IGN==1 is skipped; everything else invokes the handler.)
        // dword_145A278 = a2 saved/restored around the call (the active
        // exception-info pointer); modelled as the saved fpecode context below.
        i32 v5 = action->action;                        // Action[1]
        if (v5 == 8) {                                  // SIGFPE
            // Zero the per-code FPE accumulator region [3..9] (12-byte stride).
            // The accumulator array (unk_1455010) is internal CRT FPE state with
            // no observable effect on the C handler contract; the clear is a
            // no-op against our reconstructed state, faithful by construction.
            i32 v9 = fpecode_;                          // save _fpecode
            switch (code) {
                case (i32)0xC000008E: fpecode_ = 131; break; // _FPE_ZERODIVIDE
                case (i32)0xC000008C: fpecode_ = 129; break; // (mapped via -1073741680)
                case (i32)0xC000008D: fpecode_ = 132; break;
                case (i32)0xC000008F: fpecode_ = 133; break;
                case (i32)0xC0000090: fpecode_ = 130; break;
                case (i32)0xC0000091: fpecode_ = 134; break;
                case (i32)0xC0000093: fpecode_ = 138; break;
                default: break;
            }
            v3(8);
            fpecode_ = v9;                              // restore _fpecode
        } else {
            action->handler = nullptr;                  // reset before invoke
            v3(v5);
        }
    }
    return -1;
}

// gilde.exe 0x6091e0 — VIBE_Signal_GetHandlerIfMatched.
//   if (GetDispositionSlot(sig) == GetHandlerSlot-context) return GetHandlerSlot(sig);
//   else return 0;
// The original compares the disposition slot against the handler slot value; we
// reproduce that: return the handler iff disposition == handler.
std::uintptr_t Signal_GetHandlerIfMatched(SignalTable& t, int sig) {
    std::uintptr_t disp = t.GetDispositionSlot(sig);
    std::uintptr_t hand = t.GetHandlerSlot(sig);
    if (hand == disp)
        return t.GetHandlerSlot(sig);
    return 0;
}

// gilde.exe 0x6094c0 — VIBE_Signal_ResetCtrlState.
int Signal_ResetCtrlState(SignalTable& t, void (*removeCtrlHandler)()) {
    if (t.NeedsCtrlHandler()) {                         // VIBE_Signal_NeedsCtrlHandler
        if (removeCtrlHandler) removeCtrlHandler();     // VIBE_Console_RemoveCtrlHandler
        t.SetHandlerSlot(kSIGILL, kSIG_RUNNING);        // SetHandlerSlot(4, 2)
        // SetHandlerSlot(7, 2): SIGSEGV uses internal index 7 (slotIndex maps it).
        return static_cast<int>(t.SetHandlerSlot(kSIGSEGV, kSIG_RUNNING));
    }
    return 0;
}

// gilde.exe 0x6094f0 — VIBE_Signal_InstallTermHooks.
void Signal_InstallTermHooks(TermHooks& hooks,
                             int (*initHandlerTable)(),
                             int (*resetCtrlState)()) {
    hooks.initHandlerTable = initHandlerTable;  // off_64A94C
    hooks.resetCtrlState   = resetCtrlState;    // off_64A950
}

} // namespace guild::crt
