# Wave-16 TRUE 1:1 binary diff — world history/statistics cluster (W16-HIST)

MCP LIVE. Line-for-line decompile diff of the history-blob parse, chronicle /
second-pass / text-pass functions, the parse-context dispatch, the stats category
math, and the He_* engine tail; resolution of the entire wave-14 NEEDS-LIVE-MCP
queue. Tables golden-pinned via `get_bytes` against the live module.

## Decompile targets pulled this wave
0x4fd44c ParseContext, 0x539fd8 RewardSummary, 0x53ac34 CompletionDialog,
0x53a854 OfferDialog, 0x4c51b4 LogInvalidAllocType, 0x4c51fc LogInvalidRunType,
0x4c6ca4 DestroyIconsForEntity, 0x4c67e0 CreateGfxInfo, 0x4c662c CreateIconMesh,
0x5950a4 DumpNpcAttributes (+ disasm of the bitfield shl/shr), 0x579ad0
BuildEconomyReport, 0x5dc070 Util_ParseInt. `get_bytes`: 0x6343B8 (prefix),
0x6343d8 (funcs_4FD5D2), 0x633938 (aFest 27), 0x633FF8 (aBuergermeister_3 15),
0x62570C (scale) + 0x625714..62573C (6 thresholds).

---

## FIXED (a real divergence vs the binary)

### FIX-1 — HistoryTokenSlot read offset (DRIFT-1 settled)
`world/history_full.cpp HistoryTokenSlot` read the slot digit at **token[4]**.
The binary's ParseContext @0x4fd44c does `v12[0] = *(_BYTE *)(a1 + 5)` then
`VIBE_Util_ParseInt(v12)` (v12 was memset to 0 just above, so ParseInt sees a
single char) — i.e. the digit is at **token[5]** (token[0..3] = 4-char prefix,
token[4] = separator, token[5] = decimal slot digit). The live sibling
`sim/history_parse.cpp HistoryParseContext` already reads token[5]; the standalone
helper was the outlier.
- **Source:** `HistoryTokenSlot` now reads `token[5]` (provenance comment updated).
- **Golden:** `world_history_full_test` / `world_history_full_e2e_test` token-slot
  vectors moved to 6-char tokens (`_NEW_3`, `_USE_7`, `_REL_9`, `_USE_2`) that
  place the digit at index 5; added a binary-edge pin: `_NEW3` (NUL at [5]) -> -1.
  The old `_NEW3 -> 3` golden was WRONG per the binary and was corrected, not
  hidden.

---

## RECONSTRUCTED (deferred bodies now translated — rule 7)

### REC-1 — RewardSummary / Completion / Offer dialog driver bodies
The three frame-loop drivers named in `history_mission.h` but previously absent
from `history_mission.cpp` are now reconstructed 1:1 over the existing
`MissionDialogHooks` (inert-default) pattern; their pure decode cores were already
pinned.

- **VIBE_Mission_RunRewardSummary 0x539fd8** -> `MissionRunRewardSummary`. Flush
  voice queue; build "special\\mission" form; render 4 lines (rich ids 0x17A8 /
  0x17A9 / 0x17AA / 0x17AB). Each of the 4 lines runs the shared per-line loop
  `RunRewardLine`: audio-on -> `PlayPositionalSample(-8, slot, "AUFTRAEGE_ALLGEMEIN")`
  then `while VoiceIsPlaying { RunFrameLoop; if (dword_75BF38!=-1 && child==dword_62D22C)
  { stop; break } }`; audio-off -> deadline = tick0+250, `do RunFrameLoop while
  (!click && deadline > tick)`. Line 4 ORs 0x80 into the frame arg and uses
  "_AUFTRAEGE_ERFOLG_HS_%.2d" (reward[+12]); the 0x17AB substitution id is
  `reward[+4] + 2`. Returns `Form_Destroy(form)`. New hook fields:
  voiceQueueFlushAll, renderTextArg, playPositionalSample, voiceHandleIsPlaying,
  readGameTick.
- **VIBE_Mission_RunCompletionDialog 0x53ac34** -> `MissionRunCompletionDialog`.
  Builds form, runs RewardSummary, renders 0x17A2, loops
  `do { if (dword_63CC24==-1) dword_631614=1; } while (RunFrameLoop)` (gate via
  `MissionCompletionStep`), destroys, and returns the post-loop outcome decoded by
  the existing `MissionDecodeCompletion` (mission_rules 0x53ad50:
  1->Failure / 2->Info / 3->LoadSession / else None). NOTE: I reused the existing
  `MissionCompletionOutcome` enum from `world/mission_rules.h` (avoided an ODR
  clash; the header now includes mission_rules.h).
- **VIBE_Mission_RunOfferDialog 0x53a854** -> `MissionRunOfferDialog`. RewardSummary
  then form + 0x17A1; the give button (6053) exists only when historySeed < 4
  (`MissionOfferGiveButtonPresent`, unsigned `v31 < 4`); per-tick button decode via
  the already-pinned `MissionOfferStep` (give=ChildObjectId, decline=v34=6054,
  abandon=v13=6055). Returns the closing action; `MissionOfferTriggersReload`
  (abandon -> dword_63CC30=1 reload) verified consistent (only the v13 path sets v33).
- Tests added (`history_mission_test`, 30->42 checks): OutcomeSwitch
  (MissionDecodeCompletion), GiveButtonPresentGate (unsigned < 4, -1 absent),
  and three scripted-hook body tests (offer decline path returns kDecline + both
  forms destroyed; completion returns kInfo; reward audio-off timed path returns
  the form handle and destroys it).

---

## VERIFIED-1:1 (decompile matches the reconstruction exactly)

### DRIFT-2 settled — He_DestroyIconsForEntity +8 field
0x4c6ca4 matches `parent == *(dword_11C6168 + i)` (field +8) and passes
`dword_11C6160 + i` (slot base) to DestroyIconGfx. CreateIconMesh @0x4c662c does
`*(a1+8) = a2` where a2 is the parent handed down from CreateGfxInfo @0x4c67e0
(`v5[1]=parent` at +4, entityId at +0, node at +12). So the struct field named
`mesh` (+8) actually holds the **owning-parent pointer**; the match is correct.
Source's simple 0..64 scan is behaviorally identical to the binary's
`i!=1024 step 16` loop. Comment updated to record the confirmed semantics
(name is a historical misnomer, no behavior change).

### He_LogInvalidAllocType 0x4c51b4 / He_LogInvalidRunType 0x4c51fc
Format strings byte-exact: "he_AllocNone(): Invalid HE-Type : %i, von Spieler %s"
and "he_RunNone(): Invalid HE-Type : %i, von Spieler %s"; type byte `*a1` = rec[0];
player name `&word_12CE910[268 * *((u16*)a1 + 4) + 24]` (supplied by caller);
RunType tail-calls NullHandler. Buffer sizes 260 vs 256 match. (UNDER-VERIFIED
string text -> VERIFIED.)

### DumpNpcAttributes 0x5950a4
First sprintf: 5 floats (+16/+20/+24/+28/+32) + assembled double (lo=+36,
hi=(i32)+38>>16) + uninitialized 7th `%i` (we emit 0 — the one unavoidable,
documented divergence: indeterminate stack). Second sprintf: 9 packed bitfields
of dword@+44 — disasm confirms every extraction is `shl`/`shr` (LOGICAL right
shift), matching the source's `std::uint32_t` shifts byte-for-byte:
&0xF; <<24>>28; <<20>>28; <<18>>30; <<15>>29; <<12>>29; <<9>>29; <<7>>30;
(8*field)>>28 == <<3>>28. VERIFIED.

### Stats category math + constants (0x579ad0)
total[k] = (accum[k] + accum[k+5] + accum[k+10] + accum[k+15]) * 0.25 confirmed
from the float-global offsets (base flt_1235090, stride 4; k=0/1/2/3 map to
goods/services/trade/luxury via the +0x00/+0x14/+0x28/+0x3C ... pattern).
flt_62570C = 0x3E800000 = 0.25; dbl_625714..62573C = {0.15, 0.20, 0.30, 0.50,
0.55, 0.45} (decoded). All match the source's pinned constants exactly. The
trend-id chain (6174..6181), weakest-argmin short-circuit, weak-line/severity
gates, and the luxury branch all match the decompile control flow.

### Keyword / role / prefix / dispatch tables (byte-for-byte re-confirm)
- 0x6343B8 prefix table (5-byte stride): `_NEW`,`_USE`,`_REL`,`_SET`,`_USE` — the
  source `kHistoryPrefixTable[5]` matches; ParseContext scans only the first 3
  (`v4 < 3`), so modes are {_NEW,_USE,_REL}, else literal — matches source.
- 0x6343d8 funcs_4FD5D2: first 4 ptrs 0x4f8fac/0x4f90e0/0x4f9238/0x4f9518 match
  the parse-context doc (indexed by the resolved role `v17>>24`).
- 0x633938 aFest: 27 keywords (64-byte stride) match `kHistoryCommandNames[27]`
  byte-for-byte (FEST .. INVENTAR_PLUS).
- 0x633FF8 aBuergermeister_3: 15 role names (64-byte stride) match
  `kHistoryRoleNames[15]` byte-for-byte (BUERGERMEISTER .. RND_SPIELER).

---

## STILL-DEFERRED
None new. The DumpNpcAttributes 7th `%i` field is the original's uninitialized
stack value; reproducing indeterminate stack contents is impossible, so we emit 0
(documented). Every other queue item is resolved.

## Files touched (cluster-owned only)
- src/world/history_full.cpp        (FIX-1 token[5])
- src/world/world_history2.cpp      (DRIFT-2 comment, confirmed semantics)
- src/world/history_mission.h       (3 new driver decls; reuse MissionCompletionOutcome; 5 hook fields)
- src/world/history_mission.cpp     (3 new driver bodies + RunRewardLine helper)
- tests/unit/history_mission_test.cpp        (+12 checks: 30->42)
- tests/unit/world_history_full_test.cpp     (token-slot goldens -> token[5])
- tests/e2e/world_history_full_e2e_test.cpp  (token-slot golden -> token[5])

## Build/test status
guild library + all cluster tests green:
history_mission_test (42 checks, 0 fail), world_history_full_test,
world_history2_test, world_history_full_e2e_test, statistic_recon_test,
world_history_cmdline_pass_test, history_cart_table_test,
lookup_table_save_recon_test — all exit 0.
