# Harden pass — action_dialog.cpp / action_target_pickn.cpp / bard_dialog.cpp

Scope: every function carrying a `gilde.exe 0xADDR` / `@0xADDR` provenance comment in the
three target files, decompiled at its address and diffed line-for-line against the C++.
MCP module `gilde.exe`, imagebase 0x400000.

Result: **8/8 functions VERIFIED-1:1, 0 fixes required.** All cost-rate float constants,
preflight orderings, switch arms, seal-stamp ids, verdict arithmetic, command kinds and
float->int truncation sites match the binary. Build green, all relevant suites pass.

---

## action_dialog.cpp

### 0x547264 — VIBE_ActionDialog_Sabotage  → ActionDialog_BuildSabotage / Dispatch — VERIFIED-1:1
- Cost: `v44 = wealthSelf * flt_62411C; v6 = value2 * flt_624120 + v44; ConvertX; v45=(int)v6`.
  - flt_62411C `0a d7 a3 3c` = 0x3CA3D70A = 0.02f  (kRateSabotageSelf) ✓
  - flt_624120 `cd cc 4c 3d` = 0x3D4CCCCD = 0.05f  (kRateSabotageTarget) ✓
  - Second term in binary is `BuildingValue_ComputeRoomWorth(...,100,...)`, modeled as the
    `wealthTarget` input — faithful to `term2 = value2*rateB`.
- Float->int: ConvertX @0x5c6b08 sets FPU RC=truncate (ctrlword 0x1F) + `frndint` (truncate
  toward zero) then `fistp` → net truncation. C++ `(int)v` truncates toward zero. ✓
- Debug-27 halve: disasm 0x547353 `mov edx,eax; sar edx,1Fh; sub eax,edx; sar eax,1`
  = signed round-toward-zero `/2`; C++ `cost/=2` identical. Always-evaluated check modeled
  by `halveCost` flag. ✓
- Preflight order (binary): CheckResourceAmount → handler-filter(25)/busy(4885) →
  pairedReverse(4886) → pairedForward(4887) → IsAnimalTargetBusy(4888).
  C++ order: hasResources → busy → paired (incl. targetFree). ✓
- form `misc\perga_rolle`, slot 1, body RenderRichString id 4900-frame + body 4883, kind 25,
  label "sabotage", seals v42=2/v43=23 → {2,23}. ✓
- Dispatch: OK(1210)+confirm-object → StartAction; 1155/right → cancel. ✓

### 0x547714 — VIBE_ActionDialog_Spy  → ActionDialog_BuildSpy / Dispatch — VERIFIED-1:1
- Order (binary): handler-filter(24)/already-spying(4880 + optional RearmSpy) →
  `else if (count>=5)` 4879 → `else` cost+CheckResource. C++: busy → runningSpies>=5 →
  cost+hasResources. ✓ (kActKindSpy=24 = filter arg.)
- Cost: both terms × flt_624144 `0a d7 a3 3b` = 0x3BA3D70A = 0.005f. ConvertX + (int). ✓
- form `special\spionage`, slot 2, body 4877, kind 24, label "spionage", no seals. ✓
- Golden encodes truncation: 4000*0.005f + 6000*0.005f = 49.99999888 → 49 (not 50). ✓

### 0x547a60 — VIBE_ActionDialog_BeatUp  → ActionDialog_BuildBeatUp / Dispatch — VERIFIED-1:1
- Cost: `v43 = wealthSelf * flt_624150` only (rateB=0). flt_624150 `96 43 0b 3c` =
  0x3C0B4396 = 0.0085f. ConvertX + (int) + debug-27 halve. ✓
- Order (binary): cost → handler-filter(26)/busy(4893) → CheckResource → pairedReverse(4894)
  → pairedForward(4895). No IsAnimalTargetBusy (BeatUp has no targetFree gate).
  C++: busy → hasResources → paired (no targetFree). ✓
- form `misc\perga_rolle`, slot 1, body 4891, kind 26, label "beatup", seals v40=3/v41=20 →
  {3,20}. ✓

### 0x54891c — VIBE_ActionDialog_ConfirmAbduct  → ActionDialog_BuildConfirmAbduct / Dispatch — VERIFIED-1:1
- Gates (binary): target valid / not self / CheckSkillRequirement(2) →
  `CountMatchingEntities; if (count<=0)` 4969 → CheckSkillRequirement(2) again. C++ models
  the observable gate as `abductableCount<=0 → kBusyTarget`; skill checks gated upstream.
- Body RenderRichString **0x136C = 4972** = kTextAbductConfirm. ✓
- Two child objects: confirm + cancel (`GetChildObjectId(...,1,id+1)`). C++ confirmObj +
  cancelObj. ✓
- Seals v26=4/v27=18 → {4,18}; slot-reset kind v19=46. ✓
- Confirm path: `RequestBuildOp90(*(v5+1), -2)` + random roll → 4974/4975.
  C++ Dispatch: confirm-object → Abduct(self,target,kind,-2); cancel-object → end. ✓

### 0x548f30 — VIBE_ActionDialog_ConfirmFreePrisoner  → ActionDialog_BuildConfirmFreePrisoner / Dispatch — VERIFIED-1:1
- Gates (binary): `!FindActionByActor` 5406 (not held) → `v23>3` 5405 (too late) →
  CheckSkillRequirement(1). C++: !freeHeldByUs → kNotHeld; freeState>3 → kTooLate. ✓
- Body RenderRichString **0x151F = 5407** = kTextFreeBody. ✓
- Seals v21=4/v22=3 → {4,3}; loop switch case 1210 → Person_QueryBegin(1,5,10), kind 53,
  RandomModulo(0x1D) delay, slot-reset; case 1155 → cancel. C++ Dispatch: OK(1210) →
  FreePrisoner(target,kind,0[delay owned by sim]); 1155 → cancel. ✓

---

## action_target_pickn.cpp

### 0x548c0c — VIBE_ActionDialog_BeginAbductTargetPick — VERIFIED-1:1
- `Light_SetGrayColorThunk(0, 40, v3)`; `v3[2]=this` (payload); `v3[1]=1024` (flag);
  `v4=6` (kind); `Amt_RunOfficeOverviewWindow(v3, dword_8C845C, ConfirmAbduct)`.
- C++: grayLevel 40, payload=self, flag 1024 (kTargetPickFlag), kind 6 (kTargetPickKind),
  table=g_abductOfficeTable (dword_8C845C), callback=confirmAbductCallback (0x54891c). ✓

### 0x5488ac — VIBE_ActionDialog_PromptTargetSelect — VERIFIED-1:1
- `Light_SetGrayColorThunk(0, 40, v6)`; `v8=0` (payload); `v9=6` (kind); `v7=1024` (flag);
  `RenderFormattedMessage(v5, 4962)`; `Amt_RunOfficeOverviewWindow(v6, v5, v4)`;
  `Hud_UpdateEdgeScroll(0, a3, a4)`.
- C++: payload 0, kind 6, flag 1024, textId 4962 (kPromptTextId), edge-scroll(0,a3,a4). The
  binary's 3rd RunOfficeOverviewWindow arg (v4) is uninitialized ecx garbage; C++ passes
  nullptr — acceptable hook abstraction. ✓
- (Provenance addresses in the file were confirmed accurate; no other `@0x` markers present.)

`ActionDialog_BuildTargetPickConfig` is a pure GUI-owned helper (config-record layout),
exercised by goldens; consistent with both originals' record packing.

---

## bard_dialog.cpp

### 0x5495a8 — VIBE_BardDialog_PerformPoem  → BardDialog_Build / Dispatch — VERIFIED-1:1
- Gate order (binary): `!v5 || *(v5+112)!=2` → RichString 0x1A01=6657 (kNoScene);
  `dword_12CEAD8 & 0x20000` → 0x1A02=6658 (kRecent); `!Camera_ComputeZoomScale` →
  0x1A03=6659 (kNoZoom); else announce 0x1A05=6661.
  C++: sceneValid/activeScene!=2 → kNoScene; lawFlagRecited → kRecent; zoomScale==0 →
  kNoZoom; else kOk + announce. ✓
- lawByte = `*(_WORD*)(handler+172)` (= word offset 86). ✓
- Verdict (phase 2): `*(BYTE)RecordPtr`: ==1 → 0x1A38=6712 arg `verseId+446`;
  else(other) → 0x1A3A=6714 no arg; ==0 → 0x1A39=6713 arg `verseId+4810`.
  verseId = `*(WORD)(RecordPtr+1)`.
  C++: verdict 1 → 6712 (verseId+446); 0 → 6713 (verseId+4810); else → 6714 (0). ✓
- Dispatch: loop 1210 → EnqueueLawAction(self, lawByte) + recite; 1155 → cancel; gate!=kOk
  ends loop with no recite. C++ Dispatch identical. ✓
- mp3 templates `Gedichte_Announcement\%s.mp3` / `gedichte\%s.mp3`, scroll-button id 1753,
  phase-2 window flag byte 48 — all present in header constants and consistent. (Audio /
  frame-loop / text-engine remain BOUNDARY hooks: MSS32->SDL audio, Win32 loop.)

---

## Boundaries (legitimate, unchanged)
Frame loop (VIBE_GameLogic_RunFrameLoop), text engine (RenderRichString/RenderFormattedMessage),
audio (Audio_SetGlobalVolume / mp3 playback — MSS32->SDL), and the command codec are
forward-declared / routed through mockable sinks + hooks, as designed. No data-not-in-tree
gaps; no fake analogues.

## Tests
Built: gui_dialogs2_test, gui_contact_loops_test, gui_p6_widget_test, wire_apply_input_test
(one pre-existing truncated object `src/sim/name_tables.cpp.o` forced a recompile via touch;
not related to these files). Ran with GUILD_GAME_DIR set:
- gui_contact_loops_test  PASS
- gui_dialogs2_test       PASS  (cost 120/49/85, seals {2,23}/{3,20}, verdict +446/+4810)
- gui_p6_widget_test      PASS
- wire_apply_input_test   PASS
4/4 suites green. No source or golden edits were necessary.

## Count summary
- Functions audited: 8 (5 action_dialog, 2 action_target_pickn, 1 bard_dialog).
- VERIFIED-1:1: 8
- FIXED: 0
- BOUNDARY (subsystem hooks within otherwise-verified fns): frame loop / text / audio / cmd codec.
- Float constants byte-confirmed: 4/4 (0.02, 0.05, 0.005, 0.0085).
