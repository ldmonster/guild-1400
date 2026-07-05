# Harden — crt_00

IDA-verified pass over `src/crt/` core tables + int64/ctype primitives.
Suite green 1558/1558 after.

## Real divergence found + fixed

**`ctype.cpp` — `kStrtolCtype[256]` (`byte_64A208`)** was **shifted right by 1**
from index 92 (`'['`) onward: every value TRANSITION landed one slot late. Diffed
byte-for-byte against the binary — 6 boundary bytes wrong:
`[92]'[' 0x48→0x0C`, `[98]'a' 0x0C→0x98`, `[104]'g' 0x98→0x88`, `[124]'{' 0x88→0x0C`,
`[128] 0x0C→0x01`, `[129] 0x01→0x00`. So `'a'..'f'` carried `0x0C` instead of the
hex-digit-letter bits `0x98`, and `'g'` carried `0x98` instead of `0x88`. Re-derived
the whole 256-byte array directly from `@0x64a208`; now byte-exact (0 mismatches).

LATENT (no test moved): the current consumers — `StrToLong @0x60b350` /
`scanf.cpp` — resolve digits via `DigitValue` and only test this table's whitespace
bit `0x02` (and `0x20`), none of which the mis-set bytes changed. Fixed anyway: the
table must be byte-exact to the binary (and correct for any future reader of the hex
bits). Mirrors the earlier resolution-table transcription find in config_00.

## Verified 1:1 (no change needed)

- **`kPctype[256]` (`__pctype` @0x14529ba)** — 256×u16 class-bit table, diffed
  byte-for-byte: **0 mismatches**.
- **`int64.cpp UInt64Divide @0x1427b10`** — the bit-shift normalize / 32-bit estimate /
  correct-down algorithm matches; proved the reimpl's `full > a` correction test is
  identical to the decompile's reconstructed-`v9` compare (same low/high dwords), and
  the carry (`__CFADD__`) and the `b_hi==0` simple 64/32 path both match.
- **`DigitValue @0x60b4c0`** — the decompile's three sub-ranges (`a-i`,`j-r`,`s-z` →
  `v2-87`, else `37`) collapse exactly to the reimpl's single `a-z → lc-87` / else 37.

## Method note
Table verification (fetch `get_bytes` → programmatic diff vs the source array) is the
highest-yield check — two of two chunks so far turned up a real table transcription
error this way (config resolution table; crt strtol ctype table).
