#include "crt/signal.h"
#include "crt/runtime.h" // SetErrnoEinval

namespace guild::crt {

namespace {
// Records the last signal delivered to the default thunk (for tests). The
// original's thunk forwards to raise(); here we capture it observably.
int g_lastDefaultSignal = 0;
} // namespace

void DefaultHandlerThunk(int sig) { g_lastDefaultSignal = sig; }

// The original stores SIGSEGV under internal index 7 and SIGILL under 4 in the
// static dword_64C12C array; the per-thread block holds the rest at +88/+92
// with an 8-byte stride keyed by the same internal index. We use one flat
// table keyed by that internal index. The mapping below matches the constants
// the binary's signal() callers pass (2/4/8/11/15/22 map to slots 2/4/8/7/15/22
// reduced into 1..12 by the original's compare; we keep the raw value, which is
// already <=12 for the validated range).
int SignalTable::slotIndex(int sig) {
    // SIGSEGV(11) is handled at internal index 7 by the original's branch
    // (a1==7); every other validated signal indexes by its own number. We map
    // 11 -> 7 to mirror the dword_64C12C[2*a1] addressing for SIGSEGV.
    if (sig == kSIGSEGV)
        return 7;
    return sig;
}

void SignalTable::Init() {
    // VIBE_Signal_InitHandlerTable @0x609478: copy the static defaults into the
    // per-thread block (all SIG_DFL in the static image) and install the two
    // runtime hooks (dword_64ADDC/64ADE0). All slots start at SIG_DFL.
    for (Slot& s : slots_) {
        s.handler = kSIG_DFL;
        s.disposition = kSIG_DFL;
    }
    g_lastDefaultSignal = 0;
}

std::uintptr_t SignalTable::SetHandlerSlot(int sig, std::uintptr_t value) {
    int idx = slotIndex(sig);
    std::uintptr_t old = slots_[idx].handler;
    slots_[idx].handler = value;
    return old;
}

std::uintptr_t SignalTable::GetHandlerSlot(int sig) const {
    return slots_[slotIndex(sig)].handler;
}

std::uintptr_t SignalTable::GetDispositionSlot(int sig) const {
    return slots_[slotIndex(sig)].disposition;
}

void SignalTable::SetDispositionSlot(int sig, std::uintptr_t value) {
    slots_[slotIndex(sig)].disposition = value;
}

// VIBE_Signal_Register @0x609358 (signal()).
std::uintptr_t SignalTable::Register(int sig, std::uintptr_t handler) {
    if (sig < kSigMin || sig > kSigMax) {
        SetErrnoEinval();         // VIBE_Runtime_SetErrnoEinval
        return kSignalError;      // returns 3
    }
    // off_64AD34 = DefaultHandler_Thunk (install the dispatch hook). Modelled
    // implicitly: our Raise() already routes default delivery through the thunk.
    // The original's MBCS-mask side effect for SIGINT (sig==2) when a special
    // disposition is active is a console-thread guard with no observable state
    // here, so it is omitted.
    std::uintptr_t old = GetHandlerSlot(sig);
    SetHandlerSlot(sig, handler);
    // The original then installs or removes the console ctrl handler based on
    // NeedsCtrlHandler(); that console action is an OS leaf (modelled as state).
    return old;
}

SignalHandler SignalTable::Signal(int sig, SignalHandler handler) {
    return reinterpret_cast<SignalHandler>(
        Register(sig, reinterpret_cast<std::uintptr_t>(handler)));
}

// VIBE_Signal_RaiseSigTerm @0x609314 — the SIGINT(2) path.
//   h = GetHandlerSlot(2);
//   if (h==1 || h==2 || h==3) return -1;        // IGN/RUNNING/ACK -> nothing
//   SetHandlerSlot(2, 2); h();  return 0;        // reset to RUNNING, call
int SignalTable::RaiseSigTerm() {
    std::uintptr_t h = GetHandlerSlot(kSIGINT);
    if (h == kSIG_IGN || h == kSIG_RUNNING || h == kSIG_ACK)
        return -1;
    SetHandlerSlot(kSIGINT, kSIG_RUNNING);
    if (h && h != kSIG_DFL)
        reinterpret_cast<SignalHandler>(h)(kSIGINT);
    return 0;
}

// VIBE_Signal_Raise @0x609408 (raise()).
int SignalTable::Raise(int sig) {
    std::uintptr_t h = GetHandlerSlot(sig);
    switch (sig) {
        case 1:
            // case 1: if handler==RUNNING, abnormal-termination; fall through.
            // (VIBE_Crt_AbnormalTermination is an OS leaf; modelled as no-op.)
            // falls into LABEL_5 below.
            /* fallthrough */
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12: {
            // LABEL_5: if the handler is a real function (not IGN/RUNNING/ACK),
            // reset the slot to RUNNING and invoke it.
            if (h != kSIG_IGN && h != kSIG_RUNNING && h != kSIG_ACK) {
                SetHandlerSlot(sig, kSIG_RUNNING);
                if (h && h != kSIG_DFL)
                    reinterpret_cast<SignalHandler>(h)(sig);
                else
                    DefaultHandlerThunk(sig);
            }
            return 0;
        }
        case 2:
            return RaiseSigTerm();
        default:
            return -1;
    }
}

// VIBE_Signal_NeedsCtrlHandler @0x609264:
//   d4 = GetHandlerSlot(4); d7 = GetHandlerSlot(7);
//   return (d4 != 2 && d4 != 3) || (d7 != 2 && d7 != 3);
bool SignalTable::NeedsCtrlHandler() const {
    std::uintptr_t d4 = GetHandlerSlot(kSIGILL);        // index 4
    std::uintptr_t d7 = slots_[7].handler;              // SIGSEGV internal idx 7
    bool a = (d4 != kSIG_RUNNING && d4 != kSIG_ACK);
    bool b = (d7 != kSIG_RUNNING && d7 != kSIG_ACK);
    return a || b;
}

} // namespace guild::crt
