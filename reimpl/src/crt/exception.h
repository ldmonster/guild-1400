#pragma once
#include "guild/common/types.h"
#include <cstdint>

// CRT structured-exception (SEH) + FP-exception machinery from gilde.exe,
// namespace guild::crt.
//
// SEH (the MSVC __except_handler3 scope-walk):
//   VIBE_Eh_RunFrameHandlers     @0x1426954  — dispatch / unwind one frame
//   VIBE_Eh_UnwindNestedHandlers @0x142689e  — unwind the try-level chain
//   VIBE_Eh_SaveRegistration     @0x1426932  — stash the active frame globals
// We model the two original record layouts byte-for-byte and reproduce the
// scope-table walk and the nested-handler unwind. Register/TEB plumbing (the
// __usercall ebp args, NtCurrentTeb()->ExceptionList) is replaced by explicit
// pointers so the walk is testable in isolation.
//
// FP exceptions (the libm error reporter):
//   VIBE_FpException_ClassToType @0x1424114  — _fpclass bits -> error type
//   VIBE_FpException_SetErrno    @0x14240c9  — map type -> errno (EDOM/ERANGE)
//   VIBE_FpException_LookupHandler @0x14240ef — search the (input,fn) table
//
// UnhandledExceptionFilter @0x604dd4 and SetUnhandledExceptionFilter are OS
// leaves: declared here as a thin shim (records the last filter / pointers).
namespace guild::crt {

// ---------------------------------------------------------------------------
// SEH record layouts (recovered from the scope-walk addressing).
// ---------------------------------------------------------------------------

// One scope-table entry. RunFrameHandlers indexes it as 12-byte stride
// (`v6 + 12*v5 + {0,4,8}`) and UnwindNestedHandlers as 3 dwords (`v4 + 4*3*v5`).
struct ScopeTableEntry {
    int     enclosingLevel; // +0x00  previous try-level (-1 == none)
    void  (*filter)();      // +0x04  exception filter (0 == none)
    void  (*handler)();     // +0x08  __except / __finally body
};

// The per-frame registration record. The original reads:
//   +0x08  scopeTable pointer  (v6 = *(a2+8))
//   +0x0C  currentTryLevel     (v5 = *(a2+12))
// (the leading +0x00 prev / +0x04 handler are the standard EXCEPTION_REGISTRATION
// fields, kept for layout fidelity.)
struct ExceptionFrame {
    ExceptionFrame*  prev;         // +0x00  next outer registration
    void           (*frameHandler)();  // +0x04  __except_handler3
    ScopeTableEntry* scopeTable;   // +0x08
    int              tryLevel;     // +0x0C  current try level (-1 == none)
};

// Globals stashed by SaveRegistration (unk_145509C/A0/A4).
struct EhSavedState {
    void* scopeTable = nullptr; // unk_14550A0  (*(frame+8))
    int   code = 0;             // unk_145509C  (the eax "result")
    void* frame = nullptr;      // unk_14550A4  (the ebp/frame pointer)
};
EhSavedState& EhSaved();

// VIBE_Eh_SaveRegistration @0x1426932 — stash {code, *(frame+8), frame}.
int EhSaveRegistration(int code, ExceptionFrame* frame, int /*disposition*/);

// VIBE_Eh_UnwindNestedHandlers @0x142689e — walk the frame's try-level chain
// from its current level down to `targetLevel` (-1 == unwind all), invoking each
// level's filter==null handler (the __finally bodies) on the way out and
// updating the saved registration. Returns the frame pointer.
ExceptionFrame* EhUnwindNestedHandlers(ExceptionFrame* frame, int targetLevel);

// VIBE_Eh_RunFrameHandlers @0x1426954 — the __except_handler3 core. If the
// exception flags (`excFlags`) request an unwind (bits 6 set), unwind the frame
// and return 1. Otherwise walk the scope table from the frame's current level
// outward, calling each non-null filter; on a filter returning >0, unwind the
// nested handlers up to that level and invoke its handler (does not return in
// the original — modelled as returning 0 to signal "handled here"). Returns 1
// when no handler claims the exception, 0 when one does.
int EhRunFrameHandlers(int excFlags, ExceptionFrame* frame, void* dispatcher);

// ---------------------------------------------------------------------------
// FP exception classification (self-contained, testable).
// ---------------------------------------------------------------------------

// _fpclass-style error type codes the original produces.
enum FpErrType {
    kFpNormal = 0, // (the original returns 2*(class&2) for the residual case)
    kFpDomain = 1, // _FPCLASS_SNAN -> domain error
    kFpSing   = 2, // singularity
    kFpOver   = 3, // overflow / infinity
    kFpUnder  = 5, // _FPCLASS_NZ/PZ underflow
};

// VIBE_FpException_ClassToType @0x1424114 — map an _fpclass bit field to the
// error type. Bit tests (in order): 0x20 -> 5, 0x08 -> 1, 0x04 -> 2, 0x01 -> 3,
// else 2*(class & 2).
int FpClassToType(u8 fpclass);

// VIBE_FpException_SetErrno @0x14240c9 — map an error type to errno:
//   type==1 -> EDOM(33); type in (1,3] -> ERANGE(34); else unchanged.
// Returns the input type (the original returns `a1`). The value is written to
// the math errno slot dword_145A1DC, exposed as MathErrno().
constexpr int kEDOM = 33;
constexpr int kERANGE_FP = 34;
int& MathErrno();   // dword_145A1DC
int FpSetErrno(int type);

// ---------------------------------------------------------------------------
// OS-leaf shims: UnhandledExceptionFilter / SetUnhandledExceptionFilter.
// ---------------------------------------------------------------------------

using TopLevelExceptionFilter = long (*)(void*);

// VIBE_Crt_InstallExceptionHandler @0x605180 — record the top-level filter.
TopLevelExceptionFilter InstallExceptionHandler(TopLevelExceptionFilter filter);
// VIBE_Crt_RemoveExceptionHandler @0x6051cc — clear it.
void RemoveExceptionHandler();
// The currently installed top-level filter (for tests).
TopLevelExceptionFilter CurrentExceptionFilter();

} // namespace guild::crt
