# Hardening pass — render part 04: Picture I/O + BMP cluster

Scope: `src/render/picture_io.cpp` (9 addrs), `src/render/picture_recon_bmp.cpp` (11 addrs).
Method: decompile + disasm every `// gilde.exe 0xADDR` function, diff line-for-line
against the binary. Verified control flow/branches, constants & header layouts (via
field-offset arithmetic on the stack read buffers), packed RGB555 maths, signed/unsigned
compares, struct/row strides, and side-effect order. No float->int sites exist in this
cluster (all integer pixel ops). File I/O is reconstructed 1:1 against the binary's
VIBE_File_Open/Seek/Read stream calls via the in-memory Cursor model (NOT a tech swap).

## picture_io.cpp

| addr | function | status |
|------|----------|--------|
| 0x421B58 | VIBE_Picture_SwapRowBytes  | VERIFIED-1:1 (limit=2*count-2 off-by-one confirmed via disasm `sub ecx,2`; `jle` guards count<=1) |
| 0x421B88 | VIBE_Picture_LoadTga       | VERIFIED-1:1 (header fields v10/v11/v12 -> off 12/14/16/17; per-row reads = `width*2` sequential; descr==32 topdown vs flip; loop `i<height`) |
| 0x421CA4 | VIBE_Picture_SaveTga       | VERIFIED-1:1 (hardcoded TGA header type=2/bpp=16/descr=32; SwapRowBytes over fbWidth*h incl. unswapped last u16; row write loop) |
| 0x421DC4 | VIBE_Picture_LoadBmp24     | VERIFIED-1:1 (bpp==16 -> LoadTga; 3-byte BGR rows; Pack555 `(c>>3)&0x1F` exact; descr flip) |
| 0x421F2C | VIBE_Picture_LoadBmp32     | VERIFIED-1:1 (4-byte rows; faithful `i < height-1` off-by-one matches `>>16)-1`; Pack555) |
| 0x422094 | VIBE_Picture_BlitRegion    | VERIFIED-1:1 (guard w>=1&&h>=1; qmemcpy 2*w per row; src/dst row arithmetic) |
| 0x42210C | VIBE_Picture_FillRows      | VERIFIED-1:1 (guard rows>0; memset 0 over 2*w via SetGrayColorThunk(0,2*w,..)) |
| 0x422148 | VIBE_Picture_DrawBorder    | VERIFIED-1:1 (value 255; left/right edges clipped to cy via `if(v9>=v6)break`; top guarded `cy>y`; bottom guarded `y+h<cy`) |
| 0x422B58 | VIBE_Picture_SaveBmp24     | VERIFIED-1:1 (14-byte file hdr fileSize=3*w*h+58, dataOffset=58; 44-byte info block=40+4 gap; -height topdown; BGR<->RGB per-triple swap) |

## picture_recon_bmp.cpp

| addr | function | status |
|------|----------|--------|
| 0x4222bc | VIBE_Picture_ReadHeader      | VERIFIED-1:1 (seek 14, read 40; off 4/8/12/14/16; `planes==1 && bits==8 && comp<2` -> `width | (abs32(height)<<16)`, else -2; empty->-1) |
| 0x4228a0 | VIBE_Picture_ReadBmpType     | VERIFIED-1:1 (returns i16 at read-offset 14 = biBitCount; empty->-1) |
| 0x4228fc | VIBE_Picture_ReadBmpDimensions | VERIFIED-1:1 (`planes==1 && bits==24 && comp==0` -> `width | (height<<16)` NO abs, else -2) |
| 0x42234c | VIBE_Picture_LoadBmpPalette  | VERIFIED-1:1 (memset(pal,0,768); biClrUsed off 32, 0->256; BGRA quads -> pal[R,G,B] stride 4). NOTE: documented wave-11 clamp clrUsed<=256 — binary uses a fixed 1024-byte stack buffer and would overflow on malformed clrUsed; clamp is behavior-identical for all valid 8bpp files (clrUsed<=256). |
| 0x422418 | VIBE_Picture_LoadBmpRle      | **FIXED** delta(code 2) case — see below. Rest VERIFIED-1:1 (fields off 4/8/16/32; clrUsed 0->256; seek 4*clrUsed+54; encoded run memset; abs run reads val+1 when odd; EOL ++row; EOB row=height; flip via stride scratch `top<height/2`). |
| 0x422980 | VIBE_Picture_LoadBmpUncompressed | VERIFIED-1:1 (header bad-path returns 1; topDown=height>=0; abs32 height; pad=4-((3*(u8)width)&3); seek pad only if (3*width)&3; BGR<->RGB swap over width*height; off 4/8/12/14/16/32) |
| 0x4226ec | VIBE_Picture_LoadBmp24_226ec | VERIFIED-1:1 (seek 0, read 0x12,1; bpp!=24 -> return 1; rows read width*3; B/G/R from row[j]/[j+1]/[j+2] -> dst R,G,B; descr flip; v11=3*(x+fbWidth*dstRow)) |
| 0x422d98 | VIBE_Picture_SaveBmpPalette  | VERIFIED-1:1 (file hdr size=w*h+1078, dataOffset=1078; 36-byte info tail + 257 palette dwords = 0x428 write; v22[0]=clrImportant=0; loop fills v22[1..255]={B,G,R,0}; v22[256] emitted 0 [orig uninit, unobservable]; pixels w*h verbatim) |
| 0x422ae4 | VIBE_Picture_CreateSurfaceFromBmp | VERIFIED-1:1 (ReadBmpDimensions<0 -> null; w=(u16)dims, h=dims>>16 abs32 if neg; create 24bpp; LoadBmpUncompressed into surface data) |

## Divergence fixed

**0x422418 VIBE_Picture_LoadBmpRle — RLE8 delta escape (code 2)**
The binary (disasm 0x422515-0x42252a) re-reads 2 bytes into v23/v24, then:
- `0x422520 add esi,eax` with eax=var_14  -> `col += dx` (new v23)
- `0x422528 add ebp,eax` with eax=var_13  -> `row += dy` (NEW v24, the delta dy byte)

The reconstruction did `row += val` where `val` was the OLD escape byte (==2), not the
freshly-read dy byte. Fixed to `row += d[1]`. Existing golden tests do not exercise the
delta path, so no golden change was required; the fix is purely a binary-faithful
correction backed by the cited disasm.

## Tests

Built (test targets only; no full build, no git, no build/ recreation):
- picture_recon_bmp_test  — PASS
- render_bmp_test         — PASS
- render_picture_test     — PASS
- render_picture_e2e_test — PASS

Run with `GUILD_GAME_DIR` set: 4/4 suites pass, 0 failures.

## Summary counts
- Functions audited: 20 (9 picture_io + 11 picture_recon_bmp).
- VERIFIED-1:1: 19.
- Fixed divergences: 1 (RLE8 delta dy).
- Golden changes: 0.
