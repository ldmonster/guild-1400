# 02 — CRT / Runtime Startup

This chapter documents the Microsoft Visual C++ C-runtime (CRT) bootstrap that `gilde.exe`
executes **before** application code. It is the code between the PE entry point (`start`
`@0x6041f0`) and the call into `WinMain` (`VIBE_GameLogic_MainEntryAndShutdown` `@0x534bbc`).
Everything here is statically-linked MSVC CRT (circa VC6 / `crt0`-era `wWinMainCRTStartup`),
reconstructed 1:1 from the binary. It performs: early static-initializer pass, the initial
per-thread data (TLS) block, process-wide runtime init (`__heap_init`-equivalent, OS version
cache, module/command-line/environment capture, std handles, structured-exception handler),
stack-base recording, the C++ static-initializer pass, then `WinMain`, and finally the
`exit`/`atexit`/`ExitProcess` teardown ladder. See also
[01 — Entry point & WinMain](01-entrypoint-and-winmain.md) and
[28 — Memory management](28-memory-management.md).

> Platform boundary: this subsystem is almost entirely the MSVC C runtime and is reconstructed
> 1:1. The only true OS dependencies are the Win32 imports it calls (`GetModuleHandleA`,
> `GetVersion`, `GetCommandLineA/W`, `GetEnvironmentStrings`, `GetModuleFileNameA/W`,
> `MultiByteToWideChar`, `VirtualQuery`, `GetStdHandle`, `SetUnhandledExceptionFilter`,
> `GetCurrentThreadId`, `ExitProcess`, `UnhandledExceptionFilter`, `__writefsdword` to the TIB).
> Under the reimpl these route through the `IPlatform` shim; the CRT logic itself is portable.

---

## Control-flow overview

```
PE entry: start @0x6041f0
  ├─ VIBE_Crt_RunInitFuncs(1)              // _initterm group 0x01  (early init)
  ├─ alloca(sizeof TLS block, 4-aligned)   // initial per-thread data on stack
  ├─ VIBE_Light_SetGrayColorThunk          // == memset(block, 0, 0x108)
  ├─ block[+0x104] = 0x108                  // record self-size into the block
  ├─ VIBE_Runtime_Entry @0x5f8468
  │    ├─ GetModuleHandleA(0)
  │    ├─ VIBE_Runtime_StartupInit @0x5f8238   // the big process-init routine
  │    │     ├─ VIBE_Crt_TlsInitThreadData @0x60489c   // (here: pass-through, block already built)
  │    │     ├─ VIBE_HandleTable_InitStdHandles @0x6044b0
  │    │     ├─ GetEnvironmentStrings / GetVersion (cached)
  │    │     ├─ GetModuleFileNameA/W  (ANSI + wide exe path)
  │    │     ├─ GetCommandLineA → dup, skip argv[0] → _acmdln/_pgmptr split
  │    │     ├─ GetCommandLineW → dup, skip argv[0] → wide cmdln
  │    │     └─ (a1!=0) module-handle exe path again
  │    ├─ VIBE_Crt_GetStackBounds @0x604cd0       // stack low/high via VirtualQuery
  │    ├─ VIBE_Crt_InstallExceptionHandler @0x605180
  │    ├─ VIBE_Crt_RunInitFuncs(0x21)             // _initterm group ≤0x21 (C init)
  │    ├─ off_64A94C()                            // null stub (pre-C++-init hook)
  │    └─ VIBE_Crt_RunInitFuncs(0xFF)             // _initterm group ≤0xFF (C++ static ctors)
  ├─ VIBE_Crt_GetStackBase @0x5fe300 / VIBE_Crt_StackProbe @0x5fe350   // touch guard pages
  ├─ VIBE_Runtime_NullStub_08a90()         // empty
  ├─ VIBE_GameLogic_MainEntryAndShutdown(hInst, 0, lpCmdLine, 10)   // == WinMain
  └─ VIBE_Crt_DoExit @0x5e5724              // exit ladder → ExitProcess
```

---

## 1. PE entry point — `start` `@0x6041f0`

`__noreturn`. Custom-register prologue (`push ebx/ecx/edx/ebp; mov ebp,esp; sub esp,8`).
Pseudocode (cleaned):

```c
void __noreturn start()
{
    VIBE_Crt_RunInitFuncs(1);                 // _initterm pass, priority group 0x01

    // Allocate the *initial thread's* per-thread data block on the stack, 4-byte aligned.
    int sz = (dword_64C120 + 3) & 0xFC;       // dword_64C120 = 0x108 (264) = sizeof(ptd)
    void *ptd = alloca(sz);
    VIBE_Light_SetGrayColorThunk(0, dword_64C120, ptd);   // memset(ptd, 0, 0x108)
    *(u32*)((char*)ptd + 0x104) = dword_64C120;            // ptd->self_size = 0x108

    VIBE_Runtime_Entry();                     // full process init (heap, env, cmdline, ctors)

    // Stack probe: ensure committed/guard pages exist down to the recorded base.
    char *base = VIBE_Crt_GetStackBase();
    char *p;
    if (esp >= base) {
        p = NULL;
    } else {
        VIBE_Crt_StackProbe(esp);             // walk pages downward, touching each
        alloca((dword_64A988 + 3) & 0xFC);
        p = ptd;                              // (reuses initial frame)
    }
    dword_64A98C = (int)&p[dword_64A988];

    VIBE_Runtime_NullStub_08a90();            // empty hook

    char *lpCmdLine = (char*)dword_64A964;    // _acmdln advanced past argv[0]
    HMODULE hInst = GetModuleHandleA(0);
    VIBE_GameLogic_MainEntryAndShutdown(hInst, 0, lpCmdLine, 10);   // WinMain(hInst, hPrev=0, lpCmdLine, nShow=10)

    VIBE_Crt_DoExit(retval, code);            // never returns
}
```

Notes for a reimplementer:

* `dword_64C120 = 0x108` (264 bytes) is `sizeof(_tiddata)` — the per-thread data structure.
  The initial thread's block lives on the entry frame (not heap), zeroed, with its size mirrored
  at offset `+0x104`.
* `VIBE_Light_SetGrayColorThunk` `@0x5c6af0` is a misnamed shared helper. Called here with
  fill-byte `0`, it is exactly `memset(ptd, 0, count)` — it broadcasts the low byte of `edx`
  across a dword and calls the aligned fill (see §9). Do not read game/lighting meaning into it.
* `nShow = 10` is `SW_SHOWDEFAULT`. `hPrevInstance = 0` always (Win32 contract).
* The early `RunInitFuncs(1)` runs only the lowest-priority `_initterm` records (CRT-internal,
  e.g. the pointer-table/`pre_cpp_init` group); the heavyweight C and C++ ctor passes happen
  later inside `VIBE_Runtime_Entry`.

---

## 2. `VIBE_Runtime_Entry` `@0x5f8468`

This is `_mainCRTStartup`'s body proper (the part after the initial frame setup that `start`
inlined). Returns `char*` (the `_initterm` cursor; value unused).

```c
char *VIBE_Runtime_Entry()
{
    HMODULE h = GetModuleHandleA(0);
    VIBE_Runtime_StartupInit(0 /*eax: "this is the main module" flag*/, h /*ebx*/);
    _DWORD *lo, *hi;
    lo = off_64A90C();                 // __getptd-style; returns &ptd (see note)
    VIBE_Crt_GetStackBounds(lo, hi);   // records [stack_low, stack_high]
    VIBE_Crt_InstallExceptionHandler();
    VIBE_Crt_RunInitFuncs(0x21);       // C initializers   (_initterm, group ≤ 0x21)
    off_64A94C();                      // null stub (hook slot, currently VIBE_Util_NullStub)
    return VIBE_Crt_RunInitFuncs(0xFF);// C++ static constructors (_initterm, group ≤ 0xFF)
}
```

The argument to `VIBE_Runtime_StartupInit` is `__usercall` — `eax = 0`, `ebx = hModule`.
Passing `eax=0` flags this as the primary-module init (it controls the second
`GetModuleFileNameA(hModule, …)` block and the no-TLS-block `ExitProcess(1)` fast-fail).

> **`off_64A90C` indirection.** Statically `off_64A90C` holds `0x5f821c`
> (`VIBE_Util_NullThunk` → `VIBE_Util_NullStub` `@0x5f8224`, both empty). This is the CRT's
> indirect "get pointer to thread data" slot; in the original it returns the address of the
> per-thread `_tiddata` block. `VIBE_Crt_GetStackBase` (§6), `InstallExceptionHandler`, and
> `RemoveExceptionHandler` all dereference `off_64A90C() + 0x54` as the `_EXCEPTION_REGISTRATION`
> head and `*off_64A90C()` as the stack base. Treat `off_64A90C()` as "address of the current
> thread's `_tiddata`". The static null-stub is the single-threaded fallback.

---

## 3. `VIBE_Runtime_StartupInit` `@0x5f8238` — process-wide init

`__usercall`, `eax = mainFlag`, `ebx = hModule`. Returns `int` (1 on success). This is the
CRT's `__crtGetEnvironmentStrings`/`_setargv`/`_setenvp` equivalent fused with version caching.
Step by step:

1. `dword_1408AB0 = mainFlag;` — caches the "is this a DLL?" / non-main flag. (`(BYTE)`
   tested later in the exit ladder, §8.)
2. `lpTlsValue = VIBE_Crt_TlsInitThreadData(...)` (§5). On the main thread the block already
   exists (built in `start`), so this is effectively a pass-through that locks in the thread id.
   * If it returns 0 **and** `mainFlag == 0` (main module) → `ExitProcess(1)` immediately
     (out-of-memory during CRT bring-up). Otherwise returns the (null) result.
3. `VIBE_HandleTable_InitStdHandles()` (§4) — populate the lowio std-handle table.
4. `penv = GetEnvironmentStrings();` and `dword_140A9A4 = 0;` — cache the environment block.
5. **OS version cache** via `GetVersion()` into `dword_64A9A7` and derived fields:

   | Global | Meaning | Expression |
   |---|---|---|
   | `dword_64A9A7` | raw `GetVersion()` result | `GetVersion()` |
   | `dword_64A9AB` | `_osver` (build number) | `HIWORD(raw)` |
   | `dword_64A9AF` | `_winmajor` | `(u8)raw` |
   | `dword_64A9B3` | `_winminor` | `BYTE1(raw)` |
   | `dword_64A9B7` | `_winver` | `BYTE1(raw) | ((u8)raw << 8)` |

   The high bit of `HIWORD(raw)` (`>= 0x8000`) distinguishes Win9x from NT and is reused by
   `GetStackBounds` (§6) and `GetModuleFileNameWide` (§4) to pick code paths.
6. **Module path (ANSI):** `GetModuleFileNameA(0, byte_1408AC0, 0x104)`; `dword_64A968 = &byte_1408AC0`
   (this is `_pgmptr`).
7. **Module path (wide):** `VIBE_Crt_GetModuleFileNameWide(0, word_1408BC4, 0x208)`;
   `dword_64A974 = &word_1408BC4` (`_wpgmptr`).
8. **Command line (ANSI):**
   * `v9 = VIBE_Crt_DupString(GetCommandLineA())`; `dword_1408AB4 = v9` (this is `_acmdln`).
   * Skip past `argv[0]`: if the first char is `"` (`34`), scan to the matching closing quote
     (special-case `""`); otherwise scan while the char is **not** whitespace using the table
     `byte_64A208` (see §10) indexed by `(u8)(c + 1)` and bit `& 2`.
   * Then skip trailing whitespace (`& 2` set). `dword_64A964 = v9` — the pointer **after**
     `argv[0]`, which is what gets handed to `WinMain` as `lpCmdLine` (§1).
9. **Command line (wide):** mirror of step 8 on `GetCommandLineW()` (duped via
   `VIBE_Crt_DupWideString`). If `GetCommandLineW()` is NULL (Win9x), use empty string
   `&unk_62C3DC`. Result pointer stored in `dword_64A970`.
10. If `mainFlag != 0`: capture the *passed module's* file name again into a second pair of
    buffers (`byte_1408DCC` ANSI → `dword_64A96C`, `word_1408ED0` wide → `dword_64A978`).
11. `return 1`.

Key globals (the CRT's "magic name" equivalents):

| Global | CRT equivalent | Set to |
|---|---|---|
| `dword_1408AB0` | DLL/non-main flag | `mainFlag` |
| `lpTlsValue` (`0x1408ABC`) | `&ptd` for init thread | TlsInitThreadData result |
| `penv` (`0x64A9A1`) | environment strings | `GetEnvironmentStrings()` |
| `dword_64A968` / `dword_64A974` | `_pgmptr` / `_wpgmptr` | exe path ANSI / wide |
| `dword_1408AB4` / `dword_1408AB8` | `_acmdln` / `_wcmdln` (raw dup) | duped command lines |
| `dword_64A964` / `dword_64A970` | `lpCmdLine` ANSI / wide (post-argv0) | parsed pointers |

---

## 4. Std handles & wide module name

`VIBE_HandleTable_InitStdHandles` `@0x6044b0` — for each of the three standard handles
(`GetStdHandle(-10/-11/-12)` = STD_INPUT/OUTPUT/ERROR), if the handle is NULL or `INVALID_HANDLE_VALUE`,
substitute a fresh event handle (`VIBE_EventTable_CreateEvent`), and register each into the lowio
handle table via `VIBE_HandleTable_AddEntry`. This is the `_ioinit` equivalent (without the
inherited-handle FILE-flags parsing — the game only needs the three slots present).

`VIBE_Crt_GetModuleFileNameWide` `@0x604bb0` — Unicode-aware module name:
* On NT (`GetVersion()>>16 < 0x8000`): straight `GetModuleFileNameW`.
* On Win9x (no real `…W`): allocate a 0x208-byte scratch (`VIBE_Memory_AllocFromFreeList`),
  `GetModuleFileNameA` into it, `MultiByteToWideChar(CP_ACP=1, MB_PRECOMPOSED=1, …)` to the
  caller buffer, free scratch, NUL-terminate at `a2[len-1]`, return wide length via
  `VIBE_String_WideEnvLength`. Failure paths free the scratch and return 0.

---

## 5. Per-thread data (TLS) — `VIBE_Crt_TlsInitThreadData` `@0x60489c`

`__usercall`, `eax = existingBlock`. This is `_mtinit`/`_initptd`-style per-thread bring-up.

```c
int VIBE_Crt_TlsInitThreadData(_DWORD *block)
{
    if (!block) {                                   // need a fresh block (worker threads)
        block = VIBE_Memory_AllocZeroed(1, dword_64C120);   // calloc(1, 0x108)
        if (block) {
            *((u8*)block + 0x52)  = 1;              // +0x52: "owns / valid" flag
            *((u32*)block + 0x104) = dword_64C120;  // +0x104: self_size = 0x108
        }
    }
    VIBE_Runtime_LockInitThreadId(block);
    return block;
}
```

On the **main** thread `block` is non-NULL (built in `start`), so this just records the thread
id. `VIBE_Runtime_LockInitThreadId` `@0x608ec0`:

```c
_DWORD *VIBE_Runtime_LockInitThreadId(_DWORD *p)
{
    if (p) {
        p[3] = 1;                                   // +0x0C: init/lock flag
        VIBE_Crt_GetStackBounds(p, 0);              // fill p[0] = stack low (see §6)
        *(u32*)((char*)p + 0xEE) = GetCurrentThreadId();   // +0xEE: owning thread id
    }
    return p;
}
```

`_tiddata` offsets observed (relevant subset): `+0x00` stack-low base (used by GetStackBase),
`+0x0C` init flag, `+0x52` valid flag, `+0x54` `_EXCEPTION_REGISTRATION` head (TIB chain),
`+0xEE` thread id, `+0x104` self-size (0x108).

`VIBE_Memory_AllocZeroed` `@0x608a40` is `calloc`: `n = count*size`,
`p = VIBE_Memory_AllocFromFreeList(n)`, and on success `VIBE_Light_SetGrayColorThunk(0, n, p)`
(= `memset(p,0,n)`). See [28 — Memory management](28-memory-management.md) for the free-list
allocator.

---

## 6. Stack setup — bounds, base, probe

`VIBE_Crt_GetStackBounds` `@0x604cd0` (`__usercall`, `eax = out_low`, `edx = out_high`):
`VirtualQuery` on a local address to recover the stack region, then compute:

```c
high = Buffer.BaseAddress + Buffer.RegionSize;     // top of committed region
if (HIWORD(_osver_raw) >= 0x8000)                  // Win9x
    low = AllocationBase + (winmajor>=4 ? 77824 : 73728);   // 0x13000 : 0x12000
else                                               // NT
    low = AllocationBase + 12288;                  // 0x3000  (one guard + commit)
if (out_low)  *out_low  = low;
if (out_high) *out_high = high;
```

The reserved-page slack (0x3000 NT vs 0x12000/0x13000 Win9x) is the guard region the CRT keeps
below the live stack so its `_resetstkoflw`/probe logic has room.

`VIBE_Crt_GetStackBase` `@0x5fe300` — returns `esp - *(&ptd)`, i.e. current SP minus the stack-low
base stored at `_tiddata[0]`. It is the distance from current frame to the recorded stack floor.

```asm
push edx
call ds:off_64A90C          ; edx = &ptd
mov  edx, eax
mov  eax, esp
sub  eax, [edx]             ; esp - ptd->stack_low
pop  edx
retn
```

`VIBE_Crt_StackProbe` `@0x5fe350` (`__stdcall`) — `_chkstk`-style page walk. Starting from the
argument byte-count, it writes one dword per page and steps **down** by 4096 (`-= 4096`) until
the remaining count drops to/under 4096, touching `v4[v2] = v2*4` at 1024-dword (4 KiB) strides
to fault-in and commit guard pages.

```c
int __stdcall VIBE_Crt_StackProbe(int a1)
{
    int n = a1, i = -1;
    do {
        local[i] = i * 4;          // touch this page
        i -= 1024;                 // next page down (1024 dwords = 4096 bytes)
    } while (!(n <= 4096) && (n -= 4096, 1));
    return n;
}
```

In `start`, the probe runs only when `esp < GetStackBase()` (i.e. the frame already dipped below
the recorded floor), after which `dword_64A98C` (a stack-extent marker) is updated using the
`dword_64A988` slack constant.

---

## 7. `_initterm` — static initializer passes

The CRT's `_initterm` is split into a forward pass (constructors) and the exit ladder uses the
same table for the reverse pass.

### Table layout — `0x64C406 .. 0x64C454`

Records are **6 bytes** each: `{ u8 state, u8 priority, void(*fn)() }`.

| Bound symbol | Address | Role |
|---|---|---|
| `unk_64C406` | `0x64C406` | **start** of init records |
| `byte_64C436` | `0x64C436` | **end** of init records / start of exit records |
| `byte_64C454` | `0x64C454` | **end** of exit records |

Init table bytes (`get_bytes` @0x64C406, 78 bytes — 8 init records then exit records):

```
00 20 70 a5 5f 00   state=0 prio=0x20 fn=0x5fa570
00 02 dc a5 5f 00   state=0 prio=0x02 fn=0x5fa5dc
00 20 b0 e3 5f 00   state=0 prio=0x20 fn=0x5fe3b0
00 20 10 e3 5f 00   state=0 prio=0x20 fn=0x5fe310
00 20 08 8f 60 00   state=0 prio=0x20 fn=0x608f08
00 03 40 60 60 00   state=0 prio=0x03 fn=0x606040
00 20 e0 67 60 00   state=0 prio=0x20 fn=0x6067e0
00 20 f0 94 60 00   state=0 prio=0x20 fn=0x6094f0
-- exit records (after 0x64C436) --
00 20 50 e4 5f 00   state=0 prio=0x20 fn=0x5fe450
00 20 00 85 5f 00   state=0 prio=0x20 fn=0x5f8500
00 1f 78 45 60 00   state=0 prio=0x1f fn=0x604578
00 0a 2c 4b 60 00   state=0 prio=0x0a fn=0x604b2c
00 20 ec 8f 60 00   state=0 prio=0x20 fn=0x608fec
```

`VIBE_Crt_RunInitFuncs` `@0x5fe21c` (`__usercall`, `al = maxPriority`) runs the **init** range
`[0x64C406, 0x64C436)`. It is a selection-sort dispatch — repeatedly pick the lowest-priority
not-yet-run record whose `priority <= maxPriority`, invoke it, mark it run:

```c
char *VIBE_Crt_RunInitFuncs(u8 maxPrio)
{
    for (;;) {
        char *r = (char*)&unk_64C406;
        char *best = byte_64C436;          // sentinel = end
        u8 i = maxPrio;
        for (; r < byte_64C436; r += 6) {
            if (r[0] != 2 && i >= (u8)r[1]) {   // not-run AND priority <= current threshold
                best = r;
                i = r[1];
            }
        }
        if (best == byte_64C436) break;     // nothing left to run
        VIBE_Crt_InvokeExitFunc((fn**)(best + 2));   // call best->fn (if non-NULL)
        best[0] = 2;                        // mark "executed"
    }
    return r;
}
```

* `state == 2` means "already executed".
* The pass picks records in **ascending priority** order up to `maxPrio`, so calling with
  `1`, then `0x21`, then `0xFF` runs progressively higher-priority groups in stages — matching
  the three-stage call sequence in `start`/`Runtime_Entry` (pre-init → C init → C++ ctors).

`VIBE_Crt_InvokeExitFunc` `@0x5fe210` — null-safe indirect call: `if (*result) return (*result)();`.
Shared by both init and exit passes.

---

## 8. Exit ladder — `atexit` / `ExitProcess`

`VIBE_Crt_RunExitFuncs` `@0x5fe26c` (`__usercall`, `al = minPriority`, `dl = callMask`) runs the
**exit** range `[0x64C436, 0x64C454)` in **descending priority** (mirror image of the init pass).
It only actually *invokes* `best->fn` when `callMask >= best->priority`, but always marks the
record executed:

```c
char *VIBE_Crt_RunExitFuncs(u8 minPrio, u8 callMask)
{
    for (;;) {
        char *r = byte_64C436, *best = byte_64C454;
        u8 i = minPrio;
        for (; r < byte_64C454; r += 6)
            if (r[0] != 2 && i <= (u8)r[1]) { best = r; i = r[1]; }  // highest priority first
        if (best == byte_64C454) break;
        if (callMask >= (u8)best[1])
            VIBE_Crt_InvokeExitFunc((fn**)(best + 2));
        best[0] = 2;
    }
    return r;
}
```

`VIBE_Crt_DoExit` `@0x5e5724` (`__fastcall __noreturn`, this is `doexit`):

```c
void __noreturn VIBE_Crt_DoExit(int code, int arg)
{
    if (dword_64AA90) dword_64AA90(arg);    // optional registered "_exit hook" (NULL by default)
    off_64A588();                           // null proc (lock/flush hook, == VIBE_Crt_NullProc)
    if ((u8)dword_1408AB0) {                // DLL/non-main path
        if (dword_64A954) dword_64A954();   // _pRawDllMain-style hook (NULL by default)
    } else {                                // EXE main path
        VIBE_Crt_RunExitFuncs(0x10, 0xFF);  // run atexit/_onexit table, all priorities
    }
    VIBE_Crt_ExitProcess();                 // → ExitProcess
}
```

`VIBE_Crt_ExitProcess` `@0x5e577c` (`__noreturn`) — final teardown then OS exit:

```c
void __noreturn VIBE_Crt_ExitProcess()
{
    off_64A588();                           // null proc
    off_64A58C();                           // null proc (returns exit code in edx by ABI)
    if (dword_1408AB0) {                     // DLL
        if (dword_64A954) dword_64A954();
    } else {                                // EXE
        VIBE_Crt_RemoveExceptionHandler();  // unhook the SEH frame (§9)
        VIBE_Crt_RunExitFuncs(0, 0xF);      // run low-priority terminators (mask 0xF)
        off_64A948();                       // null hook
    }
    ExitProcess(exitCode);
}
```

The two-stage `RunExitFuncs` (mask `0xFF` in `DoExit`, then mask `0xF` in `ExitProcess`) is the
CRT splitting user `atexit` callbacks (run early) from CRT-internal terminators (run last). Both
`dword_64AA90`, `dword_64A954`, `dword_64A954` and the `off_64A588/58C/948` slots are **NULL /
empty-stub** in this build (`VIBE_Crt_NullProc` `@0x5e5720`, `VIBE_Util_NullStub` `@0x5f8224`).

---

## 9. Structured exception handler install / remove

`VIBE_Crt_InstallExceptionHandler` `@0x605180` sets up the per-thread SEH registration and the
top-level filter. It writes the registration record into `ptd[+0x54]`, points its handler at
`VIBE_Crt_StructuredExceptionHandler`, links it into the TIB via `__writefsdword(0, &record)`
(the `fs:[0]` SEH chain), then `SetUnhandledExceptionFilter(VIBE_Crt_UnhandledExceptionFilter)`.

`VIBE_Crt_StructuredExceptionHandler` `@0x604fb8` (`__cdecl`) is the CRT's
`_XcptFilter`/`__CxxFrameHandler`-style dispatcher:
* `ExceptionFlags & 6` (unwind/exit-unwind) → return `1` (continue search).
* FP exception codes `0xC000008D..0xC0000093` → set `byte_140A9B4`, `VIBE_Float_ClearExceptions`,
  raise `SIGFPE` (`VIBE_Signal_RaiseSigTerm`); clears the x87 status word low bits
  (`*(WORD*)(ctx+32) &= 0x7F00`) before continuing.
* Otherwise walks the C signal/exception action table (`dword_64ADDC`/`dword_64ADE0`, up to 12
  entries); falls through to `UnhandledExceptionFilter` → `ExitProcess(-1)` if unhandled.

`VIBE_Crt_RemoveExceptionHandler` `@0x6051cc` (called from the EXE exit path, §8) restores
`fs:[0]` to the previous registration and clears `ptd[+0x54]`.

---

## 10. Low-level fill helpers (`memset`/`memset32`)

`VIBE_Memory_FillDword` `@0x5f8197` (`__usercall`, `eax=dst`, `edx=value`, `ecx=count_dwords`)
is the dword-granular fill: it first writes single dwords until `dst` is 32-byte aligned, then
runs an unrolled 8-dword body, then writes the `count & 3` tail. Returns the final cursor.

`VIBE_Memory_FillDwordAlignedThunk` `@0x5f8160` (`__usercall`, `eax=dst`, `edx=value`,
`ecx=count_bytes`) is the **byte-count** wrapper used as `memset`. It aligns `dst` to a 4-byte
boundary first (rotating `value` with `ROR 8` per byte so the byte pattern stays phase-correct),
calls `VIBE_Memory_FillDword(dst, value, count>>2)`, then writes the `count & 3` trailing bytes.

`VIBE_Light_SetGrayColorThunk` `@0x5c6af0` (`__usercall`, `eax=dst`, `ebx=count`, `edx=fillByte`)
— despite the lighting name — broadcasts the low byte of `edx` into all four bytes of a dword and
calls `FillDwordAlignedThunk`. With `fillByte=0` it is `memset(dst, 0, count)`, which is how the
CRT zero-fills the initial `_tiddata` block (§1) and `calloc` buffers (§5). Reimplementers should
expose it as a plain `memset` and not couple it to rendering.

### Command-line whitespace class table — `byte_64A208`

The argv[0]-skip loops (§3) test `byte_64A208[(u8)(c + 1)] & 2`. The table is a 256-entry
character-class array (one bit per class). The 16 bytes at the relevant prefix are:

```
0x00 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x03 0x03 0x03 0x03 0x03 0x01
```

Bit `0x02` marks whitespace (` `, `\t`, `\n`, `\v`, `\f`, `\r` → entries `0x0A..0x0F` show
`0x03` = class|whitespace). The `+1` index bias means index `0` (== char `0xFF`, end sentinel)
maps to entry `0x00` (not whitespace), so the scan stops cleanly at the NUL terminator.

---

## 11. Reaching `WinMain`

After all three `_initterm` passes complete and the stack/SEH are armed, `start` calls
`VIBE_GameLogic_MainEntryAndShutdown(hInstance, 0, lpCmdLine, SW_SHOWDEFAULT)` `@0x534bbc` —
the game's `WinMain`. Its return value flows into `VIBE_Crt_DoExit`, which runs the exit ladder
and calls `ExitProcess`. Application-level behavior continues in
[01 — Entry point & WinMain](01-entrypoint-and-winmain.md).

---

## Address index

| Symbol | Address | Role |
|---|---|---|
| `start` | `0x6041f0` | PE entry; initial TLS frame, stack probe, WinMain dispatch |
| `VIBE_Runtime_Entry` | `0x5f8468` | CRT main-startup body; init passes |
| `VIBE_Runtime_StartupInit` | `0x5f8238` | env/version/module/cmdline capture |
| `VIBE_Crt_TlsInitThreadData` | `0x60489c` | per-thread `_tiddata` bring-up |
| `VIBE_Runtime_LockInitThreadId` | `0x608ec0` | record thread id + stack low in ptd |
| `VIBE_Memory_AllocZeroed` | `0x608a40` | `calloc` |
| `VIBE_HandleTable_InitStdHandles` | `0x6044b0` | std-handle table init |
| `VIBE_Crt_GetModuleFileNameWide` | `0x604bb0` | Unicode module path (Win9x fallback) |
| `VIBE_Crt_GetStackBounds` | `0x604cd0` | stack low/high via `VirtualQuery` |
| `VIBE_Crt_GetStackBase` | `0x5fe300` | `esp - ptd->stack_low` |
| `VIBE_Crt_StackProbe` | `0x5fe350` | `_chkstk` page walk |
| `VIBE_Crt_InstallExceptionHandler` | `0x605180` | SEH + unhandled filter install |
| `VIBE_Crt_StructuredExceptionHandler` | `0x604fb8` | `_XcptFilter` dispatcher |
| `VIBE_Crt_UnhandledExceptionFilter` | `0x604dd4` | last-chance filter |
| `VIBE_Crt_RemoveExceptionHandler` | `0x6051cc` | unhook SEH at exit |
| `VIBE_Crt_RunInitFuncs` | `0x5fe21c` | `_initterm` forward (ctors) |
| `VIBE_Crt_RunExitFuncs` | `0x5fe26c` | `_initterm` reverse (atexit) |
| `VIBE_Crt_InvokeExitFunc` | `0x5fe210` | null-safe indirect call |
| `VIBE_Crt_DoExit` | `0x5e5724` | `doexit` ladder |
| `VIBE_Crt_ExitProcess` | `0x5e577c` | final teardown → `ExitProcess` |
| `VIBE_Memory_FillDword` | `0x5f8197` | dword-granular fill |
| `VIBE_Memory_FillDwordAlignedThunk` | `0x5f8160` | byte-count `memset` core |
| `VIBE_Light_SetGrayColorThunk` | `0x5c6af0` | broadcast-byte `memset` shim |
| `VIBE_GameLogic_MainEntryAndShutdown` | `0x534bbc` | `WinMain` |

### Key data globals

| Symbol | Address | Value / role |
|---|---|---|
| `dword_64C120` | `0x64C120` | `0x108` (264) = `sizeof(_tiddata)` |
| `dword_1408AB0` | `0x1408AB0` | DLL/non-main flag |
| `off_64A90C` | `0x64A90C` | `__getptd` slot → `0x5f821c` (null stub statically) |
| `unk_64C406 / byte_64C436 / byte_64C454` | — | init-table start / mid / end (6-byte records) |
| `dword_64A9A7` (+`AB/AF/B3/B7`) | `0x64A9A7` | cached `GetVersion()` + `_osver/_winmajor/_winminor/_winver` |
| `dword_64A964 / dword_64A970` | — | `lpCmdLine` ANSI / wide (post-argv0) |
| `dword_64A968 / dword_64A974` | — | `_pgmptr` / `_wpgmptr` |
| `byte_64A208` | `0x64A208` | 256-entry char-class table; bit `0x02` = whitespace |
