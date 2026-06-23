#pragma once
// =============================================================================
// guild::drm — DiscProtect runtime: buffer/priority/import/orchestration cluster
// =============================================================================
//
// 1:1 reconstruction (user-approved) of the DiscProtect "buffer bookkeeping +
// priority management + obfuscated-import resolution + verify-disc orchestration"
// sub-cluster of gilde.exe's copy-protection layer (the high `0x141xxxx` region).
//
// WHAT IS RECONSTRUCTED EXACTLY (pure logic — in scope, faithful to the binary):
//   * sector/timing-buffer ALLOC/FREE bookkeeping (sizes, counts, list mgmt),
//   * the priority-class and thread-priority NAME LOOKUP TABLES (string maps),
//   * the obfuscated library/import name running-XOR decode (table-walk + transform),
//   * the full ordered import-resolution walk (cleartext + obfuscated names),
//   * the config-select read dispatch (SPTI vs ASPI),
//   * the priority save/restore orchestration,
//   * the OS-version detection / FT_Thunk decision control flow.
//
// WHAT IS ROUTED THROUGH AN INERT-DEFAULT HOOK INTERFACE (DrmOs):
//   Every actual OS coupling — VIBE_Mem_Alloc/Free, LoadLibrary/GetProcAddress,
//   FreeLibrary, GetThreadPriority/SetThreadPriority/GetPriorityClass/
//   SetPriorityClass, GetCurrentThread/GetCurrentProcess, GetLastError,
//   GetVersion, the stdout/stderr logging writes, and the registry probe — is
//   dispatched through DrmOs. The default implementation is inert (succeeds,
//   returns benign values) so the build is headless and depends on no third-party
//   libs. The NAME-LOOKUP tables and the orchestration control flow themselves are
//   NOT hooked: they are reconstructed here verbatim. Nothing is faked.
//
// These are NEW symbols in guild::drm; they do not collide with the pre-existing
// drm_stub.{h,cpp} success-sentinel stubs (different names).
//
// Provenance per reconstructed function is on its definition in the .cpp.
// =============================================================================

#include "guild/common/types.h"

// NOTE: `guild::drm` already hosts an unrelated `DrmOs` struct (drm_control.h) and
// a `DefaultDrmOs()` returning `const DrmDriverOs&` (copyprotect_driver.h). To
// avoid an ODR clash in the single src/** library, this cluster lives in the
// nested namespace `guild::drm::runtime`.
namespace guild::drm::runtime {

// -----------------------------------------------------------------------------
// OS hook interface (DrmOs) — every real OS coupling goes through this. The
// default impl is inert. Tests inject a fake to observe/drive the pure logic.
// Win32 prototypes are reduced to the operands the cluster actually passes.
// -----------------------------------------------------------------------------
struct DrmOs {
    virtual ~DrmOs() = default;

    // --- allocator (VIBE_Mem_Alloc @0x1421b53 / VIBE_Mem_Free @0x1421d8b) ---
    virtual void* Alloc(u32 size)      { return size ? ::operator new(size) : ::operator new(1); }
    virtual void  Free(void* p)        { ::operator delete(p); }

    // --- logging (VIBE_DiscProtect_LogStdout @0x1416100 / LogStderr @0x1416150) ---
    // The originals are printf-style FILE* writes; the reconstruction routes the
    // already-built message text. Default: drop. Tests record into vectors.
    virtual void  LogStdout(const char* /*msg*/) {}
    virtual void  LogStderr(const char* /*msg*/) {}

    // --- thread/process priority (Win32 thunks resolved into globals) ---
    // GetCurrentThread @dword_14676E0 / GetCurrentProcess @dword_14676D4 return
    // opaque pseudo-handles.
    virtual int   GetCurrentThread()              { return -2; } // (HANDLE)-2 pseudo
    virtual int   GetCurrentProcess()             { return -1; } // (HANDLE)-1 pseudo
    // GetThreadPriority @dword_14677C8 returns THREAD_PRIORITY_ERROR_RETURN
    // (0x7FFFFFFF) on failure.
    virtual int   GetThreadPriority(int /*hThread*/)               { return 0; }
    // SetThreadPriority / SetPriorityClass share thunk @dword_14677C4.
    virtual int   SetThreadPriority(int /*hThread*/, int /*prio*/) { return 1; }
    // GetPriorityClass @dword_14677CC returns 0 on failure.
    virtual int   GetPriorityClass(int /*hProcess*/)               { return 0x20; }
    virtual int   SetPriorityClass(int /*hProcess*/, int /*cls*/)  { return 1; }
    // GetLastError @dword_14676F8.
    virtual int   GetLastError()                                   { return 0; }

    // --- GetVersion @dword_145D534 (VIBE_DiscProtect_DetectOsAndThunk) ---
    // Low byte = major, byte1 = minor, high bit set on Win9x/ME (< 0x80000000 == NT).
    // Default: NT-class (e.g. Win2000 = 5.0, build 2195 in high word).
    virtual u32   GetVersion()                                     { return 0x08930005u; }

    // --- import resolution (LoadLibraryA @dword_1467738 thunk,
    //     GetProcAddress @dword_1467718 thunk, FreeLibrary @dword_14676B8) ---
    // Return an opaque module token (non-null) / proc token (non-null) headlessly.
    virtual void* LoadLibraryByName(const char* /*name*/)              { return (void*)0x1; }
    virtual void* GetProcByName(void* mod, const char* /*proc*/)       { return mod ? (void*)0x2 : nullptr; }
    virtual int   FreeLibrary(void* /*mod*/)                           { return 1; }

    // --- registry probe (Win9x branch of DetectOsAndThunk) ---
    // RegOpenKeyExA @dword_145AC68 -> 0 (ERROR_SUCCESS) + handle, else non-zero.
    virtual int   RegOpenKey(const char* /*subkey*/, int* outKey) { if (outKey) *outKey = 0x10; return 1; }
    // RegQueryValueExA @dword_145EB60.
    virtual int   RegQueryValue(int /*key*/, const char* /*name*/) { return 1; }
    // RegCloseKey @dword_145AE7C.
    virtual int   RegCloseKey(int /*key*/) { return 0; }
};

// Inert default singleton.
DrmOs& DefaultDrmOs();

// =============================================================================
// State — the DiscProtect globals this sub-cluster reads/writes. Grouped into one
// struct so the pure logic is testable and re-entrant. Field comments carry the
// original global address.
//
// Buffer model: the binary keeps three base pointers (dword_1464CCC caps,
// dword_1464C64 runVals, dword_1464C60 sampleBufs); each is VIBE_Mem_Alloc'd to
// hold `groupCapacity` (=4) DWORD slots, and slot g points to a per-group array of
// `entryCount[g]` DWORDs. To reproduce the binary's exact alloc/free order, byte
// sizes, and the success predicate `scratch && C60 && CCC && C64`, we hold the
// three base pointers as raw allocations (arrays of `int*`) rather than fixed
// slots.
// =============================================================================
struct DiscProtectState {
    DrmOs* os = &DefaultDrmOs();

    // ---- sector/timing buffer bookkeeping ----
    int  groupCapacity = 4;                 // dword_142C478 (init = 4)
    int  entryCount[4] = {0,0,0,0};         // dword_142C47C[4]

    // Base pointer-arrays (each allocated to hold groupCapacity DWORD slots).
    int**  capsBase   = nullptr;            // dword_1464CCC  (slot g -> int*  of caps)
    int**  runValsBase= nullptr;            // dword_1464C64  (slot g -> int*  of running vals)
    int*** sampBase   = nullptr;            // dword_1464C60  (slot g -> int** of sample bufs)

    void*  scratch360 = nullptr;            // dword_1464CD8 (Alloc(360))
    int    defaultCap = 0;                  // dword_1464C68 (set to 90)
    int    cursor = 0;                      // dword_1464CC8 (set to 0)

    void*  timingTableA = nullptr;          // dword_1455F80
    void*  timingTableB = nullptr;          // dword_1455F84

    // ---- verified / init flags ----
    int  verifiedFlag  = 0;                 // dword_1455F74 (SetVerifiedFlag -> 1)
    int  verifiedFlag2 = 0;                 // dword_142C468 (SetVerifiedFlag -> 1)

    // ---- priority save/restore ----
    int  savedProcessPriority = 0;          // dword_1455E48 (RaisePriorityHigh stores)
    int  savedThreadPriority = 0x7FFFFFFF;  // dword_142C130 (init = 0x7FFFFFFF)

    // ---- config select ----
    int  useSpti = 0;                       // dword_1459FEC (1 => SPTI path)

    // ---- module handles resolved by LoadObfuscatedLibraries ----
    void* hKernel32 = nullptr;              // dword_1459FC8
    void* hUser32   = nullptr;              // dword_1459FBC
    void* hGdi32    = nullptr;              // dword_1459FC0
    void* hWinmm    = nullptr;              // dword_1459FC4
    void* hAdvapi32 = nullptr;              // dword_1459FCC

    // ---- OS detection results (DetectOsAndThunk) ----
    u32  rawVersion = 0;                    // dword_145C500
    int  osMajor = 0;                       // dword_145AC50 (low byte)
    int  osMinor = 0;                       // dword_145F024 (byte1)
    int  osBuild = 0;                       // dword_145CBE0 (high word, NT)
    int  regHasVersion = 0;                 // dword_145A110
    void* ftThunk = nullptr;                // dword_145A118 (FT_Thunk export)
};

// =============================================================================
// Name-lookup tables (string maps) — reconstructed verbatim.
// =============================================================================

// gilde.exe 0x1416190 — VIBE_DiscProtect_PriorityClassName
const char* PriorityClassName(int cls);

// gilde.exe 0x14162e0 — VIBE_DiscProtect_ThreadPriorityName
const char* ThreadPriorityName(int prio);

// =============================================================================
// Obfuscated-name decode (running-XOR transform) — reconstructed verbatim.
// copy n bytes; for(i=n-2; i>=0; --i) out[i] ^= out[i+1]; out[n] = 0.
// Result is the n-char decoded name plus a NUL terminator.
// =============================================================================
void DecodeObfuscatedName(const u8* src, int n, char* out /*>= n+1*/);

// =============================================================================
// Buffer bookkeeping.
// =============================================================================

// gilde.exe 0x14165f0 — VIBE_DiscProtect_CountSectorEntries
// In the binary: for(i=0; dword_142C48C[16*group + i] > 0; ++i); return i;
// The sector map is the fixed global dword_142C48C; we accept it as a parameter so
// the logic is testable (the binary always passes the global).
int CountSectorEntries(const int* sectorMap16xN, int group);

// gilde.exe 0x1417bd0 — VIBE_DiscProtect_AllocSectorBuffers
// Returns true iff scratch360 && sampBase && capsBase && runValsBase were all
// allocated (the binary's exact predicate: dword_1464CD8 && C60 && CCC && C64).
bool AllocSectorBuffers(DiscProtectState& st, const int* sectorMap16xN);

// gilde.exe 0x1417a80 — VIBE_DiscProtect_FreeSectorBuffers
void FreeSectorBuffers(DiscProtectState& st);

// gilde.exe 0x1417880 — VIBE_DiscProtect_FreeTimingTables
void FreeTimingTables(DiscProtectState& st);

// gilde.exe 0x1417d90 — VIBE_DiscProtect_SetVerifiedFlag (sets both flags, ret 1)
int SetVerifiedFlag(DiscProtectState& st);

// =============================================================================
// Priority management.
// =============================================================================

// gilde.exe 0x1416390 — VIBE_DiscProtect_SetThreadPriority
// Returns the *previous* thread priority on success, 0 on failure.
int SetThreadPriority(DiscProtectState& st, int newPriority);

// gilde.exe 0x1416460 — VIBE_DiscProtect_SetProcessPriority
// Returns the *previous* priority class on success, 0 on failure.
int SetProcessPriority(DiscProtectState& st, int newClass);

// gilde.exe 0x1416530 — VIBE_DiscProtect_RaisePriorityHigh (-> class 0x80=128)
int RaisePriorityHigh(DiscProtectState& st);

// gilde.exe 0x1416550 — VIBE_DiscProtect_RestorePriority
int RestorePriority(DiscProtectState& st);

// =============================================================================
// Config-select dispatch.
// =============================================================================

// gilde.exe 0x141b1c0 — VIBE_DiscProtect_ReadConfigSelect
// SPTI path when dword_1459FEC, else ASPI path. We surface the branch decision
// (the real read goes through the sibling DiscIo cluster's I/O hooks).
enum class ReadPath { Spti, Aspi };
ReadPath ReadConfigSelectPath(const DiscProtectState& st);

// =============================================================================
// Import / library resolution & OS detection (table-walk + decode; the actual
// LoadLibrary/GetProcAddress go through DrmOs).
// =============================================================================

// gilde.exe 0x141b230 — VIBE_DiscProtect_LoadObfuscatedLibraries
// De-XOR-decodes the 5 obfuscated DLL names and LoadLibrary's each (via hook),
// storing handles into state. Returns the last handle (advapi32) like the binary.
void* LoadObfuscatedLibraries(DiscProtectState& st);

// The decoded DLL names, in resolution order (kernel32, user32, gdi32, winmm,
// advapi32). Exposed for golden tests.
struct DecodedLibraryNames { char kernel32[16]; char user32[16]; char gdi32[16];
                             char winmm[16]; char advapi32[16]; };
DecodedLibraryNames DecodeLibraryNames();

// gilde.exe 0x141b4e0 — VIBE_DiscProtect_ResolveImports
// Reconstructs the full ordered import walk: plain GetProcAddress for the
// cleartext names plus the running-XOR decode for the obfuscated ones, each
// resolved through the right module handle. Returns the last resolved proc
// (OpenThreadToken). Exposed walk count via ImportEntryCount() for tests.
void* ResolveImports(DiscProtectState& st);

// Test-visible view of the resolution walk (ordered names + module).
int  ImportEntryCount();
// Decode/resolve the i-th import name into `out` (>=64) and return its module
// index 0..4 = K32,U32,GDI,WMM,ADV; -1 if out of range.
int  ImportEntryName(int i, char* out);

// gilde.exe 0x141c270 — VIBE_DiscProtect_DetectOsAndThunk
// GetVersion -> major/minor/build, NT vs 9x decision, and the FT_Thunk export
// probe that may force the SPTI path. Returns (u8)version on the NT path, else
// FreeLibrary's return value on the 9x path — exactly as the binary returns.
int DetectOsAndThunk(DiscProtectState& st);

} // namespace guild::drm::runtime
