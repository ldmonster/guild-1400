# AI hardening: needs / desire-table / favorability — 1:1 diff vs gilde.exe

Scope: every provenance-carrying function in
`src/ai/desire_table.cpp`, `src/ai/building_needs.cpp`, `src/ai/favorability.cpp`
(+ owned headers). Each function was decompiled AND disassembled and diffed
line-for-line against the binary. Tables recovered byte-for-byte via `get_bytes`.

Result counts: **VERIFIED-1:1 = 11**, **FIXED = 1**, **BOUNDARY (deferred, faithfully modeled) = documented inline**.

---

## desire_table.cpp

### LookupAttributeIndex — gilde.exe 0x4794e4 — VERIFIED-1:1
Decompile: a 14-deep `StrCmpNoCase` chain returning 0..13, `-1` on miss (plus a
debug sprintf with no sim effect). Source name list + indices match the binary's
compare order exactly (APS=0 … TRAEGHEIT=13). `.rdata` string symbols confirmed
(aAps@0x61aab8 … aTraegheit@0x61ab40). No churn.

### ComputeWeights — gilde.exe 0x47936c — VERIFIED-1:1
- Table walk: `for(i=0;i<483;i+=21) if(*(int*)(&dword_6496A9+i)>>24 == *a1) break; if(v2>=23) return 0`.
  Source loop + `categoryId == plan.categoryId` (high byte key) matches.
- **Goods table dword_6496A9 (23 rows, 21-byte stride) recovered byte-for-byte
  (`get_bytes` 0x6496A9 +483) and decoded with the binary's exact read layout**
  (categoryId = dword`>>24` = byte[+3]; 8 good words at `word_6496AD`=+4, j=0..7;
  byte[+20] unused). All 23 rows + all 8 words/row match `kGoodsTable` exactly.
  Confirmed the unaligned-stride tail bytes (b20 = 0x59/0x71/0x76/0x66/0x7d/0x6b,
  and the dword low bytes on rows 3/9/12/15/18/21) are NOT read by the function —
  only `>>24` of the dword and the +4 words are consumed. Table is correct.
- float->int: both market prices go through `VIBE_Coord_ConvertX` (0x5c6b08). Disasm
  confirms ConvertX sets x87 RC=truncate-toward-zero (`HIBYTE(cw)=31` → 0x0C00),
  `frndint`, then RESTORES the cw; caller `fistp` then stores an already-integral
  value. Net = truncate toward zero == `(int)` cast. Source `static_cast<i32>` ✓.
- ratio = `(double)cached/(double)base`; `sum += ratio` (float store each step);
  max-slot update on `fcomp;jbe` (ratio > slot), min-slot on `fcomp;jnb` (ratio <
  slot); `avg = sum / count` via `fild count; fdivr`. All branch directions, store
  offsets (+0x1C base, +0x44 cached, +0x6C ratio, +0x94 avg, +0x98 max, +0x9C min)
  match. No churn.

### NpcAction wrappers — VERIFIED-1:1
- PerformEnterBuilding 0x471840: `ExecThreaten() ? 40 : EvalRejectStub()` ✓
- PerformOpenDoorLarge 0x471dfc: `FormatSlanderLabel() ? 43 : reject` ✓
- PerformOpenDoorSmall 0x471f24: `FormatSlanderLabel() ? 44 : reject` ✓
- PerformUseBack 0x471cb4: `!ExecSlander -> 0; else QueueRequestArgs25(*(actor+4),
  484,512,4,0); return 42`. Command emission routed through `g_useBackCmd` hook
  (deferred command leaf) — BOUNDARY, faithfully modeled. ✓

---

## building_needs.cpp

### BuildProbability — gilde.exe 0x4c7774 (prob step @0x4c77b5) — **FIXED**
Disasm: `mov edx,eax; sar edx,1Fh; shl edx,2; sbb eax,edx; sar eax,2; inc eax` —
the MSVC **signed-divide-by-4 truncating toward zero**, then `+1`. The source used
`(gameTick >> 2) + 1`, a bare arithmetic shift that rounds toward −∞ for negative
ticks (e.g. gameTick=-1: `>>2` = -1 but `/4` = 0). Fixed to `(gameTick / 4) + 1`
(C++ integer division truncates toward zero == binary). Header comment corrected.
Evidence: 0x4c77b5 sequence above. No golden changed (all existing goldens use
gameTick>=0 where `/4` ≡ `>>2`); the meister test suite still passes.
`p = N/(flt_4C5140[clamp(diff,0,4)] + N)`; scale table {4,3,2.5,1.5,1.0} confirmed
byte-exact (`get_bytes` 0x4C5140).

### BuildCandidateMask — gilde.exe 0x4c7774 (cand step) — VERIFIED-1:1 (BOUNDARY)
Candidate test `owned<3 || ratio>=0.5`, force when `owned==0`; ratio = owned/total
(total<=0 -> 0.0). Matches the phase-2/3 inline blocks (e.g. 0x4c809d, 0x4c8744).
`flt_61E85C`=0.5 confirmed byte-exact (`get_bytes` 0x61E85C). The full 256-slot
entity sweeps and the per-phase variant rules (phase 0 uses `<2`; phase 1 uses a
different `v23+v22 vs 2*v10` weighting) are DEFERRED plumbing — documented as the
intended abstraction. The generic rule faithfully models phases 2/3.

### BuildPickCategory — gilde.exe 0x4c7774 (pick step) — VERIFIED-1:1
forceMask wins outright; else `if RandomFloatScaled() > p return`; else
`start=RandomModulo(k); rotate +1 mod k up to k times for a set bit`. Traced the
binary loop (0x4c8108 phase-2 k=4, 0x4c8779 phase-3 k=2): `v42=k; while not set:
--v42; rotate; if !v42 bail` — identical iteration count and order to the source
`remaining=k; --; rotate; if 0 return 0`. ✓

### RunBuildingTasksOrder — gilde.exe 0x4c930c — VERIFIED-1:1
Decompile: RequestBuildingCmd43, RequestCmd134, AssignWorkersToBuilding,
ClearDarkCorner, SuperviseStammtisch, UpdateBuildingHealthState, then NullTick.
Source returns the first six in exact order; NullTick omitted as documented. ✓

---

## favorability.cpp

### ComputePersonFavorability — gilde.exe 0x594330 — VERIFIED-1:1
Diffed against the full pseudocode + key disasm spots:
- self==other -> 100.0 ✓
- (1) self workstation workers; (2) office.kind==4 adds QueryByGoodType(other)
  workers; (3) title 30..33 -> mapped {23,24,25,26}, gate `(rec[90]&1)==0`, adds
  workers. ✓
- (4) `relTerm = (*(int*)(&dword_123D6CD[192*self]+other) >> 24) + 127;
  v40 = relTerm * (1/255) * 100 + v39` (0x59444a/0x594465). Source `(relationByteSelf
  >> 24)+127` — field holds the RAW dword; the scorer shifts. Header comment was
  misleading ("…>>24") and is corrected; all tests/backends already feed the raw
  dword (e.g. 0x14000000 -> 20). ✓
- (5) law branch: weight from OTHER's office def `*(float*)&def[2]`; `*0.75` when
  Gesetz==2; same factionHigh -> `ClampLaw(v40+w)`, else `ClampLaw(v40-w)`. ClampLaw
  = `(v<0 || 100>=v) ? max(v,0) : 100` — exact (0x5947f6 / 0x594784). ✓
- (6) office-tier inventory bonuses gated by OTHER's officeId; tier 1..3 -> slots
  341,349 *10; tier 4..7 -> 343*10, 351*8; tier 8..9 -> 345*7, 357*7, 358*15. All
  multipliers confirmed byte-exact vs the .rdata floats. ✓
- (7) spouse bonus: index math `4*(4*(self+16*self)-self)` collapses to 268*self =
  self's inventory base; slot 363 *7. ✓
- (8) guild-rank penalties: `(v18&0x1C000)!=0 && rankHigh(self)!=rankHigh(other)` ->
  `-3*((u32)(gb<<15)>>29)`; `(gb&0x1800000)!=0` -> `-3*((u32)(gb<<7)>>30)`. Shifts
  exact (0x594676 / 0x5946bf). ✓
- (9) final clamp `(v40<100 && v40<=0)->0; v40>=100 ->100; else v40` ✓
- Constants byte-exact (`get_bytes`): 1/255@0x626AA4, 100@0x626AA8, 0.75@0x626AAC,
  7@0x626AB0, 15@0x626AB4, 10@0x626AB8, 8@0x626ABC, 0.01@0x626AC4.
Per-person strided field reads + office/inventory/law leaves are correctly hooked
through FavorabilityEnv (documented BOUNDARY). No source-logic churn.

### AverageObjectFavorability — gilde.exe 0x594928 — VERIFIED-1:1
`for k<count: if ids[k]!=-1: pid=FindRecordById; if pid: ++resolved; sum +=
ComputePersonFavorability(self, pid, applyLaw=0)`. No resolve -> 0.5; else
`(float)(sum * 0.01 / resolved)` — evaluation order (sum*0.01 then /resolved)
matches the x87 sequence. ✓

---

## Build / test
- Built: desire_table_test, ai_favorability_needs_test, ai_needs_test,
  audio_voice_favor_test, ai_meister_workstation_test, ai_meister_trade_test — all OK.
- `GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R "desire|favor|needs|meister"`
  -> **40/40 passed**.
