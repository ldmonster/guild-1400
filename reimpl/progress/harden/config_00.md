# Harden — config_00

IDA-verified pass over `src/config/` (ini, cmdline, config, cpu, errorlog,
provider, registry). Suite green 1558/1558 after.

## Real divergence found + fixed

**`ini.cpp` resolution table `dword_63D70C`** — the table is EXACTLY 3 entries
(24 bytes): `(800,600), (1024,768), (1152,864)`. An earlier reconstruction had 6
rows, inventing `(0,0),(800,600),(0,0)` for idx 3-5. Those "rows" are actually the
SEPARATE named globals that immediately follow the table — `byte_63D724` (cur_res),
then the two OUTPUT dwords `dword_63D728`/`dword_63D72C` — which the decompile of
`VIBE_Config_ReadGfxAndSoundSettings @0x56bbbe` references by name, not as `tab[6..8]`.
Confirmed against raw bytes `[320 258 400 300 480 360 | 00.. 320 258 ..]`. Fixed the
reimpl to 3 entries. (Engine indexing is UNBOUNDED — `tab[2*(u8)cur_res]`, no range
check — so idx≥3 reads those adjacent vars; the reimpl keeps its defensive clamp to
0 for invalid indices, which the config tests already pin for idx≥6.)

## Verified 1:1 (no change needed)

- **`ReadGfxAndSoundSettings @0x56b834`** — every `[Gfx]/[Sound]/[Game]` key,
  default, and order matches: `character_detail` def 1, `brightness_a` def 50,
  `contrast_*`/`gamma_*` def 100, the `invert_mouse := 0` force-reset after reading,
  `stadt` default "Augsburg", and the tail `difficulty`/`hints`/`panel_help` (all def 1).
  Engine uses `GetPrivateProfileIntA`; the reimpl's `ParseProfileInt` reconstructs it
  and matches for all realistic (non-negative) values.
- **`cmdline.cpp`** — `VIBE_Util_StrToUpper(cmdline)` before scanning (uppercase ✓),
  STADT/BERUF/IP/PORT quoted-value extraction, and `PORT` via `VIBE_Util_ParseInt`
  (atoi-style) ✓. config_test cmdline cases pass.

## Noted (not changed — sub-ULP, unobservable for valid input)

- `ReadGfxAndSoundSettings` computes `(double)GetPrivateProfileIntA(...) * flt_625200`
  in **double**; the reimpl does `getInt(...) * 0.01f` in **float**. For the value
  range that ships (0..100 brightness/contrast/gamma) the narrowed float result is
  identical; a divergence could only appear for out-of-range garbage. Left as-is to
  avoid churn (flagged for a future exhaustive pass if wanted).
