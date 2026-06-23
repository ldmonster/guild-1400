# 05 — Window & platform input

How `gilde.exe` creates its main window, pumps the Win32 message loop, reads the
mouse/keyboard through DirectInput, and drives its periodic game tick through the
`winmm` multimedia timer. Everything here is reconstructed from the IDA Pro
decompilation of the original binary (imagebase `0x400000`); this is the description
of the **original** behavior, with each OS/vendor dependency marked as an SDL/host swap
for the reimplementation.

Cross-links: [01 — Entry point & WinMain](01-entrypoint-and-winmain.md) ·
[14 — Per-frame loop](14-per-frame-loop.md) · [15 — Game time & tick](15-game-time-tick.md).

---

## Summary

The platform layer is split into three subsystems, each driven from the top-level
startup sequence ([01](01-entrypoint-and-winmain.md)):

| Subsystem | Original tech | Key roots | SDL/host swap |
|-----------|---------------|-----------|---------------|
| Window + message pump | Win32 `user32` (`RegisterClassA`/`CreateWindowExA`/`PeekMessageA`/`DispatchMessageA`/`DefWindowProcA`) | `VIBE_Window_CreateMainWindow` `@0x52895c`, `VIBE_Window_MainWndProc` `@0x5279dc`, `VIBE_Window_PumpMessages` `@0x4bea64`, `VIBE_Window_DestroyAndUnregisterClass` `@0x527868` | SDL window + `SDL_PollEvent` pump |
| Input | DirectInput 3 (`dinput.DirectInputCreateA`, `IDirectInputDevice` mouse + keyboard, buffered immediate data) | `VIBE_Input_DirectInputInit` `@0x40ca38`, `VIBE_Input_PollMouseDevice` `@0x40d388`, `VIBE_Input_PollKeyboardDevice` `@0x40d920`, `VIBE_Input_BuildScancodeTable` `@0x40c710` | SDL mouse/keyboard events |
| Periodic tick | `winmm` multimedia timer (`timeSetEvent`/`timeBeginPeriod`) | `VIBE_TimeBase_StartTimer` `@0x44e240`, `fptc` `@0x44e130`, `VIBE_TimeBase_StopTimer` `@0x44e2c4`, `VIBE_TimeBase_SetProcInterval` `@0x44e3ac` | SDL/host timer thread |

Call order at startup (from [01](01-entrypoint-and-winmain.md)):
`VIBE_App_InitSubsystemsAndMovieDll @0x527de0` starts the multimedia timer →
`VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` creates the window →
`VIBE_Render_InitDisplayAndPaths @0x527fa4` brings up DirectInput.

---

## 1. Window creation — `VIBE_Window_CreateMainWindow @0x52895c`

Prototype is `__usercall`: `HWND f(HINSTANCE hInst@<eax>, int displayMode@<ebx>)`.
The caller in `VIBE_GameLogic_MainEntryAndShutdown @0x53516e` loads
`eax = hInstance`, `ebx = displayMode` from its locals before the call.

### 1.1 Window class registration

A `WNDCLASSA` is zero-filled by `VIBE_Light_SetGrayColorThunk @0x5c6af0` (used here as
a 40-byte `memset(&WndClass, 0, 0x28)`; `sizeof(WNDCLASSA)==0x28`), then populated:

| `WNDCLASSA` field | Value | Source |
|-------------------|-------|--------|
| `lpfnWndProc` | `VIBE_Window_MainWndProc @0x5279dc` | set as immediate `offset` at `0x528978` |
| `hInstance` | `hInst` (eax arg) | |
| `style` | `0` | |
| `cbClsExtra` / `cbWndExtra` | `0` / `0` | |
| `hIcon` | `LoadIconA(hInst, "gilde")` | icon resource name `"gilde"` @`0x622b4c` |
| `hCursor` | `LoadCursorA(0, 0x7F00)` | `0x7F00` = `IDC_ARROW` |
| `hbrBackground` | `GetStockObject(4)` | `4` = `LTGRAY_BRUSH` |
| `lpszMenuName` | `NULL` | |
| `lpszClassName` | `"Die Gilde"` | class-name string @`0x622968` |

`RegisterClassA(&WndClass)` is called at `0x5289fc`; on failure (`ax==0`) the function
returns `0` immediately.

> **SDL swap:** no window class is registered under SDL. The class name `"Die Gilde"`,
> the `"gilde"` icon resource, `IDC_ARROW`, and `LTGRAY_BRUSH` background have no SDL
> analogues; the window proc is replaced by the SDL event-pump dispatch (§3).

### 1.2 Title and desktop work-area

- `VIBE_Text_FormatBuildVersionString @0x527c68` fills the version string at
  `byte_122F218`, then `VIBE_Crt_Sprintf_0` formats the title with `"Die Gilde - %s"`
  (@`0x622b54`) into a 128-byte local `WindowName`.
- `SystemParametersInfoA(SPI_GETWORKAREA=0x30, 0, &workarea, 0)` (@`0x528a42`) fetches
  the desktop work-area rectangle into locals `pvParam/Y/nWidth/nHeight` (left/top/
  right/bottom). The windowed path uses `width = nWidth - left`, `height = nHeight - top`.

### 1.3 `displayMode` arg → fullscreen vs. windowed (`DIRECTWINDOW`)

The `ebx` arg selects two `CreateWindowExA` configurations:

| `displayMode` | dwExStyle | dwStyle | Position / size |
|---------------|-----------|---------|-----------------|
| `== 3` (fullscreen / "DIRECTWINDOW") | `8` = `WS_EX_TOPMOST` | `0x92000000` = `WS_POPUP \| WS_VISIBLE \| WS_CLIPCHILDREN` | x=0, y=0, full work-area `nWidth × nHeight` |
| anything else (windowed) | `0` | `0x06000000` = `WS_CLIPSIBLINGS \| WS_CLIPCHILDREN` | x=left, y=top, work-area-derived size |

`CreateWindowExA(...)` (@`0x528abf`) stores the `HWND` into `dword_63CC18` — the global
main-window handle used by the pump, WndProc, and input. On `NULL` the raw handle is
returned (failure).

### 1.4 Post-create placement

On success: `ShowWindow(hWnd, SW_SHOW=5)`, `UpdateWindow`, `SetFocus`.

- **Fullscreen (`displayMode==3`):** `SetWindowPos(hWnd, HWND_TOPMOST(-1), 0,0,
  GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOACTIVATE)` —
  cover the whole screen, top-most. (In the decompiler this reads as
  `HWND_MESSAGE|0x2`; the literal is `0xFFFFFFFF` = `HWND_TOPMOST`.)
- **Windowed:** `GetClientRect`, then `MoveWindow` centering a client area of
  `dword_63D728 × dword_63D72C` (the requested back-buffer resolution) on the work-area,
  followed by `SetWindowPos(hWnd, 0, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE=3)`.

Returns `(HWND)1` (a truthy non-handle sentinel) on success — the caller only tests
truthiness.

---

## 2. The window procedure — `VIBE_Window_MainWndProc @0x5279dc`

`__fastcall LRESULT MainWndProc(... HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)`.
Decompiled as a range-dispatch (`Msg < 0x14` / `<= 0x209` / `>= 0x218` / `== 0x7E7`).
Every message not in the table below falls through to
`DefWindowProcA(hWnd, Msg, wParam, lParam)`.

### 2.1 Messages handled

| Msg | Name | Handling |
|-----|------|----------|
| `0x0002` | `WM_DESTROY` | `PostQuitMessage(0)`, return 0 |
| `0x000F` | `WM_PAINT` | `ValidateRect(hWnd, NULL)`, return 0 |
| `0x0010` | `WM_CLOSE` | `PostQuitMessage(0)`, return 0 (shares the `WM_DESTROY` branch) |
| `0x0014` | `WM_ERASEBKGND` | `ValidateRect(hWnd, NULL)`, return 1 (suppress background erase) |
| `0x001C` | `WM_ACTIVATEAPP` | Focus gain/loss — see §2.2 |
| `0x0200`–`0x0208` | `WM_MOUSEMOVE` … `WM_MBUTTONDBLCLK` (the `0x14 < Msg < 0x209` window) | `ValidateRect(hWnd, NULL)`, return 1 — mouse messages are swallowed; real mouse input comes from DirectInput (§4) |
| `0x0218` | `WM_POWERBROADCAST` | if `wParam==0` (`PBT_APMQUERYSUSPEND`) return `0x424D5144` (`'QMDB'`, a deny/ack magic); else `DefWindowProcA` |
| `0x07E7` (2023) | private app message | `VIBE_Audio_ReacquireDigitalDriver(wParam, hWnd, 0x7E7)`, return 0 — Miles audio re-acquire on focus regain |

> The exact magic `0x424D5144` returned for `WM_POWERBROADCAST/PBT_APMQUERYSUSPEND`
> must be preserved for 1:1 behavior even though its practical effect is just "non-zero".

### 2.2 `WM_ACTIVATEAPP` (focus) — `0x001C`

Gated by `byte_63CC14` (a "deactivated" latch) and `byte_63CC1C` (a "graphics
initialized" flag set during startup).

- **Activating (`wParam != 0` and `byte_63CC14` set):** if graphics are up
  (`byte_63CC1C`), spin until the rendering surface is restored
  (`while(!VIBE_Render_IsSurfaceLost())`), optionally re-decompress/re-present the
  framebuffer (`VIBE_DecompressState_Blob @0x423500` → `VIBE_Render_PresentFrame
  @0x4349e4`), clear the pause flag `dword_62EB4C = 0`, re-acquire all Miles digital
  drivers (`VIBE_Audio_ReacquireAllDigitalDrivers`), and re-acquire the DirectInput
  mouse (`VIBE_Input_AcquireMouseDevice(1)`). Then clear `byte_63CC14`.
- **Deactivating (`wParam == 0`, not already latched):** set `byte_63CC14 = 1`; if
  graphics are up, unacquire the mouse (`VIBE_Input_AcquireMouseDevice(0)`), release the
  audio drivers (`VIBE_Audio_ReleaseAllDigitalDrivers`), set the pause flag
  `dword_62EB4C`, then `SetWindowPos(hWnd, HWND_BOTTOM(1), 0,0,0,0,
  SWP_NOMOVE|SWP_NOSIZE)` to drop the window to the back.

> **SDL swap:** `WM_ACTIVATEAPP` maps to `SDL_EVENT_WINDOW_FOCUS_GAINED/LOST`. The
> surface-lost/restore spin (`VIBE_Render_IsSurfaceLost`) was a DirectDraw concept;
> under Vulkan it becomes swapchain-out-of-date handling, but the acquire/release of
> input and audio on focus transitions, and the pause-flag toggling, are preserved.

---

## 3. Message pump — `VIBE_Window_PumpMessages @0x4bea64`

```
while (PeekMessageA(&Msg, dword_63CC18, 0, 0, PM_NOREMOVE=0)) {
    if (GetMessageA(&Msg, dword_63CC18, 0, 0)) {   // != 0 => normal message
        TranslateMessage(&Msg);
        DispatchMessageA(&Msg);
    } else {                                        // 0 => WM_QUIT
        dword_63CC48 = 1;                           // quit-requested latch
    }
}
return 1;
```

- `Msg` is the global `MSG` at `0x11bc230`; the pump is filtered to the main window
  `dword_63CC18`.
- `PeekMessageA` with `PM_NOREMOVE` only checks presence; the actual dequeue is via
  `GetMessageA`. A `WM_QUIT` makes `GetMessageA` return 0, which sets the global
  quit flag `dword_63CC48 = 1` (read by the per-frame loop, [14](14-per-frame-loop.md)).
- Called once per frame from the main loop; it drains all pending messages then returns.

> **SDL swap:** replace the entire `PeekMessage/GetMessage/Translate/Dispatch` loop with
> an `SDL_PollEvent` drain. `SDL_EVENT_QUIT` sets the same `dword_63CC48`-equivalent
> latch; window-focus events route to the §2.2 logic; mouse/keyboard events are ignored
> here (input is read directly from SDL state, mirroring the DirectInput design).

---

## 4. Input — DirectInput

### 4.1 Initialization — `VIBE_Input_DirectInputInit @0x40ca38`

`__usercall f(HWND hWnd@<eax>, int hInstance@<edx>, int flags@<ebx>)`. Called from
`VIBE_Render_InitDisplayAndPaths @0x528355` with `eax = dword_63CC18` (main HWND),
`edx = hInstance`, `ebx = 1`.

Setup sequence (each step on failure logs via `VIBE_Input_FormatErrorMessage @0x40c950`
with a descriptive string, then aborts returning 0):

1. `DirectInputCreateA(hInstance, 0x0300, &dword_67216C, NULL)` — DirectInput **v3**
   (`0x0300`); stores the `IDirectInput*` in `dword_67216C`. Distinguishes
   `DIERR_BETADIRECTINPUTVERSION` / `INVALIDPARAM` / `OLDDIRECTINPUTVERSION` /
   `OUTOFMEMORY` error strings.
2. **Mouse device:** `CreateDevice(GUID_SysMouse @0x64cf68, &dword_672160)` →
   `SetDataFormat(c_dfDIMouse @0x64de98)` → `SetCooperativeLevel(hWnd, level)` where
   `level = (flags & 2) ? 0x0A : 0x05`. `0x05` =
   `DISCL_EXCLUSIVE|DISCL_FOREGROUND`; `0x0A` = `DISCL_NONEXCLUSIVE|DISCL_BACKGROUND`.
   Then `SetEventNotification(0)`, `SetProperty(DIPROP_BUFFERSIZE=1, ...buffer 128)`,
   and `VIBE_Input_AcquireMouseDevice(1)`. Seeds the packed cursor position
   `dword_6721C4 = 13107400` (≈ 200,200 in 16.16 fixed) and `word_6721C8 = 0`.
3. **Keyboard device:** `CreateDevice(GUID_SysKeyboard @0x64cf58, &dword_672164)` →
   `SetDataFormat(c_dfDIKeyboard @0x64deb0)` → `SetCooperativeLevel(hWnd, 0x0A)` →
   `SetProperty(buffer size 128)` → `VIBE_Input_AcquireKeyboardDevice(1)`.
4. `VIBE_Input_BuildScancodeTable @0x40c710` — see §4.5.

Globals: `dword_67216C` = `IDirectInput`, `dword_672160` = mouse device,
`dword_672164` = keyboard device, `hWnd` (@`0x6721c0`) = cached window for cursor
warping, `dword_62D0D8` = the saved `flags`.

> **SDL swap:** all of DirectInput (`DirectInputCreateA`, the two `CreateDevice` GUIDs,
> the `dfDIMouse`/`dfDIKeyboard` data formats, cooperative-level, buffered immediate
> data) is replaced by SDL relative-mouse + keyboard-state. Cooperative-level/exclusive
> semantics become `SDL_SetWindowRelativeMouseMode`. The HRESULT error-string branches
> (`DIERR_*`) have no SDL equivalent and are dropped.

### 4.2 Per-frame mouse read — `VIBE_Input_PollMouseDevice @0x40d388`

Drains the mouse device's **buffered** data with
`IDirectInputDevice::GetDeviceData(sizeof(DIDEVICEOBJECTDATA)=16, &buf, &count, 0)`
(vtable slot `+0x28`) in a loop until empty. Each `DIDEVICEOBJECTDATA.dwOfs` is decoded:

| `dwOfs` | Object | Effect |
|---------|--------|--------|
| `0` (`DIMOFS_X`) | X axis | scaled by `dword_62D0B8 * flt_610C5C`, accumulated into packed cursor X (`LOWORD(dword_6721C4)`) and `word_6721CA` |
| `4` (`DIMOFS_Y`) | Y axis | same scaling into packed cursor Y (`HIWORD(dword_6721C4)`) and `word_6721CC` |
| `8` (`DIMOFS_Z`) | wheel | `(dword_62D0B8 * delta) >> 8` accumulates `word_6721C8`; sets `dword_672208` (up) / `dword_672204` (down) |
| `12` (`DIMOFS_BUTTON0`) | left button | down (`0x80` bit) → `dword_6721D4=1`, edge `dword_6721DC`; up → `dword_6721D0=1` |
| `14` (`DIMOFS_BUTTON1`) | right button | down → `dword_6721E8=1`, `dword_6721F0=1`; up → `dword_6721E4=1` |

Each button transition calls `VIBE_Input_SaveMouseButtonSnapshot @0x40d338` and
`VIBE_Input_ProcessMouseClicks @0x40cdd0`. After draining, the packed cursor is clamped
to the configured rect `[dword_62D0CC/D0 .. dword_62D0C4/C8]`. If `dword_62D0D8 & 4`
(a "hardware cursor" mode bit) the code instead syncs to the OS cursor via
`GetCursorPos`/`ScreenToClient` (or warps it back with `ClientToScreen`/`SetCursorPos`).

### 4.3 Cursor warp / axis latch — `VIBE_Input_ReadMouseAxes @0x40d7c8`

Reads a single immediate `GetDeviceData` (`+0x24` vtable slot, 16-byte record), applies
HRESULT error logging (Access-Lost `0x8007001E`-style codes mapped to the strings
"Access to Input Device Lost", "Invalid Param", "Device not aquired", "Device not
initialized", "Data not available"), updates the relative delta into the packed cursor
(`dword_6721C4` / mirror `dword_672174`), then `ClientToScreen` + `SetCursorPos` warps
the OS cursor to match — and sets `dword_62D0D4 = 1` to flag that the next poll should
re-latch from the OS cursor.

### 4.4 State latch / snapshot — `VIBE_Input_LatchMouseState @0x40dab8` and `VIBE_Input_ResetMouseButtonState @0x40c87c`

- `VIBE_Input_LatchMouseState` copies the working cursor (`dword_672174`/`word_672178`)
  into the published cursor (`dword_672210`/`word_672214`) when `dword_62D0D4` is set,
  zeroes the per-frame button/edge accumulators, then walks a ring of 32 × 76-byte
  input-event records (base `dword_670FEC`, stride 76, count `2432/76`) copying the
  first armed slot (`dword_671028[i]`) into the published mouse-state block at
  `dword_67221C..dword_672254` (buttons, edges, double-clicks, wheel, X/Y), disarms that
  slot, and finishes by calling `VIBE_Input_PollKeyboardDevice(0)`.
- `VIBE_Input_ResetMouseButtonState` clears all current/edge/published button flags
  (`dword_6721D0..`, `dword_672184..`, `dword_67221C..`) to a neutral state — used on
  focus loss and mode changes.

`VIBE_Input_PollMouseAndKeyboard @0x40da88` is the simple combined entry: it calls
`VIBE_Input_PollMouseDevice`, mirrors the working cursor (`dword_672174` →
`dword_672210`), then `VIBE_Input_PollKeyboardDevice(0)`.

### 4.5 Keyboard read — `VIBE_Input_PollKeyboardDevice @0x40d920` and scancode table `VIBE_Input_BuildScancodeTable @0x40c710`

- `VIBE_Input_PollKeyboardDevice` drains buffered keyboard `GetDeviceData` (`+0x28`,
  16-byte records). Each record's high bit of the data byte = key-down; it maintains a
  256-entry down map `byte_671D60[]` and a press map `byte_671F60[]`, tracks the
  last-pressed scancode `byte_67225C`/`byte_62D100`, and implements key-repeat using the
  tick counter `dword_62EB44` (initial delay `+9` ticks via `dword_672260`, repeat every
  `+5`). Returns the active scancode (or `-1` on release of a stuck key).
- `VIBE_Input_BuildScancodeTable` precomputes `word_671960[256]`: for each ASCII char
  `VkKeyScanA` → virtual key + shift-state (remapping the `0x0600` Ctrl+Alt flag to
  `0x04`), then `MapVirtualKeyA(vk, 0)` to a scancode; `-1` for unmapped chars. This is
  the ASCII→DirectInput-scancode lookup used to interpret keystrokes.

> **SDL swap:** buffered DirectInput keyboard data → SDL key events; the
> `VkKeyScanA`/`MapVirtualKeyA` scancode table → SDL keycode/scancode mapping. The
> custom repeat timing (delay 9 / rate 5 ticks) is preserved on top of SDL events
> (i.e. driven by the same `dword_62EB44` tick counter, not SDL's own key-repeat).

### 4.6 Acquire / shutdown

- `VIBE_Input_AcquireMouseDevice @0x40c9b8` / `VIBE_Input_AcquireKeyboardDevice @0x40c9f8`
  (`__usercall ... @<eax>=acquire?`): if the device exists, call vtable `+0x1C`
  (`Acquire`) when arg!=0 or `+0x20` (`Unacquire`) when arg==0.
- `VIBE_Input_DirectInputShutdown @0x40cd40` clears `hWnd`, unacquires both devices, then
  `Release`s (vtable `+0x08`) the keyboard device, mouse device, and the `IDirectInput`
  object (in that order), and `CloseHandle`s the event object `hObject` if present.

---

## 5. Periodic tick — `winmm` multimedia timer

This is the heartbeat that advances game time and dispatches registered periodic procs.
See [15 — Game time & tick](15-game-time-tick.md) for how the tick counters drive game
logic; this section documents the timer plumbing.

### 5.1 Start — `VIBE_TimeBase_StartTimer @0x44e240`

`__usercall f(UINT period@<eax>, int oneShotFlag@<edx>)`. Called from
`VIBE_App_InitSubsystemsAndMovieDll @0x527e52`.

1. `timeGetDevCaps(&ptc, 8)` — query timer resolution into `ptc` (@`0xb537c0`).
2. `timeBeginPeriod(ptc.wPeriodMin + 5)` — raise the system timer resolution to
   `min + 5` ms.
3. Reject if requested `period < ptc.wPeriodMin` (return 0).
4. Save `uDelay = period` (@`0xb53948`), `dword_62EB50 = oneShotFlag`.
5. `uTimerID = timeSetEvent(period, ptc.wPeriodMin + 5, fptc, 0, fuEvent)` where
   `fuEvent = (oneShotFlag == 0)` → `TIME_ONESHOT(0)` when a repeating chain is wanted
   (the callback re-arms itself, §5.2), else `TIME_PERIODIC(1)`. The callback is
   `fptc @0x44e130`.
6. On success: prime `timeGetTime()` and `VIBE_Util_RandSeed`, return 1.

### 5.2 Callback — `fptc @0x44e130` (`__stdcall` `LPTIMECALLBACK`)

Re-entrancy guarded by `dword_62EB48`. When not paused (`dword_62EB4C == 0`):

- Increment the raw tick `dword_62EB44`.
- Walk the 32-slot proc table (`dword_B537C8[]`, stride 3 dwords, 96 dwords total):
  for each registered proc whose interval `dword_B537CC[i]` divides the master tick
  `dword_62EB38`, and which isn't suspended (`dword_B537D0[i]==0`), **call it**.
- Sub-rate counters: every 3rd tick bumps `dword_62EB3C`; every odd tick bumps
  `dword_62EB40`; the master tick `dword_62EB38` advances every tick, or every 20th when
  the slow-mode flag `byte_62EB54` is set.
- If `dword_62EB50` (chain mode), re-arm via
  `timeSetEvent(uDelay, ptc.wPeriodMin+5, fptc, 0, TIME_ONESHOT)`.

### 5.3 Register a proc — `VIBE_TimeBase_SetProcInterval @0x44e3ac`

`__usercall f(int procAddr@<eax>, int suspendFlag@<edx>)`. Linear-scans the 32-slot
table (`dword_B537C8`, stride 3) for `procAddr`; on match sets the suspend flag
`dword_B537D0[slot] = suspendFlag` and returns 1, else 0. (The interval and proc pointer
themselves are populated by the companion registration path; this entry toggles
enable/suspend.)

### 5.4 Stop — `VIBE_TimeBase_StopTimer @0x44e2c4`

`timeKillEvent(uTimerID)` (if a timer is live), clear `uTimerID`, and
`timeEndPeriod(ptc.wPeriodMin + 5)` to undo the resolution bump.

> **SDL swap:** the `winmm` multimedia timer (`timeSetEvent`/`timeKillEvent`/
> `timeBeginPeriod`/`timeEndPeriod`/`timeGetDevCaps`) runs `fptc` on a separate
> high-resolution OS timer thread. Under the reimplementation this becomes an
> SDL/host timer (or a dedicated tick thread); the re-entrancy guard `dword_62EB48`,
> the proc-table dispatch, the master/sub-rate tick counters, and the
> `min + 5 ms` resolution intent are all preserved. The `oneShotFlag`/self-re-arm
> chain is an implementation detail of the winmm one-shot model and folds into the
> host timer's natural periodic firing.

---

## Key globals

| Global | Addr | Meaning |
|--------|------|---------|
| `dword_63CC18` | `0x63cc18` | main window `HWND` |
| `dword_63CC48` | `0x63cc48` | quit-requested latch (set on `WM_QUIT`) |
| `byte_63CC14` | `0x63cc14` | app-deactivated latch (`WM_ACTIVATEAPP`) |
| `byte_63CC1C` | `0x63cc1c` | graphics-initialized flag |
| `dword_672160` / `dword_672164` / `dword_67216C` | | DI mouse / keyboard / `IDirectInput` |
| `hWnd` | `0x6721c0` | cached HWND for cursor warping |
| `dword_6721C4` | `0x6721c4` | packed 16.16 cursor position (working) |
| `dword_672210` | `0x672210` | published cursor position |
| `byte_671D60` / `byte_671F60` | | keyboard down map / press map |
| `word_671960` | `0x671960` | ASCII→scancode table |
| `uTimerID` | `0x62eb34` | active `timeSetEvent` id |
| `uDelay` | `0xb53948` | timer period (ms) |
| `dword_62EB44` | `0x62eb44` | raw tick counter (drives key repeat) |
| `dword_62EB38` | `0x62eb38` | master game tick |
| `dword_62EB4C` | `0x62eb4c` | tick-paused flag (set on focus loss) |
| `dword_B537C8` | `0xb537c8` | periodic-proc table (32 × 3 dwords) |
