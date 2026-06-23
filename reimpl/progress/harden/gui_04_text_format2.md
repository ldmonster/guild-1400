# Harden — gui/text/text_format2.cpp (1:1 sweep, MCP live)

Module: `src/gui/text/text_format2.cpp` (+ `text_format2.h`, tests
`tests/unit/gui_text_format2_test.cpp`, `tests/e2e/gui_text_format2_e2e_test.cpp`).

Method: DECOMPILE + DISASM each provenance'd function, diff line-for-line against
the binary; constants confirmed via `get_bytes` / `get_global_value`. DISASM wins.

## Per-function verdicts

### FormatDigitPair @0x5faafe — FIXED
- Disasm (`get_bytes` confirms `f6 f1` = `div cl`):
  - `cmp al, cl` (cl=10) tests ONLY the low byte.
  - jb taken (low byte < 10): after `xchg al,ah`, `add ah/al,'0'` write
    out[0]=high byte, out[1]=low byte (no DIV).
  - jb not taken: `div cl` divides the FULL 16-bit AX (=value) by 10 →
    AL=quotient→out[0], AH=remainder→out[1].
- BEFORE: div branch used only the low byte: `tens=al/10; units=al%10`.
- AFTER: div branch divides the full 16-bit value: `tens=value/10; units=value%10`.
  jb-taken branch unchanged (already correct: tens=high byte, units=low byte).
- Behavior-identical for all valid in-range callers (high byte is 0 there), now
  exactly 1:1 for the full 16-bit dividend. Existing goldens unaffected.
- Added goldens `DigitPairHighByte` locking the high-byte-as-tens path and the
  full-AX DIV path (value=266 → 266/10=26, 266%10=6 → out[0]=(char)(26+'0'),
  out[1]='6'), which the old low-byte-only code would have rendered as "10".

### FormatDigitPair2 @0x5faae7 — VERIFIED-1:1
- Falls through into FormatDigitPair (chain). `cmp eax, ecx` (ecx=100) on 32-bit;
  `sub edx,edx`/`xchg` force DX=0 then `div cx` → 16-bit divide of (u16)value by 100.
  Source: `value<100u` guard, `u16 v=(u16)value; hi=v/100; lo=v%100`. Matches.
- Call order: hi pair first (via call), then lo pair (via fall-through). Matches.

### FormatDigitPair3 @0x5faad1 — VERIFIED-1:1
- `cmp eax, ecx` (ecx=0x2710) 32-bit; `div ecx` 32-bit (EDX=0). Source:
  `value<0x2710u`, `hi=value/0x2710; lo=value%0x2710` (u32). Matches. hi then lo.

### StripNameTokens @0x4f8d98 — VERIFIED-1:1
- marker init 32; loop stops on `*p==0` or after 2 dashes (`v8>=2`).
- '-' (45): ++dashes, ++p. '%' (37) with marker==32: copy '%' verbatim, consume.
- '%' with marker!=32: emit space at dst, dst+=2, marker=*p++, place marker at
  dst[1] (v11=old dst+1). else: marker=*p++, copy ch. Outputs `consumed=p-src`,
  `tailLen=strlen(p)`. All branches/side-effect order match decompile.

### TrimTrailingSpace @0x59c270 — VERIFIED-1:1
- `idx>=0` guard; backward walk while `kCharClass[(u8)(*q+1)] & 0x20`; `--idx;--q`.
  `split=idx+1`; `tailCount = len - split + 1`; memcpy(dst, src+split, tailCount);
  `dst[tailCount]=0`; return split. Matches `*((BYTE*)a3+v4-v6+1)=0; return v6`.
- Table reuse: `guild::sim::kCharClass` byte-verified equal to `byte_64A208`
  (`get_bytes 0x64a208` 256 B — digit class 0x38 has bit 0x20 set).

### GetCurrentIconWord @0x5a3418 — VERIFIED-1:1
- Returns word_649D48. `get_global_value 0x649d48` = 0xFFFF (BSS init). Source
  `g_currentIconWord = (i16)0xFFFF`. Matches.

### AppendWideLines @0x411ee4 — VERIFIED-1:1
- Clears caption[0]=0; if count>0 loop: 2-byte unrolled strcat of `line`, then of
  separator. Separator `unk_610E3C` confirmed = 0x7c '|' (`get_bytes 0x610e3c`).
  `result` is assigned ONLY in the separator loop (line loop uses v8/v9); ends 0
  after the final terminator copy. Source mirrors exactly.
- Boundary note (not a divergence): original computes the caption base as
  `740*a1 + dword_69FFB4 + 216` (widget array stride 740, base word_69FFB4=0 in
  IDB). The reconstruction takes the already-resolved `caption` (+216) pointer as a
  parameter; the widget-array indexing belongs to the caller. Behavior identical.

## Counts
- Functions verified: 7 (FormatDigitPair, FormatDigitPair2, FormatDigitPair3,
  StripNameTokens, TrimTrailingSpace, GetCurrentIconWord, AppendWideLines).
- FIXED: 1 (FormatDigitPair — full 16-bit DIV dividend).
- VERIFIED-1:1: 6.
- BOUNDARY: 0 (AppendWideLines widget-base indexing noted as caller's concern).
- Constants/tables byte-confirmed: 3 (0x610e3c='|', 0x649d48=0xFFFF, 0x64a208 256B).
- Tests: unit suite GuiTextFormat2 (+ new DigitPairHighByte case) and e2e
  GuiTextFormat2E2E — all passing.
