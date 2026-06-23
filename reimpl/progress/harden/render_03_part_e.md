# Wave-H1 1:1 Hardening — render_03 part E

Scope (owned files):
- src/render/paintbox.cpp
- src/render/paintbox_shape.cpp
- src/render/perf_overlay.cpp
- tests/unit/render_paintbox_test.cpp (golden for the above)

MCP module `gilde.exe`, imagebase 0x400000. Every provenance-bearing function
was decompiled + disassembled and diffed line-for-line.

---

## paintbox.cpp

### PaintboxDrawScaledRegion — gilde.exe 0x41e920 — VERIFIED-1:1
Clip condition (inclusive on all four sides) matches the binary:
`border <= x && (w-border) >= x && y >= border && y <= (h-border)` (decompile
0x41e997..). Original uses `byte_62D28C` (the brush) as the border inset; the
reimpl models this via `pb.border` (documented abstraction, header note).
Brush dispatch: `if(brush==0)` single px; `else if(brush<=1)` 5-px plus; `else
if(brush==2)` 13-px diamond — matches `if(byte_62D28C)` / `<=1u` / `==2`.
Plot ORDER and coordinates verified against disasm 0x41ea24..0x41eb7b:
- plus (5): (x-1,y),(x+1,y),(x,y+1),(x,y-1),(x,y) — exact.
- diamond (13): (x-1,y),(x+1,y),(x-2,y),(x+2,y),(x,y),(x,y+1),(x,y-1),(x,y+2),
  (x,y-2),(x+1,y+1),(x-1,y-1),(x-1,y+1),(x+1,y-1) — exact.
BOUNDARY (documented, not a divergence): the original wraps the plotting in
`VIBE_DecompressState_Blob`(lock)/`VIBE_Decompression_Finalize`(unlock) on the
surface and fetches the window record from `dword_67EB80[238*dword_62D230]`; the
reimpl gathers target surface + brush + border into PaintboxState (self-contained,
testable). The `d2_Plot:Window is not a paintbox!` error-log branch is an
abstracted diagnostic. The colour register threading (a3=bl=green, recovered
cl/v7) is replaced by explicit (r,g,b). Pixel output is identical.

### PaintboxDrawLine — gilde.exe 0x41ec40 — VERIFIED-1:1
Recovered the true arg order from disasm: (a1=x0@eax, a2=y0@edx, a3=y1@ecx,
a4=x1@ebx) — Hex-Rays' a3/a4 are y1/x1 (verified via the vertical branch using a4
as the fixed X). reimpl naming (x0,y0,x1,y1) maps correctly.
- Horizontal (y0==y1): both sub-branches (x1>=x0 / x1<x0) match iteration counts
  and start values (0x41ec5c..0x41ecb9).
- Vertical (x0==x1): both sub-branches (y0<=y1 / y0>y1) verified (0x41ecc3..ed38).
- Diagonal: dx/sx, dy/sy selection; seed plot (x0,y0); the two Bresenham arms
  (dx<=dy stepping y, else stepping x) with err updates `2*dx-dy`/`2*dy-dx` and
  the `+=2*dx` / `+=2*(dx-dy)` increments — all exact (0x41ed3b..0x41ee93).

### PaintboxClear — gilde.exe 0x41ee9c — VERIFIED-1:1
Software path of VIBE_Surface_ColorFill (0x423b6c): for a system-memory surface
it zero-fills via VIBE_Light_SetGrayColorThunk(0, w*h, pixels) =
memset(pixels,0). reimpl `SurfaceColorFill(surface,0,0,0)` produces the same
all-zero buffer for any colour format. The DirectDraw Blt fill path (offset+32
GPU surface) and lock/finalize are the GPU/lock BOUNDARY (rule 3).

---

## paintbox_shape.cpp

### Surface_BlitPaletteToPixels — gilde.exe 0x422F80 — VERIFIED-1:1
LUT build verified at disasm 0x422f9b: esi=2 first iter, store at word-offset esi
=> LUT[1..256]; palette read [ecx],[ecx+1],[ecx+2] (R,G,B), `add ecx,4` (stride
4). Blit reads `v16[idx+1]`, so the +1 store/read cancels — reimpl flattens to
LUT[256] with `lut[i]=Pack(p[0..2]); p+=4` and `lut[srcRow[x]]` (behaviorally
identical). Pack order Final(R,G,B) == reimpl PackColor(fmt,r,g,b) — confirmed
against VIBE_Result_Handler_Final @0x434f30 channel-shift mapping. src row stride
= width; dest row stride = global `dword_7626F8` (documented → explicit fbWidth
param). Pixel addr y*stride+x.

### Surface_BlitRgbToPixels — gilde.exe 0x422EE4 — VERIFIED-1:1
src triple read R,G,B (`*v7,v7[1],v7[2]`), src row stride 3*width (`v11+=3*a2`),
dest stride = `*(a6+16)` (widthPx), dest buf = `*(a6+28)` (pixels), addr
`stride*y+x`, 16-bit write. Pack Final(R,G,B)==PackColor(surf->fmt,r,g,b). All
match. (per-surface fmt instead of the global shift table = documented abstraction.)

### Surface_CopyRegionRgb — gilde.exe 0x423050 — FIXED
DIVERGENCE: reimpl stored the dest RGB triple in the order R, B, G ("documented
quirk"). That was a MISREAD of VIBE_Render_UnpackColor (@0x434f7c) + the call's
register binding. Evidence:
  - UnpackColor writes R -> *a2(edx), G -> *a4(ebx), B -> *a3(ecx)
    (a1>>76271E<<76271C = R; a1>>76271A<<76271B = G to *a4; a1>>76271D<<762719 = B to *a3).
  - Call register setup (disasm 0x4230aa..da): edx = dst+0, ecx = dst+2 (edi),
    ebx = dst+1 (ebp).
  => dst+0 = R (edx), dst+1 = G (ebx), dst+2 = B (ecx) — i.e. NORMAL R,G,B order.
FIX (source): dstRow[3x+0]=r; dstRow[3x+1]=g; dstRow[3x+2]=b; (was r/b/g).
FIX (golden): tests/unit/render_paintbox_test.cpp CopyRegionRgbExactBuffer now
checks dst[1]==eg, dst[2]==eb (was eb/eg), with addr citation in the comment.
Other axes (src stride dword_7626F8->fbWidth param, dst stride 3*width, addr
2*(x+fbWidth*y) src / 3*(x+width*y) dst) already matched. Build + test PASS.

---

## perf_overlay.cpp — N/A (no provenance; new in-house feature)
No `gilde.exe 0xADDR` / `@0xADDR` provenance on any function. This is an
in-house in-game performance HUD (FPS/ms text + frame-time bar graph), not a
reconstruction of any gilde.exe function — there is no original to diff against.
The float->int sites are internally well-defined C++ and have no 1:1 reference:
  - FPS text: `(int)(dispFps_ + 0.5f)` (round-half-up) — display only.
  - ms text: snprintf `%.1f`.
  - bar height: `(int)(ms/50.0f*kGraphH)` (C truncation) — display only.
Nothing to harden to the binary; left as-is.

---

## Counts
- VERIFIED-1:1: 5  (DrawScaledRegion, DrawLine, Clear, BlitPaletteToPixels, BlitRgbToPixels)
- FIXED: 1         (Surface_CopyRegionRgb — dest channel order R,B,G -> R,G,B; src + golden)
- BOUNDARY: documented within the above (DDraw Blt fill + surface lock/finalize in
  Clear/DrawScaledRegion = rule-3 GPU/lock boundary; global window-record/brush/fb-width
  fetches modeled as explicit params/state — documented abstractions, not divergences)
- N/A: perf_overlay.cpp (no provenance, new feature)

## Handoffs
None. All fixes confined to owned files + their golden test. No shared-symbol or
out-of-chunk edits required.
