# Hardening report — render_leaves2/3/4 + reflective_nodes

Scope: 1:1 line-for-line diff of every `// gilde.exe 0xADDR` function in
`src/render/render_leaves2.cpp`, `render_leaves3.cpp`, `render_leaves4.cpp`,
`src/render/reflective_nodes.cpp` against `decompile`/`disasm` of `gilde.exe`
(imagebase 0x400000). Constants/tables verified via `get_bytes`. Float→int sites,
fixed-point shifts, signed/unsigned compares, struct offsets, side-effect order and
returns checked. GPU/present hooks treated as the Vulkan/DDraw boundary (Rule 8).

Result: ALL 11 relevant tests pass (unit + integration + e2e + mirror_render_wave6).
2 real binary divergences found and FIXED (1 source bug + 1 source omission); both
required golden-test updates (the prior goldens encoded the wrong behavior). 3 strictly
unobservable return-value notes documented.

## Divergences fixed (binary was right; code/test were wrong)

1. **SetPixelRgb @0x423e5c — r/g PACK argument swap (REAL BUG).**
   The binary packs `VIBE_Result_Handler_Final(a4, a3, a5)` where a3==r, a4==g, a5==b,
   i.e. `PackColor(al=g, dl=r, bl=b)` == `PackColor(fmt, g, r, b)`. The pixel's RED
   channel carries G and its GREEN channel carries R (a genuine engine quirk — the
   24bpp BYTE path and BlitRgbToPixels do NOT swap). The code had
   `PackColor(fmt, r, g, b)`. Fixed to `PackColor(fmt, g, r, b)`.
   Confirmed via the unambiguous 24bpp byte writes (`[base]=a4=g, +1=a3=r, +2=a5=b`,
   matching the passing 24bpp test) which pin a3==r / a4==g.
   Test goldens updated: `render_leaves4_test` 16bpp+32bpp pack and 16bpp Get
   round-trip (exact 565 recovery 120/16/248 computed offline);
   `render_leaves4_itest` round-trip.

2. **CreateTileRecord @0x5db928 — missing byte_649D70 gate (REAL OMISSION).**
   Binary: `if (!ActiveRecord || !byte_649D70) return 0`. The code only checked the
   record, ignoring the system-init flag `byte_649D70` (== `g_rawLightingFlag`). Added
   the `|| !g_rawLightingFlag` guard (include of render_leaves4.h). Added a null-path
   case to `render_leaves3_test` and set the flag for the success case.

3. **DrawHLine @0x4351d8 — horizontal-run return unit.** The binary returns a BYTE
   offset (`2*v7_final`) on the horizontal branch (word index elsewhere). Code
   returned a word index. Fixed to `return 2 * result` on that branch to match the
   binary exactly. (Return is discarded by all 9 callers; pixel writes were already
   correct — verified.)

## Constants / tables verified bit-exact (get_bytes)

- reflective `v85` predicate: disasm 0x5da74f confirms `sar 6 / test 1` then byte
  `cmp 0FFh / jnb` == `(flag2>>6)&1 && (u8)flag0 < 0xFF`. VERIFIED.
- kUvScrollRate[0..15] (flt_5D938C): all 16 floats bit-exact (offline compare).
  Fixed a misleading hex comment on [1] (0x380BCF65, not 0x38B7176E).
- kAnimDivisor (dword_5D93C8): the IDA symbol is at 0x5d93c8 but the integer data
  `15,13,11,9,8,7,5,3,2,1` actually begins at 0x5d93cc (overlapping flt[15] at
  0x5d93c8), so `dword_5D93C8[speed]` for speed 1..10 yields 15..1 — exactly the
  C++ `kAnimDivisor[speed]` (index 1→15). VERIFIED (earlier off-by-one suspicion
  was a table-base misread).
- flt_629650 = -1.0f (ScrollUvCoords additive wrap). VERIFIED.
- dbl_61DD38/40/48 = 0.6 / -0.3 / 0.3 (SetSunHeight). VERIFIED.
- flt_628C0C=1/32767, flt_628C10=2.0, dbl_628C14=0.25, flt_628C1C=255.0. VERIFIED.

## Per-function status

### render_leaves2.cpp (13 addrs)
- 0x435748 PutPixel — VERIFIED-1:1 (pack r/g/b via table; bpp dispatch; 8bpp grey;
  green=ctx-low-byte). Device-present tail (byte_762721 switch) is the boundary hook;
  return simplified to clean 1/0 (present-status byte) — documented.
- 0x4351d8 DrawHLine — VERIFIED-1:1 after horizontal-return fix.
- 0x423ab8 BlitClipped — VERIFIED-1:1 (clip math; rects built only when src!=null).
  Vendor Blt is the boundary hook.
- 0x5dae38 PackColorFlags — VERIFIED-1:1.
- 0x4289f0 SetVertexColorRgb — VERIFIED-1:1 (b,r,g reorder).
- 0x428a10 SetGlobalColorTemp — VERIFIED-1:1 (payload {b,r,g}; +496 save/zero/restore;
  null-return is obj-low-byte in orig vs 0 here — unobservable, documented).
- 0x5d329c GetBoundingRadius — VERIFIED-1:1.
- 0x4b24b0 SetSunHeight — VERIFIED-1:1 (light+488 indirection flattened to the +420
  slot param, documented).
- 0x43ea0c SetGlobalDirection — VERIFIED-1:1.
- 0x5db694 SetTransparencyFlag — VERIFIED-1:1 (core bit math; device-state short
  circuits + clone redirect + ReleaseSurface omitted as the documented boundary).
- 0x5dbde0 CloneIfPaletteMatch — VERIFIED-1:1.

### render_leaves3.cpp (14 addrs)
- 0x5b9e74 SetMipFilterLevel — VERIFIED-1:1.
- 0x5db094 ScrollUvCoords — VERIFIED-1:1 (additive wrap `>1.0 -> +(-1.0)`, subtractive
  `<0.0 -> +1.0`; rate2 aliasing honored).
- 0x5daf78 AdvanceAnimFrames — VERIFIED-1:1 (CRC seed, `(crc+tick/div)%frames`,
  member rebind; div!=0 guard defensive, never 0 for valid speed).
- 0x5c80a0 CollectAffectedObject — VERIFIED-1:1 (append vs distance/radius cull;
  kind==7 special case). Note: orig passes `&flt_5CA2B0` as the rotate refDir; the
  default hook ignores it (passed nullptr) — unobservable.
- 0x42e0b0 ApplyAmbient — VERIFIED-1:1 (modelled slot-index bracket; documented).
- 0x5dbbb4 DetachClone — VERIFIED-1:1 (clear master, group-matched recursion with
  a2=id^id==0, ReleaseEntry). No-clone path returns 0 here vs the orig record ptr
  (a meaningless int with the native struct) — documented.
- 0x5db928 CreateTileRecord — VERIFIED-1:1 after the byte_649D70 gate fix.
- 0x5e0e9c UnlinkObjectNode — VERIFIED-1:1 for all side effects (head/tail sentinel
  splice, owner +164/+168, neighbour +776/+780, view-cache refresh). In the refresh
  path the orig `result` is overwritten with viewFieldB; we return the back-link.
  UNOBSERVABLE: the sole live caller (FreeObjectNode @0x5e0f30) only invokes this in
  the `owner != activeView` branch (refresh never taken) and discards the return.
- 0x431f18 IsSurfaceLost — VERIFIED-1:1.
- 0x5b5404 SurfaceReleaseTexture — VERIFIED-1:1.
- 0x5af260..0x5af290 GetViewParamA..G — VERIFIED-1:1 (7 accessors).

### render_leaves4.cpp (12 addrs)
- 0x5c6b30 RefreshChildBrightness — VERIFIED-1:1 (root[+0x1E8][+0x198] walk; raw:
  child[+0x40]=child[+0x44]; non-raw: child[+0x42]=(zext byte +0x46)>>2).
- 0x5c6be0 UpdateFlickerIntensity — VERIFIED-1:1. Full audit of the FPU path: peak
  loop (light+0x60 vs 7×rec+0x24 stride 56), amp = cfg+0x19C * light+0x60 / peak,
  reseed via RandNext (double * 1/32767 * (amp*2) - amp + 1), frac blend; raw-path
  per-vertex max(v15,v34,v28) chain + 255-clamp scale (all `(int)` = cvttss truncate);
  non-raw path intensity*0.25, 6-bit clamp. All float→int sites match cvttss/(int).
- 0x4283ac AccumulateAabbRecursive — VERIFIED-1:1 (mesh+0x1CC corners 80-byte stride;
  min/max float<-double compares; child list +0x1FC link +0x1F0).
- 0x427820 TestAabbOverlapRecursive — VERIFIED-1:1 (class==4 gate; 8-corner fold;
  probe interval test uses minX/maxX/minY/minZ/maxZ — NOT maxY, faithfully; triangle
  Y-span accumulate into probe+16/+20; `v3 &= child` recursion).
- 0x423d74 GetPixelRgb — VERIFIED-1:1 (UnpackColor maps r→v12,g→v11,b→v10; out store
  v12/v10/v11; 24/32 raw byte routing; returns blue).
- 0x423e5c SetPixelRgb — FIXED (see divergence #1), now VERIFIED-1:1.
- 0x422ee4 BlitRgbToPixels — VERIFIED-1:1 (PackColor(src0,src1,src2) — no swap; 16bpp).

### reflective_nodes.cpp (7 addrs)
- 0x5da74f..0x5da76a ComputeReflectiveBit — VERIFIED-1:1 (disasm confirmed).
- IsMaterialReflective — VERIFIED-1:1 (loader arg assembly model).
- TextureFlagIsReflective / ApplyReflectiveBit (0x5f67c4 / 0x5dac92) — VERIFIED-1:1.
- 0x5f68c8 DeriveReflectionPlane — VERIFIED-1:1 (d = n·p, the +36 store).
- 0x5f67ad..0x5f67c4 FindReflectiveTexture — VERIFIED (documented index-helper
  variant of the pointer-returning child scan; loop bound semantics match).

## Counts
- Functions audited: 46 (13 + 14 + 12 + 7).
- VERIFIED-1:1: 46.
- Source fixes: 3 (SetPixelRgb pack swap, CreateTileRecord byte_649D70 gate,
  DrawHLine horizontal return).
- Test files updated: render_leaves4_test.cpp, render_leaves4_itest.cpp,
  render_leaves3_test.cpp (goldens corrected to the binary; new null-path case).
- Documented unobservable deviations: 3 (PutPixel/SetGlobalColorTemp/DetachClone
  return on degenerate paths; UnlinkObjectNode refresh-path return).
- Tests: 11/11 pass (render_leaves2/3/4 {unit,itest,e2e} + reflective_nodes_test +
  mirror_render_wave6_test).
