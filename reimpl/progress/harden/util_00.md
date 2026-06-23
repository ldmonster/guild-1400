# Harden sweep — chunk util_00 (util/math leaves)

Full-tree 1:1 hardening of the 22 `src/util/*.cpp` files in chunk util_00. Every
provenance'd function was decompiled AND disassembled via IDA MCP (module gilde.exe)
and diffed line-for-line. Divergence classes checked per file: control flow / branch
conditions, get_bytes/get_global_value on every constant+table, ConvertX(trunc) vs
fistp(round-to-nearest) at each float->int site, x87 80-bit accumulation order modeled
as double, fixed-point sar/shr + signed/unsigned compares, RNG draw count+order,
struct offsets/stride, side-effect order, return value incl edx.

## Summary

- Functions verified: ~52 across 22 files.
- FIXED: 9 functions/constants (source), 2 golden test fixes.
- VERIFIED-1:1 (no churn): the remainder.
- BOUNDARY (rule 4/8 — Win32 / x87-state / 64-bit DE-trap): 5.
- Tests: all affected suites green (see bottom).

## Per-file results

### math.cpp
- **StoreAndZero @0x602968 — FIXED.** Disasm (`fld value; fld st; call ConvertX;
  fsub st(1),st; fstp [out]`) returns the *fractional part*: `t=ConvertX(value);
  *out=t; return value-t` — not `*out=value; return 0`. Golden in util_math_test
  encoded the wrong behavior (slot==5.5,r==0) -> fixed to slot==5.0,r==0.5.
- **NormalizeAngle @0x5eef4c — FIXED.** Depended on the StoreAndZero bug; now stores
  trunc(value) then biases by -1.0 when value<0. Cross-checked vs the correct copy in
  src/ai/aimethod2.cpp.
- **VectorAngleBetween @0x5ca334 — FIXED.** Opposite-vector return was double
  -3.1415927; binary loads float 0xC0490FDB -> -3.14159274101257324f. +acos additive
  dbl_628CE8 is exact -6.283185307179586 (was -6.28318530718).
- **VectorAngleWrapped @0x5ca504 — FIXED.** Threshold dbl_628CF0 is exact
  -3.141592653589793 (-pi) (was -3.14159265359).
- VERIFIED-1:1: ClampValueRange@0x552734, Distance2D@0x58525c (dbl_6264DC=2.9081632653),
  DecrementAndAbs@0x5f0be5, CatmullRomInterp@0x5ca9d8, CubicBezierPoint@0x59b3d8,
  LerpClampedCoord@0x5d9d60, VectorLerp@0x5ca2fc, VectorNormalize@0x5cb148,
  VectorWithinTolerance@0x5caa4c, TriangleNormal@0x5cb824, MaxVectorLength@0x5cffac,
  UInt64Multiply@0x14210c0, Multiply64@0x6068dc.
- BOUNDARY: UnsignedLongLongDivide@0x5e57e7, LongLongDivide@0x5e5792 (MSVC __aulldiv/
  __alldiv; bit-exact for nonzero den; original #DE on /0 not portably reproducible).

### matrix.cpp
- **kTwoPi/kNegTwoPi — FIXED.** Binary stores imprecise 0x401921FB54442EEA; decimal
  changed to 6.28318530718 to round-trip those exact bits.
- **kDetEpsilon — FIXED.** dbl_628D38 = 0x3E7AD7F29ABCAF48 = exactly 1e-07 (was a
  different bit pattern).
- **kGimbalEps — FIXED.** dbl_628D40 = 0x3EC00000001C5F68 = 1.9073486336e-06.
- **MatrixDecompose @0x5cb354 — FIXED.** Disasm @0x5cb7b6 (`add esi,80h` end=a1+32,
  `add eax,10h`) shows the translation loop sums **8** rows and returns **a1+32** —
  source summed 4 rows / returned a1+16. Loop bound, return, and doc fixed.
  (0 callers; no golden encoded the old behavior.)
- VERIFIED-1:1: MatrixIdentity@0x5cb100, MatrixCopy@0x5cabf0 (transpose),
  MatrixInverse@0x5cac3c (1.0/(double)(float)det truncate-to-float; singular ->
  transpose), MatrixTransformVectors@0x5caaa4, MatrixFromEuler@0x5cb1bc,
  MatrixToEuler@0x5cb2cc, QuatNormalizeAxis@0x5ca8c8, SnapVectorToAxis@0x5ca940,
  BuildBasisFromAngle@0x5ca544. Constants flt_628D00=2.0, dbl_628D08=-1.0,
  flt_628D10/D14=±2pi(f), flt_628D48=0.125, flt_628CFC, flt_5CA2D0..D8={0,1,0},
  flt_5CA2B0..B8={0,0,1} all byte-confirmed.
- BOUNDARY: QuatRotateVector@0x5ca798 (0 callers / dead code; binary reads
  uninitialized aliased stack slots +0x24/+0x28 and has a typo at +0x18, leaving the
  3rd output row garbage. Recon implements the INTENDED correct rotation; the
  binary's UB is documented, not reproduced. Comment overstatement corrected: only
  m00/m01/m02/m11/m12/m22 match the binary's computed entries).

### math_rng_float.cpp
- **RandomUnitFloat @0x58b744 — FIXED.** Clamp ceiling stored as float dword 3F7FFFFEh
  and `fld dword` re-promotes -> return must be (double)(float)0.99999988
  (0x3FEFFFFFC0000000), not the raw double kUnitMax. Fixed to
  `return static_cast<float>(kUnitMax);`.
- **RandomRangeWithBase @0x58bc00 — FIXED.** `xor eax,eax; mov al,bl; fild word`
  zero-extends base before the `<42.0f` compare. Source sign-extended (i16) -> a
  negative char wrongly took mod-6. Fixed to `static_cast<u8>(base) < 42`.
- VERIFIED-1:1: RandomScaledInt@0x58b870, RandomFloatScaled@0x58b910,
  RandomSquaredSigned@0x58b92c, RandomSquaredUnit@0x58b98c, RandomSignedOffset@0x58b8c4,
  RandomRange@0x538438. Constants dbl_62674C=4.656612875245797e-10,
  dbl_626754=0.99999988, flt_62675C=3.0518509447574615e-05, flt_626770=42.0 byte-exact.
  MINSTD/Schrage (q=127773,a=16807,r=2836), 8-step prime + 32-entry shuffle fill,
  lazy-reseed guard, last>>26 index math, and per-fn RandNext draw counts all confirmed.
- Added 13 RNG golden/property tests (incl. draw-stream parity).

### math_random.cpp
- VERIFIED-1:1: RandomModulo@0x58b89c.

### math_trig.cpp
- **AcosGuarded @0x5f0b9c — FIXED.** ftst @0x5f0ba6 compares **s=1-x²** (not x) vs 0;
  saturation (0/pi) is reached only when s==0 exactly (x==±1); |x|>1 falls to acos ->
  NaN. Changed `if (s<=0.0)` to `if (s==0.0)`; comment corrected. Golden in
  util_math_test fixed (was AcosGuarded(1.5)==0 / (-1.5)==pi -> now ±1->0/pi, |x|>1->NaN).
- VERIFIED-1:1: Atan2@0x5f5701, Atan2Unary@0x5f56ec.

### float_math.cpp
- VERIFIED-1:1: Fmod@0x5d3fb2, Sqrt@0x6029b4, LogBase@0x5fca30, AtanUnary@0x5f56ec,
  Log/Log10/Log2 wrappers. FYL2X multipliers confirmed: code 9->fld1->log2,
  code 11->fldlg2->log10, code 10/default->fldln2->ln. The Log10()==log2 / Log2()==log10
  naming is a faithful clone of the binary's swapped thunks (thunk names mislabeled in
  IDA; behavior reproduced exactly). Comments hardened.
- BOUNDARY (modeled, host has no x87 software-fallback): FmodSoftware / Atan2Software /
  the byte_64A958 fallback paths route to <cmath>; numerically equivalent.

### fpu.cpp
- VERIFIED-1:1: DecodeStatusWord@0x1426c03, EncodeControlWord@0x1426c95,
  SetControlWord@0x1426bb8, ControlMask@0x1426bed (mask &0xFFF7FFFF), ControlWord@0x14242a2,
  ClassifyDouble@0x142416a (inf/qnan/snan masks 0x7FF00000/0x7FF8/0x7FF0/0x7FFFF). All
  bit tests and branch ordering confirmed. (FCW read/write modeled host-side per rules.)

### string_ops.cpp
- VERIFIED-1:1 (all 9): StrStr@0x1427fd0, StrnLen@0x14287d3 (returns raw cap when no
  terminator), StrnCpy@0x1428cb0, StrChrLast@0x5d3ef0 (strrchr incl NUL),
  StrToUpper@0x5e9f50, StrncmpN@0x5e9ee0 (raw byte diff), StrCmpNoCase@0x5cb8f0 (fold
  to lower, raw diff), StrCmpNoCaseN@0x5e0db0, StrCmpNoCaseSign@0x5ea788 (fold to UPPER,
  normalize to -1/0/+1).

### sort.cpp
- VERIFIED-1:1: SwapElements@0x142190c, InsertionSort@0x14218be, QuickSort@0x142176a
  (cmp(loScan,lo)<=0 / cmp(hiScan,lo)>=0 signed; break hiScan<loScan; signed span
  compare leftSpan=(hiScan-lo)-1 vs rightSpan=hi-loScan; on-stack depth 30 in original,
  source uses 64 >= capacity), BinarySearch@0x5e9f70 (hi=last elem; cmp(key,mid)).

### locale_recon.cpp
- VERIFIED-1:1: Language table @0x5a3260 (5x0x40, GERMAN/ENGLISH/FRENCH/ITALIAN/SPANISH
  byte-confirmed), LocaleCopyLanguageString@0x5a33a0 (word_649D48=0xFFFF init confirmed
  via disasm `mov edx,0FFFFh`; IDA dropped the constant), LocaleSetupMbcsCodePage@0x609f90
  (all switch arms, SJIS lead 129..159+224..252 inclusive, dbcs=LeadByte[0]!=0, 257-byte
  table, acp==1->GetOEMCP branch).

### misc_recon4_parse.cpp
- VERIFIED-1:1: ParseDoubleStringScan@0x5d3fe0 (all flag bits, 19-sig-digit cap, v24
  rollback, v13 bookkeeping, classification 308/-308 via cmp 134h / FFFFFECCh),
  RandSetSeed (entry 0x1422144), RandCurrentSeed, GetTickSeed@0x1414ae0. dword_1452BD0
  confirmed.

### bitset.cpp
- VERIFIED-1:1: BitSetTestTail@0x1426de9 (mask=~(0xFFFFFFFF<<(31-bit%32)) via
  `or edx,-1; shl; not`; word bound kBitSetWords=3 == `cmp esi,3`),
  BitSetAddRoundCarry@0x1426e32 (carry propagate to lower words; inlined UAddCarry ==
  0x1428dae).

### coord.cpp
- VERIFIED-1:1: ConvertX@0x5c6b08 — fstcw / RC=11 (round-toward-zero) / frndint ==
  std::trunc. The canonical project-wide truncation helper, confirmed exact.

### coord_worldtile_misc_recon.cpp
- VERIFIED-1:1: Coord_WorldToTile@0x577690 (offsets originX=+0x90, scaleU=+0xA0,
  originZ=+0x98, scaleV=+0xB8; scratch[0]=X, scratch[2]=Z; sub-then-div).

### transform.cpp
- VERIFIED-1:1: PointThroughBoneChain@0x5c8b38, PointThroughBoneChainPivot@0x5c8d0c,
  RotateVectorByHierarchy@0x5c8990. All bone/frame byte offsets confirmed (translation
  frame[30..32]@120/124/128, parent link @byte 504=[reg+1F8h], pivot i[27..29], 3x3
  rows {99,103,107}/{100,104,108}/{101,105,109}, +i[19..21]@76/80/84). RotateVector
  comment corrected (row layout, not "cols"). Original returns pathological eax
  (dangling stack ptr / float-bits-as-ptr); NO caller consumes it as a pointer, so
  recon returns `out` (safe). True original return semantics documented in comments.

### buffer.cpp
- VERIFIED-1:1: BufferPutChar@0x5f8540 (cursor +0, count +0x10).

### color.cpp
- VERIFIED-1:1: ColorSetRgb@0x4226e0 (dl->[0], cl->[2], bl->[1] — the documented
  swap is correct), ColorNotEqualRgb@0x4226bc.

### guid.cpp
- VERIFIED-1:1: GuidFormat@0x432a0c (fmt @0x614ee8 byte-confirmed; high byte from
  src[15]), GuidParse@0x432a70 (sscanf v13/v12 arg swap mirrored by store swap -> net
  sequential data4[0..7]).

### mem_ops.cpp
- VERIFIED-1:1: MemMove@0x5d9310 (src>=dst||src+n<=dst -> forward; else backward),
  MemMove2@0x1427370 (dst>src && dst<src+n -> backward; else forward).

### leaves_wave20.cpp
- VERIFIED-1:1: ShapeAnimClearSlot@0x5d8f1c (17*n stride via shl4+add; count 17),
  ExitHandlerThunk@0x5f8230 (slot in edx -> ClearEntry).

### fpu_misc_recon.cpp
- VERIFIED-1:1: Fpu_DetectNanType@0x606210, Fpu_Init@0x5fa5dc (BYTE1(result)=
  initialised; dead al==3 branch correctly elided), Fpu_InstallSaveRestore@0x5fa59c.
  FpuState fields map to byte_64A1A0/A1A1/64A99C.
- BOUNDARY: Fpu_SaveState@0x5fa590, Fpu_RestoreState@0x5fa598 (x87 fsave/frstor
  108-byte image; inert on host FPU).

### trace_misc_recon.cpp
- VERIFIED-1:1: Trace_GetEntryByIndex@0x437c00 (count@table+4, base table+16,
  stride *entry+4), Trace_IsAddressReadable@0x437d9c (all protect-class thresholds
  >=0x10/>0x10/>=0x40/>0x40&&!=0x80/!=32, then <2||(>2&&!=4), pageProtect==2||4).
- BOUNDARY: Trace_WriteSymbolLine@0x437c24 (per-frame line emitter; all side effects
  are Win32 WriteFile to the tracer file handle + needs the live tracer-context struct;
  rule-4 Win32->SDL + rule-8 no-fake-I/O boundary; deferred to Trace/IFileSystem
  integration, not stubbed. Its pure callee GetEntryByIndex IS reconstructed).

## Test results (all green)
util_math_test, util_matrix_test, util_matrix_e2e_test, util_rand_test, util_fpu_test,
util_string_ops_test, util_containers_test (bitset+sort), util_locale_recon_test,
misc_recon4_parse_test, fpu_misc_recon_test, trace_misc_recon_test,
coord_worldtile_misc_recon_test, object_transform_ops_test — 100% pass, 0 failures.

Note: a transient `src/play/slice_council.*` link break was observed mid-sweep by some
agents (a concurrent wave's in-flight state); it resolved on its own — final build of
all util_00 test targets links and passes. Not a util_00 file; not touched.
