# Wave-11 hardening — CRT cluster (W11-CRT)

Scope: `src/crt/*.{h,cpp}` (the reconstructed C runtime: printf/sprintf, scanf,
string ops, itoa/atoi/strtol, time/tzparse, rand) and their tests
(`crt_*_test`, `tzparse_recon_test`, `util_rand_test`). MCP was DOWN — hardening
only, no new 1:1 reconstruction.

Method: ASAN+UBSAN build of the cluster test targets
(`-fsanitize=address,undefined -fno-sanitize-recover=all`), added
malformed/boundary/overflow edge tests, fixed every OOB/UB found. All goldens
kept byte-identical; only the in-bounds/valid-input behaviour was preserved.

ASAN/UBSAN build dir: `build-w11crt` (Debug, GUILD_BACKEND=OFF).

## Bugs fixed

### 1. printf `%s` precision over-read — REAL OOB (src/crt/printf.cpp:~186)
`%.Ns` of a NUL-less buffer of exactly N bytes read `str_ptr[N]` because the
length-scan tested `str_ptr[n]` before `n < max`. Reordered to check the
precision bound first (`(max < 0 || n < max) && str_ptr[n]`). Output is
byte-identical (the extra byte was never used); the over-read is gone.
Caught by the new test `CrtPrintf.StringPrecisionNulless`.

### 2. printf `%p` / `%.*p` precision stack overflow (src/crt/printf.cpp)
The `%p`/`%P` path left-padded zeros by widening the fixed `char body[80]` up to
`precision` — a `%.200p` / `%.*p` 400 blew the stack buffer. Removed the
body-widening loop; pointers now emit their precision zeros via the existing
`zero_pad` count through the bounded Sink (added `pointer` to the zero_pad
condition). Identical output for in-range precision, no OOB for any precision.
Tests: `PointerHugePrecisionNoOverflow`, `PointerStarHugePrecision`.

### 3. printf width/precision parse signed-overflow UB (src/crt/printf.cpp)
`field_width`/`precision` accumulators `*10 + digit` on `int` are UB on a
malformed huge spec (`%999999999999d`). Now computed in `u32` (the exact x86
32-bit wrap) and cast back. Output unchanged for realistic specs.

### 4. Atoi signed-overflow UB (src/crt/strtol.cpp)
`acc = c + 10*acc - 48` and `return -acc` were signed-overflow / INT_MIN-negate
UB (UBSAN tripped on baseline). Both now done in `u32` 2's-complement
(`0u - (u32)acc`), preserving the documented wraparound exactly. Tests:
`CrtStrtol.AtoiWraparoundDefined`.

### 5. StrToLong / Strtol_Parse negate UB (src/crt/strtol.cpp)
`-static_cast<i32>(acc)` is UB when acc == 0x80000000. Replaced with `0u - acc`
(u32). LONG_MIN now negates to itself with no UB and no spurious ERANGE. Tests:
`StrToLongIntMinNoClamp`, `StrToLongBaseRejects`, `StrtolParseEmptyAndWhitespace`.

### 6. scanf ReadFloat fixed-buffer overflow — REAL OOB (src/crt/scanf.cpp)
`ReadFloat` collected the numeric token into `char buf[128]` via unbounded
`*w++`. An unbounded `%f` of a long digit run (no field width => width counter
never reaches 0) overflowed the stack. Added a `PUTW` macro that stops storing
at `buf+127` while still consuming/counting input. Every realistic float token
fits, so valid output is unchanged. Tests:
`CrtFloat.ScanFloatOverlongTokenNoOverflow`, `ScanFloatWidthBounded`.

### 7. scanf ReadInteger / ParseSpec overflow & negate UB (src/crt/scanf.cpp)
`acc32 = dv + base*acc32`, `acc32 = -acc32`, `-static_cast<i64>(acc64)`, and the
width accumulator were signed-overflow/negate UB. All redone in unsigned
2's-complement. Tests: `ScanIntegerOverlongNoUB`, `ScanIntMin`, `ScanEmptyInput`.

### 8. tzparse ParseDecimal overflow UB (src/crt/tzparse_recon.cpp)
`i = v4 + 10*i - 48` signed-overflow UB on a long digit run. Now in `u32`.
Test: `GameTimeReconTzHarden.ParseDecimalLongRun`.

## Tests added (all pass clean under ASAN+UBSAN)

- crt_printf_test: CapOneOnlyNul, CapZeroNoWrite, StringTruncatedExactBoundary,
  StringPrecisionNulless, WidthExtremeBounded, PrecisionExtremeNumeric,
  PointerHugePrecisionNoOverflow, PointerStarHugePrecision, ManyArgs,
  TrailingPercent, IntMinDecimal  (117 -> 141 checks)
- crt_strtol_test: AtoiWraparoundDefined, StrToLongIntMinNoClamp,
  StrToLongBaseRejects, StrtolParseEmptyAndWhitespace  (3386 -> 3403 checks)
- crt_string_test: IntToAsciiIntMin, IntToRadixExactBuffer,
  UIntToStringBinaryMax, ConcatExactBoundary  (67 -> 75 checks)
- tzparse_recon_test: EmptyString, NameLengthCapBoundary, NameExactly128,
  GarbageRule, ParseDecimalLongRun, TruncatedMRule  (51 -> 195 checks)
- crt_float_test: ScanFloatOverlongTokenNoOverflow, ScanFloatWidthBounded,
  ScanIntegerOverlongNoUB, ScanIntMin, ScanEmptyInput  (417 -> 426 checks)
- util_rand_test: ExtremeSeedsCleanWrap, Seed0Reproducible  (crt::rand part)

Final cluster status (ASAN+UBSAN, `build-w11crt`): all green, 0 failures.
Normal `build/`: all green, 0 failures.

## Documented, NOT fixed

### BEHAVIORAL / engine envelope (needs MCP to confirm original bounds)
- `StringTrimTrailingSpaces` (gilde.exe 0x44b2ac, src/crt/string.cpp): on an
  EMPTY string it computes `strlen-1 == -1` and reads `s[-1]` (OOB read). This
  matches the binary's own indexing; the helper's precondition is a non-empty
  string. No empty-string test added (it would fault exactly as the original
  would). Confirm the original's guard with MCP before "fixing".
- `FormatAsctime` (gilde.exe 0x5e55f0, src/crt/asctime.cpp): indexes
  `kDayC0[tm.wday]` / `kMonC0[tm.mon]` with no bounds check. A malformed tm
  (wday>6 / mon>11) over-reads the 7/12-entry tables — same as the binary's
  asctime, which requires a valid tm. Envelope, not a reconstruction bug.

### NON-OWNED cluster (flag for the util owner)
- `tests/unit/crtutil_p6_test.cpp:145` — UBSAN signed-overflow in the test's
  own MSVC-LCG reference lambda: `s = 214013 * s + 2531011` on `i32`. This is in
  a `util::` test (RandomMod / SetMsvcRandSeed), not the crt cluster. The fix is
  trivial (compute the LCG step in u32) but belongs to the util cluster owner —
  NOT edited here per the ownership rule.
