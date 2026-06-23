# render_06 — Full-tree 1:1 hardening sweep (MCP live)

Chunk = 22 `.cpp` files under `src/render/` (shapebank/shape*/skeleton*/sky*/snow*/sprite_scale/
sun_state/surface*/terrain). Every `gilde.exe 0xADDR` function was decompiled AND disassembled and
diffed line-for-line against the binary (disasm = reference of record). Work was fanned out across
10 parallel agents; per-cluster detail (addresses, before/after, byte evidence) lives in
`render_06_cluster1.md` … `render_06_cluster10.md`. This file is the roll-up.

No git commands were run. build/ untouched except `cmake --build build --target <test> -j`.
Only chunk `.cpp`/`.h` files + their tests + these progress files were edited.

---

## Totals

- Provenanced functions/blocks reviewed: **~80**
- **VERIFIED-1:1 (no churn): ~62**
- **FIXED (corrected to binary): 22 fix-sites across 18 functions** (+ goldens fixed where they
  encoded wrong behavior)
- **BOUNDARY (rule 3-5 GPU/DDraw swap, or leaf data genuinely out of the resolved view): ~16**
- All chunk test targets build clean and pass.

---

## Cluster 1 — shapebank.cpp / shape_blit.cpp / shape.cpp / shape_showbank_misc_recon.cpp
VERIFIED-1:1: AddShape (0x5D8330), AppendShape (0x5D843C), BlitColored16 (0x5D7164, luma
`r*0.2+b*0.59+g*0.2`, weights byte-verified dbl_629478=0.2/dbl_629480=0.59), Convert8To16Indexed
(0x43768C), DecodeRle (0x5D70CC), Velocity_Apply/ShowShapeFromBank (0x5d883c).
**FIXED:** RemoveShape (0x5D84F4) — binary zeroes `TableSlot(OLD count)` then decrements
(disasm 0x5d8592-0x5d859e); recon decremented first then zeroed `TableSlot(count-1)` (wrong index +
order), clobbering the just-compacted slot. Tests: render_shape_blit_test, shape_showbank_misc_recon_test,
render_camera_test pass.

## Cluster 2 — shape_convert16.cpp / shape_recon_cluster.cpp
VERIFIED-1:1 (16): ShapeConvertRgbTo16 (0x5d7c0c), ShapeConvert8To16 (0x5d7924), ShapeBankConvertNew
(0x5d80a8), color-mask formula word_1406944 (0x5d4ad4), ColorPack/Unpack/NotEqual/SetRgb, BuildLightTable
(0x5D49A0), ClassifyType (0x41F4F4 "SHAPBANK"), SetPalette (0x5D8294), SetSequenceData (0x5D8F54),
GrabBit16 (0x5D5908), GrabBit16NoRle (0x5D5E7C), GrabByDepth (0x5D68C4), and slot/anim helpers.
**FIXED (3 fns):** GrabBit24 (0x5D4C20) + GrabBit24NoRle (0x5D547C) — ColorSetRgb anchor-writeback arg
order (dl=R,cl=B,bl=G → memory [R,G,B]), 4 call sites, disasm 0x5d5223 etc.; new golden
`Bit24AnchorWritebackByteOrder`. GrabBit8 (0x5D6160) — Pass-2 mask scan reads/zeroes a5[row] only,
never re-indexed by column (a faithful repro of an original bug, disasm 0x5d63d1-0x5d6461).
BOUNDARY: ShapeBankConvertNew per-shape free (original's free is dead code → leaks; recon frees temp,
blob output identical). Tests: shape_convert16_test, shape_recon_cluster_test (+1 new), e2e — pass.

## Cluster 3 — skeleton.cpp / skeleton_pose.cpp
VERIFIED-1:1 (5): AdvanceFrameIndex (0x5ccf18), AccumulateBoneMatrices (0x5c8eb4), ComputeBoneWorldMatrix
(0x5c8fac), RotateVectorWithFrame (0x5c8ab4), and the non-skinned/non-morph scopes of TransformMeshVertices
(0x5c953c) / ComputeMeshVertexLightingNonSkinned (0x5c9054).
**FIXED:** InterpolateBoneFrame (0x5cbc10) — 3 x87 precision sites: lead ratio, trail ratio, and the
rotation dot-product are each rounded to 32-bit float (`fstp`) before the next op; recon kept them double.
AdvanceTrackPhase (0x5cd1d8 derived helper) — clamp and loop legs now set `phase = dur-1`
(`phase -= b+phase-dur+1` collapses to `dur-1-b`); misleading "byte-for-byte" doc corrected (the true
1:1 driver is in skeleton_pose_driver.cpp).
BOUNDARY: morph branch + skinned lighting branch (leaf calls out of resolved view).
Tests: render_skeleton_test, render_skeleton_pose_test (+2 e2e) pass.

## Cluster 4 — skeleton_pose_driver.cpp (VIBE_Anim_UpdateSkeletonPose 0x5cd1d8, ~1700 bytes)
**FIXED (2 control-flow divergences, both confirmed in disasm):**
- 0x5ce419: veg-cache search gate was INVERTED — binary searches only when `vegCacheLayer != 0`
  (`cmp [v180],0 / jz`); recon had `== 0`.
- 0x5ce93b: morph reverse-leg dispatch keyed on `toFrame (+8)` not phase (`cmp [esi+8],0 / jle`);
  added the missing fromFrame==1 clamp-finish (0x5ce954) and step-back (0x5cea19) branches.
2 regression tests added (MorphReverseClampToFrameGate, VegCacheGateSkipsSearchWhenZero).
VERIFIED-1:1: ~30 cited ranges (+0x6D mode-byte leg selectors, ConvertX truncation float→int,
`(flags<<6)>>7` reverse-sign extract, settle `dur/2` integer div, DecRepeat 2→1 idiom, morph latch/free).
BOUNDARY (rule 8, not faked): 88-byte morph keyframe durations, per-track phase rate, Catmull-Rom morph
eval + scene-graph leaves, defensive while(1) guard. 12 tests / 49 checks pass.

## Cluster 5 — skycolor_recon.cpp / sky.cpp / sky_dome.cpp
VERIFIED-1:1 (4): InterpolateBand (0x5b80ac), StoreBandColors (0x5b83f0, 12 stores), ScaledBlendAlpha
alpha (0x43f460), BuildDomeMesh (0x5ef980). All 10 float constants re-verified via get_bytes.
**FIXED (4 x87 precision sites):** SetTimeOfDay (0x5b83b0) — `fistp` low-dword-only with HIDWORD=0
(unsigned 32→64 widen), not signed i64. BlendBandLighting (0x5b85e4) — R/B lerps float-rounded
(`fstp`) before scale but G multiplied straight from 80-bit register; luma uses unrounded 80-bit channels.
BlendAmbientFog (0x5b8b04) — fog near/far `(B-A)*frac+A` kept in x87 register, single `fstp` round
before scale (confirmed (b-a)*t+a form, NOT a*(1-t)+b*t). BuildFogScratch (0x5b85e4 inner) — `(1-t)`
weight `fstp`'d to a 4-byte float and reloaded.
BOUNDARY: ClearViewport/ClearRect (0x5dd464/0x434728) — DirectDraw clear → sanctioned SDL/Vulkan fill.
Goldens already correct (fixes only move last-ULP cases). Tests: skycolor_recon_test, sky_render_test pass.

## Cluster 6 — snow.cpp / snow_recon.cpp / snow_update.cpp
VERIFIED-1:1 (9): SeedFlakes (0x42a014, six RandNext/flake exact order+math), SnowRenderStepHeader
(0x42b5c9..648), SnowBuildQuads (0x42b689..7c8, all 3 vertex bit patterns: rhw=1.0, diffuse=0x50646464,
specular=0, x/y/tu/tv byte-verified), SnowResetSceneTexTransparency (0x42a2cc), CoverageThreshold
(0x42b3fc — `div ecx` divisor recovered from disasm as 5, Hex-Rays showed uninit v6), Accumulator/Coverage
helpers, BatchLimit. dbl @0x61189C = 255.0 confirmed.
**FIXED:** SnowUpdateFlake (0x42a644) — full 1:1 rewrite from disasm: made stateful
(SnowSystem.prevAnchor/prevEye = a1+112/+128), restored per-system wind (sysVelX/sysVelZ = a1+68/+72),
fixed Euler/drift inputs (prevAnchor-camAnchor / prevEye-camEye), gravity indices (-m[3]/-m[4]/-m[5]),
drift/offset→axis pairing. Camera matrix column mapping verified against the FPU disasm (base v3+396).
BOUNDARY: DDraw lock/copy/unlock around masked-copy core (rule 3). Tests: snow_recon_test, snow_render_test pass.

## Cluster 7 — sprite_scale.cpp / sun_state.cpp
VERIFIED-1:1: SetSunDirection (0x42DC40), EnableSun (0x42DC5C), ResetGlobalState (0x5C8964),
BlitScaled16 (0x5D72F0, 8.8 fixed-point), BlitRleScaled (0x5D6A08), EncodeSpriteDrawFlags (0x559D60),
3 billboard projection arms (0x5AC970: distSq order, depth-fade `min(255)` then `255-t`, ConvertX
truncate-to-byte @+0x4F, byteOut `>>2`), effect-tint arm (0x5ACAB0).
**FIXED (4):** BlitRleLightTable (0x5D6D74) — light-table prologue runs Y-clip only, no X-clip
(disasm 0x5d6dc6); shared impl wrongly applied X-clip. ShowFromBankScaled (0x5D86C4) + ShowFromBank
(0x5D861C) — missing `byte_140694B` high-colour gate (cmp/jnz 0x5d8704/0x5d864c) added as a param.
BillboardQuadVisibilityPass (0x5ACAE0) — winding test keeps both products 80-bit on x87 (`fcompp`);
recon rounded each to float early → double.
BOUNDARY: ShowFromBankScaled clip-extent recompute to cross-module globals (HIWORD dword_64A1A2 /
word_64A1A6). Tests: render_sprite_scale_test, render_billboard_project_test, sun_daycycle_test (+i/e2e) pass.

## Cluster 8 — surface.cpp / surface_blit.cpp
VERIFIED-1:1 (11): Create (0x42311c), Destroy (0x4234b0), Clone (0x423c14), GetPixelRgb (0x423d74),
DrawHLine (0x423ffc), DrawLine Bresenham (0x424044, full sign/error/step/special-case diff),
DrawRectOutline (0x4242d4), ColorFill zero path (0x423b6c), 3 Paintbox dispatchers (0x41EF40/0x41EFC4/0x41F00C).
**FIXED (2 + 2 goldens):** SetPixelRgb (0x423e5c) 24bpp address — row term `widthPx*y` is NOT scaled by
bytespp (disasm 0x423f32-0x423f70, `imul edx,esi`); recon used `row*bytespp+col`. This makes Set/Get
genuinely asymmetric (Get DOES scale, 0x423e11) — a real binary quirk, now reproduced + 2 goldens fixed
(SetPixel24Bgr, DrawSaveReloadRoundtrip24). GetCaps (0x423620) — returns FALSE when ddSurface(+0x20) null
(`cmp [eax+0x20],0; jnz`), leaving *outCaps untouched; recon returned s->caps/true.
DOCUMENTED non-1:1: 8bpp GetPixelRgb copies uninitialized stack in the original (indeterminate — cannot
repro deterministically); ColorFill colored path is a codebase extension (r=g=b=0 path is byte-faithful).
Tests: render_surface_test, render_paintbox_test (+2 e2e) pass.

## Cluster 9 — surface_present.cpp / surface_stretch.cpp
VERIFIED-1:1 (8): BeginFrameLock (0x434508), AcquireBackBuffer (0x4345D4), CopyRegionRgb (0x423050),
StretchSurface8 (0x435D88), StretchAverage16 (0x435E00, RGB565 mask blend), BlitConvertDispatch (0x437980),
BlitThumbnailToSurface (0x56D6D4), LockSurfaceWait flag math (0x434468).
**FIXED (5 + goldens):** Convert24To16 (0x437814) — channel order src +0→R,+1→G,+2→B (disasm
0x43790e..0x437954); recon had R=s[2],B=s[0]; 2 goldens fixed. StretchSurface8Up (0x436488) — read/write
inverted, loop bounds = source dims; body rewritten, proto reordered (dst,src), dispatch+golden fixed.
StretchSurfaceDispatch (0x437530) — gate-fail returns `(u8)src.bpp` (al passthrough), not 0; golden fixed.
UnlockBackBuffer (0x434680) — GDI/Lock-copy modes return the MODE byte (`al=byte_762721 & 0xFF`), not
status; golden fixed. Lock(Wait)Surface (0x4343E4/0x434468) — `div ebx` is unsigned, recon used signed.
BOUNDARY: CopySurfacePixels (0x5DE87C) DDraw lock/desc/recursion; ReportDDrawError (0x42E4EC) DDraw SDK
string table. Tests: surface_present_test, surface_stretch_test (+downstream text_raster/thumbnail/paintbox,
+i/e2e) — 10/10 suites pass.

## Cluster 10 — terrain.cpp
**FIXED → VERIFIED-1:1:** TileIsUniform (0x5bbbf4) — the reference accumulator lives in `dl` (signed
byte); the "no reference yet" sentinel `0xFF` aliases a real terrain-type byte 0xFF. Recon modeled `ref`
as `int` seeded from `(u8)t`, destroying the collision. Fixed to `signed char ref` + `signed char` type
byte so 0xFF == -1 sentinel exactly like `dl` (disasm 0x5bbc04 `mov dl,0FFh`, 0x5bbc38 `cmp dl,0FFh`,
0x5bbc40/0x5bbc63 byte loads/compares). Control flow, loop bounds, index math `(mask&y)*size+(x&mask)`
already matched. New golden TileUniform0xFFSentinelCollision. Test: render_terrain_test 1/1 pass.

---

## Notes for the orchestrator
- Two agents observed PRE-EXISTING compile errors in files OUTSIDE this chunk and did NOT touch them:
  `src/world/office_forms.cpp` (`kTortureCase2Button`) and a transient truncated `history_text_pass.cpp.o`
  artifact. The full-lib link may break until those owners fix them; all render_06 test binaries compile and
  pass in isolation.
- progress/INDEX.md intentionally not edited (per ABSOLUTE RULES).
