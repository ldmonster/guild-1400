# 15 — Game time, calendar & tick

*Die Gilde* runs on **two independent clocks** that must never be confused:

1. A **real-time multimedia timer** (`timeSetEvent`) that fires at a fixed millisecond
   period and only increments a free-running counter (`dword_62EB38`). This is the
   *wall-clock tick* that paces animation, sound, and the frame loop. It does **not**
   advance the calendar.
2. A **simulated game clock** — a 14-byte `GameTime` record (`qword_13CE852`) holding the
   in-world year/day/hour/minute/second. It is advanced **per frame** by a tiny scaled
   delta (so the city's day-of-time-of-day visibly flows), and it is the *day boundary*
   crossing of this clock that arms the heavyweight **round/turn** machinery.

Above both sits the **round** (German *Runde*) — a discrete simulation step. One round =
"everyone takes a turn, then the world's economy/buildings/AI/events all advance one
notch, then we jump to the next morning." The round counter is `dword_63CC2C`; the
player-turn-within-round index is `word_63CC5C`.

> **Per-FRAME vs per-ROUND.** Per-frame: the time-of-day creeps forward, lighting
> rebrightens, characters animate, the netcode pumps. Per-ROUND: economy, taxes, wages,
> production, AI master decisions, events, and the family-finance summary — all run once,
> in a fixed order, when the day ends.

Cross-links: [05 — Window & timers](05-window-platform-input.md),
[13 — Save/load](13-save-load.md), [14 — Per-frame loop](14-per-frame-loop.md),
[19 — Commands & netcode](19-commands-netcode.md),
[21 — Economy & office stats](21-economy-office-stats.md),
[25 — Lighting & FX](25-lighting-fx.md).

---

## 1. The `GameTime` record

The live clock is the 14-byte global `qword_13CE852` (the symbol is a `qword` only because
IDA tiled the first 8 bytes; the record is 14 bytes wide and is treated as a flat struct).
`VIBE_GameTime_InitDefault @0x58320c` zero-fills exactly 14 bytes (`qmemcpy 0xC` + `2`,
returns 14) from an all-zero template at `0x13CE852`.

The exact field layout is recovered from the (de)serializers
`VIBE_GameTime_PackToRecord @0x583304` and `VIBE_GameTime_UnpackFromRecord @0x58334c`, and
from the arithmetic in `VIBE_GameTime_Advance @0x583150` / `VIBE_GameTime_Set @0x5831f0`:

```c
// gilde.exe 0x13CE852 — GameTime (live in-world clock), 14 bytes
struct GameTime {           //  offset  type     range / meaning
    i32  day;     // +0x00  i32   day-of-game (0-based). Calendar year = 1400 + (day's year).
                  //               season = day % 4 (Spring/Summer/Autumn/Winter); see §6.
    u16  hour;    // +0x04  u16   0..23 (written/read as a 16-bit field; only low byte stored on disk)
    // +0x06
    i32  minute;  // +0x06  i32   0..59
    // +0x0A
    i32  second;  // +0x0A  i32   0..59  (advance accumulator; carries into minute at 60)
};                // sizeof == 0x0E (14)
```

Notes recovered from the binary:

- **`day` is NOT a calendar date — it is a running day count.** Pack/Unpack treat the upper
  half of `day` as the *year* and the lower part as season/within-year. See §2.
- The clock is stored **most-significant first conceptually** but laid out as above; the
  per-field offsets are the ground truth (the disk record is a *different*, packed layout —
  §2).
- `WORD2(qword_13CE852)` (i.e. bytes +4..+5, the `hour`) is read all over the frame loop as
  "current hour"; e.g. the end-of-day test `WORD2(qword_13CE852) >= 0x17` means *hour ≥ 23*.

### Setters

```c
// 0x5831f0 — VIBE_GameTime_Set(rec, hour@dl, minute@cl, second@bl)
//   rec[+0x0A] = second; rec[+0x04] = hour(low byte); rec[+0x06] = minute
//   Used to *reset* the time to a fixed clock value, e.g. Set(rec, 6, 0, 0) = "06:00".
```

`Set(rec, 6, 0, 0)` ("snap to 06:00, the start of the in-game day") is issued repeatedly at
day boundaries (see §4).

---

## 2. Disk record & the **Europa-1400 year bias**

The save/load format uses a **different, packed 12-byte record**, and this is where the
title's "Europa 1400" calendar bias lives.

`VIBE_GameTime_PackToRecord @0x583304` (live → disk):

```c
*(u16*)(out + 2) = rec.day_year + 1400;   // <-- YEAR BIAS: in-world year = day's year + 1400
 i32 season      = rec.day % 4;            // 0..3
*(u8 *)(out + 0) = 1;                      // record tag / "day index within season" base = 1
*(u8 *)(out + 1) = 3*season + 1;           // month-ish: 1,4,7,10  → quarter/season start month
*(u8 *)(out + 4) = rec.hour;               // low byte of hour
*(u8 *)(out + 5) = rec.minute;             // low byte of minute
*(i32*)(out + 8) = rec.second;             // seconds
```

`VIBE_GameTime_UnpackFromRecord @0x58334c` (disk → a *display* struct, the inverse):

```c
*(i32*)(disp + 0x00) = (*(i32*)in >> 16) - 1400;   // <-- subtract the 1400 bias back out
*(u16*)(disp + 0x04) = *(u8*)(in + 4);             // hour
*(i32*)(disp + 0x06) = *(u8*)(in + 5);             // minute
*(i32*)(disp + 0x0A) = *(i32*)(in + 8);            // second
```

So the **Europa-1400 year bias is `1400`**: the engine stores the year internally as an
offset from the game's founding year and only adds `+1400` when writing it to disk or to a
display record (and subtracts it when reading back). A fresh game's `day == 0` therefore
prints/saves as the year **1400**. `PackToRecord` also derives a season-quarter "month"
(`3*(day%4)+1` → 1/4/7/10) for the on-screen date display.

`PackToRecord` is called whenever the date must be shown or persisted: the turn-begin and
turn-transition info scrolls (`VIBE_GameTick_BeginPlayerRound @0x533188`,
`VIBE_GameLogic_RunTurnTransition @0x5310a4`) call it to render the current date string, and
the save system (doc 13) calls it to serialize the clock.

---

## 3. `VIBE_GameTime_Advance @0x583150` — the per-frame clock advance

This is the core advance with **432 xrefs** — it is invoked from essentially every place
that needs to roll the clock forward by a `(hours, minutes, seconds)` triple, with full
borrow/carry and normalization:

```c
// 0x583150 — VIBE_GameTime_Advance(rec@eax, dHour@edx, dMin@ecx, dSec@ebx)
rec.second += dSec;                       // accumulate seconds
rec.minute += rec.second / 60;            // carry seconds → minutes  (signed division)
rec.second %= 60;
rec.hour   += rec.minute / 60;            // carry minutes → hours
rec.minute %= 60;
hour = dHour + rec.hour;
for (; hour >= 24; ++rec.day) hour -= 24; // carry hours → days, incrementing day count
while (hour < 0) { hour += 24; --rec.day; } // symmetric borrow for negative deltas
rec.hour = hour;
```

It honours signed/negative deltas (rewinding the clock decrements `day`), reproduces the
exact integer truncation, and is the single mutation point for the live clock.

`VIBE_GameTime_AdvanceThunk @0x583374` is a register-shuffling thunk
(`__userpurge`) that simply forwards to `Advance` with the argument registers permuted
(`Advance(a1, a3, a4, a2)`) — it exists so call sites with a different register convention
can reach the same routine.

### Who drives the per-frame advance: `VIBE_Clock_ComputeGameTimeOfDay @0x527778`

This is the *timer callback* registered with the tick base (§5). Each time the timer fires
it computes a scaled second-delta from the current frame interval and feeds it to `Advance`:

```c
// 0x527778 — runs only while it is "day" (hour < 23) and not paused (word_63C740 & 0x80 clear)
if ( (cur_hour < 23 && live_hour < 23) || paused ) {
    v = (dword_1233558 * 0.00625 + 0.5) * dword_63CC60;   // scaled seconds for this tick
    VIBE_GameTime_Advance(&qword_122F840,                  // (operates on the *staging* clock)
                          v/3600,        // hours
                          v%3600 % 60,   // seconds
                          v%3600 / 60);  // minutes
}
```

Constants (recovered by `get_bytes`):

| Symbol | Address | Value | Role |
|---|---|---|---|
| `flt_622958` | `0x622958` | `0x3BCCCCCD` = **0.00625** | seconds-per-(speed×interval) scale |
| `dbl_622960` | `0x622960` | `0x3FE0000000000000` = **0.5** | rounding bias added before scaling |
| `dword_63CC60` | `0x63CC60` | — | day-length multiplier (time-of-day speed) |
| `dword_1233558` | `0x1233558` | — | current frame interval / "game-speed delay" base |

Note `Advance` here mutates the **staging clock `qword_122F840`** (the command-system's
working copy); the authoritative live clock `qword_13CE852` is updated through the
deterministic command pipeline (doc 19) so single-player and multiplayer stay bit-identical.
The frame loop reads the live clock back for lighting and HUD.

---

## 4. The round / turn machinery — order of per-round systems

The **master round driver** is `VIBE_GameLogic_InitOrLoadSession @0x533a54` (doc 12). After
loading the world it enters the outer round loop:

```c
while (!quit && VIBE_GameLogic_RunFrameLoop(...) && dword_63CC2C /*round*/ < 800) {
    if (singleplayer-fresh-round) { word_63CC5C = 0; VIBE_GameTick_BeginRound(); }
    // ... iterate player turns (word_63CC5C) ...
    // ... end-of-day clock jump ...
    VIBE_GameTick_BeginPlayerRound();   // the heavy per-round economy/AI pass
    ++dword_63CC2C;                     // <-- ROUND ADVANCE
}
```

`dword_63CC2C` is the **round counter** (capped at 800; init at `0x52f30b` /
`VIBE_Game_InitWorldAndSounds`). `word_63CC5C` selects the **current player/turn** within
the round.

### 4.1 `VIBE_GameTick_BeginRound @0x52f66c` — start-of-round setup

Logs `"main_RundenBeginn(): init round no %i"` and walks all object slots
(`word_12CE910`, stride 268, 768 entries). For each owner of type 1..8 it:

1. registers per-building "AP events" for guild-master roles
   (`VIBE_MeisterAi_RegisterApEvent`),
2. populates each owner's occupant list (`VIBE_Building_PopulateOccupantList`),
3. sums workstation activity by category (`VIBE_Building_SumWorkstationByCategory`),
4. tallies AP events (`VIBE_MeisterAi_SumApEventsByOwner`) and emits a build-op-90 command
   (`VIBE_Command_RequestBuildOp90`).

### 4.2 Per-turn: `VIBE_GameLogic_ProcessTurnActions @0x52f8d0`

Run for each human/AI turn. In order it: rebuilds the day-cycle table for the season
(`VIBE_DayCycle_BuildTimeTable`, §6); syncs the clock through the command system
(`QueueRequestFlagBlob32(0, …)` carrying `day` + `timeGetTime()`); fires queued **event
handlers** (104/112/124/131/132/133/76 via `VIBE_He_FindFirstHandlerByFilter` +
`QueueRequestSlotReset28`); renders the turn-begin scroll (date, ledger, office info); runs
the **renovation / building-upkeep economy** pass (per-building worth, wear, room values,
upgrade costs — see doc 21); queues random NPC actions
(`VIBE_NpcAction_QueueRandomActions`); resolves interaction targets and seats characters.

### 4.3 End-of-day clock jump

When the day is exhausted, `InitOrLoadSession` rolls the clock to the next morning through
the command system (so it stays deterministic):

```c
while (WORD2(clock) < 0x17 /*hour < 23*/)            // fast-forward remaining day in 30-sec steps
    VIBE_GameTime_Advance(&clock, 0, 0, 30), QueueRequestPerm30(&clock);
VIBE_GameTime_Advance(&clock, 24, 0, 0);            // cross the day boundary (+24h → ++day)
VIBE_GameTime_Set(&clock, 6, 0, 0);                 // snap to 06:00 next morning
QueueRequestPerm30(&clock);
```

### 4.4 `VIBE_GameTick_BeginPlayerRound @0x533188` — the heavy per-round world advance

This is the **economy/AI tick** that runs once per round. Its callees define the canonical
**order in which per-round systems advance** (each block punctuated by
`VIBE_Amt_RefreshGuildState` + `Sleep(100)` to drip-feed network sync):

1. **City wealth grid** — `VIBE_City_ComputeWealthGrid`.
2. **Per-NPC statistics snapshot** — `VIBE_Statistic_DumpNpcRecord` over all 768 slots;
   clears per-turn flag bits (`& 0xE0874703`).
3. **Master-AI registered events** — `VIBE_MeisterAi_TickRegisteredEvents`,
   `…ExpireEventSlots`, `…ExpireApEventSlots`.
4. **Plant / garden growth** — `VIBE_Plant_AdvanceGrowthStage` for grow-type objects.
5. **Building production recompute** — `VIBE_Building_RecalcAllProduction`.
6. **City stats snapshot** — `VIBE_City_SnapshotStats`.
7. **Guild/office economy (the "Amt" cluster)**, strictly in this order
   (doc 21): `…ProcessPlayerTurn` (per player) → `…RunProductionPass` →
   `…UpdateOfficeProsperity` → `…RunBuildingTaxPass(3)` → `…ProcessLoanRepayments` →
   `…ProcessAllOfficeWages` → `…UpdateOffices`.
8. **Master-AI building tasks** — `VIBE_MeisterAi_RunBuildingTasks`, `…NullTick`.
9. **News / events** — `VIBE_He_ProcessAllPlayerNews`,
   `VIBE_AiMethod_BroadcastGroupState`.
10. **City stats broadcast & turn-timer** — `VIBE_City_TickStatsAndBroadcast`;
    `VIBE_City_SendSyncCommand` *or* `VIBE_GameTick_AdvanceTurnTimer @0x57957c`.
11. **Building-need AI** — `VIBE_MeisterAi_ProcessBuildingNeeds`.
12. **Per-character turn-state sync** — `VIBE_Character_SyncAllTurnStates`, then the
    end-of-round scroll / outro if the game is over.

### 4.5 `VIBE_GameLogic_RunTurnTransition @0x5310a4`

The **per-turn financial summary** ("Rundenabschluss"): tallies the active player's income,
taxes, wages, production worth, and building values into the on-screen ledger scroll
(`VIBE_Text_RenderRichString` with the `0x1BE…/0x1C…` string IDs), then zeroes the
per-turn accumulators on the family record. Runs once per player turn at the round's end.

### 4.6 Round-orchestration command requests

- `VIBE_GameTick_RequestStartTurn @0x579530` builds a 36-byte command payload (tagged
  `1685283436` = `'lrtS'`-ish) and submits it via **command op 86**
  (`VIBE_Command_RequestBuildOp86 @0x495a90`). This is how "start this turn" is requested
  through the deterministic command channel.
- `VIBE_GameTick_Finalize @0x41beb8` is **mis-named in IDA** — it is actually the generic
  **`.form` UI loader** (`"forms\…​.form"`): it reads a form file, instantiates its windows
  and widgets, and returns a form id. The round/turn screens (`misc\advancegame`,
  `Runden\Spielerrunde_Beginn_NEU`) are loaded through it, which is why it sits in this
  cluster, but it does not itself advance any clock.

---

## 5. The real-time tick base (frame pacing)

The wall-clock heartbeat is a Win32 multimedia timer (→ SDL timer, rule 4).

- `VIBE_TimeBase_StartTimer @0x44e240` calls `timeGetDevCaps` / `timeBeginPeriod`, stores
  the requested period in **`uDelay` (`0xB53948`)**, and arms `timeSetEvent(uDelay,
  wPeriodMin+5, fptc, …)`. It also seeds the RNG (`VIBE_Util_RandSeed`) — so the tick base
  and the deterministic RNG share an origin.
- The callback **`fptc @0x44e130`** is the heartbeat. Each fire (re-entrancy-guarded by
  `dword_62EB48`):
  - increments `dword_62EB44` (raw fire count);
  - walks a **proc table** of up to 96/3 = 32 registered callbacks
    (`dword_B537C8[]` = fn, `dword_B537CC[]` = divisor, `dword_B537D0[]` = disable flag) and
    invokes each whose divisor evenly divides the master tick `dword_62EB38` and which is
    not disabled — this is how `VIBE_Clock_ComputeGameTimeOfDay` (§3) gets called at its
    chosen rate;
  - bumps derived sub-counters (`/3`, `&1`) and advances the **master tick counter
    `dword_62EB38 @0x62EB38`** (optionally decimated 1-in-20 via `byte_62EB54`/`dword_62EB58`);
  - re-arms the next `timeSetEvent`.

`dword_62EB38` is the engine's free-running tick; the frame loop and many subsystems
schedule work relative to it (e.g. `dword_11AA488 = dword_62EB38 + 7` to throttle command
flushes). `VIBE_GameTick_GetScaledDelay @0x43c680` (the script `"GetTime"` primitive)
returns `uDelay * dword_62EB38` — wall-clock-time elapsed.

### Registering / tuning a tick callback

`VIBE_TimeBase_SetProcInterval @0x44e3ac` looks up a callback in the 32-slot table by
function pointer and writes its **disable flag** (`dword_B537D0[slot] = enable`),
effectively turning a periodic proc on/off. The frame loop and session driver call it
constantly to gate `VIBE_Clock_ComputeGameTimeOfDay` on or off — e.g. it is **disabled**
while a modal dialog or turn screen is up (so the clock freezes) and **re-enabled** when
play resumes.

---

## 6. Day cycle → lighting (link to doc 25)

`VIBE_DayCycle_BuildTimeTable @0x4b2438` builds the day's lighting keyframes for the current
**season** (`VIBE_GameTime_GetSeasonFromDay @0x58339c` = `day % 4`). It indexes a table at
`0x4AD160` (6 entries × 4 seasons, each a *minutes-of-day* threshold) and converts each into
seconds-of-day, storing the 6 thresholds into `dword_631AAC[]` / `byte_631AB4[]`:

```
0x4AD160 (per season, 6 thresholds in minutes):
  Spring: 390 510 630 1050 1170 1290    Summer: 300 420 540 1140 1260 1380
  Autumn: 420 540 660 1020 1140 1290    Winter: 480 600 690  960 1020 1140
```

(e.g. Spring dawn-band starts at 390 min = 06:30, dusk-band ends 1290 min = 21:30; summer
days are longer, winter shorter — exactly the expected seasonal daylight.)

`VIBE_DayCycle_UpdateBrightness @0x4b2504` is called **per frame** (from the frame loop's
`(v54 & 0x40) && (v54 & 0x40000)` branch, with the live clock `&qword_13CE852`). It maps the
current seconds-of-day into one of the six bands and produces a 0..600 brightness ramp
(dawn 0→100, morning 100→200, midday 200→400, afternoon 400→500, dusk 500→600), then blends
the sun/sky band lighting (`VIBE_SkyColor_BlendBandLighting`,
`VIBE_SkyColor_ApplyAmbientBlend`, `VIBE_Light_SetSunHeight`). This is the bridge from
*game time of day* to *scene lighting* — see [doc 25](25-lighting-fx.md).

---

## 7. Game-speed keys → tick rate

`VIBE_Input_HandleGameSpeedKeys @0x4ff800` (called from the frame loop) handles the speed
+/- keys (`byte_67225C == 78`/`74`):

- It issues **`VIBE_Command_IncreaseGameSpeed @0x493e2c` / `…DecreaseGameSpeed @0x493e58`**,
  which clamp the speed level **`dword_631284` (0..4)** and submit it through the command
  channel as **command op 18** (`QueueRequestFlagBlob32(18, …)`); `VIBE_Command_SetGameSpeed
  @0x493dec` clamps to ≤ 4 and is the canonical setter. So game-speed changes are
  **deterministic commands**, not local mutations — keeping multiplayer in lockstep.
- It also nudges the *local* time-of-day pacing `dword_1233558` (the interval base, in steps
  of 40, range 0..160) and calls `VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc`, which
  recomputes the scroll/zoom rate and (in multiplayer) maps the discrete speed level
  `dword_631284` through scale constants into the camera/scroll feel.

The net effect: the speed level both (a) feeds `dword_63CC60`/`dword_1233558` so the
**clock advances faster per frame** (§3), and (b) is broadcast so every client agrees on how
fast simulated time flows.

---

## 8. Provenance summary

| Symbol | Address | Role |
|---|---|---|
| `qword_13CE852` (`GameTime`) | `0x13CE852` | live in-world clock, 14 bytes (§1) |
| `qword_122F840` | `0x122F840` | staging clock used by the command pipeline (§3) |
| `VIBE_GameTime_InitDefault` | `0x58320c` | zero-init the 14-byte record |
| `VIBE_GameTime_Set` | `0x5831f0` | set hour/minute/second (snap, e.g. 06:00) |
| `VIBE_GameTime_Advance` | `0x583150` | carry/borrow advance (**432 xrefs**) |
| `VIBE_GameTime_AdvanceThunk` | `0x583374` | register-shuffle forwarder to Advance |
| `VIBE_GameTime_PackToRecord` | `0x583304` | live → disk; **+1400 year bias** |
| `VIBE_GameTime_UnpackFromRecord` | `0x58334c` | disk → display; **−1400** bias |
| `VIBE_GameTime_GetSeasonFromDay` | `0x58339c` | `day % 4` |
| `VIBE_Clock_ComputeGameTimeOfDay` | `0x527778` | timer callback → per-frame clock advance |
| `VIBE_GameTick_RunAdvanceGameDialog` | `0x4c0750` | "next day" dialog driver (frame loop) |
| `VIBE_GameTick_BeginRound` | `0x52f66c` | start-of-round building/AP setup |
| `VIBE_GameTick_BeginPlayerRound` | `0x533188` | heavy per-round economy/AI advance |
| `VIBE_GameLogic_ProcessTurnActions` | `0x52f8d0` | per-turn handlers + upkeep economy |
| `VIBE_GameLogic_RunTurnTransition` | `0x5310a4` | per-turn financial summary scroll |
| `VIBE_GameTick_RequestStartTurn` | `0x579530` | start-turn via command op 86 |
| `VIBE_GameTick_Finalize` | `0x41beb8` | (mis-named) generic `.form` UI loader |
| `VIBE_DayCycle_BuildTimeTable` | `0x4b2438` | seasonal lighting thresholds |
| `VIBE_DayCycle_UpdateBrightness` | `0x4b2504` | per-frame time-of-day → lighting |
| `VIBE_TimeBase_StartTimer` / `fptc` | `0x44e240` / `0x44e130` | mm-timer + heartbeat |
| `VIBE_TimeBase_SetProcInterval` | `0x44e3ac` | enable/disable a tick proc |
| `VIBE_GameTick_GetScaledDelay` | `0x43c680` | script `GetTime` = `uDelay * tick` |
| `VIBE_Input_HandleGameSpeedKeys` | `0x4ff800` | speed +/- → command op 18 |
| `dword_62EB38` | `0x62EB38` | master free-running tick counter |
| `uDelay` | `0xB53948` | mm-timer period (ms) |
| `dword_63CC2C` / `word_63CC5C` | `0x63CC2C` / `0x63CC5C` | round counter / current turn |
| `dword_631284` | `0x631284` | game-speed level (0..4) |
| `dword_1233558` / `dword_63CC60` | `0x1233558` / `0x63CC60` | clock-pacing interval / day-length mult |
