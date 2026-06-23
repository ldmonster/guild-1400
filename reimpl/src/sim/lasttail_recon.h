#pragma once
// "Long-tail" leaf reconstructions: the last small forwarder thunks and trivial
// no-op / return-0 stubs reached from the live call tree, plus the MSVC argv
// program-name skip helper.
//
// Namespace: guild::sim (same cluster the script/util callers live in).
//
// Each entry is reconstructed 1:1 from the gilde.exe Hex-Rays decompile (the
// reference of record). Forwarder thunks call their ALREADY-reconstructed
// targets; the trivial stubs reproduce the binary's empty / return-0 bodies.
#include "guild/common/types.h"

namespace guild::sim {

// --- Forwarder thunks to string-compare leaves ------------------------------
// These wrap util leaves that live elsewhere in src/ (StrCmp @0x5d3f10 in
// util/util_recon.cpp; StrCmpNoCase @0x5cb8f0 in util/string_ops.cpp). The thin
// thunks reproduce the original's exact deref / argument-order shuffle.

// gilde.exe 0x5dcd36 — VIBE_Util_StrCmpNoCase_Thunk (thunk, eax=a, edx=b).
//   return VIBE_Util_StrCmpNoCase(a, b);
int Util_StrCmpNoCase_Thunk(const char* a, const char* b);

// gilde.exe 0x44ec98 — VIBE_Util_StrCmpThunk (eax=a1, edx=a2 -> ptr-to-ptr).
//   return VIBE_Util_StrCmp(*a2, a1);   // note: deref edx, swapped order.
int Util_StrCmpThunk(const char* a1, const char* const* a2);

// gilde.exe 0x44eca0 — VIBE_Util_StrCmpNoCaseThunk (eax=a1, edx=a2 -> ptr-to-ptr).
//   return VIBE_Util_StrCmpNoCase_Thunk(a1, *a2);
int Util_StrCmpNoCaseThunk(const char* a1, const char* const* a2);

// gilde.exe 0x44f6a4 — VIBE_Util_StrCmpNoCaseDerefThunk (eax=a1, edx=a2; both ptr-to-ptr).
//   return VIBE_Util_StrCmpNoCase_Thunk(*a1, *a2);
int Util_StrCmpNoCaseDerefThunk(const char* const* a1, const char* const* a2);

// --- Forwarder thunks to script-VM leaves -----------------------------------
// The original thunks deref/forward to VIBE_Script_Finish (0x443f38) and
// VIBE_Script_FindByName (0x4421b8), which in this reimplementation take an
// explicit context-table base (host-table boundary). To keep the thunks 1:1
// without re-deriving the engine's global tables, the targets are injected as
// hooks; the thunk control flow (deref of a1, fastcall passthrough) is faithful.
struct ScriptThunkHooks {
    // VIBE_Script_Finish(handle) — original returns void; thunk returns 0.
    void (*scriptFinish)(int handle, void* user) = nullptr;
    // VIBE_Script_FindByName(a1, a2) — original __fastcall passthrough.
    int  (*scriptFindByName)(int a1, int a2, void* user) = nullptr;
    void* user = nullptr;
};

// gilde.exe 0x43c6fc — VIBE_Script_FinishThunk (eax=a1 -> int*).
//   VIBE_Script_Finish(*a1); return 0;
int Script_FinishThunk(const int* a1, const ScriptThunkHooks& hooks);

// gilde.exe 0x43c788 — VIBE_Script_FindByNameThunk (__fastcall a1, a2).
//   return VIBE_Script_FindByName(a1, a2);
int Script_FindByNameThunk(int a1, int a2, const ScriptThunkHooks& hooks);

// --- Trivial no-op / return-0 stubs -----------------------------------------

// gilde.exe 0x5f8224 — VIBE_Util_NullStub. Bare `retn`; an empty cleanup hook
// invoked by File_ReleaseStream/Reopen/Seek/Read and Vfs_CloseAndFreeEntry.
void Util_NullStub();

// gilde.exe 0x5fa620 — VIBE_Util_NullStub2. Bare `retn`; invoked by the CRT exit
// thunk and stored in a callback slot.
void Util_NullStub2();

// gilde.exe 0x527ddc — VIBE_Util_NullSub. Bare `retn`; the inert callback slot
// used by App_RefreshScreensaverSetting / App_InitSubsystemsAndMovieDll.
void Util_NullSub();

// gilde.exe 0x1427cc5 — VIBE_Util_RetZero_27cc5. `return 0;` — the default
// branch of FpException_Raise / File_DispatchControl.
int Util_RetZero_27cc5();

// --- MSVC command-line program-name skip ------------------------------------

// gilde.exe 0x1426221 — VIBE_CmdLine_SkipFirstArg. The MSVC startup helper that
// advances past argv[0] (the program name) in the raw command line and returns
// a pointer to the start of the first real argument. If argv[0] is quoted
// ("...") it scans to the matching quote (with a quirk: an embedded space after
// which the scan still advances), then skips the trailing whitespace run. The
// `isSpace` predicate substitutes VIBE_Locale_IsSpace (@0x14287fe); the original
// also lazily inits the locale ctype table on first use (modeled by the caller).
// Returns a pointer into `cmdline` (one-time-init side effect omitted as CRT).
const char* CmdLine_SkipFirstArg(const char* cmdline, int (*isSpace)(int) = nullptr);

} // namespace guild::sim
