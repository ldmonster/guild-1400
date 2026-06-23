# Hardening sweep — GUI text format/parse chunk

Files: `src/gui/text/format.cpp`, `src/gui/text/richtext.cpp`, `src/gui/text/markup.cpp`
(+ headers + dedicated tests `tests/unit/gui_text_test.cpp`, `tests/e2e/gui_text_e2e_test.cpp`).

MCP live; DISASM treated as reference of record over Hex-Rays.

## Per-function verdicts

### format.cpp

| Function | Addr | Verdict |
|---|---|---|
| `FormatGroupedInt` | inline @0x59d6e8 (lines 1199-1202) | **VERIFIED-1:1** |
| `FormatMoney` | 0x58f798 | **VERIFIED-1:1** (divisor BOUNDARY) |
| `PackDateRecord` | 0x583304 | **VERIFIED-1:1** |
| `SeasonFromYear` | 0x583384 | **VERIFIED-1:1** |

- **FormatGroupedInt** — matches the inline `%i` (field-less) loop byte-for-byte:
  `Sprintf("%i")`, `outLen=(strlen-1)/3+strlen`, `dst=outLen`, `rightDigits=-1`,
  loop `j=len; j>=0; ++rightDigits`, copy `dec[j]`, separator guard
  `j && rightDigits>0 && (rightDigits+1)%3==0` -> '.' (0x2E), `out[outLen]=0`.
- **FormatMoney** — `mag=(float)abs(a1)`; `scaled=(double)mag/divisor + 0.5`
  (`dbl_6269BC`=0.5, get_bytes `00 00 00 00 00 00 E0 3F`); truncated by ConvertX@0x5c6b08.
  ConvertX disasm: sets FCW high byte 0x1F -> RC=11 (round-toward-zero) round + `fistp`
  => `(int)` truncate. Sign branch on `a1<0`, sub-branches `==0` ("0%c"=`off_6269A4`
  `30 25 63 00`), `<1000` (`-%i%c`/`%i%c`), else grouped (`-%s%c`/`%s%c`). All format
  strings confirmed via get_bytes (`62699C`,`626994`,`6269B0`,`6269A8`). The grouped-len
  float path `(double)len + (double)(len-1)*flt_6269C4` (1/3, `AB AA AA 3E`) truncated ==
  integer `(len-1)/3+len` for all realistic lengths; itoa via `VIBE_AnimationState_Update`
  (base-10, no sign for positive magnitude). Divisor table `dword_13CD6F2[189*cur]>>16 ->
  dword_649A88[]` is runtime economy data not in tree -> exposed as explicit arg (BOUNDARY,
  documented in header).
- **PackDateRecord / SeasonFromYear** — field offsets, `%4`, `*3+1`, `+1400`, `>>16 %4`
  all match the decompile exactly.

### richtext.cpp — FIXED (wrong code-letter table)

| Function | Addr | Verdict |
|---|---|---|
| `RenderRichString` (`%`-code dispatch) | 0x59d6e8 | **FIXED** |
| `ProduceDate` | 0x59d6e8 line 549 | **FIXED** |
| `RenderFormattedMessage` | 0x59f99c | VERIFIED-1:1 (id resolve + delegate) |
| `%%` / `%i` / `%a` arms | 0x59d6e8 | VERIFIED-1:1 |

Decoded the real `%`-letter dispatch from the disasm comparison tree (root 0x59d837,
al = letter after the optional field digit `v209`). Evidence — disasm `cmp al,imm`/jcc
chain + leaf calls:

- `%%` (0x25, line 625) -> literal byte 0x16. **was correct, kept.**
- `%i` (0x69, lines 1199-1202) -> grouped int (field==-1). **was correct, kept.**
- `%a` (0x61, line 969, prev=='%') -> `"%i%c"` icon 0x14. **was correct, kept.**
- `%S`/`%T` (0x53/0x54, line 374, prev=='%') -> **MONEY** `VIBE_Money_FormatWithSeparators`.
  Source previously mapped money to `%m` -> WRONG. Disasm: `0x59e2fa cmp al,0x53; jnb`,
  `0x59e3cc ja 0x59e419`, `0x59e419 cmp al,0x55; jnb`, fall-through (al==0x54) ->
  `0x59e41d cmp prev,'%'; jz` money leaf (call to 0x58f798). 'S' aliases the same leaf.
- `%D` (0x44, line 549) -> **DATE** `PackToRecord` + `dword_8C37E0[GetSeasonFromYear(v204)]`
  -> `"%s %i"` season,year. Source previously mapped date to `%T` -> WRONG. Disasm:
  `0x59d83f cmp al,0x47; jnb`, `0x59e081 ja`, then `cmp al,0x43; cmp al<0x45` => al==0x44.
  Season index = `(v204[0]>>16)%4` = `rec.year%4` (NOT `yearQuarter%4` as before);
  `ProduceDate` now uses `SeasonFromYear(rec.year<<16)`.
- No standalone `%s`/`%m`/`%c` value code exists. Removed the bogus `%s`->db->Text and
  `%m`->money arms; an unhandled letter falls to the default arm (copies '%' verbatim,
  nothing dropped) — matches the decompile's "Unknown textparameter" reporting path.

Deferred (Rule 8, runtime data not in tree): the `%s`-style substitution via the
`v215` case/gender state machine + `VIBE_String_GetDelimitedField` over
`dword_8C36B0`/`dword_8C379C`/`dword_8C3790`/`dword_8C37A8`; the `%f` fixed-point float
leaf; `%N` item label; `%W` font/window select; `{rN}`/`%r` random pick over
`dword_8C36B0`+`byte_767EB0`+`dword_62EB24`. Documented in header.

### markup.cpp — FIXED (one wrong `$`-letter)

| Function | Addr | Verdict |
|---|---|---|
| `TokenizeMarkup` / `ClassifyDollar` | 0x416720 | **FIXED** ('=') |
| `$`-letters `<` A B C F L M R T Y [ ] i t | 0x416720 | VERIFIED-1:1 |
| `$i`/`$t` selector + `[label]` + "Missing ']'" | 0x416720 | VERIFIED-1:1 |
| `%`-letter -> PercentCode + digit arg | 0x416720 | VERIFIED-1:1 |

- `$=` (0x3D): source mapped to `BoundLeft` -> WRONG. Disasm `0x4169da cmp al,0x3C; jnb
  0x416f71`, `0x416f71 ja 0x41702B` (al==0x3C '<' is a valid metric token), `0x41702b
  cmp al,0x3E; jb 0x416bdd` routes al==0x3D to the "Unknown textparameter: %%%c" leaf
  (along with '>' 0x3E? '?' '@'). Removed the `0x3D` case -> falls to `default -> Unknown`.
- All other `$`-letters confirmed against the disasm tree: `<`(0x3C metric),`A`(0x41
  LineFeed),`B`(0x42 ColumnCenter v204=128),`C`(0x43 Clear),`F`(0x46 FontColor),`L`(0x4C
  ColumnReset),`M`(0x4D Embed),`R`(0x52 ColumnRight v204=64),`T`(0x54 Tab/metric "M"),
  `Y`(0x59 ColumnFull 0x100),`[`(0x5B),`]`(0x5D),`i`(0x69 Inline; 'a'/'n' -> red button),
  `t`(0x74 EditField).

## Goldens fixed (to the binary, with evidence)

- `tests/unit/gui_text_test.cpp`: `MoneyCode` now `%T`/`%S`; `DateCode` now `%D`
  (season=year%4); replaced `StringSubstitution`(`%s`) with `UnknownCodePassThrough`
  (`%s` copies through verbatim).
- `tests/e2e/gui_text_e2e_test.cpp`: `MixedRichString` uses `%i %T(money) %a %D`;
  `FormattedMessageById` uses `%T` for money.

## Counts

- Functions verified/fixed: format.cpp 4 VERIFIED; richtext.cpp 2 FIXED + 4 VERIFIED;
  markup.cpp 1 FIXED + 4 VERIFIED.
- BOUNDARY: 1 (money divisor table), plus richtext deferred substitution/float/random leaves.
- Tests: gui_text_test, gui_text_e2e_test PASS; regression check on
  gui_text_format2_test, real_text_driver_test, play_text_recon_test,
  misc_recon4_textlayout_test, charintro_markup_test all PASS; `guild` lib builds clean.
