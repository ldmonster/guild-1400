# Render Harden 08 — tile_geometry / tile_visibility / thumbnail_capture

1:1 line-for-line diff of each provenanced function against IDA decompile + disasm
(module gilde.exe, imagebase 0x400000). DISASM is the reference of record.

## Constant verification (get_bytes)
| addr | bytes | value | used as |
|------|-------|-------|---------|
| 0x62529C | CD CC 4C 3B | 0.003125f | thumbnail sample-step factor (1/320) |
| 0x628B4C | 00 00 00 40 | 2.0f | terrain type-byte light mul (also 0x628B30=2.0) |
| 0x628B40 | 00 00 00 3E | 0.125f | tile 8-corner average weight |
| 0x628B44 | 00 00 80 41 | 16.0f | bbox-min padding (bound pass, out of CenterRadius scope) |
| 0x628758 | 00 00 20 42 | 40.0f | crowd numerator |
| 0x62875C | CD CC CC 3E | 0.4f | crowd factor |

All constants CONFIRMED.

## thumbnail_capture.cpp

### 0x56D48C VIBE_Render_CaptureScreenThumbnail — VERIFIED-1:1
- step = fild SHIWORD(dword_69FFBC) * flt_62529C — same factor both axes. MATCH.
- ConvertX @0x5C6B08: `fstcw; HIBYTE(cw)=0x1F; fldcw; frndint; fldcw` => RC=11
  truncate-toward-zero. Confirmed. Both srcRow=(int)ConvertX(rowCoord) and
  srcCol=(int)ConvertX(colCoord) truncate. MATCH.
- indexing: scratch[col+320*row] = src16[srcCol + stride*srcRow], stride=*(base+16),
  pixels=*(base+28), word read at `2*(srcCol+rowBase)+base`. MATCH.
- loop bounds: inner `cmp edx,140h; jl` => 320 cols; outer `cmp eax,0F0h; jl` => 240 rows.
  MATCH.
- Descriptor field offsets re-derived from disasm stack slots (v27 = scratch/src,
  v26 = thumb/dst). dispatch call order = StretchSurfaceDispatch(v26=thumb, v27=scratch):
  - scratch v27: [0]=124 [2]=240 [3]=320 [4]=640 [9]=scratch [21]=16 [22/23/24]=masks. MATCH.
  - thumb v26: [0]=124 [2]=120 [3]=**160** (edx set by `mov edx,0A0h` @0x56D5C9 — NOT the
    dangling inner-loop counter; initial Hex-Rays `v13` reads as 160) [4]=320 [9]=word_13CED78
    [21]=16. MATCH (source thumbDesc.width=160 is CORRECT).
  - dst.width(160) < src.width(320) => StretchAverage16 box-average down-sample. CONSISTENT.
- BOUNDARY: DecompressState_Blob/Finalize bracket (surface lock) modeled as a
  `src.pixels != null` gate; StretchSurfaceDispatch box-average is the downscale
  dispatch leaf (reconstructed separately in surface_stretch.cpp). Math + offsets verified.

### 0x4FF6D4 VIBE_Render_CaptureScreenshot — VERIFIED-1:1
- serial: `v9 = dword_634480++` post-increment. MATCH.
- sprintf "gamedata/screenshots/gilde%04i.bmp". MATCH.
- w = dword_69FFBC>>16; h = *(int*)((char*)&dword_69FFB8+2)>>16. MATCH (passed in).
- alloc 3*w*h. MATCH.
- CopyRegionRgb(rows=h, cols=w, dword_7626F0, 0,0, rgb). Arg order MATCH.
- BmpSave24Bit(name, w, rgb, h) — w,h order preserved. MATCH.
- BOUNDARY: SetStatusBanner + RunFrameLoop (GUI/frame leaves) and
  BeginFrameLock/UnlockBackBuffer (rule-3 GPU present lock) are owned elsewhere;
  dimensions/serial/format/call-order verified.

## tile_geometry.cpp

### 0x5BF22C / 0x5BE668 BuildTileVertex per-vertex build — FIXED (clamp rounding)
- world = heightAxis*h + acc; l=(type&0x7F)*2.0; high-bit branch (ambient vs
  +shadowBias); byte layout +66=R/+65=G/+64=B; packed B|G<<8|R<<16. MATCH.
- **FIX #1 — ClampLightByte float->int rounding.** Disasm 0x5BECCE/0x5BED00/0x5BED33
  (and ambient branch 0x5BED77/0x5BEDA3/0x5BEDD0): the store is a BARE `fistp dword`
  with NO preceding ConvertX / no fldcw chop => default x87 RC = round-to-nearest-even,
  NOT truncate. Followed by signed `cmp eax,0FFh; jle` (high cap >255 -> 0xFF) and a
  LOW-BYTE store (`mov [edx+4x],al`, negatives wrap, no low clamp).
  - BEFORE: `int t = (int)x;` (truncate toward zero) — WRONG.
  - AFTER:  `int t = (int)std::lrint(x);` (round-nearest-even, matches the codebase
    FistpRound convention in object_light_shade.cpp).
  - Evidence: `fistp dword ptr [eax]` @0x5BECCE etc. with the surrounding control word
    untouched; identical bare-fistp convention already used at 0x5C83E0/0x5C8506/0x5C8017.
  - Golden FIX (render_terrain_render_test.cpp / ClampLightByte): old goldens
    (100.5->100, 255.9->255, 300->255, 0->0) did not distinguish trunc from round.
    Added discriminating cases: 101.5->102 (tie-to-even, DIFFERS from trunc 101),
    99.9->100 (DIFFERS from trunc 99), 99.4->99, plus 255.9->256->0xFF cap.
- The +67 vertex-flag store and +44 dword mirror are outside BuildTileVertex's scope.

### 0x5BE668 AppendTilePolysToDrawList — VERIFIED-1:1
- remain clamp: `v44 = (dword_13ECE80 - dword_13FC770 <= v82) ? remain : v82` =
  min(capacity-count, polyCount). MATCH.
- cull sign: `if (*(char*)(result+36) < 0)` => front-facing = high bit set; source
  skips when `(i8)flags36 >= 0`. MATCH. Loop iterates first `limit` polys appending
  only front-facing (equivalent to source's i<limit + continue). MATCH.
- sortKey ((tex - dword_1406A84) >> 7) + 1 else 0 (passed in via texSortId). MATCH.
- count++ (dword_13FC770). MATCH.

## tile_visibility.cpp

### 0x5BEF08 ComputeTileCenterRadius (bound pass) — VERIFIED-1:1
- seed corners[0..2]; loop `v10 = v6+20; do {sum += *v10..; v10+=20} while (v10 != v6+160)`
  => 8 corners, stride 20 floats (80-byte Vertex). MATCH.
- centre = sum * flt_628B40(0.125); radius = sqrt(cx²+cy²+cz²) -> *(v3+76). MATCH.

### 0x5BA438 VIBE_Floor_ComputeLodLevel — VERIFIED-1:1
- forced override (flags & 0x1C) -> (u8)(8*flags)>>5 unsigned. MATCH.
- !*(a1+36) -> 0. MATCH.
- crowd = (flt_628758 - count)*flt_62875C = (40-count)*0.4. MATCH.
- metric = *(a2+76)/*(a1+160) = radius/tileScale. MATCH.
- thrFar+bias+crowd >= metric ? (thrNear+bias+crowd >= metric ? 1 : 2) : 4. Compare
  directions (>=) and double promotion. MATCH.
- 8-frame debounce: prevLod 0/0xFF/equal -> counter=0,return; else ++counter,
  pending=result; counter>8 -> result=pending,counter=0; else return prevLod. MATCH.

### 0x5BEF08 StitchTileLod (stitch pass) — VERIFIED-1:1
- `if (v15)` guard (myLod != 0). MATCH.
- left seam gated on col index v13 (==0 disabled); (my==4&&left==1)||(my==1&&left==4)->2.
  Source gates on leftLod!=0 — functionally equivalent (left==1/==4 both false when 0). MATCH.
- up seam gated on row index v32 likewise. MATCH.
- minLod = 1 << (*(a1+7281) & 0xF); v16 = max(minLod, myLod). MATCH.

## Counts
- VERIFIED-1:1: 6  (CaptureScreenThumbnail, CaptureScreenshot, AppendTilePolysToDrawList,
  ComputeTileCenterRadius, ComputeLodLevel, StitchTileLod)
- FIXED: 1  (ClampLightByte / BuildTileVertex clamp: truncate -> round-nearest-even, 0x5BECCE)
- BOUNDARY: 3  (StretchSurfaceDispatch box-average + DecompressState surface lock;
  CaptureScreenshot banner/frame-pump + BeginFrameLock/Unlock GPU present)

## Tests
`ctest -R "thumbnail|tile|terrain_render|lod"` (GUILD_GAME_DIR set): 20/20 PASS,
including the strengthened round-nearest-even ClampLightByte golden.
