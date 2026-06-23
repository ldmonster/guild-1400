# Hardening sweep — chunk util_01

Files owned:
- `src/util/util_misc.cpp` (+ `src/util/util_misc.h`)
- `src/util/util_recon.cpp` (+ `src/util/util_recon.h`)

Tests built & run (all GREEN):
- `util_recon_test`  (697) — PASS
- `crtutil_p6_test`  (164) — PASS  (covers util_misc.cpp functions)
- `crtutil_p6_itest` (848) — PASS
- `crtutil_p6_e2e_test` (1155) — PASS
- `util_rand_test`   (696) — PASS (adjacent; untouched, sanity only)

MCP method: every provenance'd function decompiled at its address; control flow,
constants, casts, struct offsets, comparator operands, and float/int sites diffed
line-for-line. Where Hex-Rays collapsed `__usercall` comparator args, the DISASM
was used as the reference of record.

---

## src/util/util_misc.cpp

| Function | Addr | Verdict |
|---|---|---|
| RetZero | 0x4029c0 | VERIFIED-1:1 — `return 0`. |
| RetOne | 0x527d9c | VERIFIED-1:1 — `return 1`. |
| MemFindPattern | 0x140b000 | VERIFIED-1:1 — naive O(n*m) scan; needle cursor resets to 0 on mismatch; `return a1+v6-a4` on full match, else 0. needleLen==0 → returns haystack (quirk preserved). |
| RandomMod | 0x140b530 | VERIFIED-1:1 — `(Next() | (Next()<<16)) % mod`; draw order hi-first matches `v3 = Next()<<16` then `Next() | v3`. Default hook drives MSVC 214013 LCG (`MsvcRandNext`), exactly the `VIBE_Rand_Next @0x142214e` generator the engine call site uses. |
| BuildCrc16Table | 0x1412290 | VERIFIED-1:1 (math) — poly 0x1021 MSB-first; `v2=(i16)((u16)i<<8)`, 8 steps of `v2<0 ? (2*v2)^0x1021 : 2*v2`. INTERFACE NOTE: original writes the fixed global `word_145F160` (only caller `VIBE_Drm_Main @0x1414df8`); recon takes a `table16*` param — acceptable seam (that global is not yet reconstructed; byte logic identical). Original returns `i+1`(=256) in eax; recon returns void — callers ignore the return. |
| RotateByte | 0x14155b0 | VERIFIED-1:1 — `(u8)(v<<(s%8)) | ((v<<(s%8))&0xFF00)>>8`. |
| AlignTo8 | 0x14155f0 | VERIFIED-1:1 — `RotateByte(v, 8 - s%8)`. |
| InitAndShuffleByteArray | 0x58b9c4 | VERIFIED-1:1 — fill arr[i]=i; if count>=2 do `(max(count>>2,1) + count>>1)` successful swaps; two `RandNext()%count` draws (8-bit count divisor confirmed via `idiv esi`/`movzx si,bl` @0x58ba3f-48; signed idiv but RandNext∈[0,0x7FFF]); swap counts only when indices differ; index compare is 8-bit. Draw count & order match. |
| InitAndShuffleDwordArray | 0x58ba98 | VERIFIED-1:1 — same swap-count formula; draws `% a1` (u8 count), result truncated to u16 before compare; do-while gated by `result>0`. |
| ShuffleDwordArray | 0x58bb74 | VERIFIED-1:1 — no init fill; divisor is `(u16)count`; `quarter = count>>2` clamped to >=1, swaps = `quarter + (count>>1)` (unsigned shr on full count); early-out when swaps<=0. |
| StrCopyChecked | 0x43fc58 | VERIFIED-1:1 — `if(!dst) return 0;` else strcpy; original unrolls 2/step but observable result is byte-identical; returns 1. |

## src/util/util_recon.cpp

| Function | Addr | Verdict |
|---|---|---|
| ReconStrCmp | 0x5d3f10 | VERIFIED-1:1 — pointer-identity → 0; first differing byte normalised to exactly -1/+1 via `-v6 \| 1`; terminator counts as a byte; word-at-a-time fast path is observationally equal to the per-byte loop. Cross-checked vs `std::strcmp` sign over a matrix. |
| ReconStrCopyToNormalBuf | 0x43de48 | VERIFIED-1:1 — 2-byte unrolled strcpy into the "normal_s" buffer (orig global `aNormalS_0 @0x62d012`); returns 1. Byte after terminator on odd lengths left untouched (no over-copy) — matches. |
| ReconZeroStruct12 | 0x58f138 | VERIFIED-1:1 — single byte @+0 cleared, dwords @+4/+8 cleared, +1..+3 untouched; returns p. |
| ReconBubbleSortRecords | 0x59211c | VERIFIED-1:1 (control flow) — `skipFlag!=0` no-op; i in [0,count-1), j in (i,count); swap when `StrCmp(label[j],label[i])==1`; `i` advances even when inner loop skipped. BOUNDARY: label formatter `VIBE_Text_FormatItemLabel @0x59c2e4` is out of cluster → comparison injected via `cmp` hook (default inert). |
| ReconSortSwapElements | 0x5ea040 | VERIFIED-1:1 — original swaps `width>>2` dwords (InterlockedExchange) then `width&3` byte tail; recon byte-wise swap is observationally identical for non-overlapping blocks (documented). |
| **ReconSortChoosePivot** | 0x5e9ff0 | **FIXED** — see below. |
| ReconQuickSort | 0x5ea068 | VERIFIED-1:1 — full diff vs disasm: median-of-medians pivot selection (n>0x2A step=`width*(n>>3)`, n>0x1D), pivot→front, three-way partition with low/high equal bands (loEq=v53, i=v13, hi=v54, hiEq=v55; comparator operands confirmed `cmp(i,pivot)`/`cmp(hiEq,pivot)` via @0x5ea389/@0x5ea3f2 where pivot ptr = var_38 = lo in the swap path), equal-band folding with `min` lengths (@0x5ea515/@0x5ea575), recursion pushing the larger side (`leftBytes<=width` early-pop only on the `rightBytes<leftBytes` branch — asymmetry preserved), insertion sort gaps 3*width then -2*width with `cmp(prev,m)>0` swap test (@0x5ea184). Swap mechanism (atomic dword vs helper) and v52 temp-pivot path are observationally identical. Cross-checked vs std::sort over random/dup/sorted/wide inputs. |

---

## FIXED: ReconSortChoosePivot @0x5e9ff0  (median-of-three)

EVIDENCE (disasm @0x5e9ff0; reg map eax=a(edi), edx=b(esi), ecx=cmp, ebx=c;
comparator is `__usercall(eax=first, edx=second)`):

```
5e9ffb call cmp(a,b);  5e9fff jle  ->else
5ea005 call cmp(a,c);  5ea009 jle  ->return a      (eax=edi,edx=ebx)
5ea00f call cmp(b,c);  5ea013 jle  ->return c; else return b   (eax=esi,edx=ebx)
else:
5ea01b call cmp(a,c);  5ea01f jl   ->second; else return a
5ea029 call cmp(b,c);  5ea02d jle  ->return b; else return c
```

The Hex-Rays decompile collapses the three comparator calls into bare
`a3()/v6()/v7()/v8()` and drops the operands, so the prior reconstruction guessed
the wrong comparison pairs/order.

BEFORE (divergent — wrong comparator operands in both arms):
```cpp
if (cmp(a,b) > 0) { if (cmp(b,c) > 0) { if (cmp(a,c) > 0) return b; return c; } return a; }
if (cmp(b,c) >= 0) return a;
if (cmp(a,c) > 0) return c;
return b;
```

AFTER (matches disasm — true median-of-three):
```cpp
if (cmp(a,b) > 0) {
    if (cmp(a,c) > 0) { if (cmp(b,c) > 0) return b; return c; }   // 5ea005 / 5ea00f
    return a;                                                     // 5ea021
}
if (cmp(a,c) < 0) { if (cmp(b,c) > 0) return c; return b; }       // 5ea01b / 5ea029
return a;                                                         // 5ea021
```

GOLDEN ALSO FIXED — `util_recon_test.cpp::UtilReconPivot.DecompileExactBranches`
encoded the wrong outputs (`(1,2,3)->2 (1,3,2)->1 (2,1,3)->2 (2,3,1)->2 (3,1,2)->3
(3,2,1)->2`). Re-traced against the corrected (binary-accurate) logic with a sign
comparator: all six permutations of {1,2,3} return the true median **2**. Golden
updated to `{2,2,2,2,2,2}`. (The function is a genuine median-of-three, as expected
for the MSVC qsort pivot core; the prior "NOT a strict median" comment was an
artifact of the bad decompile.)

Impact: ReconQuickSort calls this for pivot selection; the fix yields correct
median pivots. All quicksort cross-checks vs std::sort remain green.

---

## Counts
- Functions verified: 19  (RetZero, RetOne, MemFindPattern, RandomMod,
  BuildCrc16Table, RotateByte, AlignTo8, InitAndShuffleByteArray,
  InitAndShuffleDwordArray, ShuffleDwordArray, StrCopyChecked, ReconStrCmp,
  ReconStrCopyToNormalBuf, ReconZeroStruct12, ReconBubbleSortRecords,
  ReconSortSwapElements, ReconSortChoosePivot, ReconQuickSort + the
  MsvcRandNext/SetMsvcRandSeed LCG helpers @0x142214e/0x1422144).
- VERIFIED-1:1: 18
- FIXED: 1 (ReconSortChoosePivot — source + golden)
- BOUNDARY: 1 (ReconBubbleSortRecords comparator → out-of-cluster label formatter
  `VIBE_Text_FormatItemLabel @0x59c2e4`)
- Interface notes (no behavior change): BuildCrc16Table writes the fixed global
  `word_145F160` in the binary; recon parameterises the table (global not yet
  reconstructed). MsvcRandNext (RandomMod default hook) = `VIBE_Rand_Next @0x142214e`.
