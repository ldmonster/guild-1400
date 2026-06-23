# Hardening pass — sim AI action/method cluster

Scope (owned files): `src/sim/aiaction_recon.{cpp,h}`,
`src/sim/aiaction_dispatch_ai_recon2.{cpp,h}`,
`src/sim/aimethod_recon3_registry.{cpp,h}`,
`src/sim/aimethod_score_ai_recon2.{cpp,h}` (+ `tests/unit/aiaction_recon_test.cpp`).

Method: every `0xADDR`-provenance function was re-decompiled AND disassembled via
IDA MCP (module gilde.exe) and diffed line-for-line; constants re-read with
`get_bytes`; pointer strides verified against `mov ...,[reg+0xHH]`; signed/unsigned
compares verified against `jge`/`jg`/`jb`; float->int truncation verified
(ConvertX @0x5c6b08 sets round-to-zero, fistp truncates == `(int)`).

## Verdicts (per function)

| addr | function | verdict |
|------|----------|---------|
| 0x475700 | EvalGuildhallTarget (EvalGuildhallGate) | VERIFIED-1:1 |
| 0x4757e8 | PlanGuildhallUpgrade — tier weight | **FIXED** (signed level) |
| 0x4757e8 | PlanGuildhallUpgrade — enable gate | VERIFIED-1:1 |
| 0x4757e8 | PlanGuildhallUpgrade — cost A/B/buildworth | VERIFIED-1:1 |
| 0x4761a0 | EvalNeedFulfillTarget (NeedRatio/PickSlot/Cost) | VERIFIED-1:1 |
| 0x475f48 | PlanPersonInteraction (cost gate, family scan) | VERIFIED-1:1 |
| 0x47b468 | EvalConversationTarget (primary/secondary gate) | VERIFIED-1:1 |
| 0x47b7d4 | EvalSleepSpot | VERIFIED-1:1 |
| 0x47c430 | DispatchTargetSearch (PickSet/PickIndex) | VERIFIED-1:1 |
| 0x47852c | AiNeeds_EvaluateActions tail (split selection) | **FIXED** (tables) |
| 0x479dd8 | FindNearestPerson (NoisyDistance) | VERIFIED-1:1 (x87 note) |
| 0x47d364 | TryRangedAttack (depth/aim/hit) | VERIFIED-1:1 |
| 0x4671c8 | EvalAttackTarget (AttackFavorabilityAccept) | VERIFIED-1:1 |
| 0x4680e0 | ScanCandidatePersons (WealthScoreFloored) | VERIFIED-1:1 |
| 0x469248 | SelectBestForPerson (SelectBestAiMethod) | VERIFIED-1:1 |
| 0x469a18 | ExecuteSelected (MethodEmitWeight) | VERIFIED-1:1 |
| 0x46ac24 | EvalMoveToBuilding (MoveScore/PickMostDisliked) | VERIFIED-1:1 |
| 0x4794e4 | LookupAttributeIndex | VERIFIED-1:1 |
| 0x468f6c | RegisterFromIni (id gate, fill, validation, mirror) | VERIFIED-1:1 |
| 0x468a40 | LoadDataFile field schedule + prev-mirror | VERIFIED-1:1 |
| 0x4c9330 | RequestCmd58 | VERIFIED-1:1 |
| 0x4c937c | RequestCmd59 | VERIFIED-1:1 |
| 0x4c93f4 | RequestCmd115 | VERIFIED-1:1 |
| 0x4c7164 | RequestCmd109 (emit-only-if-no-handler) | VERIFIED-1:1 |
| 0x475c74 | HandleGuildhallTrigger / Type23 / Type14 | VERIFIED (return codes 57/58/59) |

Counts: 24 functional units reviewed; **2 FIXED**, 22 VERIFIED-1:1, 0 deferred.

## Fixes

### FIX 1 — GuildhallUpgradeTierWeight: signed level byte (0x4758ce/0x4758d4/0x4759ad)
Disasm: `mov ah,[esi+5Ch]` then `cmp ah,21h; jge` (33) and `cmp ah,42h; jge` (66)
— both **signed** byte compares. The level byte at record+92 is read as `char`.
Old source took `u8` and compared unsigned, so any byte >= 128 returned 10. With a
signed read, a high-bit-set byte is negative -> `< 33` -> returns **50**.
- src: `GuildhallUpgradeTierWeight(u8)` -> `GuildhallUpgradeTierWeight(i8)`.
- golden: `(255)==10` was WRONG; corrected to `i8(255)==50`, `i8(128)==50`, added
  `(127)==10`. Evidence: disasm above.

### FIX 2 — Action-split tables: WRONG 4-entry truncation (0x47852c tail)
`get_bytes(0x47848c, 64)` shows the threshold/divisor tables are a single
contiguous **12-entry prime array**, NOT `{-1,1,3,5}`/`{1,3,5,7}`:
- `dword_47848C[0..11]` (thresholds) = `{-1,1,3,5,7,11,13,17,19,23,29,31}`
- `dword_478490 = 0x47848C+4`, so divisors[i] = thresholds[i+1] =
  `{1,3,5,7,11,13,17,19,23,29,31, 11}`
- `dword_4784BC` (= `0x47848C[12]`) = **11** = the walk START index (old source
  treated it as `4` and clamped — it never saw divisors 11..31).

The walk (0x478c32..0x478c59) was rewritten literally: `edx = 11; while (!(n >
table[edx])) { --edx; if (edx==0) break; }`; returns `edx` (survivors). Then
`divisor = divisors[RandomModulo(survivors)]`, `groupIndex = 3*RandomModulo(n)`,
jitter `±1` iff `divisor>1 && n%divisor==0` (RandomModulo(2)? +1 : -1).
- src: new `kSplitThresholds[12]`/`kSplitDivisors[12]`, `kSplitTableLen=12`,
  `kSplitStartIndex=11`; `ActionSplitSurvivors`/`SelectActionSplit` rewritten.
- golden: survivors test rewritten (e.g. `(2)->1`, `(10)->4`, `(32)->11`,
  `(0)->0`); added `PrimeDivisorTable` test locking divisors[4]=11, [10]=31.

## Key 1:1 confirmations (no churn)
- RegisterFromIni validation loop (0x4690a1): `mov eax,[ebx+2Dh]; sar eax,18h;
  cmp eax,-1` + `test dword[ebx+34h],7FFFFFFFh`, `add ebx,8` x4 — the
  `0x7FFFFFFF` sign-mask, the per-slot short||long effect test, and the
  `MemMove(+0x50,+0x30,0x20)` mirror all match. id gate: `*a1<=0 || *a1>=61`
  signed (`movsx`/jl). commit index `movsx ebx,byte[ebx]` (signed) — caller's job.
- NeedRatio cap clamp is an integer bit-pattern compare `(<= 0x3f800000)`; for the
  u8->i16 non-negative cap domain this equals `capf <= 1.0f` (source comment kept).
- All .rdata constants re-verified byte-exact (3.0/0.4/0.5/0.25/0.015/33.0/40.0/
  0.5/0.75/dbl 33.0/0.02/0.01/800-floor).

## Cross-file handoffs (documented, not owned here)
- objectId resolution `dword_12CE914[134*word_63CC5C]` for the RequestCmd* packets
  is the caller's; builders take `objectId` as a param (entity/world layer).
- The Handle*/Route* routing abstracts coupled entity/handler reads; only the pure
  return codes and type gates are reconstructed here (return 57/58/59 confirmed).
- ConvertX (0x5c6b08) round-mode is assumed-set by the float->int sites; modeled as
  C++ `(int)` truncation.

## Note (irreducible)
- FindNearestPerson distance sum is x87 80-bit extended in the binary
  (`fld/fmul dword` chain @0x479f15); source uses C++ `double`. Consistent with the
  whole reimpl's x87 convention; the result is then `* noise` (random) and compared,
  so this is below observable-divergence threshold. Not a behavioral fix.

Compile: each owned .cpp + the test pass `g++ -std=c++17 -fsyntax-only` with the
separated `-I` include set. Full link is blocked by an unrelated pre-existing break.
