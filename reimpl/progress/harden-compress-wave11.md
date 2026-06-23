# Wave-11 hardening — compress + config (W11-COMPRESS)

Scope (owned cluster): `src/compress/*.{h,cpp}` (inflate, deflate/zlib, adler/crc,
gzip, md5) and `src/config/ini.{h,cpp}` (the INI parser), plus their unit tests.
MCP was DOWN, so this is **hardening only** — no new 1:1 reconstruction. Method:
ASAN+UBSAN build of the cluster test targets, add malformed/truncated/oversized
input tests, fix memory-safety/UB/leaks with faithful guards only.

Build used:
```
cmake -S . -B build-w11asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
```

## Fixes

### FIX 1 — signed-overflow UB in ParseProfileInt (`src/config/ini.cpp:40-49`)
`GetPrivateProfileIntA` value parse accumulated digits in a signed `long`:
`acc = acc * 10 + d`. A pathologically long digit run from an untrusted .INI
(e.g. 40 nines) overflows `long`, which is C++ undefined behavior. UBSAN report:
`src/config/ini.cpp:43 runtime error: signed integer overflow: 999999999999999999 * 10`.

Fix: accumulate in `unsigned long` (wraparound is well defined) and negate via
two's-complement (`0ul - acc`), truncating through `unsigned int` to `int` at the
end. **Faithful:** for every value inside the `int` range the final truncation is
bit-identical to the prior signed accumulation, so all valid inputs are
unchanged; only the out-of-range result (which was UB before, no defined value to
preserve) differs — and it is now well-defined garbage rather than UB. Pinned by
`ConfigIniMalformed.HugeValueAndKey` plus in-range regressions (INT_MAX/INT_MIN,
leading zeros, "-0").

### Pre-existing fix built upon
The orchestrator added `~Inflater` / `~Blocks` destructors that free the owned
`Blocks` state and its `Codes`. Verified leak-free under ASAN's leak check with
`inflate_malformed.repeated_decode_no_leak` (64 reset+decode cycles) and
`reset_after_error_then_valid` (error path then reuse). No leak found.

## Audited, NO change needed (already memory-safe / faithful)

- **`inflate.cpp` InflateFast inner reads** (`while (k<20)` / `k<15` read `*p++`
  without checking `n`): protected by the zlib invariant — `inflate_codes` only
  enters the fast loop when `n>=10 && m>=258`, and the `do…while(m>=258 && n>=10)`
  guard re-checks every iteration. 10 input bytes cover the max consumed per
  symbol. Faithful to zlib 1.1.4; the fuzz sweeps below never tripped ASAN here.
- **HuftBuild table stacks** `u[BMAX]`, `x[BMAX+1]`, `c[BMAX+1]` and the `MANY`
  (0x5A0) pool cap: bounds match zlib inftrees.c exactly; the `*hn+z > MANY`
  check guards the pool. No OOB on oversubscribed/incomplete trees (rejected with
  kZDataError before any out-of-table index).
- **inflate_blocks BTREE/DTREE blens indexing**: `s->index` bounded by 19 (BTREE)
  and by `258 + HLIT + HDIST` (DTREE); the repeat guard
  (`i+j > 258+… || (cc==16 && i<1)`) prevents over-write and the `i-1` underflow.
- **gzip.cpp header parser**: every byte via a bounds-checked `get()` that returns
  -1 at EOF; FEXTRA/FNAME/FCOMMENT skips stop at EOF; `payload < 8` guard before
  the deflate slice. No over-read on truncated/oversized-len headers.
- **config/ini.cpp tokenizer**: std::string based; `t.substr`, `find('=')`,
  `find(']')` are all bounds-safe; ctype calls cast to `unsigned char` (no UB on
  high-bit bytes). Unterminated `[`, missing `=`, empty `[]`, lone `[`, embedded
  NUL, non-ASCII all handled without OOB.
- **Adler32 / CrcCompute / Crc16Update**: len-0 and null-pointer early-outs are
  the binary's own behavior; no read on empty input.

## Tests added

### `tests/unit/compress_malformed_test.cpp` (NEW, 273 checks)
- empty / 1-byte / header-only zlib & raw inputs (no over-read)
- truncated zlib AND raw at **every** byte prefix (positive control: full stream
  decodes)
- streamed-then-starved (stops before trailer, no over-read, not false stream-end)
- illegal block type (BTYPE==3) -> kZDataError "invalid block type"
- stored block: bad LEN/~NLEN complement -> "invalid stored block lengths";
  good complement but truncated body -> fail-soft; valid stored roundtrip control
- dynamic header: HLIT/HDIST=31 -> "too many length or distance symbols"
- fuzz: high-bit flip of every interior byte; XOR-mask sweep over every byte
  (drives the huffman tables + window ring; ASAN is the assertion)
- window-size edge: 0x78 (asks wbits 15) into an 8-bit decoder ->
  "invalid window size"; unknown method; bad header-check modulo
- reset-after-error reuse; 64x repeated decode (leak check)
- gzip: empty/short/bad-magic/bad-method/reserved-bits/truncated-FNAME/
  oversized-FEXTRA-len/no-trailer fail-safe; valid roundtrip control
- adler/crc/crc16 on empty + null + non-null-len-0 inputs

### `tests/unit/config_test.cpp` (added `ConfigIniMalformed` suite, +10 tests)
empty/whitespace-only/no-newline; section with no newline; unterminated `[`;
lone `[`; empty `[]`; key with no `=`; `=` first char; duplicate keys/sections
(last wins); huge value (200 KB) + huge key (100 KB); ParseProfileInt overflow
(UB regression) + in-range exactness; non-ASCII section/key/value + embedded NUL;
missing section/key lookups; ReadGfxAndSoundSettings all-defaults path;
ResolutionForIndex over all 256 u8 indices.

## Results (ASAN+UBSAN, build-w11asan)

| target | checks | status |
|---|---|---|
| compress_zlib_test | 59 | PASS |
| compress_deflate_test | 36 | PASS |
| compress_inflate_window_test | 18 | PASS (goldens byte-identical) |
| compress_hash_test | 13 | PASS |
| config_test | 654 | PASS |
| compress_malformed_test (new) | 273 | PASS |
| settings_io_test | 103 | PASS |
| ini_profile_test | 74 | PASS |

Normal `build/` also green (config_test 654, compress_malformed_test 273).
Goldens (`compress_inflate_window_vectors.inc`, `compress_zlib_vectors.inc`)
untouched and byte-identical.

## BEHAVIORAL — needs MCP (not changed)
None found in this cluster. All observable behavior on valid input is unchanged;
the single fix (ParseProfileInt) only affects previously-UB out-of-int-range
input. No 1:1 questions deferred.
