# 12 — Session init & world load

This chapter documents how `gilde.exe` turns a menu choice (new game / load / join
network / tutorial) into a *live, running* game session: it connects to the local game
server, opens the `.cty` city file, runs `standard.scr`, paints a loading-progress
screen, optionally restores an `Autosave.SAV`, builds the 3D world (terrain, sky, sounds,
lighting, camera), synchronizes every building/object/character through the command
queue, and finally enters the in-session frame loop. When the session ends it tears the
world down and returns control up the call stack to WinMain.

The single root is:

```
VIBE_GameLogic_InitOrLoadSession   @0x533a54   (__usercall, eax=mode, edi=ctx, sil=flag)
```

It is called from `VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` (call site `0x53551c`),
which is the function reached from WinMain's main-menu dispatch loop — see
[01 — Entry point & WinMain](01-entrypoint-and-winmain.md). The same root re-enters
itself recursively at `0x53419e` for the "start a fresh round in the next city" path and
is also reached from mission/options dialogs.

Platform-boundary swaps that occur inside this tree (per project rules 3–5):

- **3D scene/terrain/sky** load and the view transform feed the original DirectDraw/
  Direct3D fixed-function pipeline → **Vulkan** in the reimplementation.
- **Sound sample banks** (`*.sbf`) loaded by `VIBE_Game_InitWorldAndSounds` and the
  global volume call go through Miles Sound System → **SDL audio**.
- **`VIBE_Net_ConnectToServer @0x43b51c`** is raw **wsock32** (`WSAStartup`, `socket`,
  `connect`, `select`, `setsockopt`, `ioctlsocket`) — reconstructed **1:1** over an
  `INetSocket` shim (not replaced with a different protocol). See
  [19 — Commands & netcode](19-commands-netcode.md).

---

## The session-mode flags: `word_63C740`

Almost every branch in the root tests bits of the 16-bit global **`word_63C740`**
(set by the menu code before this function is entered). The observed bit meanings:

| Bit (mask) | Meaning |
|-----------:|---------|
| `0x01` | **Re-enter** an already-running world (next round / next city), not a cold start |
| `0x02` | **Load a save** (`VIBE_Save_LoadGameFile` of a `.SAV`) |
| `0x04` | **Network game** (multiplayer) |
| `0x08` | suppress certain start-of-round AI/time advance |
| `0x10` | **Host** the network game (spawn the local server DLL via `Init_`) |
| `0x40` | network game restored from a **saved** network game |
| `0x80` | **Tutorial** mode |

`dword_764CE0 @0x764ce0` holds the connected **socket** (`-1` = no server / single-player
fallback); it gates every network-conditional branch below.

---

## Phase table

| # | Phase | Key calls | Notes |
|--:|-------|-----------|-------|
| 0 | Office/board reset (cold start) | `VIBE_Office_InitTable @0x47de78` | only when `word_63C740 & 1` is 0 |
| 1 | Network connect / host | `VIBE_Net_ConnectToServer @0x43b51c`, `LoadLibraryA`+`Init_`, `VIBE_Net_OpenBroadcastSocket @0x43aac0` | wsock32 → INetSocket shim |
| 2 | Command queue init + server handshake | `VIBE_Command_QueueInitAndSync @0x4931e0` | waits for packet type 3, magic `0xB332C29D` |
| 3 | Loading screen | `VIBE_Loading_ShowProgressScreen @0x52ee84`, `VIBE_Loading_UpdateProgressBar @0x52effc` | `Misc\Loading_game` / `Misc\Loading_net` |
| 4 | World + sounds init | `VIBE_Game_InitWorldAndSounds @0x52f2ec` | sound banks, entity tables, clock proc |
| 5 | Source dispatch | `.cty` load / `.SAV` load / network start / network-saved load | per `word_63C740` |
| 6 | Scene sync after enter | `VIBE_Scene_SyncWorldOnEnter @0x50456c` … `VIBE_Character_EnsureGateAvatars @0x505870` | push world state through command queue |
| 7 | Chronicle + map bitmap | `VIBE_History_LoadChronicleText @0x4fced0`, `VIBE_MapView_LoadBackgroundBmp @0x5438e8` | |
| 8 | Fade out loading, build groundplan | `VIBE_Loading_FadeOutAndClose @0x52f0bc`, `VIBE_Groundplan_CreateWindow @0x4ae3b8` | |
| 9 | Lighting / view / camera | `VIBE_Light_EnableDaylight @0x504a00`, `VIBE_Render_SetupViewTransform @0x5af5f8`, `VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc` | → Vulkan view matrix |
| 10 | Sky / weather scene (saved game) | `VIBE_Sky_InitScene @0x4b1e94` | |
| 11 | Tutorial bootstrap (`0x80`) | `VIBE_Tutorial_BuildChapterChain @0x597bd8` | |
| 12 | **Game frame loop** | `VIBE_GameLogic_RunFrameLoop @0x4c09a0` + per-round logic | see [14](14-per-frame-loop.md) |
| 13 | Shutdown ladder | `VIBE_Game_ShutdownWorldAndSubsystems @0x52f44c` (or the full cold-fail ladder) | returns to WinMain |

---

## Ordered walkthrough

### Phase 0 — Office/board table (cold start only)

```c
v108 = 131079;
if ((word_63C740 & 1) != 0 && !a1)   // re-enter AND mode==0
    VIBE_Office_InitTable();
hLibModule = 0;
```

`VIBE_Office_InitTable @0x47de78` zeroes an 888-byte block at `byte_B59848`, then fills a
fixed table of 37 office/guild-rank slots (`dword_B59834`/`dword_B59844` set to `-1`,
`byte_B598xx` constants 1,2,3,4,…34). It is the city-council ("Ämter") seating layout that
the round will populate. Returns the literal `7709`.

### Phase 1 — Network connect (and optional host)

If this is a **network** session (`word_63C740 & 4`):

- `VIBE_History_SetActiveFlag(0)` disables chronicle writing.
- If **host** (`& 0x10`): `LoadLibraryA(byte_122F284)` loads the server DLL, resolves the
  exported `Init_` (`aInit @0x623518`) entry, calls it; on any failure it shows a message
  box, sets `dword_63CC30 = 1` (the quit flag) and returns. On success it `Sleep(2500)`,
  then `VIBE_Net_OpenBroadcastSocket(0x3039, …)` (UDP port **12345** for LAN discovery)
  and sets `dword_63CC6C = 1`. The connect target host (`byte_122EE90`) and port
  (`dword_122F490`) come from the network-setup screen.

Then unconditionally:

```c
VIBE_Net_ConnectToServer(host, port, ...);   // @0x43b51c
```

**`VIBE_Net_ConnectToServer @0x43b51c`** is the literal wsock32 client (reconstructed 1:1
over `INetSocket`):

1. `VIBE_Light_SetGrayColorThunk(0,1536,&dword_764CF8)` clears the 1536-byte network state
   block; resets sequence/ack counters (`dword_764CE4/E8`, `word_764CEC/EE`), sets
   `dword_764CE0 = -1`.
2. If `host == NULL` → returns `-1` (single-player; no socket).
3. `WSAStartup(2,…)`, `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)`.
4. `setsockopt` SO_REUSEADDR, TCP **NODELAY** (`-129` ≈ `TCP_NODELAY` under the WS option
   layout used), SO_SNDBUF = `0x40000`, SO_RCVBUF = `0x20000`; `ioctlsocket(FIONBIO,1)`
   (non-blocking).
5. `inet_addr` then `gethostbyname` fallback; `connect`; `select(writefds, timeout =
   0.5s)`. Failure anywhere → shutdown/closesocket/WSACleanup → return `-1`.
6. On success stores the socket in **`dword_764CE0`** and returns it.

### Phase 2 — Command queue init + server handshake

```c
VIBE_Command_QueueInitAndSync();   // @0x4931e0
```

`VIBE_Command_QueueInitAndSync @0x4931e0` builds the **command ring** of 8192 entries
(153-byte stride) at `byte_1078360` with prev/next links, resets the per-slot ack table
(`dword_B5FB58`/`byte_B5FB56`, 32768 entries × 10), enqueues an init packet
(type `3`, magic `0xB332C29D` = `-1288263715`), and waits via
`VIBE_Command_WaitForPacketType(3)`. If the echoed magic matches it unlinks the packet and
stores the assigned **player/session id** in `dword_11AA488`/`dword_62EB38`; otherwise it
`VIBE_Net_Disconnect()`s. This is the synchronous join handshake with the (possibly
local) authoritative server. See [19 — Commands & netcode](19-commands-netcode.md).

A small loop then sets 8 ints in a scratch block to `-1`, sets `dword_62D314 = 1`
(loading-active flag).

### Phase 3 — Loading progress screen

```c
VIBE_Loading_ShowProgressScreen();          // @0x52ee84
VIBE_Form_SelectWindow(dword_122F634, 1);
VIBE_Loading_UpdateProgressBar(-1);          // @0x52effc
VIBE_Window_PumpMessages();
VIBE_Loading_UpdateProgressBar(-1);
```

`VIBE_Loading_ShowProgressScreen @0x52ee84`:

- Renders the entity scene/list backdrop, sets a screen-fill view via
  `VIBE_Render_SetupViewTransform`.
- Picks the loading art by mode: **`Misc\Loading_net`** (`aMiscLoadingNet @0x623280`) when
  `word_63C740 & 4`, else **`Misc\Loading_game`** (`aMiscLoadingGam @0x623294`).
- Builds the loading form (`dword_122F634`), lays out and centers it, adds the progress
  **slider** (`VIBE_Widget_AddSliderToWindow(…582,582…)` — 0..582 range), renders, and
  registers a timer proc (`locret_52EE68`, slot 7) that keeps the screen animated.

`VIBE_Loading_UpdateProgressBar @0x52effc` re-selects the form, fetches child object id 1
and sets its fill width to `582 * pct / 100`, then renders. Called repeatedly with `-1`
through the load (the original passes a register-arg percentage; `-1` here drives the
indeterminate tick).

### Phase 4 — World & sounds init

```c
VIBE_Game_InitWorldAndSounds();   // @0x52f2ec
```

`VIBE_Game_InitWorldAndSounds @0x52f2ec`:

- `VIBE_He_InitHandlerTable @0x4c5248` — event-handler dispatch table.
- Sets session globals: `dword_631DB4=1`, `dword_63CC2C=1` (round counter),
  `LODWORD(qword_13CE852)=2` (game clock seed), clears the **quit flag**
  `dword_63CC30=0` and `dword_63CC34=0`.
- `VIBE_TimeBase_RegisterProc(VIBE_Clock_ComputeGameTimeOfDay @0x527778, 71)` + interval
  — installs the game-clock timer callback (per-frame day/time advance).
- Resets entity/event tables (`VIBE_He_ResetEntityTables`, `VIBE_MeisterAi_ExpireEventSlots`),
  city parameter table (`VIBE_City_InitParameterTable @0x577a9c`), loads the base mesh
  (`VIBE_Mesh_LoadOrAddRefByName` → `dword_63CD3C`).
- **Sound banks** (only if `dword_63C900`, i.e. audio enabled), via
  `VIBE_Sound_LoadSampleBank @0x446b2c` (→ SDL audio):
  `natur\regen.sbf`, `natur\wind.sbf`, `natur\blitzunddonner.sbf`,
  `objekte\buecher.sbf`, `objekte\TuerenUndRaeume.sbf`, `objekte\Transporter.sbf`,
  `Lebewesen\schritte.sbf`.
- `VIBE_Audio_SetGlobalVolume(3000)`, then init the event panel, status text,
  inventory grid surface, animal pool, char-action target, hotkey table, object spawn
  tables, and the cutscene command table.

After return, if re-entering an existing world with a pending heir
(`word_63C740 & 1 && dword_63C78C`) it queues an inheritance transfer
(`VIBE_Command_EnqueueInheritanceTransfer @0x5336f0`).

### Phase 5 — Source dispatch (where the world data comes from)

The big branch on `word_63C740 & 1` (cold start vs. re-enter) selects one of four sources:

**5a. Cold single-player from a city file (`& 1 == 0`, `& 4 == 0`):**

```c
VIBE_Crt_Sprintf_0(v88, "%s/%s.cty", aGamedataCities_0, ReturnedString);  // gamedata/cities/<NAME>.cty
if (VIBE_Save_LoadGameFile(v88, …, 1)) { … sync … }
else { /* full cold-fail shutdown ladder, see below */ }
```

The chosen city name lives in `ReturnedString @0x122ee50`; the base directory string is
**`gamedata/cities`** (`aGamedataCities_0 @0x63ccb4`), format **`"%s/%s.cty"`**
(`aSSCty_0 @0x623520`). The `.cty` is opened through the **save loader**
`VIBE_Save_LoadGameFile @0x5a7604` (a `.cty` is a save-format world snapshot; see doc 13).

> **Note on `VIBE_Map_LoadCityFile @0x528bd0`.** This is the *menu-side / choose-city*
> loader — it formats **`"%s/%s.CTY"`** or **`"%s/%s.NET"`** under `gamedata/cities`, writes
> a fresh "city" base via `VIBE_Save_WriteGameFile(…, "city", 0, 2)`, resets buildings/
> persons/objects, and loads the 3D selection scene **`scenes/*ChooseCity.ed3`**
> (`aScenesChooseci @0x622b84`). It runs during city selection ([20 — Buildings & city](20-buildings-city.md)),
> *before* this session root; the root itself reloads the resulting `.cty` through the save
> loader above.

If the `.cty` load **fails**, the root runs the full cold-fail teardown (a subset of the
shutdown ladder — see Phase 13) and returns to WinMain.

On success it runs the **scene-sync** block (Phase 6), then jumps to `LABEL_17`.

**5b. Save game (`& 2`):** `dword_63C744 = byte_12335BA`; then
`VIBE_Save_LoadGameFile(byte_122F530, …, 1)` where `byte_122F530` holds the chosen save
file path. Failure → message box (`dword_8C99F4`), set quit flag, shutdown, fade, return.
Success → `VIBE_DayCycle_BuildTimeTable`, set active-character index, fall through to
`LABEL_17`.

**5c. Network — re-enter path (`& 1 && & 4`):** if a socket exists
(`dword_764CE0 != -1`): host with a saved net game (`& 0x40`) →
`VIBE_Net_LoadSavedNetworkGame @0x50442c`; otherwise fresh net game →
`VIBE_Net_StartNetworkGame @0x503f78`; set `dword_63C744 = 2`, go `LABEL_17`. No socket →
message box `dword_8C9994`, shutdown, fade, return.

**5d. Network — cold path (`& 4 && & 1 == 0`):** `& 0x40` →
`VIBE_Net_LoadSavedNetworkGame(byte_122F878)`; else single check, then continue.

`VIBE_Net_StartNetworkGame @0x503f78` is the heaviest of these: it broadcasts the host's
save (`%s/AUGSBURG.NET` default, `VIBE_Net_SendSaveGameToClients`), receives the shared
stream (`VIBE_Net_LoadReceivedSaveStream`), reads the player's family name from
`\game.INI` (`[Network] Name=` / `Familienname=`, defaults *Der Wichtelmann* /
*Wichtelsippe*), loads the 3D scene **`scenes/*network.ed3`** (`aScenesNetworkE @0x620e98`),
enqueues a trade/spawn request (`VIBE_Command_EnqueueTradeRequest`), assigns the player a
character slot, optionally picks a mission, grants starting cash
(`VIBE_Command_EnqueueCmd15`, 10000/2000 × rate), then runs the same Scene-Sync calls as
Phase 6 (SyncWorldOnEnter / SyncMeisterBuildings / SyncObjectHeights) gated on the host
flag.

`VIBE_Net_LoadSavedNetworkGame @0x50442c` loads **`gamedata/network/%s.SAV`**
(`aGamedataNetwor_4 @0x620eac`) and, for the host, ships **`gamedata/network/%s.SRV`**
(`aGamedataNetwor_5`) to clients, polling `VIBE_Window_PumpMessages` /
`VIBE_Amt_RefreshGuildState` until the ack bits in `byte_63CC28` are set, then
`VIBE_Save_LoadGameFile`, sync, and `VIBE_Scene_SyncMeisterBuildings`.

### Phase 6 — Scene sync after enter (`LABEL_17`)

For the **cold single-player city** path the root first runs, in order:

```c
VIBE_Command_EnqueueInheritanceTransfer(...);
VIBE_Scene_SyncWorldOnEnter(byte_6477A1, 1);     // @0x50456c
VIBE_Scene_SyncMeisterBuildings(...);            // @0x504ce0
VIBE_Scene_SyncObjectHeights(byte_6477A1);       // @0x504e14
VIBE_Loading_UpdateProgressBar(-1); ×2
if (dword_63C7AC) VIBE_Scene_SyncMovableObjects();  // @0x504ef8
```

`byte_6477A1 @0x6477a1` is the **local player slot id** used as the `__usercall`
argument across these sync calls.

- **`VIBE_Scene_SyncWorldOnEnter @0x50456c`** — seeds the RNG from `timeGetTime` (only when
  single-player, `dword_764CE0 == -1`), `VIBE_Command_SyncSceneObjectStates`, optionally
  sets up the player's starting home (`VIBE_GameLogic_SetupHomeSweetHome`), assigns any
  unassigned character slots via `VIBE_Command_SyncCharSlotAssignments`, syncs city
  buildings, and grants the player's first good (`VIBE_Command_QueueRequest16(… 480000 …)`).
  Iterates the 768-entry character array (536-byte stride, type byte at
  `byte_12CE912[536*i]`; `6`=local master, `7`=remote master).
- **`VIBE_Scene_SyncMeisterBuildings @0x504ce0`** — recomputes production for up to 4 guild
  branches (`VIBE_Building_RecalcAllProduction`, `VIBE_Building_ComputeSlotStats`), scans
  the building array (268-stride) for master shops / branch shops (type `11`/`12`),
  registers building names, and syncs master-shop objects.
- **`VIBE_Scene_SyncObjectHeights @0x504e14`** — for the player's 62 ground-object slots
  (128-stride from `dword_13C3B5C`) it jitters their Z height by a random factor and pushes
  the corrected position through `VIBE_Command_QueueRequest17`.
- **`VIBE_Scene_SyncMovableObjects @0x504ef8`** — pushes carts/animals/transporters
  (categories 1/2/4 and object types 4,5,9,16,19) into the command stream via delta
  packets (`BeginDeltaPacket`/`AppendDeltaField`/`QueueRequestState22`/`…Quad56`/
  `…GuardTarget61`/`QueueRequest17`). Only when `dword_63C7AC` is set.
- **`VIBE_Scene_RefreshBuildingEffects @0x504910`** (called later, Phase 9) — walks all
  master buildings, re-enables chimney smoke (`VIBE_Object_SpawnChimneySmoke`), flags
  building gates, applies torch lighting, rebuilds the scene-graph octree
  (`VIBE_SceneGraph_BuildOctreeForRegion`), refreshes object lighting, and rebuilds the
  terrain mesh (`VIBE_Heightmap_BuildTerrainMesh` → Vulkan).
- **`VIBE_Character_EnsureGateAvatars @0x505870`** — for each gate/guard person
  (`VIBE_Person_QueryBegin(…,5,11)`) ensures its building avatar exists
  (`VIBE_Character_EnsureBuildingAvatar`).

For a **re-entered network world** (`& 1 && & 4 == 0`) the root sets the game clock to day
6, 00:00 via `VIBE_GameTime_Set @0x5831f0` and pushes it through the command queue
(`VIBE_Command_QueueRequestFlagBlob32(3,…)`, busy-waiting on
`VIBE_Command_GetPacketStatusById`), then seeds every master with starting cash scaled by
difficulty (`v32 = 1250 - 250*dword_63C744`, or `75000` in the "rich" mode `dword_63C7B4`).

`VIBE_GameTime_Set @0x5831f0` is a tiny leaf: it writes hour (`+4`), minute (`+10`) and
second (`+6`) fields into the 14-byte game-time record.

### Phase 7 — Chronicle & map bitmap

```c
if (word_63C740 & 1) VIBE_History_LoadChronicleText(...);  // @0x4fced0
VIBE_Window_PumpMessages();
VIBE_MapView_LoadBackgroundBmp();                          // @0x5438e8
VIBE_Window_PumpMessages();
```

Loads the running chronicle text (re-enter only) and the world-map background bitmap.

### Phase 8 — Fade out loading, build groundplan

```c
VIBE_Loading_FadeOutAndClose(a2);                 // @0x52f0bc
dword_62D314 = 0;                                  // loading no longer active
WappenLabelId = VIBE_Groundplan_GetWappenLabelId();
VIBE_Groundplan_CreateWindow(WappenLabelId, 0);   // @0x4ae3b8
VIBE_Universe_SwitchActiveSlot(0, ...);
```

`VIBE_Loading_FadeOutAndClose @0x52f0bc` unregisters the loading timer proc, registers a
30-frame fade-to-**`BLACK`** (`VIBE_Fade_Register`) and pumps `VIBE_GameLogic_RunFrameLoop`
until the fade completes, destroys the loading form, color-fills the back buffer, clears
`dword_62D314`. The frame-loop event code it passes is `147591` (net) or `147590`
(single-player, `dword_764CE0 == -1`).

`VIBE_Groundplan_CreateWindow @0x4ae3b8` opens the city-groundplan / coat-of-arms HUD.

### Phase 9 — Lighting, view transform, camera config

```c
if (word_63C740 & 1 && (word_63C740 & 0x40) == 0) {
    VIBE_Scene_RefreshBuildingEffects(0, j);   // @0x504910
    VIBE_Light_EnableDaylight();               // @0x504a00
    VIBE_Hotkey_AssignDefaults(j);
}
VIBE_Config_ApplyCameraAndScrollSettings();    // @0x56c0cc
VIBE_Render_SetupViewTransform(...);           // @0x5af5f8
```

- **`VIBE_Light_EnableDaylight @0x504a00`** frees and rebuilds the scene-graph octree for
  region 0 with light level 7 (`VIBE_SceneGraph_BuildOctreeForRegion(0,64,7,8)`) after
  flagging every character for redraw — i.e. it relights the whole scene for daytime.
- **`VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc`** computes the camera zoom factor
  (`dword_62D07C`, different formula for network vs. single-player using config doubles at
  `dbl_625214…`), sets the mouse-wheel base (`VIBE_Input_SetWheelBase`), and the edge-scroll
  margin `dword_6316C8 = 100 - <config>`.
- **`VIBE_Render_SetupViewTransform @0x5af5f8`** (`__stdcall`, the projection/raster setup
  → **Vulkan**) takes the world bounds (`dword_63CC4C..58`) and config angles
  (`flt_13FC76C` pitch, `flt_13FCAFC` scale, `flt_13FCD0C` yaw) plus screen size from
  `dword_69FFBC`/`dword_69FFB8 (>>16)`. It early-outs if nothing changed; otherwise it
  rebuilds the isometric projection (`VIBE_Render_SetProjectionTransform`), recomputes the
  view-space basis vectors (`VIBE_Coord_ConvertX`), stores the view matrix
  (`VIBE_Render_BuildViewMatrix`) and invalidates the current object cache. This is the
  single point where the engine's reconstructed 3D projection math meets the GPU backend.

### Phase 10 — Sky / weather (saved game)

For a loaded save (`& 2`):

```c
VIBE_Sky_InitScene(&qword_13CE852, 0, 0);   // @0x4b1e94
```

`VIBE_Sky_InitScene @0x4b1e94` reads the current game-time/season, looks up per-season
cloud-cover and precipitation parameters (`dword_13CD7A8`/`dword_13CD7B8`), randomly
generates a 24-slot weather curve, sets `dword_11BC1C0` (0=rain, 1=clear, 2=snow), creates
rain/snow particle systems (`VIBE_Rain_Create`/`VIBE_Snow_Create`), and builds the sky
dome with three texture layers — **`Sky_Schoen_01`**, **`Sky_Schoen_02`**,
**`sky_dunkel_01`** (`VIBE_Sky_CreateLayer`) — then refreshes dome colors across all
populated universe slots and updates day-cycle brightness. If a building was active when
the save was taken (`dword_13CECEC != -1`) it re-opens that building/object scene
(`VIBE_Scene_LoadGebaeudeScene`/`VIBE_Scene_LoadObjektScene` +
`VIBE_Dialog_OpenBuildingForActiveChar`).

`VIBE_DayCycle_BuildTimeTable @0x4b2438` (called on the save path) fills the per-season
sunrise/sunset table `dword_631AAC`/`byte_631AB4` from the minutes-of-day table
`dword_4AD160`.

Then a small loop advances `word_63CC5C` (current-character cursor) to the first local
master (`byte_12CE912[536*i] == 6`).

### Phase 11 — Tutorial bootstrap

```c
if (word_63C740 & 0x80) {
    VIBE_Tutorial_BuildChapterChain();          // @0x597bd8
    VIBE_Light_SetGrayColorThunk(0, 248, &v89);
    ... VIBE_Command_QueueRequestSlotReset28(&v89, ...);
}
```

Builds the linked list of tutorial chapters and queues the tutorial's initial slot-reset
command.

### Phase 12 — The game frame loop

```c
while (!dword_63CC30
       && VIBE_GameLogic_RunFrameLoop(v108, ...)   // @0x4c09a0
       && dword_63CC2C < 800)
{
    ... per-round processing ...
}
```

This is the in-session **round/turn driver**. Each outer iteration:

- counts active masters into `dword_63CC20`;
- begins a round (`VIBE_GameTick_BeginRound` for SP, or counts net masters);
- advances `word_63CC5C` across the local masters;
- for the player's own master turn: sets the clock to day 6 06:00 and syncs it; runs the
  turn's actions (`VIBE_GameLogic_ProcessTurnActions @0x52f8d0` for SP) or the network
  variants (`VIBE_Scene_ActivateAndRefreshCharacters`,
  `VIBE_Groundplan_FadeInScene`, AI command requests `VIBE_MeisterAi_RequestCmd58/59/115`,
  and the sync-range ack barrier `VIBE_Command_CheckSyncRangeAcked`);
- writes the **autosave**: single-player formats `"%s/standard.scr"`
  (`aSStandardScr @0x62352c`, base `gamedata/cities`) for the thumbnail then
  `VIBE_Save_WriteGameFile(aGamedataSavesA, …)` to
  **`Gamedata\Saves\Autosave.SAV`** (`aGamedataSavesA @0x623548`); the network host
  instead pushes an `AUTOSAVE` flag blob to clients;
- runs the per-frame scene loop (`VIBE_Scene_RunMainFrameLoop @0x50f0c0` — see
  [14 — Per-frame loop](14-per-frame-loop.md)), advances the game clock to ≥23:00 in
  30-minute command-driven steps, then `VIBE_GameLogic_RunTurnTransition @0x5310a4`.

The loop ends when the quit flag `dword_63CC30` is set, when `RunFrameLoop` returns false,
or when `dword_63CC2C` reaches 800 rounds. `RunFrameLoop @0x4c09a0` itself is the SDL
message-pump + render-tick that translates Win32 input/timer events (→ **SDL**) and drives
the Vulkan present; quitting the game (window close, "Beenden") sets `dword_63CC30`.

`v108` (= `131079` = `0x20007`) is the frame-loop's mode/event word carried throughout.

### Phase 13 — Shutdown and return to WinMain

When the loop exits (or any earlier hard-fail branch fires):

```c
if (word_63C740 & 0x80) VIBE_Tutorial_Shutdown(...);   // tutorial only
VIBE_Game_ShutdownWorldAndSubsystems(v35, v40);        // @0x52f44c
return;   // back to VIBE_GameLogic_MainEntryAndShutdown @0x534bbc -> WinMain
```

`VIBE_Game_ShutdownWorldAndSubsystems @0x52f44c` tears down the world, scene graph, sounds
and timers built in Phases 4–10, then control returns to the caller and ultimately to
WinMain's menu loop. The **cold-fail path** (city `.cty` load failed in Phase 5a) runs a
deeper ladder that additionally frees the GUI, gfx config, render engine, DirectInput,
timer, VFS, memory pools and (for a hosted server DLL) calls the DLL's shutdown export and
`FreeLibrary`, finishing with `VIBE_Window_DestroyAndUnregisterClass @0x527868`. Both paths
converge on the **shared shutdown ladder** documented in
[07 — Shutdown sequence](07-shutdown-sequence.md). The quit flag `dword_63CC30` is the
signal that propagates a user "Quit" all the way out to WinMain.

---

## Key globals

| Global | Addr | Meaning |
|--------|------|---------|
| `word_63C740` | `0x63c740` | session-mode bitflags (see table) |
| `dword_764CE0` | `0x764ce0` | connected TCP socket (`-1` = single-player) |
| `dword_63CC30` | `0x63cc30` | **quit flag** (set → leave frame loop / propagate to WinMain) |
| `dword_63CC34` | `0x63cc34` | secondary terminate flag |
| `dword_63CC2C` | `0x63cc2c` | round counter (loop caps at 800) |
| `word_63CC5C` | `0x63cc5c` | current-character cursor |
| `dword_63C744` | `0x63c744` | difficulty / start-cash modifier |
| `byte_6477A1` | `0x6477a1` | local player slot id (passed to sync calls) |
| `qword_13CE852` | `0x13ce852` | packed game-time/clock record |
| `dword_122F634` | `0x122f634` | loading-progress form id |
| `dword_62D314` | `0x62d314` | "loading active" flag |
| `byte_12CE912` | `0x12ce912` | character array, type byte (536-byte stride; 6=local master, 7=remote) |
| `ReturnedString` | `0x122ee50` | chosen city base name |

## Key file paths

| Template | Addr | Use |
|----------|------|-----|
| `gamedata/cities` | `0x63ccb4` | base dir for city files |
| `"%s/%s.cty"` | `0x623520` | city world snapshot (root reload) |
| `"%s/%s.CTY"` / `"%s/%s.NET"` | `0x622b70` / `0x622b64` | choose-city loader output |
| `"%s/standard.scr"` | `0x62352c` | autosave thumbnail source |
| `Gamedata\Saves\Autosave.SAV` | `0x623548` | per-round autosave |
| `gamedata/network/%s.SAV` / `.SRV` | `0x620eac` / `0x620ec4` | network saved-game transfer |
| `%s/AUGSBURG.NET` | `0x620e3c` | default network start city |
| `scenes/*ChooseCity.ed3` | `0x622b84` | city-selection 3D scene |
| `scenes/*network.ed3` | `0x620e98` | network-game 3D scene |
| `Misc\Loading_game` / `Misc\Loading_net` | `0x623294` / `0x623280` | loading-screen art |
| `natur\*.sbf`, `objekte\*.sbf`, `Lebewesen\schritte.sbf` | — | sound sample banks (→ SDL audio) |

---

## Cross-references

- [01 — Entry point & WinMain](01-entrypoint-and-winmain.md) — where this root is reached from.
- [07 — Shutdown sequence](07-shutdown-sequence.md) — the shared teardown ladder this returns through.
- [13 — Save/load](13-save-load.md) — deep `.cty`/`.SAV` format read by `VIBE_Save_LoadGameFile`.
- [14 — Per-frame loop](14-per-frame-loop.md) — `VIBE_Scene_RunMainFrameLoop` / `VIBE_GameLogic_RunFrameLoop`.
- [19 — Commands & netcode](19-commands-netcode.md) — `VIBE_Net_ConnectToServer`, the command queue, sync barriers.
- [20 — Buildings & city](20-buildings-city.md) — `VIBE_Map_LoadCityFile` and building-sync details.
