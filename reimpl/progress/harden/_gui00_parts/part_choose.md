# Harden pass — gui choose* screens (1:1 diff vs gilde.exe)

Scope: every function carrying a `gilde.exe 0xADDR` provenance in
`src/gui/{choosecharacter_intro_run,choosecharacter_run,choosecity_run,choosehistory_run,chooseplayer_run}.cpp`.
Each was decompiled + disassembled and diffed against the abstracted (hook-based) reconstruction.
These reconstructions deliberately model the *observable control flow + return semantics* and
push struct/global manipulation and the form/input/scene leaves through installable hooks
(the established pattern documented in each header's "HOST BOUNDARIES" note). Verification is
therefore at the control-flow / branch-condition / return-value level.

Test status: all 17 choose* suites green (9 unit/e2e in the first batch + 8 itest/e2e).

---

## choosecharacter_intro_run.cpp

### 0x52e4e0 — VIBE_Menu_ChooseCharacterIntroVariant — **FIXED**
Diffed against disasm 0x52e4e0..0x52e6d3.
- Build: 6 child-object ids (base+0..5), `RadioGroup_Create(6, id0)`, `Selection_Update(group,(u8)byte_12335BA)`. VERIFIED.
- Window-close edge `dword_672230 || byte_67225C==1` -> close, ret stays 0. VERIFIED.
- **BUG (fixed):** the OK/Enter commit was gated on a matched radio member (`if (variant>=0)`).
  The disasm proves the commit is **unconditional**: the final compare
  `0x52e6c7 cmp esi,id4 ; jnz loc_52E64C` jumps to the commit point loc_52E64C even when NO
  member matched. loc_52E64C sets `eax=1`, `dword_631614=1`, `ecx=eax` (return = ecx). Only the
  radio VALUE (`dword_63C744`) is gated on a match; when nothing matches it retains the seed.
  - Source fix: dropped the `if (variant>=0)` guard around the commit; `st.result/st.close` and
    `rec->confirmed` now fire whenever btn==1210||key==28; `chosenVariant = sel` (carries seed).
  - Golden fix: `OkWithoutSelectionDoesNotCommit` was asserting ret==0 (WRONG). Renamed to
    `OkWithoutSelectionStillCommitsSeed`, now asserts ret==1, introVariant unchanged (seed),
    confirmed==true, chosenVariant==seed. Evidence: 0x52e6c7/loc_52E64C.
- Counts: 1 source branch corrected, 1 golden test corrected. 5 tests in suite, all green.

### 0x52e3d8 — VIBE_Menu_ChooseCharacterIntro (spine router) — **FIXED + 1 BOUNDARY**
Diffed against disasm 0x52e3d8..0x52e4d9.
- Returns: OK/1210 or Enter/28 -> esi=1 (loc_52E4AB); window-close/ESC(1) -> esi=-1; default 0. VERIFIED.
- **BUG (fixed):** the back-button (1155) branch force-set `result=0`. The disasm 1155 path
  (0x52e4d3) only arms `dword_631614=ebx` and **leaves esi untouched**. In the rare frame where
  window-close (esi=-1) and button 1155 coexist, the original returns -1, the source returned 0.
  - Source fix: the `else if (btn==1155)` branch now only sets `close=true`; it no longer assigns
    result, so back keeps the prior value (-1 if window-close fired this frame, else the 0 init).
    The existing router tests (OK->1, Enter->1, back->0, close->-1, default->0) all still hold.
- **BOUNDARY:** the conditional 2nd-body render `if (((dword_63C8F0+1)>>24) != -1) RenderRichString(0x16ED)`
  (0x52e3fd..0x52e483) is NOT modeled. `dword_63C8F0` is a game-state global outside this
  abstraction; the call has no effect on control flow or return (the 0x16EE call that follows
  supplies the child-id base). Left as a documented host-side render boundary (header line 94,
  `kCharIntroChooseBodyId` constant). No behavioral impact on this function's result.

---

## choosecharacter_run.cpp

### 0x52bcd4 — VIBE_Menu_RunChooseCharacter — **VERIFIED-1:1 (control flow) + BOUNDARY**
Diffed against disasm/decompile 0x52bcd4..0x52c4e4 (large scene function).
- Observable spine matches: place actors until 6 filled (`v7>=6`), then
  ChooseCharacterTalent() (cancel -> result 0, close); else ChooseProfession() && BuildCharacterPreviewScene()
  -> result 1, close; otherwise re-show form and retry. Window-close (dword_672230) -> close.
  Return = v33 (1 = started). Source matches this exactly via hooks. VERIFIED.
- **BOUNDARY (by design):** the dummy-actor table (10 `Character_CreateMenuDummyActor` pairs),
  the actor->profession/slot mapping (v5/v7 parity tables at 0x52c061..0x52c2cf), the sky/cutscene
  setup, the fade-out loop (v33==1), and all object/anim/sound struct writes are host leaves
  (`SceneSetup`/`PickActor`/`PlayActorAnim`/`ChooseCharacter_ApplyActorClick`/`SceneTeardown`).
  The slot/profession mapping + fill counter live in the reused `ChooseCharacter_*` helpers
  (gui/newgame_setup). Control flow + return are 1:1. 1 suite, green.

---

## choosecity_run.cpp

### 0x52e6d8 — VIBE_Menu_RunChooseCity — **VERIFIED-1:1 (control flow)**
Diffed against decompile 0x52e6d8..0x52ee24 (incl. the per-file body at 0x52e797).
- Enumerate city files; per-file: Vfs_OpenFile + Save_LoadHeaderAndThumbnail (open/header fail
  -> skip), SpawnCityPointMarker, on marker: `_STADTAUSWAHL_%s_INFO+0` upper -> FindTextArrayIndex,
  StatusText_Register("stadt_%s", ...). VERIFIED.
- Loop: FindNearestObjectAt; hover (dword_67221C) -> hoverCity; else `dword_672230||esc(1)` -> close.
  Hover-change ("stadt_" prefix, StrncmpN n=6) renders $C name + description. VERIFIED.
- Confirm `dword_75BF38==1210 || byte_67225C==28`: hide both forms; if `v76`(network/a1) -> close+result=1;
  else ChooseCharacterIntroVariant() -> RunChooseHistory() chain (re-show main inside on success),
  then re-show main+header forms unconditionally. Cleanup: destroy forms (!=-1), tower (v78),
  markers. Return v80. Source matches structure exactly. VERIFIED.

### 0x52e797 — per-file enumeration body — **VERIFIED-1:1** (covered above; it is the loop body of 0x52e6d8).

### 0x52ee38 — VIBE_Menu_EnterChooseCity — **VERIFIED-1:1 + note**
Diffed against decompile.
- `word_63C740=0` (st.sessionFlags), `DragCursor_SetSprite(0)`, `byte_63CC1D=1` (enterMarker),
  RunChooseCity, on `!result`: Surface_ColorFill + Window_RenderEntityList(1773). Return result. VERIFIED.
- Note (not a bug): original calls `RunChooseCity(0, a2)` i.e. a1/v76(network)=0, so the network
  fast-path is never taken via this entry; the source treats `st.network` as upstream data input
  and does not reset it here. Matches the documented data-model design; observable return/render path 1:1.

---

## choosehistory_run.cpp

### 0x52d684 — VIBE_Menu_RunChooseHistory — **VERIFIED-1:1 (control flow) + BOUNDARY**
Diffed against decompile 0x52d684..0x52daae.
- Build: 4 child ids (base+0..3), `RadioGroup_Create(4, id0)`. Seed switch on `dword_12335AC`:
  case0->Selection_Update(2), case1->0, case2->1, no default. Source `ChooseHistory_SeedIndex`
  (0->2,1->0,2->1, else -1) + `if (seedIdx>=0)`. VERIFIED.
- Confirm: id0->flag1, id1->flag2, id2||Enter(28)->flag0, then History_SetActiveFlag + re-derive
  Selection_Update (flag 0->idx2,1->idx0,2->idx1 == `ChooseHistory_FlagToIndex`). VERIFIED.
- Dialog (Mission_RunChooseHistoryDialog, 0xFF==cancel); on confirm: form hide, run character
  spine -> v8, close. dialog-cancel: form re-shown, stay. Back(1155)/window-close -> close.
  Return: `if (v8) word_63C740|=8; return v8; else word_63C740=0; return 0`. VERIFIED.
- **BOUNDARY (by design):** the post-dialog character SPINE (the v9/v10 retry loop:
  cutscene B_Persoenliches -> RunChoosePlayer -> ChooseCharacterIntro -> RunChooseCharacter |
  ChooseProfession, with the v10 "retry from history" path and the Universe_SwitchActiveSlot /
  Building_LookupTypeRecordA / dword_122F4EC / dword_122F528=1555 side effects) is collapsed into
  `RunCharacterSpine()` returning the final v8. Documented in header lines 38-43. The radio screen,
  seed/flag mapping, dialog gate and the `word_63C740` return are 1:1. 1 suite, green.

---

## chooseplayer_run.cpp

### 0x52ccd8 — VIBE_Menu_RunChoosePlayer — **VERIFIED-1:1 (page state machine) + BOUNDARY**
Diffed against decompile 0x52ccd8..0x52d3a0.
- INI seed: [Network] Name(Vorname), Familienname(Nachname), Wappen(+1342), Geschlecht, Glauben. VERIFIED.
- Page wizard (v2, 0..5): close edge (dword_672230 window-close, back-widget v61==dword_62D22C,
  or ESC(1)) steps back a page when v2>0 else arms dword_631614 (exit, v70 stays 0). Page 5 (case 5)
  auto-commits: v70=1, dword_631614=1. Pages 0/1 commit on Enter(28); 2/3 on radio click; 4 on
  wappen gfx click (id 1342..1349) then advance. Return v70. Source matches via PageAction/GetText/
  GetChoice. VERIFIED.
- **BOUNDARY (by design):** the literal per-page input plumbing — the 8 wappen `Object_AddToWindow`
  buttons + 740-byte object struct writes (0x52cfac..), the text-field copy loops (0x52d24d / 0x52d430),
  the radio InitStateReader dispatch (0x52d50a/0x52d56e/0x52d5d2), the selection-highlight clearing
  (0x52d618..0x52d665), the `dword_63C8F0` "already-has-character" check at page 5 (0x52d4e6), and the
  INI read/write — are host leaves (ReadIniDefaults/WriteIni/PageAction/GetText/GetChoice). The page
  state machine + per-page value collection + return are 1:1. Note: source clamps wappenIndex to
  [0,7] for safety; the original page-4 path only ever matches an existing button id in [1342,1349],
  so the clamp is non-observable. 2 suites (run + textedit), green.

---

## Summary of changes
- choosecharacter_intro_run.cpp 0x52e4e0: removed the radio-match guard on the OK/Enter commit
  (commit is unconditional per loc_52E64C); chosenVariant carries the current selection.
- choosecharacter_intro_run.cpp 0x52e3d8: back(1155) no longer overwrites result (matches the
  disasm leaving esi untouched), preserving -1 on the window-close+back same-frame edge.
- tests/unit/choosecharacter_intro_run_test.cpp: corrected the OK-without-selection golden
  (now `OkWithoutSelectionStillCommitsSeed`, ret==1).
- All other provenance functions VERIFIED-1:1 at the control-flow/return level; deep struct/scene
  plumbing intentionally lives behind the documented host-boundary hooks (no cheap analogues —
  reused helpers carry the real tables/mappings).
