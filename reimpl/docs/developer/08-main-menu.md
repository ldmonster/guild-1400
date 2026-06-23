# 08 — Main menu

This chapter documents the **main-menu state machine** of `gilde.exe` (Die Gilde /
Europa 1400, 32-bit x86, imagebase `0x400000`), as recovered from the IDA Pro
decompilation. It is a description of how the **original binary** behaves; it is not a
description of any reimplementation.

The root is **`VIBE_Menu_RunMainMenu @0x529d08`**. This function owns the title screen:
it loads two 3D scenes behind a radio-button menu, optionally plays a background MP3,
and then runs a per-frame loop that dispatches each menu click to a sub-screen routine.
Every sub-screen is itself a small loop built on the same per-frame primitive
(`VIBE_GameLogic_RunFrameLoop @0x4c09a0`, see [14 — Per-frame loop](14-per-frame-loop.md)),
and each one writes a **game-mode code** into the global `word_63C740 @0x63C740` that the
new-game / session bootstrap later reads.

Platform note: the menu draws a real 3D scene (the original used **DirectDraw / Direct3D**
fixed-function; this is swapped for **Vulkan** per project rule 3) and plays **MP3** music
through the original **Miles Sound System / MSS32** streaming layer (swapped for **SDL**
audio per rule 5). See [30 — Audio](30-audio.md) for the deep audio path.

Cross-links: [01 — Entry / WinMain](01-entrypoint-and-winmain.md) ·
[09 — New-game flow](09-newgame-flow.md) · [10 — GUI widget/form](10-gui-widget-form.md) ·
[14 — Per-frame loop](14-per-frame-loop.md) · [30 — Audio](30-audio.md).

---

## 1. `VIBE_Menu_RunMainMenu @0x529d08` — the title-screen loop

### 1.1 Prologue and fade-in

```
VIBE_Window_PumpMessages();           // 0x4bea64 — Win32 pump → SDL
VIBE_Input_LatchMouseState();         // 0x40dab8
byte_67225C = 0;                      // "back/ESC" edge flag, cleared
VIBE_Script_ResetCurrentHandle();     // 0x445d7c
v2 = VIBE_Fade_Register(0,0, H,W, "BLACK", 0, 1);   // 0x41f0e8
while ( (*(BYTE*)v2 & 4) == 0 )       // run frames until fade object reports "done"
    VIBE_GameLogic_RunFrameLoop(147591, 147591, a1);
```

The screen dimensions come from the packed framebuffer descriptor at
`dword_69FFB8`/`dword_69FFBC` (`>>16` extracts width/height). A fade-from-black overlay
named **`"BLACK"`** (`aBlack_2 @0x622b44`) is registered, and the menu spins the global
per-frame loop until that fade object's status byte has bit `4` set (animation finished).
The constant `147591` (`0x24087`) is the frame-loop's standard "menu context" argument
that recurs throughout every sub-screen.

### 1.2 Loading the two background scenes

The title screen is a **two-slot 3D scene** (universe "slots" 0 and 1):

```
dword_62D314 = 1;
VIBE_Universe_SwitchActiveSlot(0, …);     // 0x5b4a24
VIBE_Universe_ResetCurrentSlot(…);        // 0x5b44c4
VIBE_Scene_LoadFromStream("scenes/*ChooseCity.ed3", 0, …);   // 0x5e7e38
VIBE_Window_PumpMessages();
VIBE_Universe_SwitchActiveSlot(1, …);
VIBE_Universe_ResetCurrentSlot(…);
if ( !VIBE_Scene_LoadFromStream("scenes/*spielerauswahl.ed3", 0, …) )
    VIBE_ErrorLog_ReportMessage("main_Menue:Could not load 3D-scene...");
```

* Slot 0 ← **`scenes/*ChooseCity.ed3`** (`aScenesChooseci @0x622b84`) — the city/landscape
  backdrop, reused later by `VIBE_Map_LoadCityFile`.
* Slot 1 ← **`scenes/*spielerauswahl.ed3`** (`aScenesSpielera @0x622c7c`,
  *spielerauswahl* = "player selection") — the character/throne backdrop.
* If the second scene fails to load, the error
  **`"main_Menue:Could not load 3D-scene..."`** (`aMainMenueCould @0x622c98`) is logged via
  `VIBE_ErrorLog_ReportMessage @0x438da8`, but the menu continues.

The view transform is then set up (`VIBE_Render_SetupViewTransform @0x5af5f8`) from the
framebuffer size and three baked focal/clip floats (`flt_13FC76C`, `flt_13FCAFC`,
`flt_13FCD0C`), and a 140-byte scratch block at `dword_122F4A0` is zeroed (the inlined
`memset` loop). `word_63C740` is cleared to `0` (default game-mode code), and
`dword_631618 = dword_631610 + 1` bumps a scene-generation counter.

### 1.3 Building the radio-button menu (`MENU\MAIN_MENU`)

```
VIBE_Window_RenderEntityScene(0,0);   // 0x41523c
VIBE_Window_RenderEntityList(...);    // 0x4134f0
v68 = VIBE_GameTick_Finalize(0, 0, "MENU\\MAIN_MENU");   // 0x41beb8 — load the form
VIBE_Window_PositionAtCoord_Thunk(v68, 3);
VIBE_Form_SelectWindow(...);
```

The menu form is the resource **`MENU\MAIN_MENU`** (`aMenuMainMenu @0x622cc0`), loaded by
`VIBE_GameTick_Finalize @0x41beb8` (the generic "instantiate a `.ed3`/form resource and
return its window id" routine; see [10 — GUI widget/form](10-gui-widget-form.md)).

Menu entries are created as **sprite widgets** with `VIBE_Widget_AddSpriteToWindow @0x41217c`
at fixed vertical offsets (x=32, sprite id 174). Each widget gets field `+88` set to `1`
(radio-selectable) and text color `300` via `VIBE_Widget_SetTextColor @0x412530`. The
y-coordinates lay out the buttons:

| local | y | meaning |
|-------|----|---------|
| `v80` | 10  | New game (single) — *Choose City* |
| `v85` | 53  | Load game |
| `v82`/`v19` | 96  | Multiplayer / network |
| `v73` | 139 | Options (game) |
| `v83` | 268 | Credits scroll |
| `v84` | 311 | Quit |

The block at **y = 139…225** is conditional on `dword_63C7CC @0x63C7CC` (a build / "extra
features" flag):

* If `dword_63C7CC != 0`: three extra entries are added at y=139/182/225 (`v88`, `v70`,
  `v71`) — the **mission / city-loader / credits-window** debug entries.
* Otherwise: two entries at y=182/225 (`v86`, `v87`) are the **graphics / sound options**
  shortcuts.

All entries are then bound into one radio group:

```
v72 = VIBE_RadioGroup_Create(8, v75);   // 0x412728 — 8 buttons, anchored at first widget
```

### 1.4 Background music selection

If `dword_63C8F8 @0x63C8F8` is set (audio enabled / "music on"), the menu picks one of
three CD tracks at random:

```
v22 = (unsigned)VIBE_Util_RandNext() % 3;   // 0x5cb8bc
switch (v22) {
  case 0: v23 = "cd1\\Rittersleut.mp3";       break;  // aCd1Rittersleut @0x622d10
  case 1: v23 = "cd1\\MauerUndTor.mp3";        break;  // aCd1Mauerundtor  @0x622d24
  case 2: v23 = "cd2\\KraeuterUndPhiolen.mp3"; break;  // aCd2Kraeuterund  @0x622d38
}
dword_63C760 = (int)VIBE_Audio_LoadTrack(v23, 3);   // 0x439ed0 — load + start, loop mode 3
```

The returned track handle is cached in `dword_63C760 @0x63C760`; it is started looping and
stopped again at menu exit (§1.7). MP3 decoding is the **Miles → SDL** swap.

### 1.5 Re-fade and frame-time label

```
if (v2) VIBE_Fade_Unregister(v2);                          // drop the load-time fade
VIBE_Fade_Register(0,0, H,W, "BLACK", 90, 10);             // fade-in over 90 frames
byte_67225C = 0;
VIBE_Audio_SetGlobalVolume(2000);                          // 0x43a3dc
TextLabel = VIBE_Object_CreateTextLabel(8, H-20, byte_122F218);  // 0x41b494 — version/frame label
VIBE_Object_SetColor(TextLabel, 67);
```

A small text label (game version string at `byte_122F218`) is placed near the bottom of the
screen, colored palette index `67`.

### 1.6 The dispatch loop

The body is a `do … while ( VIBE_GameLogic_RunFrameLoop(147591, …) )` loop. Each iteration:

1. `VIBE_InitStateReader(v72)` — reads the radio group's current state into the GUI globals.
2. If `dword_672228 @0x672228` (a click-confirmed flag) is set, the **selected object id**
   `dword_62D22C @0x62D22C` is compared against every menu widget id and dispatched.

`byte_67225C` (set by the input layer on ESC / window-close) forces `dword_631614 = 1`
("leave this loop") and sets the quit path. `dword_631614 @0x631614` is the universal
"exit current frame-loop" request flag honored by `VIBE_GameLogic_RunFrameLoop`.

The dispatch arms (in source order), with the resulting `word_63C740` value:

| Click (widget) | Action | `word_63C740` after |
|----------------|--------|---------------------|
| `v80` (New game) | `VIBE_Menu_EnterChooseCity @0x52ee38` → new-game flow ([09](09-newgame-flow.md)). On success sets `dword_631614=1`. | OR'd with `1` (bit 0 = single-player new game) |
| `v81` (Mission) | `VIBE_Menu_BuildChooseMissionDialog @0x59b998`; on accept copies **`"Tutorial"`** (`aTutorial @0x622d54`) into `ReturnedString`, sets `byte_63CC1D=1`. | `137` (`0x89` = bit0 new-game + bit3 + bit7 mission) |
| `v88` (City loader A) | `VIBE_Menu_RunFileSelector(… "gamedata/cities", "$Z$[Load INI$]", ".INI", …)`; reads `Stadtname` from `A - ALLGEMEIN` of the chosen `.INI`, then `VIBE_Map_LoadCityFile(0, …)`. | (unchanged; city preview only) |
| `v79` (City loader B / net) | Same file selector; on success sets `dword_63C798=1`, `word_63C740 |= 8`, then `|= 1`. | OR'd with `8` then `1` |
| `v70` (City loader C, net `.NET`) | File selector → `VIBE_Map_LoadCityFile(1, …)` (the `1` selects `.NET`). | (unchanged) |
| `v85` (Load game) | `VIBE_Menu_RunLoadGame @0x56a270`; on success `dword_631614=1`. | `10` (set inside RunLoadGame) |
| `v82` (Network) | `VIBE_Menu_ChooseNetworkMode @0x529a64`; on cancel resets `word_63C740=0` and re-shows menu. | `5` host/join, `4` profile (set inside) |
| `v86` (Options GFX) | `VIBE_Menu_RunOptionsGfx @0x56c21c`. If `dword_63CC38` afterwards, `dword_631614=1` (mode change requires restart of menu render). | (unchanged) |
| `v87` (Options SFX) | `VIBE_Menu_RunOptionsSfx @0x56c808`. | (unchanged) |
| `v73` (Options Game) | `VIBE_Menu_RunOptionsGame @0x56cc44`. | (unchanged) |
| `v84` (Quit) | Sets `dword_63CC30=1`, `dword_63CC48=1`, `dword_631614=1` (request app exit). | (unchanged) |
| `v71` (Credits window) | `VIBE_Menu_RunCreditsWindow @0x529c30` (text popup). | (unchanged) |
| `v83` (Credits scroll) | If music on, lowers volume and loads **`cd2\\ZumGutenEnd.mp3`** (`aCd2Zumgutenend @0x622da4`); runs `VIBE_Menu_RunCreditsScroll @0x56e524`; on return restores volume and restarts `dword_63C760`. | (unchanged) |

Each non-trivial arm hides the menu objects with
`VIBE_Object_SetVisibleRecursive(v18, 0)` (`0x41dd98`) before entering the sub-screen and
re-shows them with `…(v18, 1)` (label `LABEL_55`) on return — so the title 3D scene stays
live underneath while a sub-form is open.

The **New-game handoff** (`v80` arm) is the important one for gameplay: it sets
`word_63C740 |= 1` and `dword_631614 = 1`, which breaks the menu loop and lets the session
bootstrap pick up the chosen city/history/player/character that the new-game flow stored.
That flow is documented in [09 — New-game flow](09-newgame-flow.md); here we only note the
handoff value `word_63C740 = 1`.

### 1.7 Teardown

When `dword_631614` finally ends the loop:

```
VIBE_Widget_DestroyByType(TextLabel, …);        // remove version label
VIBE_RadioGroup_FreeSurface_Thunk(v72);         // 0x41287c — free radio group
VIBE_Form_Destroy(v68);                          // 0x41da04 — destroy MENU\MAIN_MENU
VIBE_Window_RenderEntityScene(...);
VIBE_Universe_SwitchActiveSlot(0); VIBE_Universe_ResetCurrentSlot();   // free slot 0 scene
VIBE_Universe_SwitchActiveSlot(1); VIBE_Universe_ResetCurrentSlot();   // free slot 1 scene
VIBE_Audio_SetGlobalVolume(1000);
if (dword_63C760) {                              // if a music track was playing
    VIBE_Audio_StopTrack(dword_63C760, 1, …);    // 0x43a2fc — fade out
    dword_62D314 = 1;
    while ( v54 != *(BYTE*)(dword_63C760 + 260) )  // spin frames until track byte+260 (playing) clears
        VIBE_GameLogic_RunFrameLoop(dword_11BC2D0, …);
    Sleep(1000);                                  // 0x3E8 ms settle
}
return VIBE_Audio_SetGlobalVolume(5000);          // restore in-game volume, return
```

`word_63C740` is the live return contract: whatever bits the dispatch set survive into the
caller (the WinMain-level outer loop, see [01](01-entrypoint-and-winmain.md)), which uses
them to decide whether to start a new game, load a save, quit, etc.

---

## 2. The `word_63C740` game-mode bitfield

`word_63C740 @0x63C740` is a 16-bit mode code written by the menu and read by the session
bootstrap. Observed values/bits across the menu tree:

| Value / bit | Set by | Meaning |
|-------------|--------|---------|
| `0` | menu prologue; network-cancel | no mode / fresh title |
| bit `1` | New-game arm; net city-load tail | single-player new game requested |
| `4` | `VIBE_Menu_ChooseNetworkMode` (profile branch) | network: choose-profile mode |
| `5` | `VIBE_Menu_ChooseNetworkMode` (host/join branches) | network: host or join session |
| bit `8` | net city-loader (`v79`) | load a network-shared city `.INI` |
| `10` | `VIBE_Menu_RunLoadGame` | load a saved `.SAV` game |
| `137` (`0x89`) | mission arm | mission/tutorial game (bits 1+8+128) |
| bit `0x80` | (read in Options-Main) | in-game / network "running game" context |

The high bits (`0x4`, `0x10`, `0x80`) are also *read* by `VIBE_Menu_RunOptionsMain` to
decide which buttons to disable (e.g. "Save network game" vs "Save game"), confirming the
field doubles as the running session descriptor once gameplay starts.

---

## 3. Sub-screen routines

All sub-screens follow the same shape: load a `.ed3` form via `VIBE_GameTick_Finalize`,
populate widgets, then `while ( VIBE_GameLogic_RunFrameLoop(...) ) { … dispatch … }`, and
finally `VIBE_Form_Destroy`. `dword_672230 @0x672230` is the form's "back" flag,
`dword_672228 @0x672228` the "OK/click" flag, `dword_75BF38 @0x75BF38` the last-pressed
key, and `dword_62D22C @0x62D22C` the clicked object id.

| Routine @addr | Form resource | Returns / effect |
|---------------|---------------|------------------|
| `VIBE_Menu_EnterChooseCity @0x52ee38` | (delegates) | Clears `word_63C740=0`, `byte_63CC1D=1`, calls `VIBE_Menu_RunChooseCity @0x52e6d8` (new-game flow, [09](09-newgame-flow.md)). On cancel color-fills `dword_62D210` and redraws. |
| `VIBE_Menu_RunLoadGame @0x56a270` | `menu\loadgame_new` (`aMenuLoadgameNe @0x624f1c`) | Lists saves from **`gamedata/saves`** via `VIBE_SaveBrowser_LoadSlotMetadata`; on pick (with optional confirm `VIBE_Dialog_RunMessageBox`) builds `Gamedata\Saves\%s.SAV`, sets `word_63C740 = 10`, returns 1. |
| `VIBE_Menu_ChooseNetworkMode @0x529a64` | `Menu\CHOOSENETWORK` (`aMenuChoosenetw @0x622c68`) | Three radio entries (rich-text ids `0x18B3`/`0x18B4`/`0x18B5`): **Host** → `word_63C740=5`, `VIBE_Menu_RunHostNetworkSetup @0x528dac`; **Join/Search** → `word_63C740=5`, `VIBE_Menu_SearchNetworkGames @0x529248`; **Profile** → `word_63C740=4`, `VIBE_Menu_ChooseNetworkProfile @0x52991c`. |
| `VIBE_Menu_RunOptionsGfx @0x56c21c` | `menu\options_gfx` (`aMenuOptionsGfx @0x625234`) | 9 sliders (resolution, detail, shadows, gamma `100-x`, …) backed by `byte_1233514…` block. On OK persists via `VIBE_Config_WriteGfxSettings @0x56af54` and `VIBE_Render_ApplyGfxSettings @0x56be58`; a resolution change sets `dword_63CC30/38` to force re-init. |
| `VIBE_Menu_RunOptionsSfx @0x56c808` | `menu\options_sfx` (`aMenuOptionsSfx @0x625248`) | 4 volume sliders + 1 toggle (`byte_1233550…1233554`); live-previews master/music volume via `VIBE_Audio_SetMasterVolume`/`VIBE_Audio_ApplyMasterVolume` and plays a sample (`VIBE_Audio_StartVoiceSample`, voice id `0x3F`) while dragging. On OK `VIBE_Audio_ApplyVolumeSettings @0x56c148`. |
| `VIBE_Menu_RunOptionsGame @0x56cc44` | `menu\options_game` (`aMenuOptionsGam @0x625270`) | Gameplay sliders (scroll speed, autosave, difficulty…) into `dword_1233558…byte_12335BC`. A change to the "ground-plan detail" (`byte_12335B8`) rebuilds the groundplan window and fades to black (`VIBE_Transition_FadeOutToBlack`). On OK `VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc`. |
| `VIBE_Menu_RunOptionsMain @0x56dccc` | `menu\options` (`aMenuOptions @0x625314`) | The **in-game** pause/options menu (Save/Load/Quit-to-menu + the three options sub-forms). Captures a screen thumbnail, fades, builds a 7-button radio group; uses `word_63C740` bits 4/0x10/0x80 to enable/disable Save-network vs Save-game and quit. Reached from in-game, not the title menu's dispatch. |
| `VIBE_Menu_RunCreditsScroll @0x56e524` | (window 16, full height) | Scrolling credits: registers a `"BLACK"` fade, creates a 100×H window, renders rich text id `0x1BDF`, auto-scrolls (`+584` offset) with speed `1/2/4` chosen from `dword_631630`, ends on `byte_67225C`/scroll-complete, then fades out. |
| `VIBE_Menu_RunCreditsWindow @0x529c30` | window type 21 (32,96,400×620) | Static credits popup: renders credits block id `5576` via `VIBE_Text_RenderCreditsBlock`, loops until `byte_67225C`, removes window. |
| `VIBE_Menu_RunFileSelector @0x569668` | `menu\fileselector` (`aMenuFileselect @0x624e5c`) | Generic browser. Enumerates files in a dir (e.g. `gamedata/cities`) via `VIBE_SaveBrowser_EnumerateSaveFiles`, lists them as labels in a slider panel; on pick (or text entry when `a2&1==0`) formats `"%s\\%s%s"` (dir, name, ext) into the caller's buffer and returns 1. Used by the title menu's city-loader arms with title `"$Z$[Load INI$]"` and ext `.INI`. |
| `VIBE_Menu_BuildChooseMissionDialog @0x59b998` | `special\CHOOSE_MISSION` (`aSpecialChooseM_0 @0x627d0c`) | Mission picker. Renders header `$Z%s` (string id `0x1CB2`) and 5 checkbox rows (`%ia[%s]$N`, ids `7347…`) bound to `dword_649CD8[]`; OK button enables only when ≥1 checked; writes the 5 selections back into `dword_649CD8[]` and returns 1. |
| `VIBE_Map_LoadCityFile @0x528bd0` | (no form) | Loads a city for preview/network. Switches to slot 0, enters the city scene, formats `"%s/%s.CTY"` or (`a1!=0`) `"%s/%s.NET"` under `gamedata/cities`, writes it via `VIBE_Save_WriteGameFile(… "city" …)`, resets buildings/persons/objects, and reloads `scenes/*ChooseCity.ed3`. |

---

## 4. Music helpers (summary — see [30 — Audio](30-audio.md))

These three are the only audio entry points the menu touches directly; the streaming
internals (MSS32 → SDL) are in chapter 30.

* **`VIBE_Audio_LoadTrack @0x439ed0`** — guarded by `dword_62DA28` (audio system ready).
  Under a critical section it finds/reuses a track slot
  (`VIBE_Audio_FindActiveTrackSlot`/`…FindFreeVoiceSlot`), copies the filename string into
  the slot (+0), clears the "paused" byte at slot+262, and immediately calls
  `VIBE_Audio_StartTrack`. Returns the slot pointer (the handle cached in `dword_63C760`),
  or `0` on failure.

* **`VIBE_Audio_StartTrack @0x439f8c`** — opens the MP3 stream
  (`VIBE_Audio_OpenStream` on `dword_62DA28`/`dword_62DA2C`), builds the full path by
  concatenating `unk_7649B8` (the music base dir) + the track name, queries length
  (`VIBE_Audio_GetStreamMsLength`), sets volume/loop count (loop when arg `a2==0`), and
  either starts immediately or queues behind a fading track. Sets slot+260 ("playing")=1.

* **`VIBE_Audio_StopTrack @0x43a2fc`** — if the slot is playing (slot+260) and arg
  `a2!=0`, fades out (`VIBE_Audio_FadeOutTrack(a1,2)`); otherwise pauses, closes the stream
  (`VIBE_Audio_CloseStream`), and clears slot+260 and the stream pointer at slot+256. The
  menu teardown (§1.7) uses the fade path then spins frames until slot+260 reads back 0.

---

## 5. Asset/string reference

| String global | Value |
|---------------|-------|
| `aScenesChooseci @0x622b84` | `scenes/*ChooseCity.ed3` |
| `aScenesSpielera @0x622c7c` | `scenes/*spielerauswahl.ed3` |
| `aMainMenueCould @0x622c98` | `main_Menue:Could not load 3D-scene...` |
| `aMenuMainMenu @0x622cc0` | `MENU\MAIN_MENU` |
| `aCd1Rittersleut @0x622d10` | `cd1\Rittersleut.mp3` |
| `aCd1Mauerundtor @0x622d24` | `cd1\MauerUndTor.mp3` |
| `aCd2Kraeuterund @0x622d38` | `cd2\KraeuterUndPhiolen.mp3` |
| `aCd2Zumgutenend @0x622da4` | `cd2\ZumGutenEnd.mp3` |
| `aTutorial @0x622d54` | `Tutorial` |
| `aGamedataCities_0 @0x63ccb4` | `gamedata/cities` |
| `aGamedataSaves @0x624f30` | `gamedata/saves` |
| `aZLoadIni @0x622d68` | `$Z$[Load INI$]` |
| `aAAllgemein @0x622d94` | `A - ALLGEMEIN` (INI section) |
| `aStadtname @0x622d88` | `Stadtname` (INI key) |
| `aMenuLoadgameNe @0x624f1c` | `menu\loadgame_new` |
| `aMenuChoosenetw @0x622c68` | `Menu\CHOOSENETWORK` |
| `aMenuOptionsGfx @0x625234` | `menu\options_gfx` |
| `aMenuOptionsSfx @0x625248` | `menu\options_sfx` |
| `aMenuOptionsGam @0x625270` | `menu\options_game` |
| `aMenuOptions @0x625314` | `menu\options` |
| `aMenuFileselect @0x624e5c` | `menu\fileselector` |
| `aSpecialChooseM_0 @0x627d0c` | `special\CHOOSE_MISSION` |

Key globals: `word_63C740 @0x63C740` (game-mode code), `dword_63C760 @0x63C760` (menu
music track handle), `dword_63C8F8 @0x63C8F8` (music-enabled), `dword_63C7CC @0x63C7CC`
(extra-entries build flag), `dword_62D22C @0x62D22C` (clicked object id),
`dword_631614 @0x631614` (exit-frame-loop request), `byte_67225C @0x67225C` (ESC/back
edge), `dword_672228 @0x672228` (click-confirmed), `dword_672230 @0x672230` (back
pressed), `dword_63CC30/63CC48 @0x63CC30/0x63CC48` (quit-application flags).
