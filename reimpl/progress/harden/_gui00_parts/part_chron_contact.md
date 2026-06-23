# Harden pass — chronicle_window / contact_actions / contact_loops / contact_menu

1:1 line-for-line diff of every provenance-tagged function against its IDA decompile.
MCP module `gilde.exe`, imagebase 0x400000.

Files in scope:
- src/gui/chronicle_window.cpp
- src/gui/contact_actions.cpp
- src/gui/contact_loops.cpp
- src/gui/contact_menu.cpp

## Result: ALL VERIFIED-1:1 — no source or golden changes needed.

Build: `gui_remaining_test gui_chronicle_window_test gui_contact_loops_test
gui_dialogs_test gui_hud_test` all built green.
Tests run (GUILD_GAME_DIR set): 9/9 passed
  #233 gui_chronicle_window_test, #234 gui_contact_loops_test, #245 gui_dialogs_test,
  #253 gui_hud_test, #270 gui_remaining_test, #870/#871 itests, #1196/#1197 e2e.

---

## chronicle_window.cpp

### 0x4fed20  VIBE_History_DisplayCurrentEvent  — Chronicle_DispatchMode  — VERIFIED-1:1
Dispatch core: `if (!dword_764CF0 && byte_633924) { ==1 -> ShowEventScrollForward;
==2 -> ShowEventScrollReal; }`. Reimpl `Chronicle_DispatchMode(available, mode)` models
the gate + the two-arm select exactly (mode 1 forward / 2 real / else idle). The tick/queue
and `RefreshGuildState` spin (0x4fed37..0x4fed4b) are the modal boundary, out of scope.

### 0x4fe74c  VIBE_History_ShowEventScrollForward  — Chronicle_FormatPage / Chronicle_BuildScrollPlan — VERIFIED-1:1
- ScanNextEventForward gates open: `result==1` -> open scroll; else `return result` (early
  bail). Reimpl: `if (result != 1) { opened=false; return; }`. ✓
- Render plan slots: slot2 header RenderRichString(0xD13)+scroll buttons, slot1 `"$C%s"`
  body, slot3 `"$C$Z$[%s$]"` title. Format strings byte-confirmed via refs
  (aCS=0x620b14 "$C%s", aCZS_1=0x620b1c "$C$Z$[%s$]"). Reimpl ExpandSingleS over
  kChronicleBodyFmt/kChronicleTitleFmt with the single `%s` specifier the engine uses. ✓
- Page loop: `do { UpdateAnimation; if (dword_75BF38==1210) { if ScanNext==1 re-render
  slots 2/1/3 else dword_631614=1; } } while (RunFrameLoop(417927,...))`. Reimpl models the
  page-signal advance + the "no more pages -> exit flag/break" path. ✓
- The huge memset prologue blocks are stack-buffer zeroing of v21[8120]/v22[256]; modeled
  by zero-init std::string. Frame loop + Scroll_Open/Close are the boundary.

---

## contact_menu.cpp  (status-text leaves)

### 0x4bcc4c  VIBE_StatusText_ResetEntries  — VERIFIED-1:1
`for(result=0; result!=1600; result+=50){ v1=dword_11B5220[result]; if(v1)
*(v1+536)=0; dword_11B5158[result]=0; } return result*4`. Reimpl loops 32 entries,
clears object active flag (+536) when handle!=0, clears parallel selection slot, returns
`1600*4`. Stride 50 dwords, return value preserved verbatim. ✓

### 0x4bcc80  VIBE_StatusText_Register  — VERIFIED-1:1
- (1) De-dup: scan 0..1600 step 50; `dword_11B5220[i] && !StrCmp(name,&[i+1])` ->
  set +536=1, return handle. ✓
- (2) First-free scan: only enters do-while if slot0 handle set; `v6>=32` -> "Too many
  Objects:%s" -> return 0. Reimpl `slot>=kMaxStatusEntries`. ✓
- (3) Resolve handle (Character_RunMeshCallback); 0 -> "Object not found: %s" -> 0. ✓
- (4) Fill gfx(+68)/label(+72,128)/name(+4,64), clear flagA(+67)/flagB(+199),
  set +536=1, return handle. Offsets match header (kStatusOff*). ✓
String literals byte-confirmed (aRegisterstatus_0 0x61e198, aRegisterstatus 0x61e1c4).

---

## contact_actions.cpp  (production builders)

Note on dispatch order: the original interleaves Register + dispatch inside one
`while(RunFrameLoop(425983,...))` body; each trade's if/else-if click chain uses a
trade-specific order. The reimpl splits Build*/Dispatch* and uses one shared
`ContactMenu_DispatchProductionEx` with order production/transport/staff/storage/gather/
master. Because StatusText_Register dedups by name and returns DISTINCT handles per
distinct name, all registered ids within one trade are distinct, so dispatch order is
observationally identical to each trade's own chain. All id->action mappings verified
matching. The frame loop / RegisterEinkaufContact / overlay updater are the boundary.

### 0x513954  VIBE_ContactMenu_SmithProduction  — ContactMenu_BuildSmith — VERIFIED-1:1
Page 0x200: production gfx19 gated HandlerFlag(32); master ob_MEISTERBRIEF gfx22 gated
HandlerFlag(128). Page 0x400: storage contact_LAGER gfx14 gate(16); transport gfx21
gate(256); staff ob_PERSONALBUCH gfx12 gate(64). Production name
contact_PRODUKTION_SCHMIEDEN. All gfx/flags/page-split match. Actions: storage->Storage,
production->ProductionPanel, transport->Transport, staff->StaffBook, master->MasterCert. ✓

### 0x514c44  VIBE_ContactMenu_StonemasonProduction  — ContactMenu_BuildStonemason — VERIFIED-1:1
Ungated. Page 0x200: production contact_PRODUKTION_STEINMETZ gfx19. Page 0x400: master
gfx22, storage gfx14, transport gfx21, staff gfx12. ✓

### 0x518dc4  VIBE_ContactMenu_BreweryProduction  — ContactMenu_BuildBrewery — VERIFIED-1:1
Ungated. Page 0x200: production contact_PRODUKTION_BRAUEN gfx19. Page 0x400 (order
master/staff/transport/storage): master gfx22, staff gfx12, transport gfx21, storage
gfx14. ✓

### 0x5148b0  VIBE_ContactMenu_PerfumeryProduction  — ContactMenu_BuildPerfumery — VERIFIED-1:1
Page 0x200: gather contact_SAMMELN gfx22, production contact_PRODUKTION_PARFUMERIE gfx19.
Page 0x400: storage gfx14, transport gfx21, master gfx22, staff gfx12. gather ->
TradeSearchExport. ✓

### 0x514ac8  VIBE_ContactMenu_MixingProduction  — ContactMenu_BuildMixing — VERIFIED-1:1
Page 0x200: gather contact_SAMMELN gfx22, production contact_PRODUKTION_MISCHEN gfx19.
Page 0x400 (order master/staff/storage/transport): master gfx22, staff gfx12, storage
gfx14, transport gfx21. ✓

(Non-provenance helpers in this file — Flytrap/Threat/Feast/Mistress builders — read
consistent with their cluster decompiles; not in the assigned address list.)

---

## contact_loops.cpp  (section-header provenance addresses)

contact_loops carries cluster provenance as `(0xADDR)` section headers (no `@0x`/
`gilde.exe 0x` per-function tags). All nine verified against decompiles.

### 0x511af4  Hud_RunErzAbbauContactLoop  — Hud_BuildErzAbbau / Dispatch — VERIFIED-1:1
Page 0x200: search contact_SUCHEN_ERZ gfx22, mine contact_ABBAUEN gfx10. Dispatch
search->WindowA, mine->WindowB. ✓

### 0x511b74  Hud_RunProductionContactDispatch  — Hud_BuildProductionMetal / Dispatch — VERIFIED-1:1
Page 0x200: production contact_PRODUKTION_METALL gfx19, transport gfx21, storage
contact_LAGER gfx14, feast contact_GELAGE gfx22; targetA ob_STROHPUPPE gfx23 if
QueryFind(..53), targetB ob_ZIELSCHEIBE gfx23 if QueryFind(..52). Dispatch order
transport, storage, production(WindowC), feast, (targetB||targetA->training). ✓
(Original outer guard `if(v7 != dword_631720)` uses uninit v7 — Hex-Rays artifact; reimpl
`if(!clicked) return false` is the faithful model.)

### 0x513274  ContactMenu_RobberHideout  — Build/Dispatch — VERIFIED-1:1
Page 0x200 order: equip AUSRUESTEN gfx13, ambush AUF_LAUER_LEGEN gfx21, attack ANGRIFF
gfx23, [targetA STROHPUPPE 23 if 53] [targetB ZIELSCHEIBE 23 if 52], extort
SCHUTZGELD_ERPRESSEN 23, spyBuild GEBAEUDE_AUSSPIONIEREN 23, raid RAUBUEBERFALL 23, regen
REGENERATION 23. Page 0x400: storage LAGER gfx14, feast GELAGE gfx14 (NOT 22 — confirmed),
transport "TRANSPORT" gfx21. Dispatch: storage, feast, transport, equip,
(targetB||targetA||regen -> training), ambush (guarded CheckActiveCharFlag burglary),
attack (ShowBar), extort (CheckAndShow), spyBuild (Bribery), raid (RaidConfirm); trailing
`byte_67225C==19 -> RaidConfirm` regardless of click. ✓

### 0x514a34  ContactMenu_RunEmptyLoop  — ContactMenu_BuildEmpty — VERIFIED-1:1
ResetEntries then `do RunFrameLoop while(result)`; no Register calls. ✓

### 0x514d78  ContactMenu_SabotageActions  — Build/Dispatch — VERIFIED-1:1
Ungated, every frame: sabotage SABOTAGE gfx23, beatUp VERPRUEGELN gfx13, night
BEI_NACHT_UND_NEBEL gfx12, bribe BESTECHUNG gfx16. Dispatch: sabotage->Sabotage dialog,
beatUp->office overview, night->Talent(2), bribe->PromptTargetSelect. ✓

### 0x515ea4  ContactMenu_GuildMasterActions  — Build/Dispatch — VERIFIED-1:1
HandlerFlag(8): negotiate VERHANDELN gfx12, craft HANDWERKSKUNST gfx12. TutorialInactive:
proof BEWEISBUCH gfx12, spy SPIONAGE gfx23, exam MEISTERPRUEFUNG gfx12. Dispatch:
negotiate->Talent(0), craft->Talent(1), proof->EvidenceBrowse, exam->MasterExam,
spy->SpionageConfirm; else (nothing clicked) `dword_63C7C0 && byte_67225C==45 ->
ResidenceMistress`. ✓

### 0x518c50  ContactMenu_Tavern  — Build/Dispatch — VERIFIED-1:1
Every frame (after RegisterEinkaufContact): dice WUERFELSPIEL gfx22 (return discarded in
original; reimpl note correct), regulars STAMMTISCH gfx22, darkCorner ob_DUNKLE_ECKE gfx22.
Dispatch dice->CardGame, regulars->Stammtisch, darkCorner->DarkCorner; then if
dword_63C7C0: byte 18->queue favour comment, byte 23->DarkCorner. ✓

### 0x519d74  WineCellar_RunContactLoop  — Build/Dispatch — VERIFIED-1:1
Single entry wine contact_WEINSCHRANK gfx22 -> ShowBuyDialog. ✓

### 0x51f6a8  CityTreasury_RunContactLoop  — Build/Dispatch — VERIFIED-1:1
Gated `byte_12CEA76[536 * word_63CC5C]` (activeChar stride 536) -> treasury ob_STADTKASSE
gfx22 else 0; dispatch -> ShowCashDialog. ✓

---

## Counts
Provenance functions diffed: 17
  chronicle_window: 2  | contact_menu: 2  | contact_actions: 5  | contact_loops: 8 (section-header provenance)
VERIFIED-1:1: 17   FIXED: 0   BOUNDARY (genuine, frame-loop / overlay / text engine): noted inline
Source edits: 0   Golden edits: 0
Test suites green: 9/9
