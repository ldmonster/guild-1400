# 07 — Shutdown sequence

When *Die Gilde* exits, it does not just call `ExitProcess`. `WinMain`
(`VIBE_GameLogic_MainEntryAndShutdown @0x534bbc`) runs a **fixed, ordered teardown ladder**
of fifteen calls that walk the engine back down from the high-level game world to the raw
heap and the OS window. The ordering is not cosmetic: each step depends on the ones before
it still being alive (you free the lightmap *tables* before you free the *heap tracker* that
owns them; you persist the config *before* you tear down the data it was read from), and the
two diagnostic steps at the bottom (`VIBE_Memory_ShutdownTracker`, `VIBE_ErrorLog_Shutdown`)
must run last so they can still report leaks that any earlier step left behind.

This exact ladder is **textually duplicated at every exit path of `WinMain`** — each
init-failure bailout (window create, subsystem/movie-DLL init, display init, engine/script
init), the normal "quit requested" path (`dword_63CC48`), and the resolution-switch-failure
path all inline the same fifteen calls in the same order. It is **also** run by
`VIBE_GameLogic_InitOrLoadSession @0x533a54` on its city-load-failure exit, so a failed
session entry tears the whole process down through the identical sequence. See
[01 — Entry Point & WinMain](01-entrypoint-and-winmain.md) for where these exit paths sit in
the control flow.

---

## Global flags raised before the ladder runs

Before the teardown calls, `WinMain` sets two globals that change how the rest of the engine
behaves while it is being dismantled:

| Flag | Set to | Gates |
| --- | --- | --- |
| `byte_63CC1C @0x63CC1C` | `0` | The "GUI/menu active" flag. Cleared so input/widget code stops treating the (about-to-be-destroyed) widget system as live. |
| `dword_62D314 @0x62D314` | `1` | The "loading / non-interactive" flag (the same one `InitOrLoadSession` raises around `VIBE_Loading_ShowProgressScreen`). Raised so per-frame/scene code that might still be pumped during teardown takes the inert path. |

On the normal quit path the loop sentinel `dword_63CC48 @0x63CC48` is what selects the
teardown branch (`v23 = dword_63CC48; if ( dword_63CC48 ) { … ladder … return 1; }`), and
`dword_631610`/`dword_631614` are zeroed just before it. `dword_63CC38` is the
resolution-change request that diverts to the special re-init branch.

---

## The ordered teardown ladder

Steps run top to bottom. "Frees / does" is what the function tears down; "Why here" is the
ordering constraint.

| # | Function | Frees / does | Why this order |
| --- | --- | --- | --- |
| 0 | `VIBE_Game_ShutdownSubsystems @0x5278cc` | High-level game teardown: re-persists gfx settings, unloads all 96 audio sample banks (`dword_122EF18[]`), shuts the script engine (`VIBE_Script_ShutdownEngine`), unregisters the cutscene tick proc, and — gated on the sound flags — shuts the 3D sound lib + pool (`dword_63C900`), the sound mixer (`dword_63C8F8`), and the wave tables. Then frees all text files, all game-object tables, the char-action queue, and resets the command queue. | Runs **first**: it drains the live game/audio/script subsystems that sit on top of the renderer and resources. (On three `WinMain` bailout paths it is *not* called because those subsystems were never brought up yet.) |
| 1 | `VIBE_Config_WriteGfxSettings @0x56af54` | **WRITES** config back to `gilde.INI` (`byte_122F638`): every `[Gfx]` (texture_scale, details, lod_handling, shadow_detail, cur_res, brightness/contrast/gamma R/G/B/A …), `[Sound]` (master/sfx/msx/speech vol, msx_freq) and `[Game]` (speed, mouse/scroll/camera speed, invert_mouse, stadt, historie, mission, difficulty, hints …) value via `WritePrivateProfileStringA`. | Must run **before** any resource/world teardown: it reads the current in-memory settings (`byte_1233xxx`, `flt_1233xxx`) and persists them while they are still valid. (It is also called once inside step 0, so settings are flushed twice — harmless, value-identical.) |
| 2 | `VIBE_Widget_ShutdownSystem @0x4201f4` | Destroys the GUI back-buffer surface (`dword_75BEE0`), the 3 cached widget surfaces (`dword_75BF1C[]`, resetting their dirty rects to `-1`), and the main GUI compositing surface (`dword_75BEFC`) via `VIBE_Surface_Destroy`. | The GUI sits on DirectDraw surfaces; tear it down before the resource pools and the render device that own/back those surfaces. Runs right after `byte_63CC1C = 0` so nothing repaints into a freed surface. |
| 3 | `VIBE_GameState_FreeAllResources @0x40e308` | Unregisters all 32 fade entries (`dword_672280[]`), destroys every active widget instance (the 22848-byte table + ids 0..510 via `VIBE_Widget_DestroyByType`), frees the 172032-byte texture-slot table (`dword_62D204`, releasing each block and decrementing `dword_62D20C`), destroys the four panel surfaces (`dword_62D210/214/218/21C`), frees the shape light table, releases the cursor texture (`dword_62D268`), closes the open VFS entry (`dword_62D2A0`), and frees a string of misc buffers (`dword_62D26C`, `dword_69FFB4`, `dword_62D2D0/2D4/2D8/2DC`). | Frees the per-game-state heap blocks and UI resources that the widget layer (step 2) referenced. Done before the lightmaps and the 3D engine so the surfaces/textures are released while DDraw is still up. |
| 4 | `VIBE_Universe_SwitchActiveSlot(0, …) @0x5b4a24` | Called with **slot 0** and `a2 = live` so it takes the *free* path: frees the staged scene buffer (`dword_1408A78`), flushes the previously-active universe slot's camera/scene snapshot, and (since `a2`/no-rebuild) frees or rebuilds the heightmap (`dword_64A048`). Effectively swaps the active scene out and releases its terrain/camera state. | The scene graph must be parked on slot 0 and its terrain freed **before** `VIBE_Render_ShutdownEngine` walks all 64 slots disposing objects — this puts the universe in the known state that step 6 iterates from. |
| 5 | `VIBE_Table_ResetLightmaps @0x42e19c` | Frees four parallel 1116-byte lightmap pointer tables (`dword_75E9F0`, `dword_75EE4C`, `dword_75F2A8`, `dword_75F704`) via `VIBE_Memory_FreeDebug`, zeroing their backing arrays (`dword_75E974`, `dword_75EDD0`, `dword_75F22C`, `dword_75F688`). | Lightmaps are scene-lighting heap blocks; free them after the scene is parked (step 4) but before the render engine and the heap tracker go away — they are tracked allocations and must be released while `VIBE_Memory_FreeDebug` still works. |
| 6 | `VIBE_Render_ShutdownEngine @0x5b0228` | The **3D device teardown** (original: DirectDraw / Direct3D; in this port, the Vulkan device — see platform boundary below). Gated on `byte_649D71`. Disposes the root render object, then for every populated universe slot (0..63) switches to it and disposes its object list, frees the floor/terrain buffers (`dword_64A028`), destroys the sky (`dword_64A7C8`) and per-slot allocations; then releases the global object lists, the animation mesh data (`dword_13FC760`), staged buffers (`dword_13FC584`, `dword_13FC51C`), global sky, shadow buffers, **both texture caches** (`VIBE_TextureCache_Shutdown` + `_d9c98`), resets global light state, and finally releases the device objects (`byte_649D7C`) and the DDraw/Vulkan surfaces (`VIBE_Render_ReleaseSurfaces`). | The whole renderer. Runs after every higher-level consumer of GPU resources (widgets, game-state surfaces, lightmaps, scene) is gone, so nothing references a surface/texture/device object after it is freed. This is the GPU-API boundary tear-down. |
| 7 | `VIBE_Input_DirectInputShutdown @0x40cd40` | **Input teardown** (original: DirectInput; in this port, SDL/DInput shim). Clears `hWnd`, un-acquires the keyboard and mouse devices, releases the DInput device COM objects (`dword_672164/672160/67216C` via their vtable `Release` at +8), and closes the input event handle (`hObject`). | Input is released after the render window's device objects (step 6) but while the OS window still exists (the window is destroyed last). Devices were bound to that window, so they go down here in the middle. |
| 8 | `VIBE_TimeBase_StopTimer @0x44e2c4` | **winmm multimedia timer** teardown. Kills the periodic timer (`timeKillEvent(uTimerID)`), zeroes `uTimerID`, and ends the timer-resolution period (`timeEndPeriod(ptc.wPeriodMin + 5)`). | Stops the multimedia-timer callback so no scheduled tick fires into subsystems that are now (steps 0–7) torn down. Must precede the VFS/heap teardown a timer callback might touch. (Some bailout paths that fail before the window even exists *omit* this step.) |
| 9 | `VIBE_Vfs_Shutdown @0x452004` | **Virtual file system** teardown ([29 — VFS](29-vfs-fileio-compression.md)). Frees the VFS node tree (`dword_62EB78`), and — if `a1` set — reports `"%i vfs_file(s) were left open!"` via the error log when `dword_62EB88 != 0`; closes every still-open stream from the used-handle pool (`dword_62EB80`), then frees the file-record pool (`VIBE_MemPool_FreeAll(&dword_62EB7C)`) and clears `byte_62EB84`. | Files/streams are closed after every subsystem that could still be reading assets (audio, script, render, scene) has shut down. It reports leaked file handles, which is why it runs while the error log (step 13) is still alive. |
| 10 | `VIBE_MemPool_ShutdownStack @0x44e544` | Frees the **stack/scratch mempool** ([28 — Memory](28-memory-management.md)): if the pool top (`dword_62EB64`) is sane (`>= base dword_62EB5C`), frees it via `VIBE_Memory_FreeDebug` and zeroes base/cursor/top (`dword_62EB5C/60/64`); otherwise `__debugbreak()` (a corrupted-pool assertion). | The scratch stack pool is freed after VFS (which allocated record blocks from the general heap, not this pool) and just before the heap tracker is torn down — it is itself a tracked allocation. |
| 11 | `VIBE_Memory_ShutdownTracker @0x439640` | **Heap allocation tracker** teardown — **prints a leak report** ([28 — Memory](28-memory-management.md)). If any blocks remain (`dword_62D9E4`/`dword_62D9E0`/`dword_62D9F0`), it logs `"[*] Memory-Shutdown-Anomaly: [*] Used %i blocks in %i bytes. %i block(s) left in %i bytes."`, optionally dumps group stats, and for each un-freed pointer logs `"-> Pointer not freed:%s"` and reclaims it. Then it returns the whole tracking node list and the descriptor table (`dword_62D9F4`) to the free list and zeroes the counters. | Must run **after every other free** (steps 0–10), because it audits what is still allocated and reports leaks. Anything freed earlier removed itself from the tracker; whatever is left at this point is a genuine leak. It still needs the error log (step 13) alive to print into. |
| 12 | `VIBE_ErrorLog_Shutdown @0x438c0c` | Writes the final `"Error-Handler closed.\n"` record for each of 48 log sinks (`dword_62D82C[]`), and if console logging was on (`byte_62D828 & 8`) calls `FreeConsole()` and clears `hConsoleOutput`. Clears the log state (`dword_62D820`, `dword_764834`, `byte_62D828 = 0`). | The error/diagnostic log is the **last** subsystem torn down, because every earlier step (VFS leak warning, memory leak report) writes into it. Once it closes, no further diagnostics are possible. |

After the ladder, `WinMain` performs the final OS-level cleanup:

| # | Action | Does |
| --- | --- | --- |
| 13 | `dword_63C76C()` then `FreeLibrary(hModule)` | **Unloads `moveahead.dll`** (the intro-movie DLL), but only if `dword_63C8F0` (the `show_intro` config flag) and the module handle `::hModule @0x122F494` are both set. `dword_63C76C` is the DLL's own shutdown export (resolved at load); it is called first to let the DLL release its resources, *then* `FreeLibrary` drops the module. |
| 14 | `VIBE_Window_DestroyAndUnregisterClass @0x527868` | Destroys the main window (`DestroyWindow(dword_63CC18)`) and unregisters its window class (`UnregisterClassA("Die Gilde", hInstance)`). The OS window is destroyed **last**, after input devices, the render device, and the movie DLL — all of which were bound to it. |
| 15 | Screensaver restore | If `dword_63CC64 @0x63CC64` was non-zero (the screensaver had been active and `WinMain` disabled it at startup via `SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE…)`), it is re-enabled with `SystemParametersInfoA(0x11, 1, 0, 2)`. This pairs with the startup save of `pvParam` into `dword_63CC64`. |

---

## Platform boundary (technology swaps)

The original binary's shutdown touches several vendor technologies; in this reimplementation
those are swapped behind the `shim/` interfaces, while the **order and the engine-side logic
stay 1:1**:

- **Step 6 — 3D device** (`VIBE_Render_ShutdownEngine`): original tears down DirectDraw /
  Direct3D surfaces and device objects; the port releases the **Vulkan** device/surfaces via
  `IGraphicsDevice`. The slot-walk and free order are reconstructed unchanged.
- **Step 7 — input** (`VIBE_Input_DirectInputShutdown`): original releases **DirectInput**
  COM devices; the port un-acquires and releases via the **SDL** input shim (`IPlatform`).
- **Step 8 — timer** (`VIBE_TimeBase_StopTimer`): original uses **winmm** (`timeKillEvent` /
  `timeEndPeriod`); the port routes through the SDL timer shim.
- **Step 0 — audio** (inside `VIBE_Game_ShutdownSubsystems`): original shuts down the **Miles
  Sound System (MSS32)** lib/pool/wave tables; the port shuts down the **SDL** audio device.
- **Steps 9–11 — VFS / mempool / heap tracker** are pure engine logic (no vendor API) and are
  reconstructed exactly; see [28 — Memory](28-memory-management.md) and
  [29 — VFS](29-vfs-fileio-compression.md).
- **Steps 13–14 — `moveahead.dll` unload and window destroy** are Win32; `FreeLibrary` /
  `DestroyWindow` / `UnregisterClassA` map onto the platform/library shims. The movie DLL
  itself is handled per the project's video decision (pl_mpeg shim).

---

## Why the duplication matters

Because the ladder is copy-pasted into every `WinMain` exit path (and into
`VIBE_GameLogic_InitOrLoadSession`), there is no single "shutdown function" in the binary —
the sequence *is* the contract. Any reconstruction must reproduce the same fifteen steps in
the same order at each exit site, including the early bailout variants that legitimately
**skip** `VIBE_Game_ShutdownSubsystems` and/or `VIBE_TimeBase_StopTimer` when those
subsystems were never started. The flag writes (`byte_63CC1C = 0`, `dword_62D314 = 1`) are
part of the sequence and appear at every site too.
