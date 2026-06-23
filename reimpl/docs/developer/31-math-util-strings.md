# 31 — Math, util & strings

This chapter documents the shared low-level primitives that almost every other
subsystem of `gilde.exe` builds on: the pseudo-random number generator (which **must**
be bit-identical for deterministic simulation), the float/fixed-coordinate math
(vectors, matrices, angle helpers, the `frndint` coordinate rounder), the byte-faithful
string/parse helpers, and the in-house `printf`/`sprintf` formatter.

Everything here is reconstructed straight from the IDA decompilation; the binary is the
source of record. Provenance is given as `VIBE_Name @0xADDR`.

Cross-links: [02 — CRT startup](02-crt-runtime-startup.md) (where the RNG state block is
allocated), [15 — Game time](15-game-time-tick.md) and [19 — Commands](19-commands-netcode.md)
(consumers of the deterministic RNG), [24 — Projection & rasterizer](24-projection-rasterizer.md)
(consumer of the coordinate math below).

---

## 1. The RNG (exact — determinism critical)

The game's randomness is a single classic **ANSI-C / `glibc`-style Linear Congruential
Generator** with a 32-bit state. There is exactly one generator instance; every
"random" decision in the simulation (NPC events, AI, world generation, combat rolls)
flows through it, so reproducing it bit-for-bit is mandatory for save/replay and
multiplayer command-stream determinism.

### 1.1 State location

`VIBE_Util_GetRandStatePtr @0x5cb8b0` returns the address of the 32-bit seed word:

```c
// VIBE_Util_GetRandStatePtr @0x5cb8b0
int VIBE_Util_GetRandStatePtr() {
    return off_64A90C() + 12;   // base block + 0x0C
}
```

`off_64A90C` is a per-module data block (allocated during CRT/locale init, see
[02 — CRT startup](02-crt-runtime-startup.md)); the RNG seed lives at **offset `+0x0C`**
inside it. The accessor guards against a NULL block (returns NULL / yields 0 random).

### 1.2 The core step — `VIBE_Util_RandNext @0x5cb8bc`

```c
// VIBE_Util_RandNext @0x5cb8bc  (__cdecl, returns the 15-bit result in eax)
unsigned VIBE_Util_RandNext() {
    unsigned *s = VIBE_Util_GetRandStatePtr();
    if (!s) return 0;
    unsigned v = 1103515245u * (*s) + 12345u;   // LCG advance (mod 2^32)
    *s = v;                                       // store new state
    return (v >> 16) & 0x7FFF;                    // HIWORD(v) & 0x7FFF
}
```

Exact algorithm — **this is the canonical `rand()` of the MSVC/ANSI lineage**:

| Quantity        | Value                                  |
| --------------- | -------------------------------------- |
| Multiplier `a`  | `1103515245` (`0x41C64E6D`)            |
| Increment `c`   | `12345` (`0x3039`)                     |
| Modulus `m`     | `2^32` (implicit 32-bit wraparound)    |
| State width     | 32 bits, stored at `[off_64A90C]+0x0C` |
| Output          | `(state >> 16) & 0x7FFF` → range `[0, 32767]` |

State update is the full 32-bit `state = a*state + c` with **natural unsigned 32-bit
overflow** (no masking other than the implicit wrap). The returned value is **not** the
state — it is bits 16..30 of the new state, i.e. `HIWORD(state) & 0x7FFF`, giving the
familiar 15-bit `RAND_MAX = 32767` range.

> Reimplementation note: compute in `uint32_t`, let it wrap, then return
> `(uint32_t)(state >> 16) & 0x7FFF`. Do **not** use `<random>` or any host `rand()`.

### 1.3 Seeding — `VIBE_Util_RandSeed @0x5cb8e0`

```c
// VIBE_Util_RandSeed @0x5cb8e0  (__usercall, seed passed in edx)
void VIBE_Util_RandSeed(/* seed in edx */) {
    unsigned *s = VIBE_Util_GetRandStatePtr();
    if (s) *s = seed;       // straight store, no transform
}
```

This is plain `srand`: it writes the incoming seed word directly into the state. There
is **no** scrambling of the seed and **no** discard of the first outputs. Notable
callers (from `xrefs_to`): `VIBE_Sound_LibInit @0x445d90`,
`VIBE_TimeBase_StartTimer @0x44e240`, `VIBE_Scene_SyncWorldOnEnter @0x50456c`,
`VIBE_World_LoadBuildingAndObjectData @0x5835f8`, and a 6-byte tail-call thunk
`VIBE_Math_RandomSeed_Thunk @0x58b9bc`. World/scene load re-seeds before generating
content — that is what makes a given world reproducible.

### 1.4 The bounded draw — `VIBE_Math_RandomModulo @0x58b89c` (790 xrefs)

```c
// VIBE_Math_RandomModulo @0x58b89c  (__usercall, bound a1 in ax, result in eax)
int VIBE_Math_RandomModulo(unsigned short bound) {
    if (bound == 0) return 0;
    return (int)VIBE_Util_RandNext() % bound;   // signed % of a 15-bit value
}
```

This is the workhorse — **790 cross-references**. It returns `RandNext() % bound`, with
`bound == 0` short-circuiting to `0` (no divide). Because `RandNext()` is always in
`[0, 32767]` the result is in `[0, bound-1]` (for `bound <= 32768`). The `%` is the
plain C signed remainder; the dividend is non-negative so there is no sign subtlety, but
reproduce the truncating remainder exactly.

`VIBE_Util_RandomModulo @0x404b68` is a **byte-identical duplicate** of the same code
(same `bound==0 → 0`, same `RandNext() % bound`); the compiler emitted the helper twice.
Both must behave identically.

> Determinism contract: the *order* of `RandomModulo`/`RandNext` calls is part of the
> behavior. Any reimplementation must call the RNG at exactly the same points and in the
> same sequence as the original, or the streams diverge even though the generator math is
> correct.

---

## 2. Math primitives

The engine uses **column-stored 4×4 `float` matrices** (16 floats = 64 bytes) with an
affine layout, plain 3-float vectors, and the x87 FPU for trig. The relevant double/float
constants were recovered with `get_bytes`:

| Symbol         | Bytes (LE)                          | Value           | Meaning            |
| -------------- | ----------------------------------- | --------------- | ------------------ |
| `dbl_628CF0`   | `ea 2e 44 54 fb 21 09 c0`           | `-3.14159265…`  | −π (angle wrap)    |
| `flt_628CF8`   | `db 0f c9 40`                       | `6.2831853…`    | 2π (angle wrap)    |
| `dbl_62BFFC`   | `00 00 00 00 00 00 f0 bf`           | `-1.0`          | sign fix-up        |
| `dbl_610784`   | `00 00 00 00 00 00 e0 3f`           | `0.5`           | projection bias    |
| `tbyte_64A7D4` | `00 c0 68 21 a2 da 0f c9 ff 3f`     | `1.57079632…`   | π/2 (80-bit, acos) |

### 2.1 Vector copy — `VIBE_Vector_Copy3 @0x407414`

A trivial 3-DWORD copy `dst[0..2] = src[0..2]` (copies the raw 32-bit words, so it works
for both float and int triples). Returns `dst`.

### 2.2 Vector normalize — `VIBE_Math_VectorNormalize @0x5cb148`

```c
len = sqrt(x*x + y*y + z*z);
if ((bits(len) & 0x7FFFFFFF) != 0) {   // len != ±0
    inv = 1.0f / len;
    x *= inv; y *= inv; z *= inv;
} else {
    x = y = z = 0.0f;                   // degenerate → zero vector
}
```

Length is taken via `sqrt` of the dot product. The zero test masks the sign bit of the
`float` bit pattern (so both `+0` and `-0` count as zero) and, in that case, zeroes the
whole vector instead of dividing. In-place; returns the vector pointer.

### 2.3 Matrix layout & identity — `VIBE_Math_MatrixIdentity @0x5cb100`

The matrix is **64 bytes / 16 floats**. `MatrixIdentity` zeroes all 64 bytes, then sets
the four diagonal floats to `1.0f` (`0x3F800000 = 1065353216`) at DWORD indices
**0, 5, 10, 15** (byte offsets `0x00, 0x14, 0x28, 0x3C`). So it is a row/column-major
4×4 with diagonal at `[i*5]`. (The leading code is just an inlined `memset` with
alignment handling — behaviorally `memset(m,0,64)`.)

### 2.4 Build from Euler — `VIBE_Math_MatrixFromEuler @0x5cb1bc`

Takes a 3-float Euler angle vector `e = a1[0..2]` (X, Y, Z / pitch, yaw, roll in
radians) and writes a 64-byte matrix at `a2`. It precomputes
`sin/cos` of each angle and fills a standard ZYX-style rotation. Exact assignments
(byte offsets into the output matrix; `m[k]` = float at `a2 + 4*k`):

```
sy = sin(e.y); cx = cos(e.x); cy = cos(e.y);
cz = cos(e.z); sz = sin(e.z); sx = sin(e.x);
m[0]  = cy*cz;                         // +0x00
m[1]  = sx*(sy*cz) - cx*sz;            // +0x04
m[2]  = (sy*cz)*cx + sx*sz;            // +0x08
m[4]  = cy*sz;                         // +0x10
m[5]  = cx*cz + sx*(sy*sz);            // +0x14
m[6]  = (sy*sz)*cx - sx*cz;            // +0x18
m[8]  = -sy;                           // +0x20
m[9]  = sx*cy;                         // +0x24
m[10] = cx*cy;                         // +0x28
m[3] = m[7] = m[11] = 0;               // translation-row pads
m[12]=m[13]=m[14] = 0;  m[15] = 1.0f;  // +0x3C = 1.0
```

(The decompiler's `v3..v13` temporaries reorder the multiplies, but the products above
are exactly the floats it stores.) The rotation block is the upper-left 3×3; row/column 3
is the homogeneous `[0,0,0,1]`.

### 2.5 Transform vectors / compose — `VIBE_Math_MatrixTransformVectors @0x5caaa4`

Multiplies a transform `result` (treated as 4 rows of `result[4*r + c]`) by matrix `a2`
and writes a full affine 4×4 to `a3`, **including the translation add** in the last row:

```
out.row[r].x = R[r].x*a2[0] + R[r].y*a2[4] + R[r].z*a2[8]   (+ a2[12] for r==3)
out.row[r].y = R[r].x*a2[1] + R[r].y*a2[5] + R[r].z*a2[9]   (+ a2[13] for r==3)
out.row[r].z = R[r].x*a2[2] + R[r].y*a2[6] + R[r].z*a2[10]  (+ a2[14] for r==3)
out.row[r].w = 0  (rows 0..2)   /   1.0f (row 3, the +0x3C slot)
```

So `a2` columns are `{a2[0],a2[4],a2[8]}` etc. — confirming the **column-major basis**
read, row-major write convention. The w-column of rows 0–2 is forced to `0`, the bottom
row's w to `1.0f`. (Other matrix ops — `MatrixCopy @0x5cabf0`, `MatrixInverse @0x5cac3c`,
`MatrixToEuler @0x5cb2cc`, `MatrixDecompose @0x5cb354` — live in the same cluster.)

### 2.6 Angle helpers

**`VIBE_Math_NormalizeAngle @0x5eef4c`** — wraps a result into `[0, 2π)`:
`VIBE_Math_StoreAndZero` reduces the angle, then if the input `< 0` it adds
`dbl_62BFFC`-area correction; the net effect is "if angle is negative, bring it positive".

**`VIBE_Math_VectorAngleWrapped @0x5ca504`** — computes the angle between two vectors via
`VIBE_Math_VectorAngleBetween @0x5ca334`, then if the result is below −π
(`dbl_628CF0`) it adds 2π (`flt_628CF8`), wrapping the angle into the principal range.

**`VIBE_Math_AcosGuarded @0x5f0b9c`** — a domain-safe `acos`. It computes `1 - x*x`;
if that is ≤ 0 (argument out of `[-1,1]`) it returns `0` or `π` (`fldpi`) depending on
the sign of `x`; otherwise it evaluates `acos` as `π/2 - atan2(x, sqrt(1-x*x))` using
`VIBE_Math_Atan2 @0x5f5701`, `VIBE_Math_SqrtGuarded @0x6029b4`, and the 80-bit
`tbyte_64A7D4 = π/2`. This avoids NaN on edge inputs (cos exactly ±1).

**`VIBE_Math_Fmod @0x5d3fb2`** — floating remainder via the x87 `fprem` loop
(`__FPREM__`), repeating while the C2 (incomplete-reduction) flag is set, matching the
CRT `fmod`. Helpers `VIBE_Math_FmodPrepare @0x60d506` handle the denormal/exception path.

### 2.7 Coordinate rounding — `VIBE_Coord_ConvertX @0x5c6b08`

This is the **screen-coordinate rounder** used by projection. It does an x87
**round-to-nearest** of `st(0)`:

```asm
fstcw  [save]            ; save current control word
mov    al, [save]        ; load low byte of CW
mov    ah, 0x1F          ; high byte = 0x1F → clears RC bits (round-to-nearest-even)
fldcw  [new]             ; install temporary CW (RC = 00 = nearest)
frndint                  ; round st0 to integer per current rounding mode
fldcw  [save]            ; restore original CW
```

The constructed control word forces **RC = 00 (round to nearest, ties to even)** for the
`frndint`, then restores the caller's control word. So coordinate conversion uses
**banker's rounding (round-half-to-even)**, *not* truncation and *not* round-half-up.
This is load-bearing: the rasterizer's pixel coordinates depend on it.

### 2.8 Projection — `VIBE_Coord_ProjectPoint @0x407428` / `VIBE_Coord_ProjectFramePoint @0x407488`

`ProjectFramePoint` first converts a tile (x,y) to a world point via
`VIBE_Heightmap_TileToWorld @0x5c65d4`, then calls `ProjectPoint`. `ProjectPoint`
computes a simple perspective-ish screen mapping:

```
dx = world.x - cam.x;
dz = world.z - cam.z;
inv = 1.0 / cam[4];                // cam[+0x10] = depth/scale divisor
sx = dx * inv + 0.5;               // dbl_610784 = 0.5 rounding bias
sy = 0.5 + dz * inv;
out.x = round_to_nearest(sx);      // via VIBE_Coord_ConvertX
out.y = round_to_nearest(sy);
```

The `+0.5` bias plus the round-to-nearest `frndint` is the projection's pixel-centering.
See [24 — Projection & rasterizer](24-projection-rasterizer.md) for how these feed the
triangle setup.

---

## 3. String & parse helpers (byte-faithful, reconstructed 1:1)

These are hand-rolled `<string.h>` replacements. They operate on raw bytes (Latin-1 /
codepage, **not** locale-aware) and must be reproduced exactly — especially the
ASCII-only case folding, which differs from `tolower`/`toupper` over a locale.

### 3.1 `VIBE_Util_StrChr @0x5d3ef0`

Returns a pointer to the **last** occurrence of byte `c` in the string (it keeps
scanning the whole string, overwriting `v2` on each match, and the loop includes the
terminating NUL via `while (*p++)`). So it behaves like `strrchr`, and `c == '\0'`
returns a pointer to the terminator. Returns NULL if not found.

### 3.2 `VIBE_Util_StrCmp @0x5d3f10`

A word-at-a-time `strcmp`: it compares 4 bytes per iteration, using the classic
`(~v4 & (v3 - 0x01010101) & 0x80808080)` zero-byte test to detect end-of-string within a
word, and unrolls 4 words per loop. On the first differing word it falls back to a
byte-by-byte comparison and returns the signed difference normalized to `-1 / 0 / +1`
(`result = result|1` over a `-borrow`). Equal pointers short-circuit to `0`. Net
behavior is identical to a standard `strcmp` (`<0`, `0`, `>0`), just optimized.

### 3.3 `VIBE_Util_StrCmpNoCase @0x5cb8f0`

Case-insensitive compare with **ASCII-only** folding:

```c
for each position:
    a = *p1; b = *p2;
    if (a >= 'A' && a <= 'Z') a += 32;   // fold upper→lower, ASCII only
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b || b == 0) break;
    ++p1; ++p2;
return a - b;
```

Only bytes `0x41..0x5A` are folded; bytes ≥ `0x80` (accented Latin-1) are compared
as-is. Returns the signed difference of the (folded) bytes at the first mismatch.

### 3.4 `VIBE_Util_StrToUpper @0x5e9f50`

In-place uppercase, ASCII-only: for each byte, `v = c - 'a'; if (v <= 25) c = v + 'A';`
i.e. only `'a'..'z'` are mapped to `'A'..'Z'`; everything else (including high Latin-1) is
left untouched. Returns the original pointer.

### 3.5 `VIBE_Util_ParseInt @0x5dc070`

A hand-rolled `atoi`. It uses the CRT character-class table `byte_64A208`
(indexed by `byte + 1`):

```c
while (ctype[*p + 1] & 0x02) ++p;     // skip leading whitespace (class bit 0x02)
sign = (*p=='+' || *p=='-'); if (sign) ++p;
v = 0;
while (ctype[*p + 1] & 0x20) {        // while digit (class bit 0x20)
    v = 10*v + (*p++ - '0');
}
return (first char was '-') ? -v : v;
```

Accumulates in a signed `int` (decimal only, no overflow check, base-10 only), honors a
leading `+`/`-`, and stops at the first non-digit. The `0x02` bit is "whitespace" and
`0x20` is "digit" in the runtime ctype table.

---

## 4. CRT formatting — `sprintf` and the core formatter

### 4.1 `VIBE_Crt_Sprintf_0 @0x5cba00` (788 xrefs)

The ubiquitous `sprintf` wrapper — **788 cross-references**, used everywhere strings are
built.

```c
// VIBE_Crt_Sprintf_0 @0x5cba00
int VIBE_Crt_Sprintf_0(char *dst, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    return VIBE_Crt_Vsprintf(dst, fmt, &ap);
}
```

It is `sprintf(dst, fmt, ...)` — **no length bound** (the destination buffer must be
large enough; this is the original's behavior and a known footgun to preserve).

### 4.2 `VIBE_Crt_Vsprintf @0x5f8554`

```c
int VIBE_Crt_Vsprintf(char *dst, const char *fmt, int *ap) {
    int n = VIBE_Crt_FormatStringCore(dst, fmt, VIBE_Buffer_PutChar, ap);
    dst[n] = '\0';      // NUL-terminate
    return n;           // character count
}
```

It drives the shared formatter with a **sink callback** `VIBE_Buffer_PutChar @0x5f8540`
that appends one byte to `dst`, then writes the terminating NUL and returns the length.
The same `FormatStringCore` is shared by the `printf`/`fprintf` family with different
sinks.

### 4.3 The engine — `VIBE_Crt_FormatStringCore @0x6051f0`

This is a full, MSVC-lineage `printf` formatter. Pipeline:

1. Copy literal bytes to the sink until `'%'`.
2. `VIBE_Crt_ParseFormatSpec @0x605598` parses **flags → width → precision → length →
   conversion**, then `VIBE_Crt_FormatConversion @0x605a18` renders the argument into a
   scratch buffer, and the core emits left/right padding around it.
3. `%n` is handled specially (writes the running output count back through the pointer
   argument, honoring the width/length modifiers).

**Flags** (parsed in `ParseFormatSpec` / `VIBE_Crt_ParseFormatFlags @0x6056ec`):
`-` (left-justify, flag bit `0x08`), `+` (force sign, bit `0x04`), space (sign-pad, bit
`0x02`), `#` (alt form, bit `0x01`), `0` (zero-pad). **Width** and **precision** accept a
decimal count or `*` (consume an `int` arg; a negative `*` width flips to left-justify).

**Length modifiers** (set flag bits used by the conversion):
- `h` → `short` (bit `0x10`)
- `l`, `w` → wide/`long` (bit `0x20`)
- `L`, `I64` → 64-bit (`0x31`-byte flag bit `0x01`); the `I64` form is parsed explicitly
  (`I`, `6`, `4`).
- `N`, `F` → near/far pointer modifiers (bits `0x40` / `0x80`) — 16-bit legacy, retained.

**Conversion specifiers** handled by `VIBE_Crt_FormatConversion @0x605a18`:

| Specifier        | Handling |
| ---------------- | -------- |
| `d`, `i`         | signed decimal (`VIBE_String_UIntToString` / `UInt64ToString`, base 10) |
| `u`              | unsigned decimal |
| `o`              | unsigned octal (base 8; `#` emits leading `0`) |
| `x` / `X`        | unsigned hex (base 16; `#` emits `0x`/`0X`; `X` upper-cases via `VIBE_Crt_StrToUpper`) |
| `p` / `P`        | pointer — `VIBE_Crt_FormatPointer`, default precision 8 (or 13 with `#`/`N:`) |
| `c`              | single byte char; `C`/wide path via `VIBE_String_WideCharToBytes` |
| `s`              | byte string (length via `VIBE_Crt_StrNLen`, capped by precision) |
| `S`              | wide string (`VIBE_Crt_WideStrByteLen` / `EmitWideString`) |
| `e`,`E`,`f`,`g`,`G` | floating point via `VIBE_Crt_FormatFixedFloat @0x605848` (the `h` flag here routes a fixed-point variant) |
| `n`              | store output count back through the arg pointer |
| anything else    | emitted literally (the `%X` passthrough at `LABEL_142`) |

Sign handling for integers: a `'-'` is emitted for negatives (with 64-bit two's-complement
negation when the `I64` flag is set), else `'+'` or space per the flags. Width padding
is then applied with the fill char (`'0'` or space) on the correct side depending on the
left-justify flag; precision controls minimum digit count / max string length.

> Reproduction note: the formatter is a faithful translation of the original CRT printf,
> including its quirks (unbounded `sprintf`, ASCII-only `%X` upper-casing, the legacy
> `N`/`F` near/far modifiers, `%I64`). Match its output byte-for-byte; several save-file
> and log code paths compare formatted strings.

---

## 5. Summary table

| Function                          | Addr        | Role |
| --------------------------------- | ----------- | ---- |
| `VIBE_Util_GetRandStatePtr`       | `0x5cb8b0`  | seed word = `[off_64A90C]+0x0C` |
| `VIBE_Util_RandNext`              | `0x5cb8bc`  | LCG step `s=1103515245*s+12345`, ret `(s>>16)&0x7FFF` |
| `VIBE_Util_RandSeed`              | `0x5cb8e0`  | `srand` — direct store of seed |
| `VIBE_Math_RandomModulo`          | `0x58b89c`  | `RandNext()%bound` (790 xrefs) |
| `VIBE_Util_RandomModulo`          | `0x404b68`  | duplicate of the above |
| `VIBE_Vector_Copy3`               | `0x407414`  | copy 3 DWORDs |
| `VIBE_Math_VectorNormalize`       | `0x5cb148`  | normalize, zero on degenerate |
| `VIBE_Math_MatrixIdentity`        | `0x5cb100`  | 64-byte zero + diag `1.0` at 0/5/10/15 |
| `VIBE_Math_MatrixFromEuler`       | `0x5cb1bc`  | ZYX rotation matrix from Euler |
| `VIBE_Math_MatrixTransformVectors`| `0x5caaa4`  | compose affine 4×4 (+translation) |
| `VIBE_Math_NormalizeAngle`        | `0x5eef4c`  | wrap angle ≥0 |
| `VIBE_Math_VectorAngleWrapped`    | `0x5ca504`  | angle between, +2π if < −π |
| `VIBE_Math_AcosGuarded`           | `0x5f0b9c`  | domain-safe `acos` |
| `VIBE_Math_Fmod`                  | `0x5d3fb2`  | x87 `fprem` remainder |
| `VIBE_Coord_ConvertX`             | `0x5c6b08`  | `frndint` round-to-nearest-even |
| `VIBE_Coord_ProjectPoint`         | `0x407428`  | world→screen with +0.5 bias |
| `VIBE_Coord_ProjectFramePoint`    | `0x407488`  | tile→world→screen |
| `VIBE_Util_StrChr`                | `0x5d3ef0`  | last-occurrence (`strrchr`) |
| `VIBE_Util_StrCmp`                | `0x5d3f10`  | word-at-a-time `strcmp` |
| `VIBE_Util_StrCmpNoCase`          | `0x5cb8f0`  | ASCII-only case-insensitive cmp |
| `VIBE_Util_StrToUpper`            | `0x5e9f50`  | ASCII-only in-place upper |
| `VIBE_Util_ParseInt`              | `0x5dc070`  | `atoi` via ctype table `byte_64A208` |
| `VIBE_Crt_Sprintf_0`              | `0x5cba00`  | `sprintf` wrapper (788 xrefs) |
| `VIBE_Crt_Vsprintf`               | `0x5f8554`  | drives core, NUL-terminates |
| `VIBE_Crt_FormatStringCore`       | `0x6051f0`  | the printf engine |
| `VIBE_Crt_ParseFormatSpec`        | `0x605598`  | flags/width/precision/length parse |
| `VIBE_Crt_FormatConversion`       | `0x605a18`  | per-specifier rendering |
