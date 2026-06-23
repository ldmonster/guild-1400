# Hardening sweep — sim chunk B (command_recon4 resolve + senders)

Files owned:
- src/sim/command_recon4_resolve.cpp  (10 ResolveTarget* handlers)
- src/sim/command_recon4_senders.cpp  (Send*/Enqueue*/Combat* packet emitters + law table)

MCP LIVE; every provenance-tagged function decompiled at its 0xADDR and diffed
line-for-line vs the binary (disasm used as reference of record where Hex-Rays is
garbled). All float->int sites and every table/constant confirmed with
get_bytes / get_global_value.

## command_recon4_senders.cpp

| Function | Addr | Verdict |
|---|---|---|
| CommandRetZero | 0x493a08 | VERIFIED-1:1 (const return 0) |
| SendEntityActionA | 0x5675ac | **FIXED** (u16 typeWord index) |
| SendEntityActionB | 0x567944 | VERIFIED-1:1 |
| SendEntityActionC | 0x567e24 | VERIFIED-1:1 (within emit abstraction) |
| SendEntityActionD | 0x56801c | VERIFIED-1:1 (within emit abstraction) |
| SendMapEntityAction | 0x568278 | BOUNDARY (per-slot owner-ptr scan) |
| QueueRequestTransform64 | 0x4952b4 | VERIFIED-1:1 (all int/float delta offsets exact) |
| kLawActionTable | 0x4c1810 | VERIFIED-1:1 (51×40B byte-confirmed) |
| EnqueueLawAction | 0x4c2218 | VERIFIED-1:1 (ConvertX trunc confirmed) |
| FormationPass/CombatSetUnitFormationMode | 0x48980c | VERIFIED-1:1 (opcodes 342/344/352, delta 0xA8/0xD2) |
| EnqueueDuelChallenge | 0x538224 | VERIFIED-1:1 (incl. preserved find-loop defect) |

### FIXED — SendEntityActionA (0x5675ac)
- Before: `u8 w = (u8)g_hooks.recTypeWord(tgt); tblKind(w); tblId(w);`
- After:  `u16 w = (u16)g_hooks.recTypeWord(tgt); tblKind(w); tblId(w);`
- Evidence: disasm 0x567745 `byte_12CE912[536 * *(u16*)(v7+39)]` and 0x567792
  `dword_12CE914[134 * *(u16*)(v42+39)]` index by the FULL u16 typeWord. The (u8)
  truncation dropped the high byte, mis-indexing tblKind/tblId for any typeWord
  >= 256. (SendEntityActionB already used the full u16 — only A was wrong.)

### VERIFIED notes
- **EnqueueLawAction (0x4c2218)** — float->int: 0x4c22de `call ConvertX` (RC forced
  to truncate) + `fistp` → `(i32)take` matches truncation. flt_61E584 confirmed
  = 0x437C0000 = 252.0f. stat read `fild word ptr` of zero-extended u8, roll
  `fild word ptr`, compare `fcomp/jnb` → `(fRoll<fMax)?fRoll:fMax`. arg<=4 guard
  is unsigned (`*(WORD*)v6+1 > 4u`). GroupFromCode compare: `movsx al` vs
  zero-extended arg word — `(i32)(i8)group == (i32)e.arg`. RNG: rand%8+8, then
  rand%4 per guild-group match. Loop stride 0x218 over 768 slots. All 1:1.
- **QueueRequestTransform64 (0x4952b4)** — int deltas at copy/slot +0x10/+0x14/
  +0x18 (two's-complement wrap), float deltas at +0x20/+0x24/+0x2C/+0x38 via plain
  x87 fsub (single binary32 rounding, exact). Packet field offsets 0x00/0x10/0x11/
  0x13/0x17/0x1B/0x1F/0x23/0x27/0x2B all match the disasm stores exactly.
- **CombatSetUnitFormationMode (0x48980c)** — Hex-Rays garbled; verified against raw
  disasm. mode!=0 stores zext mode at +0x110; mode1 = 2×(op 0x156=342, δ0xA8);
  mode2/3 = (op 0x158=344 then op 0x160=352, δ0xD2). AdjustStockAndNotify arg =
  -*(i32)(unit+0x24) (neg edx), prot = *(u16)unit; st0 discarded (fstp st).
  AppendDeltaField slotOff = LOWORD(unit)+0x83-LOWORD(dword_11AA474) (reported as
  the rel 0x83). kFormOp* constants confirmed in combat_drivers.h (342/344/352).
- **EnqueueDuelChallenge (0x538224)** — packet field offsets 0x26=0xA2, 0x14=-1,
  0x30=2, 0x08=3, 0x0C/0x34/0x38 ids, 0x7C=9; -1 fill of +0x80..+0xFC via the
  pre-increment loop (verified equivalent to off=4..0x40); challenged trio at
  +0x100; mode==2 → calls 2 then 1; +0x10C opponent id; duel-group scan (768 rows,
  stride 536, +0x16C owner ptr == rec, max 4 ids at +0xC0..0xCC); 14-byte clock at
  +0x18 advanced +15 min. Original find-loop defect (IterNext preserves ECX, stale
  rec re-tested) preserved 1:1.

### BOUNDARY — SendMapEntityAction (0x568278)
The original scans ALL 768 person rows (stride 536) and, for EVERY row whose
`dword_12CEA7C[i]` (owner-record-pointer column) == the resolved target, emits a
BeginDeltaPacket/AppendDeltaField(1,1,value,453)/QueueRequestState22, where
value = (byte_12CEAD5[i]%2 + byte_12CEAD5[i]>>1). The reconstruction emits a
SINGLE delta keyed on the target's own typeWord instead of the per-row scan,
because the owner-pointer column (dword_12CEA7C) and the value column
(byte_12CEAD5, +0x1C5) are live game-state columns NOT exposed by the hooks
struct. Faithful reconstruction needs two new row-column hooks (handoff: extend
Recon4SenderHooks with tblRowOwnerRec-style + byte_12CEAD5 accessors and replace
the single emit with the scan). Return value (`ok`) matches. Documented, not
silently approximated.

## command_recon4_resolve.cpp

| Function | Addr | Verdict |
|---|---|---|
| ResolveTargetClergy | 0x4f9238 | VERIFIED-1:1 |
| ResolveTargetPersonByName | 0x4f9518 | **FIXED** (mode-2 fallback gate) |
| ResolveTargetPersonAlt | 0x4f989c | **FIXED** (mode-2 fallback gate) |
| ResolveTargetPersonScoped | 0x4f9c20 | VERIFIED-1:1 |
| ResolveTargetBestRated | 0x4f9f74 | VERIFIED-1:1 (filter is garbage-edx, doc'd) |
| ResolveTargetCraftWorker | 0x4fa50c | VERIFIED-1:1 |
| ResolveTargetByStatGroup | 0x4fa818 | VERIFIED-1:1 (bubble + constants exact) |
| ResolveTargetByProfessionRange | 0x4fac80 | VERIFIED-1:1 |
| ResolveTargetWoundedPerson | 0x4faab8 | VERIFIED-1:1 (filter is garbage-edx, doc'd) |
| ResolveTargetRandomCarried | 0x4faf54 | **FIXED** (inner-loop advance/bail order) |

### FIXED — ByName / Alt mode-2 fallback gate (0x4f9722 / 0x4f9ab7)
- Before: person arm gated on a sticky `bool seekingPerson = true;` that was never
  updated → the person arm ran on EVERY iteration, so a later qualifying person
  OVERWROTE the fallback (last-wins).
- After: gated on `fallback == -1`.
- Evidence: in the binary the carried-arm condition is `v19 || !IsPersonType ||
  role-invalid`; once v19 (fallback) is set the person `else` arm is never re-
  entered, so the fallback is the FIRST qualifying person (first-wins). The two
  functions use a single inline `while(1)` (NOT the goto-retry shape that Clergy/
  Scoped/CraftWorker use), so the correct guard is `v19==0`, i.e. `fallback==-1`.
  (Clergy 0x4f9238 / Scoped 0x4f9c20 / CraftWorker 0x4fa50c use the goto LABEL_x
  retry structure where the source's resettable `seekingPerson` is already 1:1 —
  left unchanged.)

### FIXED — RandomCarried inner walk bail order (0x4fb040)
- Before:
  `while(fail){ if(--count==0) goto next; i=(i+stride)%768; }`
- After (matches disasm `--v11; v13=(v13+v12)%768; if(!v11) goto LABEL_8`):
  `while(fail){ --count; i=(i+stride)%768; if(count==0) goto next; }`
- Evidence: the original ADVANCES i before the count==0 bail, so on exhaustion i
  has wrapped 768 times (i == start), and that value feeds the outer
  `while(v13+1<768)` retry decision. The old order advanced only 767 times
  (i == start-stride), changing whether/when the outer do/while repeats. Success
  path is unchanged (existing golden test RandomCarriedPicksCarried still holds:
  it starts at 767, matches immediately, ends at 767 → single pass).

### VERIFIED notes / documented indeterminacies
- **Clergy/Scoped/CraftWorker mode 2** — the goto LABEL_x person/carried alternation
  (skip the carried test on a person hit; re-test person only while v19==0) is
  reproduced exactly; person∩carried = ∅ so the skipped same-index carried test is
  always-false (harmless).
- **ByStatGroup / ByProfessionRange** — top-5 wealth pool init constants confirmed:
  dword_4F8BF0/4F8C10 = 5×0xFF676980 (−10,000,000); dword_4F8C04/4F8C24 = 5×0xFFFF
  (i16 −1). The descending bubble (keys[j+1]>keys[j] swap, j=3..0, parallel i16
  slot swap) is byte-offset-exact; ByProfessionRange's skip-optimized variant is
  equivalent to ByStatGroup's always-compare variant. profOk ranges confirmed
  ([19,69] minus [31,33]/[40,45]/[52,57]; CraftWorker [13,18]; Clergy rank
  [0x1E,0x21]). Pick = RandomModulo(5); record fetched via word_12CE910[268*
  slotWord] (typeWord used as record index — identity-keyed table; modeled by a
  typeWord-scan for the back-write, documented abstraction).
- **BestRated / WoundedPerson group filter** — the binary reads an UNINITIALIZED edx
  (`var_14 = edx` at 0x4fa004 / 0x4fab54; comparand `var_18 = (val==edx)?0:edx`)
  and compares `(dword_12CE914+2)[i] sar 24` (byte +9, the flag9 column — NOT
  entityId>>24). Both the filter-active flag and comparand are genuinely garbage-
  dependent. The reconstruction keeps the documented best-effort approximation
  (filterActive = val!=0, groupKey 0); the same `val==val` tautology marker is in
  ByStatGroup's predicate-selection (v11==v12 arm). Not a fixable divergence — the
  binary itself consumes uninitialized state. Core scan/scoring/return paths are 1:1.
- **dword_4F8C30** = {3,5,7,11} (RandomCarried strides) confirmed.

## Counts
- Functions audited (this chunk): 21 (11 senders + 10 resolvers).
- VERIFIED-1:1: 16
- FIXED: 4  (SendEntityActionA u16; ByName mode-2; Alt mode-2; RandomCarried bail order)
- BOUNDARY: 1 (SendMapEntityAction per-row owner-ptr scan)
- Documented garbage-edx indeterminacies: BestRated, WoundedPerson, ByStatGroup
  predicate arm (match the existing documented approximation; no code change).

## Build / tests
- Both owned files compile clean (`g++ -std=c++17 -Wall -Wextra -fsyntax-only`),
  only the pre-existing intentional `val==val` tautological-compare warning.
- Full `guild` lib link currently blocked by an UNRELATED, out-of-chunk compile
  error in src/gui/widget_layout.cpp (`Widget::ld<>` member missing) — HANDOFF to
  the gui owner; not touched.
- No API/signature changes were made, so the existing golden tests
  (command_recon4_resolve_test, command_recon4_senders_test/-senders2_test) stay
  valid; none exercise the count-exhausted RandomCarried edge or mode-2 fallback
  paths that were fixed, so no golden vector encoded the old (wrong) behavior.
- NOTE: an external revert during the session reset both files to an earlier state
  twice; all four fixes were re-applied and re-verified against the file on disk.
