# 04 — App init & subsystem bring-up

How the original `gilde.exe` (Die Gilde / Europa 1400, 32-bit x86, imagebase
`0x400000`) brings up its subsystems before entering the main game loop. This document
describes the **original binary** as recovered from the IDA Pro / Hex-Rays
decompilation; the binary is the source of truth.

Everything here runs inside the WinMain body
`VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` (see [01 — Entry point & WinMain](01-entrypoint-and-winmain.md)).
That function calls, in order, the four init roots below, then drops into the per-frame
loop (see [14 — Per-frame loop](14-per-frame-loop.md)):

| Order | Call site | Init root | Address | Purpose |
|------:|-----------|-----------|---------|---------|
| 1 | `0x53510b` | `VIBE_App_CreateSingleInstanceMutex` | `0x527d48` | Single-instance guard via `CreateMutexA` |
| 2 | `0x53520f` | `VIBE_App_InitSubsystemsAndMovieDll` | `0x527de0` | Error log, memory, VFS, timer, Screenshots dir, load `moveahead.dll` |
| 3 | `0x5352b7` / `0x5353c5` | `VIBE_Render_InitDisplayAndPaths` | `0x527fa4` | Display modes, window sizing, render device, input, asset paths, GUI |
| 4 | `0x53534b` | `VIBE_App_InitEngineAndScriptCommands` | `0x528560` | Text engine, world data, audio bring-up, script command registration |

`InitDisplayAndPaths` is invoked from two sites in WinMain — once for the normal display
bring-up and once on a re-init path (e.g. after a settings/mode change).

> **Reimplementation note (platform boundary).** Steps below touch four vendor
> technologies. Per project rules: Win32 windowing/input/timers and DirectDraw/Direct3D →
> **SDL** + **Vulkan**; Miles Sound System (MSS32 / `AIL_*`) → **SDL audio**;
> `moveahead.dll` movie playback → **pl_mpeg** shim (see memory: rule-6 tech decisions).
> The engine's own logic (VFS, error log, memory tracker, text/world/script engines,
> projection math) is **reconstructed 1:1**. Each section flags swap vs reconstruct.

---

## Step 1 — Single-instance mutex

### `VIBE_App_CreateSingleInstanceMutex @0x527d48` (`__usercall`, `_DWORD *a1@<eax>`, returns `eax`)

```c
MutexA = CreateMutexA(0, 1, "Die Gilde");   // ClassName @0x622968
if (!MutexA) return -1;
*a1 = MutexA;                                // store handle out-param
return GetLastError() == 183;                // 183 = ERROR_ALREADY_EXISTS
```

Creates a named mutex `"Die Gilde"` with initial-owner = TRUE. The handle is written
back through the `eax` out-pointer `a1`. The return value is the truthiness of
`GetLastError() == ERROR_ALREADY_EXISTS (183)`: a non-zero return means another instance
already holds the mutex, so WinMain aborts startup. Return `-1` only on outright
`CreateMutexA` failure.

- **Constants:** mutex name `"Die Gilde"`; `ERROR_ALREADY_EXISTS = 183`.
- **Imports:** `CreateMutexA @__imp 0x60e610`, `GetLastError @__imp 0x60e694`.
- **Reimpl:** Win32 → SDL/portable. A process-singleton primitive replaces `CreateMutexA`;
  the "already running" semantics are preserved 1:1.

---

## Step 2 — Core subsystems + movie DLL

### `VIBE_App_InitSubsystemsAndMovieDll @0x527de0` (returns `1`)

Brings up the engine's foundation services and loads the movie back-end. Sequence:

1. **Error log** — `VIBE_ErrorLog_Init @0x438a98` (args: shutdown callback
   `VIBE_Game_ShutdownAllSubsystems @0x52794c`, a null sub `VIBE_Util_NullSub @0x527ddc`,
   flags `16`, and the main window handle `dword_63CC18`).
2. **Memory tracker** — `VIBE_Memory_InitTracker(32678) @0x43937c`.
3. **Startup memory pool** — `VIBE_MemPool_StartupStack(0x80) @0x44e420`.
4. **Movie DLL** — if `dword_63C8F0` (movie enabled), `LoadLibraryA("moveahead.dll")` and
   resolve nine exports into global function pointers (table below).
5. **Screenshots dir** — `sprintf(buf, "%sgamedata\\Screenshots", "\project\gfx\")` then
   `VIBE_File_CreateDirectory @0x5eb920`.
6. **VFS** — `VIBE_Vfs_Init("\project\gfx\", …) @0x451f98`.
7. **Timer** — `VIBE_TimeBase_StartTimer(0xE, 0) @0x44e240`.

#### Error log — `VIBE_ErrorLog_Init @0x438a98` (`__usercall`)

Installs a crash handler and prepares the per-run log. It calls
`SetUnhandledExceptionFilter(TopLevelExceptionFilter @0x437ea4)`, reads the window title
(`GetWindowTextA`) and the module path (`GetModuleFileNameA`, truncated at the last `\`),
formats a banner `"* ----…* \n[%s], Date: %s"` with the window title and `ctime`, and —
when flag bit `1` is set — opens `"_error.log"` (`@0x62d85c`) next to the exe in append
mode; if it exceeds `0x10000` bytes it is truncated to a fresh file with
`"LogFile has been deleted!\n"`. When flag bit `8` is set it calls `AllocConsole()` and
caches `GetStdHandle(STD_ERROR_HANDLE)`. Finally it flushes 48 pre-queued records via
`VIBE_ErrorLog_WriteRecord @0x437ab0`.
- **Reimpl:** the exception filter and console are Win32; logging logic itself is
  reconstructed. `SetUnhandledExceptionFilter`/`AllocConsole` map to portable equivalents.

#### Memory tracker — `VIBE_Memory_InitTracker @0x43937c`

Stores the tracker capacity (`32678`) in `dword_62D9DC`, allocates `16 * capacity` bytes
via `VIBE_Memory_AllocFromFreeList @0x5dbe70`, zeroes it, and labels the root allocation
block `"_main_:"` (`@0x615b5c`). Reconstructed 1:1 (pure engine bookkeeping).

#### Startup memory pool — `VIBE_MemPool_StartupStack @0x44e420`

Allocates `132 * count` bytes (`count = 0x80`) tagged `"tr_startup"` (`@0x618e70`) via
`VIBE_Memory_AllocDebug @0x438f10`, stores the base/end in `dword_62EB5C`/`dword_62EB60`,
and fills the region with a 132-byte template record (two label fields copied with
`VIBE_Util_StrNCopyPad`). Returns `-1` on zero size, `-2` if already inited, `-3` on OOM.
Reconstructed 1:1.

#### VFS — `VIBE_Vfs_Init @0x451f98` (`__usercall`)

Scans the asset root (`VIBE_Vfs_ScanDirectory("\project\gfx\", 0) @0x450234`), stores the
resulting directory handle in `dword_62EB78`, records the working dir, and logs
`vfs_init: root -> "%s" relative to "%s"`. Returns `true` iff the scan produced a handle.
- **Reimpl:** reconstructed 1:1 (engine file I/O). See [29 — VFS](29-vfs-fileio-compression.md).

#### Screenshots dir — `VIBE_File_CreateDirectory @0x5eb920`

Thin wrapper over `CreateDirectoryA(path, 0)`; on failure returns
`VIBE_File_MapLastError() @0x5fe744`. Used here to ensure
`\project\gfx\gamedata\Screenshots` exists. Reconstructed 1:1; the OS call maps to a
portable filesystem call.

#### Timer — `VIBE_TimeBase_StartTimer @0x44e240`

Win32 multimedia timer: `timeGetDevCaps` → `timeBeginPeriod(wPeriodMin + 5)` →
`timeSetEvent(delay, wPeriodMin+5, fptc @0x44e130, …)`. Stores the delay/timer-id in
`uDelay`/`uTimerID`, and seeds the RNG (`timeGetTime` → `VIBE_Util_RandSeed @0x5cb8e0`).
Returns `1` on success, `0` if the requested period is below the device minimum.
- **Reimpl:** Win32 multimedia timer → **SDL** timer; the periodic callback `fptc` and RNG
  seeding are preserved 1:1.

#### Movie DLL — `moveahead.dll` (`@0x6229ac`)

Loaded only when `dword_63C8F0` is set. The handle is stored in `hModule @0x122f494` and
each export is resolved with `GetProcAddress` into a dedicated global function pointer.
All export names carry a trailing underscore.

| Export name | String addr | Stored in global | Purpose (deep coverage: [06 — Movie](06-movie-intro.md)) |
|-------------|-------------|------------------|---------|
| `mov_Init_` | `0x6229bc` | `dword_63C768 @0x63C768` | Initialize the movie player |
| `mov_Exit_` | `0x6229c8` | `dword_63C76C @0x63C76C` | Shut down / tear down the player |
| `mov_Play_` | `0x6229d4` | `dword_63C770 @0x63C770` | Start playback of a prepared movie |
| `mov_Stop_` | `0x6229e0` | `dword_63C774 @0x63C774` | Stop the currently playing movie |
| `mov_SetVisible_` | `0x6229ec` | `dword_63C788 @0x63C788` | Show/hide the movie surface |
| `mov_GetEvent_` | `0x6229fc` | `dword_63C784 @0x63C784` | Poll playback events (end-of-clip, etc.) |
| `mov_Prepare_` | `0x622a0c` | `dword_63C778 @0x63C778` | Prepare/open a movie (generic) |
| `mov_PrepareDD_` | `0x622a1c` | `dword_63C77C @0x63C77C` | Prepare a movie bound to a DirectDraw surface |
| `mov_Dispose_` | `0x622a2c` | `dword_63C780 @0x63C780` | Free a prepared movie |

If `LoadLibraryA` fails, none of the pointers are set and movie features stay disabled.
- **Reimpl:** `moveahead.dll` is a vendor codec DLL; the reimpl substitutes a **pl_mpeg**
  shim exposing the same nine entry points (rule-6 decision). The DirectDraw-bound
  `mov_PrepareDD_` path is the one that becomes a Vulkan/SDL surface.

---

## Step 3 — Display, render device & asset paths

### `VIBE_Render_InitDisplayAndPaths @0x527fa4` (`__usercall`, `int a1@<eax>` = display variant)

`a1` selects the display/network variant (values `3` and `4` take special branches; see
below). Deep render coverage is [23 — Render universe chain](23-render-universe-chain.md);
this is the bring-up summary.

1. **Read config** — copies a default settings template (`dword_5271F8`) then
   `VIBE_Config_ReadGfxAndSoundSettings @0x56b834`.
2. **Enumerate display modes** — `VIBE_Render_EnumDisplayModes @0x43371c`. Three
   capability flags (`byte_62D599/62D59A/62D59B`) report which renderer back-ends
   (selected by `byte_63D724`: 0/1/2) are available; if none, the function returns 0
   (fatal). Picks the chosen mode's width/height into `dword_63D728`/`dword_63D72C`.
3. **Window sizing** — `SystemParametersInfoA(SPI_GETWORKAREA=0x30,…)` for the work area,
   builds a client `RECT` of the chosen resolution, then
   `GetWindowLongA(GWL_EXSTYLE=-20)` + `GetWindowLongA(GWL_STYLE=-16)` +
   `AdjustWindowRectEx` to get the full window size, and centers it via `SetWindowPos`.
   Variants `3`/`4` pass `SWP_NOACTIVATE|0x2` (the network/special case); others pass `0`.
   Pumps messages (`VIBE_Window_PumpMessages @0x4bea64`).
4. **Build the device descriptor** — fills a 24-dword struct `v26` with resolution, bit
   depth `16`, near/far/FOV-ish floats, etc., then
   `VIBE_Render_InitEngineDevice(v26, hwnd, hInstance) @0x5af984` (returns 0 → fatal).
5. **Cameras** — `VIBE_Universe_CreateDefaultCameras @0x5b5f48`; hide the cursor
   (`ShowCursor(0)` loop).
6. **Input** — `VIBE_Input_DirectInputInit @0x40ca38`: variant `3` inits with mode `1`
   and sets wheel base `80` (`VIBE_Input_SetWheelBase @0x40c870`); otherwise mode `6`.
7. **Asset base paths** — installs the runtime search prefixes:
   `VIBE_Texture_SetBasePath("textures/") @0x5d995c`,
   `VIBE_Mesh_SetActiveTexturePath("objects/") @0x5d1020`, and copies
   `"animations/"`, `"groups/"`, `"scripts/"`, `"forms\"`, `"x:\engine\gfx\scripts\"`
   into engine globals.
8. **Apply gfx settings & load GUI** — `VIBE_Render_ApplyGfxSettings @0x56be58`, then
   `VIBE_Gui_LoadGfxFile(…, "gilde.gfx") @0x41b888`. On success it records the window
   dims, runs `VIBE_Widget_InitSystem @0x4200f8`, pumps messages, returns `1`.

- **Reimpl:** DirectDraw/Direct3D device creation and DirectInput are the swap points —
  the device descriptor and projection setup are reconstructed 1:1, but
  `VIBE_Render_InitEngineDevice` is backed by **Vulkan** and the window/input by **SDL**.
  `EnumDisplayModes`, `AdjustWindowRectEx`, `SetWindowPos`, `ShowCursor` → SDL.

---

## Step 4 — Text engine, world data, audio & script commands

### `VIBE_App_InitEngineAndScriptCommands @0x528560` (returns `1`)

The largest init step. Order of operations:

#### 4a. Text definition engine

Builds the base name `"gilde_text"` (`@0x622ab0`) and formats
`"%s%s.def"` against `"\project\game\"` (`aProjectGame @0x63ca0c`) → e.g.
`\project\game\gilde_text.def`. If `dword_63C7D4` is set it uses
`VIBE_Text_BuildTextArray @0x44bb5c`; otherwise it opens the engine with
`VIBE_Text_LoadDefinitionFile @0x44b8f4` and, on failure, reports
`"main_OpenEngine(): could not open txt-engine!"` (`@0x622ac8`).

`VIBE_Text_LoadDefinitionFile @0x44b8f4` opens the `.def` (mode `"rt"`), reads it line by
line (`VIBE_Text_ReadLine @0x5e9e30`), extracts quoted file names (strips at `.`),
collects up to 128 entries (512-byte stride; aborts past `0x10000`), and loads each
referenced text file via `VIBE_Text_LoadTextFile @0x44dba0`. On open failure it pops
`MessageBoxA("Could not open Textfiledefinition!", "Error", MB_ICONHAND)`.
Reconstructed 1:1 (engine text format). See [22 — Script engine](22-script-engine.md) for
adjacent text/string handling.

#### 4b. Loading screen + misc bring-up

Frees chronicle files (`VIBE_History_FreeChronicleFiles @0x4fd194`), shows the
`"Misc\Loading_main"` (`@0x622af8`) loading form via `VIBE_GameTick_Finalize @0x41beb8`,
positions/lays out widgets, renders a rich status string, connects networking
(`VIBE_Net_ConnectToServer @0x43b51c`), queues command init
(`VIBE_Command_QueueInitAndSync @0x4931e0`).

#### 4c. World building & object data

`sprintf(buf, "%sdata\\", "\project\game\")` → `VIBE_World_LoadBuildingAndObjectData @0x5835f8`.
That function: seeds a per-building price table (1000 defaults), allocates building PROT
(`f3_gm:lpGebaeudePROT`, `0xA5A8` bytes) and INST (`f3_gm:lpGebaeudeINST`, `0xA900`)
buffers and an object INST buffer (`f3_gm:lpObjekteINST`, `0x86000`), reads
`"%s%sA_Geb.dat"` (`A_Geb.dat @0x62643c`, 72 records of `0x24D` bytes) and
`"%s%sA_Obj.dat"` (`A_Obj.dat @0x62647c`, 731 records of `0x41` bytes) in binary mode
(`"rb"`), then `VIBE_World_InitBuildingTypeTable @0x5833b4` and
`VIBE_Building_ResetAllBuildings @0x5896fc`, and finally walks every building counting
type-2 occupied slots into a per-building counter. Returns negative codes on alloc/open
failure. Reconstructed 1:1 (engine data format + file I/O).

Then it computes seven initial market prices via
`VIBE_Building_ComputeMarketPrice @0x58f3d0` for object ids
`469, 471, 468, 474, 473, 470, 472` (each at quantity `100 / 0x64`).

#### 4d. Audio bring-up (Miles / MSS32 → SDL)

Guarded by `dword_63C900` (digital audio enabled):

| Step | Function | Address | Notes |
|------|----------|---------|-------|
| Start Miles driver | `VIBE_Audio_StartupMilesDriver` | `0x449840` | `AIL_startup()` + `AIL_last_error()`; sets `dword_62EADC` |
| Init sound library | `VIBE_Sound_LibInit` | `0x445d90` | args `(buf=&unk_989680, 48 voices, 2, 44100 Hz)`; opens digital output, allocates a 48-byte-per-voice handle table; logs `"sblib successfully initialzied !…"` |
| 3D sound pool | `VIBE_Sound3d_InitPool` | `0x424538` | 64 emitters |
| Sine tables | `VIBE_SoundWave_InitSineTables` | `0x424d40` | `0x30` entries |
| Bank path | `VIBE_Sound_SetBankPath` | `0x4463d8` | `"%ssfx\\"` under game dir; appends a trailing `\` if absent |
| Load sample bank | `VIBE_Sound_LoadSampleBank` | `0x446b2c` | `"system.sbf"` (`@0x622b1c`); parses a `0x144`-byte header + `0x40`-byte entries, links into the bank list, logs `"Samplebank %s loaded…"` |
| Preload from include | `VIBE_Sound_PreloadFromIncludeFile` | `0x52f154` | `"%s\include_sfx.ini"`; per line either loads a sample bank or, for the `"sprache\"` (`@0x6232fc`) prefix, a language/voice bank (`VIBE_Voice_LoadLanguageBank @0x582074`); up to 96 entries |

If `VIBE_Sound_LibInit` succeeds it *clears* `dword_63C900`/`dword_63C8F8` (the inverse-of
intuition flags). When streaming audio is enabled (`dword_63C8F8`) it additionally calls
`VIBE_Sound_InitThread(2, …, 44100) @0x439bf8` and sets the music track prefix
`"%smsx\\"` (`VIBE_Audio_SetTrackNamePrefix @0x43a95c`).

`VIBE_Audio_StartupMilesDriver` calls Miles `AIL_startup`/`AIL_last_error`
(`__imp__AIL_startup@0 @0x60e800`, `__imp__AIL_last_error@0 @0x60e780`).
`VIBE_Sound_InitThread` uses `InitializeCriticalSection`, two digital outputs,
`QueryPerformanceFrequency`, and `CreateThread(StartAddress @0x439b00)` for the streaming
mixer.
- **Reimpl:** all of Miles/MSS32 (`AIL_*`) → **SDL audio** (rule 5). The bank/sample
  formats (`.sbf`, `include_sfx.ini`), 3D pool, sine tables, and the streaming-thread
  bookkeeping are reconstructed 1:1; only the digital-output back-end and the
  `CriticalSection`/`CreateThread`/`QueryPerformanceFrequency` primitives are swapped.
  Deep coverage: [30 — Audio](30-audio.md).

Then volume settings are applied (`VIBE_Audio_ApplyVolumeSettings @0x56c148`).

#### 4e. Game-state init + intro fade

Registers the cutscene tick proc, resets spawn tables, registers character-action
handlers (4f below), builds the snow texture, inits guard state and default combat
parameters, destroys the loading form, then registers a black fade
(`VIBE_Fade_Register(…, "BLACK", 90, 1) @0x41f0e8`) and spins
`VIBE_GameLogic_RunFrameLoop @0x4c09a0` until the fade completes, then
`VIBE_Fade_Unregister @0x41f18c` and zeroes a 140-byte state block.

#### 4f. Script command registration

`byte_63CC1C = 1`, then `VIBE_Script_ConsoleParseLine @0x4453a4` followed by five
registration clusters. Each registers named engine-script commands through
`VIBE_Script_ImportCommand @0x445bc8` (per-command: arg-type tuple, handler address,
name string, and an opcode/flags word). Deep coverage:
[22 — Script engine](22-script-engine.md).

| Cluster | Function | Address | # cmds | Representative commands |
|---------|----------|---------|-------:|-------------------------|
| Core/flow | `VIBE_Script_RegisterCommands` | `0x43c850` | 14 | `ecmd_Dummy`, `Print`, `PrintInt`, `PrintFloat`, `GetTime`, `RunScript`, `RunScriptInt`, `RunScriptString`, `StopScript`, `Sleep`, `FindScript`, `KillLocalScripts`, `CallUserFunction`, `CallUserFunctionExtended` |
| Object/scene | `VIBE_Script_RegisterObjectCommands` | `0x440618` | ~62 | `CreateObject`, `CreateObjectAtDummy`, `CreateObjectGroupAtDummy`, `KillObject`, `SetPos`, `SetAngle`, `MoveObject`, `RotateObject`, `AttachAnim`, `AttachAnimLooped`, `LoadScene`, `SetLightSet`, `SetFogSet`, `CameraFlight`, `ZoomOnObject`, `NewCameraFlight`, `CreateRain`, `CreateParticle`, `SetCameraToDummy`, `RenameObject`, `SetBlocked`, `SetTransient`, … |
| Character | `VIBE_Character_RegisterScriptCommands` | `0x43dfb0` | ~39 | `CreateCharacter`, `CreateCharacterAtDummy`, `KillCharacter`, `WalkToDummy`(`Rotate`/`Verified` variants), `PlayAnimation`(`Sound`/`Script`/`ScriptInt`), `TakeObject`, `GiveObject`, `DropObject`, `SitDown`, `GetUp`, `SetCharacterCamera`, `LookAtCharacter`, `LookAtObject`, `SetCharacterToDummy`, `AttachObject`, `SetCharacterTransperancy`, … |
| Sound | `VIBE_Script_RegisterSoundCommands` | `0x440df0` | 9 cmds + tokens | `LoadSampleBank`, `KillSampleBank`, `PlaySampleHandle`, `PlaySample`, `PlaySample3D`, `StopSample`, `GetSampleBankHandle`, `GetSampleHandle`, `SpeechQueued`; plus event tokens (`VIBE_Script_AddEventToken @0x441140`) `SND_W2/W1/M1/M2/M3/HS/NONE/VAR` |
| Char actions | `VIBE_CharAction_RegisterHandlers` | `0x40be30` | — | Builds the action queue (`"ch:Open:ActionQueue" @0x610a00`) and declares movement actions (`VIBE_Character_DeclareAction @0x405558`), e.g. `bewegung/gehen` (walk) and `bewegung/dreh_90_rechts` (turn 90° right) |

After registration it stores two callback globals
(`off_64A904`, `dword_64A094`), re-applies camera/scroll settings
(`VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc`), pumps messages, and returns `1`.

- **Reimpl:** the entire script command table is engine logic — reconstructed 1:1, no
  tech swap. Each command's handler address, arg-type tuple, and opcode must match the
  binary exactly.

---

## Cross-references

- [01 — Entry point & WinMain](01-entrypoint-and-winmain.md) — caller
  `VIBE_GameLogic_MainEntryAndShutdown @0x534bbc`.
- [06 — Movie / intro](06-movie-intro.md) — `moveahead.dll` exports in depth.
- [22 — Script engine](22-script-engine.md) — script command system & text engine.
- [23 — Render universe chain](23-render-universe-chain.md) — display/device bring-up.
- [29 — VFS, file I/O & compression](29-vfs-fileio-compression.md) — `VIBE_Vfs_Init`.
- [30 — Audio](30-audio.md) — Miles/MSS32 → SDL audio bring-up.
