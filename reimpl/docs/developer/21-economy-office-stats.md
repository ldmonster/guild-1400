# 21 — Economy, office & statistics

This chapter documents three intertwined late-game subsystems of `gilde.exe`
(imagebase `0x400000`): the **economy** (production recipes, market pricing, and
the trade contact panels), the **office / politics** system (the 37-office hierarchy,
council elections, court trials, the torture/sentencing branch, and privileges), and
the **statistics** dashboards. All three are reached from the player's right-click
*contact menu* on a building or person, which runs inside the per-frame loop
(`VIBE_GameLogic_RunFrameLoop @0x4c09a0` — see [14 — Per-frame loop](14-per-frame-loop.md)),
so every dialog in this chapter is a *blocking modal sub-loop*: it owns the frame loop,
drives one form, and returns when `RunFrameLoop` returns 0.

Conventions: addresses are RVA-absolute (`VIBE_Foo @0x12345`). Object/ware records are
fixed-stride arrays indexed by a 16-bit id stored in the **high word** of a packed dword
(the recurring `*(int*)x >> 16` idiom is "extract ware/prototype id"). Building/object
prototype records are **65 bytes** each, based at `dword_13CE27C`; the ware/material
catalog uses **589-byte** records based at `dword_13CE294`; person records are **536-byte**
strided (`536 * id`), and the per-person *office/role* table `word_12CE910` uses a
**268-word (536-byte)** stride.

---

## Part A — Economy

### A.1 Money and rate scaling — `VIBE_Money_MultiplyByRate @0x58f19c`

```c
int VIBE_Money_MultiplyByRate(int a1 /*eax*/, u8 a2 /*dl*/) {
    return a1 * dword_649A88[ dword_13CD6F2[189 * a2] >> 16 ];
}
```

A scalar money/credit value `a1` is scaled by a per-entity rate. `a2` selects a
536-/189-strided record (`dword_13CD6F2[189*a2]`, the high word of which is a small
index), and that index reads a **multiplier byte** from the table `dword_649A88`
(`@0x649a88`). The first 32 raw bytes of that table are:

```
15 01 3F 54 15 15 15 01  93 BD 69 69 69 04 69 93
3F 3F 3F 03 54 69 3F 3F  2A 02 3F 69 2A 15 15 02 …
```

i.e. multipliers cluster around `0x15`(21), `0x2A`(42), `0x3F`(63), `0x54`(84),
`0x69`(105), `0x93`(147), `0xBD`(189) — a quantized "rate ladder" used for taxes,
fees and interest. The function is a raw `int*int` multiply with no clamping, so
overflow wraps (32-bit) exactly as the original.

### A.2 Market price — `VIBE_Building_ComputeMarketPrice @0x58f3d0`

`double ComputeMarketPrice(i16 protoId /*ax*/, u8 cityIdx /*dl*/)` — the core
ware-pricing routine. It indexes the 65-byte prototype record
`v3 = 65*protoId + dword_13CE27C`:

* **Cached/leaf price** — if `*(dword*)(v3+56)` is non-zero, the price is already
  cached as a raw stock count `v4`; the result is
  `32*v4 * cityIdx * 0.01` (`flt_626948 = 0x3c23d70a = 0.01`), and **×1.5**
  (`flt_62694C = 0x3fc00000`) when the record's type byte `*(v3+33) == 3`. `32` is
  `0x42000000`.
* **Produced price (recursive)** — otherwise it computes a *cost-of-materials* price:
  1. `v29 = max(1, *(u16*)(v3+54))` — the recipe's batch/output size (divisor).
  2. `v23 = (double)*(u32*)(v3+34) / v29` — base value per unit.
  3. A *labor* term: if `byte_13CE862[protoId]` (worker count) is non-zero it sums two
     bytes from a 589-strided worker record (`*(u8*)(v9+563)`) scaled by
     `flt_626950 · flt_626954 · dbl_62695C · dbl_626964`; otherwise the labor term is
     a flat `4.0`.
  4. `v28 = labor · dbl_62696C · v23`.
  5. **Type 23** ("raw"/`*v3==23`) short-circuits: returns `v28 · flt_626974`.
  6. Otherwise it walks up to **4 input ingredients** (`*(u16*)(v13+46)`): for each
     valid input it *recurses* `ComputeMarketPrice(inputProto, cityIdx)`, multiplies
     by the ingredient quantity `*(u16*)(v13+38)` (as float), and divides by the
     output count `*(u16*)(v3+54)`, accumulating into `v28`. Input id `0xFFFF` is the
     special "sub-object" placeholder, formatted via the debug string
     `"eval_ObjektPreis2Credits:Unterobjekt f…"` (`byte_626904`).
  7. Final: caches `(int)(v28 · 2.2 · flt_626978)` back into `*(v3+56)`
     (`v24 = 2.2`), and returns `2.2 · (v28 · cityIdx · 0.01)`.

So an item's price = (recursive material cost + labor) × city-demand factor × `0.01`,
with a `2.2` global markup and a `1.5` premium for type-3 goods. The recursion is the
production tree: finished goods are priced from their inputs all the way down to type-23
raw resources.

### A.3 Production recipes & the trade panels

The recipe data lives in the **65-byte prototype record** (base `dword_13CE27C`); the
trade panels read it directly. Relevant fields recovered from the panel code:

| Offset | Meaning |
|---|---|
| `+0`  | object **type byte** (4,6,8,11,12,13,14,16,19,22,23,37 seen — craft/building class) |
| `+33` | secondary type (==3 → ×1.5 price) |
| `+34` | base value (u32) |
| `+38` | per-ingredient **input quantity** (u16, 4 slots, stride 2) |
| `+46` | per-ingredient **input ware id** (u16, 4 slots, stride 2; `0xFFFF`=sub-object) |
| `+54` | recipe **output count** (u16) |
| `+56` | cached price (written by `ComputeMarketPrice`) |

#### Production window — `VIBE_TradePanel_BuildProductionWindow @0x5087bc`

Loads form `Handel\Handel_PRODUKTION`, builds the object-action panel header chosen by
the building's type byte:

| Building type byte (`*(589*type+dword_13CE294)`) | Header text id |
|---|---|
| 19, 4, 16 | 1697 |
| 8, 14 | 1665 |
| 6 | 1669 |
| 22, 13, 12, 11 | 1673 |
| default | 1661 |

It enumerates *produceable prototypes* via `VIBE_GameObject_QueryFind` over the
building's inventory, builds up to **16** recipe rows (`dword_122E94E`, 72-byte stride),
and for each row computes the **maximum buildable count** `dword_122E990` by, for each
of the 4 inputs, dividing available stock (`VIBE_Inventory_GetEffectiveStock @0x5923fc`)
by the required quantity and taking the min, then multiplying by the output count
`*(v3+54)`. Current finished-goods stock goes to `dword_122E994`. Selecting a row issues
a craft command (`VIBE_Command_QueueRequestSlotReset28 @0x4948c8`, see
[19 — Commands](19-commands-netcode.md)) and assigns the worker action via
`VIBE_Character_ChangePlayerAction @0x4b09c8`.

#### Three production *modes* A/B/C — handler verb codes

`OpenProductionWindowA/B/C @0x510084 / 0x510a80 / 0x511170` are three variants of the
same panel, differing only in the **handler verb** passed to
`VIBE_He_FindFirstHandlerByFilter` and the action title id:

| Function | Title id (`RenderRichString`) | Handler verbs used |
|---|---|---|
| `OpenProductionWindowA @0x510084` | 196 | **8** (produce) / **9** (cancel) |
| `OpenProductionWindowB @0x510a80` | 199 | **3** |
| `OpenProductionWindowC @0x511170` | 200 | **4** |

(Verb 8/9 = "queue craft" / "dequeue craft"; 3 and 4 are the build-from-recipe and
build-product verbs for carpenter/smith trees.) All three share the slider panel
`VIBE_Hud_BuildSliderPanel(624, 350, …, 120, …)` for batch quantity and the same
max-buildable computation described above. Window C additionally pulls candidate
prototypes from `VIBE_Object_CollectMatchingProts @0x586508` into `word_13CE860`.

#### Craft contact menus — smith & carpenter

`VIBE_ContactMenu_SmithProduction @0x513954` and
`VIBE_ContactMenu_CarpenterProduction @0x513b28` are the right-click contact loops for
the two craft shops. They register status-bar entries (gated on the flag word
`word_631758`, bits `0x200`/`0x400` = "menu page 1/2") and dispatch on the clicked entry
`dword_631720`:

| Entry string | Smith id | Carpenter id | Handler → opens |
|---|---|---|---|
| `contact_PRODUKTION_SCHMIEDEN` / `…_TISCHLER` | 19 | 19 | `BuildProductionWindow @0x5087bc` |
| `contact_LAGER` | 14 | 14 | `VIBE_StorageDialog_Options @0x5461b0` (warehouse) |
| `contact_TRANSPORT` | 21 | 21 | `TradeTransport_OpenPanelMode1` (`@0x54011c`) |
| `ob_PERSONALBUCH` | 12 | 12 | `VIBE_Personnel_RunStaffBook @0x53bccc` |
| `ob_MEISTERBRIEF` | 22 | 12 | `VIBE_Meister_RunMasterCertificateDialog @0x558e58` |

Each uses `VIBE_Interaction_TestHandlerFlagWord` to show an entry only when the
corresponding interaction handler exists.

#### Transport, export & import

* **`VIBE_Location_TradeTransport @0x513568`** — only runs when the target object's type
  byte is **71** (a transport/cart object). Loads `Handel\Handel_Transport`, shows the
  origin coat-of-arms (`ief_Wappen%li` ← `*(result+101)`), and a two-button toggle
  (`VIBE_Hud_BuildButtonRow`) between **outbound** (object id 475, rich-text `0x1854`)
  and **inbound** (476, `0x1855`) cargo. A progress bar `*((float*)v27+229)` fills at
  `(dword_62EB38 - startTick) · dbl_621918`, clamped to **0.25**.
* **`VIBE_Location_TradeSearchExport @0x513c60`** — the "find a buyer" panel
  (`HANDEL\HANDEL_SUCHEN`, title `0x140D`). It tallies demand along **three export
  ware axes** read from `dword_507F90 = {0x1C4, 0x1C5, 0x1C6}` = ware ids **452, 453,
  454**, by scanning handlers with verb **20** and accumulating handler byte `[172]`.
  Number of axis columns `v70` is **2** if building type==8 or its `[583]<2`, else
  **3**. Selecting an axis issues a sell command (verb 20, `Light_SetGrayColorThunk(…,20…)`).
* **`VIBE_Location_TradeSearchImport @0x5142ec`** — the mirror "find a seller" panel
  (title `0x1415`), with **three import ware axes** `dword_507F9C = {0x1C1, 0x1C2,
  0x1C3}` = ware ids **449, 450, 451**, scanning handlers with verb **21**.
* **`VIBE_ContactMenu_RemoteTrade @0x5138d0`** — the long-distance trade menu. Two
  entries: `FERNEINKAUF` (remote buy → `TradeTransport_OpenPanelMode4`) and `VERKAUF`
  (sell → `TradeTransport_OpenPanelMode2`). Both register with width hint `16`.

Demand text uses message ids `5134`/`5135` (export "buy N at city") and
`5142`/`5143` (import), with the per-ware name pulled at `2*wareId + 2151`.

### A.4 City demand snapshot (economy → statistics bridge)

`VIBE_Economy_LoadDemandSnapshot @0x57a5dc` copies a **40-byte (10-float)** demand
vector out of `dword_1234910` and overrides slot `[6]` with the global price index
`flt_641DA8`. This vector is the data source for the city-statistics panel (Part C).

---

## Part B — Office & politics

### B.1 Office table & definitions

#### `VIBE_Office_InitTable @0x47de78`

Zeroes the **888-byte** office state block at `byte_B59848` and seeds two parallel
arrays of **37 entries** (loop `v3 < 37`, 24-byte stride):
`byte_B59830[i*24] = i` (office id), `dword_B59834[i] = -1` and `dword_B59844[i] = -1`
(holder/successor person ids = "vacant"). It then hard-codes a **rank/level byte** into
each office slot (`byte_B59850`=1, `…868`=1, `…880`=2, `…898`=3, `…8B0/8C8`=4, `…8E0`=5,
`…8F8`=6, `…910/928`=7, `…940`=8, `…958`=9, `…970`=10, `…988`=11, `…9A0`=12, `…9B8`=13,
`…9D0`=14, `…9E8`=15, `…A00`=16, `…A18`=17, `…A30`=18, `…A48`=19, `…A60`=20, `…A78`=21,
`…A90`=22, `…AA8`=23, `…AC0`=24, `…AD8`=25, `…AF0`=26, `…B08`=27, `…B20`=28, `…B38`=29,
`…B50`=30, `…B68`=31, `…B80`=32, `…B98`=33, `…BB0`=34). Returns `7709`. This is the
in-memory office *holder/successor* registry; the on-disk form is saved/loaded by
`amt_fio_SaveAemter`/`amt_fio_LoadAemter` (string refs `@0x61af68`/`@0x61af8c`).

#### `VIBE_Office_GetDefinition @0x47f008`

Static office *definition* lookup, valid ids **0–0x24 (0..36)**, 12-byte records based
at `dword_62EC8E` (record = `&dword_62EC8E[3*id] + 2`). Decoded record layout (from the
444-byte table dump) is `{ byte index; byte categoryTier; byte rankGroup; byte cost;
… float salary/weight }`:

| Office id | tier | rank | cost byte | salary float |
|---|---|---|---|---|
| 1 | 1 | 1 | 0x05 (5) | 5.0 |
| 2 | 1 | 2 | 0x07 (7) | 5.0 |
| 3 | 1 | 3 | 0x0A (10) | 5.0 |
| 4–6 | 2 | 1–3 | 5/7/10 | 5.0 |
| 7–9 | 3 | 1–3 | 5/7/10 | 5.0 |
| 10–12 | 4 | 4 | 0x14 (20) | 5.0 |
| 13–14 | 4 | 5 | 0x19 (25) | 5.0 |
| 15 | 4 | 6 | 0x1E (30) | 5.0 |
| 16–18 | 5 | 4/4/4 | 20 | 6.0 |
| 19–20 | 5 | 5 | 25 | 6.0 / 8.0 |
| 21 | 5 | 6 | 30 | 8.0 |
| 22–24 | 6 | 7 | 0x32 (50) | 7.0/7.0/10.0 |
| 25–26 | 6 | 8 | 0x46 (70) | 7.0/12.0 |
| 27 | 6 | 9 | 0x64 (100) | 7.0 |
| 28–34 | 7 | 4 | 0x1E (30) | 5.0…8.0 |

(`BYTE2` of the float — used by the comparison chart as a vote-weight byte — is the
exponent byte of the IEEE float, which is how the binary derives "office influence" as a
small integer 0x40/0x40A0→≈64 etc.) The German office names appear as strings:
`Buergermeister` (mayor, `@0x61a9b4`, with upgrade/`Rathaus`/`Kerker` variants),
`RICHTER` (judge, `@0x53621c`), `ratsherr2` (councilman, `@0x527298`); the upgrade verb
`upgr_rathaus` (`@0x61a5a4`).

### B.2 Council sessions — `VIBE_Office_RunCouncilSession @0x49dd8c`

The town-council meeting (a large blocking cutscene-driven routine, ~56 KB pseudocode).
It loads one of **six chamber meshes/flights** keyed by an index (`byte_49D444`):

| idx | mesh (`.ed3`) | flight (`.esc`) | chamber |
|---|---|---|---|
| 0 | `Sitzung_Wohnsitz.ed3` | `sitzungen_flug_wohnsitz.esc` | residence/town |
| 1 | `Sitzung_Kirche.ed3` | `sitzungen_flug_kirche.esc` | church |
| 2 | (rat) | `sitzungen_flug_rat.esc` | council |
| 3 | `Sitzung_Diebe.ed3` | `sitzungen_flug_diebe.esc` | thieves' guild |
| 4 | `Sitzung_Wache.ed3` | `sitzungen_flug_wache.esc` | city watch |
| 5 | `Sitzung_zunft.ed3` | `sitzungen_flug_zunft.esc` | (craft) guild |

It plays voice banks `AMTSWAHL.sbf` (office **election**) and `ABSETZUNG.sbf`
(office **deposition/recall**) via `VIBE_Voice_LoadLanguageBank @0x582074`, ambient
`Athmos\Athmo_sitzung.mp3`, and drives cutscene flights
`sitzungen_create.esc` → `…_create_2.esc` → per-action → `…_exit.esc`
(`cutscenes\sitzungen`). Two on-screen overlays: `sitzung_left_wahl` (election ballot)
and `sitzung_top_absetzen` (recall banner). The deposition petition the player can file
is `prva_ABSETZUNG_BEANTRAGEN` (`@0x61ac48`). A live **session clock** is drawn by:

#### `VIBE_Office_RenderSessionTimer @0x49d910`

```c
VIBE_Crt_Sprintf_0(buf, "%2i : %2i : %3i ms",
  14*(dword_62EB38 - dword_6315D8) / 60000,
  14*(dword_62EB38 - dword_6315D8) / 1000 % 60,
  14*(dword_62EB38 - dword_6315D8) % 1000);
```

Session elapsed time = `(currentTick − sessionStartTick) × 14` ms (the `×14` is the
game's tick→ms factor, see [15 — Game time](15-game-time-tick.md)), shown as
`mm : ss : mmm`.

#### Successor election dialogs — `VIBE_Office_BuildSuccessorDialogA/B @0x4a003c / 0x4a01a4`

`BuildSuccessorDialogA(candidate1, candidate2, outResult, flags)` opens the
`cutscenes\sitzungen` form, renders rich-text `3848` ("who succeeds…", with the two
candidate names `*a1`/`*a2`), creates a centered 300-wide auto-advancing slider/progress
bar (`VIBE_Widget_AddSliderToWindow(… ,300,100,100,66,…)` +
`VIBE_Cutscene_UpdateProgressBar(…,1500,1)` = 1.5 s), and lets the AI pre-pick a winner
via `VIBE_AiPlayer_EvaluateApproachDirection @0x47d6a0`. (Variant **B** `@0x4a01a4` is the
two-candidate counterpart used in the other branch.)

### B.3 Court trials — `VIBE_Office_RunCourtTrial @0x4a0eb8`

The full courtroom sequence (a very large routine, ~63 KB), loading `Gericht.ed3`
(`@0x61c608`) and ambient via `VIBE_Audio_PlayAmbientTrack @0x43a984`. It runs the
fixed cutscene script chain:
`prozess-create2` → `prozess-richter` (judge) → `prozess-beisitzer1/2` (assessors) →
`prozess-intro` → `prozess-richter-reden` → `prozess-klaeger` (plaintiff) →
`prozess-angeklagter` (defendant). Voice banks: `PROZESS_2_VORWURF_KOMMENTARE.sbf`
(charge commentary), `PROZESS_3_SCHULDIG.sbf` / `PROZESS_3_NICHT_SCHULDIG.sbf`
(guilty / not-guilty verdict). It collects evidence with
`VIBE_Beweis_CollectByOwner @0x4c32ec`, looks up parties with
`VIBE_Person_FindRecordById @0x58bc6c`, and reads the active law record via
`VIBE_Gesetz_GetRecord @0x4c244c`.

#### Torture / sentencing branch — `VIBE_Office_BuildTortureChoiceForm @0x4a3dc8`

A `switch(*(BYTE*)stage)` with **7 stages (0–6)** driving sentencing. Each stage builds a
`cutscenes\prozess` or `cutscenes\folterwahl` form, a 1.5 s progress slider, and writes
the chosen outcome to `*(a1+148)`:

| Stage | Form / rich-text | Choice semantics |
|---|---|---|
| 0 | `prozess` rich `4321` | accuser==defendant self-case; result 0/1 by button `1155`/`1210` |
| 1 | `prozess` rich `4336` | binary guilt vote (`1155`→0, `1210`→1) |
| 2 | `folterwahl` rich `4352/4353` | **torture method** menu of 7 (`InitAndShuffleDwordArray(7)`), cost = `(wealthA+wealthB)/2000 × methodByte`; default pick is randomized `VIBE_Cutscene_RandInt(3,…)` |
| 3 | `folterwahl` rich `4360` | confess-under-torture; on `1210` issues `VIBE_Command_QueueRequestCoord27(…,35)` (apply torture, action 35) |
| 4 | `prozess` rich `4430/4431/4432` | three-way ruling |
| 5 | `prozess` rich `4439/4440` | **sentence** menu; option enabled/disabled per `VIBE_He_ValidatePunishmentType @0x4c45e4`; wealth tier from `VIBE_AiMethod_ClassifyWealthTier @0x467d60` |
| 6 | `prozess` rich `4407` | binary confirm |

`VIBE_Person_ComputeTotalWealth @0x591f7c` supplies the gold figures used for torture
cost and sentence scaling.

### B.4 Privileges — `VIBE_Panel_ShowPrivileges @0x55fe60`

The privilege browser. It first checks `byte_12CEAC1[536*selfId]`: if the player is
*disqualified* it shows a static info form via `VIBE_Privilege_ShowDialog`. Otherwise it
opens `panel\privilegien`, and for each of **46 privilege entries** (loop `i < 46`)
queries the privilege's state with `VIBE_DebugCmd_DispatchByType @0x5711ec`, which
dispatches through the **46-entry function-pointer table** `funcs_57120D @0x63d8ac`
(verified: 46 pointers `0x56efb0 … 0x571134`). Each privilege handler returns a status
code; the panel filters by the current page mode `v46` (**2** = "granted/available",
**10** = "petitionable"), and toggling the mode button swaps `2↔10`. Granted items show
rich-text `6364`, the mode headers are `6365`/`6366`, and items render with the format
`"%ia[%s]"`. The "request privilege" action (status `9` in mode 2) sets the object
visible/enabled and lets the player petition. `ShowDialog` itself
(`VIBE_Privilege_ShowDialog @0x571218`) picks one of three form sizes:
`privillegien\prv_small` (arg 0), `…prv_big` (arg 1), `…prv_very_big` (arg 2).

### B.5 Guild-state sync — `VIBE_Amt_RefreshGuildState @0x4becdc`

A thin command-pump wrapper run after office changes:

```c
VIBE_Command_FlushSendQueue();      // 0x4934cc
VIBE_Command_ReceiveAndQueue(...);  // 0x493ebc
return VIBE_Command_ExecCommands(); // 0x494088
```

It flushes pending office/election commands, drains the inbound queue, and executes them,
keeping every client's office registry consistent (see
[19 — Commands](19-commands-netcode.md)). The error strings
`amt_GetDesireByName()`, `amt_ExecGebaeudeBauen()`, `amt_calc_zuenfte()` mark the broader
`amt_*` (office) module these tie into.

---

## Part C — Statistics

### C.1 City statistics — `VIBE_StatPanel_ShowCityStatistics @0x55f4ac`

Gated on interaction-handler flag bit `16`. Opens `panel\statistik_stadt` (title `6889`),
loads the 10-float demand snapshot via `VIBE_Economy_LoadDemandSnapshot @0x57a5dc`, and
renders five labeled metrics, each `value = snapshot[k] × scale`, converted to screen
space with `VIBE_Coord_ConvertX @0x5c6b08`:

| Slot | Source | Scale const | Text id | Meaning |
|---|---|---|---|---|
| `[6]` | price index `flt_641DA8` | — | 6895 | overall price level |
| `[9]` | `(snap[9]+1.0)` | `dbl_624AD4` | 6896 (+6897, ≤4 tiers) | satisfaction tier |
| `[2]` | `snap[2]` | `flt_624ADC` | 6902 (+6903) | demand tier |
| `[1]` | `snap[1]` (clamp ≥0) | `flt_624AE0` | 6908 | supply |
| `[3]` | `snap[3]` (clamp ≥0) | `dbl_624AE4` | 6909 | growth |

It then draws a multi-series **chart** (`VIBE_StatPanel_RenderChart @0x55ee70`) over the
10 floats, with **5 toggleable series** stored as a bitmask in `word_1233500` (default 1).
Each series gets a checkbox (`VIBE_Object_AddToWindow`, text id `6912+n`) and a 3-color
legend line drawn with `VIBE_Paintbox_DrawLine @0x41ec40` (RGB from the palette block
`dword_5526F4`). Toggling a checkbox rebuilds `word_1233500` and re-renders the chart.

### C.2 Player/official comparison — `VIBE_StatPanel_ShowCompareChart @0x55fa88`

Gated on handler flag bit `4`. First builds the comparison dataset via
`VIBE_StatChart_BuildOfficialComparison @0x58beb8` (below); if there are officials it opens
`panel\pcompare4plus6` (≤6 entries) or `panel\pcompare8` (>6), title `0x1B10`. For each
official it draws: a name label (`%1N3`, role from `word_12CE910[268*roleId]`), a
**reputation slider** 0–300 colored by an office-class index
(`dword_12CE964[134*roleId] − 1342`, clamped 0–7, into palette `dword_552704`), and a
wealth value label. Bars are 58 px wide (>4 officials) or 80 px (≤4).

#### `VIBE_StatChart_BuildOfficialComparison @0x58beb8` — the metric model

Scans up to **768 persons** (`byte_12CE912`, 536 stride) selecting those whose role byte
is **5, 6, or 7** (the office-holder classes), up to 8 officials (`v2 < 72`, 9 floats
each). For each official it computes a weighted "score":

| Component | Source | Weight const |
|---|---|---|
| skill sum (5 bytes) | `byte_12CE990[536*id .. +5]` | `flt_6267DC ≈ 4e-4` |
| reputation byte | `byte_12CE91D[536*id]` | `flt_6267E0 ≈ 0.167` |
| office influence (two `GetDefinition` exponent bytes summed) | `byte_12CEA76`,`byte_12CEA79` | `flt_6267E4 ≈ 0.0769` |
| avg favorability vs all other officials (classes 4–7) | `VIBE_Ai_ComputePersonFavorability @0x594330` | `flt_6267E8 ≈ 0.01` |
| wealth | `VIBE_Person_ComputeTotalWealth @0x591f7c` | normalized by max, ×`flt_6267CC ≈ 1e-4` |

Final bar value `score = wealthNorm·flt_6267D0(0.3) + rep·flt_6267D4(0.15) +
influence·0.15 + favor·0.15 + skill·flt_6267D8(0.25)`, then scaled by the global
`maxWealth·1e-4`. (Float consts at `0x6267cc`: `1e-4, 0.3, 0.15, 0.25, 4e-4, 0.167,
0.0769, 0.01`.) The formatted row goes through `byte_626774`.

### C.3 Stats summary

| Stat | Tracked where | Surfaced by |
|---|---|---|
| Price level / demand / supply / growth / satisfaction | `dword_1234910` (10 floats) + `flt_641DA8` | `ShowCityStatistics` |
| Per-official skill, reputation, influence, favor, wealth | person records (536 stride) + office defs | `ShowCompareChart` / `BuildOfficialComparison` |
| Office holders & successors | `byte_B59830`/`dword_B59834`/`dword_B59844` (37 entries) | council sessions, save via `amt_fio_*` |
| Cached ware prices | prototype `+56` | `ComputeMarketPrice`, trade panels |

---

## Cross-references

* [14 — Per-frame loop](14-per-frame-loop.md) — every panel here is a modal `RunFrameLoop` sub-loop.
* [15 — Game time](15-game-time-tick.md) — the `×14` tick→ms factor in the session timer.
* [18 — NPC events / history](18-npc-events-history.md) — trials and depositions feed the event log.
* [19 — Commands / netcode](19-commands-netcode.md) — craft, trade, torture and office changes are issued as commands and synced by `VIBE_Amt_RefreshGuildState`.
* [20 — Buildings & city](20-buildings-city.md) — the 65-byte prototype records priced here.
* [22 — Script engine](22-script-engine.md) — the `.esc` cutscene flights driving sessions and trials.
