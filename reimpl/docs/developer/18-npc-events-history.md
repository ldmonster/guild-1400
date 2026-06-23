# 18 — NPC events & history

> Provenance: reconstructed from `gilde.exe` (32-bit x86, imagebase `0x400000`).
> Every symbol below is quoted as `VIBE_Foo @0xADDR`; globals as `name @0xADDR`.
> The Hex-Rays decompilation is the reference of record.

This chapter documents the two subsystems that give *Die Gilde* its living-world
feel:

1. **The history / chronicle system** (`VIBE_History_*`, `0x4fc...0x4fe...`) — a
   templated text database (the *Chronik* / "historie\\" files) whose lines are
   re-rendered every play session into per-event scroll pop-ups. Templates carry
   embedded **context tokens** (`_NEW`, `_USE`, `_REL`, `_SET`) and **replacement
   labels** (`BUERGERMEISTER`, `REICHSTER_EINWOHNER`, …) that are resolved at
   display time against the live 768-slot entity table by a 15-entry **resolver
   dispatch table** at `funcs_4FD5D2 @0x6343d8`.
2. **The NPC-event cluster** (`VIBE_NpcEvent_*`, `VIBE_Event_*`) — the per-round
   action state-machines that drive births, deaths, crimes, plague, gambling,
   patrols, etc., plus the **cutscene triggers** (`VIBE_Cutscene_Birth/Death/
   Execution/Auction…`) fired from the frame loop, and the `VIBE_History_Notify*`
   family that records those happenings as in-world news messages.

Cross-links: [14 — Per-frame loop](14-per-frame-loop.md),
[15 — Game time](15-game-time-tick.md),
[19 — Commands](19-commands-netcode.md),
[21 — Economy & office](21-economy-office-stats.md),
[22 — Script engine](22-script-engine.md).

---

## 1. The chronicle text database

### 1.1 Files and global state

The chronicle is a pair of localized text files loaded from the `historie\`
directory. `VIBE_History_LoadChronicleText @0x4fced0` builds the file names from
the active locale string `byte_13CD6A0[756 * byte_6477A1]` (756-byte locale
records, selected by `byte_6477A1 @0x6477a1`):

```
"historie\text_H_%s"             (aHistorieTextHS  @0x6207d0)  — the event lines
"historie\text_H_%s_Kommentare"  (aHistorieTextHS_0@0x6207e4)  — the comment lines
```

Each is loaded through the shared **text-file slot cache**
(`VIBE_Text_FindTextFileSlot @0x44d8f8`, `VIBE_Text_ReloadTextFile @0x44d970`,
28-dword slot stride at `dword_77BF10 @0x77bf10`). The loader records, for each
file, the first label index and the last label index into a small bank of
globals:

| Global | Addr | Meaning |
|---|---|---|
| `dword_63391C` | `0x63391c` | first label index of `text_H` (event lines); `-1` = not loaded |
| `dword_633920` | `0x633920` | last label index of `text_H` |
| `dword_633928` | `0x633928` | **scan cursor** — current label being shown |
| `dword_63392C` | `0x63392c` | first label index of the `_Kommentare` file |
| `dword_633930` | `0x633930` | last label index of the `_Kommentare` file |
| `byte_633924`  | `0x633924` | **active flag** (0=idle, 1=show-forward, 2=show-real) |
| `dword_122DAE0[17*g]` | `0x122dae0` | per-group context slot table (4 groups × 17 dwords) |
| `dword_8C36B0[i]` | `0x8c36b0` | the actual loaded line-pointer array (text DB) |
| `dword_B537B4` | `0xb537b4` | "history subsystem available" guard |

A label reference `< 0x4000` is an **index into `dword_8C36B0[]`**; a value
`>= 0x4000` is treated as a raw `char*` to an inline template (this dual mode
appears in `ParseDate`, `ParseTextReal`, and `ParseCommandlineSecondPass`).

`VIBE_History_SetActiveFlag @0x4fd218` is a one-liner that just writes
`byte_633924` (sets which scroll mode is armed). Callers:
`VIBE_GameLogic_InitOrLoadSession @0x533a54` and
`VIBE_Save_LoadHistoryAndCarts @0x5ab59c` load the DB at session start / load.

`VIBE_History_FreeChronicleFiles @0x4fd194` resets the four group slots
(`dword_122DAE0[17*i]=1`, fill the 8-dword group payload with `-1`) and frees
every cached text file whose name begins with `"historie\"`
(`aHistorie_0 @0x620804`, compared case-insensitively over the 112-byte file
records `byte_77BEB0 @0x77beb0 … unk_77F6B0`).

### 1.2 The label record / line layout

A chronicle entry is a run of consecutive **labels** in the text DB. The runtime
treats them positionally, three labels per event, by stepping the cursor in
increments of **3** (`dword_633928 += 3`):

```
label N+0 : <<DATE>>     "DD.MM.YYYY"  (parsed by VIBE_History_ParseDate)
label N+1 : event text   (template with _CTX tokens / [#...#] replacements)
label N+2 : commandline  (side-effect directives: _SET groups, FEST … etc.)
```

`VIBE_History_ParseDate @0x4fe2d4` validates that label N is a 10-char
`"%i.%i.%i"` date (`aHstParsedateFa @0x620abc` on failure: *"Label %i is not a
<<DATE>>"*). It splits the string into day/month/year via `VIBE_Util_ParseInt`,
normalises missing day/month (`v72=1` ⇒ month-only, `v72=2` ⇒ year-only), and
stores `year-1400` into the caller's `*a4`. Depending on the mode flag `a2`:

* `a2 & 4` → copy the raw date string out;
* `a2 & 2` → render a localized long date:
  `"%s, %i %s %i"` (`aSISI @0x620af0`), `"%s, %s %i"` (`aSSI @0x620b00`), or
  `"%s, %i"` (`aSI_4 @0x620b0c`) using the locale month names at
  `byte_13CD6A0[756*byte_6477A1 + 32]` and month base `v39+79`;
* `a2 & 1` → stash the first 10 chars into the date scratch `byte_122DBF0`.

### 1.3 Per-session re-rendering of event text

Event lines are **not** stored expanded; they are re-resolved against the *current*
world every time they are shown. Two passes do this:

* `VIBE_History_ParseTextFirstPass @0x4fd220` — first pass over the event-text
  label (label N+1).
* `VIBE_History_ParseTextSecondPass @0x4fdcec` — second pass: walks the template,
  detects `_`-prefixed context tokens, accumulates the token name, and on the
  closing pattern (two `-` separators) calls `VIBE_History_ParseContext` to
  substitute the resolved entity name in place. Syntax errors emit
  *"hst_ParseText_Fake_2nd_Pass() failed: Syntax Error in Label %i < %s >"*
  (`aHstParsetextFa_1 @0x620a3c`). `byte_64A208 @0x64a208` is the ctype table used
  to classify identifier characters (`& 0x20` = alnum).

`VIBE_History_ParseTextReal @0x4fe0ec` is the **display-time** renderer used by the
"Real" scroll path. It scans the line for `[`, `#`, `]` markers: `#…#` brackets a
hidden span, `[ … ]` a span to drop. On a clean line (no unmatched markers) it
renders `"%s"` (`aS_14 @0x62088c`) into the output. Syntax errors:
*"hst_ParseText_Real() failed: Syntax Error in Label %i"* (`aHstParsetextRe @0x620a84`).

### 1.4 Context tokens and the context-group table

`VIBE_History_ParseContext @0x4fd44c` is the heart of the substitution engine. It
first classifies the leading **context token** by matching the line's first dword
against the 5-byte records of `dword_6343B8 @0x6343b8`:

```
0x6343b8:  "_NEW\0"  "_USE\0"  "_REL\0"   ("_SET\0" / "_USE\0" follow at 0x6343c7/0x6343cc)
```

| Token | meaning |
|---|---|
| `_NEW` (idx 0) | allocate a fresh resolved entity into the group slot and remember it |
| `_USE` (idx 1) | reuse the entity already bound to this group slot |
| `_REL` (idx 2) | release / dereference |
| `_SET` | commandline directive (binds a group, handled in 2nd-pass commandline) |

`v16` holds the matched token index (default `4` = "no token / literal"). If a
token is present, the next byte is the **group slot number** (`VIBE_Util_ParseInt`,
must be `< 8`, else *"Wrong slot reference %i"* `aHstParsecomman`-style error). The
group-slot store is `v15` = `dword_122DAE0[17*g]` passed in by the caller; slot 0
of each group is the "occupied" flag, the rest hold the bound entity + cached
label index.

For `_USE` the cached label index is read straight back
(`HIBYTE(v17) = v15[2*v13+2]`). Otherwise the **replacement label** name is matched
against the 15-entry label table `aBuergermeister_3 @0x633ff8` (64-byte stride):

```
idx  label (0x633ff8 + 64*idx)
 0   BUERGERMEISTER
 1   BISCHOF
 2   RND_GILDENMEISTER
 3   RND_AMTSTRAEGERIN
 4   RND_AMTSTRAEGER
 5   RND_AMTSPERSON
 6   REICHSTER_EINWOHNER
 7   BESTES_WIRTSHAUS
 8   GELD
 9   STADTKASSE
10   RND_KIRCHENBERUF
11   RND_REICH
12   RND_NPC_EINWOHNER
13   RND_HANDELSHERR
14   RND_SPIELER
```

The matched label index (`v10`, 0–14, `>=15` ⇒ *"Unknown Replacement"*
`aHstParsecontex_1 @0x6208f8`) is then used as the index into the **resolver
dispatch table** and the resolver is tail-called:

```c
result = ((int (__userpurge*)(...))funcs_4FD5D2[v17>>24])(
            v16 /*token: 0=_NEW,1=_USE,2=_REL*/,
            (int)v15 /*group store*/,
            v3 /*remaining text*/,
            v13 /*group slot*/,
            ...,
            v14 /*output buffer*/);
```

If the resolver returns 0, the group slot is cleared (`*v15 = 0`). For `_NEW`
(token 0) the resolver also writes the resolved entity id back into the slot
(`LOBYTE(v15[2*v13+2]) = label_index`) so a later `_USE` reuses it.

---

## 2. The resolver dispatch table `funcs_4FD5D2 @0x6343d8`

20 function pointers; the first **15** are reachable from `ParseContext`
(index = replacement-label index above). Raw bytes (little-endian):

```
0x6343d8:
  ac 8f 4f 00  e0 90 4f 00  38 92 4f 00  18 95 4f 00  9c 98 4f 00
  20 9c 4f 00  74 9f 4f 00  78 a1 4f 00  90 a2 4f 00  bc a3 4f 00
  0c a5 4f 00  18 a8 4f 00  b8 aa 4f 00  80 ac 4f 00  54 af 4f 00
```

All resolvers share the **`__userpurge`** prototype
`(char token@al, int groupStore@edx, const char* text@ecx, int slot@ebx, char* out)`
(WoundedPerson additionally takes `a5@ebp`). Common scaffolding in every one:

* zero a 512-byte scratch, call `VIBE_Text_StripNameTokens @0x4f8d98` to split the
  template tail into the printf format string + a trailing numeric **scope code**;
* `VIBE_Util_ParseInt` the scope code, clamp `>2` to default `1`. Scope `0` =
  active persons, `1` = any person-typed, `2` = "carried"/secondary set;
* `token==1` (`_USE`) path: re-fetch the bound entity via
  `VIBE_Person_FindRecordById @0x58bc6c` and re-validate it (alive flag at `+8`,
  profession byte at `+358`/`+361`);
* otherwise **scan the 768-slot entity table** starting at a random offset
  (`VIBE_Math_RandomModulo(0x300u) @0x58b89c`, `0x300 == 768`), wrapping
  `(i+1) % 768`, for the first slot matching the resolver's predicate;
* on success render `out = sprintf(format, entityName)` via
  `VIBE_Text_RenderFormattedMessage @0x59f99c` and, for `_NEW`, write the entity
  id back into the group slot (`*(group + 8*slot + 4) = id`).

The 768-slot person table lives at `word_12CE910 @0x12ce910` (268-word /
536-byte stride per record). Key per-record fields used by the predicates:

| Field | Addr base | Meaning |
|---|---|---|
| `word_12CE910[268*i]` | `0x12ce910` | entity handle (`-1` = empty) |
| `dword_12CE914[134*i]` | `0x12ce914` | entity id |
| `byte_12CE912[536*i]` | `0x12ce912` | entity **type/category** byte (`<10` filter; `6/7`=family) |
| `byte_12CE918[536*i]` | `0x12ce918` | "alive / valid" byte |
| `byte_12CEA74[536*i]` | `0x12cea74` | **profession** byte |
| `byte_12CEA76[536*i]` | `0x12cea76` | **office/role** byte (15=mayor, 26/21=officials, 30-33=clergy) |
| `byte_12CEA79[536*i]` | `0x12cea79` | secondary role byte (clergy uses 0x1E–0x21) |

### 2.1 Resolver-by-resolver

| idx | label | `@addr` | resolver | what it picks |
|---|---|---|---|---|
| 0 | `BUERGERMEISTER` | `0x4f8fac` | `VIBE_Command_ResolveTargetGuard` | the **mayor**: first slot with role `byte_12CEA76 == 15` |
| 1 | `BISCHOF` | `0x4f90e0` | `VIBE_Command_ResolveTargetOfficial` | an **office-holder**: role `== 26` or `== 21` |
| 2 | `RND_GILDENMEISTER` | `0x4f9238` | `VIBE_Command_ResolveTargetClergy` | a **cleric/guildmaster**: secondary role `byte_12CEA79 ∈ [0x1E,0x21]`; scope 0/1/2 vary person vs carried fallback |
| 3 | `RND_AMTSTRAEGERIN` | `0x4f9518` | `VIBE_Command_ResolveTargetPersonByName` | a person flagged `dword_12CE919==1`, role not in {0,15,26,21}, not already in the selection list |
| 4 | `RND_AMTSTRAEGER` | `0x4f989c` | `VIBE_Command_ResolveTargetPersonAlt` | same as #3 but selecting the **complement** flag (`dword_12CE919==0`) |
| 5 | `RND_AMTSPERSON` | `0x4f9c20` | `VIBE_Command_ResolveTargetPersonScoped` | person, role not in {0,15,26,21}, scope-aware person→carried fallback; rejects `token==2` |
| 6 | `REICHSTER_EINWOHNER` | `0x4f9f74` | `VIBE_Command_ResolveTargetBestRated` | the **richest** resident: max `VIBE_Person_ComputeTotalWealth @0x591f7c` over the table |
| 7 | `BESTES_WIRTSHAUS` | `0x4fa178` | `VIBE_Command_ResolveTargetBestTavern` *(building-scoped, best-rated inn)* |
| 8 | `GELD` | `0x4fa290` | `VIBE_Command_ResolveTargetMoney` *(formats a money amount, not an entity)* |
| 9 | `STADTKASSE` | `0x4fa3bc` | `VIBE_Command_ResolveTargetCityTreasury` *(city-treasury value)* |
| 10 | `RND_KIRCHENBERUF` | `0x4fa50c` | `VIBE_Command_ResolveTargetCraftWorker` | profession `byte_12CEA74 ∈ [13,18]` (craft/church-trade range) |
| 11 | `RND_REICH` | `0x4fa818` | `VIBE_Command_ResolveTargetByStatGroup` | builds a **top-5** wealth list (insertion-sort of the 5-wide `dword_4F8C04` slot array) then picks one at random; predicate fn chosen by scope (`IsPersonType`/`IsCarriedType`/`IsValidActiveRecord`) |
| 12 | `RND_NPC_EINWOHNER` | `0x4faab8` | `VIBE_Command_ResolveTargetWoundedPerson` | a person with `byte_12CE912 == 0` (unwounded/healthy NPC inhabitant), optional profession-range filter `>>24 == scope` |
| 13 | `RND_HANDELSHERR` | `0x4fac80` | `VIBE_Command_ResolveTargetByProfessionRange` | profession in `[19,69]` **excluding** `[31,33]`, `[40,45]`, `[52,57]`; top-5 wealth list then random pick |
| 14 | `RND_SPIELER` | `0x4faf54` | `VIBE_Command_ResolveTargetRandomCarried` | a random **carried** entity (`VIBE_Entity_IsCarriedType @0x4f8f2c`), stepping by a random stride from `dword_4F8C30`; rejects `token==2` |

(indices 15–19 of the table are auxiliary thunks not reached via the label path.)

The "top-5" resolvers (#11, #13) keep a parallel 5-entry score array
(`dword_4F8BF0/dword_4F8C04` and `dword_4F8C10/dword_4F8C24`) primed from static
seeds, do an in-place bubble of new high scorers into it, then index a random one
of the five with `VIBE_Math_RandomModulo(5u)`. The profession ranges encode Die
Gilde's craft families (smith/baker/tavern/etc.); the excluded sub-ranges in #13
remove offices and clergy from the "merchant lord" pool.

Helper predicates (all `@0x4f8...`): `VIBE_Entity_IsPersonType @0x4f8ee4`,
`VIBE_Entity_IsCarriedType @0x4f8f2c`, `VIBE_Entity_IsNotInSelectionList @0x4f8e1c`,
`VIBE_Person_IsValidActiveRecord @0x4f8e60`, `VIBE_Text_StripNameTokens @0x4f8d98`.

### 2.2 The commandline (side-effect) dispatch `funcs_4FDAA5 @0x634414`

`VIBE_History_ParseCommandlineFirstPass @0x4fd6ac` and
`…SecondPass @0x4fd8ac` handle label N+2. The second pass matches each
whitespace-separated directive's verb against the 64-byte-stride keyword table
`aFest @0x633938` (first entry `"FEST"`), then tail-calls the matching handler in
`funcs_4FDAA5 @0x634414` (`27` keywords; index `>=27` ⇒ ignored). When the line is
group-scoped (`v27 == dword_6343C7 /*"_SET"*/` or `== dword_6343CC /*"_USE"*/`) the
leading int selects the group slot (`dword_122DAE0[17*v20]`, must be `< 4` else
*"Invalid GROUP reference in Label %i"* `aHstParsecomman_2 @0x6209f0`). This is how
an event line both prints and **mutates** persistent group bindings.

---

## 3. Showing an event — the scroll path

`VIBE_History_DisplayCurrentEvent @0x4fed20` is the per-frame entry called from
the game loop. It:

1. queues a network/replay request stamp (`VIBE_Command_QueueRequestFlagBlob32`
   with `GetTickCount`) if `byte_63CC28 & 8` or `dword_764CE0 == -1`;
2. spins `VIBE_Amt_RefreshGuildState @0x4becdc` until the guild-state counters
   `dword_122DC00`/`dword_764CF0` agree;
3. when settled and `byte_633924` is armed, dispatches by mode:
   * `byte_633924 == 1` → `VIBE_History_ShowEventScrollForward @0x4fe74c`
   * `byte_633924 == 2` → `VIBE_History_ShowEventScrollReal @0x4fea88`

Both scroll functions share the same shape:

* zero a 256-byte **date** buffer and an 8120-byte **body** buffer;
* call the scanner (`ScanNextEventForward @0x4fe694` / `ScanNextEventReal @0x4fe9cc`)
  to fill them; return early if the scan result `!= 1`;
* open the parchment UI (`VIBE_Scroll_Open @0x4be8d8`, `VIBE_DragCursor_SetSprite`),
  select the three sub-windows (`VIBE_Form_SelectWindow @0x41e4cc` indices 1/2/3),
  render the body with `"$C%s"` (`aCS @0x620b14`) and the date with
  `"$C$Z$[%s$]"` (`aCZS_1 @0x620b1c`) through `VIBE_Text_RenderRichString @0x59d6e8`,
  and create the forward/back scroll buttons
  (`VIBE_Window_CreateScrollButtons @0x419ad8`, button id 1753, rich-string id 3347);
* run an inner loop driven by `VIBE_GameLogic_RunFrameLoop @0x4c09a0` and
  `VIBE_Scroll_UpdateAnimation @0x4be990`; when the UI command latch
  `dword_75BF38 == 1210` ("next page") it re-scans the next event in place; when no
  more events the "end" flag `dword_631614` is set;
* close with `VIBE_Scroll_Close @0x4be960`.

### 3.1 The scanners

`VIBE_History_ScanNextEventForward @0x4fe694` walks labels from the cursor
`dword_633928` in steps of 3. For each triple it calls `ParseDate(cursor,3,…)`; it
shows the event only when the event date is **exactly one year before** the current
in-game date (`qword_13CE852 @0x13ce852` holds the packed current date;
`(DWORD)qword_13CE852 - parsedYear == 1`). It then expands the body with
`VIBE_History_ParseTextReal`. Return codes:

| code | meaning |
|---|---|
| 1 | event ready (body non-empty) |
| 6 | matched-but-empty body (skip, keep scanning) |
| 2 | reached a future-dated event → stop here |
| 3 | inactive / scan exhausted (`byte_633924` cleared) |
| 4 | `ParseDate` failed (corrupt label) → deactivate |

`VIBE_History_ScanNextEventReal @0x4fe9cc` is the same loop but renders through
`VIBE_History_ParseLabelPasses @0x4fe0b0`, which runs both
`ParseTextFirstPass` and `ParseTextSecondPass`, and (when its `a3` flag is set)
also `ParseCommandlineSecondPass` — i.e. the **"real" path applies side effects**,
the "forward" path is display-only. It adds code `5` for a label that failed the
two-pass parse.

---

## 4. The NPC-event cluster

Beyond the scripted chronicle, the world is animated by **per-actor event
state-machines**. These are not part of the history text DB; they are stepped each
frame for active NPCs and many of them *call into* the `VIBE_History_Notify*`
recorders (§5) and the cutscene triggers (§6) when something noteworthy happens.

### 4.1 `VIBE_NpcEvent_*` (`0x4d3c50 … 0x4db2b1`, 45 functions)

Each event is a small `(Init/Setup, Step, Reset)` triple keyed off a per-actor
state byte. Representative members:

| Event | Step `@addr` | Drives |
|---|---|---|
| Protection money | `VIBE_NpcEvent_ProtectionMoneyStep @0x4d3c50` (`Init @0x4d43f8`) | extortion racket collection |
| Extortion | `VIBE_NpcEvent_ExtortionStep @0x4d4460` | shake-down dialogue |
| Patrol | `VIBE_NpcEvent_PatrolStep @0x4d49b8` | guard patrol routes |
| Accident sim | `VIBE_NpcEvent_RunSimAccident @0x4d4fcc` | random injuries |
| Disease sim | `VIBE_NpcEvent_RunSimDiseases @0x4d766c` | illness onset |
| Award title | `VIBE_NpcEvent_AwardTitleStep @0x4d57a0` | bestowing offices/titles |
| Tavern sim | `VIBE_NpcEvent_TavernSimStep @0x4d5cdc` | inn socialising |
| Talent level-up | `VIBE_NpcEvent_TalentLevelUpStep @0x4d63e8` | skill growth |
| Bard script | `VIBE_NpcEvent_BardCreateScriptStep @0x4d66b4` | bard performances |
| Lover / matchmaking | `VIBE_NpcEvent_AllocLoverStep @0x4d71d8`, `OfficeMatchmakingStep @0x4da978` | romance / marriages |
| **Plague reaper** | `VIBE_NpcEvent_ReaperPlagueStep @0x4d96f8` (+ `ReaperApproachTarget @0x4d8c34`, `ReaperMoveTowardTarget @0x4d8f74`, `ReaperPickNextTarget @0x4d9600`) | the Grim-Reaper death visitor |
| Politicians | `VIBE_NpcEvent_SimPoliticiansStep @0x4da7c0` (+ `PoliticianFindTarget @0x4da0dc`, `TalkToTarget @0x4da3c4`, `ReleaseTarget @0x4d9f9c`) | office-holder behaviour |
| Master exam | `VIBE_NpcEvent_MasterExamDialogStep @0x4dadd4` | journeyman→master |
| Gambling | `VIBE_NpcEvent_GamblingStep @0x4daf88` | dice/cards |
| Dark corner | `VIBE_NpcEvent_DarkCornerStep @0x4d7bd0` (`Init @0x4d79e8`) | back-alley crime |
| Broadcast winner points | `VIBE_NpcEvent_BroadcastWinnerPointsStep @0x4d9a60` | contest results |

Common scaffolding (`*Reset`, `*Init…`, `*Queue…Entity`,
`VIBE_NpcEvent_CountdownTickEntity @0x4db248`) sets durations, queues the actor
into the active-event ring, restores pose, and ticks the countdown that ends the
event.

### 4.2 `VIBE_Event_*` (`0x4ee804 … 0x4f6cd4`, 58 functions)

The lower-level **action / production** state machines and their registration:

* Production / building: `VIBE_Event_RunProduktion @0x4f2bd0`,
  `AllocProduktion @0x4f2a80`, `RunGebaeudeBauen @0x4f5b5c` (construction),
  `BuildingProductionTrigger @0x4f16b8`, `UpdateBuildingHeState @0x4f52f8`.
* Crime / raids: `VIBE_Event_FireRaidRun @0x4ee960`,
  `DiscoveryRaidRun @0x4f0708`, `RequestGuardInteraction @0x4ef7e8`,
  `AllocKillPlayer @0x4efcdc`.
* Work / harvest: `WorkActionRun @0x4f2614`, `HarvestWageRun @0x4f3b34`,
  `SlotProcessRun @0x4f4348`.
* Town-life: `NightWatchmanAnnounceRun @0x4f0250`, `ConversationSinkRun @0x4efd88`.
* **Registration**: `VIBE_Event_RegisterHandlerTable @0x4f1ed0` builds the dispatch
  table; `VIBE_Event_RegisterEvent @0x5f4980` / `RegisterSceneEvent @0x5f4a70` /
  `LoadEventBindings @0x5f4bc8` / `LoadSceneEventBindings @0x5f4c6c` wire event
  names to handlers (`WriteEventNames @0x5f4b60`). Help/tutorial events load from
  INI via `OpenHelpEventsFromIni @0x4f1850`.

`VIBE_Event_BroadcastFamilyNews @0x58c92c` is the large family-event broadcaster:
on a birth/death/marriage (type bytes `5/6/7`) it walks the family's buildings and
relatives, builds a news string with `VIBE_Text_RenderFormattedMessage`, and pushes
`VIBE_He_SendEntityMessage @0x4c5c54` (message id 1418, news templates
`_NACHRICHTEN_HS_63 @0x6267f4`, `_NACHRICHTEN_HS_70 @0x626808`). It also enqueues
inheritance / wealth-transfer command packets
(`VIBE_Command_BeginDeltaPacket @0x493a94`, …).

---

## 5. Recording happenings — `VIBE_History_Notify*` (`0x535780 … 0x536070`)

This family is the **write side**: gameplay subsystems call these to broadcast an
in-world news message (and, where applicable, to flag the event for the chronicle
scroll). They all funnel a localized template id through
`VIBE_Text_RenderFormattedMessage @0x59f99c` into the shared news buffer
`byte_122F8C0 @0x122f8c0` and push it with `VIBE_He_SendEntityMessage @0x4c5c54`
(message id **1418**, news-key string in `esi`):

| Notifier `@addr` | Event | template id / news key |
|---|---|---|
| `VIBE_History_NotifyArrestTarget @0x535780` | arrest | |
| `VIBE_History_NotifyUseItemEvent @0x5357f8` | item used | |
| `VIBE_History_NotifyBuildingLinkRemoved @0x535844` | building unlinked | |
| `VIBE_History_NotifyOfficeTransfer @0x535890` | office handed over | |
| `VIBE_History_NotifyWanderEventA/B @0x5358e0/0x535928` | NPC wandering | |
| `VIBE_History_NotifyWanderPairEvent @0x535970` | two NPCs meet | |
| `VIBE_History_BroadcastAttackEvent @0x535a04` | combat/attack | id 7323/7324/7325, `_NACHRICHTEN_…` |
| `VIBE_History_NotifyTargetFound @0x535afc` | target acquired | |
| `VIBE_History_NotifyTargetReachedA/B @0x535bb0/0x535c68` | target reached | |
| `VIBE_History_NotifyLawChangeToMaster @0x535d20` | law change | |
| `VIBE_History_NotifyCrimeAdded @0x535d88` | **crime recorded** | id 7328, `_NACHRICHTEN_HS_46 @0x6237b8` |
| `VIBE_History_BroadcastPlagueOutbreak @0x535dd8`, `NotifyPlagueSpreadStep @0x535e64`, `BroadcastPlagueSpread @0x535edc` | plague | |
| `VIBE_History_NotifyOfficeSwap @0x535f64` | office swap | |
| `VIBE_History_NotifyRivalEvent @0x536070` | rival action | |

`BroadcastAttackEvent` is typical: it checks the target's type byte
(`+2 == 6 || == 7` = a family/notable), renders id 7323/7324, then loops the whole
768-slot table and sends id 1418 to every other family member so the news spreads.

`VIBE_Save_WriteHistoryAndCarts @0x5a6ff4` / `VIBE_Save_LoadHistoryAndCarts
@0x5ab59c` persist the accumulated history group-state and the cart list across
save/load (see [13 — Save/Load](13-save-load.md)).

---

## 6. Cutscene triggers from the frame loop

The big life-events get full cutscenes. They are registered as command-table
entries by `VIBE_Cutscene_InitCommandTable @0x4acefc` (the only xref to each is the
data slot in that table) and fired by the cutscene scheduler off the frame loop:

| Cutscene | `@addr` | Trigger / guard |
|---|---|---|
| Birth | `VIBE_Cutscene_Birth @0x4a7b5c` | `VIBE_Cutscene_CheckBirthParticipants @0x4a7a64` |
| Death | `VIBE_Cutscene_Death @0x4a7fdc` | `VIBE_Cutscene_CheckDeathTimer @0x4a7fbc` |
| Execution | `VIBE_Cutscene_Execution @0x4a6b90` | |
| Auction | `VIBE_Cutscene_Auction @0x4a89f8` | |
| Wedding | `VIBE_Cutscene_Wedding @0x4a73a4` | `CheckMarriageEligible @0x4a7118` |
| Duel | `VIBE_Cutscene_Duel @0x4a53a8` | `RollDuelOutcomeTier @0x4a6908` |
| Bankruptcy | `VIBE_Cutscene_Bankruptcy @0x4a851c` | |
| Lease / Salon | `VIBE_Cutscene_LeaseWindow @0x4a9868`, `Salon @0x4a9c24` | |

Each runs through the shared cutscene engine
(`VIBE_Cutscene_AllocSlot @0x4ac7e4`, `RunParticipants @0x4aac78`,
`ExecMainFunc @0x4ab55c`, `PauseGame @0x4aa944`/`ResumeGame @0x4aa960`,
music via `VIBE_Music_PlayCutsceneTrack @0x581b0c`). The same birth/death/marriage
that triggers a cutscene also fires `VIBE_Event_BroadcastFamilyNews @0x58c92c`
(§4.2) and the matching `VIBE_History_Notify*` recorder (§5), so the moment shows
as both a cinematic and a piece of town news — and is eligible to surface a year
later as a chronicle scroll (§3).

---

## 7. End-to-end summary

```
session start ─ VIBE_GameLogic_InitOrLoadSession @0x533a54
              └ VIBE_History_LoadChronicleText @0x4fced0
                  loads historie\text_H_<loc> + _Kommentare into dword_8C36B0[]
                  records label-range globals dword_63391C..633934

per frame ─ NPC-event step machines (VIBE_NpcEvent_* / VIBE_Event_*)
          ├ life-event fires cutscene (VIBE_Cutscene_Birth/Death/… @0x4a...)
          ├ VIBE_Event_BroadcastFamilyNews @0x58c92c  → 1418 news to relatives
          └ VIBE_History_Notify* @0x535...           → 1418 news, flag chronicle

display ─ VIBE_History_DisplayCurrentEvent @0x4fed20  (byte_633924 armed)
        └ ShowEventScrollForward/Real @0x4fe74c/0x4fea88
            └ ScanNext… @0x4fe694/0x4fe9cc   (event year == now-1)
                ├ ParseDate @0x4fe2d4         (10-char DD.MM.YYYY)
                ├ ParseText{First,Second}Pass @0x4fd220/0x4fdcec
                │   └ ParseContext @0x4fd44c
                │       ├ token = _NEW/_USE/_REL  (dword_6343B8 @0x6343b8)
                │       ├ label  = BUERGERMEISTER…RND_SPIELER (0x633ff8, 15×64B)
                │       └ funcs_4FD5D2[label] @0x6343d8 → resolver (768-slot scan)
                └ ParseCommandlineSecondPass @0x4fd8ac
                    └ funcs_4FDAA5[verb] @0x634414  (FEST… 27 verbs, side effects)
```
