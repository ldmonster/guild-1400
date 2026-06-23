# 06 — Movie / Intro Playback

How `gilde.exe` plays its startup logo reel, the in-game intro, and the
end-of-game outro using the external **`moveahead.dll`** ("MoveAhead") video
player. This document describes the *original binary* — the reimplementation
swaps the MoveAhead player for a `pl_mpeg` shim (Rule 6 decision), but must
reproduce the behavior documented here.

Cross-links: [01 — Entry point / WinMain](01-entrypoint-and-winmain.md) ·
[04 — App init subsystems](04-app-init-subsystems.md) ·
[23 — Render universe chain](23-render-universe-chain.md)

---

## 1. Summary

The game does not contain its own video codec. All cutscene/intro video is
delegated to a small companion DLL, **`moveahead.dll`**, loaded at runtime with
`LoadLibraryA` and bound by name with `GetProcAddress`. Nine exports
(`mov_Init_`, `mov_Exit_`, `mov_Play_`, `mov_Stop_`, `mov_SetVisible_`,
`mov_GetEvent_`, `mov_Prepare_`, `mov_PrepareDD_`, `mov_Dispose_`) are stored in
a contiguous block of global function-pointer slots `0x63C768..0x63C788`. The
movie files themselves are MPEG (`*.mpg`) under the configured **MoviePath**
(default `\project\movie\`).

Whether the movie subsystem runs at all is gated by a single global,
`dword_63C8F0` — the value of the `gilde.INI` `[General] show_intro` flag.

There are exactly two play sites in the live call tree:

| Function | Address | Movies played | When |
|---|---|---|---|
| `VIBE_Movie_PlayIntroSequence` | `0x5347D4` | `jowood.mpg`, `4head.mpg`, `intro.mpg` | once at startup, after subsystem init, before the render display is brought up |
| `VIBE_Movie_PlayOutro` | `0x534924` | `outro.mpg` | game over / end sequence |

Both are reached from the application's main driver
`VIBE_GameLogic_MainEntryAndShutdown` @ `0x534BBC` (see
[01](01-entrypoint-and-winmain.md)).

A crucial detail for the Vulkan swap (Rule 3): the MoveAhead player blits
directly to a **DirectDraw surface** — that is the purpose of the
`mov_PrepareDD_` export. See [§6](#6-directdraw-surface-handoff-and-the-vulkan-swap).

---

## 2. The `moveahead.dll` exports and their global pointer slots

Resolution happens in **`VIBE_App_InitSubsystemsAndMovieDll`** @ `0x527DE0`
(documented in [04](04-app-init-subsystems.md)). The relevant block:

```c
if ( dword_63C8F0 )                                   // show_intro from gilde.INI
{
    hModule = LoadLibraryA(aMoveaheadDll);            // "moveahead.dll"
    if ( hModule )
    {
        dword_63C768 = GetProcAddress(hModule, "mov_Init_");
        dword_63C76C = GetProcAddress(hModule, "mov_Exit_");
        dword_63C770 = GetProcAddress(hModule, "mov_Play_");
        dword_63C774 = GetProcAddress(hModule, "mov_Stop_");
        dword_63C788 = GetProcAddress(hModule, "mov_SetVisible_");
        dword_63C784 = GetProcAddress(hModule, "mov_GetEvent_");
        dword_63C778 = GetProcAddress(hModule, "mov_Prepare_");
        dword_63C77C = GetProcAddress(hModule, "mov_PrepareDD_");
        dword_63C780 = GetProcAddress(hModule, "mov_Dispose_");
    }
}
```

`hModule` is stored at the global `0x122F494` (`hModule`). The DLL name string is
`aMoveaheadDll` @ `0x6229AC` = `"moveahead.dll"`.

### Export → global pointer → purpose

| Export name (string addr) | Global slot | Used by | Purpose |
|---|---|---|---|
| `mov_Init_` (`0x6229BC`) | `dword_63C768` | Intro, Outro | One-time player init; called once per playback session before any `Prepare`/`Play` |
| `mov_Exit_` (`0x6229C8`) | `dword_63C76C` | shutdown paths only | Tear down the player at app shutdown; called as `dword_63C76C(...)` right before `FreeLibrary` |
| `mov_Play_` (`0x6229D4`) | `dword_63C770` | Intro, Outro | Run a prepared clip to completion (blocking; the prepared handle is passed in `eax`) |
| `mov_Stop_` (`0x6229E0`) | `dword_63C774` | (resolved, not invoked in live tree) | Abort current playback |
| `mov_SetVisible_` (`0x6229EC`) | `dword_63C788` | (resolved, not invoked) | Show/hide the player surface |
| `mov_GetEvent_` (`0x6229FC`) | `dword_63C784` | (resolved, not invoked) | Poll a player event (skip/keypress/finished) |
| `mov_Prepare_` (`0x622A0C`) | `dword_63C778` | Intro, Outro | Open/prepare a clip from a file path; returns a handle stored in `dword_63C764` |
| `mov_PrepareDD_` (`0x622A1C`) | `dword_63C77C` | (resolved, not invoked) | Prepare a clip bound to a caller-supplied DirectDraw surface |
| `mov_Dispose_` (`0x622A2C`) | `dword_63C780` | (resolved, not invoked) | Free a prepared clip handle |

The nine slots are contiguous DWORDs in `.data`:

```
0x63C764  dword_63C764   <- last prepared-clip handle (return of mov_Prepare_)
0x63C768  dword_63C768   mov_Init_
0x63C76C  dword_63C76C   mov_Exit_
0x63C770  dword_63C770   mov_Play_
0x63C774  dword_63C774   mov_Stop_
0x63C778  dword_63C778   mov_Prepare_
0x63C77C  dword_63C77C   mov_PrepareDD_
0x63C780  dword_63C780   mov_Dispose_
0x63C784  dword_63C784   mov_GetEvent_
0x63C788  dword_63C788   mov_SetVisible_
```

> **Resolved-but-unused exports.** `xrefs_to` on `dword_63C774` (Stop),
> `dword_63C77C` (PrepareDD), `dword_63C780` (Dispose), `dword_63C784`
> (GetEvent) and `dword_63C788` (SetVisible) shows their *only* references are
> the two `GetProcAddress` assignment sites (in `VIBE_App_InitSubsystemsAndMovieDll`
> and the lazy-load block of `VIBE_Movie_PlayOutro`). They are never **called**
> anywhere in the binary. The shipped game therefore plays movies in the
> simplest possible way: `Init → Prepare → Play (blocking) → Play(end)`; it never
> polls `mov_GetEvent_`, never uses the DirectDraw-surface variant, and relies on
> `mov_Play_` itself to handle the user-skip and run-to-completion logic
> internally. (This matters for the shim: skip handling lives inside the player,
> not in `gilde.exe`.)

---

## 3. `VIBE_Movie_PlayIntroSequence` @ 0x5347D4 (root)

The startup logo/intro reel. Decompilation (cleaned):

```c
int __thiscall VIBE_Movie_PlayIntroSequence(void *this)
{
    _BYTE pathbuf[264];

    Sleep(0x190);                                          // 400 ms settle
    dword_63C768();                                        // mov_Init_()

    // --- clip 1: JoWooD publisher logo ---
    VIBE_Crt_Sprintf_0(pathbuf, "%sjowood.mpg", aProjectMovie);   // \project\movie\jowood.mpg
    dword_63C764 = dword_63C778(10, 0, 10, 220, 220, 138);        // mov_Prepare_(...)
    Sleep(0x64);                                                  // 100 ms
    dword_63C770();                                               // mov_Play_()  (blocking)

    // --- clip 2: 4HEAD Studios developer logo ---
    Sleep(0x190);                                          // 400 ms
    VIBE_Crt_Sprintf_0(pathbuf, "%s4head.mpg", aProjectMovie);    // \project\movie\4head.mpg
    dword_63C764 = dword_63C778(10, 0, 10, 220, 220, 138);
    Sleep(0x64);
    dword_63C770();

    // --- clip 3: the game intro cinematic ---
    Sleep(0x190);                                          // 400 ms
    VIBE_Crt_Sprintf_0(pathbuf, "%sintro.mpg", aProjectMovie);    // \project\movie\intro.mpg
    dword_63C764 = dword_63C778(10, 0, 10, 220, 220, 138);
    VIBE_Window_PumpMessages();                            // drain Win32 queue
    Sleep(0x64);
    dword_63C770();
    return VIBE_Window_PumpMessages();
}
```

### Step by step

1. **`Sleep(400 ms)`** — lets the freshly-created window/display settle before
   the first clip.
2. **`mov_Init_()`** (`dword_63C768`) — one-time player init.
3. For each of the three clips, the same pattern:
   - **Build the path** with `VIBE_Crt_Sprintf_0(buf, "%s<name>.mpg", aProjectMovie)`.
     `aProjectMovie` @ `0x63CB10` is the **MoviePath**, default `"\project\movie\"`,
     overridable via `gilde.INI` (see [§5](#5-the-show_intro-gate-and-moviepath)).
     The three files are `jowood.mpg`, `4head.mpg`, `intro.mpg`.
   - **`mov_Prepare_(...)`** (`dword_63C778`) — opens the clip; its return (a clip
     handle) is stashed in `dword_63C764`. See the argument layout below.
   - **`Sleep(100 ms)`**, then **`mov_Play_()`** (`dword_63C770`) — runs the clip
     to completion. `mov_Play_` is **blocking**: control returns only when the
     clip finishes or the user skips it.
4. Between clips there is a **`Sleep(400 ms)`** transition gap.
5. For the third clip, `VIBE_Window_PumpMessages` (@ `0x4BEA64`) is called before
   and after to keep the Win32 message pump alive (so the OS doesn't mark the app
   "not responding" during the longer cinematic).

### `mov_Prepare_` argument layout (from disassembly @ 0x534828)

The Hex-Rays prototype renders `mov_Prepare_` as a 6-arg `__fastcall`, but the
actual call site is a custom register/stack mix:

```asm
push 8Ah           ; 138   -> stack arg
push 0DCh          ; 220   -> stack arg (height)
push 0DCh          ; 220   -> stack arg (width)
mov  ecx, 0Ah      ; 10    -> ecx  (x position)
xor  edx, edx      ; 0     -> edx  (y position)
push 0Ah           ; 10    -> stack arg
lea  eax, [path]   ;        -> eax  (pointer to the formatted "...mpg" path)
mov  ebx, hInstance;        -> ebx  (app HINSTANCE)
call ds:dword_63C778
```

So `mov_Prepare_` receives: the **path string in `eax`**, x/y in `ecx`/`edx`
(10, 0), the app **`HINSTANCE` in `ebx`** (global `0x122F52C`), and width=220,
height=220, plus a mode/flag value `138` (`0x8A`) on the stack. The literal `10`
appears both as `ecx` and as a stack arg. The window placement is fixed at
(10,10) with a 220×220 region. The return value (clip handle) is the only thing
the caller keeps.

### Callers

`xrefs_to 0x5347D4` → a single caller: `VIBE_GameLogic_MainEntryAndShutdown`
@ `0x5352AB`, guarded by `if ( dword_63C8F0 )`:

```c
if ( !VIBE_App_InitSubsystemsAndMovieDll() ) { ...shutdown... }
if ( dword_63C8F0 )
    VIBE_Movie_PlayIntroSequence(...);     // <- here
if ( !VIBE_Render_InitDisplayAndPaths(...) ) { ...shutdown... }
```

i.e. the intro plays **after** subsystem/movie-DLL init and **before** the 3D
render display is initialized ([23](23-render-universe-chain.md)).

---

## 4. `VIBE_Movie_PlayOutro` @ 0x534924

The end-of-game cinematic (`outro.mpg`). It is more elaborate than the intro
because it must wrap the movie in audio fade, a black screen fade, and input
suspend/restore.

```c
char VIBE_Movie_PlayOutro(...)
{
    // (a) lazy-load moveahead.dll if not already loaded
    if ( !dword_63C8F0 )                       // NB: here used as "dll loaded?" latch
    {
        hModule = LoadLibraryA("moveahead.dll");
        if ( !hModule ) return 0;
        dword_63C768 = GetProcAddress(hModule, "mov_Init_");
        ... // resolve all nine exports exactly as in §2
        dword_63C8F0 = 1;
        dword_63C768();                        // mov_Init_()
    }

    VIBE_Music_SetTrackFade(0.0, 1000);        // fade music out over 1 s

    // (b) fade the screen to BLACK over 90 frames, running the frame loop
    for ( i = VIBE_Fade_Register(0, ..., "BLACK", 90, 1);
          (*i & 4) == 0 || flt_62DA00 >= 0.0; )
        VIBE_GameLogic_RunFrameLoop(...);

    VIBE_Crt_Sprintf_0(pathbuf, "%soutro.mpg", aProjectMovie);   // \project\movie\outro.mpg

    VIBE_Render_PresentFrame(...);             // flush a frame if needed
    // (c) suspend input while the movie owns the screen
    dword_62D0E0 = 1; VIBE_Input_PollKeyboardDevice(...); dword_62D0E0 = 0;
    VIBE_Input_AcquireMouseDevice(0, ...);     // release mouse
    VIBE_Window_PumpMessages();
    Sleep(0x1F4);                              // 500 ms

    // (d) play it
    dword_63C764 = dword_63C778(0, 0, 0, 0, 0, 138);   // mov_Prepare_  (full-screen: 0,0,0,0)
    Sleep(0xC8);                                       // 200 ms
    dword_63C770();                                    // mov_Play_()  (blocking)

    // (e) restore: unfade, refocus, re-acquire input, restore music volume
    VIBE_Fade_Unregister(i, ...);
    VIBE_Window_PumpMessages();
    SetFocus(dword_63CC18);                            // main HWND
    VIBE_Input_AcquireMouseDevice(1, ...);
    dword_62D0E0 = 1; VIBE_Input_PollKeyboardDevice(...);
    VIBE_Music_SetTrackFade(volume, 1000);            // music back up
    return v14;
}
```

Notable differences from the intro:

- **Lazy load.** If `show_intro` was 0 (so the DLL was *not* loaded at app init),
  the outro loads `moveahead.dll` on demand, resolves all nine exports, sets the
  latch `dword_63C8F0 = 1`, and calls `mov_Init_`. (Note the `dword_63C8F0`
  overload: it is the `show_intro` config flag *and* the "movie DLL loaded"
  latch — once the outro sets it, the shutdown paths will also call `mov_Exit_`/
  `FreeLibrary`.)
- **`mov_Prepare_(0,0,0,0,0,138)`** — x=0, y=0, width=0, height=0: full-screen /
  player-default sizing (the `138` mode flag is the same as the intro).
- It brackets the movie with **input suspend** (`VIBE_Input_*`), **screen fade**
  (`VIBE_Fade_Register`/`Unregister` to color `"BLACK"`), and **music fade**
  (`VIBE_Music_SetTrackFade`), none of which the intro bothers with.

`VIBE_Movie_PlayOutro` is invoked from the game-over branches; its callers also
sit under `VIBE_GameLogic_MainEntryAndShutdown` / the session loop.

---

## 5. The `show_intro` gate and MoviePath

Both governing globals are filled from `gilde.INI` in
`VIBE_GameLogic_MainEntryAndShutdown` @ `0x534BBC`:

```c
GetPrivateProfileStringA("General", "MoviePath", "\project\movie\",
                         aProjectMovie, 0x104, byte_122F638);   // -> aProjectMovie @0x63CB10
...
dword_63C8F0 = GetPrivateProfileIntA("General", "show_intro", 0, byte_122F638);
```

- **`MoviePath`** (`aMoviepath` @ `0x623714`) → stored in `aProjectMovie`
  @ `0x63CB10`, default `"\project\movie\"`. This is the `"%s"` prefix in every
  `Sprintf` that builds a clip path, so all six string templates
  (`%sjowood.mpg`, `%s4head.mpg`, `%sintro.mpg`, `%soutro.mpg`) resolve under it.
- **`show_intro`** (`aShowIntro` @ `0x62373C`) → `dword_63C8F0`, default `0`.
  When non-zero it (1) causes `VIBE_App_InitSubsystemsAndMovieDll` to load
  `moveahead.dll` and resolve the exports, and (2) gates the
  `VIBE_Movie_PlayIntroSequence` call. When zero, the intro is skipped entirely
  and the DLL is loaded lazily only if the outro runs.

The MPEG filenames are fixed string literals in `.rdata`; only the directory
prefix is configurable.

---

## 6. DirectDraw surface handoff, and the Vulkan swap

The MoveAhead player is a DirectDraw blitter. Two of its exports describe how it
gets a surface:

- **`mov_Prepare_`** (used) lets the player **own** its output: it is given a
  position/size rectangle (intro: 10,10 @ 220×220; outro: full screen) and a
  HINSTANCE in `ebx`, and the player creates/manages its own DirectDraw surface
  and presents the decoded frames there directly.
- **`mov_PrepareDD_`** (resolved, **not used** by the shipped game) is the
  variant that would accept a **caller-supplied DirectDraw surface** to blit
  into — i.e. composite the video into the game's own DDraw back buffer rather
  than a separate one.

Because the shipped game only ever calls `mov_Prepare_`, the original video
output path is entirely **inside `moveahead.dll`**, on its own DirectDraw
surface, independent of the engine's render target.

**Implication for the reimplementation (Rules 3 & 6).** There is no DirectDraw
surface to thread through from the engine side at the call sites — the only
contract `gilde.exe` relies on is "open this `.mpg`, then block until it finishes
or the user skips." The `pl_mpeg` shim therefore needs to reproduce:

1. the **blocking** `Play` semantics (return only at end-of-clip or on skip),
2. the **internal skip/keypress handling** (the game never polls `mov_GetEvent_`),
3. the **fixed window region** (10,10 220×220 for the intro logos; full screen
   for `intro.mpg`/`outro.mpg`), and
4. the surrounding **timing** (`Sleep` settle/transition gaps of 400/100/200/500
   ms) and `VIBE_Window_PumpMessages` interleave so the host window stays
   responsive.

The DirectDraw blit itself is replaced by presenting decoded frames through the
Vulkan render path ([23](23-render-universe-chain.md)); `mov_PrepareDD_`,
`mov_GetEvent_`, `mov_SetVisible_`, `mov_Stop_`, and `mov_Dispose_` do not need
reimplementing for behavioral parity, since the original binary never calls them.

---

## 7. Shutdown / `mov_Exit_`

The player is torn down in `VIBE_Game_ShutdownAllSubsystems` @ `0x52794C` (and
the inlined shutdown tails of `VIBE_GameLogic_MainEntryAndShutdown` and
`VIBE_GameLogic_InitOrLoadSession`):

```c
if ( dword_63C8F0 && hModule )
{
    dword_63C76C(...);      // mov_Exit_()
    FreeLibrary(hModule);   // unload moveahead.dll
}
```

So `mov_Exit_` is called exactly once on the way out, only if the DLL was loaded
(latch `dword_63C8F0` set and `hModule != 0`), immediately before
`FreeLibrary`. This is the only consumer of the `dword_63C76C` slot.

---

## 8. Provenance index

| Symbol | Address | Role |
|---|---|---|
| `VIBE_Movie_PlayIntroSequence` | `0x5347D4` | startup logo/intro reel (jowood/4head/intro) |
| `VIBE_Movie_PlayOutro` | `0x534924` | end-game cinematic (outro) |
| `VIBE_App_InitSubsystemsAndMovieDll` | `0x527DE0` | loads `moveahead.dll`, resolves exports |
| `VIBE_Game_ShutdownAllSubsystems` | `0x52794C` | calls `mov_Exit_` + `FreeLibrary` |
| `VIBE_GameLogic_MainEntryAndShutdown` | `0x534BBC` | reads INI, gates + drives intro |
| `aProjectMovie` (MoviePath) | `0x63CB10` | `"\project\movie\"` |
| `aShowIntro` | `0x62373C` / `dword_63C8F0` | `show_intro` flag + DLL-loaded latch |
| `aMoveaheadDll` | `0x6229AC` | `"moveahead.dll"` |
| clip-handle slot | `0x63C764` (`dword_63C764`) | last `mov_Prepare_` return |
| export pointer block | `0x63C768`–`0x63C788` | `mov_Init_` … `mov_SetVisible_` |
| movie filename templates | `0x62360C`/`0x62361C`/`0x623628`/`0x623634` | `%sjowood.mpg` / `%s4head.mpg` / `%sintro.mpg` / `%soutro.mpg` |
