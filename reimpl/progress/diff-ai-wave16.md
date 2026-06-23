# Wave-16 TRUE 1:1 binary diff — sim AI / NPC-action cluster (agent W16-AI)

MCP live. Decompiled every brief-named target and compared LINE-FOR-LINE against
the reconstruction in `src/sim/`. One real divergence found and FIXED (the
npc_daily season signed-index, hidden by a wave-12 hardening mask + a wrong
golden); everything else VERIFIED-1:1 against the decompile + get_bytes. The
wave-12 he.h misaligned-accessor verdict is RESOLVED (keep — faithful, no UB).

---

## FIXED — npc_daily season is a SIGNED `day % 4`, indexed UNMASKED (the brief's flag)

### The binary (the reference of record)
* `VIBE_GameTime_GetSeasonFromDay` @0x58339c — disasm:
  `mov ecx,4; sar edx,1Fh; idiv ecx; mov al, dl; retn`. This is a **signed**
  division (`idiv`), returning the remainder `day % 4` truncated to a **char**
  (`al`). For day>=0 it is 0..3; for a negative day the remainder is negative
  (-3..-1). Decompile: `return *a1 % 4;`.
* `VIBE_NpcAction_DailyRoutineStep` @0x4e7e88 stores that season as a **byte**
  (`mov [esp+..var_1C], al`, var_1C is `_BYTE`), then indexes the season tables
  with the **sign-extended** byte — disasm @0x4e80c3:
  ```
  mov eax, [esp+..var_20+1]   ; pull the season byte into the high byte
  sar eax, 18h                ; arithmetic shift 24 => SIGN-EXTEND season
  fld ds:flt_6476FC[eax*4]    ; index work-start table with the SIGNED season
  ```
  (Same idiom at 0x4e81d6/0x4e81e0 for the work-start transition compare.)
  There is **no `& 3` mask and no clamp** anywhere — a negative day genuinely
  indexes the 4-entry tables negatively in the original (its own latent property).

### The divergence (wave-12 hardening that was NOT 1:1)
* `src/sim/npc_daily.cpp`: `NpcDaily_DailyRoutineStep` did `SeasonFromDay(day) & 3`
  and `SelectDailyActivity` did `season & 3`. The `& 3` mask changes behavior for
  any out-of-domain / negative season vs the binary's raw signed index — a real
  divergence per rule 1 (edge cases / integer behavior must match).
* `src/sim/npc_daily.h`: `SeasonFromDay` returned `int(day % 4)` — missing the
  `char` truncation (a no-op for the [-3,3] domain, but recorded for fidelity).

### The fix (source + header + golden, all to the binary)
* `npc_daily.h`: `SeasonFromDay(day)` now returns
  `(int)(signed char)(day % 4)` — char-truncated signed remainder, exactly
  `GetSeasonFromDay`. Provenance comment cites the 0x58339c disasm + the
  0x4e80c3 sign-extend index.
* `npc_daily.cpp`: both `season & 3` masks removed; the season is used as the raw
  signed index, matching the binary (no-op on the real day>=0 domain).
* `tests/unit/sim_npc_daily_test.cpp`:
  * `ConstantsAndSeason`: added the **signed-remainder** golden cases
    (`SeasonFromDay(-1)==-1`, `(-3)==-3`, `(-4)==0`, `(-7)==-3`) — the
    `idiv; mov al,dl` behavior.
  * Replaced the old `RuleSeasonWraparoundNoOOB` test (which PINNED the wrong
    `& 3` masking — `season==4 -> spring`, `-1 -> winter`, `0x7fffffff -> winter`)
    with `RuleSeasonAllFourUnmasked`, which pins the four valid seasons' work-start
    windows {7,6,7,8} and go-home windows {22,23,22,21} via direct (unmasked)
    table lookups. The wrong golden was deleted, not hidden.

Result: `sim_npc_daily_test` 102 checks, 0 failures.

---

## VERIFIED-1:1 (matched the decompile / get_bytes; no change)

### npc_daily season tables (get_bytes @0x6476FC / 0x64770C / 0x61F968/64/38)
* `kWorkStartHour` flt_6476FC = {8,7,8,9}  (0x41000000,0x40e00000,0x41000000,0x41100000) ✓
* `kWorkEndHour`   flt_64770C = {20,21,20,19} (0x41a00000,0x41a80000,0x41a00000,0x41980000) ✓
* `kWorkStartSlack` flt_61F968 = -1.0 (0xbf800000) ✓
* `kEveningOffset`  flt_61F964 = +2.0 (0x40000000) ✓
* `kPickWeightFactor` dbl_61F938 = 0.0125 (0x3f8999999999999a) ✓

### npcaction dispatch jump table (the type->handler map)
* `VIBE_NpcAction_Dispatch` @0x5766a0 decompile: `(int)qword_13CE852 < 8 ->
  DebugCmd_RetZero(); !a1 -> -1; *(WORD*)(a1+4) < 0x45 -> funcs_5766CB[type](a1,
  type); else -2`. Matches `NpcAction_Dispatch` exactly (day<8 -> 0, null -> -1,
  type>=0x45 -> -2, else table dispatch).
* `funcs_5766CB` @0x63d964 — get_bytes of all 70 dwords (69 handlers + a
  0x00000000 sentinel). Decoded little-endian and compared element-by-element
  against `kNpcActionTableAddrs[69]`: **exact match** (incl. the three repeated
  0x57477c slots at indices 41, 53, 54, 55 and the 0x575028/0x57502c adjacent
  pair). Table size 0x45 == 69 confirmed.

### clip-select state->clip mapping (VIBE_Character_Update @0x405148 core)
* All five clip strings + the two jitter constants re-confirmed via get_bytes:
  0x610170 "bewegung/gehen", 0x61024c "bewegung/karren_ziehen",
  0x61070c "bewegung/dreh_90_rechts", 0x610158 "stehen/stehen_newnoise",
  0x6103bc "sitzend/sitz_newnoise", 0x6103FC dbl=0.01, 0x610134 "" (empty).
  `npc_clip_select.cpp`'s constants + the type-7/45/58 catalog mapping + the
  gait/idle/sit gates are byte-faithful. `npc_clip_select_test` 96 checks, 0 fail.

### action-queue ring (DispatchCurrent / CheckDurationExpiry)
* `VIBE_ActionQueue_DispatchCurrent` @0x404768 — decompile matches the recon's
  modeled control flow: node=+296; null->0; !node[5](ready)->1; !node[0](step)->1;
  run step; v5=node[4](chained); ++node[12](callCount); !chained->1; then the
  motion gate at char+112 (with the +128/+133-vs-+108/+8-priority sub-checks) and
  the chained-phase run + latch write. The recon documents the anim-frame-counter
  gate as a faithful model (no anim handle => chain skipped), which is the same
  observable result.
* `VIBE_ActionQueue_CheckDurationExpiry` @0x40bdd8 — decompile EXACT:
  `if(!+12) +52=dword_62EB38; if(+48 + +52 < (unsigned)dword_62EB38 || +400)
  return UnlinkEntry(); return record;`. The recon's callCount/+48/+52/tick/abort
  + UnlinkEntry-on-expiry matches 1:1 (the per-tick clock dword_62EB38 is g_gameTick).

### npctarget scan (FindNearestEnemy + reject curve)
* flt_61A680 block get_bytes: {8.0, 52.0, 66.0, 33.0} == {kFavRejectSlope,
  kFavRejectBase, kSeqAThreshold, kSeqBThreshold} ✓
* `VIBE_NpcTarget_FindNearestEnemy` @0x474dd4 — decompile confirms the recon's two
  paths: `+358==0` (CatA) => office-holder ring walk (favorability-min over
  GetHolderEntryByCity, step 268 words/record); else `+360` (CatC) =>
  CollectSuccessorCandidates(catC, 5, buf) favorability-min; then the reject
  curve `v20 * flt_61A680 + flt_61A684 < bestFav` == `count*8.0 + 52.0 < bestFav`.
  Recon constant order + gate order match. (The wave-12 buffer-cap clamps on the
  5/6-entry candidate buffers remain no-ops on valid input — the collectors always
  respect the cap; goldens byte-identical.)

### aimethod / score registry tables
* `VIBE_AiNeeds_LookupAttributeIndex` @0x4794e4 — decompile lists all 14 tokens in
  catalog order (APS=0, UNVERSEHRTHEIT=1, WOHNUNG, GELD, BERUF, VERGNUEGEN,
  ANSEHEN, AMT, BILDUNG, RECHTSCHAFFENHEIT, GEMEINHEIT, SICHERHEIT, FORTPFLANZUNG,
  TRAEGHEIT=13), case-insensitive compare, first hit wins, -1 + sprintf-log on
  miss. `kAttrNames[14]` matches byte-for-byte. The id gate (signed `id<=0 ||
  id>=61`), the per-slot fill schedule, the `& 0x7FFFFFFF` change-nonzero test,
  and the field schedule (0x468a40) all already match prior verification.

### ConvertX-truncate check (wave-15 heads-up)
* Audited the owned cluster for float->int sites. The only season->int path
  (`(int)kWorkEndHour[season]` for the appointment hour in DailyRoutineStep) is a
  whole-number table value (19/20/21) so truncation vs round is moot; the binary
  routes it through Coord_ConvertX (truncate) and the recon's `static_cast<int>` of
  an exact float is identical. No nearbyint/round-to-nearest misuse found in the
  cluster — the AdjustRelationByMood `(int)inc` is already a faithful ftol/trunc,
  and AddTimeToActionDuration uses integer RandomModulo (no float). No fix needed.

---

## RESOLVED — wave-12 he.h misaligned-accessor verdict (this cluster)

VERDICT: **KEEP the misaligned accessors — they are 1:1 and have no real UB.**
`src/sim/he.h` reads/writes the He record via `reinterpret_cast<T*>(base+off)` at
the engine's exact byte offsets (e.g. dword at +172, word at +180, GameTime at
+82, bytes at +358..+361). On the binary's x86 target these unaligned accesses are
hardware-legal and are exactly how the original addresses the record (raw
`*(T*)(reg+off)`). The wave-12 ASAN+UBSAN sweep already proved that with alignment
made recoverable, **every owned test passes with zero buffer-overflows and no
non-alignment UB** — the only trips are the alignment class, which is the engine's
intentional byte-faithful layout, not a bug. Reworking the packed layout to
satisfy strict `-fsanitize=alignment` would change nothing observable and would
obscure the byte map (and break the offset static_asserts that pin fidelity).
Resolution: no change; documented as the engine's envelope per rule 1.

---

## Build / tests

Owned cluster targets all green (built `libguild.a` + linked before the sibling
world-cluster edit landed):

| target | result |
|---|---|
| sim_npc_daily_test            | 102 checks, 0 failures |
| npc_clip_select_test          | 96 checks, 0 failures |
| npcaction_dispatch_boundary_test | 10 checks, 0 failures |
| aimethod_index_boundary_test  | 21 checks, 0 failures |
| actionqueue_boundary_test     | 28 checks, 0 failures |
| npctarget_boundary_test       | 11 checks, 0 failures |
| sim_npc_daily_e2e_test        | 20 checks, 0 failures |

### Cross-cluster note (NOT mine, NOT my edits)
A concurrent sibling wave-16 agent owns `src/world/**` and is mid-edit;
`src/world/mission_rules.h:70` currently has a transient "multiple definition of
enum class MissionCompletionOutcome" that breaks the shared `libguild.a` link
after my owned objects compiled. My cluster's source + tests compile and pass
cleanly (the runs above linked the lib before that regression). No action taken —
out of ownership; it will resolve when the world owner finishes.

## Files changed (owned only)
* `src/sim/npc_daily.h`   — SeasonFromDay = signed char(day%4); provenance.
* `src/sim/npc_daily.cpp` — removed both `season & 3` masks (1:1 signed index).
* `tests/unit/sim_npc_daily_test.cpp` — signed-season goldens; replaced the
  wrong `& 3` wraparound test with the unmasked four-season window test.
