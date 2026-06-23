# Harden chunk: gui_04_slider — slider/scrollbar value model

Files owned: `src/gui/slider.cpp`, `src/gui/scrollbar.cpp` (+ headers).
Method: DECOMPILE + DISASM each provenance'd function via IDA MCP (module gilde.exe),
diff line-for-line. DISASM is the reference of record. Every immediate verified at its
`cmp`/`mov` site in the disassembly.

## src/gui/slider.cpp

### `Slider_ComputeStep` @0x41df08 — VERIFIED-1:1
Span = `*(rec+0x18) - *(rec+0x1C)` (taken as param). 12-bucket table confirmed from disasm
immediates (`cmp edx, imm` / `jge` signed) → return immediates (`mov eax, imm`):
| span >= (dec / hex)        | step (dec / hex) |
|----------------------------|------------------|
| 125000 / 0x1E848           | 10000 / 0x2710   |
| 100000 / 0x186A0           |  5000 / 0x1388   |
|  75000 / 0x124F8           |  4000 / 0x0FA0   |
|  50000 / 0x0C350           |  3000 / 0x0BB8   |
|  37500 / 0x0927C           |  2000 / 0x07D0   |
|  25000 / 0x061A8           |  1500 / 0x05DC   |
|  12500 / 0x030D4           |  1000 / 0x03E8   |
|   5000 / 0x01388           |   500 / 0x01F4   |
|   2500 / 0x009C4           |   200 / 0x000C8  |
|   1000 / 0x003E8           |   100 / 0x00064  |
|    250 / 0x000FA           |    50 / 0x00032  |
|    100 / 0x00064           |    10 / 0x0000A  |
| else                       |     1            |
All compares signed `jge`; last bucket uses `jl ...; mov eax,0Ah`. Default `eax=1`. Exact.

### `Scrollbar_ValueFromThumb` @0x420270 (value-math view) — VERIFIED-1:1
`step = ComputeStep(max-min)`. Disasm computes the shift on `delta = thumbHi - thumbLo`
(`sub edx,ebx` then `neg edx` => thumbHi-thumbLo), then the
`sar/shl/sbb/sar 2` round-toward-zero idiom = signed `delta/4` toward zero. C `delta/4` on
signed int rounds toward zero — identical. `value = step*(delta/4) + base`, clamp [min,max].
(The full sentinel branch lives in scrollbar.cpp's SetThumbPosition; this view's plain
[min,max] clamp is a faithful subset.)

### `Slider_QuantizeRange` @0x41dfec (slider block of VIBE_Object_SetValueOrText) — VERIFIED-1:1
Disasm @0x41e0e3..0x41e188 confirms, in order, all signed `idiv` (sar edx,1Fh; idiv ebx):
- `v15 = ComputeStep(rec)`
- `v16(max') = rec24 - (rec24-rec28) % step`  (idiv, signed)
- `v17(min') = (v16-rec28) % step + rec28`     (idiv, signed)
- `v18(val') = step * (rec296 / step)`          (idiv, signed; snap toward zero)
- store rec24=v16, rec28=v17, rec296=v18
- `steps = (rec24-rec28) / step` (idiv); `cmp eax,19h (25); jg`  → if >25
- 25-step clamp @0x41e162: `(((step<<2)-step)<<3)+step = 25*step`; writes only rec24(max).
- Final clamp @0x41e13f: skip iff `rec296 == -1 && (flags & 0x40)`; else clamp value up to
  min when `min > value` (`cmp eax,edx; jle` keeps, else value=min).
Source matches modulo/division signedness, the 25-step max-only clamp, and the -1/flag
sentinel branch exactly.

## src/gui/scrollbar.cpp

### `Scrollbar_SetThumbPosition` @0x420270 — VERIFIED-1:1
Field offsets confirmed: max=+0x18, min=+0x1C, flags=+0x26, value=+0x128, text=+0x28.
Shift idiom = signed `delta/4` toward zero (see above). Clamp logic from disasm:
- `cmp edi,max; jg` → if v5>max, value=max.
- else `cmp edi,min; jge → skip` (clamp only when v5<min).
- when v5<min: `test [+0x26],40h; jz → clamp`; if flag set: `cmp edi,-1; jnz → clamp`;
  `test min; jnz → clamp`; `test max; jnz → clamp`.
  ⇒ leave value untouched ONLY iff (flag&0x40) && v5==-1 && min==0 && max==0.
Source condition `v5 < v7 && ((flags&0x40)==0 || v5!=-1 || v7!=0 || v6!=0)` is the exact
de Morgan complement. Exact.

### int→string @0x5d92ec (VIBE_AnimationState_Update) + @0x5d92a0 (VIBE_Util_IntToStringRadix) — VERIFIED-1:1
Base-10 path: if value<0, emit '-', `neg eax` (two's-complement; INT_MIN stays 0x80000000
and is consumed as unsigned by `div`). Radix loop: `div dword[ebx]` (unsigned), digit =
`byte_64A1D0[rem]` (table @0x64A1D0 = "0123456789abc...xyz"; base-10 uses only '0'..'9'),
least-significant first, then reverse-copy to dest, NUL-terminated. Source
`AnimationState_Update10` reproduces this exactly: `-(long long)value` cast to unsigned
matches the unsigned wrap of `neg` for INT_MIN; `'0' + u%10` == table lookup for 0..9.

## Counts
- Functions audited: 5 (0x41df08, 0x420270 value-math, 0x41dfec block, 0x420270 full
  SetThumbPosition, 0x5d92ec+0x5d92a0).
- VERIFIED-1:1: 5
- FIXED: 0
- BOUNDARY: 0
- Source edits: none required — reconstruction already byte-faithful.

## Build / test
No dedicated slider/scrollbar test exists (only `tests/unit/gui_widget_interact_test.cpp`
sanity-pins `Slider_ComputeStep`). Built `gui_widget_interact_test` (links the slider/
scrollbar TU) — clean. `ctest -R gui_widget_interact_test`: 1/1 passed.
