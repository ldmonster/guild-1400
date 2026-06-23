# Harden report — src/sim/ai_meister_calc_angriff.cpp

Target binary: gilde.exe (imagebase 0x400000). Reference of record: IDA decompile +
disasm of `VIBE_Ai_CalcAngriff @0x4569a8`.

## Counts
- Functions with provenance in file: **1** (`CalcAngriff` @0x4569a8)
- VERIFIED-1:1 sub-regions: most of the function (see below)
- FIXED: **2** divergences (1 offset bug, 1 guard threshold bug) + 4 stale tests/comments
- BOUNDARY: 1 (documented null-deref hardening, behavior-preserving)

---

## CalcAngriff @0x4569a8 — FIXED (2 bugs), otherwise VERIFIED-1:1

### Constants — VERIFIED-1:1 (get_bytes/get_global_value)
- flt_619528 @0x619528 = `0x3e000000` = **0.125f** → `kTileBWeight` ✓
- dbl_619530 @0x619530 = `0x3fe0000000000000` = **0.5** → `kSecScale05` ✓
- dbl_619538 @0x619538 = `0x3f847ae147ae147b` = **0.01** → `kRandScale001` ✓
- flt_619540 @0x619540 = `0x41000000` = **8.0f** → `kSecBase8` ✓

### Float→int / int→float sites — VERIFIED-1:1 (disasm 0x456c2c..0x456c84)
- secScore path: `fild [int]; fsubr flt_619540; fmul dbl_619530; fmul; fstp [float]`.
  No float→int truncation site; all conversions are int→double via `fild` after the
  RNG result is masked `& 0xFFFF` and stored as int. Reconstruction's
  `static_cast<int>(static_cast<u16>(RandomModulo(..)))` then double math + `(float)`
  store mirrors the `fstp [esp+var_34]` single-precision truncation exactly. ✓
- RandomModulo(0x64) then RandomModulo(0x32): draw COUNT and ORDER verified. The
  0x32 draw is short-circuited (only when `j != null`, disasm 0x456d40 `jz` skips the
  `mov eax,32h; call RandomModulo`). Reconstruction's `if (j) { ...RandomModulo(0x32) }`
  matches. ✓

### Strides / loop bounds — VERIFIED-1:1
- object stride `add ecx,0A9h` = 169 ✓; object count `cmp edi,100h` = 256 ✓
- building type-def stride 589 ✓ (lea/shl/sub at 0x456cca)
- person stride `add eax,218h` = 536 ✓; person cap bound `(int)v32 < 411648` ✓
- danger grid: inner `v4 += 192`, terminator `v3` (1536 + 24*i); dangerB at base+2
  (word_12349A2), dangerA at base+0 (word_12349A0). ✓
- He handler match field `*((_DWORD*)k+43)` = byte 0xAC ✓; j strength `[edx+37h]` = byte
  55, zero-extended, signed `jge` vs rng+50 ✓.

### Branch logic — VERIFIED-1:1
- Target-validation block, LABEL_2/LABEL_3/LABEL_26 flow, type gate {19,4,16},
  owner/0xFFFF/flag90/local-player filter, He cascade (64→63→73, first non-null wins),
  attack budget<3 gate, spy-vs-attack split, *(mr+448) writes (LABEL_26 store, attack
  clear, spy no-clear). All match.

---

### FIX 1 — QueryFind match field offset (j+21 → j+42)
- **Site:** loc_456D2B, disasm `mov eax, [edx+2Ah]` (byte offset **0x2A = 42**).
- **Cause:** Hex-Rays declares `j` as `__int16 *`, so its `*(_DWORD*)(j + 21)` is
  pointer arithmetic on a 2-byte element = **byte offset 42**, not 21. The reconstruction
  read byte offset 21.
- **Before:** `i32 nodeId = rd32(qfResult, 21);`
- **After:**  `i32 nodeId = rd32(qfResult, 42); // *(_DWORD*)((char*)j + 0x2A)`
- **Evidence:** disasm @0x456d2b `mov eax,[edx+2Ah]; cmp eax,[ebp+1]` (j+0x2A vs a3+1).
- **Impact:** Without the fix, the type-202 scene-node-for-target lookup never matched,
  so `j` was always effectively null → the function always took the SPY path and never
  the ATTACK path. This is a behavioral divergence affecting attack-vs-spy decisions.

### FIX 2 — attack worker emit guard (v31 < 2 → v31 < 1)
- **Site:** stack layout + worker writes (0x45735d) + pad loop (0x457372) + guard
  (0x4573e0 `cmp [esp+...+438h], -1 ; jz`).
- **Cause:** `v45` lives at `esp+0x438`; `&v43` at `esp+0x434`. Worker writes use
  `mov [esp+esi+434h], id` with esi pre-incremented by 4, so the FIRST worker id lands
  at esp+0x438 = v45. The pad loop only writes -1 to esp+0x438 when zero workers were
  found. Therefore `v45 != -1` IFF **≥1** worker found — not ≥2 as the reconstruction
  assumed.
- **Before:** `bool v45IsNeg1 = (v31 < 2);`
- **After:**  `bool v45IsNeg1 = (v31 < 1);`
- **Evidence:** disasm 0x45735d `mov [esp+esi+434h], ebx` (esi=4 first write → 0x438);
  0x457372 pad `lea eax,[edx*4]` starting m=4*v31; 0x4573e0 reads var_11C (0x438).
- **Impact:** Attack command was suppressed when exactly 1 worker qualified; the binary
  emits it. (The shipped golden tests use 0 workers, so they still pass either way; this
  fix corrects the 1-worker edge.)

### Test/comment updates (golden alignment to binary)
- `tests/unit/ai_meister_angriff_test.cpp`: 3 fake scene-node writes moved from `+21`
  to `+42` (Tests `AttackBudgetGateReturns0`, `AttackCommandType73WithWorkers`,
  `V45GuardOneWorkerNoEmit`) so they exercise the real attack-path match. Test 8 now
  genuinely tests the attack budget<3 gate (previously passed via the spy path by
  accident). Stale comments ("<2 workers", "*(j+21)") corrected.

### BOUNDARY — *(mr+448) = *(a3+1) unconditional write (0x456cc0)
- The binary writes `*(mr+448) = *(a3+1)` BEFORE the `test ebp,ebp` null check at
  0x456cc6. When `a3`(bestTarget) is null, the original dereferences address 1 (UB /
  crash). The reconstruction guards with `if (bestTarget)`. Behavior is identical on all
  reachable inputs (a3==null → LABEL_26 returns 0; the garbage write is unobservable),
  and the guard avoids a host crash. Documented, not "fixed".

---

## Verification
- `g++ -std=c++17 -fsyntax-only` on the TU: clean.
- `cmake --build build --target ai_meister_angriff_test`: builds (pre-existing warnings
  only), **39 checks, 0 failures**.
