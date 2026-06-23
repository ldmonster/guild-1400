# 01 — Entry Point & WinMain

This is the spine: from the PE entry stub to the application's main loop and its ordered
shutdown. Two functions matter — `start @0x6041f0` (the MSVC CRT stub) and
`VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` (the real `WinMain`).

---

## 1. `start @0x6041f0` — the CRT bootstrap

The PE entry point is the standard Microsoft C runtime startup. It does the minimum to get
a valid C environment, then calls `WinMain`:

```c
void __noreturn start()
{
    VIBE_Crt_RunInitFuncs(1);            // 0x5fe21c — run C++ static initializers / _initterm
    ...                                   // stack-probe / alloca setup using dword_64C120
    VIBE_Runtime_Entry();                // 0x5f8468 — CRT runtime init (TLS, heap, FP) → doc 02
    StackBase = VIBE_Crt_GetStackBase(); // 0x5fe300
    ...                                   // VIBE_Crt_StackProbe @0x5fe350 if needed
    VIBE_Runtime_NullStub_08a90();       // 0x608a90
    ModuleHandleA = GetModuleHandleA(0);
    VIBE_GameLogic_MainEntryAndShutdown(ModuleHandleA, 0, dword_64A964, 10);  // "WinMain"
    VIBE_Crt_DoExit(...);                // 0x5e5724 — flush, run atexit, ExitProcess
}
```

The four args passed to the "WinMain" are the classic `WinMain(hInstance, hPrevInstance,
lpCmdLine, nCmdShow)`: the module handle, `0`, the command-line pointer (`dword_64A964`),
and `nCmdShow = 10` (`SW_SHOWDEFAULT`). CRT internals (init-term tables, TLS, heap, the
exit ladder) are detailed in [doc 02](02-crt-runtime-startup.md).

---

## 2. `VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` — WinMain

`int __stdcall WinMain(HMODULE hModule, int, _BYTE *lpCmdLine, int nCmdShow)`. This single
3-KB function is the entire application lifecycle: configure → init → run → shut down. It is
the only caller of every top-level subsystem init and of the main menu/session loop.

### 2.1 Locate the install dir and the INI file

```c
hInstance = hModule;
GetModuleFileNameA(hModule, byte_122F638, 0x104);   // full path of gilde.exe
// copy path → byte_122F73C, then strip after last '\'  →  the install directory
*VIBE_Util_StrChr(byte_122F73C, '\\') = 0;
// append "gilde.INI" after the last '\' of the exe path  →  byte_122F638 now = "<dir>\gilde.INI"
```

`byte_122F638` becomes the absolute path to **`gilde.INI`**, used as the `lpFileName` for
every `GetPrivateProfile*` call below. `byte_122F73C` holds the bare install directory.

### 2.2 Read configuration from `gilde.INI`

A long sequence of `GetPrivateProfileStringA`/`GetPrivateProfileIntA` reads — the complete
config surface. Full detail in [doc 03](03-config-ini-paths-locale.md); the keys:

| Section | Key | Default | Global | Meaning |
|---|---|---|---|---|
| `Network` | `Server` | `Server\Server.dll` | `byte_122F284` | server DLL path (dir → `byte_122F388`) |
| `Gfx` | `TextureDivider` | 0 | `dword_63C790` | texture downscale |
| `Sound` | `msx`/`ambient`/`sfx`/`weather` | 0/0/0/1 | `dword_63C8F8..904` | audio channel enables |
| `General` | `Bildmodus` | `FULLSCREEN` | `v120` | `FULLSCREEN`→3, `DIRECTWINDOW`→1 (window mode) |
| `General` | `GfxPath` | `\project\gfx\` | `aProjectGfx` | asset root |
| `General` | `GamePath` | `\project\game\` | `aProjectGame` | data root |
| `General` | `MoviePath` | `\project\movie\` | `aProjectMovie` | video root |
| `General` | `Language` | `german` | `aGerman` | localization → `VIBE_Locale_CopyLanguageString @0x5a33a0` |
| `General` | `show_intro` | 0 | `dword_63C8F0` | play intro movie |
| `Gfx` | `cur_res` | 0 | `byte_63D724` | resolution index → `dword_63D728/72C` (W/H from tables `dword_63D70C/710`) |
| `General` | `Stadt` | `Augsburg` | `ReturnedString` | starting city |
| `Network` | `Host` | `128.0.0.1` | `byte_122EE90` | server host |
| `Network` | `Port` | 7531 | `dword_122F490` | server port |

### 2.3 Parse command-line overrides

If `lpCmdLine` is non-empty, the function scans it (after `VIBE_Util_StrToUpper`) for
quoted `KEY="value"` tokens and overrides the INI values. The four recognized keys:

- `STADT="…"` → starting city (`ReturnedString`)
- `BERUF="…"` → starting profession (`byte_63C7DC`)
- `IP="…"` → server host (`byte_122EE90`), and sets `v16 = 1` (**direct-connect / network mode**)
- `PORT="…"` → server port (`dword_122F490`, via `VIBE_Util_ParseInt @0x5dc070`)

The quote-scanning is an inline hand-rolled extractor (the repeated `while (*p != '"')`
loops in the decompile). `v16` (network-join flag) and `dword_63C798` (a "skip menu / quick
start" flag) decide whether the main menu is shown or a session starts directly.

### 2.4 Single-instance guard and display mode

```c
if (VIBE_App_CreateSingleInstanceMutex(&v118))   // 0x527d48 — CreateMutexA, already-exists → bail
    VIBE_Crt_DoExit(...);                          // another instance is running: exit
SystemParametersInfoA(SPI_GETSCREENSAVEACTIVE, …); // remember & disable the screensaver
dword_63CC64 = pvParam;
if (pvParam) SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 0, 0, 2);
```

### 2.5 The init ladder (each step can fail → shutdown)

Four subsystems are brought up in order. **Any** failure jumps to the full shutdown ladder
(§2.7) and the program exits cleanly:

```c
if (!VIBE_Window_CreateMainWindow(hModule, v120))        goto shutdown;  // 0x52895c → doc 05
if (!VIBE_App_InitSubsystemsAndMovieDll())               goto shutdown;  // 0x527de0 → doc 04
if (dword_63C8F0)  VIBE_Movie_PlayIntroSequence(...);                    // 0x5347d4 → doc 06
if (!VIBE_Render_InitDisplayAndPaths(v120, v16))         goto shutdown;  // 0x527fa4 → doc 04/23
if (!VIBE_App_InitEngineAndScriptCommands(...))          goto shutdown;  // 0x528560 → doc 04/22/30
```

- **`VIBE_Window_CreateMainWindow`** — registers the window class, creates the main window
  in the chosen display mode (`v120`). Win32→SDL boundary; see doc 05.
- **`VIBE_App_InitSubsystemsAndMovieDll`** — error log, the memory tracker
  (`VIBE_Memory_InitTracker @0x43937c`), the stack mempool (`VIBE_MemPool_StartupStack
  @0x44e420`), the VFS (`VIBE_Vfs_Init @0x451f98`), the high-res timer
  (`VIBE_TimeBase_StartTimer @0x44e240`), the screenshots dir, and **loads `moveahead.dll`**
  resolving its `mov_Init_/Play_/Stop_/…` exports (the movie player). See doc 04.
- **`VIBE_Render_InitDisplayAndPaths`** — brings up the 3D device (DirectDraw/Direct3D →
  Vulkan) and the render paths; `v16` selects the network variant. See docs 04/23.
- **`VIBE_App_InitEngineAndScriptCommands`** — opens the **text engine** (`gilde_text` `.def`
  files), loads world building/object data (`VIBE_World_LoadBuildingAndObjectData @0x5835f8`),
  starts the **Miles audio driver** and loads the sample banks (`system.sbf`,
  `include_sfx.ini`), and **registers the entire script command table**
  (`VIBE_Script_RegisterCommands @0x43c850`, `…RegisterObjectCommands`,
  `Character_RegisterScriptCommands`, `…RegisterSoundCommands`). See docs 04/22/30.

### 2.6 The main loop

After init, the function enters `while (1)`. The branch at the top decides the entry mode:

```c
while (1)
{
    if (dword_63C798 || v16)          // quick-start (cmdline) or network-join
    {
        if (dword_63C798) { /* build a default character (Tobias/Severin), pick a
                               random building type, set History flag — a debug/quick game */ }
        else if (v16)     word_63C740 = 5;   // network game mode
        goto run_session;
    }

    VIBE_Menu_RunMainMenu(v23);       // 0x529d08 — the 3D main menu → doc 08
    if (dword_63CC38) break;          // user chose Quit → leave the loop → shutdown

run_session:
    dword_63CC34 = 1;
    if (word_63C740 && !dword_63CC48)
        for (i = 0; i != dword_63CC34; )
            VIBE_GameLogic_InitOrLoadSession(i, v23, 1);   // 0x533a54 — load/run a game → doc 12
    ...
    if (dword_63CC48) { /* the session asked to quit to desktop → full shutdown, return 1 */ }
    // otherwise loop back to the main menu
}
```

`word_63C740` is the **chosen game mode** the menu writes (new game / load / tutorial /
network / …). `VIBE_GameLogic_InitOrLoadSession` actually loads the city and runs the game;
control returns here when the player quits to the menu (loop) or to the desktop
(`dword_63CC48` → shutdown). The quick-start branch (`dword_63C798`) constructs a throwaway
character — names `Tobias`/`Severin`, a random building type via
`VIBE_Building_LookupTypeRecordA @0x589778`, `word_63C740 = 9` — used by the cmdline/debug
fast path. See [doc 08](08-main-menu.md) and [doc 12](12-session-init-worldload.md).

### 2.7 The shutdown ladder

Every exit path (init failure, quit-to-desktop, a render re-init failure) runs the **same**
ordered teardown. The order is load-bearing — later subsystems depend on earlier ones:

```c
VIBE_Game_ShutdownSubsystems(...);          // 0x5278cc — high-level game teardown
byte_63CC1C = 0;  dword_62D314 = 1;
VIBE_Widget_ShutdownSystem(...);            // 0x4201f4 — GUI
VIBE_Config_WriteGfxSettings(...);          // 0x56af54 — persist settings back to INI/registry
VIBE_GameState_FreeAllResources();          // 0x40e308
VIBE_Universe_SwitchActiveSlot(0, …);       // 0x5b4a24 — free the scene/universe
VIBE_Table_ResetLightmaps(...);             // 0x42e19c
VIBE_Render_ShutdownEngine(...);            // 0x5b0228 — 3D device (Vulkan/DDraw)
VIBE_Input_DirectInputShutdown();           // 0x40cd40 — input (DInput/SDL)
VIBE_TimeBase_StopTimer();                  // 0x44e2c4 — winmm timer
VIBE_Vfs_Shutdown(...);                     // 0x452004 — file system
VIBE_MemPool_ShutdownStack(...);            // 0x44e544 — stack allocator
VIBE_Memory_ShutdownTracker();              // 0x439640 — heap tracker (leak report)
VIBE_ErrorLog_Shutdown();                   // 0x438c0c
if (dword_63C8F0 && hModule)                // if the movie DLL was loaded
    { dword_63C76C(...); FreeLibrary(hModule); }   // mov_Exit_ then unload moveahead.dll
VIBE_Window_DestroyAndUnregisterClass();    // 0x527868 — destroy window, unregister class
if (dword_63CC64) SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 1, …);  // restore screensaver
return 0;  // or 1 on quit-to-desktop
```

This sequence is reproduced verbatim at each of the function's exit points (that is why the
decompile repeats it many times). It is documented once in
[doc 07 — Shutdown sequence](07-shutdown-sequence.md).

---

## 3. Where to go next

- The CRT internals before WinMain → [02 — CRT / runtime startup](02-crt-runtime-startup.md)
- The INI keys and path/locale handling → [03 — Config, INI, paths & locale](03-config-ini-paths-locale.md)
- The four init steps → [04 — App init & subsystem bring-up](04-app-init-subsystems.md)
- The menu the loop calls → [08 — Main menu](08-main-menu.md)
- The session the loop loads → [12 — Session init & world load](12-session-init-worldload.md)
- The frame tick everything below runs on → [14 — Per-frame game loop](14-per-frame-loop.md)
