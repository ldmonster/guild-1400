# Hardening report — render_recon2.cpp + render_recon4_d3dcfg.cpp

Scope: line-for-line 1:1 verification of every `// gilde.exe 0xADDR` function in
the two assigned files against IDA Pro decompile + disasm (module gilde.exe,
imagebase 0x400000). Constants/tables checked via get_global_value/get_bytes;
float->int sites, signed/unsigned compares, branch polarity, struct offsets,
side-effect order, and return values all diffed.

Files:
- src/render/render_recon2.cpp
- src/render/render_recon4_d3dcfg.cpp (+ .h)
- tests/unit/render_recon4_ddraw_test.cpp (golden corrected)

Test targets: render_recon2_test (#493), render_recon4_ddraw_test (#494) — both PASS.

## render_recon2.cpp — 6 functions

| Addr | Name | Status |
|------|------|--------|
| 0x5af200 | ResetEngineState | VERIFIED-1:1 |
| 0x5af8e4 | SetEngineEnabled | VERIFIED-1:1 |
| 0x5b04a8 | ApplyFogAndLightFlags | VERIFIED-1:1 |
| 0x5b499c | PresentSceneAndClearFlags | VERIFIED-1:1 |
| 0x5d9014 | WithSurfaceContext2 | VERIFIED-1:1 |

Notes:
- 0x5af200: control flow (engineEnabled branch -> radix/draw vs radix/raster),
  the four field clears, `field13FC570 = drawListCount`, `drawListHead = 0`,
  and `return drawListCount` all match.
- 0x5af8e4: `prev` snapshot, `enableAllowed && enable` gate, sceneInit-guarded
  TraverseTree(...,64) with ShadowClearAllCasters, textureCacheReset, sky guard,
  and the post-change `prev != engineEnabled && sceneInit` block returning
  lightRefreshAllObjects(1) — exact. `byte_649D7C`=enableAllowed, `byte_649D71`
  =sceneInit, `byte_649D70`=engineEnabled confirmed.
- 0x5b04a8: register-arg mapping a1=dl mip, a2=cl fog, a3=bl shadowQuality,
  a4=al gamma, a5=upload cb confirmed. `(fogFlags&7) != (fogMode&7) || shadow !=
  byte_64A350` gate, `fogFlags = (fogMode&7)|(fogFlags&0xF8)`, TraverseTree(...,
  192) ResetCasterTransforms, gamma/mip, then shadow-quality TextureCache_Reset
  + UploadAllRecords. Signed `char` compare on shadowQuality preserved.
- 0x5b499c: object loop clears `*(table + v3 + 104) &= ~0x80` over objectCount
  records, stride 128; v5 low-byte present flag; devicePresentBegin via vtable
  +36; TraverseTree MarkAllFramesDirty(...,64); floor && drawMinimap minimap;
  devicePresentEnd via vtable +40. Pre-increment `++v2` before the AND matches.
- 0x5d9014: signed clip math — `y < clipMinY` => `width -= clipMinY - y; y = 0`,
  then `y + width > clipMaxY` => `width = clipMaxY - y - 1` (NO re-clamp; width
  may go negative and is passed verbatim). stride save/restore around
  RemapSurfacePalette. Pixel address `pixels + (stride*y + x)` on a u16* equals
  the binary's `*(DWORD*)(a5+28) + 2*(stride*y + x)` byte address. Offsets +16
  (stride), +28 (pixel base) confirmed.

## render_recon4_d3dcfg.cpp — 5 functions

| Addr | Name | Status |
|------|------|--------|
| 0x5d3938 | SaveD3dRegistryConfig | VERIFIED-1:1 |
| 0x5d3be8 | LoadD3dRegistryConfig | FIXED (missing viewDist compute) |
| 0x5e0890 | SaveD3DSettingsToRegistry | VERIFIED-1:1 |
| 0x5e0abc | LoadD3DSettingsFromRegistry | FIXED (mipfilter polarity) |
| 0x5e010c | BeginScene | VERIFIED-1:1 |

### Constants verified
- All registry key-name string literals (0x629178..0x62935c, 0x62b8e4..0x62b9fc)
  match the decompile xref strings exactly, including the save/load mipmaps key
  asymmetry: save writes "d3_max_mipmaps" (aD3MaxMipmaps 0x6291ec), load reads
  "d3d_max_mipmaps" (aD3dMaxMipmaps 0x629388). Preserved.
- d3_mode table DRAWDIB/DIRECTWINDOW/DIRECTWINDOWSOFT/FULLSCREEN/FULLSCREENSOFT
  (0x629258..0x629290), <NULL> sentinel unk_6292A0. Match.
- d3io LOD strings LOD_SWITCH/LOD_LOW/LOD_HIGH (0x6292ac..0x6292cc). Match.
- D3DFILL_POINT/WIREFRAME/SOLID, D3DTFP_NONE/POINT/LINEAR (0x62b958..0x62b9bc),
  <NULL> sentinel unk_62B98C. Match.
- d3_registry_version / d3d_registry_version literal value 3 on both save sides.
- viewDist scale constants: flt_629398 = 0x44000000 = 512.0f,
  flt_62939C = 0x3acccccd = 0.0015625f (get_global_value confirmed).

### Switch-arm / branch verification
- Save mode switch (0..4 -> string, default -> <NULL>) matches.
- Save LOD ladder `>=2 {<=2 LOW; ==4 HIGH; else NULL}; ==1 SWITCH; else NULL`
  and the identical fillmode/mipfilter ladders in SaveD3DSettings match.
- SaveD3DSettings bit extraction confirmed against shifts:
  antialias a2&1, bilinear (a2<<6)>>7=bit1, dither (32*a2)>>7=bit2,
  perspective (16*a2)>>7=bit3, subpixel (8*a2)>>7=bit4,
  multitexture (4*a2)>>7=bit5, mirrors (2*a2)>>7=bit6, shadows HIBYTE(a2)&7.
- The verbatim gilde.exe bug at 0x5e0a0a (mipfilter string written to the SAME
  "d3d_fillmode" key) is preserved 1:1 (and asserted by the golden test).

### Fix 1 — LoadD3dRegistryConfig viewDist (0x5d3be8 @ 0x5d3df0-0x5d3e16)
The original recomputes window viewdistance after the float queries:
`fild a1[0]; fmul 512.0; fmul 0.0015625; fstp *(float*)(a1+23)` i.e.
`winViewDist = (double)resX * 512.0 * 0.0015625` (= resX*0.8), computed in
double then truncated to float by `fstp dword`. The C++ omitted this entirely.
Added the exact computation (double math, cast to float, unsigned resX load to
match `*a1` being `unsigned int*`). No test depended on the float fields, so
this is a pure faithfulness addition (rule 8).

### Fix 2 — LoadD3DSettingsFromRegistry mipfilter polarity (0x5e0abc)
The reconstructed mipfilter parse had the POINT/LINEAR test inverted. Disasm
(0x5e0d72-0x5e0da5, StrCmpNoCase returns 0 when EQUAL):
  buf==NONE                  -> 1
  buf==POINT                 -> 2  (jnz 0x5e0d80 NOT taken)
  buf!=POINT && buf!=LINEAR  -> 2  (jnz 0x5e0d9c taken)
  buf!=POINT && buf==LINEAR  -> 3  (fall-through 0x5e0d9e)
So `=2` condition is `(buf==POINT) || (buf!=LINEAR)`; `=3` ONLY for LINEAR.
The old C++ encoded `(buf!=POINT) || (buf==LINEAR)` -> swapped POINT and LINEAR
outcomes. Fixed C++ to `strieq(POINT) || !strieq(LINEAR) -> 2; else 3`.
The golden test asserted mipfilter==2 for input "D3DTFP_LINEAR"; the binary
yields 3. Corrected the golden to 3 with addr+evidence citation. (fillmode parse
was already correct: POINT->1, WIREFRAME->2, else->3.)

### BeginScene (0x5e010c) verification
Register-arg signature a1=al alphaTest, a2=edx alphaRef, a3=bl zEnable.
Sequence: vtable+36 BeginScene; vtable+152 SetCurrentTexture(0,0); +88 RS 24=191;
+88 RS 25=5; if(alphaTest) RS 34=alphaRef; RS 28=(u8)alphaTest;
RS 7 = zEnable ? dword_1408080 : 0 (fogTable global); RS 14 = zEnable &&
!byte_64A352 (noZBuffer global); byte_64A357=zEnable, byte_64A356=alphaTest.
The C++ surfaces fogTable/noZBuffer as params (== the two globals) and routes the
device + SetRenderState through the rule-3 hook boundary; SetCurrentTexture takes
inert (0,0,0) at the GPU boundary vs (0,0) in the binary. Logic/values/order
exact. VERIFIED-1:1.

## Result
- 11 functions reviewed: 9 VERIFIED-1:1, 2 FIXED to binary.
- 1 golden corrected (mipfilter LINEAR -> 3) with cited disasm evidence.
- render_recon2_test PASS, render_recon4_ddraw_test PASS.
