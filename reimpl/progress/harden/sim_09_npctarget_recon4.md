# Harden — sim_09 npctarget + npcevent_recon4 (target/reaper)

1:1 line-for-line hardening pass over the 3 files, every provenance'd function
DECOMPILE + DISASM diffed against the binary (DISASM authoritative). IDA Pro MCP,
module `gilde.exe`, imagebase 0x400000.

Files:
- `src/sim/npctarget.cpp`
- `src/sim/npcevent_recon4_eval_target.cpp`
- `src/sim/npcevent_recon4_reaper_move.cpp`

Build/run (all 6 green):
```
cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original \
  ctest -R 'npctarget|npcevent_recon4|reaper' --output-on-failure
# 385 npcevent_reaper_full_test         Passed
# 386 npcevent_recon4_eval_target_test  Passed
# 387 npcevent_recon4_reaper_move_test  Passed
# 388 npctarget_boundary_test           Passed
# 389 npctarget_golden_test             Passed
# 455 real_reaper_wiring_test           Passed
```

---

## Constants (get_bytes — every constant verified)

reaper_move (`0x61EFE0` block):
| addr | bytes | value | use |
|------|-------|-------|-----|
| 0x61EFE0 | 00 00 a0 42 | 80.0f | height bias (approach src/dst y) |
| 0x61EFE4 | 00 00 a0 42 | 80.0f | height bias (move-toward d.y / clamp) |
| 0x61EFE8 | 00 00 34 42 | 45.0f | arrival distance |
| 0x61EFEC | 00 00 80 3e | 0.25f | game-speed step coef |
| 0x61EFF0 | 00 00 00 3f | 0.5f  | step base coef |
| 0x61EFF4 | b2 9d ef a7 c6 4b ef 3f | **0.978 (DOUBLE, 8 bytes)** | hop arc up |
| 0x61EFFC | cd cc cc 3d | 0.1f | hop arc forward |
| 0x61F000 / 0x61F004 | 00 00 a0 42 | 80.0f | cache-pose / sound-pos y bias |

npctarget (`0x61A680` block):
| addr | bytes | value | use |
|------|-------|-------|-----|
| 0x61A680 | 00 00 00 41 | 8.0f  | FindNearestEnemy reject slope |
| 0x61A684 | 00 00 50 42 | 52.0f | FindNearestEnemy reject base |
| 0x61A688 | 00 00 84 42 | 66.0f | SeqA accept threshold ("<") |
| 0x61A68C | 00 00 04 42 | 33.0f | SeqB accept threshold (">") |

All source constants matched.

---

## FLOAT FINDINGS

### FIXED — hop-arc 0.978 was truncated to float (real divergence)
`reaper_move.cpp` ReaperMoveTowardTarget @0x4d9219..0x4d9238.

Binary (disasm):
```
4d920f  fmul ds:dbl_61EFF4     ; stepScale * 0.978  (0.978 is a DOUBLE, no f32 trunc)
4d9215  fadd [var_60]          ; + newPos.y, result -> f32 store
...
4d921d  fmul ds:flt_61EFFC     ; fwd = stepScale * 0.1f   (kept on x87 stack, 80-bit)
4d9225  fmul [var_74]          ; fwd * d.x     (no f32 spill of fwd)
4d9229  fadd [var_84]          ; + reaperPos.x  -> f32 store (newPos.x)
4d9230  fmul [var_6C]          ; fwd * d.z
4d9234  fadd [var_7C]          ; + reaperPos.z  -> f32 store (newPos.z)
```
Old source did `r.stepScale * (f32)kReaperHopArcUp` — `(f32)0.978 == 0.97799998...`,
which is NOT what the binary multiplies (it uses the full double 0.978). Old source
also stored `fwd` as `f32`, truncating the extended-precision intermediate.

Fix: keep 0.978 as double, compute `fwd` as double, form the x/z products in double
before the final f32 store (modeling the x87 80-bit chain as `double` per the
x87-as-double rule). The existing golden tolerances (0.2 / 0.05) already covered the
~2e-5 delta, so goldens stay valid; correctness now matches the binary exactly.

### dt / stepScale time math — VERIFIED 1:1
@0x4d912d..0x4d917f. `lastTick` and `gameTick` are loaded via `fild m64` (high dword
forced to 0) → both non-negative; `dt = (gameTick - lastTick)` formed in 80-bit then
stored to f32. `gameSpeed` via `fild m32` (signed) then `*0.25 + 0.5` (both float
constants) `* dt`, stored to f32. Source models `(f32)(((f64)(u32)gameSpeed*0.25 +
0.5)*(f64)dt)` and `(f32)((f64)gameTick - (f64)lastTick)`. Equivalent for the
non-negative tick/speed domain. Note: `fild` of gameSpeed is signed-32; source uses
`(u32)` — identical for the small positive speeds this ever takes.

### arrival code — VERIFIED 1:1
@0x4d90c0 `fcomp flt_61EFE8(45.0)` / `setb` / `inc` → code = (planarDist<45) + 1
(2=arrived, 1=moving). Source matches; `within` drives the early-return (no move).

### planar distance y term — VERIFIED 1:1
@0x4d903f `var_70 (d.y) = 0`, sqrt is `dx*dx + d.y*d.y + dz*dz` with d.y==0. Source
writes `0.0f*0.0f` — numerically identical (value 0).

### VectorNormalize @0x5cb148 — VERIFIED 1:1
`v3 = sqrt(x*x+y*y+z*z)` (x87 faddp order matches source left-to-right); zero test is
`bits & 0x7FFFFFFF` (epsilon = exact zero, no fudge constant); else `1.0/v3` then
in-place scale (y,z computed to temps first, then x,y,z stored — matches source temp
ordering). retn through eax = result ptr.

### Approach/CachePose/SoundPos delta+sqrLen — VERIFIED 1:1
- Approach else (first-spawn) @0x4d8c34: delta = dst - src; spawnPos = src; sqrLen =
  dx*dx+dy*dy+dz*dz (@0x4d8da3). Matches.
- CachePose @0x4d92a4: delta = target - reaper (@0x4d9370/80/90); sqrLen @0x4d93ba.
  Matches.
- SoundPos @0x4d9440: delta = target - reaper (@0x4d9517/27/38); sqrLen @0x4d955a.
  Matches.
All three accumulate in the same x87 faddp order as the source.

---

## EvalBestPersonTarget @0x475dd0 — VERIFIED 1:1

Recovered from disasm (Hex-Rays declined locals). Diffed branch-for-branch:
- earlyOut(a4) → write sentinel 0xF149F2CA to *out0,*out1, return 0. (@0x475e72)
- pre-check (action[+2]!=5): FindRecordById(action[+0x5C]); null→loop; rec.type==5→
  return 0; rec.flag9!=0→loop; else IsActiveType, active→return 0, else loop.
- candidate loop: ecx walks ebp+0x0C..ebp+0x20 step 4, reads [+0x5C] ⇒ action offsets
  0x68,0x6C,0x70,0x74,0x78 (5 slots). Per rec: null skip; `cmp [+8],0/jbe`
  (activeCount==0 skip); `(fild (u16)rank+4) fcomp [+0x20] / jb` (rank+4<threshold
  skip); `[+0x164]!=0` (busy) skip; He_FindFirstHandler(2,2,(u16)[+0],0,0x5E)!=0 skip.
- selection: first sets best; else `cmp (u16)rec.rank > (u16)best.rank` via `jg`
  (signed, but both zero-extended u16 ⇒ same as unsigned). best=rec on strictly-greater.
- best==null → return 0.
- score: *out0=*out1=0.0 before the call; scorer is `__userpurge(out0@eax, out1@edx,
  action@ecx, relArg@bl, a5,a6,a7 stack)`. The `mov ebx,[var_14+1]; sar ebx,18h` idiom
  loads relArg (the byte at +0x10) sign-extended into bl — that is the only meaningful
  scorer arg. a5/a6/a7 stack args are DEAD in 0x4796b0 (confirmed by decompile — it
  only reads a4). Source forwards relArg faithfully and passes throwaway values for the
  dead a5/a6/a7 — behavior-identical.
- final gate: `gate = (fild (u16)rank+4) >= [+0x20] ? 1 : 0` (setnb), then
  `*out0 = *out0 * gate; *out1 = gate * *out1`. Operand order + (0/1) gate match. retn 1.

No source change needed.

---

## npctarget.cpp

### PickDirectionSeqA @0x474fe8 — VERIFIED 1:1
- init dir[0..5] = 1..6.
- category precedence +360 → +358 → +359 (matches source PickCategory CatC/CatA/CatB).
- tier = (BYTE2(officeDef)-1)/3: tier1 = rotate-by-4 `dir[i]=(4+i)%6+1`; tier2 =
  reverse `dir[i]=6-i`. (GetDefinition fail ⇒ return 0.)
- shuffle: 6 rounds, each = 4 RandomModulo(3) draws in order
  (a,b → swap dir[a],dir[b]; c,d → swap dir[c+3],dir[d+3]). Draw count+order match.
- accept: for k=0..4, category = dir[k] (HIBYTE(dword@&dir[k]-... = dir[k]); confirmed),
  CollectByCategoryResolved(dir[k],6,buf); if n>=3, sum Fav(selfKind, id.kind) over all
  n (selfKind = first u16 of person), accept first where `sum/(double)n < 66.0`. Match.
  (Original buf stride is 24 bytes, id at +4 — abstracted by the `collectByCategory`
  hook which returns the same ids in the same order.)

### PickDirectionSeqB @0x475274 — VERIFIED 1:1
Same build/shuffle/tier core as SeqA. Accept rule differs:
- outer i in 0..n-2 (`v36=n-1`, guarded `if (n-1>0)`); skip ids[i]==selfId(*(person+4))
  or ids[i]==-1; fetch pi.
- inner j in i+1..n-1; skip if pi null (binary breaks the inner loop on first j ⇒
  equivalent to source `if(!pi) continue`); skip ids[j]==selfId or -1; fetch pj, skip
  if null.
- per valid pair: `sum += Fav(pi.kind, pj.kind); sum += Fav(pj.kind, pi.kind); pairs+=2`.
  SeqB uses pi/pj kinds, NOT selfKind (source matches — selfKind unused here).
- accept first dir where `pairs && sum/(double)pairs > 33.0`. Match.

### EvalCombatOrMoveAction @0x475638 — VERIFIED 1:1 (with documented leaf boundary)
- gate `if (blocked || busy || rank != 1) return 0`. Binary calls
  `VIBE_Amt_CheckGuildRankLevel3(edx)` and compares == 1; source takes the resolved
  `guildRank` int as a parameter (CheckGuildRankLevel3 @0x481cf0 is a coupled leaf —
  caller supplies the rank). Documented in header.
- subMethod = person[+361]. 30 → FindNearestEnemy, emit verb=7(+0), mode=1(+16),
  target=enemy.id(+4) [`*(_DWORD*)(enemy+4)`], return 56. 32 → SeqA, verb=22, mode=4,
  target=dir. 31/33 → SeqB, same emit. else return 0. Frame offsets (+0/+4/+16) match
  NpcActionDesc. Match.

### FindNearestEnemy @0x474dd4 — GENUINE BOUNDARY (documented model, unchanged)
The reject curve `(double)count*8.0 + 52.0 < bestScore` (constants 8.0/52.0 verified)
and the lowest-favorability minimization are reproduced 1:1. However the binary's
*first* path (taken when person[+358]==0) is a raw walk of the global Person ring
`word_12CE910` stepping 268 words/record, gated by VIBE_Office_GetHolderEntryByCity
and `BYTE2 > 1` — engine globals + leaves not available in the headless build. The
existing reconstruction models that ring via the `collectSuccessors` hook (reduces to
the same lowest-favorability + reject-curve selection once the candidate set is
provided) and keeps the visible-enemy fallback (FindMatchingColors / radius [0,25] →
personByIndex). This is an explicit hook boundary, not a 1:1 line translation of the
ring walk; left as-is (rewriting it faithfully requires the live person table + the
two Office/ObjectSearch leaves). The favorability math, the count-based reject curve,
and the fallback are exact.

---

## Net change
- `reaper_move.cpp`: fixed the hop-arc to use double 0.978 (was `(f32)0.978`) and to
  carry `fwd` at extended (double) precision before the final f32 store — matches the
  x87 chain at 0x4d9209..0x4d9238. Cite: dbl_61EFF4 is an 8-byte double; `fmul`
  consumes it directly with no f32 conversion, and `fwd` is never spilled to a 32-bit
  slot.
- All other functions VERIFIED-1:1; no source edits.
- 6/6 tests green.
