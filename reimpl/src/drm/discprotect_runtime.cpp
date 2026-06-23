#include "drm/discprotect_runtime.h"

// =============================================================================
// guild::drm — DiscProtect runtime reconstruction (see discprotect_runtime.h).
//
// 1:1 reconstruction of the pure logic; OS couplings dispatched through DrmOs.
// =============================================================================

namespace guild::drm::runtime {

DrmOs& DefaultDrmOs() {
    static DrmOs inert;
    return inert;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1416190 — VIBE_DiscProtect_PriorityClassName
// Switch over the Win32 PRIORITY_CLASS values; returns the static name string.
//   32 (NORMAL_PRIORITY_CLASS)    -> "normal"    (aNormal   @0x142C144)
//   64 (IDLE_PRIORITY_CLASS)      -> "idle"      (aIdle     @0x142C13C)
//   128 (HIGH_PRIORITY_CLASS)     -> "high"      (aHigh     @0x142C134)
//   256 (REALTIME_PRIORITY_CLASS) -> "real-time" (aRealTime @0x142C14C)
//   default                       -> "unknown"   (aUnknown_1@0x142C158)
// -----------------------------------------------------------------------------
const char* PriorityClassName(int cls) {
    switch (cls) {
        case 32:  return "normal";
        case 64:  return "idle";
        case 128: return "high";
        case 256: return "real-time";
        default:  return "unknown";
    }
}

// -----------------------------------------------------------------------------
// gilde.exe 0x14162e0 — VIBE_DiscProtect_ThreadPriorityName
// Switch over the Win32 THREAD_PRIORITY values; returns the static name string.
//   -15 (IDLE)          -> "idle"          (aIdle_0      @0x142C188)
//    -2 (LOWEST)        -> "lowest"        (aLowest      @0x142C190)
//    -1 (BELOW_NORMAL)  -> "below normal"  (aBelowNormal @0x142C170)
//     0 (NORMAL)        -> "normal"        (aNormal_0    @0x142C198)
//     1 (ABOVE_NORMAL)  -> "above normal"  (aAboveNormal @0x142C160)
//     2 (HIGHEST)       -> "highest"       (aHighest     @0x142C180)
//    15 (TIME_CRITICAL) -> "time critical" (aTimeCritical@0x142C1A0)
//   default             -> "unknown"       (aUnknown_2   @0x142C1B0)
// -----------------------------------------------------------------------------
const char* ThreadPriorityName(int prio) {
    switch (prio) {
        case -15: return "idle";
        case -2:  return "lowest";
        case -1:  return "below normal";
        case 0:   return "normal";
        case 1:   return "above normal";
        case 2:   return "highest";
        case 15:  return "time critical";
        default:  return "unknown";
    }
}

// -----------------------------------------------------------------------------
// Obfuscated-name decode. Mirrors the inline loop used everywhere in
// LoadObfuscatedLibraries / ResolveImports / DetectOsAndThunk:
//   copy n bytes to scratch (aPlaybackletter);
//   for(i = n-2; i >= 0; --i) scratch[i] ^= scratch[i+1];
//   copy n bytes to out (aPlayback); out[n] = 0;
// The source's last encoded byte (index n-1) is left untouched by the running
// XOR and is itself the final decoded character; out[n]=0 terminates.
// -----------------------------------------------------------------------------
void DecodeObfuscatedName(const u8* src, int n, char* out) {
    for (int i = 0; i < n; ++i) out[i] = static_cast<char>(src[i]);
    for (int i = n - 2; i >= 0; --i)
        out[i] = static_cast<char>(static_cast<u8>(out[i]) ^ static_cast<u8>(out[i + 1]));
    out[n] = 0;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x14165f0 — VIBE_DiscProtect_CountSectorEntries
// for ( i = 0; dword_142C48C[16*group + i] > 0; ++i ) ; return i;
// -----------------------------------------------------------------------------
int CountSectorEntries(const int* sectorMap16xN, int group) {
    int i = 0;
    while (sectorMap16xN[16 * group + i] > 0) ++i;
    return i;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1417bd0 — VIBE_DiscProtect_AllocSectorBuffers
//   dword_1464CCC = Alloc(4 * dword_142C478);   // caps base   (groupCapacity ptrs)
//   dword_1464C64 = Alloc(4 * dword_142C478);   // runVals base
//   dword_1464C60 = Alloc(4 * dword_142C478);   // sampleBufs base
//   for i in 0..3:
//     dword_142C47C[i] = CountSectorEntries(i);
//     CCC[i] = Alloc(4 * cnt); C64[i] = Alloc(4 * cnt); C60[i] = Alloc(4 * cnt);
//     for j in 0..cnt-1: CCC[i][j]=90; C64[i][j]=0; C60[i][j]=Alloc(4 * CCC[i][j]);
//   dword_1464C68 = 90; dword_1464CC8 = 0; dword_1464CD8 = Alloc(360);
//   return dword_1464CD8 && dword_1464C60 && dword_1464CCC && dword_1464C64;
// -----------------------------------------------------------------------------
bool AllocSectorBuffers(DiscProtectState& st, const int* sectorMap16xN) {
    DrmOs& os = *st.os;
    // The original alloc'd 4*groupCapacity bytes per base array (32-bit pointer
    // slots). Pointers/int arrays are also stored in the per-group arrays. On a
    // 64-bit host a pointer is 8 bytes, so pointer-typed arrays are sized by
    // element count * sizeof(slot) (per types.h: do not assume sizeof(ptr)==4);
    // int-typed arrays keep the byte-exact 4*count. The element COUNT and the
    // alloc/free order/bookkeeping are identical to the binary.
    const u32 baseSlots = static_cast<u32>(st.groupCapacity);
    st.capsBase    = static_cast<int**>(os.Alloc(baseSlots * static_cast<u32>(sizeof(int*))));   // dword_1464CCC (4*gc bytes @x86)
    st.runValsBase = static_cast<int**>(os.Alloc(baseSlots * static_cast<u32>(sizeof(int*))));   // dword_1464C64
    st.sampBase    = static_cast<int***>(os.Alloc(baseSlots * static_cast<u32>(sizeof(int**))));  // dword_1464C60
    for (int i = 0; i < 4; ++i) {
        st.entryCount[i] = CountSectorEntries(sectorMap16xN, i);  // dword_142C47C[i]
        int cnt = st.entryCount[i];
        st.capsBase[i]    = static_cast<int*>(os.Alloc(4u * static_cast<u32>(cnt)));                 // ints: byte-exact
        st.runValsBase[i] = static_cast<int*>(os.Alloc(4u * static_cast<u32>(cnt)));                 // ints: byte-exact
        st.sampBase[i]    = static_cast<int**>(os.Alloc(static_cast<u32>(cnt) * static_cast<u32>(sizeof(int*)))); // ptrs
        for (int j = 0; j < cnt; ++j) {
            st.capsBase[i][j]    = 90;
            st.runValsBase[i][j] = 0;
            st.sampBase[i][j]    = static_cast<int*>(os.Alloc(4u * static_cast<u32>(st.capsBase[i][j])));
        }
    }
    st.defaultCap = 90;             // dword_1464C68 = 90
    st.cursor = 0;                  // dword_1464CC8 = 0
    st.scratch360 = os.Alloc(360);  // dword_1464CD8 = Alloc(360)

    return st.scratch360 != nullptr && st.sampBase != nullptr
        && st.capsBase != nullptr && st.runValsBase != nullptr;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1417a80 — VIBE_DiscProtect_FreeSectorBuffers
//   Free(dword_1464CD8); = 0;
//   for i in 0..3:
//     for j in 0..entryCount[i]-1: Free(C60[i][j]); C60[i][j] = 0;
//     Free(C60[i]); =0; Free(C64[i]); =0; Free(CCC[i]); =0;
//   Free(dword_1464C60); =0; Free(dword_1464C64); =0; Free(dword_1464CCC); =0;
// -----------------------------------------------------------------------------
void FreeSectorBuffers(DiscProtectState& st) {
    DrmOs& os = *st.os;
    os.Free(st.scratch360);
    st.scratch360 = nullptr;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < st.entryCount[i]; ++j) {
            os.Free(st.sampBase[i][j]);
            st.sampBase[i][j] = nullptr;
        }
        os.Free(st.sampBase[i]);    st.sampBase[i]    = nullptr;
        os.Free(st.runValsBase[i]); st.runValsBase[i] = nullptr;
        os.Free(st.capsBase[i]);    st.capsBase[i]    = nullptr;
    }
    os.Free(st.sampBase);    st.sampBase    = nullptr;
    os.Free(st.runValsBase); st.runValsBase = nullptr;
    os.Free(st.capsBase);    st.capsBase    = nullptr;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1417880 — VIBE_DiscProtect_FreeTimingTables
// Free(dword_1455F80); =0; Free(dword_1455F84); =0;
// -----------------------------------------------------------------------------
void FreeTimingTables(DiscProtectState& st) {
    DrmOs& os = *st.os;
    os.Free(st.timingTableA); st.timingTableA = nullptr;
    os.Free(st.timingTableB); st.timingTableB = nullptr;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1417d90 — VIBE_DiscProtect_SetVerifiedFlag
// dword_1455F74 = 1; dword_142C468 = 1; return 1;
// -----------------------------------------------------------------------------
int SetVerifiedFlag(DiscProtectState& st) {
    st.verifiedFlag  = 1;
    st.verifiedFlag2 = 1;
    return 1;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1416390 — VIBE_DiscProtect_SetThreadPriority
//   h = GetCurrentThread(); prev = GetThreadPriority(h);
//   if (prev == 0x7FFFFFFF) { LogStderr "failed to get thread priority (1)..."; return 0; }
//   LogStdout "running at thread priority <name(prev)>"
//   if (SetThreadPriority(h, new)) {
//       cur = GetThreadPriority(h);
//       // NOTE: the binary re-tests `prev == 0x7FFFFFFF` here (a known quirk),
//       // not `cur`. Reproduced verbatim (0x1416425 tests v7, not v9).
//       if (prev == 0x7FFFFFFF) { LogStderr "(2)..."; return 0; }
//       LogStdout "now running at thread priority <name(cur)>"; return prev;
//   } else { LogStderr "failed to set thread priority..."; return 0; }
// -----------------------------------------------------------------------------
int SetThreadPriority(DiscProtectState& st, int newPriority) {
    DrmOs& os = *st.os;
    int h = os.GetCurrentThread();
    int prev = os.GetThreadPriority(h);
    if (prev == 0x7FFFFFFF) {
        os.LogStderr("failed to get thread priority (1), err=%d\n");
        return 0;
    }
    os.LogStdout("running at thread priority %s\n");
    if (os.SetThreadPriority(h, newPriority)) {
        int cur = os.GetThreadPriority(h);
        if (prev == 0x7FFFFFFF) {            // verbatim quirk: tests `prev`, not `cur`
            os.LogStderr("failed to get thread priority (2), err=%d\n");
            return 0;
        }
        (void)cur;
        os.LogStdout("now running at thread priority %s\n");
        return prev;
    }
    os.LogStderr("failed to set thread priority, err=%d\n");
    return 0;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1416460 — VIBE_DiscProtect_SetProcessPriority
//   h = GetCurrentProcess(); prev = GetPriorityClass(h);
//   if (!prev) { LogStderr "failed to get process priority class (1)..."; return 0; }
//   LogStdout "running priority class <name(prev)>"
//   if (SetPriorityClass(h, new)) {
//       cur = GetPriorityClass(h);
//       if (cur) { LogStdout "now running at priority class <name(cur)>"; return prev; }
//       else     { LogStderr "(2)..."; return 0; }
//   } else { LogStderr "failed to set process priority class..."; return 0; }
// -----------------------------------------------------------------------------
int SetProcessPriority(DiscProtectState& st, int newClass) {
    DrmOs& os = *st.os;
    int h = os.GetCurrentProcess();
    int prev = os.GetPriorityClass(h);
    if (!prev) {
        os.LogStderr("failed to get process priority class (1), err=%d\n");
        return 0;
    }
    os.LogStdout("running priority class %s\n");
    if (os.SetPriorityClass(h, newClass)) {
        int cur = os.GetPriorityClass(h);
        if (cur) {
            os.LogStdout("now running at priority class %s\n");
            return prev;
        }
        os.LogStderr("failed to get process priority class (2), err=%d\n");
        return 0;
    }
    os.LogStderr("failed to set process priority class, err=%d\n");
    return 0;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1416530 — VIBE_DiscProtect_RaisePriorityHigh
// dword_1455E48 = SetProcessPriority(128); return it;
// -----------------------------------------------------------------------------
int RaisePriorityHigh(DiscProtectState& st) {
    int r = SetProcessPriority(st, 128);
    st.savedProcessPriority = r;
    return r;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1416550 — VIBE_DiscProtect_RestorePriority
//   if (dword_1455E48) SetProcessPriority(dword_1455E48); else SetProcessPriority(128);
//   if (dword_142C130 == 0x7FFFFFFF) return SetThreadPriority(0);
//   else return SetThreadPriority(dword_142C130);
// -----------------------------------------------------------------------------
int RestorePriority(DiscProtectState& st) {
    if (st.savedProcessPriority)
        SetProcessPriority(st, st.savedProcessPriority);
    else
        SetProcessPriority(st, 128);

    if (st.savedThreadPriority == 0x7FFFFFFF)
        return SetThreadPriority(st, 0);
    return SetThreadPriority(st, st.savedThreadPriority);
}

// -----------------------------------------------------------------------------
// gilde.exe 0x141b1c0 — VIBE_DiscProtect_ReadConfigSelect
//   if (dword_1459FEC) return VIBE_Disc_SptiReadSectorEcc(...);
//   else               return VIBE_Disc_AspiReadSectorEcc(...);
// We surface the branch decision (the real read goes through the disc I/O hooks
// owned by the sibling DiscIo cluster).
// -----------------------------------------------------------------------------
ReadPath ReadConfigSelectPath(const DiscProtectState& st) {
    return st.useSpti ? ReadPath::Spti : ReadPath::Aspi;
}

// =============================================================================
// Obfuscated DLL-name source tables (verbatim from .data; running-XOR encoded;
// n = copy length). Decoded by LoadObfuscatedLibraries.
// =============================================================================
namespace {
// byte_1451CA8 (n=0xC) -> "kernel32.dll"
const u8 kKernel32Enc[] = {0x0e,0x17,0x1c,0x0b,0x09,0x5f,0x01,0x1c,0x4a,0x08,0x00,0x6c};
// byte_1451C9C (n=0xA) -> "user32.dll"
const u8 kUser32Enc[]   = {0x06,0x16,0x17,0x41,0x01,0x1c,0x4a,0x08,0x00,0x6c};
// byte_1451C90 (n=9)   -> "gdi32.dll"
const u8 kGdi32Enc[]    = {0x03,0x0d,0x5a,0x01,0x1c,0x4a,0x08,0x00,0x6c};
// byte_1451C84 (n=9)   -> "winmm.dll"
const u8 kWinmmEnc[]    = {0x1e,0x07,0x03,0x00,0x43,0x4a,0x08,0x00,0x6c};
// byte_1451C74 (n=0xC) -> "advapi32.dll"
const u8 kAdvapi32Enc[] = {0x05,0x12,0x17,0x11,0x19,0x5a,0x01,0x1c,0x4a,0x08,0x00,0x6c};
} // namespace

DecodedLibraryNames DecodeLibraryNames() {
    DecodedLibraryNames d{};
    DecodeObfuscatedName(kKernel32Enc, 0xC, d.kernel32);
    DecodeObfuscatedName(kUser32Enc,   0xA, d.user32);
    DecodeObfuscatedName(kGdi32Enc,    0x9, d.gdi32);
    DecodeObfuscatedName(kWinmmEnc,    0x9, d.winmm);
    DecodeObfuscatedName(kAdvapi32Enc, 0xC, d.advapi32);
    return d;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x141b230 — VIBE_DiscProtect_LoadObfuscatedLibraries
// Decode each obfuscated DLL name in place, LoadLibrary it, store the handle.
// Order in the binary: kernel32, user32, gdi32, winmm, advapi32. Returns advapi32.
// -----------------------------------------------------------------------------
void* LoadObfuscatedLibraries(DiscProtectState& st) {
    DrmOs& os = *st.os;
    DecodedLibraryNames d = DecodeLibraryNames();
    st.hKernel32 = os.LoadLibraryByName(d.kernel32); // dword_1459FC8
    st.hUser32   = os.LoadLibraryByName(d.user32);   // dword_1459FBC
    st.hGdi32    = os.LoadLibraryByName(d.gdi32);    // dword_1459FC0
    st.hWinmm    = os.LoadLibraryByName(d.winmm);    // dword_1459FC4
    st.hAdvapi32 = os.LoadLibraryByName(d.advapi32); // dword_1459FCC
    return st.hAdvapi32;
}

// =============================================================================
// ResolveImports name table. The binary (0x141b4e0) resolves the imports in the
// exact order below: a kernel32 block (cleartext + a few obfuscated), then user32,
// winmm, and the advapi32 registry/SID run. Each obfuscated entry carries its
// running-XOR source bytes + length; each plain entry the literal name. The module
// handle used is the one the binary passes at that call site.
// =============================================================================
namespace {

enum Mod { K32, U32, GDI, WMM, ADV };

struct ImportEntry {
    Mod         mod;
    bool        obf;        // true => name is XOR-encoded (enc/encLen), else literal
    const char* literal;    // for obf == false
    const u8*   enc;        // for obf == true
    int         encLen;     // copy length n
};

// --- obfuscated source tables (verbatim, byte-exact from .data) ---
const u8 e_DeviceIoControl[]    = {0x21,0x13,0x1f,0x0a,0x06,0x2c,0x26,0x2c,0x2c,0x01,0x1a,0x06,0x1d,0x03,0x6c};                          // 0xF  byte_145202C
const u8 e_GetLocalTime[]       = {0x22,0x11,0x38,0x23,0x0c,0x02,0x0d,0x38,0x3d,0x04,0x08,0x65};                                         // 0xC  byte_1451F44
const u8 e_ReadProcessMemory[]  = {0x37,0x04,0x05,0x34,0x22,0x1d,0x0c,0x06,0x16,0x00,0x3e,0x28,0x08,0x02,0x1d,0x0b,0x79};                // 0x11 byte_1451E88
const u8 e_VirtualProtect[]     = {0x3f,0x1b,0x06,0x01,0x14,0x0d,0x3c,0x22,0x1d,0x1b,0x11,0x06,0x17,0x74};                               // 0xE  byte_1451E28
const u8 e_WriteProcessMemory[] = {0x25,0x1b,0x1d,0x11,0x35,0x22,0x1d,0x0c,0x06,0x16,0x00,0x3e,0x28,0x08,0x02,0x1d,0x0b,0x79};           // 0x12 byte_1451DF4
const u8 e_CallNamedPipeA[]     = {0x22,0x0d,0x00,0x22,0x2f,0x0c,0x08,0x01,0x34,0x39,0x19,0x15,0x24,0x41};                               // 0xE  asc_1451DE4
const u8 e_timeGetTime[]        = {0x1d,0x04,0x08,0x22,0x22,0x11,0x20,0x3d,0x04,0x08,0x65};                                              // 0xB  byte_1451D98
const u8 e_RegCloseKey[]        = {0x37,0x02,0x24,0x2f,0x03,0x1c,0x16,0x2e,0x2e,0x1c,0x79};                                              // 0xB  byte_1451D7C
const u8 e_RegEnumKeyExA[]      = {0x37,0x02,0x22,0x2b,0x1b,0x18,0x26,0x2e,0x1c,0x3c,0x3d,0x39,0x41};                                    // 0xD  byte_1451D6C
const u8 e_RegOpenKeyExA[]      = {0x37,0x02,0x28,0x3f,0x15,0x0b,0x25,0x2e,0x1c,0x3c,0x3d,0x39,0x41};                                    // 0xD  byte_1451D5C
const u8 e_RegQueryValueExA[]   = {0x37,0x02,0x36,0x24,0x10,0x17,0x0b,0x2f,0x37,0x0d,0x19,0x10,0x20,0x3d,0x39,0x41};                     // 0x10 byte_1451D48
const u8 e_RegSetValueExA[]     = {0x37,0x02,0x34,0x36,0x11,0x22,0x37,0x0d,0x19,0x10,0x20,0x3d,0x39,0x41};                               // 0xE  byte_1451D38
const u8 e_RegDeleteValueA[]    = {0x37,0x02,0x23,0x21,0x09,0x09,0x11,0x11,0x33,0x37,0x0d,0x19,0x10,0x24,0x41};                          // 0xF  byte_1451D28
const u8 e_AllocateAndInitializeSid[] = {0x2d,0x00,0x03,0x0c,0x02,0x15,0x11,0x24,0x2f,0x0a,0x2d,0x27,0x07,0x1d,0x1d,0x08,0x0d,0x05,0x13,0x1f,0x36,0x3a,0x0d,0x64}; // 0x18 byte_1451CFC
const u8 e_EqualSid[]           = {0x34,0x04,0x14,0x0d,0x3f,0x3a,0x0d,0x64};                                                            // 8    byte_1451CF0
const u8 e_GetTokenInformation[]= {0x22,0x11,0x20,0x3b,0x04,0x0e,0x0b,0x27,0x27,0x08,0x09,0x1d,0x1f,0x0c,0x15,0x1d,0x06,0x01,0x6e};      // 0x13 byte_1451CDC
const u8 e_OpenProcessToken[]   = {0x3f,0x15,0x0b,0x3e,0x22,0x1d,0x0c,0x06,0x16,0x00,0x27,0x3b,0x04,0x0e,0x0b,0x6e};                     // 0x10 byte_1451CC8
const u8 e_OpenThreadToken[]    = {0x3f,0x15,0x0b,0x3a,0x3c,0x1a,0x17,0x04,0x05,0x30,0x3b,0x04,0x0e,0x0b,0x6e};                          // 0xF  byte_1451CB8

#define P(m, lit)    { m, false, lit, nullptr, 0 }
#define O(m, arr, n) { m, true, nullptr, arr, n }

// Ordered exactly as ResolveImports walks (0x141b4e0).
const ImportEntry kImports[] = {
    // -- kernel32 (dword_1459FC8) --
    P(K32, "CloseHandle"), P(K32, "CreateEventA"), P(K32, "CreateFileA"),
    P(K32, "CreateMutexA"), P(K32, "CreateProcessA"), P(K32, "SetErrorMode"),
    O(K32, e_DeviceIoControl, 0xF),
    P(K32, "EnterCriticalSection"), P(K32, "FreeLibrary"), P(K32, "FlushFileBuffers"),
    P(K32, "GetCommandLineA"), P(K32, "GetCurrentProcess"), P(K32, "GetCurrentProcessId"),
    P(K32, "GetCurrentThread"), P(K32, "GetCurrentThreadId"), P(K32, "GetDiskFreeSpaceA"),
    P(K32, "GetDriveTypeA"), P(K32, "GetFileSize"), P(K32, "GetLastError"),
    O(K32, e_GetLocalTime, 0xC),
    P(K32, "GetModuleFileNameA"), P(K32, "GetModuleHandleA"), P(K32, "GetSystemDirectoryA"),
    P(K32, "GetVersion"), P(K32, "GetTickCount"), P(K32, "GetTempPathA"),
    P(K32, "InitializeCriticalSection"), P(K32, "LeaveCriticalSection"), P(K32, "ReadFile"),
    O(K32, e_ReadProcessMemory, 0x11),
    P(K32, "ReleaseMutex"), P(K32, "SetFileAttributesA"), P(K32, "SetFilePointer"),
    P(K32, "SetUnhandledExceptionFilter"),
    O(K32, e_VirtualProtect, 0xE),
    P(K32, "WaitForSingleObject"), P(K32, "WriteFile"),
    O(K32, e_WriteProcessMemory, 0x12),
    O(K32, e_CallNamedPipeA, 0xE),
    P(K32, "OpenEventA"), P(K32, "SetEvent"), P(K32, "ResetEvent"),
    // -- user32 (dword_1459FBC) --
    P(U32, "PostMessageA"), P(U32, "MessageBoxA"),
    // -- winmm (dword_1459FC4) --
    O(WMM, e_timeGetTime, 0xB),
    P(WMM, "timeSetEvent"),
    // -- advapi32 (dword_1459FCC) --
    O(ADV, e_RegCloseKey, 0xB),
    O(ADV, e_RegEnumKeyExA, 0xD),
    O(ADV, e_RegOpenKeyExA, 0xD),
    O(ADV, e_RegQueryValueExA, 0x10),
    O(ADV, e_RegSetValueExA, 0xE),
    O(ADV, e_RegDeleteValueA, 0xF),     // byte_1451D28 (0x141bf88) — precedes RegCreateKeyExA
    P(ADV, "RegCreateKeyExA"),          // aRegcreatekeyex (0x141bf9e)
    O(ADV, e_AllocateAndInitializeSid, 0x18),
    O(ADV, e_EqualSid, 8),
    O(ADV, e_GetTokenInformation, 0x13),
    O(ADV, e_OpenProcessToken, 0x10),
    O(ADV, e_OpenThreadToken, 0xF),
};
#undef P
#undef O

const int kImportCount = static_cast<int>(sizeof(kImports) / sizeof(kImports[0]));

void* moduleHandle(const DiscProtectState& st, Mod m) {
    switch (m) {
        case K32: return st.hKernel32;
        case U32: return st.hUser32;
        case GDI: return st.hGdi32;
        case WMM: return st.hWinmm;
        case ADV: return st.hAdvapi32;
    }
    return nullptr;
}

} // namespace

int ImportEntryCount() { return kImportCount; }

int ImportEntryName(int i, char* out) {
    if (i < 0 || i >= kImportCount) return -1;
    const ImportEntry& e = kImports[i];
    if (e.obf) {
        DecodeObfuscatedName(e.enc, e.encLen, out);
    } else {
        int k = 0;
        for (; e.literal[k]; ++k) out[k] = e.literal[k];
        out[k] = 0;
    }
    return static_cast<int>(e.mod);
}

// -----------------------------------------------------------------------------
// gilde.exe 0x141b4e0 — VIBE_DiscProtect_ResolveImports
// Walk the ordered import table: plain entries resolve by literal name; obfuscated
// entries de-XOR the source bytes first. Each resolves against the module handle
// the binary uses. Returns the last resolved proc (OpenThreadToken @dword_145E1E8).
// -----------------------------------------------------------------------------
void* ResolveImports(DiscProtectState& st) {
    DrmOs& os = *st.os;
    void* last = nullptr;
    char name[64];
    for (int i = 0; i < kImportCount; ++i) {
        const ImportEntry& e = kImports[i];
        const char* n;
        if (e.obf) {
            DecodeObfuscatedName(e.enc, e.encLen, name);
            n = name;
        } else {
            n = e.literal;
        }
        last = os.GetProcByName(moduleHandle(st, e.mod), n);
    }
    return last;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x141c270 — VIBE_DiscProtect_DetectOsAndThunk
//   dword_145C500 = GetVersion();
//   result = (u8)dword_145C500;                 // <- low byte is the return seed
//   dword_145AC50 = (u8)dword_145C500;          // osMajor
//   dword_145F024 = BYTE1(dword_145C500);       // osMinor
//   if (dword_145C500 < 0x80000000) dword_1459FEC = 1;     // NT-class
//   if (dword_1459FEC) {
//       dword_145CBE0 = HIWORD(dword_145C500);  // NT build number
//   } else {  // Win9x/ME branch: registry probe + FT_Thunk export check
//       if (!RegOpenKeyExA(HKLM, "Software\\Microsoft\\Windows NT\\CurrentVersion", ...)) {
//           dword_145A108 = 32;
//           if (!RegQueryValueExA(key, "CurrentVersion", ...)) dword_145A110 = 1;
//           RegCloseKey(key);
//       }
//       dword_145A114 = LoadLibrary("kernel32.dll");
//       dword_145A118 = GetProcAddress(hK32, "FT_Thunk");
//       result = FreeLibrary(hK32);             // <- 9x path returns FreeLibrary's value
//       if (dword_145A110 && !dword_145A118) dword_1459FEC = 1;
//   }
//   return result;
// -----------------------------------------------------------------------------
int DetectOsAndThunk(DiscProtectState& st) {
    DrmOs& os = *st.os;
    u32 ver = os.GetVersion();
    st.rawVersion = ver;                        // dword_145C500
    int result = static_cast<int>(ver & 0xFF);  // (u8)version — the return seed
    st.osMajor = static_cast<int>(ver & 0xFF);  // dword_145AC50
    st.osMinor = static_cast<int>((ver >> 8) & 0xFF); // dword_145F024
    if (ver < 0x80000000u)
        st.useSpti = 1;                         // dword_1459FEC = 1 (NT)
    if (st.useSpti) {
        st.osBuild = static_cast<int>((ver >> 16) & 0xFFFF); // HIWORD -> dword_145CBE0
    } else {
        st.regHasVersion = 0;                   // dword_145A110
        int key = 0;
        // RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", ...)
        if (os.RegOpenKey("Software\\Microsoft\\Windows NT\\CurrentVersion", &key) == 0) {
            if (os.RegQueryValue(key, "CurrentVersion") == 0)
                st.regHasVersion = 1;           // dword_145A110 = 1
            os.RegCloseKey(key);
        }
        void* hK32 = os.LoadLibraryByName("kernel32.dll"); // dword_145A114
        st.ftThunk = os.GetProcByName(hK32, "FT_Thunk");    // dword_145A118
        result = os.FreeLibrary(hK32);          // result = FreeLibrary(hK32)
        if (st.regHasVersion && !st.ftThunk)
            st.useSpti = 1;                     // dword_1459FEC = 1
    }
    return result;
}

} // namespace guild::drm::runtime
