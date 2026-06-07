#pragma once
#include "guild/common/types.h"
#include "crt/strtol.h"   // reuse Errno() / kEINVAL (the per-thread errno slot)
#include <cstddef>
#include <vector>

// CRT startup / shutdown runtime from gilde.exe, namespace guild::crt.
//
// This is the MSVC C-runtime init/exit machinery. Two distinct registries live
// in the original and are modelled here byte-for-byte:
//
//  1. The PRIORITY-TAGGED init/term table (the "_initterm with priority" form).
//     A flat array of 6-byte slots between three cursors:
//        unk_64C406  — start of the init region                (kInitStart)
//        byte_64C436 — boundary: end of init / start of exit    (kInitEnd)
//        byte_64C454 — end of the exit region                   (kExitEnd)
//     Each slot is:
//        +0x00  u8  state     (2 == already run / disabled)
//        +0x01  u8  priority  (lower priority runs first on init)
//        +0x02  fn  funcptr   (4-byte function pointer)
//     VIBE_Crt_RunInitFuncs @0x5fe21c walks the init region selecting the
//     lowest still-pending priority >= a floor each pass; VIBE_Crt_RunExitFuncs
//     @0x5fe26c walks the exit region selecting the highest pending priority
//     <= a ceiling each pass (and gates the actual call on a second threshold).
//     The static image leaves the whole region zero-filled (runtime/linker
//     populated), so there is no byte table to recover — we model the registry.
//
//  2. The classic atexit/onexit function-pointer array (grow-down):
//        dword_1466074 — base   (__onexitbegin)
//        dword_1466070 — top    (__onexitend, points one past the last entry)
//     VIBE_Crt_DoExit_21281 @0x1421281 walks it from top-1 down to base calling
//     each non-null pointer — i.e. LIFO. We model the array + a registrar (the
//     original __onexit is outside this module's owned set) and the LIFO walk.
//
// And the plain initializer-table walker:
//     VIBE_Crt_RunInitializerTable @0x142131a — iterate [begin,end) of function
//     pointers, calling each non-null one in array order.
namespace guild::crt {

using CrtFunc = void (*)();
using CrtInitFunc = int (*)(); // _initterm entries return int (0 == ok)

// ---------------------------------------------------------------------------
// (1) Priority-tagged init/term registry.
// ---------------------------------------------------------------------------

// One 6-byte slot of the priority-tagged table. Field offsets match the
// original record stride (RunInitFuncs advances `result += 6`).
struct PrioritySlot {
    u8       state;    // +0x00  2 == already invoked / disabled
    u8       priority; // +0x01
    CrtInitFunc func;  // +0x02  (4-byte fn ptr in the 32-bit original)
};

class PriorityInitTable {
public:
    // Register an init-region entry (run by RunInitFuncs).
    void RegisterInit(u8 priority, CrtInitFunc fn);
    // Register an exit-region entry (run by RunExitFuncs).
    void RegisterExit(u8 priority, CrtInitFunc fn);

    // VIBE_Crt_RunInitFuncs @0x5fe21c (__usercall, al = floor).
    // Repeatedly: scan the init region for the entry with the lowest priority
    // that is still >= `floor` and not yet run (state!=2); invoke it (via
    // InvokeExitFunc) and mark state=2. Stops when no candidate remains.
    void RunInitFuncs(u8 floor);

    // VIBE_Crt_RunExitFuncs @0x5fe26c (__usercall, al = floor, dl = gate).
    // Repeatedly: scan the exit region for the entry with the highest priority
    // that is still >= `floor` (the `i <= priority` ratchet) and not yet run;
    // if `gate >= priority` invoke it; mark state=2. Stops when no candidate
    // remains. Entries fire in DESCENDING priority order. The original callers
    // pass floor=0x10/0 and gate=0xFF/0xF (DoExit/ExitProcess).
    void RunExitFuncs(u8 floor, u8 gate);

    const std::vector<PrioritySlot>& InitSlots() const { return init_; }
    const std::vector<PrioritySlot>& ExitSlots() const { return exit_; }

private:
    std::vector<PrioritySlot> init_; // [unk_64C406 .. byte_64C436)
    std::vector<PrioritySlot> exit_; // [byte_64C436 .. byte_64C454)
};

// VIBE_Crt_InvokeExitFunc @0x5fe210 — if *slot.func is non-null, call it and
// return its (re-interpreted) result; else return the slot unchanged. Modelled
// as: call the function if present.
void InvokeExitFunc(PrioritySlot& slot);

// ---------------------------------------------------------------------------
// (2) atexit / exit machinery.
// ---------------------------------------------------------------------------

// VIBE_Crt_RunInitializerTable @0x142131a — call every non-null fn in [begin,end).
void RunInitializerTable(CrtFunc* begin, CrtFunc* end);

// The process exit registry, modelling the dword_1466070/74 onexit array plus
// the dword_142C0FC..142C124 initializer-table cursors used by RunExitHandlers.
class ExitRegistry {
public:
    // __onexit / atexit registrar: push onto the grow-down onexit array.
    // Returns the registered pointer (atexit returns 0 on success in C, but the
    // original onexit returns the fn ptr; we expose the boolean-ish fn).
    CrtFunc Atexit(CrtFunc fn);

    // VIBE_Crt_DoExit_21281 @0x1421281 (a1=code, a2=quick, a3=skipFinal).
    // When !quick: walk the onexit array top-1..base calling each non-null
    // pointer (LIFO), then run the pre-terminator initializer tables. Always
    // runs the terminator table. Records the exit code and the skipFinal flag.
    void DoExit(int code, int quick, int skipFinal);

    // VIBE_Crt_Exit @0x142125f — DoExit(code, 0, 0).
    void Exit(int code);
    // VIBE_Crt_QuickExit @0x1421270 — DoExit(code, 1, 0).
    void QuickExit(int code);

    // VIBE_Crt_RunExitHandlers @0x1421232 — run the two pre-exit initializer
    // tables (off_145298C optional hook + dword_142C0FC/142C104 ranges).
    void RunExitHandlers();

    // Observable state for tests.
    int  ExitCode() const { return exitCode_; }       // recorded a1
    bool Terminating() const { return terminating_; }  // dword_145A220
    int  SkipFinal() const { return skipFinal_; }      // byte_145A21C
    const std::vector<CrtFunc>& Handlers() const { return handlers_; }

    // Optional pre-exit hook (off_145298C) and ordered initializer tables that
    // RunExitHandlers / DoExit walk. Empty by default.
    void SetPreExitHook(CrtFunc hook) { preExitHook_ = hook; }
    void AddPreTerminator(CrtFunc fn) { preTerm_.push_back(fn); }
    void AddTerminator(CrtFunc fn) { term_.push_back(fn); }

    // Record of the order handlers actually fired in (for the e2e test).
    const std::vector<CrtFunc>& FireOrder() const { return fireOrder_; }

private:
    std::vector<CrtFunc> handlers_; // dword_1466074..1466070 onexit array
    std::vector<CrtFunc> preTerm_;  // dword_142C114..142C11C
    std::vector<CrtFunc> term_;     // dword_142C120..142C124
    CrtFunc preExitHook_ = nullptr; // off_145298C
    std::vector<CrtFunc> fireOrder_;
    int  exitCode_ = 0;
    bool terminating_ = false;
    int  skipFinal_ = 0;
};

// ---------------------------------------------------------------------------
// Stack probe / bounds (the _chkstk page-touch loop).
// ---------------------------------------------------------------------------

// VIBE_Crt_StackProbe @0x5fe350 (__stdcall) — the classic page-by-page stack
// touch loop (_chkstk). Touches one dword per 4 KiB page down to `size` bytes
// and returns `size` after the final 4096 decrement (the original's return is
// the residual: size - 4096*ceil(size/4096), i.e. <= 0). We reproduce that
// integer residual exactly WITHOUT actually scribbling the stack (the probe is
// a no-op observationally beyond the return value in a hosted reimpl).
int StackProbe(int size);

// ---------------------------------------------------------------------------
// errno model (reuses the per-thread CRT errno slot from strtol.h: Errno()).
// ---------------------------------------------------------------------------

// VIBE_Runtime_SetErrnoEinval @0x5fb1c0 — set the thread errno slot to EINVAL.
void SetErrnoEinval();

} // namespace guild::crt
