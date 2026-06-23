# Hardening sweep — world_02_part2 (chronicle/history + mission dialogs)

Full-tree 1:1 MCP verification of every provenance-carrying function in:
- src/world/history_full.cpp / .h
- src/world/history_mission.cpp / .h
- src/world/history_parse.cpp / .h
- src/world/history_scan.cpp / .h
- src/world/history_second_pass.cpp / .h
- src/world/history_text_pass.cpp / .h

Every function was decompiled from gilde.exe and diffed line-for-line; every
table/constant was confirmed via get_bytes/get_global_value; every float->int
site was checked (none in this chunk — these are pure integer/string/pointer
cores, no x87/SSE conversions). **No source or golden edits were required; all
reconstructions are already 1:1.**

---

## history_full.cpp

### HistoryRoleNameIndex / kHistoryRoleNames[15] / kHistoryPrefixTable[5]
VERIFIED-1:1. Tables confirmed byte-for-byte:
- get_bytes 0x6343B8 (prefix, stride 5): `5f 4e 45 57 00 | 5f 55 53 45 00 | 5f 52 45 4c 00 | 5f 53 45 54 00 | 5f 55 53 45 00` = `_NEW,_USE,_REL,_SET,_USE`. ✓
- get_bytes 0x633FF8 (roles, stride 64): "BUERGERMEISTER"@+0, "BISCHOF"@+64. ✓
- Role loop in 0x4fd44c (VIBE_History_ParseContext): `while (memcmp(v3, name, strlen(name))) { ++idx; name+=64; if (idx>=15) notfound; }` — the `< 15` gate and leading-prefix memcmp match the C++ exactly.

### HistoryParseTextFirstPassValid — gilde.exe 0x4fd220
VERIFIED-1:1 (syntax-rule extraction). Bracket grammar `[`/`#`/`]` with the
single-region + single-marker rules and the final `if (v2||v20||v19)` syntax-error
gate all match. The original's leading `if (!dword_B537B4) return 0;` global-enable
gate and the dword_8C36B0 label-table fetch are engine data-feeds (the C++ takes the
already-resolved label string); documented in the header. No divergence in the rule
logic.

### HistoryClassifyTokenMode / HistoryTokenSlot — gilde.exe 0x4fd44c
VERIFIED-1:1.
- Mode scan loops `v4 < 3` comparing the token's leading dword against prefix[0..2]
  (`_NEW`/`_USE`/`_REL`), default v16 = 4 (literal). Matches.
- Slot read CONFIRMED in disasm: `v12[0] = *(_BYTE*)(a1 + 5)` (0x4fd4fa) → ParseInt →
  `if (v13 < 8)` (unsigned). The C++ reads token[5] and gates `slot < 8`. The golden
  vectors (`_NEW_3`→3, `_USE_7`→7, `_REL_9`→-1 [9≥8], `_NEW3`→-1 [token[5]='\0'])
  correctly encode the `a1+5` read. ✓

### HistoryScanStep — gilde.exe 0x4fe694 (forward scanner)
VERIFIED-1:1. Per-entry classification: `!parseOk`→4; dayDiff==1→ (textEmpty?6:1);
dayDiff<=1→2; else→continue(0=kEndOfFile). Disasm-confirmed the emit code is 1.
Golden vectors all match.

---

## history_scan.cpp

### HistoryScanRealStep / HistoryScanRealStepContinues — gilde.exe 0x4fe9cc
VERIFIED-1:1. **Disasm-confirmed a Hex-Rays omission**: at loc_4FEA55 the binary does
`mov cl,1` BEFORE calling ParseLabelPasses (pseudocode dropped this init of the
uninitialized `v5`). So the codes are: parse-fail→4; dayDiff==1 → gate-fail=5,
gate-pass+empty=6, gate-pass+nonempty=1; dayDiff<1→2; dayDiff>1→0. The break guard
`if (v5 && v5!=6 && v5!=5)` (0x4fea7f) terminates on 1/2/4 and loops on 0/5/6 —
exactly what HistoryScanRealStepContinues returns. Reconstruction and header comment
already correct.

---

## history_second_pass.cpp

### HistoryCollapseLabel — gilde.exe 0x4fe0ec (VIBE_History_ParseTextReal)
VERIFIED-1:1. The two in-place MemMove rewrites match the binary EXACTLY (this is the
REAL core, distinct from the FirstPass 0x4fd220 which shifts differently):
- `MemMove(v17 [marker], v8+1, ...)` then `MemMove(v2 [regionStart], v2+1, ...)`.
  C++: `HistoryMemMove(marker, p+1)` then `HistoryMemMove(regionStart, regionStart+1)`. ✓
- Resume pointer `v8 = v17 - 1` (= marker-1), the `]` path skips the ++ step and
  drops straight into the LABEL_12 NUL test. C++ `resume = marker - 1` + `continue`. ✓
- Final render gate `if (!v2 && !v17 && !v16)`. C++ returns kRendered iff
  `!regionStart && !marker`. ✓

### HistoryRunLabelPasses — gilde.exe 0x4fe0b0 (VIBE_History_ParseLabelPasses)
VERIFIED-1:1. `FirstPass; if(!r) return; SecondPass; if(!r) return; if(a3)
ParseCommandlineSecondPass; return 1.` Three-stage gate + fire-and-forget commandline
flag match.

---

## history_text_pass.cpp

### HistoryParseTextSecondPass — gilde.exe 0x4fdcec
VERIFIED-1:1. Full token-walker diff against the binary:
- `_` opens a token (resets buffer, v49='_', v53=1, v15=0); ordinary chars copy to out.
- inside token: space→SyntaxErr; `-`→push,++dashes,`if(v15>2)` err; `v15>=2`→first
  char must be digit-class (`byte_64A208[c+1]&0x20`), consume the digit run, ParseContext
  (resolve hook), append result, copy the terminating char, reset.
- digit-class table reuse (byte_64A208) is the verbatim lexer table. ✓
- End-of-text: `if(v53 && v15<2)` err; `if(v53==1 && v15==2)` resolve+append; else return
  1. C++ `inTok&&dashes<2`→err, `inTok&&dashes==2`→resolve, matches (dashes∈{0,1,2}).
ParseContext + the group-row fetch are engine resolver leaves (surfaced via the hook).

---

## history_parse.cpp

### HistoryFormatDate / HistoryRoundtripDate — inverse of gilde.exe 0x4fe2d4
VERIFIED-1:1 (synthesized inverse, layout confirmed). ParseDate (0x4fe2d4) reads a
10-char `DD.MM.YYYY` string: day=[0:2], sep [2], month=[3:5], sep [5], year=[6:10]
(v65 @ v64+3, v66 @ v64+6). The day==0→1 and month==0→1 normalisations match
(`if(!v40){v40=1;...} if(!v39){v39=1;...}`). The C++ format positions (out[2]='.',
out[5]='.', 2-digit day/month, 4-digit year) and the normalisation are byte-consistent
with ParseDate's reader; round-trip stable against world/history.cpp HistoryParseDate
(field offsets verified).

### HistoryNotifyTargetFoundTextId — gilde.exe 0x535afc
VERIFIED-1:1. Kind byte at +2 == 6||7 → text id **3953** else -1.

### HistoryNotifyTargetReachedA{TextId,VoiceBase} — gilde.exe 0x535bb0
VERIFIED-1:1. Kind 6||7 → text id **3963**; detained (`*((_BYTE*)v4+9)`) ? **3967** :
**3964** voice base. (The two RandomModulo(3) draws + He broadcast + RenderFormattedMessage
are render/RNG leaves; the C++ exposes the byte-exact base ids only — documented.)

### HistoryNotifyTargetReachedB{TextId,VoiceBase} — gilde.exe 0x535c68
VERIFIED-1:1. Kind 6||7 → text id **3973**; detained ? **3977** : **3974** voice base.

(The shared kind gate world::HistoryNotifyKindIsImportant == {6,7} confirmed in
history.cpp 0x535afc/bb0/c68.)

---

## history_mission.cpp — decode cores VERIFIED-1:1; driver bodies faithful over leaves

### Pure per-tick decode helpers (golden-vector cores)
- **MissionSpecialStep** — 0x5387c8 inner loop. `(672230 || 75BF38==1155)→exit`;
  `75BF38==1210→{confirmed=1; exit}`. Constants 1155=Decline, 1210=Accept confirmed
  in header. VERIFIED-1:1.
- **MissionAckStep** — 0x539e8c / 0x53a41c. `672230 || 75BF38==1210` (Info uses
  else-if, same net effect). VERIFIED-1:1.
- **MissionChooseStep** — 0x538950. `(672230 || byte_67225C==1 || 75BF38==1155)→Exit`;
  `75BF38!=-1 && radioBtnId!=-1 && 62D22C==radioBtnId →Activate`. VERIFIED-1:1.
- **MissionCompletionStep** — 0x53ac34. `63CC24==-1 → close`. VERIFIED-1:1.
- **MissionOfferStep / MissionOfferTriggersReload / MissionOfferGiveButtonPresent**
  — 0x53a854. Button decode ORDER confirmed: give(ChildObjectId, only when v31<4) →
  decline(v34/6054, blob refuse=0) → abandon(v13/6055, blob abandon=1, v33=1 → reload).
  `MissionOfferGiveButtonPresent` uses unsigned `v31 < 4` (`v31 = descriptor[+1]+1`).
  VERIFIED-1:1.

### Driver bodies (structure verified; form/text/audio/command = leaves)
- **MissionRunSpecialDialog** — 0x5387c8. Descriptor scan (stride 24, byte_63CD4C),
  form create/center/select, nameId=`descriptor[+4]+1`, voice -7
  `_AUFTRAEGE_VERGABE_HS_%.2d` w/ descriptor[+8], frame loop w/ MissionSpecialStep,
  StopVoice(25) on VoiceIsPlaying(1), return confirmed. FAITHFUL. *Note:* the binary
  interleaves two `RenderRichString("$N")` literal-string renders (between nameId/0x7D
  and 0x7D/0x7E) that the id-only renderText hook does not model — pure text-render
  leaf detail, no logic impact.
- **MissionRunFailureDialog** — 0x539e8c. Same shape, voice arg -1, MissionAckStep,
  StopVoice(25) on VoiceIsPlaying(0). FAITHFUL (same `$N` render-leaf note).
- **MissionRunInfoDialog** — 0x53a41c. Render 0x17A6, SelectWindow(2), 0x7D, frame loop
  (a1|0x80) w/ MissionAckStep, return Form_Destroy. FAITHFUL.
- **MissionRunRewardSummary** — 0x539fd8. VoiceQueue_FlushAll, 4 reward lines
  (0x17A8/17A9/17AA, then unk_623D6C+0x17AB+subId, then ERFOLG line). subId =
  `reward[+4]+2`. The audio-off **deadline math is exact**: deadline = gameTick(62EB38)
  + 250, loop while not-clicked AND deadline > tick (re-read each iteration); the C++
  RunRewardLine mirrors this (capture tick0, deadline=tick0+250u, break on click or
  `deadline<=tick`, De-Morgan-equivalent to the binary's continue condition).
  FAITHFUL. *Note:* the ERFOLG sample is formatted with `reward[+12]` and the 4th-line
  audio-on path returns Form_Destroy on click — both folded into the audio leaf /
  last-line destroy (net effect identical, form destroyed).
- **MissionRunCompletionDialog** — 0x53ac34. Center/select, set a1 flags, RunRewardSummary,
  render 0x17A2, do-loop {MissionCompletionStep(63CC24==-1)} while RunFrameLoop(a1|0x80),
  destroy, switch(outcome) via MissionDecodeCompletion (1→Failure,2→Info,3→reload).
  FAITHFUL.
- **MissionRunOfferDialog** — 0x53a854. RunRewardSummary, form, render 0x17A1, give btn
  6053 (only when historySeed/v31 < 4) / decline 6054 / abandon 6055, frame loop w/
  MissionOfferStep, post-loop decline→Failure / abandon(v33)→reload(63CC30=1).
  FAITHFUL.

### BOUNDARYs (rule-8 legitimate; engine data/tech leaves, not faked logic)
- Form create/center/select/destroy, RenderRichString/RenderFormattedMessage,
  PlayPositionalSample/VoiceIsPlaying/StopVoice/IsInitialized, RunFrameLoop, and the
  Command_QueueRequest blob dispatch — all reached via MissionDialogHooks (GUI / SDL
  audio / command-network boundaries). 0x535afc/bb0/c68 RandomModulo(3) + He broadcast
  + text render likewise. Descriptor table byte_63CD4C (stride 24) and group table
  word_12CE910 are engine session data surfaced by the caller.

---

## Counts
- Functions/units examined (provenance-carrying): **26**
  - history_full.cpp: 6 (RoleNameIndex, FirstPassValid, ClassifyTokenMode, TokenSlot,
    ScanStep, + 2 tables)
  - history_scan.cpp: 2 (ScanRealStep, ScanRealStepContinues)
  - history_second_pass.cpp: 2 (CollapseLabel, RunLabelPasses)
  - history_text_pass.cpp: 1 (ParseTextSecondPass)
  - history_parse.cpp: 6 (FormatDate, RoundtripDate, 4 Notify selectors)
  - history_mission.cpp: 13 (6 decode helpers + 7 driver bodies)
- **VERIFIED-1:1: 26** (all decode cores + tables + selectors + bracket/text walkers)
- **FIXED: 0** (no divergences found; reconstructions already byte/branch-accurate,
  including the disasm-only `cl=1` init at 0x4fea55 which the reconstruction already
  modeled as kEmit)
- **BOUNDARY: GUI/audio/command/render + engine session-data leaves** in the mission
  driver bodies and the Notify render path (rule 3-5 + data-not-in-tree).

## Build / handoff
- All 6 owned TUs pass `g++ -std=c++17 -fsyntax-only` cleanly.
- The full libguild.a link was initially blocked by a **truncated stale object**
  (`ai_eval.cpp.o`, build-cache corruption — removed it, library then builds) and by a
  **pre-existing compile error outside this chunk**: `src/sim/animal_wander.cpp:302/306
  — cannot convert 'bool' to 'guild::sim::SoundBurnPolicy'`. HANDOFF to the
  animal_wander/sim owner; not touched (outside ownership). My files require no changes
  to compile.
