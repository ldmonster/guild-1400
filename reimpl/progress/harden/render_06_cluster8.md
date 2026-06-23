# Hardening sweep — render cluster 8 (surface.cpp / surface_blit.cpp)

MCP live (gilde.exe). Every function below decompiled + disassembled and diffed
line-for-line against the binary. Test targets: `render_surface_test`,
`render_paintbox_test` (both green). Related e2e (`render_surface_e2e_test`,
`render_shape_blit_e2e_test`) also rebuilt + green.

Files:
- src/render/surface.cpp
- src/render/surface_blit.cpp
- tests/unit/render_surface_test.cpp        (golden corrected)
- tests/e2e/render_surface_e2e_test.cpp     (golden corrected)

## src/render/surface.cpp

### 0x42311c VIBE_Surface_Create (sysmem branch) — VERIFIED-1:1
Struct offsets confirmed from disasm: +0x04 width, +0x08 height, +0x0C pitch,
+0x10 widthPx, +0x14 bpp, +0x1C pixels, +0x20 ddSurface, +0x24/28/2C/30 clip
rect, +0x34 decompState, +0x38 shared. sysmem path: pitch = (bpp>>3)*width,
pixels = alloc(pitch*height), widthPx = pitch/(bpp>>3) (signed div), clip =
[0,w)x[0,h). Reconstruction matches (original pulls w/h from the memcpy'd 64B
template + clipX1/Y1 from template[1]/[2]; the reimpl takes them as args — a
faithful sysmem reconstruction). DDraw allocation path is the boundary.

### 0x4234b0 VIBE_Surface_Destroy — VERIFIED-1:1 (boundaries documented)
`if (result)`: finalize decompState(+0x34) [boundary]; if !shared(+0x38) free
pixels(+0x1C)+null, release ddSurface(+0x20) [boundary]; free record; ret 1.
Pixel free correctly gated on !shared. Null -> ret 0 (reimpl adds the guard;
original would deref). Decompression + DDraw release are genuine boundaries.

### 0x423c14 VIBE_Surface_Clone — VERIFIED-1:1 (reconstruction)
Copies 64B template, zeroes pixels(+0x1C)/ddSurface(+0x20), Create(template,
caps), then VIBE_Result_Finalize blits src->dst. Reimpl Create+memcpy(pitch*
height) is the faithful software equivalent of the blit.

### 0x423620 VIBE_Surface_GetCaps — FIXED
Evidence (disasm 0x423621): `cmp [eax+0x20],0; jnz` — when ddSurface==0 the
function does `xor eax,eax; ret` (FALSE) and never writes *outCaps. Caps only
come from the DDraw COM vtable (call [ecx+0x58]). The software model has
ddSurface==null always, so faithful result is always FALSE.
- BEFORE: `*outCaps = s->caps; return true;`
- AFTER:  returns false when !s->ddSurface (the only reachable software case);
  *outCaps untouched. DDraw vtable path marked boundary (rules 3-5).
No caller consumes the caps value (grep) and the null-guard test still passes.

### 0x423e5c VIBE_Surface_SetPixelRgb — FIXED (24bpp address)
Clip check +0x24/+0x2C (x), +0x28/+0x30 (y); pack = Result_Handler_Final(r,g,b);
bpp dispatch (<8 nop, ==8 luma (b+r+g)/3, ==15/<=16 u16 packed, ==24 BGR,
==32 u32). 8/15/16/32 addressing all VERIFIED.
24bpp DIVERGENCE — disasm 0x423f32-0x423f70:
  `imul edx,esi` (edx=[+0x10]=widthPx, esi=y) -> widthPx*y (row NOT scaled by
  bytespp); `imul edi,bytespp` (edi=x) -> bytespp*x; addr = pixels+widthPx*y+3x.
- BEFORE: `s->pixels + row*bytespp + col`   (row scaled — WRONG)
- AFTER:  `s->pixels + row + col`           (row = widthPx*y, unscaled — 1:1)
This makes the 24bpp Set/Get pair asymmetric vs GetPixelRgb (below): a genuine
binary quirk, now reproduced verbatim.

### 0x423d74 VIBE_Surface_GetPixelRgb — VERIFIED-1:1 (+ honest 8bpp comment)
24bpp read addr (disasm 0x423e11): `imul eax,edx; imul eax,ecx` ->
widthPx*y*bytespp + bytespp*x — row IS scaled, the OPPOSITE of SetPixelRgb.
Reimpl already did `row*bytespp + col` here => matches the binary. 16/32 paths
symmetric with Set; VERIFIED.
8bpp / bpp(<15,17..23) NON-1:1-BY-NATURE: disasm 0x423d91 copies var_10/var_18/
var_14 which are UNINITIALISED stack (prologue only `sub esp,0Ch`). Original
returns indeterminate garbage; impossible to reproduce deterministically. Reimpl
keeps a defined stand-in (raw stored byte x3) and the comment now states this.

### 0x423ffc VIBE_Surface_DrawHLine — VERIFIED-1:1
`if (len>0){ x=x0; end=x0+len; do{ Set(x++,y) }while(x<end); }` matches disasm.

### 0x424044 VIBE_Surface_DrawLine (Bresenham) — VERIFIED-1:1
Full line-for-line diff:
- arg map (call sites): a1=x0@eax, a4=x1@ebx, a2=y0@edx, a3=y1@ecx, a5=r, a6=g,
  a7=b, a8=surface. SetPixel(x,y,g,r,b,surf).
- horizontal (y0==y1): x1>=x0 walk up from x0 (i=x1-x0); else walk up from x1
  (j=x0-x1). Match.
- vertical (x0==x1): y0<=y1 draws old-y in the for-increment (y0..y1-1); else
  walk from y1 (m=y0-y1). Match (incl. the increment-side draw ordering).
- general: dx/sx (x0>=x1 => dx=x0-x1,sx=-1 else dx=x1-x0,sx=1), dy/sy likewise.
  start pixel drawn once. dx<=dy: err=2dx-dy, loop while y!=y1 (y+=sy; err<0 ->
  +=2dx else x+=sx,err+=2(dx-dy); draw). else: err=2dy-dx, loop while x!=x1
  (x+=sx; err<0 -> +=2dy else y+=sy,err+=2(dy-dx); draw). All signs, step
  decisions, error terms, loop conditions, and the for-increment draw placement
  match the disasm exactly. Single-point result=0 case matches (result seeded
  a4-a1=0). VERIFIED.

### 0x4242d4 VIBE_Surface_DrawRectOutline — VERIFIED-1:1
Gated on a7(b)!=0. h>=1 -> vertical edges loop y in [y,y+h) drawing x and
(x+w-1). w>0 -> horizontal edges loop x in [x,x+w) drawing y and (h+y-1).
Matches disasm (incl. b-channel gate quirk). VERIFIED.

### 0x423b6c VIBE_Surface_ColorFill — VERIFIED-1:1 for the zero path; documented divergence
Software branch (ddSurface==0): SetGrayColorThunk(0, pitch*height, pixels) =
memset-0 of pitch*height bytes (confirmed SetGrayColorThunk @0x5c6af0 fills a
byte-replicated dword; arg=0 -> zero). The ORIGINAL always zeroes regardless of
colour. The reimpl `SurfaceColorFill(r,g,b)` is used across the WHOLE tree
(~30 call sites/tests outside this chunk, e.g. clear-to-(0,0,64)/magenta) as a
colored clear. Forcing the 1:1 zero-only behavior would break many out-of-scope
suites. The r=g=b=0 path is byte-faithful (memset 0, size pitch*height). The
colored path is a documented codebase-wide extension, NOT 1:1; left intact to
avoid breaking the tree (would need an owner decision to rename/split).

## src/render/surface_blit.cpp — VERIFIED-1:1 (structural reconstruction)

These are thin dispatchers over other-module/boundary calls (VIBE_State_Update
@0x40e9e8, Lock=VIBE_DecompressState_Blob @0x423500, Unlock=Decompression_
Finalize @0x4235dc, VIBE_Animation_Basic @0x5d85b8 = the RLE blitter, and
VIBE_Coord_Push @0x5d8ae8). Lock/Unlock are the DDraw boundary -> ISurfaceLock
mock; the pixel work is AnimationBasic (another chunk).

- 0x41EF40 DrawShape: window-paintbox lookup; if bank present -> Update -> Lock
  -> AnimationBasic -> Unlock; else logs + no draw. Reimpl matches (bank==null
  stands for the "no paintbox" log/early-out).
- 0x41EFC4 DrawShapeDirect: Update -> AnimationBasic(bank=[surf+0x28]). Reimpl
  matches.
- 0x41F00C DrawShapeClipped: Update; VIBE_Coord_Push(clipX0=[surf+0x24],
  clipY0=[+0x28], clipX1=[+0x2C], clipY1=[+0x30]) — disasm 0x41f022-0x41f02e
  confirms the clip rect is read from the SURFACE struct's own fields, not
  external args. The reconstruction passes those four values in via the
  dispatcher (FrameBlitState) instead of a Surface struct — a faithful
  structural model (the mock's clip stands in for [surf+0x24..0x30]). The e2e
  test PaintboxClippedRoutesToXClip pins this (installs clip + routes to the
  X-clipped FrameTableNext). VERIFIED.

## Golden corrections (both cite addr+evidence)

1. tests/unit/render_surface_test.cpp RenderSurface.SetPixel24Bgr — was reading
   back at `widthPx*y*3 + 3x` and expecting the written RGB. Now asserts the
   FAITHFUL split: Set writes byte (widthPx*y)+3x (=14 for (2,1)); GetPixelRgb
   reads byte (widthPx*y*3)+3x (=30) -> returns 0,0,0 (no round-trip). Cites
   0x423f32 (set) and 0x423e11 (get).
2. tests/e2e/render_surface_e2e_test.cpp DrawSaveReloadRoundtrip24 — capture
   loop now reads raw bytes along the Set layout (pixels+widthPx*y+3x) instead
   of via SurfaceGetPixelRgb (whose asymmetric read would not recover the drawn
   pixels for y>0). BMP roundtrip + drawn-feature spot checks pass. Comment
   cites both addresses.

## Counts
Functions verified: 11 VERIFIED-1:1 (Create, Destroy, Clone, GetPixelRgb,
DrawHLine, DrawLine, DrawRectOutline, ColorFill[zero path], DrawShape,
DrawShapeDirect, DrawShapeClipped) + 2 FIXED (GetCaps, SetPixelRgb 24bpp).
Goldens corrected: 2. Boundaries documented: DDraw alloc/release, decompression
lock/unlock, Coord_Push/Animation_Basic, GetCaps COM vtable. Non-deterministic-
by-nature: GetPixelRgb 8bpp (uninitialised stack). Documented codebase-wide
extension left intact: ColorFill colored path.
Tests: render_surface_test PASS, render_paintbox_test PASS,
render_surface_e2e_test PASS, render_shape_blit_e2e_test PASS.
