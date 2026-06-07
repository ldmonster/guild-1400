# Recon 05 — World / Economy / Law / Politics / Social / Events

Cluster scope: world structure, economy/trade/production, law & politics
(crimes, evidence, trials, offices/Amt, privileges, laws), dynasty/family-tree,
relations, events, missions, scripted cutscenes. Binary `gilde.exe`
(32-bit x86, imagebase 0x400000, MSVC + Borland-style usercall). All addresses
are virtual addresses unless noted as RVA.

> Confidence legend: **(C)** confirmed by decompile, **(I)** inferred from
> signatures/strings/call patterns. Person record stride is **589 bytes**
> (base ptr `dword_13CE294`, index = person id). This cluster heavily *reads*
> `VIBE_Person_*` / `VIBE_Object_*` / `VIBE_Building_*` and *drives*
> `VIBE_Form_*` / `VIBE_Window_*` GUI.

---

## 1. System overview

### 1.1 World / Universe / time
- **Universe** = scene-slot manager. `VIBE_Universe_SwitchActiveSlot` (0x5b4a24,
  **92 callers** — central) swaps the active "slot" (city/building/world
  interior). Slots hold cameras, object states, meshes. `VIBE_Universe_ResetCurrentSlot`,
  `_DestroySlot`, `_RestoreObjectStates` (0x5b43f0, 16 callers) manage lifecycle.
- **World** = building/object/person population for the active scene.
  `VIBE_World_LoadBuildingAndObjectData` (0x5835f8), `VIBE_World_InitBuildingTypeTable`
  (0x5833b4) builds the building-type table; `VIBE_WorldIo_ReadObject` (0x5e67c8,
  ~4KB) / `_WriteObject` (0x5e5ab4) are the save/load serializers (io dependency).
- **GameTime** (anchor `VIBE_GameTime_Advance` 0x583150, **199 callers**) is the
  master clock. **(C)** Time record layout: `+0 day(int)`, `+4 hour(u16)`,
  `+6 minute(int)`, `+10 second(int)`; Advance carries sec→min→hour→day with
  60/60/24 wrap. `_Compare` (47 callers), `_DiffMinutes` (20), `_PackToRecord`
  (13), `_GetSeasonFromDay` (29). Season derived from day-of-year.

### 1.2 City model **(C, high confidence)**
City table at **`byte_13CD6A0`, stride 756 bytes/city** (multi-city; up to ~8
neighbours loaded recursively via `NachbarStadt`). Loaded from
`gamedata/cities/<name>.ini` by `VIBE_City_LoadDefinitionIni` (0x507144).
INI-derived field map (offsets within the 756-byte record):

| Off | Field | Notes |
|----:|-------|-------|
| +0   | Stadtname (UTF-16, 64B) | city name |
| +64  | KartenPosition (2×int) | world-map coord |
| +72  | MaxPlayer (byte) | |
| +73  | Glaube (byte) | dominant faith |
| +76  | HistorieStart (int) | start year (default 1400) |
| +80  | HistorieEnde (int) | end year |
| +84  | Waehrung (u16 @+42w) | currency object id |
| +96  | Land (byte) / +97 Sprache (byte) | |
| +100 | Einwohner[10]×8B | population growth pairs |
| +180 | Prunk[10]×8B | splendor / luxury levels |
| +264 | Regenwahrscheinlichkeit[4 int] | rain prob |
| +280 | Schneewahrscheinlichkeit[4 int] | snow prob |
| +296 | Zufrierenwahrscheinlichkeit (int) | freeze prob |
| +300 | Privilegien[11] | privilege flags |
| +322 | Umland[8]×18B | surrounding regions (pop/buildings) |
| +476 | Verfassungsgesetze[12] | constitution laws (active law ids) |
| +524 | Finanzgesetze[12] | finance laws |
| +556 | Strafgesetze[12] | criminal laws |
| +604 | Gildengesetze[12] | guild laws |
| +628 | Kirchengesetze[12] | church laws |
| +656 | DiebeRaeubergesetze[12] | thief/robber laws |
| +682 | Import goods[16 u16] | KONTOR import |
| +714 | Export goods[16 u16] | KONTOR export |
| +748 | KartenOffset (2×int) | |

City runtime stats are computed on grids: `VIBE_City_ComputeWealthGrid`
(0x577e74, **8×8 district grid** of building worth), `_BuildSatisfactionGrid`
(0x5788d4), `_AggregateDistrictStats` (0x578abc), crime placement on grid
(`_AddCrimeToGrid` 0x577fc4 / `_RemoveCrimeFromGrid` 0x578110 / `_PlaceRandomCrime`).
Stats are ticked & network-broadcast (`_TickStatsAndBroadcast`, `_SendSyncCommand`,
`_ApplyStatsFromAck`) — game is network/command-driven (see §4).

### 1.3 Economy / trade / production **(C)**
Supply/demand model over **28 good/profession categories** stored in interleaved
arrays around `0x1234750` (per-good u16 weight `word_1234750`, u16 cap
`word_1234754`, float accumulator `flt_1234758`, float price-delta `flt_123475C`),
plus city totals `flt_641FD4/flt_641FD8`.
- `VIBE_Economy_ComputeGoodsDemand` (0x578438) / `_ComputeGoodsSupply` (0x578634):
  iterate Persons via `VIBE_Person_QueryBegin(.., 1, 5/6, profIndex)`, read need
  byte at `person+583`, weight by profession class (cases 3 / {7,15,19,>22} /
  default) and by employment (`person+39 == 0xFFFF` → unemployed scalar).
  Tuning constants are `dbl_62563C..dbl_62567C`.
- `VIBE_Economy_ComputePriceDeltas` (0x5787d4): price drift = weight×demand/cap,
  clamped to ±1, with special-case goods (indices 4 & 16 inverted) → drives
  prices toward equilibrium each tick.
- `VIBE_Economy_TickPriceLevel`, `_ComputePopulationTrend` (0x57a008),
  `_ComputeIndustryRatio`/`_ComputeResidentialRatio`, `_ComputeLawSatisfaction`
  / `_ComputeWeightedLawScore` (laws affect citizen satisfaction →
  population/economy feedback). `_LookupRateScalar` (0x579a24, 13 callers)
  maps a good id → tax/rate scalar byte.
- **Production**: `VIBE_Production_ComputeOutputOverTime` (0x59064c) &
  `_ComputeDailyHourOutput` (0x590a3c) integrate output across game-time using
  per-weekday work windows `flt_6476FC` (start hr) / `flt_64770C` (end hr) and
  rate `flt_626A04`.
- **Trade UI** is large: `VIBE_TradeTransport_PanelDispatcher` (0x54014c, ~14KB,
  the master trade panel state machine), `VIBE_TradePanel_*` (production windows,
  slot/slider layout), `VIBE_Trade_*` (sorted item lists, slot icons).
  `VIBE_Exchange_*` (goods exchange / courier / fees dialogs, contor).
  `VIBE_TradeTransport_LoadFromStorage`/`_AssignRoute`/`_ComputeCargoValue`/
  `_ComputeCartCost` model the cart caravans. `VIBE_TradeDialog_BuyTransportCart`.
- **Money** helpers (0x58f14c+): rate multiply/divide, `_FormatWithSeparators`.

### 1.4 Law & politics — the legal system **(C)**
Three linked runtime tables, all index-by-id:

1. **Law/Gesetz table** `unk_631E98`, **26 entries × 36 bytes** (accessor
   `VIBE_Gesetz_GetRecord` 0x4c244c, **71 callers**). Decoded fields:
   `+0 id/type(byte)`, `+16 penalty(u16)`, `+20 comparison-operator(byte 1..7:
   ==, !=, <, <=, >, >=, special)`, `+24 threshold(int)`. Laws are grouped into
   6 books matching the city law arrays (Verfassung/Finanz/Straf/Gilde/Kirche/
   Diebe). `VIBE_Gesetz_EvaluateViolation` (0x4c2c5c, **14 callers**) checks a
   value against the operator/threshold, rolls vs. `_ComputeMaxWantedLevel`, and
   on violation **queues a network command** (`VIBE_Command_QueueRequestObject34`)
   building a crime record. `_RequestApply`/`_ApplyAndNotify` enact laws;
   `_SaveState`/`_LoadState` persist. Formatting: `_FormatDescription*`,
   `_BuildPenaltyText`. UI: `_OpenLawBookSection` (0x5587c0),
   `_RunPersonSelectionWindow`.

2. **Crime/Straftat table** `dword_11BC760`, **512 entries × 45 bytes**
   (`23040/45`). Fields: `+0 crime-id(int)`, `+22 wanted-counter(u16
   word_11BC77A)`, `+0x16 perpetrator person id (dword_11BC776)`,
   `+0x21 proven-state(int dword_11BC785, 1=proven)`, plus target/location
   (`dword_11BC781`, `byte_11BC77C`). Ops: `_FindFreeSlot`/`_FindIndexById`,
   `_ResolveAndClear` (0x4c354c — decrements wanted, clears linked evidence,
   notifies History), `_SetRecordState`, `_BroadcastAccusation`,
   `_ClearWantedFlagOnNpcs`, `_SyncAllToNetwork`, `_CountActiveByTarget`.

3. **Evidence/Beweis** — paired arrays `dword_11C2160` (owner) / `dword_11C2164`
   (crime-id), **2048 slots** (`4096/2`). `VIBE_Beweis_Add` (0x4c3338) writes a
   pair (logs `gs_AddBeweis()`), `_FindOrAllocSlot`, `_CollectByOwner` (used by
   trial), `_ExistsForPair`.

   `VIBE_StraftatTable_*` and `VIBE_Mission_*` share a secondary descriptor table
   `dword_63CD48` (stride 24B, count `dword_5383F0`) used for mission/crime types.

- **Court trial** `VIBE_Office_RunCourtTrial` (0x4a0eb8, ~12KB, 146 string refs):
  full cutscene-driven trial — loads `Gericht.ed3`, runs `cutscenes\prozess\
  prozess-*.esc` scenes (judge/Beisitzer/Klaeger/Delinquent), pulls the charge
  via `Gesetz_GetRecord`/`Gesetz_FormatDescription`, gathers evidence
  (`Beweis_CollectByOwner`), and decides via AI scoring
  (`VIBE_Ai_ComputePersonFavorability`, `VIBE_AiMethod_ComputeWealthScoreA/B`).
  Verdict/torture path: `VIBE_Office_BuildTortureChoiceForm` (0x4a3dc8).

### 1.5 Offices / Amt / elections **(C)**
- **Office definition table** `dword_62EC8E`: **37 entries** (`<0x25`), 12 bytes
  each (+2 base offset) → 3 dwords (category, requirement-flags, text id) read
  by `VIBE_Office_GetDefinition` (0x47f008, 18 callers).
- **Office holder table** `byte_B59848`: **30 entries × 24 bytes**. Fields:
  `+0 holder char id (byte)`, `+4 city/value (int, dword_B5984C)`, `+8 office
  type (byte, byte_B59850)`, `+12 rank-level (int, dword_B59854, <4)`, `+16 state
  (byte, byte_B59858; ==3 → vacant/electable)`. Accessors
  `_GetHolderEntryByCity` (0x47dfec), `_GetEntryByHolder`, `_AddTableEntry`
  (0x47e750, 19 callers), `_TransferHoldership`, `_SwapHolders`,
  `_ReleaseCharacterHoldings`.
- Candidacy/promotion: `_ApplyForCandidacy` (0x47e1b8), `_CanRunForOffice`,
  `_CanPromoteRank`, `_AssignToCandidate`, `_BuildPromotionList(Filtered)`,
  `_GetRankRequirements` (0x47fb30), `_IsNextRankInCategory`,
  `_CollectSuccessorCandidates` and the family-aware successor collectors
  (`_CollectFamilyHeirCandidates`, `_CollectRelativeCandidates`,
  `_CollectSpouseAndBusinessCandidates`, `_CollectGuildSuccessorCandidates`).
- **Amt** = higher-level office/guild orchestration. `VIBE_Amt_RefreshGuildState`
  (0x4becdc, **64 callers**), `_ComputeGuildAssignment` (0x47ff5c),
  `_AssignGuildMembers`, `_ElectGuildMaster`, `_CalcZuenfte` (guild/zunft calc),
  `_UpdateOffices`, `_SaveAemter`/`_LoadAemter` (io). Periodic economic passes
  driven from Amt: `_ProcessAllOfficeWages`/`_ComputeOfficeWages`,
  `_ProcessLoanRepayments`, `_RunBuildingTaxPass`, `_RunProductionPass`,
  `_RunGoodsDistributionPass` (0x57dd84), `_UpdateOfficeProsperity`,
  `_EnforceLawViolations` (0x57bf20), `_PickRandomEventBuildings`.
- **Council session** `VIBE_Office_RunCouncilSession` (0x49dd8c, ~8.8KB):
  cutscene-driven (`Sitzung_Wohnsitz.ed3`, `cutscenes\sitzungen\*.esc`,
  `AMTSWAHL.sbf`/`ABSETZUNG.sbf` voice banks). Voting options
  `_ABSETZEN_JA/_NEIN/_ENTHALTUNG` (remove-from-office: yes/no/abstain),
  `_AMTSWAHL` (election). `VIBE_Office_BuildElectionForm` (0x4a0610),
  `VIBE_Office_BuildVotePanel`/`_AddVoteMarker`, `VIBE_Amt_RunElectionCandidateWindow`.
- Tax: `VIBE_Tax_Collect{Trade,Guild,Property,Staff,Building}Income` →
  `_CollectOfficeAllTaxes` (0x57b214); `VIBE_Office_ComputeCityTaxRates`.

### 1.6 Privileges **(C)** — player "powers" purchased via office/standing
~30 `VIBE_Privilege_Panel*` actions, each a dialog that confirms then issues a
network command (`VIBE_Privilege_SendSimpleCmd`/`_SendBuildCmd`, dispatch
`VIBE_Privilege_ShowDialog` 0x571218 with **24 callers**). Categories:
political (`PanelEnactLaw`, `PanelSwapSeats`, `RemoveFromOffice`, `PanelChangeProfession`,
`PanelExpelWorker`), criminal/intrigue (`PanelBlackmail`, `PanelEmbezzlement`,
`PanelInterrogation`, `PanelCounterEspionage`, `PanelInstillFear`,
`PanelGenerateHatred`, `PanelMakePeace`, `PanelApology`), social
(`PanelCharm`, `PanelConvert`, `PanelMiracle`, `PanelMedicus`, `PanelDivorce`),
plus evidence review panels (`PanelEvidence{Review,Details}`, `_BuildEvidenceEntry`)
and office-member tables (`_BuildOfficeMemberTable`).

### 1.7 Social — dynasty, relations, history
- **Stammbaum** anchor `VIBE_Stammbaum_RunFamilyTreeWindow` (0x55ab84, ~5.2KB):
  full family-tree GUI (read-only display, walks Person parent/spouse/child links).
- **Relations**: `VIBE_Relation_LookupMatrixEntry` (0x5942fc, 9 callers).
  **(C)** N×N relation matrix `dword_123D6CD`, **192-byte row stride**, value
  packed in the high byte (`>>24`), self = 127 (neutral/max). Drives AI
  favorability, marriage, succession, voting.
- **History/Chronicle**: text-template event log. `VIBE_History_Notify*` family
  (~25 fns) records arrests, office transfers/swaps, law changes, crimes,
  plague outbreak/spread, attacks, rival events into the chronicle.
  `VIBE_History_Parse*` / `_ShowEventScroll*` parse & render dated chronicle
  text files (loaded by `_LoadChronicleText`). `VIBE_History_ParseDate` (0x4fe2d4).

### 1.8 Events, missions, scripted content
- **Events**: `VIBE_EventPanel_*` is the on-screen active-event bar (slot
  create/destroy/select/click). Concrete event: `VIBE_Event_FireRaidRun`
  (0x4ee960) — a house-fire/raid ("Brand") event running
  `specialEvents\hausbrand.esc`, spawning fire FX & sound, advancing time.
- **Missions**: `VIBE_Mission_PickRandomByType` (0x538680, LCG random
  `1103515245*x+12345`), `_FindByType`, `_SlotRegister`, `_TrackCrimeProgress`
  (links mission completion to crime records), plus dialogs
  `_RunChooseMissionDialog`, `_RunChooseHistoryDialog`, `_RunHistoryRewardDialog`,
  `_RunSpecialDialog`. Mission descriptors share `dword_63CD48` table.
- **Scripting**: `VIBE_Script_*` is a thin wrapper over an external `.esc`
  script VM (`VIBE_Script_LoadAndRunMain/WithArg`, `_CmdSleep`,
  `_CallUserFunction`, `_CmdKillLocalScripts`, `_FindByName`). Real script
  engine lives elsewhere (`VIBE_Script_FindByName` 0x4421b8, `_RunWithArgs`,
  `_LoadFromScriptDir` 0x4424e0 — outside this cluster's prefixes).
- **Cutscenes**: `VIBE_Cutscene_Duel` (0x4a53a8), `_Execution` (0x4a6b90),
  wedding (`_CheckMarriageEligible`/`_WeddingExit`), `_PlayTobyScene`. Duel/court/
  execution/wedding/session are all cutscene-scripted (`.ed3` scenes + `.esc`).
- **Tutorial**: `VIBE_Tutorial_InitChapter{1..5}Steps` build a chained,
  voice-narrated step list with highlight arrows (`_DrawHighlightArrow`).

### 1.9 Location dispatchers (player actions in the world)
`VIBE_Location_*` (~60 fns) are the per-building/per-NPC interaction menus:
thieves' guild (burglary, pickpocket, kidnap, ransom, spy, breakout, prison),
guards (arrest, raid, customs, detain, patrol), church (donation, indulgence,
baptism, confession, sermon, top-5), tavern (card game, Stammtisch, dark corner),
residence (mistress, master exam), robber camp, bribery. These glue Person/Object
state to the GUI and to crime/law/relation systems.

### 1.10 Smaller game-logic modules
- **Animal**: livestock/pets sim (`_Update`, `_SpawnDog/Cat/Sheep/Cow/Livestock`,
  `_FindHerdGrouping`, `_BuildWanderPath`, model loading).
- **Plant**: vegetation growth stages & model loading.
- **Book**: in-game book/ledger reader UI (page turn).
- **Bauplatz**: building-plot markers mapped onto the supermap.
- **MarketStall / WineCellar / Bank / CityTreasury / GuildTreasury**: small
  contact-dispatch + dialog modules.
- **Credit**: full loan system (new loan, take/release, lender list, account info).
- **Duel**: pistol/sword duel resolution (`_ResolveShot`, `_CheckFatalHit`,
  `_RollDuelOutcomeTier`, `_ReportToOffice`). *Combat-adjacent — coordinate with
  sim agent.*
- **Feast / Bard / Theatre**: social-event dialogs (invite guests, perform poem,
  theatre event menu).
- **Statistics**: economy report + general/tax windows + NPC debug dumps.

---

## 2. Key functions (address · prototype · purpose)

| Addr | Name | Purpose |
|------|------|---------|
| 0x583150 | VIBE_GameTime_Advance | master clock advance (199 callers) **(C)** |
| 0x583230 | VIBE_GameTime_Compare | compare two time records |
| 0x5b4a24 | VIBE_Universe_SwitchActiveSlot | swap active scene slot (92 callers) |
| 0x507144 | VIBE_City_LoadDefinitionIni | parse city .ini → 756B record **(C)** |
| 0x577e74 | VIBE_City_ComputeWealthGrid | 8×8 district wealth grid |
| 0x5788d4 | VIBE_City_BuildSatisfactionGrid | satisfaction grid |
| 0x577fc4 | VIBE_City_AddCrimeToGrid | place crime on district grid |
| 0x57919c | VIBE_City_TickStatsAndBroadcast | tick + net-sync city stats |
| 0x578438 | VIBE_Economy_ComputeGoodsDemand | 28-good demand from Persons **(C)** |
| 0x578634 | VIBE_Economy_ComputeGoodsSupply | 28-good supply |
| 0x5787d4 | VIBE_Economy_ComputePriceDeltas | price drift toward equilibrium **(C)** |
| 0x57a580 | VIBE_Economy_ComputeWeightedLawScore | law→satisfaction |
| 0x59064c | VIBE_Production_ComputeOutputOverTime | integrate production over time |
| 0x54014c | VIBE_TradeTransport_PanelDispatcher | master trade panel FSM (~14KB) |
| 0x53f6bc | VIBE_TradeTransport_LoadFromStorage | load cart cargo |
| 0x53f404 | VIBE_TradeTransport_AssignRoute | assign caravan route |
| 0x53ff3c | VIBE_TradeTransport_ComputeCargoValue | value cargo |
| 0x51bb4c | VIBE_Exchange_ShowGoodsExchangeDialog | contor buy/sell dialog |
| 0x57aa88 | VIBE_Tax_CollectTradeIncome | trade tax |
| 0x57b214 | VIBE_Tax_CollectOfficeAllTaxes | aggregate all office taxes |
| 0x4c244c | VIBE_Gesetz_GetRecord | law table accessor (71 callers) **(C)** |
| 0x4c2c5c | VIBE_Gesetz_EvaluateViolation | op/threshold check → queue crime **(C)** |
| 0x4c247c | VIBE_Gesetz_RequestApply | enact a law (net cmd) |
| 0x4c25e0 | VIBE_Gesetz_SaveState | persist laws |
| 0x4c2ba0 | VIBE_Gesetz_ComputeMaxWantedLevel | wanted-roll threshold |
| 0x5587c0 | VIBE_Gesetz_OpenLawBookSection | law-book UI |
| 0x4c354c | VIBE_Straftat_ResolveAndClear | resolve/clear crime + evidence **(C)** |
| 0x4c3874 | VIBE_Straftat_SetRecordState | set proven/state |
| 0x4c3728 | VIBE_Straftat_BroadcastAccusation | accuse (net) |
| 0x4c33f4 | VIBE_Straftat_SyncAllToNetwork | sync crimes |
| 0x4c3338 | VIBE_Beweis_Add | add evidence pair **(C)** |
| 0x4c32ec | VIBE_Beweis_CollectByOwner | gather evidence for trial |
| 0x47f008 | VIBE_Office_GetDefinition | office def table (37 entries) **(C)** |
| 0x47dfec | VIBE_Office_GetHolderEntryByCity | holder table lookup **(C)** |
| 0x47e1b8 | VIBE_Office_ApplyForCandidacy | run for office **(C)** |
| 0x47e870 | VIBE_Office_TransferHoldership | hand over office |
| 0x47f1cc | VIBE_Office_BuildPromotionList | rank-up candidates |
| 0x49dd8c | VIBE_Office_RunCouncilSession | council/vote cutscene (~8.8KB) |
| 0x4a0610 | VIBE_Office_BuildElectionForm | election ballot |
| 0x4a0eb8 | VIBE_Office_RunCourtTrial | full court trial (~12KB) **(C)** |
| 0x4a3dc8 | VIBE_Office_BuildTortureChoiceForm | verdict/torture |
| 0x47ff5c | VIBE_Amt_ComputeGuildAssignment | guild role assignment |
| 0x481228 | VIBE_Amt_ElectGuildMaster | elect guild master |
| 0x4813d0 | VIBE_Amt_CalcZuenfte | recompute zunft/guild state |
| 0x4becdc | VIBE_Amt_RefreshGuildState | refresh guild (64 callers) |
| 0x57bf20 | VIBE_Amt_EnforceLawViolations | scan + punish violations |
| 0x57dd84 | VIBE_Amt_RunGoodsDistributionPass | per-tick goods distribution |
| 0x483198 | VIBE_Amt_SaveAemter | persist offices (io) |
| 0x571218 | VIBE_Privilege_ShowDialog | privilege dispatch (24 callers) |
| 0x561bb4 | VIBE_Privilege_PanelEnactLaw | enact law power |
| 0x561fd0 | VIBE_Privilege_RemoveFromOffice | depose office holder |
| 0x55ab84 | VIBE_Stammbaum_RunFamilyTreeWindow | family-tree window (anchor) |
| 0x5942fc | VIBE_Relation_LookupMatrixEntry | N×N relation matrix read **(C)** |
| 0x4fd44c | VIBE_History_ParseContext | chronicle parse |
| 0x536070 | VIBE_History_NotifyRivalEvent | log rival event |
| 0x535dd8 | VIBE_History_BroadcastPlagueOutbreak | plague event |
| 0x538680 | VIBE_Mission_PickRandomByType | pick mission (LCG) **(C)** |
| 0x53872c | VIBE_Mission_SlotRegister | register active mission |
| 0x539054 | VIBE_Mission_TrackCrimeProgress | mission↔crime link |
| 0x4ee960 | VIBE_Event_FireRaidRun | house-fire/raid event |
| 0x4c5460 | VIBE_EventPanel_CreateSlot | event bar slot (17 callers) |
| 0x4a53a8 | VIBE_Cutscene_Duel | duel cutscene |
| 0x4a6b90 | VIBE_Cutscene_Execution | execution cutscene |
| 0x4a7118 | VIBE_Cutscene_CheckMarriageEligible | marriage gate |
| 0x43c690 | VIBE_Script_LoadAndRunMain | run .esc script |
| 0x51a244 | VIBE_Credit_ShowTakeLoanDialog | loan UI |
| 0x4a4b68 | VIBE_Duel_ResolveShot | duel shot resolution |
| 0x549a54 | VIBE_FeastDialog_InviteGuests | feast invite |
| 0x536a30 | VIBE_Theatre_RunEventMenu | theatre menu |
| 0x48364c | VIBE_Animal_Update | animal sim tick |
| 0x579ad0 | VIBE_Statistics_BuildEconomyReport | economy report |

---

## 3. Data structures (summary)

- **City record** — `byte_13CD6A0`, 756B stride. Full layout in §1.2. **(C)**
- **GameTime record** — 14B: day(i32)/hour(u16)/min(i32)/sec(i32). **(C)**
- **Law (Gesetz) record** — `unk_631E98`, 36B × 26. id/penalty/operator/
  threshold (+0/+16/+20/+24). 6 law books (12-id arrays per city). **(C)**
- **Crime (Straftat) record** — `dword_11BC760`, 45B × 512. id/wanted/perp/
  state/target. **(C)**
- **Evidence (Beweis)** — parallel `dword_11C2160`/`dword_11C2164`, 2048 pairs
  (owner, crime-id). **(C)**
- **Office definition** — `dword_62EC8E`, 12B × 37 (category, req-flags, text).
  **(C)**
- **Office holder** — `byte_B59848`, 24B × 30 (holder/city/type/rank/state).
  **(C)**
- **Relation matrix** — `dword_123D6CD`, 192B row stride, hi-byte value, self=127.
  **(C)**
- **Mission/StraftatType descriptor** — `dword_63CD48`, 24B stride, count
  `dword_5383F0`; type byte at +2>>24. **(I)**
- **Economy good arrays** — interleaved at `0x1234750`: weight u16 / cap u16 /
  demand float / price-delta float, 28 entries. City totals `flt_641FD4/8`. **(C)**
- **Production work-windows** — `flt_6476FC[4]` start hr, `flt_64770C[4]` end hr,
  rate `flt_626A04`. **(C)**
- **Person record** — stride 589B (owned by sim agent); fields used here:
  `+39` employment/owner u16 (0xFFFF=none), `+583` need byte, `+89/+97`
  building/room links.

External data: `gamedata/cities/*.ini` (cities), `cutscenes\**\*.esc` scripts,
`*.ed3` scenes, `*.sbf` voice banks, chronicle text files, `*.mp3` ambience.

---

## 4. Dependencies

- **sim (Person/Object/Building)** — heavy *read* dependency. Economy/demand,
  office succession, crime perpetrators, relations, trials all iterate
  `VIBE_Person_QueryBegin/IterNext`, `VIBE_Person_FindRecordById` (0x58bc6c),
  `VIBE_GameObject_ResolveEntityById`, `VIBE_Building_FindNearestSameType`,
  `VIBE_BuildingValue_ComputeRoomWorth`. AI scoring (`VIBE_Ai_*`,
  `VIBE_AiMethod_*`) used by trial/council. Person stride 589 must be agreed
  with sim agent.
- **Command / network layer** — all *mutations* (enact law, accuse, build,
  privileges, office transfer, city stats) go through
  `VIBE_Command_QueueRequest*` / `VIBE_Command_EnqueueBuildingAction*` and are
  acked (`_ApplyStatsFromAck`, `VIBE_Net_RunWaitLoop`). The game is
  lockstep/command-driven even single-player. This is a cross-cutting dependency
  for the whole cluster.
- **gui** — `VIBE_Form_*`, `VIBE_Window_*`, `VIBE_Text_Render*`,
  `VIBE_EventPanel_*`. Most Office/Amt/Privilege/Trade/Location/Credit/Guild
  functions are dialogs.
- **io** — `GetPrivateProfileStringA` (city ini), `VIBE_WorldIo_*`,
  `VIBE_Gesetz_Save/LoadState`, `VIBE_Amt_Save/LoadAemter`,
  `VIBE_History_LoadChronicleText`.
- **cutscene/script/audio** — council, trial, duel, execution, wedding, events
  depend on the `.esc` script VM (`VIBE_Script_*` outside cluster),
  `VIBE_Cutscene_LoadScene`, `VIBE_Voice_*`, `VIBE_Music_*`, `VIBE_Sound*`.

---

## 5. Effort & risk per module

| Module group | Effort | Risk / notes |
|--------------|:------:|--------------|
| GameTime | S | self-contained, fully decoded **(C)** |
| Universe/World scene mgmt | M | ties into rendering & io; slot lifecycle |
| City record + grids | M | 756B layout decoded; grid math repetitive |
| Economy (supply/demand/price) | M | tuning constants must be copied exactly (FP); 28-good indexing |
| Production | M | game-time integration, FP edge cases |
| Trade / TradeTransport / Exchange | **L** | huge dispatchers (PanelDispatcher ~14KB), GUI-bound, drag/slot UI |
| Gesetz/Law | S–M | table-driven, decoded; net-command coupling |
| Straftat/Beweis | M | parallel tables + History/net side-effects |
| Office/Amt | **L** | data-table-heavy; council (8.8KB) & trial (12KB) are cutscene state machines; succession AI; **HIGH RISK** |
| Privilege | M–L | ~30 near-duplicate dialog panels; net commands |
| Stammbaum/Relation | M | relation matrix simple; family-tree window large GUI |
| History/Chronicle | M | text-template parser (custom format) |
| Events/Mission | M | descriptor tables + LCG RNG; per-event scripted |
| Cutscene | M–L | script/scene/voice orchestration; engine dependency |
| Location dispatchers | **L** | ~60 menu fns gluing many systems |
| Duel | M | **combat-adjacent — owned-boundary with sim agent** |
| Credit/Bank/Treasury | S–M | mostly dialogs |
| Tutorial | M | 5 hard-coded chapter step lists (large but mechanical) |
| Animal/Plant/Book/Bauplatz/Feast/Bard/Theatre/Statistics | S each | small, isolated |

**Top risks**: (1) Office/Amt election+trial state machines are the largest and
most entangled (AI + cutscene + net). (2) The command/network indirection — must
decide whether to faithfully reproduce the queue/ack model or short-circuit it.
(3) Exact FP constants in economy/duel RNG. (4) Person 589B layout coordination.
(5) Many tables are sized by magic constants (26 laws, 30 offices, 512 crimes,
2048 evidence, 28 goods, 756B city) — keep as named constants.

---

## 6. Proposed C++ layout & implementation order

Namespace `guild::world` with submodules. Suggested files:

```
world/time.{h,cpp}          // GameTime  (start here)
world/universe.{h,cpp}      // scene slots
world/city.{h,cpp}          // CityRecord (756B), ini loader, grids
world/economy.{h,cpp}       // supply/demand/price, 28 goods
world/production.{h,cpp}    // output-over-time
world/trade.{h,cpp}         // caravans, exchange, contor
law/gesetz.{h,cpp}          // LawRecord, evaluate, enact, books
law/straftat.{h,cpp}        // crime table
law/beweis.{h,cpp}          // evidence pairs
politics/office.{h,cpp}     // OfficeDef + holder tables, candidacy
politics/amt.{h,cpp}        // guild/zunft orchestration, taxes, passes
politics/council.{h,cpp}    // session + voting + election form
politics/trial.{h,cpp}      // court trial
politics/privilege.{h,cpp}  // privilege panels
social/relation.{h,cpp}     // relation matrix
social/stammbaum.{h,cpp}    // family tree
social/history.{h,cpp}      // chronicle log + parser
content/event.{h,cpp}       // event panel + scripted events
content/mission.{h,cpp}     // mission descriptors + dialogs
content/cutscene.{h,cpp}    // duel/execution/wedding/session glue
content/tutorial.{h,cpp}
location/*.cpp              // per-building interaction menus
misc/{animal,plant,book,credit,feast,bard,theatre,statistics}.{h,cpp}
```

**Implementation order** (low-risk foundation → high-risk):
1. `time` (anchor, no deps).
2. Core records & tables as plain structs: `city`, `office` (defs+holder),
   `gesetz`, `straftat`, `beweis`, `relation`. Get the data layouts byte-exact.
3. `economy` + `production` + `tax` (pure logic, verifiable against constants).
4. `amt` periodic passes (taxes/wages/production/goods/law-enforcement) — the
   simulation heartbeat.
5. `gesetz` evaluate/enact + `straftat`/`beweis` flow (net-command stubs first).
6. `office` candidacy/promotion/succession, then `council`/`trial` (largest).
7. `privilege` panels, `history`, `social/stammbaum`.
8. `event`/`mission`/`cutscene`/`location` content glue.
9. `tutorial` + misc modules last.

Stub the command/network layer and GUI behind interfaces early so the logic
modules can be implemented and unit-tested without the full engine.
