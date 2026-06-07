#pragma once
#include "guild/common/types.h"
#include <cstdint>

// CRT signal handling from gilde.exe, namespace guild::crt.
//
// The original keeps a per-signal (handler, disposition) pair. Most signals are
// stored in the per-thread CRT block (off_64A90C()+88 / +92, 8-byte stride),
// but SIGSEGV (7) and SIGFPE (4) use the process-static array dword_64C12C[]
// (also 8-byte stride: +0 handler, +4 disposition). We model a single flat
// 12-entry table of {handler, disposition} indexed by signal number, which is
// observationally identical for a single-threaded reimpl.
//
//   VIBE_Signal_SetHandlerSlot   @0x609150
//   VIBE_Signal_GetHandlerSlot   @0x609198
//   VIBE_Signal_GetDispositionSlot @0x6091bc
//   VIBE_Signal_Register         @0x609358  (signal())
//   VIBE_Signal_Raise            @0x609408  (raise())
//   VIBE_Signal_RaiseSigTerm     @0x609314
//   VIBE_Signal_NeedsCtrlHandler @0x609264
//   VIBE_Signal_InitHandlerTable @0x609478
//   VIBE_Signal_DefaultHandler_Thunk @0x609308
namespace guild::crt {

using SignalHandler = void (*)(int);

// Signal numbers (MSVC <signal.h>); the original validates 1..12.
constexpr int kSIGINT  = 2;
constexpr int kSIGILL  = 4;  // stored in the static dword_64C12C array
constexpr int kSIGFPE  = 8;
constexpr int kSIGSEGV = 11; // NB: index 7 in the original's slot math (see .cpp)
constexpr int kSIGTERM = 15;
constexpr int kSIGABRT = 22;
constexpr int kSigMin  = 1;
constexpr int kSigMax  = 12;

// Disposition magic values stored in the handler slot (matching MSVC):
//   0 (SIG_DFL) — default; 1 (SIG_IGN) — ignore; the original also treats the
//   sentinel values 1/2/3 specially (1==IGN, 2==internal "raised/running",
//   3==SIG_ACK/SIG_SGE) and never invokes those as function pointers.
constexpr int kSIG_DFL = 0;
constexpr int kSIG_IGN = 1;
constexpr int kSIG_RUNNING = 2; // internal: set while a handler runs / default
constexpr int kSIG_ACK = 3;

constexpr int kSignalError = 3; // VIBE_Signal_Register returns 3 on EINVAL

// The process-wide signal table. The original indexes the static array for
// signals 4 and 7 and the per-thread block otherwise; we keep one table.
class SignalTable {
public:
    SignalTable() { Init(); }

    // VIBE_Signal_InitHandlerTable @0x609478 — seed the per-signal table from
    // the static defaults (all SIG_DFL) and install the runtime hooks.
    void Init();

    // VIBE_Signal_SetHandlerSlot @0x609150 — store `value` (a handler pointer
    // or disposition int) in the handler slot for `sig`; return the old value.
    std::uintptr_t SetHandlerSlot(int sig, std::uintptr_t value);

    // VIBE_Signal_GetHandlerSlot @0x609198 — read the handler slot for `sig`.
    std::uintptr_t GetHandlerSlot(int sig) const;

    // VIBE_Signal_GetDispositionSlot @0x6091bc — read the disposition slot.
    std::uintptr_t GetDispositionSlot(int sig) const;

    void SetDispositionSlot(int sig, std::uintptr_t value);

    // VIBE_Signal_Register @0x609358 (signal()): validate sig in 1..12, install
    // `handler` (a SignalHandler or one of the SIG_* dispositions), return the
    // previous handler. On out-of-range sig sets errno=EINVAL and returns 3.
    std::uintptr_t Register(int sig, std::uintptr_t handler);

    // Convenience typed wrapper around Register.
    SignalHandler Signal(int sig, SignalHandler handler);

    // VIBE_Signal_Raise @0x609408 (raise()): dispatch `sig`. If the current
    // handler is a real function pointer (not 1/2/3) reset the slot to
    // SIG_RUNNING and invoke it; SIG_DFL/abort handling per the original.
    // Returns 0 on a handled/known signal, -1 for an unknown one.
    int Raise(int sig);

    // VIBE_Signal_RaiseSigTerm @0x609314 — the SIGINT(2) fast path used by Raise.
    int RaiseSigTerm();

    // VIBE_Signal_NeedsCtrlHandler @0x609264 — true if SIGILL(4) or SIGSEGV(7)
    // is set to anything other than IGN/RUNNING (i.e. a console ctrl handler is
    // still required). Returns the original's boolean expression 1:1.
    bool NeedsCtrlHandler() const;

private:
    // Map a signal number to the original's internal slot index. SIGSEGV uses
    // index 7 and SIGILL index 4 in the static array; the others follow suit.
    static int slotIndex(int sig);

    struct Slot {
        std::uintptr_t handler = kSIG_DFL;     // +0x00 (off+88 / dword_64C12C[2i])
        std::uintptr_t disposition = kSIG_DFL; // +0x04 (off+92 / dword_64C12C[2i+1])
    };
    Slot slots_[16] = {};
};

// VIBE_Signal_DefaultHandler_Thunk @0x609308 — `return VIBE_Signal_Raise()`
// (forwards to Raise with the implicit current signal). Modelled as a no-op
// default that records the delivered signal for tests.
void DefaultHandlerThunk(int sig);

} // namespace guild::crt
