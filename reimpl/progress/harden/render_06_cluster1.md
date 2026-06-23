# Harden sweep — render_06_cluster1 (shapebank / shape blit / RLE / showbank)

MCP-driven line-for-line diff of every `gilde.exe 0xADDR` function in:
- src/render/shapebank.cpp
- src/render/shape_blit.cpp
- src/render/shape.cpp
- src/render/shape_showbank_misc_recon.cpp

Test targets built + green: `render_shape_blit_test`, `shape_showbank_misc_recon_test`
(plus `render_camera_test`, which owns the shapebank unit tests touched by the fix).

---

## src/render/shapebank.cpp

### 0x5D8330 VIBE_ShapeBank_AddShape — VERIFIED-1:1
Diffed against disasm 0x5d8330..0x5d8438.
- count gate `cmp word [+0x2a],0xFF; jnb` -> `count >= 0xFF`. OK.
- lazy-init predicate `[+0x30]==0 || [+0x2a]==0`. OK (kWriteCursor=0x30, kShapeCount=0x2a).
- writeCursor init = 0x845 = 2117. OK. magic "SHAPBANK"@+0, version=1@+8.
- colorDepth: shape[+0x0c] -> bank[+0x34]; light table bank+0x445 <- byte_1406530, 0x400 bytes,
  only when depth==0. OK.
- depth-mismatch gate, copy shape[size@+0] to bank+cursor, TableSlot(count)=bank+0x45+4*count=cursor,
  cursor += size, max-width (shape+6 vs bank+0x2c, signed jle), max-height (shape+0x0a vs bank+0x2e,
  jg), ++count, return 1. All match. The decompiler's trailing `while (v7 < count) ++v7` is dead
  code (compiler artifact); net effect is ++count.

### 0x5D843C VIBE_ShapeBank_AppendShape — VERIFIED-1:1
Diffed against disasm 0x5d843c..0x5d84f1.
- table-tail MemMove(dst=bank+0x45+4*(index+1), src=bank+0x45+4*index, (count-index)*4). OK.
- TableSlot(index)=writeCursor, copy shape[size]@cursor, writeCursor+=size, ++count. OK.
- max-width/height updates. OK.
- Return value: function always leaves `ax = shape height (shape+0x0a)` in both the update
  (0x84df) and no-update (0x84bc fall-through) paths -> returns shape height. Recon returns `h`
  (= GetU16(shape,kHeight)). Match.

### 0x5D84F4 VIBE_ShapeBank_RemoveShape — FIXED
Diffed against disasm 0x5d84f4..0x5d85b7.
Most of the function verified 1:1 (null guard returns 0; `cmp count,index; jl` signed -> return 0
when count<index; last-shape branch SetGrayColorThunk(fill=0,count=size,dst=shapePtr) == memset
0; non-last MemMove(shapePtr, shapePtr+size, writeCursor-removedOffset); offset-decrement loop
`offset[i] > removedOffset (unsigned ja) -> offset[i]-=size` for i in [0,count); table-compaction
MemMove).

DIVERGENCE (final cleanup, 0x5d8590-0x5d859e):
```
5d8592 mov ax,[ecx+2Ah]                 ; ax = OLD count
5d8596 mov dword [ecx+eax*4+45h],0      ; zero TableSlot(OLD count)
5d859e dec word [ecx+2Ah]               ; THEN count--
```
The binary zeroes `TableSlot(count)` using the OLD (pre-decrement) count, i.e. the slot ONE PAST
the last live entry, and the `dec` happens AFTER the zero.

Before (recon): decremented count first, then zeroed `TableSlot(count-1)` — wrong index AND wrong
order:
```cpp
SetU16(kShapeCount, count - 1);
SetU32(TableSlot(GetU16(kShapeCount)), 0);   // = TableSlot(count-1)
```
After (recon): match the binary exactly:
```cpp
SetU32(TableSlot(count), 0);                  // OLD count, before decrement
SetU16(kShapeCount, static_cast<u16>(count - 1));
```
Evidence: disasm bytes above. (After the compaction MemMove, slot[count-1] legitimately holds the
shifted-in value from old slot[count]; the binary preserves that and clears slot[count]. The recon
was clobbering slot[count-1] instead.)
The existing golden `RenderShapeBankUnit.RemoveShapeCompacts` does not assert on the cleared slot
contents (both indices were 0 in that scenario) so it stays valid and green; no golden change
needed.

---

## src/render/shape_blit.cpp

### 0x5D7164 VIBE_Shape_BlitColored16 — VERIFIED-1:1
Diffed against disasm 0x5d716f..0x5d72e7 incl. the FPU/luma block 0x5d71fa..0x5d7256.
- dst byte ptr v23 = pixels(+0x1c) + 2*(widthPx(+0x10)*y + x); modelled by `dst.pixels` (u16*) +
  (widthPx*y+x). OK.
- per-run: `v7 += 2*(*v26>>1)` (skip>>1 pixels), inner copies v26[1] pixels, `v26 += 2*v26[1]+8`.
  Row: `v23 += 2*widthPx`, `v24=v26; ++v26` (v26 is dword* so +4). OK.
- LUMA / channel mapping (the high-risk part). UnpackColor(pixel@eax, r@edx, g@ecx, b@ebx). The
  call passes ecx=var_34, ebx=var_30, edx=var_2C => r->var_2C, g->var_34, b->var_30. The FPU code
  computes `var_2C*0.2 + var_30*0.59 + var_34*0.2` = r*0.2 + b*0.59 + g*0.2. Recon's
  `r*kWLow + b*kWHigh + g*kWLow` with kWLow=0.2, kWHigh=0.59 matches EXACTLY.
- Weights byte-verified: dbl_629478 = 9A 99 99 99 99 99 C9 3F = 0.2; dbl_629480 =
  E1 7A 14 AE 47 E1 E2 3F = 0.59.
- float->int: ConvertX@0x5c6b08 sets FPU control word RC=11 (truncate toward zero: HIBYTE=31 =>
  0x1Fxx) then `frndint`, restores CW; the subsequent `fistp` stores the already-integral value.
  Net = trunc-toward-zero. Recon `(int)v` (ChopToInt) is identical for the non-negative luma here
  (and for negatives both truncate toward zero). OK.
- Return = `ax` = `*(u16*)(shape+0x0a)` (height), preserved when height<=rowIdx at top of loop.
  Recon returns `height`. OK.
Golden `BlitColored16Golden` / `ColoredLumaSinglePixels` confirm the weighting and truncation and
remain green.

### 0x43768C VIBE_Render_Convert8To16Indexed — VERIFIED-1:1 (one documented degenerate-edge note)
Diffed against decompile 0x43769f..0x437809.
- field derivation per dest mask: trailing-zero count = field pos, set-bit count = precision; drops
  = 8-bits (v19=8-j red, v16=8-m green, v17=8-ii blue). OK.
- inner pack: `(G>>gDrop<<gPos) | (R>>rDrop<<rPos) | (B>>bDrop<<bPos)` with G first in the OR,
  matching the binary's `(a3+4*idx+1)>>v16<<k | (a3+4*idx)>>v19<<i | (a3+4*idx+2)>>v17<<n`.
  Palette stride 4 (R@+0,G@+1,B@+2). Shifts of `(int)(unsigned __int8)` are non-negative => no
  signed-shift hazard. OK.
- dst index = widthPx*row + base (a1[4]*v15 + a1[9]); src = pixels + widthPx*row (a2[9]+a2[4]*v15).
  Loops: row<height(a2[2]), col<width(a2[3]). OK.
- Return value: binary returns `result` = the last packed pixel in the normal path; when
  height>0 but width==0 the binary returns `v10 + a1[9]` (the per-row dest BYTE pointer, reassigned
  at the top of each row), whereas recon returns 0. This only differs for a zero-WIDTH conversion
  block (no pixels written) — a degenerate input that does not occur in the live render path
  (BlitConvertDispatch only reaches here for real 8bpp->16bpp surface blocks), and the
  pixel-writing behaviour is byte-identical in all real cases. Recon models the dest as
  pixel-stride/base (u16*) rather than the original's byte pointer, so reproducing that one
  out-of-band pointer-valued return is not meaningful in the portable model. Documented edge; not
  a behavioural divergence for any in-tree caller.
- Defensive note: the recon's trailing-zero lambda adds `&& v != 0` (the binary omits it and would
  spin on a zero mask); identical for all valid contiguous channel masks.

---

## src/render/shape.cpp

### 0x5D70CC VIBE_Shape_DecodeRle — VERIFIED-1:1
Diffed against decompile 0x5d70e0..0x5d715d.
- dst v11 = pixels(+0x1c) + 2*(widthPx(+0x10)*y + x); height = *(u16*)(shape+0x0a). OK.
- v5 -> runCount@+0x32, v4 -> runs@+0x36. Per run: `result += *v4>>1` (skip>>1 words), copy v4[1]
  u16 pixels from v4+8, `v4 += 2*v4[1] + 8`. Row: `v5=v4; ++v4` (dword*, +4); `v11 += widthPx`.
- Return = `result` (last write cursor / row start). Recon returns `result`. OK.
Golden `DecodeRleGolden` green.

---

## src/render/shape_showbank_misc_recon.cpp

### 0x5d883c VIBE_Velocity_Apply (shp_ShowShapeFromBank) — VERIFIED-1:1
Diffed against disasm 0x5d883c..0x5d88ba.
- null bank -> return 0 (`retn 4`, __userpurge: caller args popped — modelled by the recon's plain
  return). OK.
- `movzx esi,shapeNr; movzx ebp,word[bank+0x2a]; cmp; jle ok` => error path when shapeNr > count.
  On error: sprintf(buf, "shp_ShowShapeFromBank:Shapenr is invalidate! Bank:%s", bank+0x0a) then
  return 0. Recon routes this through the injected onError(bank+0x0a) hook + returns 0. OK.
- success: v7 = bank + 4*shapeNr; shapeOff = *(u32*)(v7+0x45); shape = bank + shapeOff;
  saved = shape[0x0d]; shape[0x0d] = 2; FrameData_Process(a1, a2, bank+shapeOff, a4);
  shape[0x0d] = saved; return 1. Field offsets +0x2a / +0x45 / +0x0d all confirmed. OK.

---

## Counts
- Functions audited: 7
- VERIFIED-1:1: 6  (AddShape, AppendShape, BlitColored16, Convert8To16Indexed, DecodeRle,
  ShowFromBank)
- FIXED: 1  (RemoveShape — wrong cleared-slot index + wrong order)
- BOUNDARY / documented edge: 1 note  (Convert8To16Indexed return value in the never-occurring
  zero-width case)
- Golden tests changed: 0 (existing goldens already encode the correct behaviour and stay green)
- Test targets green: render_shape_blit_test, shape_showbank_misc_recon_test, render_camera_test
