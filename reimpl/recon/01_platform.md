# Recon Report 01 — Platform / Runtime / CRT / Low-Level Support Cluster

Binary: `gilde.exe` ("Die Gilde" / "The Guild"), 32-bit x86, imagebase 0x400000, 5410 named functions.
Cluster owner: platform / runtime / CRT / low-level support layer.
Convention: `VIBE_<Module>_<Action>`.

This binary is statically linked against an early-2000s **Microsoft Visual C++ CRT** (the `__usercall`/register-passing prototypes, the `_DWORD dword_1465044` heap-mode global, the `msvcrt heap select`/`global heap select` strings, the `__p__environ`/`_setargv` patterns). Most of this cluster is a 1:1 reimplementation of that CRT, plus a vendored **zlib 1.x** (deflate/inflate/gzip/unzip), an **MD5** implementation, a **CRC/CRC16**, a **color-quantizer** (PNG/GIF-style palette reduction), and a thin set of game-facing helpers. The registry root is `Software\Ahead Entertainment\` (publisher of the German release).

IMPORTANT SCOPE NOTE — prefix collisions with other clusters:
Several prefixes assigned to me are actually **game logic**, not platform code, and should be handed to the gameplay/engine clusters:
- `VIBE_He_*` (≈45 funcs @ 0x4c3a78–0x537358): entity/"handler" gameplay system (player news, punishment/fines, icon meshes, NPC handlers). GAME.
- `VIBE_HandlerEntry_*` (≈24 funcs @ 0x4c9d3c+): per-action gameplay state-machine init/queue routines. GAME.
- `VIBE_Hotkey_*`, `VIBE_Input_*` (DirectInput mouse/keyboard polling, icon/field UI): ENGINE/UI.
- `VIBE_App_*`, `VIBE_Game_*`: top-level init/shutdown orchestration. ENGINE.
- `VIBE_Resource_*`, `VIBE_Table_*` (lightmaps), `VIBE_Result_*` (interaction broadcast), `VIBE_Color_*`, `VIBE_Guid_*`: ENGINE/UTIL boundary.
- `VIBE_DecompressGameState` / `VIBE_DecompressState_Blob` / `VIBE_Decompression_Finalize` (@0x41ceb4, 0x423500, 0x4235dc): game savegame/state decompression that *consumes* my zlib/inflate; the wrapper logic is game-specific, the inner `VIBE_Decompress_InflateFast` (@0x60a1f0) is zlib.

The genuine platform cluster below is overwhelmingly **standard-library reimplementation**; treat it as a CRT/zlib port rather than original code.

---

## 1. Subsystem overview

### CRT core (`Crt`, `Printf`, `Format`, `FloatFormat`, `Stdio`, `VaArg`, `Buffer`) — STD-LIB
Reimplementation of the MSVC CRT: process startup/teardown (`VIBE_Crt_Entry`/`Runtime_Entry`, `_initterm`-style `RunInitFuncs`/`RunExitFuncs`, `atexit`, `exit`/`_exit`), `printf`/`sprintf`/`vsprintf`/`_snprintf` family (`Crt_FormatStringCore` + `Crt_FormatConversion` + `Printf_Output`), `scanf`/`sscanf` family (`Crt_ScanFormatCore` and the `Crt_ScanRead*` helpers), buffered FILE I/O (`Crt_PutcBuffered`, `Crt_FillReadBuffer`, `Crt_FlushStream*`, `Crt_OpenFile`, `Crt_FileRead`, `Stdio_AllocBuffer`), assertion/abort/structured-exception reporting (`Crt_ReportAssertion`, `Crt_UnhandledExceptionFilter`, `Crt_StructuredExceptionHandler`, `Crt_ReportRuntimeError`), stack probe (`Crt__chkstk` = `Crt_StackProbe`), time (`Crt_LocalTime`, `Crt_MakeTimeFromTm`, `Crt_Asctime`/`Ctime`).

### Numeric conversion (`Strtol`, `Strtoul`, `Float`, `Fp`, `Fpu`, `FpException`, `Math`, `Rand`) — STD-LIB + game RNG
- `Strtol_Parse`/`Strtoul`/`Crt_StrToLong`/`Crt_Atoi`/`Util_ParseInt`/`Util_StrToDouble`: integer/float parsing.
- `Float_*64` (@0x6096a8+): software 64-bit float emul (`__adddf3`/`__muldf3`/`__divdf3`-style helpers + exception record building) — present because the binary supports a no-FPU fallback path (`Cpu_DetectFpFallback`, `Fpu_DetectFeatures`).
- `Fpu_*`/`FpException_*`/`Fp_*`: FPU control-word save/restore, NaN normalization, matherr dispatch. **FPU control-word juggling present** (see risks).
- `Math_Random*` and `Rand_*`/`Util_Rand*`: the game RNG. `Rand_Next` is the classic `state = state*214013 + 2531011` LCG (`Rand_SetSeed`/`Rand_Next` @0x1422144/0x142214e). `Util_RandNext` (@0x5cb8bc, 63 callers) and `Math_RandomModulo` (@0x58b89c, 344 callers) are the hot game-facing RNG wrappers — GAME-adjacent but small.

### Memory (`Memory`, `Mem`, `MemPool`, `Heap`) — STD-LIB heap + game debug tracker
Two distinct layers:
1. **CRT heap** (`Mem_*` / `Heap_*`): a malloc/free/realloc over `VirtualAlloc`/`VirtualFree` (NO `HeapAlloc` import). Mode global `dword_1465044` selects: 2 = small-block segment allocator (`Heap_AllocFromSegment`/`InitSmallBlock`), 3 = mixed, else direct virtual commit (rounded to 16 bytes). `Heap_SelectGlobalMode` reads the `GLOBAL`/`MSVCRT` heap-select env strings. `Mem_MoveOverlapping` = memmove, `Mem_Set` = memset, `Mem_Compare` = memcmp.
2. **Game debug allocator** (`Memory_*` @0x438f10+): a tracking wrapper (`m_alloc_debug`) over `Memory_AllocFromFreeList`. Tags each allocation with a "GroupInformation" string (`name:`), keeps per-group + global counters (peak bytes/count), and writes 8-byte `0xDEADBEEF` (= -559038737) guard words before/after each block. `Memory_AllocFromFreeList`/`ReturnToFreeList` is a segregated free-list over `Memory_HeapAllocBlock`/`GrowHeapDefault`, guarded by a lock (`off_64A928`/`off_64A930`). This is GAME-side instrumentation but lives in the platform layer.
3. **MemPool** (`MemPool_*` @0x439778+): fixed-block pool allocator with used/free chains + stats; plus `MemPool_StartupStack`/`ShutdownStack` for a startup scratch stack.

### Threading / sync / TLS / signals (`Thread`, `Lock`, `Tls`, `Crt`Tls, `Signal`, `Eh`) — STD-LIB
- `Thread_BeginThread`/`StartRoutine`/`EndThread`: `_beginthread`-style over CreateThread.
- `Lock_*`: CRT lock table — static + dynamically-allocated `CRITICAL_SECTION`s, recursive acquire/release (`Lock_AcquireRecursive`), plus many tiny per-lock thunks (multithreaded-CRT lock id table).
- `Tls_*` (@0x608a60+) and `Crt_Tls*` (@0x604858+): two TLS layers — the CRT per-thread data block (`Crt_TlsAllocIndex` over `TlsAlloc`, `dwTlsIndex` @0x64a908, thread-data init/free, `TlsInstallCallbacks`) and a higher per-thread resizable buffer set (`Tls_ResizeThreadData`/`RegisterThreadData`).
- `Signal_*`: ANSI `signal`/`raise` table (SIGINT/SIGTERM/etc.) + a console Ctrl handler.
- `Eh_*` (@0x142689e+): MSVC SEH frame walking (`__except_handler`-style `Eh_RunFrameHandlers`, `Eh_UnwindNestedHandlers`, `Eh_SaveRegistration`).

### Config / Registry / CmdLine / Env (`Registry`, `Config`, `CmdLine`, `Env`) — mostly STD-LIB + game settings
- `Registry_*` (@0x40c420+): thin `RegOpenKeyExA`/`RegQueryValueExA`/`RegSetValueExA` wrappers; key root `Software\Ahead Entertainment\`; typed get/set for DWORD/string/float.
- `Config_*` (@0x56af54+): GAME-specific graphics/sound/camera settings reader/writer (90+ string refs each) built on `Registry_*`. GAME boundary.
- `CmdLine_*`, `Env_*`: CRT `__getmainargs`/`environ` machinery — argv parsing (`CmdLine_Parse`), ANSI+Unicode environment block management (`Env_PutEnvAnsi`/`PutEnvWide`/`BuildTable`/`SyncWideToAnsi`), `_putenv`/`SetEnvironmentVariableW`.

### Strings / locale / MBCS (`String`, `Util`, `Mbcs`, `Locale`) — STD-LIB
- `String_*` and `Util_*`: strlen/strcpy/strcat/strcmp/stricmp/strncmp/strstr/strchr/strupr/strlwr, itoa/`_ui64toa`, parse helpers, qsort/bsearch (`Util_QuickSort` @0x5ea068, `Sort_QuickSort` @0x142176a, `Util_BinarySearch`), memmove, Fisher-Yates shuffles (`Util_InitAndShuffle*Array`). `Util` also hosts CRC32 (`Util_Crc32`), CRC16 table build, tick-based RNG seed, byte rotate.
- `Mbcs_*`: MSVC `_mbs*` multibyte (lead-byte/DBCS, JIS/Kanji aware) — full `_ismbblead`/`_mbsupr`/`_mbschr`/`_mbsicmp` set.
- `Locale_*`: codepage setup, `MultiByteToWideChar` adapters, ctype-table build, `tolower`/`toupper`, plus timezone/DST machinery (`Locale_TimezoneInit`, `Locale_ApplyDstRules`).

### Time (`Time`, `TimeBase`) — STD-LIB + multimedia timer
- `Time_*`: CRT time/gmtime/localtime/mktime, leap-year, DST transition, `tzset`/TZ parsing, `GetLocalTime`-backed `Time_GetLocalTime`.
- `TimeBase_*` (@0x44e240+): WINMM multimedia-timer service (`timeSetEvent`/`timeKillEvent`/`timeBeginPeriod`) with a small registered-proc table (interval callbacks). Used by the game tick loop — ENGINE-adjacent but self-contained.

### Hashing & compression (`Md5`, `Crc`, `Crc16`, `Zlib`, `Deflate`, `Inflate`, `Gzip`, `Zip`, `Quant`, `Decompress*`) — VENDORED LIBS
- `Md5_*`: standard RFC 1321 MD5 (Init/Update/Final + 1270-instruction Transform).
- `Crc_*`/`Crc16_*`: table-driven CRC32 (zlib polynomial) and a CRC16 used to hash files.
- **zlib 1.x**: `Zlib_Adler32`, `Deflate_*` (full: trees, longest-match, fast/slow deflate, block flush, bit I/O), `Inflate_*` (blocks, huffman tree build, dynamic/fixed trees, `Inflate_DecodeCodes`, `Decompress_InflateFast`), `Gzip_*` (gzopen/gzread/gzwrite/gzseek/gzprintf — gzio.c), `Zip_*` (minizip `unzip.c`: open archive, central dir, local headers, read current file, DOS date decode).
- `Quant_*`: median-cut/octree-style color quantizer (histogram, heap reduce, nearest-color, run-length encode) — image export path.

### CPU / error reporting / trace (`Cpu`, `ErrorLog`, `Trace`)
- `Cpu_DetectFpFallback`/`Cpu_FeaturePresent`: CPUID feature probe (FPU presence → software-float fallback).
- `ErrorLog_*` (@0x437ab0+): game-facing log file writer (`ErrorLog_ReportMessage` has 90 callers) — writes records with module/line. Half platform, half game.
- `Trace_*` (@0x437c00+): a mini stack-walker/symbolizer used by the crash handler (`Trace_DecodeCallInstruction`, `Trace_WriteSymbolLine`, `Trace_IsAddressReadable`).

---

## 2. Key functions (address, prototype, purpose)

| Addr | Name | Prototype | Purpose |
|------|------|-----------|---------|
| 0x5f8468 | VIBE_Runtime_Entry | `int()` | CRT entry → StartupInit → mainCRTStartup |
| 0x5f8238 | VIBE_Runtime_StartupInit | `int(int, HMODULE)` | CRT init: heap, TLS, env, FPU, exception filter |
| 0x5e577c | VIBE_Crt_ExitProcess | `void __noreturn()` | Run exit funcs → ExitProcess |
| 0x142125f | VIBE_Crt_Exit | `int(int)` | `exit()` (9 callers) |
| 0x5fe21c / 0x5fe26c | VIBE_Crt_RunInitFuncs / RunExitFuncs | `char(u8)` | `_initterm` / atexit table run |
| 0x6051f0 | VIBE_Crt_FormatStringCore | `int(int,_BYTE*,void(*)(void),int*)` | printf engine core (vfprintf) |
| 0x605a18 | VIBE_Crt_FormatConversion | `u16*(int,int*,int)` | per-spec conversion (d/x/f/e/s/p…) |
| 0x1422ec6 | VIBE_Printf_Output | `int(int,char*,int)` | low-level formatted output writer |
| 0x5cba00 | VIBE_Crt_Sprintf_0 | `int(int,_BYTE*,...)` | sprintf (333 callers — hottest) |
| 0x5fd070 | VIBE_Crt_ScanFormatCore | `int(int,u8*,int,_DWORD*)` | scanf engine core |
| 0x5fcf40 | VIBE_Crt_PutcBuffered | `int(int)` | buffered fputc |
| 0x5fba68 | VIBE_Crt_FillReadBuffer | `int(int)` | buffered fread refill |
| 0x604dd4 | VIBE_Crt_UnhandledExceptionFilter | `LONG(_EXCEPTION_POINTERS*)` | top-level crash filter (18 strs) |
| 0x604fb8 | VIBE_Crt_StructuredExceptionHandler | `int(EXCEPTION_RECORD*,int,int)` | SEH handler |
| 0x5e5880 | VIBE_Crt_ReportAssertion | `void(const char*,const char*,int)` | assert() report |
| 0x5fe350 | VIBE_Crt_StackProbe | `int __stdcall(int)` | `_chkstk` stack probe |
| 0x1421b91 | VIBE_Mem_HeapAlloc | `int(uint)` | malloc core (mode-dispatched, VirtualAlloc) |
| 0x1421d8b | VIBE_Mem_Free | `void(int)` | free core |
| 0x1421340 | VIBE_Mem_MoveOverlapping | `uint(uint,_BYTE*,uint)` | memmove |
| 0x1421a20 | VIBE_Mem_Set | `u8*(u8*,u8,uint)` | memset |
| 0x1422010 | VIBE_Mem_Compare | `int(_BYTE*,_BYTE*,uint)` | memcmp |
| 0x142473e | VIBE_Heap_SelectGlobalMode | `int()` | choose heap mode from env |
| 0x438f10 | VIBE_Memory_AllocDebug | `char*(char*,const char*)` | game tracking malloc (`m_alloc_debug`, 145 callers, DEADBEEF guards) |
| 0x43923c | VIBE_Memory_FreeDebug | `int(...)` | game tracking free (155 callers) |
| 0x5dbe70 | VIBE_Memory_AllocFromFreeList | `int(uint)` | segregated free-list alloc (30 callers) |
| 0x439778 / 0x439880 | VIBE_MemPool_Alloc / Free | pool alloc/free | fixed-block pool |
| 0x6048d8 | VIBE_Crt_TlsAllocIndex | `BOOL()` | TlsAlloc CRT per-thread index |
| 0x604858 | VIBE_Crt_TlsGetThreadData | `_BYTE*()` | get CRT per-thread block |
| 0x6045f4 | VIBE_Lock_AllocCriticalSection | `_RTL_CRITICAL_SECTION*()` | CRT lock alloc |
| 0x60b234 | VIBE_Thread_BeginThread | `BOOL(int,int,int)` | `_beginthread` |
| 0x40c420 | VIBE_Registry_OpenKey | `HKEY(const char*,DWORD)` | open/create `Software\Ahead Entertainment\<sub>` |
| 0x40c550 | VIBE_Registry_QueryStringValue | `int(HKEY,const CHAR*,BYTE*)` | RegQueryValueExA string |
| 0x1418c20 / 0x1418e80 | VIBE_Md5_Init / Transform | MD5 | RFC1321 MD5 |
| 0x5dc6e0 / 0x5eed98 | VIBE_Util_Crc32 / VIBE_Crc_Compute | CRC32 | zlib-poly CRC32 |
| 0x5ed5f0 | VIBE_Deflate_Process | `int(int,uint)` | zlib deflate() |
| 0x5eca44 | VIBE_Inflate_Process | `int(u8**,int)` | zlib inflate() |
| 0x5fec3c | VIBE_Inflate_BlocksProcess | huge (1124 insns) | inflate block decode |
| 0x60a1f0 | VIBE_Decompress_InflateFast | `int(...)` | zlib inflate_fast |
| 0x5eb970 | VIBE_Gzip_OpenStream | gzopen | gzio |
| 0x5ea908 | VIBE_Zip_OpenArchive | unzOpen | minizip |
| 0x6033b4 | VIBE_Quant_MapImageToPalette | `int(...)` | color quantizer apply |
| 0x5ea068 | VIBE_Util_QuickSort | `int(...)` | qsort |
| 0x5e9f70 | VIBE_Util_BinarySearch | `uint(...)` | bsearch |
| 0x44e240 | VIBE_TimeBase_StartTimer | `MMRESULT(UINT,int)` | timeSetEvent mm-timer |
| 0x142216c | VIBE_Time_GetLocalTime | `int(_DWORD*)` | localtime |
| 0x5cb8bc | VIBE_Util_RandNext | `uint*()` | game RNG next (63 callers) |
| 0x58b89c | VIBE_Math_RandomModulo | `int(u16)` | rand()%n (344 callers) |

---

## 3. Primary data structures & globals

- **CRT heap mode** `dword_1465044` (@0x1465044): 2=small-block segment, 3=mixed, else direct VirtualAlloc. `dword_1465040` = heap handle/base, `dword_14677D4` = indirect alloc fn ptr, `dword_146503C` = small-block threshold, `dword_1455004` = segment max size.
- **CRT free-list heap** globals @0x64A30C–0x64A314: `dword_64A30C` = free-list head, `dword_64A310` = rover, `dword_64A314` = largest-free cache. Block header layout (from `AllocFromFreeList`): `[+0x08]=next`, `[+0x14]=size` (field index 5). Allocation rounding: `(size+11)&~7`, min 16. `off_64A928`/`off_64A930` = lock acquire/release fn ptrs; `byte_14090E0` = lock-held flag.
- **Memory debug tracker** globals @0x62D9DC–0x62D9F4: `dword_62D9F4` = allocation table base (records of 4 dwords: `{group*, name*, ptr, userptr}`, stride 0x10), `dword_62D9DC` = capacity, `dword_62D9E4` = live count, `dword_62D9E0` = live bytes, `dword_62D9E8`/`62D9EC` = peaks. Group node (40 bytes, from `AllocFromFreeList(0x28)`): `[0]=name string`, `[+0x10..]=curBytes,count,peakBytes,peakCount`, `[+0x24]=next` (field 9). Guard word = **0xDEADBEEF** at start and end+4 of each block; user pointer = `block+4`.
- **TLS**: `dwTlsIndex` (@0x64a908) from `TlsAlloc`; `lpTlsValue` (@0x1408abc) main-thread fallback. CRT per-thread data block holds errno, strtok state, asctime/gmtime buffers, rand seed (typical MSVC `_tiddata`).
- **errno**: `Util_GetErrnoPtr` (@0x5d4200) returns `&errno` in the per-thread block; `Runtime_SetErrno*` family sets EINVAL/ERANGE.
- **Registry**: all keys under `HKEY_CURRENT_USER\Software\Ahead Entertainment\<subkey>`; values DWORD/SZ/float (float stored as string).
- **zlib `z_stream`**: standard layout — `next_in/avail_in/total_in/next_out/avail_out/total_out/msg/state/zalloc/zfree/opaque/data_type/adler`; internal deflate/inflate state structs follow zlib 1.1.x.
- **Signal table**: `Signal_SetHandlerSlot`/`GetHandlerSlot`/`GetDispositionSlot` index a small per-signal array (handler + disposition).
- **mm-timer proc table** (`TimeBase_*`): array of {proc, interval, active} entries.

---

## 4. External dependencies (Win32 imports wrapped)

| Module group | Win32 / API imports |
|---|---|
| Heap/Memory | `VirtualAlloc`, `VirtualFree`, `VirtualQuery` (NO HeapAlloc — fully custom heap) |
| Lock | `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection` |
| Tls | `TlsAlloc`, `TlsFree`, `TlsGetValue`, `TlsSetValue` |
| Thread | `CreateThread`/`ExitThread` (via Thread_*); `GetCurrentThreadId` |
| Registry/Config | `RegOpenKeyExA`, `RegCreateKeyExA`, `RegQueryValueExA`, `RegSetValueExA`, `RegCloseKey` (advapi32) |
| Time | `GetLocalTime`/`GetSystemTime`, `GetTimeZoneInformation`, `GetTickCount` |
| TimeBase | `timeSetEvent`, `timeKillEvent`, `timeBeginPeriod`, `timeEndPeriod`, `timeGetTime`, `timeGetDevCaps` (winmm) |
| Crt exception/exit | `SetUnhandledExceptionFilter`, `UnhandledExceptionFilter`, `ExitProcess`, `TerminateProcess`, `RaiseException` |
| Crt module/file | `GetModuleFileName(W)`, file CreateFile/ReadFile/WriteFile/CloseHandle via HandleTable |
| Locale/Mbcs | `MultiByteToWideChar`, `GetCPInfo`, `GetStringTypeW`, `LCMapString` |
| Cpu | CPUID (inline asm), no import |

No `Drm`/`Disc` module is present in this cluster — **no copy-protection code here** (CD check, if any, lives elsewhere; not observed in platform).

---

## 5. Effort estimate & risks (per module group)

| Module(s) | Effort | Risk notes |
|---|---|---|
| Mem/Heap (malloc over VirtualAlloc, 3 modes) | **L** | Custom segmented allocator; bit-exact behavior matters for any pointer-value-dependent logic. Mode dispatch via env. |
| Memory debug tracker / MemPool | **M** | Straightforward but DEADBEEF guards + group accounting must match for save compatibility. |
| Printf/Format/FloatFormat/Scan | **L** | Large hand-tuned format engines (`FormatConversion` 527 insns, `ScanReadInteger` 340). Float formatting (`FormatFixedFloat`/`FormatExponentFloat`) needs exact rounding. |
| Float64 software emul + Fpu/FpException | **M-L** | **FPU control-word juggling** and software double add/mul/div (`Float_*64`) with exception-record building. Only active on no-FPU fallback; risk if reproduced bit-exactly. Hand-written asm likely. |
| Crt exit/init/TLS/Lock/Signal/Eh | **M** | MSVC SEH frame walking (`Eh_*`) and `_initterm`/atexit ordering; standard but fiddly. |
| String/Util/Mbcs/Locale | **M** | High volume but mechanical; MBCS/JIS tables and ctype tables must be transcribed exactly. |
| Time/TimeBase | **S-M** | TZ/DST parsing nontrivial; mm-timer is a thin wrapper. |
| Registry/Config/CmdLine/Env | **S** (Registry/CmdLine/Env), **M** (Config: 90+ settings) | Config_* is game settings — large but flat. |
| Md5/Crc/Crc16 | **S** | Textbook; transcribe constants. |
| zlib (Deflate/Inflate/Gzip/Zip) + InflateFast | **L** | Large vendored lib; reimplement by pulling real zlib 1.1.4 + minizip and matching. Compressed output must match exactly if savegames store deflated blobs. |
| Quant | **M** | Self-contained color quantizer. |
| Cpu/Trace/ErrorLog | **S-M** | Trace stack-walker uses instruction decoding (`Trace_DecodeCallInstruction`) — modest asm awareness. |

Top risks: (a) software-float `Float_*64` + FPU control word — verify whether the FPU-present path is the only one exercised at runtime; (b) deflate output bit-exactness vs. shipped data; (c) the dual heap (CRT `Mem_*` vs game `Memory_*` tracker) — keep them as separate translation units, do not merge.

---

## 6. Proposed C++ file / namespace layout (`platform/` + `util/` + `compress/`)

```
platform/crt_startup.cpp      // Runtime_Entry/StartupInit, init/exit term tables, atexit, exit
platform/crt_printf.cpp       // Format*/Printf_Output, sprintf/vsprintf/snprintf
platform/crt_scanf.cpp        // Scan* family, sscanf
platform/crt_stdio.cpp        // FILE buffering: Putc/Getc/Fill/Flush/OpenFile/FileRead, Stdio
platform/crt_float.cpp        // Float_*64 software emul, Fp*/Fpu*/FpException, matherr
platform/crt_strtox.cpp       // Strtol/Strtoul/Atoi/StrToLong/StrToDouble, digit tables
platform/crt_exceptions.cpp   // UnhandledExceptionFilter, SEH (Eh_*), ReportAssertion/RuntimeError
platform/crt_signal.cpp       // Signal_* table, raise/signal, Ctrl handler
platform/crt_time.cpp         // Time_*, localtime/gmtime/mktime, tzset/DST
platform/crt_locale.cpp       // Locale_*, Mbcs_*, ctype/codepage tables
platform/heap.cpp             // Mem_*/Heap_* malloc over VirtualAlloc, mode dispatch
platform/mem_tracker.cpp      // Memory_* debug allocator + MemPool_*
platform/thread.cpp           // Thread_*, Lock_*, Crt_Tls*/Tls_* TLS
platform/registry.cpp         // Registry_* (Software\Ahead Entertainment\)
platform/config.cpp           // Config_* gfx/sound/camera settings (GAME-adjacent)
platform/cmdline_env.cpp      // CmdLine_*, Env_* argv/environ
platform/mm_timer.cpp         // TimeBase_* winmm timer service
platform/cpu.cpp              // Cpu_*, Fpu feature detect
platform/diagnostics.cpp      // ErrorLog_*, Trace_* stack-walker

util/strings.cpp              // String_*/Util_* str*/mem*/itoa
util/sort_search.cpp          // Util_QuickSort/Sort_*/BinarySearch, shuffles
util/rand.cpp                 // Rand_*, Util_Rand*, Math_Random* (LCG 214013/2531011)
util/hash.cpp                 // Md5_*, Crc_*/Crc16_*, Crc32, Adler32
util/bitset.cpp               // BitSet_*, Guid_*, Color_* small helpers
util/varargs.h                // VaArg_*

compress/zlib_deflate.cpp     // Deflate_* (or vendor real zlib)
compress/zlib_inflate.cpp     // Inflate_*, Decompress_InflateFast
compress/gzio.cpp             // Gzip_*
compress/unzip.cpp            // Zip_* (minizip)
compress/quantize.cpp         // Quant_*
```

Namespaces: `gilde::platform`, `gilde::util`, `gilde::compress`. Keep `gilde::compress` as a thin shim over upstream zlib/minizip if bit-exact output is required; otherwise reimplement.

---

## 7. Suggested implementation order (dependencies first)

1. **util/varargs, util/strings, util/rand, util/hash** — leaf utilities, no internal deps; unblock everything else.
2. **platform/heap + platform/mem_tracker + platform/thread (TLS/Lock)** — allocator + per-thread state; nearly everything allocates.
3. **platform/crt_float + crt_strtox + crt_locale** — numeric/locale primitives needed by printf/scanf.
4. **platform/crt_printf + crt_scanf + crt_stdio** — formatted I/O (depends on 1–3).
5. **platform/crt_signal + crt_exceptions + crt_startup** — process lifecycle (depends on TLS, stdio, heap).
6. **platform/crt_time + mm_timer + cpu + diagnostics** — services.
7. **platform/registry + config + cmdline_env** — config layer (depends on strings, registry).
8. **compress/** (zlib → gzio → unzip → quantize) — vendored libs, depend only on heap + hash.
9. **util/sort_search, util/bitset** — fill-ins, can slot anywhere after strings.

Hand the `He_*`, `HandlerEntry_*`, `Hotkey_*`, `Input_*`, `App_*`, `Game_*`, `Resource_*`, `Result_*`, `DecompressGameState*` items to the gameplay/engine clusters (they only *use* this cluster's heap/compression/RNG).
